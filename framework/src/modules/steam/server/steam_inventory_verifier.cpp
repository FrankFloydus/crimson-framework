// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/server/steam_inventory_verifier.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/char_string.hpp>

#include <charconv>
#include <utility>

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/runtime/steam_runtime.hpp"
#include "modules/steam/server/steam_server_runtime.hpp"

#include <godot_cpp/classes/time.hpp>

#include <map>
#include <set>
#include <vector>
#endif

namespace crimson::steam {
using namespace godot;

namespace {

// Match the client proof serialization byte limit before parsing any untrusted proof.
constexpr int64_t max_proof_bytes = 1024 * 1024;

// Read one canonical decimal SteamID64 without losing any unsigned bits.
bool parse_account(const String &text, uint64_t &account) {
    if (text.is_empty() || text.length() > 20 || text[0] == U'0') {
        return false;
    }
    const CharString bytes = text.utf8();
    const char *begin = bytes.get_data();
    const char *end = begin + bytes.length();
    const auto parsed = std::from_chars(begin, end, account);
    return parsed.ec == std::errc() && parsed.ptr == end && account != 0;
}

#ifdef CRIMSON_WITH_STEAM
// Match the maximum selection accepted by the client proof builder.
constexpr uint32_t max_proof_items = 256;
// Bound asynchronous verification pressure from remote clients.
constexpr size_t max_pending = 32;
// A verified proof is usable for equipment decisions for at most five minutes.
constexpr uint32_t max_age_seconds = 300;
// Local deadline for an asynchronous Steam verification result.
constexpr uint64_t timeout_msec = 15000;

// One owned SDK result and the exact admitted identity that requested verification.
struct PendingProof {
    // Keep submitted bytes alive until Steam's result handle is destroyed.
    PackedByteArray bytes;
    // Claimed Steam account already checked against the active Session.
    uint64_t account = 0;
    // Identity of the exact admitted connection, including reconnects.
    ObjectID identity;
    // Monotonic observation deadline in milliseconds.
    uint64_t deadline = 0;
    // Result delivered to the awaiting authority.
    Ref<SteamInventorySnapshotOperation> operation;
};

// Detached completion waiting for native state and handles to be fully updated.
struct CompletedProof {
    // Steam account to resolve through the authority again before delivery.
    uint64_t account = 0;
    // Exact admitted identity that must still own the account.
    ObjectID identity;
    // Detached terminal verification result.
    Ref<SteamInventorySnapshotResult> result;
    // Retained observer to notify after native cleanup.
    Ref<SteamInventorySnapshotOperation> operation;
};
#endif

} // namespace

// Dedicated interface pointers are used only while the exact server runtime is held.
struct SteamInventoryVerifierState {
#ifdef CRIMSON_WITH_STEAM
    // Borrowed GameServer inventory interface.
    ISteamInventory *inventory = nullptr;
    // Borrowed GameServer utilities for Steam's time.
    ISteamUtils *utils = nullptr;
    // Submit signed result bytes for verification.
    decltype(&SteamAPI_ISteamInventory_DeserializeResult) deserialize = nullptr;
    // Release one owned verification result handle.
    decltype(&SteamAPI_ISteamInventory_DestroyResult) destroy = nullptr;
    // Read a verification result's current SDK status.
    decltype(&SteamAPI_ISteamInventory_GetResultStatus) status = nullptr;
    // Bind the verified result to its signed account.
    decltype(&SteamAPI_ISteamInventory_CheckResultSteamID) check_owner = nullptr;
    // Read the signed result timestamp.
    decltype(&SteamAPI_ISteamInventory_GetResultTimestamp) timestamp = nullptr;
    // Copy the verified result's selected rows.
    decltype(&SteamAPI_ISteamInventory_GetResultItems) items = nullptr;
    // Read Steam's current server clock through the dedicated interface.
    decltype(&SteamAPI_ISteamUtils_GetServerRealTime) steam_time = nullptr;
    // Owned result handles are polled on the dedicated server update.
    std::map<SteamInventoryResult_t, PendingProof> pending;
#endif
};

SteamInventoryVerifier::SteamInventoryVerifier() = default;

SteamInventoryVerifier::~SteamInventoryVerifier() {
    close();
}

void SteamInventoryVerifier::_bind_methods() {
    ClassDB::bind_method(D_METHOD("verify", "proof", "expected_steam_id"), &SteamInventoryVerifier::verify);
}

void SteamInventoryVerifier::attach(const std::shared_ptr<SteamServerRuntime> &active, AuthenticatedIdentity resolver) {
#ifdef CRIMSON_WITH_STEAM
    if (SteamRuntime::on_main_thread() && active && resolver && runtime.expired() && !authenticated_identity) {
        runtime = active;
        authenticated_identity = std::move(resolver);
    }
#endif
}

Variant SteamInventoryVerifier::verify(const PackedByteArray &proof, const String &expected_steam_id) {
    uint64_t account = 0;
    if (proof.is_empty() || proof.size() > max_proof_bytes || !parse_account(expected_steam_id, account)) {
        return SteamInventorySnapshotResult::create(ERR_INVALID_PARAMETER);
    }
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread()) {
        return SteamInventorySnapshotResult::create(ERR_BUSY);
    }
    const auto active = runtime.lock();
    if (!active || !authenticated_identity || !active->is_ready()) {
        return SteamInventorySnapshotResult::create(ERR_UNAVAILABLE);
    }
    const CSteamID expected(account);
    if (!expected.IsValid() || expected.GetEAccountType() != k_EAccountTypeIndividual) {
        return SteamInventorySnapshotResult::create(ERR_INVALID_PARAMETER);
    }
    const ObjectID identity = authenticated_identity(account);
    if (!identity.is_valid()) {
        return SteamInventorySnapshotResult::create(ERR_UNAUTHORIZED);
    }
    if (!native) {
        auto api = std::make_unique<SteamInventoryVerifierState>();
        const auto get_inventory = active->symbol<decltype(&SteamAPI_SteamGameServerInventory_v003)>(
                "SteamAPI_SteamGameServerInventory_v003");
        const auto get_utils = active->symbol<decltype(&SteamAPI_SteamGameServerUtils_v011)>(
                "SteamAPI_SteamGameServerUtils_v011");
        api->inventory = get_inventory ? get_inventory() : nullptr;
        api->utils = get_utils ? get_utils() : nullptr;
        api->deserialize = active->symbol<decltype(&SteamAPI_ISteamInventory_DeserializeResult)>(
                "SteamAPI_ISteamInventory_DeserializeResult");
        api->destroy = active->symbol<decltype(&SteamAPI_ISteamInventory_DestroyResult)>(
                "SteamAPI_ISteamInventory_DestroyResult");
        api->status = active->symbol<decltype(&SteamAPI_ISteamInventory_GetResultStatus)>(
                "SteamAPI_ISteamInventory_GetResultStatus");
        api->check_owner = active->symbol<decltype(&SteamAPI_ISteamInventory_CheckResultSteamID)>(
                "SteamAPI_ISteamInventory_CheckResultSteamID");
        api->timestamp = active->symbol<decltype(&SteamAPI_ISteamInventory_GetResultTimestamp)>(
                "SteamAPI_ISteamInventory_GetResultTimestamp");
        api->items = active->symbol<decltype(&SteamAPI_ISteamInventory_GetResultItems)>(
                "SteamAPI_ISteamInventory_GetResultItems");
        api->steam_time = active->symbol<decltype(&SteamAPI_ISteamUtils_GetServerRealTime)>(
                "SteamAPI_ISteamUtils_GetServerRealTime");
        if (!api->inventory || !api->utils || !api->deserialize || !api->destroy || !api->status ||
                !api->check_owner || !api->timestamp || !api->items || !api->steam_time) {
            return SteamInventorySnapshotResult::create(ERR_UNAVAILABLE);
        }
        native = std::move(api);
    }
    if (native->pending.size() >= max_pending) {
        return SteamInventorySnapshotResult::create(ERR_BUSY);
    }

