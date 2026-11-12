// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "spatial_inventory.h"

#include <algorithm>
#include <array>
#include <optional>
#include <utility>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/templates/hash_set.hpp>

namespace godot {

namespace {

constexpr int64_t MAX_CELLS = 1024 * 1024;
constexpr int64_t SNAPSHOT_VERSION = 1;
constexpr uint32_t DENSE_BATCH_CELLS = 4096;
constexpr uint32_t LARGE_SEARCH_FOOTPRINT = 4096;
constexpr uint32_t LARGE_SEARCH_GRID = 65536;

// Checks that both grid dimensions are positive.
bool is_positive_size(const Vector2i &p_size) {
	return p_size.x > 0 && p_size.y > 0;
}

// Checks the dimensions and total cell limit.
bool is_valid_grid_size(const Vector2i &p_size) {
	return is_positive_size(p_size) && int64_t(p_size.x) * p_size.y <= MAX_CELLS;
}

// Returns the footprint dimensions in the requested orientation.
Vector2i oriented_size(const Vector2i &p_footprint, bool p_rotated) {
	return p_rotated ? Vector2i(p_footprint.y, p_footprint.x) : p_footprint;
}

} // namespace

Vector2i SpatialInventory::Placement::extent() const {
	return oriented_size(footprint, rotated);
}

bool SpatialInventory::Placement::same_geometry(const Placement &p_other) const {
	return footprint == p_other.footprint && origin == p_other.origin && rotated == p_other.rotated;
}

Dictionary SpatialInventory::Placement::to_dictionary() const {
	Dictionary result;
	result["id"] = id;
	result["footprint"] = footprint;
	result["origin"] = origin;
	result["rotated"] = rotated;
	return result;
}

bool SpatialInventory::Placement::from_dictionary(const Dictionary &p_fields, Placement &r_placement) {
	const Variant id = p_fields.get("id", Variant());
	const Variant footprint = p_fields.get("footprint", Variant());
	const Variant origin = p_fields.get("origin", Variant());
	const Variant rotated = p_fields.get("rotated", Variant());
	if ((id.get_type() != Variant::STRING_NAME && id.get_type() != Variant::STRING) ||
			footprint.get_type() != Variant::VECTOR2I || origin.get_type() != Variant::VECTOR2I ||
			rotated.get_type() != Variant::BOOL) {
		return false;
	}
	r_placement = Placement{ StringName(id), Vector2i(footprint), Vector2i(origin), bool(rotated) };
	return !r_placement.id.is_empty();
}

SpatialInventory::GridState::GridState(const Vector2i &p_size) :
		size(p_size),
		cells(size_t(p_size.x) * p_size.y, EMPTY_SLOT) {
}

SpatialInventory::Slot SpatialInventory::GridState::slot_for(const StringName &p_id) const {
	const Slot *slot = slots_by_id.getptr(p_id);
	return slot ? *slot : EMPTY_SLOT;
}

SpatialInventory::Placement &SpatialInventory::GridState::placement_at(Slot p_slot) {
	return placements[p_slot - 1];
}

const SpatialInventory::Placement &SpatialInventory::GridState::placement_at(Slot p_slot) const {
	return placements[p_slot - 1];
}

Error SpatialInventory::GridState::check_region_bounds(const Vector2i &p_origin, const Vector2i &p_extent) const {
	if (cells.empty()) {
		return ERR_UNCONFIGURED;
	}
	if (!is_positive_size(p_extent)) {
		return ERR_INVALID_PARAMETER;
	}
	if (p_origin.x < 0 || p_origin.y < 0 || p_origin.x > size.x - p_extent.x || p_origin.y > size.y - p_extent.y) {
		return ERR_PARAMETER_RANGE_ERROR;
	}
	return OK;
}

Error SpatialInventory::GridState::check_region(const Vector2i &p_origin, const Vector2i &p_extent, Slot p_ignored_slot) const {
	const Error bounds_error = check_region_bounds(p_origin, p_extent);
	if (bounds_error != OK) {
		return bounds_error;
	}
	return scan_region<false>(p_origin, p_extent, p_ignored_slot) == 0 ? OK : ERR_ALREADY_IN_USE;
}

template <bool FindNextOrigin, bool CountVisited>
int32_t SpatialInventory::GridState::scan_region(const Vector2i &p_origin, const Vector2i &p_extent, Slot p_ignored_slot, uint32_t *r_visited) const {
	for (int32_t y = p_origin.y; y < p_origin.y + p_extent.y; ++y) {
		const Slot *row = cells.data() + size_t(y) * size.x + p_origin.x;
		for (int32_t x = 0; x < p_extent.x; ++x) {
			const Slot slot = row[x];
			if (slot == EMPTY_SLOT || slot == p_ignored_slot) {
				continue;
			}
			if constexpr (FindNextOrigin) {
				if constexpr (CountVisited) {
					*r_visited += uint32_t((y - p_origin.y) * p_extent.x + x + 1);
				}
				const int32_t next_column = p_origin.x + x + 1;
				if (next_column == size.x || row[x + 1] != slot) {
					return next_column;
				}
				// Every intermediate origin at this y still overlaps the blocking item.
				const Placement &blocker = placement_at(slot);
				return blocker.origin.x + blocker.extent().x;
			}
			return 1;
		}
	}
	return 0;
}

template <bool TrackWork>
Vector2i SpatialInventory::GridState::find_fit_from(const Vector2i &p_extent, const Vector2i &p_start, const Vector2i &p_last) const {
	uint32_t visited = 0;
	int32_t x = p_start.x;
	for (int32_t y = p_start.y; y <= p_last.y; ++y) {
		while (x <= p_last.x) {
			const Vector2i origin(x, y);
			const int32_t next_column = scan_region<true, TrackWork>(origin, p_extent, EMPTY_SLOT, &visited);
			if (next_column == 0) {
				return origin;
			}
			x = next_column;
			if constexpr (TrackWork) {
				// Keep early hits cheap. Switch after repeated reads exceed a full-grid pass.
				if (visited > cells.size()) {
					return find_fit_with_column_counts(p_extent, Vector2i(x, y));
				}
			}
		}
		x = 0;
	}
	return Vector2i(-1, -1);
}

Vector2i SpatialInventory::GridState::find_fit_with_column_counts(const Vector2i &p_extent, const Vector2i &p_start) const {
	const int32_t last_row = size.y - p_extent.y;
	std::vector<int32_t> occupied_by_column(size.x, 0);
	for (int32_t y = p_start.y; y < p_start.y + p_extent.y; ++y) {
		const Slot *row = cells.data() + size_t(y) * size.x;
		for (int32_t x = 0; x < size.x; ++x) {
			occupied_by_column[x] += row[x] != EMPTY_SLOT;
		}
	}

	// A zero count marks a column free across the whole vertical footprint.
	for (int32_t y = p_start.y; y <= last_row; ++y) {
		int32_t free_columns = 0;
		for (int32_t x = y == p_start.y ? p_start.x : 0; x < size.x; ++x) {
			free_columns = occupied_by_column[x] == 0 ? free_columns + 1 : 0;
			if (free_columns == p_extent.x) {
				return Vector2i(x + 1 - p_extent.x, y);
			}
		}
		if (y == last_row) {
			break;
		}
		const Slot *leaving = cells.data() + size_t(y) * size.x;
		const Slot *entering = leaving + size_t(p_extent.y) * size.x;
		for (int32_t x = 0; x < size.x; ++x) {
			occupied_by_column[x] += int32_t(entering[x] != EMPTY_SLOT) - int32_t(leaving[x] != EMPTY_SLOT);
		}
	}
	return Vector2i(-1, -1);
}

SpatialInventory::Slot SpatialInventory::GridState::find_single_blocker(const Vector2i &p_origin, const Vector2i &p_extent) const {
	if (!is_positive_size(p_extent) ||
			p_origin.x < 0 || p_origin.y < 0 || p_origin.x > size.x - p_extent.x || p_origin.y > size.y - p_extent.y) {
		return EMPTY_SLOT;
	}

	Slot blocker = EMPTY_SLOT;
	for (int32_t y = p_origin.y; y < p_origin.y + p_extent.y; ++y) {
		const Slot *row = cells.data() + size_t(y) * size.x + p_origin.x;
		for (int32_t x = 0; x < p_extent.x; ++x) {
			const Slot slot = row[x];
			if (slot == EMPTY_SLOT || slot == blocker) {
				continue;
			}
			if (blocker != EMPTY_SLOT) {
				return EMPTY_SLOT;
			}
			blocker = slot;
		}
	}
	return blocker;
}

void SpatialInventory::GridState::fill_region(const Vector2i &p_origin, const Vector2i &p_extent, Slot p_slot) {
	for (int32_t y = p_origin.y; y < p_origin.y + p_extent.y; ++y) {
		Slot *row = cells.data() + size_t(y) * size.x + p_origin.x;
		std::fill_n(row, p_extent.x, p_slot);
	}
}

Error SpatialInventory::GridState::check_insert(const StringName &p_id, const Vector2i &p_origin, const Vector2i &p_extent) const {
	if (p_id.is_empty()) {
		return ERR_INVALID_PARAMETER;
	}
	if (slots_by_id.has(p_id)) {
		return ERR_ALREADY_EXISTS;
	}
	return check_region(p_origin, p_extent);
}

void SpatialInventory::GridState::insert(const Placement &p_placement) {
	Slot slot = free_slot;
	if (slot == EMPTY_SLOT) {
		placements.push_back(p_placement);
		slot = Slot(placements.size());
	} else {
		Placement &reused = placement_at(slot);
		free_slot = reused.next_free;
		reused = p_placement;
	}
	slots_by_id.insert(p_placement.id, slot);
	fill_region(p_placement.origin, p_placement.extent(), slot);
}

void SpatialInventory::GridState::erase(Slot p_slot) {
	Placement &placement = placement_at(p_slot);
	fill_region(placement.origin, placement.extent(), EMPTY_SLOT);
	slots_by_id.erase(placement.id);
	placement = Placement();
	placement.next_free = free_slot;
	free_slot = p_slot;
}

void SpatialInventory::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_placement_deltas_enabled", "enabled"), &SpatialInventory::set_placement_deltas_enabled);
	ClassDB::bind_method(D_METHOD("is_placement_deltas_enabled"), &SpatialInventory::is_placement_deltas_enabled);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("apply_placement_changes", "changes"), &SpatialInventory::apply_placement_changes);
	ClassDB::bind_method(D_METHOD("configure", "size"), &SpatialInventory::configure);
	ClassDB::bind_method(D_METHOD("get_grid_size"), &SpatialInventory::get_grid_size);
	ClassDB::bind_method(D_METHOD("get_placement_revision"), &SpatialInventory::get_placement_revision);
	ClassDB::bind_method(D_METHOD("try_place", "id", "footprint", "origin", "rotated"), &SpatialInventory::try_place, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("check_placement", "footprint", "origin", "rotated"), &SpatialInventory::check_placement, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("get_swap_target", "footprint", "origin", "rotated"), &SpatialInventory::get_swap_target, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("try_swap_in", "id", "footprint", "origin", "rotated", "expected_id"), &SpatialInventory::try_swap_in);
	ClassDB::bind_method(D_METHOD("check_move", "id", "origin", "rotated"), &SpatialInventory::check_move);
	ClassDB::bind_method(D_METHOD("try_move", "id", "origin", "rotated"), &SpatialInventory::try_move);
	ClassDB::bind_method(D_METHOD("check_transfer", "id", "destination", "origin", "rotated"), &SpatialInventory::check_transfer);
	ClassDB::bind_method(D_METHOD("try_transfer", "id", "destination", "origin", "rotated"), &SpatialInventory::try_transfer);
	ClassDB::bind_method(D_METHOD("try_resize_item", "id", "footprint"), &SpatialInventory::try_resize_item);
	ClassDB::bind_method(D_METHOD("remove", "id"), &SpatialInventory::remove);
	ClassDB::bind_method(D_METHOD("get_item_at", "cell"), &SpatialInventory::get_item_at);
	ClassDB::bind_method(D_METHOD("get_placement", "id"), &SpatialInventory::get_placement);
	ClassDB::bind_method(D_METHOD("get_placements"), &SpatialInventory::get_placements);
	ClassDB::bind_method(D_METHOD("find_first_fit", "footprint", "rotated", "start"), &SpatialInventory::find_first_fit, DEFVAL(false), DEFVAL(Vector2i()));
	ClassDB::bind_method(D_METHOD("export_snapshot"), &SpatialInventory::export_snapshot);
	ClassDB::bind_method(D_METHOD("import_snapshot", "snapshot"), &SpatialInventory::import_snapshot);
	ADD_SIGNAL(MethodInfo("changed"));
	ADD_SIGNAL(MethodInfo("placements_changed", PropertyInfo(Variant::ARRAY, "ids"), PropertyInfo(Variant::BOOL, "full_refresh")));
}

