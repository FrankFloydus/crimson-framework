// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/steam_inventory.hpp"

#include "modules/steam/client/steam_bytes_result.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/char_string.hpp>

#include <algorithm>
#include <charconv>
#include <limits>
#include <set>
#include <utility>
#include <variant>
#include <vector>

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/client/steam_client_scope.hpp"
#include "modules/steam/runtime/steam_runtime.hpp"

#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/object_id.hpp>

#include <array>
#include <cstring>
#include <map>
#include <optional>
#endif

namespace crimson::steam {
using namespace godot;

// Terminal data copied from Steam before the result handle is destroyed.
using InventoryPayload = std::variant<std::monostate, Ref<SteamInventorySnapshot>,
        Ref<SteamInventoryChange>, PackedByteArray>;

// One validated command and its exact completion observer.
struct SteamInventoryRequest {
    // Select the native Steam action and the expected payload shape.
    enum class Action {
        Full, Selected, Proof, Promotions, Promotion, Drop, Craft, Consume, Split, Merge,
    };

    // Action this request will dispatch to Steam.
    Action action = Action::Full;
    // Definition ID for a promotion, generator, or recipe output.
    int64_t definition_id = 0;
    // Selected instances or the ordered mutation inputs.
    std::vector<uint64_t> instances;
    // Positive quantities aligned with the mutation inputs where applicable.
    std::vector<uint32_t> quantities;
    // Result observer retained until Steam replies or observation ends.
    std::function<void(Error, InventoryPayload)> finish;
};

#ifdef CRIMSON_WITH_STEAM
// One result handle owned by the SDK scope that submitted the request.
struct InventoryPending {
    // Native action and detached result observer.
    SteamInventoryRequest request;
    // Monotonic deadline in milliseconds for local observation.
    uint64_t deadline = 0;
    // Submission order for full results with the same SDK timestamp.
    uint64_t order = 0;
    // Whether Steam announced this exact handle as a newer full view.
    bool full_update = false;
};
#endif

// Internal state for one start of the owning client, accessed on the main thread.
struct SteamInventoryState {
#ifdef CRIMSON_WITH_STEAM
    // Borrowed interface valid while the signed-in runtime is retained.
    ISteamInventory *interface = nullptr;
    // SDK result release export kept for every accepted dispatch.
    decltype(&SteamAPI_ISteamInventory_DestroyResult) destroy_result = nullptr;
    // Manual callback subscriptions for definitions, full updates, and result completions.
    std::array<uint64_t, 3> subscriptions{};
    // Owned handles only. A foreign handle is never read, freed, or published.
    std::map<SteamInventoryResult_t, InventoryPending> pending;
    // Shared observers of the next Steam definition update.
    std::vector<std::function<void(Error, const TypedArray<SteamItemDefinition> &)>> catalog_observers;
    // Deadline for the current definition load, or zero if there is no load.
    uint64_t catalog_deadline = 0;
    // Whether Steam has announced the current SDK catalog contents.
    bool catalog_known = false;
    // Snapshot built lazily when callers actually read this catalog revision.
    std::optional<TypedArray<SteamItemDefinition>> catalog;
    // Sequence used to break ties between full result timestamps.
    uint64_t next_order = 1;
    // Latest full-view timestamp published by this client scope.
    int64_t last_full_timestamp = 0;
    // Submission order of that full view.
    uint64_t last_full_order = 0;
#endif
};

namespace {

using Action = SteamInventoryRequest::Action;

// Limit selected-instance and recipe inputs before creating native buffers.
constexpr int64_t max_selected_items = 256;

// Check the app-defined ID range in Steam's item definition schema.
bool valid_definition(int64_t id) {
    return id > 0 && id < 1000000000;
}

// Check the unsigned SDK quantity range before narrowing a Godot integer.
bool valid_quantity(int64_t quantity) {
    return quantity > 0 && quantity <= (std::numeric_limits<uint32_t>::max)();
}

// Parse an unsigned instance ID without a signed cast or partial string match.
bool parse_instance_id(const String &text, uint64_t &id) {
    if (text.is_empty() || text.length() > 20) {
        return false;
    }
    const CharString bytes = text.utf8();
    const char *end = bytes.get_data() + bytes.length();
    const auto parsed = std::from_chars(bytes.get_data(), end, id);
    return parsed.ec == std::errc() && parsed.ptr == end && id != 0 &&
            id != (std::numeric_limits<uint64_t>::max)();
}

// Copy a bounded selection of distinct valid instance IDs.
bool parse_selection(const PackedStringArray &selection, std::vector<uint64_t> &instances) {
    if (selection.is_empty() || selection.size() > max_selected_items) {
        return false;
    }
    std::set<uint64_t> unique;
    for (int64_t index = 0; index < selection.size(); ++index) {
        uint64_t id = 0;
        if (!parse_instance_id(selection[index], id) || !unique.insert(id).second) {
            return false;
        }
        instances.push_back(id);
    }
    return true;
}

// Create a small request whose validation is independent of the SDK build.
SteamInventoryRequest make_request(Action action, int64_t definition_id = 0) {
    SteamInventoryRequest request;
    request.action = action;
    request.definition_id = definition_id;
    return request;
}

// Describe known failures without exposing account data or a native result handle.
String inventory_message(Error error) {
    switch (error) {
        case OK: return {};
        case ERR_INVALID_PARAMETER: return "Invalid Steam inventory input.";
        case ERR_UNAVAILABLE: return "Steam inventory is unavailable for this client.";
        case ERR_DOES_NOT_EXIST: return "The requested Steam inventory entry is missing.";
        case ERR_UNAUTHORIZED: return "Steam denied the inventory request.";
        case ERR_BUSY: return "Too many Steam inventory requests are pending.";
        case ERR_TIMEOUT: return "The Steam inventory request timed out.";
        case ERR_OUT_OF_MEMORY: return "The Steam inventory response exceeded its size limit.";
        default: return "Steam could not complete the inventory request.";
    }
}

#ifdef CRIMSON_WITH_STEAM
// Number of requests the facade may observe concurrently.
constexpr size_t max_pending = 32;
// Maximum lifetime of a pending observation in milliseconds.
constexpr uint64_t timeout_msec = 15000;
// Inventory rows and catalog definitions are bounded before SDK copies.
constexpr uint32_t max_rows = 32768;
// Bound the keys and bytes used by Steam's app-defined catalog fields.
constexpr int64_t max_properties = 256;
constexpr uint32_t max_property_bytes = 65536;
constexpr uint32_t max_catalog_bytes = 16 * 1024 * 1024;
// Proofs are scoped to a selection and bounded before allocating their byte array.
constexpr uint32_t max_proof_bytes = 1024 * 1024;

// Resolve the exact RefCounted observer without retaining a dead client scope.
Ref<SteamInventory> lookup_service(ObjectID id) {
    return Ref<SteamInventory>(Object::cast_to<SteamInventory>(ObjectDB::get_instance(id)));
}

// Translate the SDK's terminal status using the framework's operation errors.
Error result_error(EResult result) {
    switch (result) {
        case k_EResultOK: return OK;
        case k_EResultInvalidParam: return ERR_INVALID_PARAMETER;
        case k_EResultAccessDenied: return ERR_UNAUTHORIZED;
        case k_EResultNoMatch: return ERR_DOES_NOT_EXIST;
        case k_EResultTimeout: return ERR_TIMEOUT;
        case k_EResultServiceUnavailable: return ERR_UNAVAILABLE;
        case k_EResultLimitExceeded:
        case k_EResultBusy: return ERR_BUSY;
        default: return FAILED;
    }
}

// Report when a row represents a fully removed instance instead of a present stack.
bool is_removed(const SteamItemDetails_t &row) {
    return (row.m_unFlags & k_ESteamItemRemoved) != 0 || row.m_unQuantity == 0;
}

// Read only the owned result handle and cap the SDK-reported row count.
Error read_rows(const SteamRuntime &active, ISteamInventory *inventory, SteamInventoryResult_t handle,
        std::vector<SteamItemDetails_t> &rows) {
    const auto get = active.symbol<decltype(&SteamAPI_ISteamInventory_GetResultItems)>(
            "SteamAPI_ISteamInventory_GetResultItems");
    if (!get) {
        return ERR_UNAVAILABLE;
    }
    uint32 count = 0;
    const bool available = get(inventory, handle, nullptr, &count);
    if (count == 0) {
        // Steam can return false for a successfully completed result with no items.
        return OK;
    }
    if (!available || count > max_rows) {
        return available ? ERR_OUT_OF_MEMORY : FAILED;
    }
    rows.resize(count);
    if (!get(inventory, handle, rows.data(), &count) || count > rows.size()) {
        return FAILED;
    }
    rows.resize(count);
    return OK;
}

// Copy exactly one property after bounding its SDK-reported size.
Error read_property(ISteamInventory *inventory, decltype(&SteamAPI_ISteamInventory_GetItemDefinitionProperty) get,
        SteamItemDef_t id, const char *key, uint32_t &remaining, String &property) {
    uint32 bytes = 0;
    if (!get(inventory, id, key, nullptr, &bytes)) {
        return ERR_DOES_NOT_EXIST;
    }
    if (bytes == 0) {
        return OK;
    }
    if (bytes > max_property_bytes || bytes > remaining) {
        return ERR_OUT_OF_MEMORY;
    }
    std::vector<char> buffer(bytes);
    uint32 written = bytes;
    if (!get(inventory, id, key, buffer.data(), &written) || written > bytes) {
        return FAILED;
    }
    remaining -= written;
    const auto end = std::find(buffer.begin(), buffer.begin() + written, '\0');
    property = String::utf8(buffer.data(), end - buffer.begin());
    return OK;
}

// Validate the exact selected items before serializing a signed proof.
Error serialize_proof(const SteamRuntime &active, ISteamInventory *inventory, SteamInventoryResult_t handle,
        const std::vector<uint64_t> &selection, const std::vector<SteamItemDetails_t> &rows, PackedByteArray &proof) {
    const auto check_owner = active.symbol<decltype(&SteamAPI_ISteamInventory_CheckResultSteamID)>(
            "SteamAPI_ISteamInventory_CheckResultSteamID");
    const auto serialize = active.symbol<decltype(&SteamAPI_ISteamInventory_SerializeResult)>(
            "SteamAPI_ISteamInventory_SerializeResult");
    if (!check_owner || !serialize) {
        return ERR_UNAVAILABLE;
    }
    if (!check_owner(inventory, handle, active.get_user_id())) {
        return ERR_UNAUTHORIZED;
    }
    if (rows.size() != selection.size()) {
        return ERR_DOES_NOT_EXIST;
    }
    std::set<uint64_t> included;
    for (const auto &row : rows) {
        if (is_removed(row) || !included.insert(row.m_itemId).second ||
                std::find(selection.begin(), selection.end(), row.m_itemId) == selection.end()) {
            return ERR_DOES_NOT_EXIST;
        }
    }
    uint32 bytes = 0;
    if (!serialize(inventory, handle, nullptr, &bytes) || bytes == 0) {
        return FAILED;
    }
    if (bytes > max_proof_bytes || proof.resize(bytes) != OK) {
        return ERR_OUT_OF_MEMORY;
    }
    if (!serialize(inventory, handle, proof.ptrw(), &bytes) || bytes == 0 || bytes > proof.size()) {
        return FAILED;
    }
    return static_cast<Error>(proof.resize(bytes));
}

// Call a flat SDK entry point without handing a missing symbol a result handle.
template <typename Function, typename... Args>
Error call_inventory(const SteamRuntime &runtime, ISteamInventory *inventory, SteamInventoryResult_t &handle,
        const char *name, Args... args) {
    const auto call = runtime.symbol<Function>(name);
    if (!call) {
        return ERR_UNAVAILABLE;
    }
    return call(inventory, &handle, args...) ? OK : FAILED;
}

// Map each validated request to its versioned flat Steam API action.
Error dispatch_request(const SteamRuntime &runtime, ISteamInventory *inventory,
        const SteamInventoryRequest &request, SteamInventoryResult_t &handle) {
    const auto definition_id = static_cast<SteamItemDef_t>(request.definition_id);
    switch (request.action) {
        case Action::Full:
            return call_inventory<decltype(&SteamAPI_ISteamInventory_GetAllItems)>(
                    runtime, inventory, handle, "SteamAPI_ISteamInventory_GetAllItems");
        case Action::Selected:
        case Action::Proof: {
            const std::vector<SteamItemInstanceID_t> items(request.instances.begin(), request.instances.end());
            return call_inventory<decltype(&SteamAPI_ISteamInventory_GetItemsByID)>(
                    runtime, inventory, handle, "SteamAPI_ISteamInventory_GetItemsByID", items.data(),
                    static_cast<uint32>(items.size()));
        }
        case Action::Promotions:
            return call_inventory<decltype(&SteamAPI_ISteamInventory_GrantPromoItems)>(
                    runtime, inventory, handle, "SteamAPI_ISteamInventory_GrantPromoItems");
        case Action::Promotion:
            return call_inventory<decltype(&SteamAPI_ISteamInventory_AddPromoItem)>(
                    runtime, inventory, handle, "SteamAPI_ISteamInventory_AddPromoItem", definition_id);
        case Action::Drop:
            return call_inventory<decltype(&SteamAPI_ISteamInventory_TriggerItemDrop)>(
                    runtime, inventory, handle, "SteamAPI_ISteamInventory_TriggerItemDrop", definition_id);
        case Action::Craft: {
            const uint32 output_quantity = 1;
            const std::vector<SteamItemInstanceID_t> items(request.instances.begin(), request.instances.end());
            const std::vector<uint32> quantities(request.quantities.begin(), request.quantities.end());
            return call_inventory<decltype(&SteamAPI_ISteamInventory_ExchangeItems)>(
                    runtime, inventory, handle, "SteamAPI_ISteamInventory_ExchangeItems", &definition_id,
                    &output_quantity, uint32(1), items.data(), quantities.data(), static_cast<uint32>(items.size()));
        }
        case Action::Consume:
            return call_inventory<decltype(&SteamAPI_ISteamInventory_ConsumeItem)>(
                    runtime, inventory, handle, "SteamAPI_ISteamInventory_ConsumeItem",
                    request.instances.front(), request.quantities.front());
        case Action::Split:
        case Action::Merge: {
            const SteamItemInstanceID_t destination = request.action == Action::Split ?
                    k_SteamItemInstanceIDInvalid : request.instances[1];
            return call_inventory<decltype(&SteamAPI_ISteamInventory_TransferItemQuantity)>(
                    runtime, inventory, handle, "SteamAPI_ISteamInventory_TransferItemQuantity",
                    request.instances.front(), request.quantities.front(), destination);
        }
    }
    return ERR_BUG;
}
#endif

} // namespace

SteamInventory::SteamInventory() = default;
SteamInventory::~SteamInventory() = default;

void SteamInventory::_bind_methods() {
    ClassDB::bind_method(D_METHOD("definitions"), &SteamInventory::definitions);
    ClassDB::bind_method(D_METHOD("definition", "definition_id"), &SteamInventory::definition);
    ClassDB::bind_method(D_METHOD("items"), &SteamInventory::items);
    ClassDB::bind_method(D_METHOD("items_by_id", "instance_ids"), &SteamInventory::items_by_id);
    ClassDB::bind_method(D_METHOD("claim_promotions"), &SteamInventory::claim_promotions);
    ClassDB::bind_method(D_METHOD("claim_promotion", "definition_id"), &SteamInventory::claim_promotion);
    ClassDB::bind_method(D_METHOD("claim_playtime_drop", "generator_id"), &SteamInventory::claim_playtime_drop);
    ClassDB::bind_method(D_METHOD("craft", "output_definition_id", "ingredients"), &SteamInventory::craft);
    ClassDB::bind_method(D_METHOD("consume", "instance_id", "quantity"), &SteamInventory::consume, DEFVAL(1));
    ClassDB::bind_method(D_METHOD("split", "instance_id", "quantity"), &SteamInventory::split);
    ClassDB::bind_method(D_METHOD("merge", "from_instance_id", "into_instance_id", "quantity"),
            &SteamInventory::merge);
    ClassDB::bind_method(D_METHOD("proof", "instance_ids"), &SteamInventory::proof);
    ADD_SIGNAL(MethodInfo("definitions_changed"));
    ADD_SIGNAL(MethodInfo("items_changed", PropertyInfo(Variant::OBJECT, "snapshot", PROPERTY_HINT_RESOURCE_TYPE,
            SteamInventorySnapshot::get_class_static())));
}

void SteamInventory::attach(const std::shared_ptr<SteamRuntime> &active,
        const std::shared_ptr<SteamClientScope> &scope) {
#ifdef CRIMSON_WITH_STEAM
    if (!active || !scope || !scope->active || !SteamRuntime::on_main_thread()) {
        return;
    }
    const auto get = active->symbol<decltype(&SteamAPI_SteamInventory_v003)>("SteamAPI_SteamInventory_v003");
    auto native = std::make_unique<SteamInventoryState>();
    native->interface = get ? get() : nullptr;
    native->destroy_result = active->symbol<decltype(&SteamAPI_ISteamInventory_DestroyResult)>(
            "SteamAPI_ISteamInventory_DestroyResult");
    if (!native->interface || !native->destroy_result) {
        return;
    }
    const ObjectID id(get_instance_id());
    native->subscriptions[0] = active->subscribe(SteamInventoryDefinitionUpdate_t::k_iCallback,
            [id](const void *, size_t size) {
                const auto owner = lookup_service(id);
                if (owner.is_valid() && (size == 0 || size == sizeof(SteamInventoryDefinitionUpdate_t))) {
                    owner->on_definitions_updated();
                }
            });
    native->subscriptions[1] = active->subscribe(SteamInventoryFullUpdate_t::k_iCallback,
            [id](const void *data, size_t size) {
                if (!data || size != sizeof(SteamInventoryFullUpdate_t)) {
                    return;
                }
                SteamInventoryFullUpdate_t event{};
                std::memcpy(&event, data, sizeof(event));
                const auto owner = lookup_service(id);
                if (owner.is_valid()) {
                    owner->on_full_update(event.m_handle);
                }
            });
    native->subscriptions[2] = active->subscribe(SteamInventoryResultReady_t::k_iCallback,
            [id](const void *data, size_t size) {
                if (!data || size != sizeof(SteamInventoryResultReady_t)) {
                    return;
                }
                SteamInventoryResultReady_t event{};
                std::memcpy(&event, data, sizeof(event));
                const auto owner = lookup_service(id);
                if (owner.is_valid()) {
                    owner->on_result_ready(event.m_handle, event.m_result);
                }
            });
    if (std::find(native->subscriptions.begin(), native->subscriptions.end(), 0) != native->subscriptions.end()) {
        for (const auto subscription : native->subscriptions) {
            active->unsubscribe(subscription);
        }
        return;
    }
    runtime = active;
    client_scope = scope;
    state = std::move(native);
#endif
}

std::shared_ptr<SteamRuntime> SteamInventory::active_runtime() const {
#ifdef CRIMSON_WITH_STEAM
    if (SteamRuntime::on_main_thread() && state) {
        const auto scope = client_scope.lock();
        if (scope && scope->active) {
            return runtime.lock();
        }
    }
#endif
    return {};
}

void SteamInventory::close() {
#ifdef CRIMSON_WITH_STEAM
    const auto active = runtime.lock();
    auto native = std::move(state);
    client_scope.reset();
    runtime.reset();
    if (!native) {
        return;
    }
    if (active) {
        for (const auto subscription : native->subscriptions) {
            active->unsubscribe(subscription);
        }
        for (const auto &pending : native->pending) {
            native->destroy_result(native->interface, pending.first);
        }
    }
    for (const auto &pending : native->pending) {
        pending.second.request.finish(ERR_UNAVAILABLE, {});
    }
    for (const auto &observer : native->catalog_observers) {
        observer(ERR_UNAVAILABLE, {});
    }
#endif
}

void SteamInventory::tick() {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (!active) {
        return;
    }
    const uint64_t now = Time::get_singleton()->get_ticks_msec();
    std::vector<SteamInventoryRequest> expired;
    for (auto entry = state->pending.begin(); entry != state->pending.end();) {
        if (entry->second.deadline > now) {
            ++entry;
            continue;
        }
        state->destroy_result(state->interface, entry->first);
        expired.push_back(std::move(entry->second.request));
        entry = state->pending.erase(entry);
    }
    std::vector<CatalogObserver> expired_catalog;
    if (state->catalog_deadline != 0 && state->catalog_deadline <= now) {
        state->catalog_deadline = 0;
        expired_catalog.swap(state->catalog_observers);
    }
    for (const auto &request : expired) {
        request.finish(ERR_TIMEOUT, {});
    }
    for (const auto &observer : expired_catalog) {
        observer(ERR_TIMEOUT, {});
    }
#endif
}

void SteamInventory::request_catalog(CatalogObserver observer) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (active) {
        if (state->catalog_known) {
            if (state->catalog) {
                observer(OK, *state->catalog);
                return;
            }
            TypedArray<SteamItemDefinition> catalog;
            const Error error = read_catalog(*active, catalog);
            if (error == OK) {
                state->catalog = catalog;
            }
            observer(error, catalog);
            return;
        }
        if (state->catalog_observers.size() == max_pending) {
            observer(ERR_BUSY, {});
            return;
        }
        if (state->catalog_observers.empty()) {
            const auto load = active->symbol<decltype(&SteamAPI_ISteamInventory_LoadItemDefinitions)>(
                    "SteamAPI_ISteamInventory_LoadItemDefinitions");
            if (!load || !load(state->interface)) {
                observer(ERR_UNAVAILABLE, {});
                return;
            }
            state->catalog_deadline = Time::get_singleton()->get_ticks_msec() + timeout_msec;
        }
        state->catalog_observers.push_back(std::move(observer));
        return;
    }
#endif
    observer(ERR_UNAVAILABLE, {});
}

