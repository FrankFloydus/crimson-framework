// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/publisher/steam_ownership.hpp"
#include "modules/steam/publisher/steam_ownership_challenge_store.hpp"
#include "modules/steam/publisher/steam_publisher_codec.hpp"
#include "modules/steam/publisher/steam_publisher_transport.hpp"

#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <string>

#if defined(CRIMSON_WITH_STEAM) && defined(_WIN32) && !defined(CRIMSON_WITH_STEAM_PUBLISHER_TESTS)
#include <steamencryptedappticket.h>
#include <windows.h>
#endif

namespace crimson::steam {

using namespace godot;

namespace {

#if defined(CRIMSON_WITH_STEAM) && defined(_WIN32)
// Bound encrypted and decrypted ticket storage consistently with the Steam client.
constexpr int64_t maximum_ticket_bytes = 16 * 1024;
// Match the challenge bound enforced when issuing the encrypted client ticket.
constexpr int64_t maximum_challenge_bytes = 1024;
// Bound the number of DLC entitlement checks performed on a single ticket.
constexpr int64_t maximum_dlc_ids = 32;
// Steam encrypted app-ticket symmetric keys contain exactly 32 raw bytes.
constexpr int64_t key_bytes = 32;
// Require a newly issued ticket for a one-time challenge.
constexpr int64_t maximum_ticket_age_seconds = 300;
// Allow small wall-clock differences between the issuer and the trusted backend.
constexpr int64_t maximum_future_seconds = 30;

// Internal proof normalized after native decryption or the compile-time test seam.
struct OwnershipProof {
    // Exact AppID carried in the decrypted ticket.
    int64_t app_id = 0;
    // Ticket issue time in Unix seconds.
    int64_t issued_at_unix = 0;
    // Verified numeric Steam account identifier.
    uint64_t steam_id = 0;
    // Whether the ticket explicitly proves the base app entitlement.
    bool app_owned = false;
    // Whether the embedded user data exactly matches the backend challenge.
    bool challenge_matches = false;
    // Entitled DLC IDs within the original requested set.
    PackedInt64Array owned_dlc_ids;
};

// Validate positive, distinct DLC AppIDs that differ from the base app.
bool valid_dlc_ids(const PackedInt64Array &ids, int64_t app_id) {
    if (ids.size() > maximum_dlc_ids) {
        return false;
    }
    std::set<int64_t> seen;
    for (int64_t index = 0; index < ids.size(); ++index) {
        const int64_t id = ids[index];
        if (id <= 0 || id > (std::numeric_limits<uint32_t>::max)() || id == app_id || !seen.insert(id).second) {
            return false;
        }
    }
    return true;
}

// Compare an exact-length backend nonce without revealing a matching prefix.
bool equal_challenge(const uint8_t *embedded, size_t size, const PackedByteArray &expected) {
    if (!embedded || size != static_cast<size_t>(expected.size())) {
        return false;
    }
    const uint8_t *challenge = expected.ptr();
    uint8_t difference = 0;
    for (size_t index = 0; index < size; ++index) {
        difference |= static_cast<uint8_t>(embedded[index] ^ challenge[index]);
    }
    return difference == 0;
}

#ifdef CRIMSON_WITH_STEAM_PUBLISHER_TESTS
// Accept synthetic decrypted metadata only in a test binary with no live transport.
bool read_fixture(const Dictionary &fixture, const PackedByteArray &expected, const PackedInt64Array &requested,
        OwnershipProof &proof) {
    const Variant app_id = fixture.get("app_id", Variant());
    const Variant issued = fixture.get("issued_at_unix", Variant());
    const Variant steam_id = fixture.get("steam_id", Variant());
    const Variant challenge = fixture.get("challenge", Variant());
    const Variant owned_ids = fixture.get("owned_app_ids", Variant());
    if (app_id.get_type() != Variant::INT || issued.get_type() != Variant::INT ||
            steam_id.get_type() != Variant::STRING || challenge.get_type() != Variant::PACKED_BYTE_ARRAY ||
            owned_ids.get_type() != Variant::PACKED_INT64_ARRAY) {
        return false;
    }
    const CharString account = String(steam_id).utf8();
    if (!publisher_codec::unsigned_value(publisher_codec::Json(std::string(account.get_data(), account.length())),
                proof.steam_id)) {
        return false;
    }
    const PackedByteArray embedded = challenge;
    const PackedInt64Array owned = owned_ids;
    proof.app_id = static_cast<int64_t>(app_id);
    proof.issued_at_unix = static_cast<int64_t>(issued);
    proof.challenge_matches = equal_challenge(embedded.ptr(), embedded.size(), expected);
    proof.app_owned = owned.has(proof.app_id);
    for (int64_t index = 0; index < requested.size(); ++index) {
        if (owned.has(requested[index])) {
            proof.owned_dlc_ids.append(requested[index]);
        }
    }
    return true;
}
#else
// Locate the encrypted ticket library beside the exact loaded GDExtension module.
std::wstring ticket_library_path() {
    static const char module_anchor = 0;
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&module_anchor), &module)) {
        return {};
    }
    std::array<wchar_t, 32768> path{};
    const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) {
        return {};
    }
    const std::wstring module_path(path.data(), length);
    const auto directory = module_path.find_last_of(L"/\\");
    return directory == std::wstring::npos ? std::wstring()
            : module_path.substr(0, directory + 1) + L"sdkencryptedappticket64.dll";
}