    PackedByteArray bytes = proof;
    SteamInventoryResult_t handle = k_SteamInventoryResultInvalid;
    const bool submitted = native->deserialize(native->inventory, &handle, bytes.ptr(),
            static_cast<uint32>(bytes.size()), false);
    const bool tracked = native->pending.count(handle) != 0;
    if (!submitted || handle == k_SteamInventoryResultInvalid || tracked) {
        if (!submitted && handle != k_SteamInventoryResultInvalid && !tracked) {
            native->destroy(native->inventory, handle);
        }
        return SteamInventorySnapshotResult::create(ERR_INVALID_DATA);
    }
    Ref<SteamInventorySnapshotOperation> operation;
    operation.instantiate();
    native->pending.emplace(handle, PendingProof{std::move(bytes), account, identity,
            Time::get_singleton()->get_ticks_msec() + timeout_msec, operation});
    return operation->wait();
#else
    return SteamInventorySnapshotResult::create(ERR_UNAVAILABLE);
#endif
}

Ref<SteamInventorySnapshotResult> SteamInventoryVerifier::read_snapshot(int32_t handle, uint64_t account) const {
#ifdef CRIMSON_WITH_STEAM
    if (!native->check_owner(native->inventory, handle, account)) {
        return SteamInventorySnapshotResult::create(ERR_UNAUTHORIZED);
    }
    const uint32_t timestamp = native->timestamp(native->inventory, handle);
    const uint32_t steam_now = native->steam_time(native->utils);
    if (steam_now == 0) {
        return SteamInventorySnapshotResult::create(ERR_UNAVAILABLE);
    }
    if (timestamp == 0 || timestamp > steam_now) {
        return SteamInventorySnapshotResult::create(ERR_INVALID_DATA);
    }
    if (steam_now - timestamp > max_age_seconds) {
        return SteamInventorySnapshotResult::create(ERR_TIMEOUT,
                "The verified inventory proof is older than five minutes.");
    }
    uint32 count = 0;
    if (!native->items(native->inventory, handle, nullptr, &count) || count == 0 || count > max_proof_items) {
        return SteamInventorySnapshotResult::create(ERR_INVALID_DATA);
    }
    std::vector<SteamItemDetails_t> rows(count);
    if (!native->items(native->inventory, handle, rows.data(), &count) || count != rows.size()) {
        return SteamInventorySnapshotResult::create(ERR_INVALID_DATA);
    }
    TypedArray<SteamItemInstance> items;
    std::set<SteamItemInstanceID_t> unique;
    for (const auto &row : rows) {
        if (row.m_itemId == 0 || row.m_itemId == k_SteamItemInstanceIDInvalid ||
                !unique.insert(row.m_itemId).second || row.m_iDefinition <= 0 || row.m_iDefinition >= 1000000000 ||
                row.m_unQuantity == 0 || (row.m_unFlags & k_ESteamItemRemoved) != 0) {
            return SteamInventorySnapshotResult::create(ERR_INVALID_DATA);
        }
        Ref<SteamItemInstance> item;
        item.instantiate();
        item->instance_id = String::num_uint64(row.m_itemId);
        item->definition_id = row.m_iDefinition;
        item->quantity = row.m_unQuantity;
        item->flags = row.m_unFlags;
        items.push_back(item);
    }
    items.make_read_only();
    Ref<SteamInventorySnapshot> snapshot;
    snapshot.instantiate();
    snapshot->items = items;
    snapshot->freshness = SteamInventorySnapshot::FRESHNESS_VERIFIED_PROOF;
    snapshot->observed_at_unix = timestamp;
    return SteamInventorySnapshotResult::create(OK, {}, snapshot);
#else
    return SteamInventorySnapshotResult::create(ERR_UNAVAILABLE);
#endif
}