Error SteamInventory::read_catalog(const SteamRuntime &active, TypedArray<SteamItemDefinition> &catalog) {
#ifdef CRIMSON_WITH_STEAM
    const auto get_ids = active.symbol<decltype(&SteamAPI_ISteamInventory_GetItemDefinitionIDs)>(
            "SteamAPI_ISteamInventory_GetItemDefinitionIDs");
    const auto get_property = active.symbol<decltype(&SteamAPI_ISteamInventory_GetItemDefinitionProperty)>(
            "SteamAPI_ISteamInventory_GetItemDefinitionProperty");
    if (!get_ids || !get_property) {
        return ERR_UNAVAILABLE;
    }
    uint32 count = 0;
    const bool available = get_ids(state->interface, nullptr, &count);
    if (count == 0) {
        catalog.make_read_only();
        return OK;
    }
    if (!available || count > max_rows) {
        return available ? ERR_OUT_OF_MEMORY : FAILED;
    }
    std::vector<SteamItemDef_t> ids(count);
    if (!get_ids(state->interface, ids.data(), &count) || count > ids.size()) {
        return FAILED;
    }
    uint32_t remaining = max_catalog_bytes;
    for (uint32 index = 0; index < count; ++index) {
        const auto id = ids[index];
        if (!valid_definition(id)) {
            return ERR_INVALID_DATA;
        }
        String key_list;
        const Error list_error = read_property(state->interface, get_property, id, nullptr, remaining, key_list);
        if (list_error != OK) {
            return list_error;
        }
        const PackedStringArray keys = key_list.split(",", false, max_properties);
        if (keys.size() > max_properties) {
            return ERR_OUT_OF_MEMORY;
        }
        TypedDictionary<String, String> properties;
        for (int64_t key_index = 0; key_index < keys.size(); ++key_index) {
            const String key = keys[key_index];
            const CharString name = key.utf8();
            String value;
            const Error error = read_property(state->interface, get_property, id, name.get_data(), remaining, value);
            if (error != OK) {
                return error;
            }
            properties[key] = value;
        }
        Ref<SteamItemDefinition> definition;
        definition.instantiate();
        definition->definition_id = id;
        definition->display_name = properties.get("name", String());
        definition->type = StringName(String(properties.get("type", String())));
        properties.make_read_only();
        definition->properties = properties;
        catalog.push_back(definition);
    }
    catalog.make_read_only();
    return OK;
#else
    return ERR_UNAVAILABLE;
#endif
}

