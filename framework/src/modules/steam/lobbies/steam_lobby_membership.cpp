// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/lobbies/steam_lobbies.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/signal.hpp>

#include <utility>

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/client/steam_client_scope.hpp"
#include "modules/steam/lobbies/steam_lobby_lease.hpp"
#include "modules/steam/runtime/steam_runtime.hpp"

#include <godot_cpp/classes/time.hpp>

#include <cstring>
#include <string>
#endif

namespace crimson::steam {
using namespace godot;

namespace {

// Reject embedded controls before crossing the C-string boundary.
bool valid_text(const String &value, int64_t maximum, bool allow_empty) {
    if ((!allow_empty && value.is_empty()) || value.length() > maximum || value.utf8().length() > maximum) {
        return false;
    }
    for (int64_t index = 0; index < value.length(); ++index) {
        if (value[index] < U' ' || value[index] == 127) {
            return false;
        }
    }
    return true;
}

// Keep all public custom metadata disjoint from the discovery and route keys.
bool valid_tag(const String &key) {
    return key.length() <= 32 && valid_steam_lobby_namespace(key) && !key.to_lower().begins_with("crimson_");
}

#ifdef CRIMSON_WITH_STEAM

// Copy a bounded and valid SDK string immediately.
String sdk_text(const char *value, int64_t maximum) {
    if (!value) {
        return {};
    }
    int64_t length = 0;
    while (length <= maximum && value[length] != '\0') {
        ++length;
    }
    String result;
    return length <= maximum && result.parse_utf8(value, length) == OK ? result : String();
}

// Check the exact Steam account type used by ordinary lobby membership.
bool is_individual(CSteamID id) {
    return id.IsValid() && id.GetEAccountType() == k_EAccountTypeIndividual;
}

// Check a single SDK identity against the current bounded lobby roster.
bool contains_member(ISteamMatchmaking &api, CSteamID lobby, CSteamID account) {
    const int32_t count = api.GetNumLobbyMembers(lobby);
    if (count < 1 || count > 65) {
        return false;
    }
    for (int32_t index = 0; index < count; ++index) {
        if (api.GetLobbyMemberByIndex(lobby, index) == account) {
            return true;
        }
    }
    return false;
}

// Decode a native create or enter completion before handing membership to a scope.
uint64_t acquired_lobby(const void *data, bool created) {
    if (!data) {
        return 0;
    }
    uint64_t id = 0;
    if (created) {
        LobbyCreated_t event{};
        std::memcpy(&event, data, sizeof(event));
        if (event.m_eResult == k_EResultOK) {
            id = event.m_ulSteamIDLobby;
        }
    } else {
        LobbyEnter_t event{};
        std::memcpy(&event, data, sizeof(event));
        if (event.m_EChatRoomEnterResponse == k_EChatRoomEnterResponseSuccess) {
            id = event.m_ulSteamIDLobby;
        }
    }
    const CSteamID native(id);
    return native.IsValid() && native.IsLobby() ? id : 0;
}

#endif

} // namespace

void SteamLobbyOptions::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("configure"), &SteamLobbyOptions::configure);
    ClassDB::bind_method(D_METHOD("with_visibility", "visibility"), &SteamLobbyOptions::with_visibility);
    ClassDB::bind_method(D_METHOD("in_namespace", "namespace"), &SteamLobbyOptions::in_namespace);
    ClassDB::bind_method(D_METHOD("for_players", "capacity"), &SteamLobbyOptions::for_players);
    BIND_ENUM_CONSTANT(PRIVATE);
    BIND_ENUM_CONSTANT(FRIENDS_ONLY);
    BIND_ENUM_CONSTANT(PUBLIC);
    BIND_ENUM_CONSTANT(INVISIBLE);
}

Ref<SteamLobbyOptions> SteamLobbyOptions::configure() {
    Ref<SteamLobbyOptions> options;
    options.instantiate();
    return options;
}

Ref<SteamLobbyOptions> SteamLobbyOptions::branch() const {
    const auto next = configure();
    next->valid = valid;
    next->visibility = visibility;
    next->name_space = name_space;
    next->capacity = capacity;
    return next;
}

Ref<SteamLobbyOptions> SteamLobbyOptions::with_visibility(Visibility value) const {
    const auto next = branch();
    next->valid = valid && value >= PRIVATE && value <= INVISIBLE;
    next->visibility = value;
    return next;
}

