// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/steam_bytes_result.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/signal.hpp>

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/runtime/steam_runtime.hpp"
#endif

#include <utility>

namespace crimson::steam {

using namespace godot;

void SteamBytesResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamBytesResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::PACKED_BYTE_ARRAY, "value", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_value");
}

PackedByteArray SteamBytesResult::get_value() const {
    return value;
}

Ref<SteamBytesResult> SteamBytesResult::create(Error error, const String &message, PackedByteArray bytes) {
    Ref<SteamBytesResult> result;
    result.instantiate();
    result->set_outcome(error, message);
    result->value = std::move(bytes);
    return result;
}

void SteamBytesOperation::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_finished"), &SteamBytesOperation::is_finished);
    ClassDB::bind_method(D_METHOD("result"), &SteamBytesOperation::result);
    ClassDB::bind_method(D_METHOD("wait"), &SteamBytesOperation::wait);
    ClassDB::bind_method(D_METHOD("cancel"), &SteamBytesOperation::cancel);
    ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::OBJECT, "result", PROPERTY_HINT_RESOURCE_TYPE,
            "SteamBytesResult")));
}

bool SteamBytesOperation::is_finished() const {
    return outcome.is_valid();
}

Ref<SteamBytesResult> SteamBytesOperation::result() const {
    return outcome;
}

Variant SteamBytesOperation::wait() {
    return outcome.is_valid() ? Variant(outcome) : Variant(Signal(this, "completed"));
}

Error SteamBytesOperation::cancel() {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread()) {
        return ERR_BUSY;
    }
#endif
    const auto cancel = cancel_observation;
    return cancel ? cancel() : ERR_DOES_NOT_EXIST;
}

void SteamBytesOperation::finish(const Ref<SteamBytesResult> &result) {
    if (outcome.is_valid()) {
        return;
    }
    const Ref<SteamBytesOperation> keep_alive(this);
    outcome = result;
    cancel_observation = {};
    emit_signal("completed", outcome);
}

} // namespace crimson::steam