void SteamInventory::on_definitions_updated() {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (!active) {
        return;
    }
    state->catalog_known = true;
    state->catalog.reset();
    state->catalog_deadline = 0;
    auto observers = std::move(state->catalog_observers);
    state->catalog_observers.clear();
    if (!observers.empty()) {
        TypedArray<SteamItemDefinition> catalog;
        const Error error = read_catalog(*active, catalog);
        if (error == OK) {
            state->catalog = catalog;
        }
        for (const auto &observer : observers) {
            observer(error, catalog);
        }
    }
    if (active_runtime()) {
        emit_signal("definitions_changed");
    }
#endif
}

void SteamInventory::on_full_update(int32_t handle) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (active) {
        const auto pending = state->pending.find(handle);
        if (pending != state->pending.end() && pending->second.request.action == Action::Full) {
            pending->second.full_update = true;
        }
    }
#endif
}

void SteamInventory::on_result_ready(int32_t handle, int status) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (!active || status == k_EResultPending) {
        return;
    }
    const auto entry = state->pending.find(handle);
    if (entry == state->pending.end()) {
        return;
    }
    auto pending = std::move(entry->second);
    state->pending.erase(entry);
    const auto &request = pending.request;
    const bool snapshot_request = request.action == Action::Full || request.action == Action::Selected;
    const bool expired = status == k_EResultExpired && snapshot_request;
    Error error = expired ? OK : result_error(static_cast<EResult>(status));
    std::vector<SteamItemDetails_t> rows;
    if (error == OK) {
        error = read_rows(*active, state->interface, handle, rows);
    }
    InventoryPayload payload;
    Ref<SteamInventorySnapshot> changed_snapshot;
    if (error == OK && request.action == Action::Proof) {
        PackedByteArray bytes;
        error = serialize_proof(*active, state->interface, handle, request.instances, rows, bytes);
        payload = std::move(bytes);
    } else if (error == OK) {
        TypedArray<SteamItemInstance> granted;
        TypedArray<SteamItemInstance> changed;
        PackedStringArray removed;
        for (const auto &row : rows) {
            const bool selected = std::find(request.instances.begin(), request.instances.end(), row.m_itemId) !=
                    request.instances.end();
            if (request.action == Action::Selected && !selected) {
                error = ERR_INVALID_DATA;
                break;
            }
            if (row.m_itemId == 0 || row.m_itemId == k_SteamItemInstanceIDInvalid) {
                error = ERR_INVALID_DATA;
                break;
            }
            const String id = String::num_uint64(row.m_itemId);
            if (is_removed(row)) {
                if (!snapshot_request) {
                    removed.push_back(id);
                }
                continue;
            }
            if (!valid_definition(row.m_iDefinition)) {
                error = ERR_INVALID_DATA;
                break;
            }
            Ref<SteamItemInstance> item;
            item.instantiate();
            item->instance_id = id;
            item->definition_id = row.m_iDefinition;
            item->quantity = row.m_unQuantity;
            item->flags = row.m_unFlags;
            if (snapshot_request || selected) {
                changed.push_back(item);
            } else {
                granted.push_back(item);
            }
        }
        if (error == OK && snapshot_request) {
            const auto timestamp = active->symbol<decltype(&SteamAPI_ISteamInventory_GetResultTimestamp)>(
                    "SteamAPI_ISteamInventory_GetResultTimestamp");
            const int64_t observed_at = timestamp ? timestamp(state->interface, handle) : 0;
            const bool newer = pending.full_update && !expired &&
                    (observed_at > state->last_full_timestamp ||
                            (observed_at == state->last_full_timestamp && pending.order > state->last_full_order));
            Ref<SteamInventorySnapshot> snapshot;
            snapshot.instantiate();
            snapshot->observed_at_unix = observed_at;
            changed.make_read_only();
            snapshot->items = changed;
            if (newer) {
                snapshot->freshness = SteamInventorySnapshot::FRESHNESS_FRESH;
                state->last_full_timestamp = observed_at;
                state->last_full_order = pending.order;
                changed_snapshot = snapshot;
            } else if (expired) {
                snapshot->freshness = SteamInventorySnapshot::FRESHNESS_CACHED;
            }
            payload = snapshot;
        } else if (error == OK) {
            Ref<SteamInventoryChange> result;
            result.instantiate();
            granted.make_read_only();
            changed.make_read_only();
            result->granted = granted;
            result->changed = changed;
            result->removed_instance_ids = removed;
            payload = result;
        }
    }
    state->destroy_result(state->interface, handle);
    request.finish(error, std::move(payload));
    if (changed_snapshot.is_valid() && active_runtime()) {
        emit_signal("items_changed", changed_snapshot);
    }
