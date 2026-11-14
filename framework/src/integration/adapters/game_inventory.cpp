// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/adapters/game_inventory.hpp"

#include "integration/adapters/inventory_transaction_adapter.hpp"
#include "integration/game/crimson_game.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/signal.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <limits>

using namespace godot;

namespace crimson::integration {
namespace {

// Stable checkpoint store containing all committed backpacks, keyed by durable player ID.
constexpr const char *BACKPACKS = "crimson.inventory";
// Command declared on both roles and routed through the trusted player binding.
constexpr const char *PICKUP_COMMAND = "crimson.inventory.pickup";

// Construct an authority rejection without leaking any other player's inventory.
Dictionary failure(const char *code, const String &message) {
    Dictionary result;
    result["ok"] = false;
    result["code"] = code;
    result["message"] = message;
    return result;
}

// Check the bound shared by command identifiers and portable receipts.
bool valid_id(const String &value) {
    return !value.is_empty() && value.length() <= 128 && value.strip_edges() == value;
}

// Decode a bounded portable coordinate pair from an authority result.
Vector2i read_origin(const Variant &value) {
    if (value.get_type() != Variant::ARRAY) {
        return {-1, -1};
    }
    const Array cell = value;
    if (cell.size() != 2 || cell[0].get_type() != Variant::INT || cell[1].get_type() != Variant::INT) {
        return {-1, -1};
    }
    const int64_t x = cell[0];
    const int64_t y = cell[1];
    if (x < 0 || y < 0 || x > std::numeric_limits<int32_t>::max() ||
            y > std::numeric_limits<int32_t>::max()) {
        return {-1, -1};
    }
    return {static_cast<int32_t>(x), static_cast<int32_t>(y)};
}

// Build a successful store preparation callback result.
Dictionary prepared(const Dictionary &state) {
    Dictionary result;
    result["ok"] = true;
    result["state"] = state;
    return result;
}

} // namespace

void InventoryPickup::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("take", "item_id", "footprint", "source_store", "source_edit"),
            &InventoryPickup::take);
}

Ref<InventoryPickup> InventoryPickup::take(const StringName &p_item_id, const Vector2i &p_footprint,
        const String &p_source_store, const Dictionary &p_source_edit) {
    Dictionary detached;
    if (!valid_id(String(p_item_id)) || p_footprint.x <= 0 || p_footprint.y <= 0 ||
            !valid_id(p_source_store) || p_source_store == BACKPACKS || p_source_edit.is_empty() ||
            !match_runtime::copy_runtime_dto(p_source_edit, detached)) {
        return {};
    }
    Ref<InventoryPickup> pickup;
    pickup.instantiate();
    pickup->item_id = p_item_id;
    pickup->footprint = p_footprint;
    pickup->source_store = p_source_store;
    pickup->source_edit = detached;
    return pickup;
}

void InventoryPickupSource::_bind_methods() {
    GDVIRTUAL_BIND(_register_stores, "stores");
    GDVIRTUAL_BIND(_resolve_pickup, "player_id", "pickup_id", "stores");
}

Error InventoryPickupSource::register_stores(const Ref<match_runtime::TransactionCoordinator> &stores) {
    Error status = OK;
    GDVIRTUAL_CALL(_register_stores, stores, status);
    return status;
}

Ref<InventoryPickup> InventoryPickupSource::resolve(const String &player_id, const StringName &pickup_id,
        const Ref<match_runtime::TransactionCoordinator> &stores) {
    Ref<InventoryPickup> pickup;
    GDVIRTUAL_CALL(_resolve_pickup, player_id, pickup_id, stores, pickup);
    return pickup;
}

void InventoryConfig::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("backpack", "size"), &InventoryConfig::backpack);
    ClassDB::bind_method(D_METHOD("pickups", "source"), &InventoryConfig::pickups);
}

Ref<InventoryConfig> InventoryConfig::backpack(const Vector2i &p_size) {
    if (Inventory::grid(p_size).is_null()) {
        return {};
    }
    Ref<InventoryConfig> settings;
    settings.instantiate();
    settings->size = p_size;
    return settings;
}

Ref<InventoryConfig> InventoryConfig::pickups(const Ref<InventoryPickupSource> &p_source) const {
    Ref<InventoryConfig> settings;
    settings.instantiate();
    settings->size = size;
    settings->source = p_source;
    return settings;
}

void InventoryResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_ok"), &InventoryResult::is_ok);
    ClassDB::bind_method(D_METHOD("get_code"), &InventoryResult::get_code);
    ClassDB::bind_method(D_METHOD("get_message"), &InventoryResult::get_message);
    ClassDB::bind_method(D_METHOD("get_item_id"), &InventoryResult::get_item_id);
    ClassDB::bind_method(D_METHOD("get_origin"), &InventoryResult::get_origin);
    ClassDB::bind_method(D_METHOD("get_revision"), &InventoryResult::get_revision);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "ok", PROPERTY_HINT_NONE, "",
            PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "is_ok");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "code", PROPERTY_HINT_NONE, "",
            PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_code");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "message", PROPERTY_HINT_NONE, "",
            PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_message");
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "item_id", PROPERTY_HINT_NONE, "",
            PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_item_id");
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2I, "origin", PROPERTY_HINT_NONE, "",
            PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_origin");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "revision", PROPERTY_HINT_NONE, "",
            PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_revision");
}

Ref<InventoryResult> InventoryResult::from_dictionary(const Dictionary &outcome) {
    Ref<InventoryResult> result;
    result.instantiate();
    result->ok = outcome.get("ok", false);
    result->code = outcome.get("code", "InventoryPickupFailed");
    result->message = outcome.get("message", "The inventory pickup did not complete.");
    if (result->ok) {
        result->item_id = StringName(String(outcome.get("item_id", String())));
        result->origin = read_origin(outcome.get("origin", Variant()));
        result->revision = outcome.get("revision", int64_t(-1));
    }
    return result;
}

bool InventoryResult::is_ok() const {
    return ok;
}

String InventoryResult::get_code() const {
    return code;
}

String InventoryResult::get_message() const {
    return message;
}

StringName InventoryResult::get_item_id() const {
    return item_id;
}

Vector2i InventoryResult::get_origin() const {
    return origin;
}

int64_t InventoryResult::get_revision() const {
    return revision;
}

void InventoryPickupWaiter::_bind_methods() {
    ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::OBJECT, "result", PROPERTY_HINT_NONE, "",
            PROPERTY_USAGE_DEFAULT, InventoryResult::get_class_static())));
}

void InventoryPickupWaiter::complete(const Dictionary &outcome, const Ref<InventoryPickupWaiter> &waiter) {
    waiter->emit_signal("completed", InventoryResult::from_dictionary(outcome));
}

Variant InventoryPickupWaiter::wait(const Ref<GameOperation> &operation) {
    if (operation.is_null()) {
        return InventoryResult::from_dictionary(failure("InventoryUnavailable", "The game has no active pickup operation."));
    }
    if (operation->is_finished()) {
        return InventoryResult::from_dictionary(operation->get_result());
    }
    Ref<InventoryPickupWaiter> waiter;
    waiter.instantiate();
    if (operation->connect("completed", callable_mp_static(&InventoryPickupWaiter::complete).bind(waiter),
                Object::CONNECT_ONE_SHOT) != OK) {
        return InventoryResult::from_dictionary(failure("InventoryUnavailable", "The pickup completion could not be observed."));
    }
    return Signal(waiter.ptr(), "completed");
}

void GameInventory::_bind_methods() {
    ClassDB::bind_method(D_METHOD("backpack"), &GameInventory::backpack);
}

GameRuntime *GameInventory::resolve_game() const {
    if (std::this_thread::get_id() != owner_thread) {
        return nullptr;
    }
    auto *runtime = Object::cast_to<GameRuntime>(ObjectDB::get_instance(runtime_id));
    return runtime && GameServiceContainer::scenes_for(runtime) == scope ? runtime : nullptr;
}

Ref<GameInventory> GameInventory::attach(GameRuntime *runtime, const Ref<InventoryConfig> &settings) {
    if (!runtime || settings.is_null() || settings->size == Vector2i()) {
        return {};
    }
    const Ref<SceneService> scene_scope = GameServiceContainer::scenes_for(runtime);
    if (scene_scope.is_null()) {
        return {};
    }
    Ref<GameInventory> service;
    service.instantiate();
    service->runtime_id = ObjectID(runtime->get_instance_id());
    service->scope = scene_scope;
    service->config = settings;
    service->owner_thread = std::this_thread::get_id();
    if (runtime->register_command(PICKUP_COMMAND, callable_mp(service.ptr(), &GameInventory::take_pickup)) != OK ||
            runtime->connect("phase_changed", callable_mp(service.ptr(), &GameInventory::on_phase_changed)) != OK) {
        return {};
    }
    return service;
}