// Wipe framework-owned native storage even when decryption or validation fails.
template <size_t Size>
struct SensitiveBuffer {
    // Stack bytes controlled and wiped by this verifier.
    std::array<uint8_t, Size> bytes{};

    // Erase the owned buffer with the platform's non-elidable zeroing primitive.
    ~SensitiveBuffer() {
        SecureZeroMemory(bytes.data(), bytes.size());
    }
};

// Retain the vendor library until all decryption and proof accessors finish.
class EncryptedTicketApi {
public:
    // Load the exact local library and resolve every required official export.
    EncryptedTicketApi() {
        const auto path = ticket_library_path();
        if (!path.empty()) {
            library = LoadLibraryExW(path.c_str(), nullptr,
                    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        }
        if (library) {
            decrypt = symbol<decltype(decrypt)>("SteamEncryptedAppTicket_BDecryptTicket");
            get_app_id = symbol<decltype(get_app_id)>("SteamEncryptedAppTicket_GetTicketAppID");
            get_issued = symbol<decltype(get_issued)>("SteamEncryptedAppTicket_GetTicketIssueTime");
            get_steam_id = symbol<decltype(get_steam_id)>("SteamEncryptedAppTicket_GetTicketSteamID");
            owns = symbol<decltype(owns)>("SteamEncryptedAppTicket_BUserOwnsAppInTicket");
            get_data = symbol<decltype(get_data)>("SteamEncryptedAppTicket_GetUserVariableData");
        }
    }

    // Retire this library lease after the last SDK call in this verification.
    ~EncryptedTicketApi() {
        if (library) {
            FreeLibrary(library);
        }
    }

    // Prevent copies from releasing the same library twice.
    EncryptedTicketApi(const EncryptedTicketApi &) = delete;
    // Prevent assignment from copying the native library handle.
    EncryptedTicketApi &operator=(const EncryptedTicketApi &) = delete;

    // Require the complete native interface before resolving a secret.
    bool available() const {
        return decrypt && get_app_id && get_issued && get_steam_id && owns && get_data;
    }

    // Read the normalized proof and erase private plaintext and key buffers on return.
    bool read(const PackedByteArray &ticket, const PackedByteArray &key, const PackedByteArray &challenge,
            const PackedInt64Array &requested, OwnershipProof &proof) const {
        static_assert(key_bytes == k_nSteamEncryptedAppTicketSymmetricKeyLen);
        SensitiveBuffer<key_bytes> native_key;
        SensitiveBuffer<maximum_ticket_bytes> plaintext;
        std::memcpy(native_key.bytes.data(), key.ptr(), native_key.bytes.size());
        uint32 decrypted_size = static_cast<uint32>(plaintext.bytes.size());
        auto *decrypted = plaintext.bytes.data();
        if (!decrypt(ticket.ptr(), static_cast<uint32>(ticket.size()), decrypted, &decrypted_size,
                    native_key.bytes.data(), static_cast<int>(native_key.bytes.size())) ||
                decrypted_size == 0 || decrypted_size > plaintext.bytes.size()) {
            return false;
        }
        proof.app_id = get_app_id(decrypted, decrypted_size);
        proof.issued_at_unix = get_issued(decrypted, decrypted_size);
        CSteamID account;
        get_steam_id(decrypted, decrypted_size, &account);
        proof.steam_id = account.ConvertToUint64();
        proof.app_owned = owns(decrypted, decrypted_size, static_cast<AppId_t>(proof.app_id));
        uint32 embedded_size = 0;
        const uint8 *embedded = get_data(decrypted, decrypted_size, &embedded_size);
        const uintptr_t begin = reinterpret_cast<uintptr_t>(decrypted);
        const uintptr_t address = reinterpret_cast<uintptr_t>(embedded);
        if (embedded && address >= begin && address - begin <= decrypted_size &&
                embedded_size <= decrypted_size - (address - begin)) {
            proof.challenge_matches = equal_challenge(embedded, embedded_size, challenge);
        }
        for (int64_t index = 0; index < requested.size(); ++index) {
            if (owns(decrypted, decrypted_size, static_cast<AppId_t>(requested[index]))) {
                proof.owned_dlc_ids.append(requested[index]);
            }
        }
        return true;
    }

private:
    // Resolve a named export only from the retained vendor library.
    template <typename Function>
    Function symbol(const char *name) const {
        return reinterpret_cast<Function>(GetProcAddress(library, name));
    }

    // Owned library handle for this local verification.
    HMODULE library = nullptr;
    // Decrypt and validate the encrypted SDK ticket.
    decltype(&SteamEncryptedAppTicket_BDecryptTicket) decrypt = nullptr;
    // Read the verified ticket's AppID.
    decltype(&SteamEncryptedAppTicket_GetTicketAppID) get_app_id = nullptr;
    // Read the verified ticket's issue time.
    decltype(&SteamEncryptedAppTicket_GetTicketIssueTime) get_issued = nullptr;
    // Read the verified ticket's Steam account ID.
    decltype(&SteamEncryptedAppTicket_GetTicketSteamID) get_steam_id = nullptr;
    // Read app and DLC ownership from the verified ticket.
    decltype(&SteamEncryptedAppTicket_BUserOwnsAppInTicket) owns = nullptr;
    // Borrow the challenge bytes inside the owned decrypted buffer.
    decltype(&SteamEncryptedAppTicket_GetUserVariableData) get_data = nullptr;
};
#endif
#endif

} // namespace