void SpatialInventory::set_placement_deltas_enabled(bool p_enabled) {
	placement_deltas_enabled = p_enabled;
}

bool SpatialInventory::is_placement_deltas_enabled() const {
	return placement_deltas_enabled;
}

void SpatialInventory::notify_change(const StringName *p_id, const StringName *p_other_id) {
	if (!placement_deltas_enabled) {
		emit_signal(changed_signal);
		return;
	}

	// Keep the grid alive through both signals even if a listener releases its reference.
	const Ref<SpatialInventory> keep_alive(this);
	Array ids;
	if (p_id) {
		ids.append(*p_id);
	}
	if (p_other_id) {
		ids.append(*p_other_id);
	}
	ids.make_read_only();
	emit_signal(placements_changed_signal, ids, p_id == nullptr);
	emit_signal(changed_signal);
}

Error SpatialInventory::apply_placement_changes(const Array &p_changes) {
	struct Change {
		StringName id;
		Slot before_slot;
		std::optional<Placement> after;
	};
	struct GridChanges {
		Ref<SpatialInventory> inventory;
		HashSet<StringName> ids;
		HashSet<Slot> vacated_slots;
		std::vector<Change> entries;
		uint32_t planned_cell_count = 0;
	};

	std::vector<GridChanges> grids;
	HashMap<SpatialInventory *, size_t> grid_indices;
	for (int64_t i = 0; i < p_changes.size(); ++i) {
		const Variant row = p_changes[i];
		if (row.get_type() != Variant::DICTIONARY) {
			return ERR_INVALID_PARAMETER;
		}
		const Dictionary fields = row;
		const Ref<SpatialInventory> inventory(fields.get("inventory", Variant()));
		const Variant id_value = fields.get("id", Variant());
		const Variant before_value = fields.get("before", Variant());
		const Variant after_value = fields.get("after", Variant());
		if (inventory.is_null() ||
				(id_value.get_type() != Variant::STRING_NAME && id_value.get_type() != Variant::STRING) ||
				before_value.get_type() != Variant::DICTIONARY || after_value.get_type() != Variant::DICTIONARY) {
			return ERR_INVALID_PARAMETER;
		}

		const StringName id = id_value;
		if (id.is_empty()) {
			return ERR_INVALID_PARAMETER;
		}
		const Dictionary before_fields = before_value;
		const Dictionary after_fields = after_value;
		Placement before;
		std::optional<Placement> after;
		const bool expects_placement = !before_fields.is_empty();
		if (expects_placement && (!Placement::from_dictionary(before_fields, before) || before.id != id)) {
			return ERR_INVALID_PARAMETER;
		}
		if (!after_fields.is_empty()) {
			after.emplace();
			if (!Placement::from_dictionary(after_fields, *after) || after->id != id) {
				return ERR_INVALID_PARAMETER;
			}
		}

		const size_t *grid_index = grid_indices.getptr(inventory.ptr());
		if (!grid_index) {
			grid_indices.insert(inventory.ptr(), grids.size());
			grids.emplace_back();
			grids.back().inventory = inventory;
			grid_index = grid_indices.getptr(inventory.ptr());
		}
		GridChanges &grid = grids[*grid_index];
		if (grid.ids.has(id)) {
			return ERR_ALREADY_EXISTS;
		}
		grid.ids.insert(id);

		const GridState &state = inventory->state;
		const Slot before_slot = state.slot_for(id);
		const bool is_placed = before_slot != EMPTY_SLOT;
		if (expects_placement != is_placed || (is_placed && !state.placement_at(before_slot).same_geometry(before))) {
			return ERR_BUSY;
		}

		if (is_placed == after.has_value() && (!is_placed || before.same_geometry(*after))) {
			continue;
		}
		if (is_placed) {
			grid.vacated_slots.insert(before_slot);
		}
		if (after && grid.planned_cell_count < DENSE_BATCH_CELLS && is_positive_size(after->footprint)) {
			const int64_t area = int64_t(after->footprint.x) * after->footprint.y;
			grid.planned_cell_count += uint32_t(std::min<int64_t>(area, DENSE_BATCH_CELLS));
		}
		grid.entries.push_back(Change{ id, before_slot, std::move(after) });
	}

	// Validate the proposed final occupancy without copying or writing the live grids.
	for (const GridChanges &grid : grids) {
		const GridState &state = grid.inventory->state;
		HashSet<uint32_t> sparse_cells;
		std::array<uint64_t, 4> small_cells{};
		std::vector<uint64_t> dense_cells;
		uint64_t *planned_bits = nullptr;
		const size_t words = (state.cells.size() + 63) / 64;
		if (words <= small_cells.size()) {
			planned_bits = small_cells.data();
		} else if (grid.planned_cell_count >= DENSE_BATCH_CELLS) {
			dense_cells.resize(words, 0);
			planned_bits = dense_cells.data();
		}
		Slot checked_occupant = EMPTY_SLOT;
		for (const Change &change : grid.entries) {
			if (!change.after) {
				continue;
			}
			const Placement &placement = *change.after;
			const Vector2i extent = placement.extent();
			const Error bounds_error = state.check_region_bounds(placement.origin, extent);
			if (bounds_error != OK) {
				return bounds_error;
			}
			for (int32_t y = placement.origin.y; y < placement.origin.y + extent.y; ++y) {
				uint32_t cell = uint32_t(y * state.size.x + placement.origin.x);
				for (int32_t x = 0; x < extent.x; ++x, ++cell) {
					const Slot occupant = state.cells[cell];
					if (occupant != EMPTY_SLOT && occupant != checked_occupant) {
						if (!grid.vacated_slots.has(occupant)) {
							return ERR_ALREADY_IN_USE;
						}
						checked_occupant = occupant;
					}
					if (planned_bits) {
						uint64_t &word = planned_bits[cell / 64];
						const uint64_t mask = uint64_t(1) << (cell % 64);
						if (word & mask) {
							return ERR_ALREADY_IN_USE;
						}
						word |= mask;
					} else {
						if (sparse_cells.has(cell)) {
							return ERR_ALREADY_IN_USE;
						}
						sparse_cells.insert(cell);
					}
				}
			}
		}
	}

	// Vacate every old footprint before installing any replacement in that grid.
	for (GridChanges &grid : grids) {
		GridState &state = grid.inventory->state;
		for (const Change &change : grid.entries) {
			if (change.before_slot == EMPTY_SLOT) {
				continue;
			}
			if (change.after) {
				const Placement &before = state.placement_at(change.before_slot);
				state.fill_region(before.origin, before.extent(), EMPTY_SLOT);
			} else {
				state.erase(change.before_slot);
			}
		}
		for (const Change &change : grid.entries) {
			if (!change.after) {
				continue;
			}
			if (change.before_slot == EMPTY_SLOT) {
				state.insert(*change.after);
			} else {
				state.placement_at(change.before_slot) = *change.after;
				state.fill_region(change.after->origin, change.after->extent(), change.before_slot);
			}
		}
		if (!grid.entries.empty()) {
			++grid.inventory->placement_revision;
		}
	}

	// A listener on any grid sees the full transaction committed.
	for (const GridChanges &grid : grids) {
		if (grid.entries.empty()) {
			continue;
		}
		const Ref<SpatialInventory> &inventory = grid.inventory;
		if (inventory->placement_deltas_enabled) {
			Array ids;
			ids.resize(grid.entries.size());
			for (size_t i = 0; i < grid.entries.size(); ++i) {
				ids[i] = grid.entries[i].id;
			}
			ids.make_read_only();
			inventory->emit_signal(inventory->placements_changed_signal, ids, false);
		}
		inventory->emit_signal(inventory->changed_signal);
	}
	return OK;
}