#endif
}

void SteamInventory::submit(SteamInventoryRequest request) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (active) {
        if (state->pending.size() >= max_pending) {
            request.finish(ERR_BUSY, {});
            return;
        }
        SteamInventoryResult_t handle = k_SteamInventoryResultInvalid;
        const Error error = dispatch_request(*active, state->interface, request, handle);
        const bool owned = state->pending.count(handle) != 0;
        if (error != OK) {
            // GetAllItems also uses zero as an output when dispatch reports unavailable.
            if (handle != k_SteamInventoryResultInvalid && !owned && (request.action != Action::Full || handle != 0)) {
                state->destroy_result(state->interface, handle);
            }
            request.finish(error, {});
            return;
        }
        if (handle == k_SteamInventoryResultInvalid || owned) {
            request.finish(FAILED, {});
            return;
        }
        state->pending.emplace(handle, InventoryPending{std::move(request),
                Time::get_singleton()->get_ticks_msec() + timeout_msec, state->next_order++, false});
        return;
    }
#endif
    request.finish(ERR_UNAVAILABLE, {});
}

Variant SteamInventory::definitions() {
    Ref<SteamItemDefinitionsOperation> operation;
    operation.instantiate();
    request_catalog([operation](Error error, const TypedArray<SteamItemDefinition> &catalog) {
        operation->finish(SteamItemDefinitionsResult::create(error, inventory_message(error),
                error == OK ? catalog : TypedArray<SteamItemDefinition>()));
    });
    return operation->wait();
}