Ref<SteamLobbyOptions> SteamLobbyOptions::in_namespace(const StringName &value) const {
    const auto next = branch();
    next->valid = valid && valid_steam_lobby_namespace(String(value));
    next->name_space = value;
    return next;
}

Ref<SteamLobbyOptions> SteamLobbyOptions::for_players(int64_t value) const {
    const auto next = branch();
    next->valid = valid && value >= 1 && value <= 65;
    if (next->valid) {
        next->capacity = static_cast<int32_t>(value);
    }
    return next;
}

void SteamLobbyMember::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_steam_id"), &SteamLobbyMember::get_steam_id);
    ClassDB::bind_method(D_METHOD("get_display_name"), &SteamLobbyMember::get_display_name);
    ClassDB::bind_method(D_METHOD("get_is_owner"), &SteamLobbyMember::is_owner);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "steam_id"), "", "get_steam_id");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "display_name"), "", "get_display_name");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_owner"), "", "get_is_owner");
}

String SteamLobbyMember::get_steam_id() const { return steam_id; }
String SteamLobbyMember::get_display_name() const { return display_name; }
bool SteamLobbyMember::is_owner() const { return owner; }

void SteamLobbyEdit::_bind_methods() {
    ClassDB::bind_method(D_METHOD("named", "name"), &SteamLobbyEdit::named);
    ClassDB::bind_method(D_METHOD("tag", "key", "value"), &SteamLobbyEdit::tag);
    ClassDB::bind_method(D_METHOD("submit"), &SteamLobbyEdit::submit);
}

Ref<SteamLobbyEdit> SteamLobbyEdit::branch() const {
    Ref<SteamLobbyEdit> next;
    next.instantiate();
    next->lobby_id = lobby_id;
    next->updates = updates;
    next->valid = valid;
    return next;
}

Ref<SteamLobbyEdit> SteamLobbyEdit::named(const String &name) const {
    const auto next = branch();
    next->valid = valid && valid_text(name, 127, true) &&
            (updates.size() < 16 || updates.find("crimson_name") != updates.end());
    if (next->valid) {
        next->updates["crimson_name"] = name;
    }
    return next;
}

Ref<SteamLobbyEdit> SteamLobbyEdit::tag(const StringName &key, const String &value) const {
    const auto next = branch();
    const String name(key);
    next->valid = valid && valid_tag(name) && valid_text(value, 255, true) &&
            (updates.size() < 16 || updates.find(name) != updates.end());
    if (next->valid) {
        next->updates[name] = value;
    }
    return next;
}

Error SteamLobbyEdit::submit() const {
    if (!valid || updates.empty()) {
        return ERR_INVALID_PARAMETER;
    }
#ifdef CRIMSON_WITH_STEAM
    if (SteamRuntime::on_main_thread()) {
        const Ref<SteamLobby> lobby(Object::cast_to<SteamLobby>(ObjectDB::get_instance(lobby_id)));
        if (lobby.is_valid()) {
            const auto sdk = lobby->runtime.lock();
            const uint64_t id = lobby->live_lobby(sdk);
            if (id == 0) {
                return ERR_UNAVAILABLE;
            }
            auto *api = sdk->get_matchmaking();
            const CSteamID native_lobby(id);
            if (api->GetLobbyOwner(native_lobby).ConvertToUint64() != sdk->get_user_id()) {
                return ERR_UNAUTHORIZED;
            }
            for (const auto &[key, value] : updates) {
                const CharString native_key = key.utf8();
                const char *previous = value.is_empty() ? api->GetLobbyData(native_lobby, native_key.get_data()) : nullptr;
                const bool submitted = value.is_empty() ?
                        !previous || previous[0] == '\0' || api->DeleteLobbyData(native_lobby, native_key.get_data()) :
                        api->SetLobbyData(native_lobby, native_key.get_data(), value.utf8().get_data());
                if (!submitted) {
                    return ERR_CANT_CONNECT;
                }
            }
            return OK;
        }
    }
#endif
    return ERR_UNAVAILABLE;
}