Error SpatialInventory::configure(const Vector2i &p_size) {
	if (!is_valid_grid_size(p_size)) {
		return ERR_INVALID_PARAMETER;
	}
	if (p_size == state.size) {
		return OK;
	}
	if (!state.slots_by_id.is_empty()) {
		return ERR_BUSY;
	}
	state = GridState(p_size);
	++placement_revision;
	notify_change();
	return OK;
}

Vector2i SpatialInventory::get_grid_size() const {
	return state.size;
}

uint64_t SpatialInventory::get_placement_revision() const {
	return placement_revision;
}

Error SpatialInventory::try_place(const StringName &p_id, const Vector2i &p_footprint, const Vector2i &p_origin, bool p_rotated) {
	const Error error = state.check_insert(p_id, p_origin, oriented_size(p_footprint, p_rotated));
	if (error != OK) {
		return error;
	}
	state.insert(Placement{ p_id, p_footprint, p_origin, p_rotated });
	++placement_revision;
	notify_change(&p_id);
	return OK;
}

Error SpatialInventory::check_placement(const Vector2i &p_footprint, const Vector2i &p_origin, bool p_rotated) const {
	return state.check_region(p_origin, oriented_size(p_footprint, p_rotated));
}

StringName SpatialInventory::get_swap_target(const Vector2i &p_footprint, const Vector2i &p_origin, bool p_rotated) const {
	const Slot slot = state.find_single_blocker(p_origin, oriented_size(p_footprint, p_rotated));
	return slot == EMPTY_SLOT ? StringName() : state.placement_at(slot).id;
}