Variant SteamInventory::definition(int64_t definition_id) {
    if (!valid_definition(definition_id)) {
        return SteamItemDefinitionResult::create(ERR_INVALID_PARAMETER, inventory_message(ERR_INVALID_PARAMETER));
    }
    Ref<SteamItemDefinitionOperation> operation;
    operation.instantiate();
    request_catalog([operation, definition_id](Error error, const TypedArray<SteamItemDefinition> &catalog) {
        Ref<SteamItemDefinition> definition;
        if (error == OK) {
            error = ERR_DOES_NOT_EXIST;
            for (int64_t index = 0; index < catalog.size(); ++index) {
                const Ref<SteamItemDefinition> item = catalog[index];
                if (item->get_definition_id() == definition_id) {
                    definition = item;
                    error = OK;
                    break;
                }
            }
        }
        operation->finish(SteamItemDefinitionResult::create(error, inventory_message(error), definition));
    });
    return operation->wait();
}

Variant SteamInventory::observe_snapshot(SteamInventoryRequest request) {
    Ref<SteamInventorySnapshotOperation> operation;
    operation.instantiate();
    request.finish = [operation](Error error, InventoryPayload payload) {
        const auto value = std::get_if<Ref<SteamInventorySnapshot>>(&payload);
        operation->finish(SteamInventorySnapshotResult::create(error, inventory_message(error),
                value ? *value : Ref<SteamInventorySnapshot>()));
    };
    submit(std::move(request));
    return operation->wait();
}