void SteamLobby::_bind_methods() {
    ClassDB::bind_method(D_METHOD("listing"), &SteamLobby::listing);
    ClassDB::bind_method(D_METHOD("members"), &SteamLobby::members);
    ClassDB::bind_method(D_METHOD("edit"), &SteamLobby::edit);
    ClassDB::bind_method(D_METHOD("tag", "key"), &SteamLobby::tag);
    ClassDB::bind_method(D_METHOD("set_joinable", "open"), &SteamLobby::set_joinable);
    ClassDB::bind_method(D_METHOD("leave"), &SteamLobby::leave);
    ClassDB::bind_method(D_METHOD("invite", "steam_id"), &SteamLobby::invite);
    ClassDB::bind_method(D_METHOD("open_invite_dialog"), &SteamLobby::open_invite_dialog);
    ClassDB::bind_method(D_METHOD("send_chat", "text"), &SteamLobby::send_chat);
    // Publish a fresh bounded member list after an exact Steam membership event.
    ADD_SIGNAL(MethodInfo("members_changed", PropertyInfo(Variant::ARRAY, "members", PROPERTY_HINT_ARRAY_TYPE,
                                               "SteamLobbyMember")));
    // Publish valid, bounded text from a member of this exact Steam lobby.
    ADD_SIGNAL(MethodInfo("chat_received",
            PropertyInfo(Variant::OBJECT, "member", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT, "SteamLobbyMember"),
            PropertyInfo(Variant::STRING, "text")));
    // Publish retirement after membership has become unavailable.
    ADD_SIGNAL(MethodInfo("closed", PropertyInfo(Variant::INT, "reason")));
}

uint64_t SteamLobby::live_lobby(const std::shared_ptr<SteamRuntime> &sdk) const {
#ifdef CRIMSON_WITH_STEAM
    if (SteamRuntime::on_main_thread() && sdk && sdk->get_matchmaking()) {
        const auto attached = scope.lock();
        const auto membership = lease.lock();
        if (attached && attached->active && membership) {
            return membership->current();
        }
    }
#endif
    return 0;
}

Ref<SteamListing> SteamLobby::listing() const {
#ifdef CRIMSON_WITH_STEAM
    const auto sdk = runtime.lock();
    const uint64_t id = live_lobby(sdk);
    if (id != 0 && snapshot.is_valid()) {
        auto *api = sdk->get_matchmaking();
        const CSteamID lobby(id);
        return SteamListing::lobby(id, sdk->get_app_id(), snapshot->get_namespace(), snapshot->get_world_id(),
                sdk_text(api->GetLobbyData(lobby, "crimson_name"), 127), api->GetNumLobbyMembers(lobby),
                api->GetLobbyMemberLimit(lobby), snapshot->is_social());
    }
#endif
    return snapshot;
}

Ref<SteamLobbyMember> SteamLobby::member(const std::shared_ptr<SteamRuntime> &sdk, uint64_t id, uint64_t owner) const {
#ifdef CRIMSON_WITH_STEAM
    const CSteamID account(id);
    if (is_individual(account)) {
        Ref<SteamLobbyMember> snapshot;
        snapshot.instantiate();
        snapshot->steam_id = String(std::to_string(id).c_str());
        auto *friends = sdk->get_friends();
        snapshot->display_name = friends ? sdk_text(friends->GetFriendPersonaName(account), 127) : String();
        snapshot->owner = id == owner;
        return snapshot;
    }
#endif
    return {};
}

TypedArray<SteamLobbyMember> SteamLobby::members() const {
    TypedArray<SteamLobbyMember> result;
#ifdef CRIMSON_WITH_STEAM
    const auto sdk = runtime.lock();
    const uint64_t id = live_lobby(sdk);
    if (id != 0) {
        auto *api = sdk->get_matchmaking();
        const CSteamID lobby(id);
        const int32_t count = api->GetNumLobbyMembers(lobby);
        const uint64_t owner = api->GetLobbyOwner(lobby).ConvertToUint64();
        if (count < 1 || count > 65) {
            return result;
        }
        for (int32_t index = 0; index < count; ++index) {
            const auto current = member(sdk, api->GetLobbyMemberByIndex(lobby, index).ConvertToUint64(), owner);
            if (current.is_valid()) {
                result.push_back(current);
            }
        }
    }
#endif
    return result;
}

Ref<SteamLobbyEdit> SteamLobby::edit() const {
    Ref<SteamLobbyEdit> draft;
    draft.instantiate();
    draft->lobby_id = ObjectID(get_instance_id());
    return draft;
}

String SteamLobby::tag(const StringName &key) const {
#ifdef CRIMSON_WITH_STEAM
    const auto sdk = runtime.lock();
    const uint64_t id = live_lobby(sdk);
    if (id != 0 && valid_tag(String(key))) {
        return sdk_text(sdk->get_matchmaking()->GetLobbyData(CSteamID(id), String(key).utf8().get_data()), 255);
    }
#endif
    return {};
}