void SteamOwnership::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_identity"), &SteamOwnership::get_identity);
    ClassDB::bind_method(D_METHOD("owns_app"), &SteamOwnership::owns_app);
    ClassDB::bind_method(D_METHOD("get_issued_at_unix"), &SteamOwnership::get_issued_at_unix);
    ClassDB::bind_method(D_METHOD("get_checked_dlc_ids"), &SteamOwnership::get_checked_dlc_ids);
    ClassDB::bind_method(D_METHOD("get_owned_dlc_ids"), &SteamOwnership::get_owned_dlc_ids);
    ClassDB::bind_method(D_METHOD("owns_dlc", "app_id"), &SteamOwnership::owns_dlc);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "identity", PROPERTY_HINT_RESOURCE_TYPE, "SteamIdentity",
                         PROPERTY_USAGE_READ_ONLY), "", "get_identity");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "app_owned", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "owns_app");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "issued_at_unix", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_issued_at_unix");
    ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT64_ARRAY, "checked_dlc_ids", PROPERTY_HINT_NONE, "",
                         PROPERTY_USAGE_READ_ONLY), "", "get_checked_dlc_ids");
    ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT64_ARRAY, "owned_dlc_ids", PROPERTY_HINT_NONE, "",
                         PROPERTY_USAGE_READ_ONLY), "", "get_owned_dlc_ids");
}

Ref<SteamIdentity> SteamOwnership::get_identity() const {
    return identity;
}

bool SteamOwnership::owns_app() const {
    return app_owned;
}

int64_t SteamOwnership::get_issued_at_unix() const {
    return issued_at_unix;
}

PackedInt64Array SteamOwnership::get_checked_dlc_ids() const {
    return checked_dlc_ids;
}

PackedInt64Array SteamOwnership::get_owned_dlc_ids() const {
    return owned_dlc_ids;
}

bool SteamOwnership::owns_dlc(int64_t app_id) const {
    return owned_dlc_ids.has(app_id);
}

void SteamOwnershipResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamOwnershipResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE, "SteamOwnership",
                         PROPERTY_USAGE_READ_ONLY), "", "get_value");
}

Ref<SteamOwnership> SteamOwnershipResult::get_value() const {
    return value;
}

Ref<SteamOwnershipResult> SteamOwnershipResult::create(Error error, const Ref<SteamOwnership> &receipt) {
    Ref<SteamOwnershipResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value = receipt;
    return result;
}