Variant SteamInventory::observe_change(SteamInventoryRequest request) {
    Ref<SteamInventoryChangeOperation> operation;
    operation.instantiate();
    request.finish = [operation](Error error, InventoryPayload payload) {
        const auto value = std::get_if<Ref<SteamInventoryChange>>(&payload);
        String message = inventory_message(error);
        if (error == ERR_TIMEOUT) {
            message += " Refresh the inventory before retrying.";
        }
        operation->finish(SteamInventoryChangeResult::create(error, message,
                value ? *value : Ref<SteamInventoryChange>()));
    };
    submit(std::move(request));
    return operation->wait();
}

Variant SteamInventory::items() {
    return observe_snapshot(make_request(Action::Full));
}

Variant SteamInventory::items_by_id(const PackedStringArray &instance_ids) {
    auto request = make_request(Action::Selected);
    if (!parse_selection(instance_ids, request.instances)) {
        return SteamInventorySnapshotResult::create(ERR_INVALID_PARAMETER, inventory_message(ERR_INVALID_PARAMETER));
    }
    return observe_snapshot(std::move(request));
}

Variant SteamInventory::claim_promotions() {
    return observe_change(make_request(Action::Promotions));
}

Variant SteamInventory::claim_promotion(int64_t definition_id) {
    if (!valid_definition(definition_id)) {
        return SteamInventoryChangeResult::create(ERR_INVALID_PARAMETER, inventory_message(ERR_INVALID_PARAMETER));
    }
    return observe_change(make_request(Action::Promotion, definition_id));
}