Error SteamLobby::set_joinable(bool open) {
#ifdef CRIMSON_WITH_STEAM
    const auto sdk = runtime.lock();
    const uint64_t id = live_lobby(sdk);
    if (id == 0) {
        return ERR_UNAVAILABLE;
    }
    auto *api = sdk->get_matchmaking();
    if (game_managed || api->GetLobbyOwner(CSteamID(id)).ConvertToUint64() != sdk->get_user_id()) {
        return ERR_UNAUTHORIZED;
    }
    return api->SetLobbyJoinable(CSteamID(id), open) ? OK : ERR_CANT_CONNECT;
#else
    return ERR_UNAVAILABLE;
#endif
}

Error SteamLobby::leave() {
#ifdef CRIMSON_WITH_STEAM
    if (live_lobby(runtime.lock()) == 0) {
        return ERR_UNAVAILABLE;
    }
    if (game_managed) {
        return ERR_UNAUTHORIZED;
    }
    const Ref<SteamLobbies> service(Object::cast_to<SteamLobbies>(ObjectDB::get_instance(service_id)));
    if (service.is_valid()) {
        return service->retire_lobby(this, OK);
    }
#endif
    return ERR_UNAVAILABLE;
}

Error SteamLobby::invite(const String &steam_id) {
#ifdef CRIMSON_WITH_STEAM
    const auto sdk = runtime.lock();
    const uint64_t id = live_lobby(sdk);
    if (id == 0) {
        return ERR_UNAVAILABLE;
    }
    uint64_t recipient = 0;
    if (!parse_steam_lobby_account(steam_id, recipient) || recipient == sdk->get_user_id()) {
        return ERR_INVALID_PARAMETER;
    }
    auto *api = sdk->get_matchmaking();
    if (api->GetLobbyOwner(CSteamID(id)).ConvertToUint64() != sdk->get_user_id()) {
        return ERR_UNAUTHORIZED;
    }
    return api->InviteUserToLobby(CSteamID(id), CSteamID(recipient)) ? OK : ERR_CANT_CONNECT;
#else
    return ERR_UNAVAILABLE;
#endif
}

Error SteamLobby::open_invite_dialog() {
#ifdef CRIMSON_WITH_STEAM
    const auto sdk = runtime.lock();
    const uint64_t id = live_lobby(sdk);
    if (id == 0 || !sdk->get_friends() || !sdk->get_utils()) {
        return ERR_UNAVAILABLE;
    }
    if (sdk->get_matchmaking()->GetLobbyOwner(CSteamID(id)).ConvertToUint64() != sdk->get_user_id()) {
        return ERR_UNAUTHORIZED;
    }
    if (!sdk->get_utils()->IsOverlayEnabled()) {
        return ERR_BUSY;
    }
    sdk->get_friends()->ActivateGameOverlayInviteDialog(CSteamID(id));
    return OK;
#else
    return ERR_UNAVAILABLE;
#endif
}

Error SteamLobby::send_chat(const String &text) {
    if (!valid_text(text, 1024, false)) {
        return ERR_INVALID_PARAMETER;
    }
#ifdef CRIMSON_WITH_STEAM
    const auto sdk = runtime.lock();
    const uint64_t id = live_lobby(sdk);
    if (id == 0) {
        return ERR_UNAVAILABLE;
    }
    const uint64_t now = Time::get_singleton()->get_ticks_msec();
    if (last_sent_ms != 0 && now - last_sent_ms < 250) {
        return ERR_BUSY;
    }
    const CharString utf8 = text.utf8();
    if (!sdk->get_matchmaking()->SendLobbyChatMsg(CSteamID(id), utf8.get_data(), static_cast<int>(utf8.length()))) {
        return ERR_CANT_CONNECT;
    }
    last_sent_ms = now;
    return OK;
#else
    return ERR_UNAVAILABLE;
#endif
}

