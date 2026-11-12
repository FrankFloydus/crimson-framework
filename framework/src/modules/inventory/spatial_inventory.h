// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <cstdint>
#include <vector>

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/vector2i.hpp>

namespace godot {

class SpatialInventory : public RefCounted {
	GDCLASS(SpatialInventory, RefCounted)

	using Slot = uint32_t;
	// Sentinel for an empty cell or absent slot.
	static constexpr Slot EMPTY_SLOT = 0;

	struct Placement {
		// Stable identifier for the item instance.
		StringName id;
		// Unrotated width and height in cells.
		Vector2i footprint;
		// Top-left cell of the placement.
		Vector2i origin;
		// Whether the placement swaps its footprint axes.
		bool rotated = false;
		// Next reusable slot in the free list.
		Slot next_free = EMPTY_SLOT;

		// Computes the footprint in its placed orientation.
		Vector2i extent() const;
		// Compares placement ID, footprint, origin, and rotation.
		bool same_geometry(const Placement &p_other) const;
		// Exports a placement as a native Godot dictionary.
		Dictionary to_dictionary() const;
		// Validates a placement dictionary and populates the record.
		static bool from_dictionary(const Dictionary &p_fields, Placement &r_placement);
	};

	struct GridState {
		// Inventory dimensions in cells.
		Vector2i size;
		// Occupancy array mapping each cell to its placement slot.
		std::vector<Slot> cells;
		// Placement records stored at slot indexes.
		std::vector<Placement> placements;
		// Lookup from stable item ID to its placement slot.
		HashMap<StringName, Slot> slots_by_id;
		// Head of the reusable placement slot list.
		Slot free_slot = EMPTY_SLOT;

		// Creates a grid with empty occupancy for the requested dimensions.
		explicit GridState(const Vector2i &p_size = Vector2i());
		// Looks up an item's placement slot.
		Slot slot_for(const StringName &p_id) const;
		// Accesses a placement record by slot index.
		Placement &placement_at(Slot p_slot);
		// Accesses a placement record by slot index.
		const Placement &placement_at(Slot p_slot) const;
		// Validates that a rectangle is fully inside the grid.
		Error check_region_bounds(const Vector2i &p_origin, const Vector2i &p_extent) const;
		// Checks that a rectangle is in bounds and free except for an optional ignored slot.
		Error check_region(const Vector2i &p_origin, const Vector2i &p_extent, Slot p_ignored_slot = EMPTY_SLOT) const;
		// Scans a rectangle for occupancy and optionally tracks visited cells.
		template <bool FindNextOrigin, bool CountVisited = false>
		int32_t scan_region(const Vector2i &p_origin, const Vector2i &p_extent, Slot p_ignored_slot, uint32_t *r_visited = nullptr) const;
		// Searches candidate origins from the supplied row-major position.
		template <bool TrackWork>
		Vector2i find_fit_from(const Vector2i &p_extent, const Vector2i &p_start, const Vector2i &p_last) const;
		// Searches large grids using counts across the active row window.
		Vector2i find_fit_with_column_counts(const Vector2i &p_extent, const Vector2i &p_start) const;
		// Returns the sole occupying placement when a rectangle has exactly one blocker.
		Slot find_single_blocker(const Vector2i &p_origin, const Vector2i &p_extent) const;
		// Writes one slot value into every cell of the rectangle.
		void fill_region(const Vector2i &p_origin, const Vector2i &p_extent, Slot p_slot);
		// Checks that the ID is unused and its proposed rectangle is free.
		Error check_insert(const StringName &p_id, const Vector2i &p_origin, const Vector2i &p_extent) const;
		// Adds a placement record and updates the occupancy and ID indexes.
		void insert(const Placement &p_placement);
		// Clears occupancy and releases the placement slot for reuse.
		void erase(Slot p_slot);
	};

	// Current grid occupancy and placement indexes.
	GridState state;
	// Signal emitted after the grid changes.
	const StringName changed_signal{ "changed" };
	// Optional signal carrying placement details.
	const StringName placements_changed_signal{ "placements_changed" };
	// Placement revision used to detect stale plans.
	uint64_t placement_revision = 0;
	// Whether per-item placement details are emitted.
	bool placement_deltas_enabled = false;

	// Emits change notifications after the grid has committed the edit.
	void notify_change(const StringName *p_id = nullptr, const StringName *p_other_id = nullptr);

	// Updates the existing item after validating its new rectangle.
	Error update_placement(Slot p_slot, const Vector2i &p_footprint, const Vector2i &p_origin, bool p_rotated);

protected:
	// Registers Godot methods, properties, and signals declared by this type.
	static void _bind_methods();

public:
	// Sets whether detailed placement change notifications are enabled.
	void set_placement_deltas_enabled(bool p_enabled);
	// Returns whether detailed placement change notifications are enabled.
	bool is_placement_deltas_enabled() const;
	// Validates and applies expected placement changes across all affected grids.
	static Error apply_placement_changes(const Array &p_changes);
	// Configures the dimensions of an empty inventory grid.
	Error configure(const Vector2i &p_size);
	// Returns the current dimensions of the grid in cells.
	Vector2i get_grid_size() const;
	// Returns the current revision of committed grid edits.
	uint64_t get_placement_revision() const;
	// Places an unused item ID at a valid free origin.
	Error try_place(const StringName &p_id, const Vector2i &p_footprint, const Vector2i &p_origin, bool p_rotated = false);
	// Checks whether a footprint can occupy the specified rectangle.
	Error check_placement(const Vector2i &p_footprint, const Vector2i &p_origin, bool p_rotated = false) const;
	// Returns the single item that blocks the proposed placement, if any.
	StringName get_swap_target(const Vector2i &p_footprint, const Vector2i &p_origin, bool p_rotated = false) const;
	// Replaces the expected single blocker with the incoming item.
	Dictionary try_swap_in(const StringName &p_id, const Vector2i &p_footprint, const Vector2i &p_origin, bool p_rotated, const StringName &p_expected_id);
	// Checks whether an existing item can move or rotate to the target cells.
	Error check_move(const StringName &p_id, const Vector2i &p_origin, bool p_rotated) const;
	// Moves or rotates an existing item after validating its target rectangle.
	Error try_move(const StringName &p_id, const Vector2i &p_origin, bool p_rotated);
	// Checks source ownership and destination placement for a transfer.
	Error check_transfer(const StringName &p_id, const Ref<SpatialInventory> &p_destination, const Vector2i &p_origin, bool p_rotated) const;
	// Commits a checked transfer across both inventory grids.
	Error try_transfer(const StringName &p_id, Ref<SpatialInventory> p_destination, const Vector2i &p_origin, bool p_rotated);
	// Changes an item's footprint when its resized rectangle fits.
	Error try_resize_item(const StringName &p_id, const Vector2i &p_footprint);
	// Removes an item and clears its occupied cells.
	Error remove(const StringName &p_id);
	// Returns the item occupying the supplied cell, if any.
	StringName get_item_at(const Vector2i &p_cell) const;
	// Returns the current position, footprint, and rotation of the item.
	Dictionary get_placement(const StringName &p_id) const;
	// Returns the committed placement records in the grid.
	Array get_placements() const;
	// Finds the first free origin for the oriented footprint.
	Vector2i find_first_fit(const Vector2i &p_footprint, bool p_rotated = false, const Vector2i &p_start = Vector2i()) const;
	// Returns a versioned snapshot of the model's committed state.
	Dictionary export_snapshot() const;
	// Validates a snapshot before replacing the model's committed state.
	Error import_snapshot(const Dictionary &p_snapshot);
};

} // namespace godot