Variant SteamInventory::claim_playtime_drop(int64_t generator_id) {
    if (!valid_definition(generator_id)) {
        return SteamInventoryChangeResult::create(ERR_INVALID_PARAMETER, inventory_message(ERR_INVALID_PARAMETER));
    }
    return observe_change(make_request(Action::Drop, generator_id));
}

Variant SteamInventory::craft(int64_t output_definition_id, const TypedArray<SteamItemQuantity> &ingredients) {
    if (!valid_definition(output_definition_id) || ingredients.is_empty() || ingredients.size() > max_selected_items) {
        return SteamInventoryChangeResult::create(ERR_INVALID_PARAMETER, inventory_message(ERR_INVALID_PARAMETER));
    }
    auto request = make_request(Action::Craft, output_definition_id);
    std::set<uint64_t> unique;
    for (int64_t index = 0; index < ingredients.size(); ++index) {
        const Ref<SteamItemQuantity> item = ingredients[index];
        uint64_t id = 0;
        if (item.is_null() || !parse_instance_id(item->get_instance_id(), id) ||
                !valid_quantity(item->get_quantity()) || !unique.insert(id).second) {
            return SteamInventoryChangeResult::create(ERR_INVALID_PARAMETER, inventory_message(ERR_INVALID_PARAMETER));
        }
        request.instances.push_back(id);
        request.quantities.push_back(static_cast<uint32_t>(item->get_quantity()));
    }
    return observe_change(std::move(request));
}