void SteamLobby::on_membership(const void *data, size_t size) {
#ifdef CRIMSON_WITH_STEAM
    const auto sdk = runtime.lock();
    const uint64_t id = live_lobby(sdk);
    if (id == 0 || !data || size != sizeof(LobbyChatUpdate_t)) {
        return;
    }
    LobbyChatUpdate_t event{};
    std::memcpy(&event, data, sizeof(event));
    if (event.m_ulSteamIDLobby != id) {
        return;
    }
    constexpr uint32_t departed = k_EChatMemberStateChangeLeft | k_EChatMemberStateChangeDisconnected |
            k_EChatMemberStateChangeKicked | k_EChatMemberStateChangeBanned;
    if (event.m_ulSteamIDUserChanged == sdk->get_user_id() && (event.m_rgfChatMemberStateChange & departed)) {
        // A delayed callback for a previous membership must not retire a current rejoin.
        if (contains_member(*sdk->get_matchmaking(), CSteamID(id), CSteamID(sdk->get_user_id()))) {
            return;
        }
        const Ref<SteamLobbies> service(Object::cast_to<SteamLobbies>(ObjectDB::get_instance(service_id)));
        if (service.is_valid()) {
            service->retire_lobby(this, ERR_CONNECTION_ERROR);
        }
        return;
    }
    emit_signal("members_changed", members());
#endif
}

void SteamLobby::on_chat(const void *data, size_t size) {
#ifdef CRIMSON_WITH_STEAM
    const auto sdk = runtime.lock();
    const uint64_t id = live_lobby(sdk);
    if (id == 0 || !data || size != sizeof(SteamLobbyChatRecord)) {
        return;
    }
    const auto &message = *static_cast<const SteamLobbyChatRecord *>(data);
    if (message.lobby != id || message.size <= 0 || message.size > 1024 ||
            std::memchr(message.bytes.data(), 0, message.size)) {
        return;
    }
    const uint64_t now = Time::get_singleton()->get_ticks_msec();
    if (now - chat_window_ms >= 1000) {
        chat_window_ms = now;
        chat_events = 0;
    }
    if (chat_events >= 20) {
        return;
    }
    ++chat_events;

    auto *api = sdk->get_matchmaking();
    const CSteamID lobby(id);
    const CSteamID actual_sender(message.sender);
    if (!contains_member(*api, lobby, actual_sender)) {
        return;
    }
    String text;
    const auto sender = member(sdk, actual_sender.ConvertToUint64(), api->GetLobbyOwner(lobby).ConvertToUint64());
    if (sender.is_valid() && text.parse_utf8(message.bytes.data(), message.size) == OK && valid_text(text, 1024, false)) {
        emit_signal("chat_received", sender, text);
    }
#endif
}

void SteamLobbyResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamLobbyResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_NONE, "",
                         PROPERTY_USAGE_READ_ONLY | PROPERTY_USAGE_NO_INSTANCE_STATE, "SteamLobby"), "", "get_value");
}

Ref<SteamLobby> SteamLobbyResult::get_value() const { return value; }

void SteamLobbyOperation::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_finished"), &SteamLobbyOperation::is_finished);
    ClassDB::bind_method(D_METHOD("result"), &SteamLobbyOperation::result);
    ClassDB::bind_method(D_METHOD("wait"), &SteamLobbyOperation::wait);
    ClassDB::bind_method(D_METHOD("cancel"), &SteamLobbyOperation::cancel);
    // Publish one typed result after native ownership is transferred or retired.
    ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::OBJECT, "result", PROPERTY_HINT_NONE, "",
                                              PROPERTY_USAGE_DEFAULT, "SteamLobbyResult")));
}

bool SteamLobbyOperation::is_finished() const { return terminal_result.is_valid(); }
Ref<SteamLobbyResult> SteamLobbyOperation::result() const { return terminal_result; }

Variant SteamLobbyOperation::wait() {
    return terminal_result.is_valid() ? Variant(terminal_result) : Variant(Signal(this, "completed"));
}

Error SteamLobbyOperation::cancel() {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread()) {
        return ERR_BUSY;
    }
    const Ref<SteamLobbies> service(Object::cast_to<SteamLobbies>(ObjectDB::get_instance(service_id)));
    if (service.is_valid()) {
        return service->cancel_membership(this);
    }
#endif
    return ERR_DOES_NOT_EXIST;
}

Ref<SteamLobbyOperation> SteamLobbyOperation::rejected(Error error) {
    Ref<SteamLobbyOperation> operation;
    operation.instantiate();
    operation->finish(error);
    return operation;
}