Error GameInventory::prepare() {
    GameRuntime *runtime = resolve_game();
    if (!runtime) {
        return ERR_UNAVAILABLE;
    }
    const Ref<match_runtime::TransactionCoordinator> stores = runtime->get_transactions();
    if (stores.is_null()) {
        return ERR_UNAVAILABLE;
    }
    const ObjectID coordinator_id(stores->get_instance_id());
    if (prepared_id == coordinator_id) {
        return prepared_status;
    }
    if (runtime->get_phase() != GameRuntime::IDLE) {
        return ERR_BUSY;
    }
    const Ref<GameInventory> keep_alive(this);
    prepared_id = coordinator_id;
    prepared_status = ERR_BUSY;
    const Error registered = stores->register_store(BACKPACKS, {},
            callable_mp(this, &GameInventory::prepare_backpacks),
            callable_mp(this, &GameInventory::validate_backpacks));
    if (registered != OK) {
        prepared_status = registered;
    } else {
        prepared_status = config->source.is_valid() ? config->source->register_stores(stores) : OK;
        runtime = resolve_game();
        if (!runtime || runtime->get_phase() != GameRuntime::IDLE || runtime->get_transactions() != stores) {
            prepared_status = ERR_UNAVAILABLE;
        }
    }
    return prepared_status;
}

void GameInventory::on_phase_changed(int64_t phase) {
    if (phase == GameRuntime::IDLE || phase == GameRuntime::LOBBY) {
        ++match_generation;
    }
}

Ref<MatchInventory> GameInventory::backpack() {
    const GameRuntime *runtime = resolve_game();
    if (!runtime || runtime->get_phase() != GameRuntime::RUNNING) {
        return {};
    }
    const String player = runtime->get_local_player_id();
    if (player.is_empty()) {
        return {};
    }
    Ref<MatchInventory> bag;
    bag.instantiate();
    bag->owner = Ref<GameInventory>(this);
    bag->generation = match_generation;
    bag->player_id = player;
    return bag;
}

Dictionary GameInventory::prepare_backpacks(const Dictionary &current, const Array &operations,
        const Dictionary &context) const {
    if (operations.size() != 1 || operations[0].get_type() != Variant::DICTIONARY) {
        return failure("InvalidBackpackEdit", "A pickup must stage one backpack change.");
    }
    const Dictionary edit = operations[0];
    const Variant player = edit.get("player_id", Variant());
    const Variant packed = edit.get("grid", Variant());
    if (player.get_type() != Variant::STRING || !valid_id(String(player)) ||
            player != context.get("inventory_player_id", Variant()) || packed.get_type() != Variant::DICTIONARY) {
        return failure("InvalidBackpackEdit", "The backpack edit must target the trusted player.");
    }
    const Ref<SpatialInventory> grid = InventoryTransactionAdapter::unpack_grid(packed);
    if (grid.is_null() || grid->get_grid_size() != config->size) {
        return failure("InvalidBackpackEdit", "The backpack snapshot does not match its configured grid.");
    }
    Dictionary state = current.duplicate();
    state[player] = packed;
    return prepared(state);
}

Dictionary GameInventory::validate_backpacks(const Dictionary &candidate, const Dictionary &) const {
    const Array players = candidate.keys();
    for (const Variant &player : players) {
        if (player.get_type() != Variant::STRING || !valid_id(String(player)) ||
                candidate[player].get_type() != Variant::DICTIONARY) {
            return failure("InvalidBackpackSnapshot", "A saved backpack must belong to a stable player ID.");
        }
        const Ref<SpatialInventory> grid = InventoryTransactionAdapter::unpack_grid(candidate[player]);
        if (grid.is_null() || grid->get_grid_size() != config->size) {
            return failure("InvalidBackpackSnapshot", "A saved backpack does not match its configured grid.");
        }
    }
    return prepared(candidate);
}