Dictionary SpatialInventory::try_swap_in(const StringName &p_id, const Vector2i &p_footprint, const Vector2i &p_origin, bool p_rotated, const StringName &p_expected_id) {
	if (p_id.is_empty() || state.slots_by_id.has(p_id)) {
		return Dictionary();
	}
	const Slot slot = state.find_single_blocker(p_origin, oriented_size(p_footprint, p_rotated));
	if (slot == EMPTY_SLOT || state.placement_at(slot).id != p_expected_id) {
		return Dictionary();
	}

	const Dictionary displaced = state.placement_at(slot).to_dictionary();
	state.erase(slot);
	state.insert(Placement{ p_id, p_footprint, p_origin, p_rotated });
	++placement_revision;
	notify_change(&p_id, &p_expected_id);
	return displaced;
}

Error SpatialInventory::check_move(const StringName &p_id, const Vector2i &p_origin, bool p_rotated) const {
	const Slot slot = state.slot_for(p_id);
	if (slot == EMPTY_SLOT) {
		return ERR_DOES_NOT_EXIST;
	}
	const Vector2i &footprint = state.placement_at(slot).footprint;
	return state.check_region(p_origin, oriented_size(footprint, p_rotated), slot);
}

Error SpatialInventory::update_placement(Slot p_slot, const Vector2i &p_footprint, const Vector2i &p_origin, bool p_rotated) {
	Placement &placement = state.placement_at(p_slot);
	if (placement.footprint == p_footprint && placement.origin == p_origin && placement.rotated == p_rotated) {
		return OK;
	}

	const Vector2i extent = oriented_size(p_footprint, p_rotated);
	const Error error = state.check_region(p_origin, extent, p_slot);
	if (error != OK) {
		return error;
	}

	state.fill_region(placement.origin, placement.extent(), EMPTY_SLOT);
	state.fill_region(p_origin, extent, p_slot);
	placement.footprint = p_footprint;
	placement.origin = p_origin;
	placement.rotated = p_rotated;
	++placement_revision;
	notify_change(&placement.id);
	return OK;
}