void SteamLobbyOperation::finish(Error error, const Ref<SteamLobby> &lobby) {
    if (terminal_result.is_null()) {
        const Ref<SteamLobbyOperation> keep_alive(this);
        service_id = ObjectID();
        terminal_result.instantiate();
        terminal_result->set_outcome(error);
        terminal_result->value = lobby;
        emit_signal("completed", terminal_result);
    }
}

void SteamLobbies::attach(const std::shared_ptr<SteamRuntime> &sdk, const std::shared_ptr<SteamClientScope> &attachment) {
    runtime = sdk;
    scope = attachment;
#ifdef CRIMSON_WITH_STEAM
    if (!is_available()) {
        return;
    }
    const ObjectID self(get_instance_id());
    if (!game_managed) {
        invite_subscription = sdk->subscribe(GameLobbyJoinRequested_t::k_iCallback, [self](const void *data, size_t size) {
            const Ref<SteamLobbies> service(Object::cast_to<SteamLobbies>(ObjectDB::get_instance(self)));
            if (service.is_valid()) {
                service->on_invite(data, size);
            }
        });
    }
    members_subscription = sdk->subscribe(LobbyChatUpdate_t::k_iCallback, [self](const void *data, size_t size) {
        const Ref<SteamLobbies> service(Object::cast_to<SteamLobbies>(ObjectDB::get_instance(self)));
        const auto lobby = service.is_valid() ? service->active_lobby : Ref<SteamLobby>();
        if (lobby.is_valid()) {
            lobby->on_membership(data, size);
        }
    });
    chat_subscription = sdk->subscribe(LobbyChatMsg_t::k_iCallback, [self](const void *data, size_t size) {
        const Ref<SteamLobbies> service(Object::cast_to<SteamLobbies>(ObjectDB::get_instance(self)));
        const auto lobby = service.is_valid() ? service->active_lobby : Ref<SteamLobby>();
        if (lobby.is_valid()) {
            lobby->on_chat(data, size);
        }
    });
#endif
}

void SteamLobbies::configure_game(const StringName &selected_namespace) {
    game_managed = true;
    name_space = selected_namespace;
}

void SteamLobbies::detach() {
    const auto sdk = runtime.lock();
    scope.reset();
    runtime.reset();
    const auto operation = std::exchange(pending_operation, {});
    // The Game closes its view after retiring the native membership it owns.
    const auto lobby = game_managed ? Ref<SteamLobby>() : std::exchange(active_lobby, {});
#ifdef CRIMSON_WITH_STEAM
    if (sdk) {
        sdk->unsubscribe(std::exchange(invite_subscription, 0));
        sdk->unsubscribe(std::exchange(members_subscription, 0));
        sdk->unsubscribe(std::exchange(chat_subscription, 0));
    }
    const auto membership = std::exchange(social_lease, {});
    if (membership) {
        membership->retire();
    }
#endif
    cancel_search();
    if (operation.is_valid()) {
        operation->finish(ERR_SKIP);
    }
    if (lobby.is_valid()) {
        lobby->emit_signal("closed", ERR_SKIP);
    }
}

void SteamLobbies::tick() {
#ifdef CRIMSON_WITH_STEAM
    if (pending_operation.is_valid() && Time::get_singleton()->get_ticks_msec() >= membership_deadline_ms) {
        cancel_membership(pending_operation.ptr(), ERR_TIMEOUT);
    }
#endif
}

Ref<SteamLobbyOperation> SteamLobbies::create(const Ref<SteamLobbyOptions> &options) {
    const auto selected = options.is_valid() ? options : SteamLobbyOptions::configure();
    if (!selected->valid) {
        return SteamLobbyOperation::rejected(ERR_INVALID_PARAMETER);
    }
    return start_membership(selected, 0, selected->name_space);
}

Ref<SteamLobbyOperation> SteamLobbies::join(const Ref<SteamListing> &listing) {
    if (!is_available()) {
        return SteamLobbyOperation::rejected(ERR_UNAVAILABLE);
    }
#ifdef CRIMSON_WITH_STEAM
    const auto sdk = runtime.lock();
    uint64_t target = 0;
    // A populated gameplay world means discovery already identified this as a Game route.
    if (listing.is_null() || listing->get_kind() != SteamListing::LOBBY || listing->get_app_id() != sdk->get_app_id() ||
            (!listing->is_social() && !listing->get_world_id().is_empty()) ||
            (!listing->get_namespace().is_empty() && !valid_steam_lobby_namespace(String(listing->get_namespace()))) ||
            !parse_steam_lobby_id(listing->get_id(), target)) {
        return SteamLobbyOperation::rejected(ERR_INVALID_PARAMETER);
    }
    return start_membership({}, target, listing->get_namespace());
#else
    return SteamLobbyOperation::rejected(ERR_UNAVAILABLE);
#endif
}