void SteamInventoryVerifier::tick() {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread() || !native || native->pending.empty()) {
        return;
    }
    const auto active = runtime.lock();
    if (!active) {
        close();
        return;
    }
    const uint64_t now_msec = Time::get_singleton()->get_ticks_msec();
    std::vector<CompletedProof> completed;
    for (auto entry = native->pending.begin(); entry != native->pending.end();) {
        const auto handle = entry->first;
        const auto &request = entry->second;
        Error error = OK;
        String message;
        Ref<SteamInventorySnapshotResult> result;
        if (!active->is_ready()) {
            error = ERR_UNAVAILABLE;
        } else if (!authenticated_identity || authenticated_identity(request.account) != request.identity) {
            error = ERR_UNAUTHORIZED;
        } else {
            const auto status = native->status(native->inventory, handle);
            if (status == k_EResultPending && now_msec < request.deadline) {
                ++entry;
                continue;
            }
            if (now_msec >= request.deadline || status == k_EResultExpired) {
                error = ERR_TIMEOUT;
                message = status == k_EResultExpired ? "Steam reported a stale inventory proof." :
                        "Steam inventory proof verification timed out.";
            } else if (status != k_EResultOK) {
                error = ERR_INVALID_DATA;
            } else {
                result = read_snapshot(handle, request.account);
            }
        }
        native->destroy(native->inventory, handle);
        if (result.is_null()) {
            result = SteamInventorySnapshotResult::create(error, message);
        }
        completed.push_back({request.account, request.identity, result, request.operation});
        entry = native->pending.erase(entry);
    }
    for (const auto &proof : completed) {
        if (proof.result->is_ok() && (runtime.lock() != active || !active->is_ready() ||
                !authenticated_identity || authenticated_identity(proof.account) != proof.identity)) {
            proof.operation->finish(SteamInventorySnapshotResult::create(ERR_SKIP));
        } else {
            proof.operation->finish(proof.result);
        }
    }
#endif
}

void SteamInventoryVerifier::close() {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread()) {
        return;
    }
    const auto active = runtime.lock();
    auto retired = std::move(native);
    runtime.reset();
    authenticated_identity = {};
    if (!retired) {
        return;
    }
    if (active) {
        for (const auto &entry : retired->pending) {
            retired->destroy(retired->inventory, entry.first);
        }
    }
    for (const auto &entry : retired->pending) {
        entry.second.operation->finish(SteamInventorySnapshotResult::create(ERR_SKIP));
    }
#endif
}

} // namespace crimson::steam