Error SpatialInventory::try_move(const StringName &p_id, const Vector2i &p_origin, bool p_rotated) {
	const Slot slot = state.slot_for(p_id);
	if (slot == EMPTY_SLOT) {
		return ERR_DOES_NOT_EXIST;
	}
	return update_placement(slot, state.placement_at(slot).footprint, p_origin, p_rotated);
}

Error SpatialInventory::check_transfer(const StringName &p_id, const Ref<SpatialInventory> &p_destination, const Vector2i &p_origin, bool p_rotated) const {
	if (p_destination.is_null()) {
		return ERR_INVALID_PARAMETER;
	}
	if (p_destination.ptr() == this) {
		return check_move(p_id, p_origin, p_rotated);
	}
	const Slot slot = state.slot_for(p_id);
	if (slot == EMPTY_SLOT) {
		return ERR_DOES_NOT_EXIST;
	}
	const Vector2i &footprint = state.placement_at(slot).footprint;
	return p_destination->state.check_insert(p_id, p_origin, oriented_size(footprint, p_rotated));
}

Error SpatialInventory::try_transfer(const StringName &p_id, Ref<SpatialInventory> p_destination, const Vector2i &p_origin, bool p_rotated) {
	if (p_destination.is_null()) {
		return ERR_INVALID_PARAMETER;
	}
	if (p_destination.ptr() == this) {
		return try_move(p_id, p_origin, p_rotated);
	}
	const Slot slot = state.slot_for(p_id);
	if (slot == EMPTY_SLOT) {
		return ERR_DOES_NOT_EXIST;
	}
	const Vector2i &footprint = state.placement_at(slot).footprint;
	const Error error = p_destination->state.check_insert(p_id, p_origin, oriented_size(footprint, p_rotated));
	if (error != OK) {
		return error;
	}

	// Complete both mutations before notifying listeners.
	p_destination->state.insert(Placement{ p_id, footprint, p_origin, p_rotated });
	state.erase(slot);
	++placement_revision;
	++p_destination->placement_revision;
	notify_change(&p_id);
	p_destination->notify_change(&p_id);
	return OK;
}