Ref<SteamLobbyOperation> SteamLobbies::start_membership(const Ref<SteamLobbyOptions> &options,
        uint64_t target, const StringName &selected_namespace) {
    if (!is_available()) {
        return SteamLobbyOperation::rejected(ERR_UNAVAILABLE);
    }
    if (game_managed) {
        return SteamLobbyOperation::rejected(ERR_UNAUTHORIZED);
    }
#ifdef CRIMSON_WITH_STEAM
    const auto sdk = runtime.lock();
    const bool hosting = options.is_valid();
    const auto membership = sdk->can_track_call() ? SteamLobbyLease::reserve(sdk, hosting) : nullptr;
    if (!membership) {
        return SteamLobbyOperation::rejected(ERR_BUSY);
    }
    auto *api = sdk->get_matchmaking();
    const SteamAPICall_t call = hosting ? api->CreateLobby(k_ELobbyTypePrivate, options->capacity) :
                                         api->JoinLobby(CSteamID(target));
    const ObjectID self(get_instance_id());
    const bool watched = sdk->watch_call(call, hosting ? LobbyCreated_t::k_iCallback : LobbyEnter_t::k_iCallback,
            hosting ? sizeof(LobbyCreated_t) : sizeof(LobbyEnter_t),
            [self, membership, options, target, selected_namespace, hosting](const void *data) {
                membership->settle(acquired_lobby(data, hosting));
                const Ref<SteamLobbies> service(Object::cast_to<SteamLobbies>(ObjectDB::get_instance(self)));
                if (service.is_valid()) {
                    service->finish_membership(membership, options, target, selected_namespace);
                } else {
                    membership->retire();
                }
            });
    if (!watched) {
        membership->settle(0);
        return SteamLobbyOperation::rejected(ERR_CANT_CONNECT);
    }
    social_lease = membership;
    pending_operation.instantiate();
    pending_operation->service_id = self;
    membership_deadline_ms = Time::get_singleton()->get_ticks_msec() + 30000;
    return pending_operation;
#else
    return SteamLobbyOperation::rejected(ERR_UNAVAILABLE);
#endif
}

void SteamLobbies::finish_membership(const std::shared_ptr<SteamLobbyLease> &membership,
        const Ref<SteamLobbyOptions> &options, uint64_t target, const StringName &selected_namespace) {
#ifdef CRIMSON_WITH_STEAM
    if (membership != social_lease) {
        membership->retire();
        return;
    }
    const auto operation = std::exchange(pending_operation, {});
    const auto sdk = runtime.lock();
    const uint64_t id = membership->current();
    const bool timed_out = Time::get_singleton()->get_ticks_msec() >= membership_deadline_ms;
    Error outcome = timed_out ? ERR_TIMEOUT : ERR_CANT_CONNECT;
    if (!timed_out && is_available() && operation.is_valid() && id != 0) {
        auto *api = sdk->get_matchmaking();
        const CSteamID lobby(id);
        const CSteamID owner = api->GetLobbyOwner(lobby);
        StringName actual_namespace = selected_namespace;
        if (options.is_valid()) {
            const CharString native_namespace = String(selected_namespace).utf8();
            if (owner.ConvertToUint64() == sdk->get_user_id() && api->SetLobbyJoinable(lobby, false) &&
                    api->SetLobbyData(lobby, "crimson_protocol", STEAM_SOCIAL_LOBBY_PROTOCOL) &&
                    api->SetLobbyData(lobby, "crimson_namespace", native_namespace.get_data()) &&
                    (options->visibility == SteamLobbyOptions::PRIVATE ||
                            api->SetLobbyType(lobby, static_cast<ELobbyType>(options->visibility))) &&
                    api->SetLobbyJoinable(lobby, true)) {
                outcome = OK;
            }
        } else {
            const int32_t capacity = api->GetLobbyMemberLimit(lobby);
            const int32_t players = api->GetNumLobbyMembers(lobby);
            const String remote_namespace = sdk_text(api->GetLobbyData(lobby, "crimson_namespace"), 64);
            const bool compatible = id == target && is_individual(owner) && capacity >= 1 && capacity <= 65 &&
                    players >= 1 && players <= capacity &&
                    contains_member(*api, lobby, CSteamID(sdk->get_user_id())) &&
                    sdk_text(api->GetLobbyData(lobby, "crimson_protocol"), 127) == STEAM_SOCIAL_LOBBY_PROTOCOL &&
                    valid_steam_lobby_namespace(remote_namespace) &&
                    (selected_namespace.is_empty() || remote_namespace == String(selected_namespace));
            outcome = compatible ? OK : ERR_INVALID_DATA;
            actual_namespace = StringName(remote_namespace);
        }
        if (outcome == OK) {
            const auto listing = SteamListing::lobby(id, sdk->get_app_id(), actual_namespace, {},
                    sdk_text(api->GetLobbyData(lobby, "crimson_name"), 127),
                    api->GetNumLobbyMembers(lobby), api->GetLobbyMemberLimit(lobby), true);
            set_lobby(membership, listing);
        }
    }
    if (outcome != OK) {
        social_lease.reset();
        membership->retire();
    }
    if (operation.is_valid()) {
        const auto lobby = active_lobby;
        operation->finish(outcome, lobby);
    }
#endif
}