Variant SteamInventory::consume(const String &instance_id, int64_t quantity) {
    uint64_t id = 0;
    if (!valid_quantity(quantity) || !parse_instance_id(instance_id, id)) {
        return SteamInventoryChangeResult::create(ERR_INVALID_PARAMETER, inventory_message(ERR_INVALID_PARAMETER));
    }
    auto request = make_request(Action::Consume);
    request.instances = {id};
    request.quantities = {static_cast<uint32_t>(quantity)};
    return observe_change(std::move(request));
}

Variant SteamInventory::transfer(const String &from_instance_id, const String &into_instance_id,
        int64_t quantity, bool split_stack) {
    uint64_t from = 0;
    uint64_t into = 0;
    if (!valid_quantity(quantity) || !parse_instance_id(from_instance_id, from) ||
            (!split_stack && (!parse_instance_id(into_instance_id, into) || from == into))) {
        return SteamInventoryChangeResult::create(ERR_INVALID_PARAMETER, inventory_message(ERR_INVALID_PARAMETER));
    }
    auto request = make_request(split_stack ? Action::Split : Action::Merge);
    request.instances = {from, into};
    request.quantities = {static_cast<uint32_t>(quantity)};
    return observe_change(std::move(request));
}

Variant SteamInventory::split(const String &instance_id, int64_t quantity) {
    return transfer(instance_id, {}, quantity, true);
}

Variant SteamInventory::merge(const String &from_instance_id, const String &into_instance_id, int64_t quantity) {
    return transfer(from_instance_id, into_instance_id, quantity, false);
}

Variant SteamInventory::proof(const PackedStringArray &instance_ids) {
    auto request = make_request(Action::Proof);
    if (!parse_selection(instance_ids, request.instances)) {
        return SteamBytesResult::create(ERR_INVALID_PARAMETER, inventory_message(ERR_INVALID_PARAMETER));
    }
    Ref<SteamBytesOperation> operation;
    operation.instantiate();
    request.finish = [operation](Error error, InventoryPayload payload) {
        const auto value = std::get_if<PackedByteArray>(&payload);
        operation->finish(SteamBytesResult::create(error, inventory_message(error),
                error == OK && value ? *value : PackedByteArray()));
    };
    submit(std::move(request));
    return operation->wait();
}

} // namespace crimson::steam