Error SpatialInventory::try_resize_item(const StringName &p_id, const Vector2i &p_footprint) {
	const Slot slot = state.slot_for(p_id);
	if (slot == EMPTY_SLOT) {
		return ERR_DOES_NOT_EXIST;
	}
	const Placement &placement = state.placement_at(slot);
	return update_placement(slot, p_footprint, placement.origin, placement.rotated);
}

Error SpatialInventory::remove(const StringName &p_id) {
	const Slot slot = state.slot_for(p_id);
	if (slot == EMPTY_SLOT) {
		return ERR_DOES_NOT_EXIST;
	}
	state.erase(slot);
	++placement_revision;
	notify_change(&p_id);
	return OK;
}

StringName SpatialInventory::get_item_at(const Vector2i &p_cell) const {
	if (p_cell.x < 0 || p_cell.y < 0 || p_cell.x >= state.size.x || p_cell.y >= state.size.y) {
		return StringName();
	}
	const Slot slot = state.cells[size_t(p_cell.y) * state.size.x + p_cell.x];
	return slot == EMPTY_SLOT ? StringName() : state.placement_at(slot).id;
}

Dictionary SpatialInventory::get_placement(const StringName &p_id) const {
	const Slot slot = state.slot_for(p_id);
	return slot == EMPTY_SLOT ? Dictionary() : state.placement_at(slot).to_dictionary();
}

