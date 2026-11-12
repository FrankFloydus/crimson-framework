// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/steam_diagnostics_service.hpp"

#include <godot_cpp/core/class_db.hpp>

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/client/steam_client_scope.hpp"
#include "modules/steam/client/steam_diagnostics.hpp"
#include "modules/steam/runtime/steam_runtime.hpp"

#include <godot_cpp/core/object_id.hpp>
#endif

namespace crimson::steam {
using namespace godot;

namespace {

// Keep snapshot data visible to scripts without storing instance state.
constexpr uint32_t read_only = PROPERTY_USAGE_NO_INSTANCE_STATE | PROPERTY_USAGE_READ_ONLY;

#ifdef CRIMSON_WITH_STEAM
// Exact service instance that owns the warning hook, accessed on the main thread.
ObjectID warning_owner;
// Process-wide queue shared with the SDK callback's calling thread.
detail::SteamDiagnosticBuffer warning_buffer;

// Capture the severity without copying the SDK payload or calling Godot or Steam.
extern "C" void S_CALLTYPE crimson_steam_warning_hook(int severity, const char *) noexcept {
    warning_buffer.append(severity);
}
#endif

} // namespace

void SteamDiagnosticMessage::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_severity"), &SteamDiagnosticMessage::get_severity);
    ClassDB::bind_method(D_METHOD("get_text"), &SteamDiagnosticMessage::get_text);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "severity", PROPERTY_HINT_NONE, "", read_only), "", "get_severity");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "text", PROPERTY_HINT_NONE, "", read_only), "", "get_text");
}

int64_t SteamDiagnosticMessage::get_severity() const {
    return severity;
}

String SteamDiagnosticMessage::get_text() const {
    return text;
}

SteamDiagnosticBatch::SteamDiagnosticBatch() {
    messages.make_read_only();
}

void SteamDiagnosticBatch::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_messages"), &SteamDiagnosticBatch::get_messages);
    ClassDB::bind_method(D_METHOD("get_dropped_count"), &SteamDiagnosticBatch::get_dropped_count);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "messages", PROPERTY_HINT_ARRAY_TYPE,
                         SteamDiagnosticMessage::get_class_static(), read_only), "", "get_messages");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "dropped_count", PROPERTY_HINT_NONE, "", read_only), "", "get_dropped_count");
}

TypedArray<SteamDiagnosticMessage> SteamDiagnosticBatch::get_messages() const {
    return messages;
}

int64_t SteamDiagnosticBatch::get_dropped_count() const {
    return dropped_count;
}

void SteamDiagnostics::_bind_methods() {
    ClassDB::bind_method(D_METHOD("start"), &SteamDiagnostics::start);
    ClassDB::bind_method(D_METHOD("drain"), &SteamDiagnostics::drain);
    ClassDB::bind_method(D_METHOD("stop"), &SteamDiagnostics::stop);
}

void SteamDiagnostics::attach(const std::shared_ptr<SteamRuntime> &active,
        const std::shared_ptr<SteamClientScope> &scope) {
    runtime = active;
    client_scope = scope;
}

void SteamDiagnostics::detach() {
    stop();
    client_scope.reset();
    runtime.reset();
}

Error SteamDiagnostics::start() {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread()) {
        return ERR_BUSY;
    }
    const auto scope = client_scope.lock();
    const auto active = scope && scope->active ? runtime.lock() : nullptr;
    ISteamUtils *utils = active ? active->get_utils() : nullptr;
    if (!utils) {
        return ERR_UNAVAILABLE;
    }
    const ObjectID owner(get_instance_id());
    if (warning_owner.is_valid()) {
        return warning_owner == owner ? OK : ERR_BUSY;
    }
    const auto set_hook = active->symbol<decltype(&SteamAPI_ISteamUtils_SetWarningMessageHook)>(
            "SteamAPI_ISteamUtils_SetWarningMessageHook");
    if (!set_hook) {
        return ERR_UNAVAILABLE;
    }

    warning_owner = owner;
    warning_buffer.start();
    set_hook(utils, &crimson_steam_warning_hook);
    return OK;
#else
    return ERR_UNAVAILABLE;
#endif
}

Ref<SteamDiagnosticBatch> SteamDiagnostics::drain() {
    Ref<SteamDiagnosticBatch> result;
    result.instantiate();
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread() || warning_owner != ObjectID(get_instance_id())) {
        return result;
    }
    const auto scope = client_scope.lock();
    if (!scope || !scope->active) {
        return result;
    }

    const auto batch = warning_buffer.drain();
    TypedArray<SteamDiagnosticMessage> messages;
    for (std::size_t index = 0; index < batch.count; ++index) {
        const int severity = batch.severities[index];
        Ref<SteamDiagnosticMessage> message;
        message.instantiate();
        message->severity = severity;
        message->text = String(detail::warning_summary(severity));
        messages.push_back(message);
    }
    messages.make_read_only();
    result->messages = messages;
    result->dropped_count = batch.dropped;
#endif
    return result;
}

void SteamDiagnostics::stop() {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread() || warning_owner != ObjectID(get_instance_id())) {
        return;
    }
    const auto active = runtime.lock();
    warning_owner = ObjectID();
    warning_buffer.stop();

    ISteamUtils *utils = active ? active->get_utils() : nullptr;
    const auto set_hook = utils ? active->symbol<decltype(&SteamAPI_ISteamUtils_SetWarningMessageHook)>(
            "SteamAPI_ISteamUtils_SetWarningMessageHook") : nullptr;
    if (set_hook) {
        set_hook(utils, nullptr);
    }
#endif
}

} // namespace crimson::steam
