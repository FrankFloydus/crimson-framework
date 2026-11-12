// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/steam_operation.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/signal.hpp>

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/runtime/steam_runtime.hpp"
#endif

#include <utility>

namespace crimson::steam {

using namespace godot;

void SteamOperation::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_finished"), &SteamOperation::is_finished);
    ClassDB::bind_method(D_METHOD("result"), &SteamOperation::result);
    ClassDB::bind_method(D_METHOD("wait"), &SteamOperation::wait);
    ClassDB::bind_method(D_METHOD("cancel"), &SteamOperation::cancel);
    ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::OBJECT, "result", PROPERTY_HINT_RESOURCE_TYPE,
            "SteamResult")));
}

bool SteamOperation::is_finished() const {
    return outcome.is_valid();
}

Ref<SteamResult> SteamOperation::result() const {
    return outcome;
}

Variant SteamOperation::wait() {
    return outcome.is_valid() ? Variant(outcome) : Variant(Signal(this, "completed"));
}

Error SteamOperation::cancel() {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread()) {
        return ERR_BUSY;
    }
#endif
    const auto cancel = cancel_observation;
    return cancel ? cancel() : ERR_DOES_NOT_EXIST;
}

void SteamOperation::when_finished(std::function<void(const Ref<SteamResult> &)> next) {
    if (!next) {
        return;
    }
    const Ref<SteamResult> settled = outcome;
    if (settled.is_valid()) {
        next(settled);
        return;
    }
    continuation = std::move(next);
}

void SteamOperation::finish(const Ref<SteamResult> &result) {
    if (outcome.is_valid()) {
        return;
    }
    const Ref<SteamOperation> keep_alive(this);
    outcome = result;
    cancel_observation = {};
    if (const auto next = std::exchange(continuation, {})) {
        next(outcome);
    }
    emit_signal("completed", outcome);
}

} // namespace crimson::steam