Ref<SteamOwnershipResult> SteamPublisherAuth::verify_ownership(const PackedByteArray &ticket_bytes,
        const PackedByteArray &challenge, const PackedInt64Array &dlc_ids) {
#if defined(CRIMSON_WITH_STEAM) && defined(_WIN32)
    const auto owner = publisher_on_main_thread() ? scope.lock() : nullptr;
    auto *host = SteamPublisherHost::resolve(owner);
    if (!host) {
        return SteamOwnershipResult::create(ERR_UNAVAILABLE);
    }
    if (ticket_bytes.is_empty() || ticket_bytes.size() > maximum_ticket_bytes || challenge.is_empty() ||
            challenge.size() > maximum_challenge_bytes || !valid_dlc_ids(dlc_ids, owner->app_id)) {
        return SteamOwnershipResult::create(ERR_INVALID_PARAMETER);
    }
#ifndef CRIMSON_WITH_STEAM_PUBLISHER_TESTS
    const EncryptedTicketApi sdk;
    if (!sdk.available()) {
        return SteamOwnershipResult::create(ERR_UNAVAILABLE);
    }
#endif
    const auto store = owner->ownership_challenge_store;
    if (store.is_null() || owner->encrypted_ticket_key_ref.is_empty()) {
        return SteamOwnershipResult::create(ERR_UNCONFIGURED);
    }
    const auto ticket = ticket_bytes.duplicate();
    const auto expected = challenge.duplicate();
    const auto requested = dlc_ids.duplicate();
    PackedByteArray key;
    if (!host->GDVIRTUAL_CALL(_resolve_encrypted_ticket_key, owner->encrypted_ticket_key_ref, key) ||
            key.size() != key_bytes) {
        return SteamOwnershipResult::create(ERR_UNCONFIGURED);
    }
    host = SteamPublisherHost::resolve(owner);
    if (!host) {
        return SteamOwnershipResult::create(ERR_SKIP);
    }

    OwnershipProof proof;
#ifdef CRIMSON_WITH_STEAM_PUBLISHER_TESTS
    key = PackedByteArray();
    Dictionary fixture;
    const bool decoded = host->GDVIRTUAL_CALL(_test_decrypted_ownership_ticket, ticket, fixture) &&
            read_fixture(fixture, expected, requested, proof);
#else
    const bool decoded = sdk.read(ticket, key, expected, requested, proof);
    key = PackedByteArray();
#endif
    if (!SteamPublisherHost::resolve(owner)) {
        return SteamOwnershipResult::create(ERR_SKIP);
    }
    const double clock = Time::get_singleton()->get_unix_time_from_system();
    if (!std::isfinite(clock) || clock <= 0.0 || clock > (std::numeric_limits<uint32_t>::max)()) {
        return SteamOwnershipResult::create(ERR_UNAVAILABLE);
    }
    const int64_t now = static_cast<int64_t>(clock);
    if (!decoded || proof.app_id != owner->app_id || !proof.app_owned || !proof.challenge_matches ||
            !publisher_codec::individual_id(proof.steam_id) || proof.issued_at_unix <= 0 ||
            proof.issued_at_unix > (std::numeric_limits<uint32_t>::max)() ||
            proof.issued_at_unix < now - maximum_ticket_age_seconds || proof.issued_at_unix > now + maximum_future_seconds) {
        return SteamOwnershipResult::create(ERR_UNAUTHORIZED);
    }
    const String account = String::utf8(std::to_string(proof.steam_id).c_str());
    const bool consumed = store->consume(owner->app_id, expected, account, proof.issued_at_unix, now);
    if (!SteamPublisherHost::resolve(owner)) {
        return SteamOwnershipResult::create(ERR_SKIP);
    }
    if (!consumed) {
        return SteamOwnershipResult::create(ERR_UNAUTHORIZED);
    }

    Ref<SteamIdentity> identity;
    identity.instantiate();
    identity->steam_id = account;
    identity->app_id = owner->app_id;
    identity->issuer = owner;
    Ref<SteamOwnership> receipt;
    receipt.instantiate();
    receipt->identity = identity;
    receipt->app_owned = true;
    receipt->issued_at_unix = proof.issued_at_unix;
    receipt->checked_dlc_ids = requested;
    receipt->owned_dlc_ids = proof.owned_dlc_ids;
    return SteamOwnershipResult::create(OK, receipt);
#else
    return SteamOwnershipResult::create(ERR_UNAVAILABLE);
#endif
}

} // namespace crimson::steam