Dictionary GameInventory::take_pickup(const String &player_id, const Dictionary &intent) {
    const Dictionary payload = intent.get("payload", Dictionary());
    const Variant requested = payload.get("pickup_id", Variant());
    if (payload.size() != 1 || requested.get_type() != Variant::STRING || !valid_id(String(requested))) {
        return failure("InvalidPickup", "Select an item using its stable pickup ID.");
    }
    GameRuntime *runtime = resolve_game();
    if (!runtime || !runtime->is_authority() || runtime->get_phase() != GameRuntime::RUNNING) {
        return failure("InventoryUnavailable", "The pickup needs a ready authority match.");
    }
    const Ref<match_runtime::TransactionCoordinator> stores = runtime->get_transactions();
    if (stores.is_null() || stores->get_instance_id() != uint64_t(prepared_id) || prepared_status != OK ||
            config->source.is_null()) {
        return failure("InventoryUnavailable", "Configure a pickup source before starting the match.");
    }
    const Ref<GameInventory> keep_alive(this);
    const int64_t revision = stores->get_revision();
    const int64_t epoch = runtime->get_world_epoch();
    const Ref<InventoryPickup> pickup = config->source->resolve(player_id, StringName(String(requested)), stores);
    runtime = resolve_game();
    if (!runtime || runtime->get_phase() != GameRuntime::RUNNING || runtime->get_world_epoch() != epoch ||
            runtime->get_transactions() != stores || stores->get_revision() != revision) {
        return failure("PickupChanged", "The world or its committed state changed while resolving the pickup.");
    }
    if (pickup.is_null()) {
        return failure("PickupDenied", "The item is unavailable or the player cannot take it.");
    }
    const int64_t source_revision = stores->get_store_revision(pickup->source_store);
    if (source_revision < 0) {
        return failure("PickupUnavailable", "The pickup source store was not registered.");
    }
    const Variant existing = stores->read_store_value(BACKPACKS, player_id);
    const Ref<SpatialInventory> grid = existing.get_type() == Variant::DICTIONARY ?
            InventoryTransactionAdapter::unpack_grid(existing) : Inventory::grid(config->size);
    if (grid.is_null()) {
        return failure("InventoryUnavailable", "The player's backpack snapshot could not be read.");
    }
    const Error placed = grid->add(pickup->item_id, pickup->footprint);
    if (placed != OK) {
        return placed == ERR_CANT_CREATE ? failure("InventoryFull", "The backpack has no room for that item.") :
                failure("InventoryPlacementRejected", "The backpack rejected the pickup: " +
                        UtilityFunctions::error_string(placed) + ".");
    }
    Dictionary context;
    context["inventory_player_id"] = player_id;
    const auto transaction = stores->begin(context);
    Dictionary bag_edit;
    bag_edit["player_id"] = player_id;
    bag_edit["grid"] = InventoryTransactionAdapter::pack_snapshot(grid->export_snapshot());
    if (transaction.is_null() ||
            transaction->add_change(pickup->source_store, pickup->source_edit, source_revision) != OK ||
            transaction->add_change(BACKPACKS, bag_edit, stores->get_store_revision(BACKPACKS)) != OK) {
        return failure("PickupRejected", "The source item and backpack changes could not be staged together.");
    }
    Dictionary outcome = stores->commit(transaction);
    if (bool(outcome.get("ok", false))) {
        const Vector2i origin = grid->get_placement(pickup->item_id)["origin"];
        outcome["item_id"] = String(pickup->item_id);
        outcome["origin"] = Array::make(origin.x, origin.y);
    }
    return outcome;
}

Variant GameInventory::submit_pickup(uint64_t generation, const String &player_id, const StringName &pickup_id) {
    GameRuntime *runtime = resolve_game();
    if (!runtime || generation != match_generation || runtime->get_phase() != GameRuntime::RUNNING ||
            runtime->get_local_player_id() != player_id) {
        return InventoryResult::from_dictionary(failure("InventoryUnavailable", "The backpack belongs to a retired or unavailable match."));
    }
    if (!valid_id(String(pickup_id))) {
        return InventoryResult::from_dictionary(failure("InvalidPickup", "Select an item using its stable pickup ID."));
    }
    Dictionary payload;
    payload["pickup_id"] = String(pickup_id);
    return InventoryPickupWaiter::wait(runtime->submit(PICKUP_COMMAND, payload));
}

void MatchInventory::_bind_methods() {
    ClassDB::bind_method(D_METHOD("pick_up", "pickup_id"), &MatchInventory::pick_up);
}

Variant MatchInventory::pick_up(const StringName &pickup_id) {
    if (owner.is_null()) {
        return InventoryResult::from_dictionary(failure("InventoryUnavailable", "Resolve the local player's backpack from the game first."));
    }
    const Ref<MatchInventory> keep_alive(this);
    return owner->submit_pickup(generation, player_id, pickup_id);
}

void register_inventory_integration() {
    GDREGISTER_ABSTRACT_CLASS(InventoryPickup);
    GDREGISTER_CLASS(InventoryPickupSource);
    GDREGISTER_ABSTRACT_CLASS(InventoryConfig);
    GDREGISTER_ABSTRACT_CLASS(InventoryResult);
    GDREGISTER_INTERNAL_CLASS(InventoryPickupWaiter);
    GDREGISTER_ABSTRACT_CLASS(GameInventory);
    GDREGISTER_ABSTRACT_CLASS(MatchInventory);
}

} // namespace crimson::integration