Array SpatialInventory::get_placements() const {
	Array result;
	result.resize(state.slots_by_id.size());
	int64_t index = 0;
	for (const KeyValue<StringName, Slot> &entry : state.slots_by_id) {
		result[index++] = state.placement_at(entry.value).to_dictionary();
	}
	return result;
}

Vector2i SpatialInventory::find_first_fit(const Vector2i &p_footprint, bool p_rotated, const Vector2i &p_start) const {
	const Vector2i extent = oriented_size(p_footprint, p_rotated);
	if (!is_positive_size(extent) || p_start.x < 0 || p_start.y < 0) {
		return Vector2i(-1, -1);
	}
	const Vector2i last_origin = state.size - extent;
	if (last_origin.x < 0 || p_start.y > last_origin.y) {
		return Vector2i(-1, -1);
	}
	if (state.cells.size() >= LARGE_SEARCH_GRID && uint32_t(extent.x * extent.y) >= LARGE_SEARCH_FOOTPRINT) {
		return state.find_fit_from<true>(extent, p_start, last_origin);
	}
	return state.find_fit_from<false>(extent, p_start, last_origin);
}

Dictionary SpatialInventory::export_snapshot() const {
	Dictionary result;
	result["version"] = SNAPSHOT_VERSION;
	result["grid_size"] = state.size;
	result["placements"] = get_placements();
	return result;
}