Error SteamLobbies::cancel_membership(SteamLobbyOperation *operation, Error reason) {
#ifdef CRIMSON_WITH_STEAM
    if (pending_operation.is_valid() && pending_operation.ptr() == operation) {
        const auto retiring = std::exchange(pending_operation, {});
        const auto membership = std::exchange(social_lease, {});
        membership->retire();
        retiring->finish(reason);
        return OK;
    }
#endif
    return ERR_DOES_NOT_EXIST;
}

Error SteamLobbies::retire_lobby(SteamLobby *lobby, Error reason) {
#ifdef CRIMSON_WITH_STEAM
    if (active_lobby.is_valid() && active_lobby.ptr() == lobby) {
        const auto old = std::exchange(active_lobby, {});
        old->lease.reset();
        const auto membership = std::exchange(social_lease, {});
        if (membership) {
            membership->retire();
        }
        old->emit_signal("closed", reason);
        return OK;
    }
#endif
    return ERR_DOES_NOT_EXIST;
}

void SteamLobbies::set_lobby(const std::shared_ptr<SteamLobbyLease> &membership, const Ref<SteamListing> &listing) {
    active_lobby.instantiate();
    active_lobby->snapshot = listing;
    active_lobby->service_id = ObjectID(get_instance_id());
    active_lobby->lease = membership;
    active_lobby->scope = scope;
    active_lobby->runtime = runtime;
    active_lobby->game_managed = game_managed;
}

void SteamLobbies::set_game_lobby(const std::shared_ptr<SteamLobbyLease> &membership, const Ref<SteamListing> &listing) {
    if (game_managed && is_available() && active_lobby.is_null()) {
        set_lobby(membership, listing);
    }
}

void SteamLobbies::clear_game_lobby(const std::shared_ptr<SteamLobbyLease> &membership) {
    if (game_managed && active_lobby.is_valid() && active_lobby->lease.lock() == membership) {
        retire_lobby(active_lobby.ptr(), is_available() ? OK : ERR_SKIP);
    }
}

void SteamLobbies::on_invite(const void *data, size_t size) {
#ifdef CRIMSON_WITH_STEAM
    if (game_managed || !is_available() || !data || size != sizeof(GameLobbyJoinRequested_t)) {
        return;
    }
    GameLobbyJoinRequested_t event{};
    std::memcpy(&event, data, sizeof(event));
    const CSteamID lobby = event.m_steamIDLobby;
    if (!lobby.IsValid() || !lobby.IsLobby()) {
        return;
    }
    const auto sdk = runtime.lock();
    auto *api = sdk->get_matchmaking();
    const String protocol = sdk_text(api->GetLobbyData(lobby, "crimson_protocol"), 127);
    const String known_namespace = sdk_text(api->GetLobbyData(lobby, "crimson_namespace"), 64);
    const StringName selected_namespace = valid_steam_lobby_namespace(known_namespace) ?
            StringName(known_namespace) : StringName();
    const auto listing = SteamListing::lobby(lobby.ConvertToUint64(), sdk->get_app_id(), selected_namespace,
            {}, {}, 0, 0, protocol == STEAM_SOCIAL_LOBBY_PROTOCOL);
    emit_signal("invite_received", listing);
#endif
}

} // namespace crimson::steam