Error SpatialInventory::import_snapshot(const Dictionary &p_snapshot) {
	const Variant version = p_snapshot.get("version", Variant());
	const Variant grid_size = p_snapshot.get("grid_size", Variant());
	const Variant entries = p_snapshot.get("placements", Variant());
	if (version.get_type() != Variant::INT || int64_t(version) != SNAPSHOT_VERSION ||
			grid_size.get_type() != Variant::VECTOR2I || entries.get_type() != Variant::ARRAY) {
		return ERR_INVALID_PARAMETER;
	}

	const Vector2i size = grid_size;
	const Array placements = entries;
	if ((!is_valid_grid_size(size) && size != Vector2i()) ||
			placements.size() > int64_t(size.x) * size.y) {
		return ERR_INVALID_PARAMETER;
	}

	std::optional<GridState> candidate;
	std::vector<Slot> matching_slots;
	std::vector<uint64_t> seen_slots;
	if (size == state.size && placements.size() == state.slots_by_id.size()) {
		seen_slots.resize((state.placements.size() + 63) / 64, 0);
	} else {
		candidate.emplace(size);
	}
	for (int64_t i = 0; i < placements.size(); ++i) {
		const Variant entry = placements[i];
		if (entry.get_type() != Variant::DICTIONARY) {
			return ERR_INVALID_PARAMETER;
		}
		Placement placement;
		if (!Placement::from_dictionary(entry, placement)) {
			return ERR_INVALID_PARAMETER;
		}
		if (!candidate) {
			const Slot slot = state.slot_for(placement.id);
			if (slot != EMPTY_SLOT && state.placement_at(slot).same_geometry(placement)) {
				const size_t index = slot - 1;
				uint64_t &word = seen_slots[index / 64];
				const uint64_t mask = uint64_t(1) << (index % 64);
				if (word & mask) {
					return ERR_ALREADY_EXISTS;
				}
				word |= mask;
				matching_slots.push_back(slot);
				continue;
			}
			// Matching entries already passed geometry and uniqueness checks against the live grid.
			candidate.emplace(size);
			for (const Slot matched : matching_slots) {
				candidate->insert(state.placement_at(matched));
			}
		}
		const Error error = candidate->check_insert(placement.id, placement.origin, placement.extent());
		if (error != OK) {
			return error;
		}
		candidate->insert(placement);
	}

	if (candidate) {
		state = std::move(*candidate);
		++placement_revision;
		notify_change();
	}
	return OK;
}

} // namespace godot
