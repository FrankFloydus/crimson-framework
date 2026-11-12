// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/steam_input.hpp"

#include <godot_cpp/core/class_db.hpp>

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/client/steam_input_session.hpp"

#include <godot_cpp/variant/char_string.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>
#endif

namespace crimson::steam {
using namespace godot;

namespace {

#ifdef CRIMSON_WITH_STEAM
static_assert(int(SteamController::TYPE_STEAM_FRAME_PAIR) + 1 == k_ESteamInputType_Count);
static_assert(int(SteamAnalogInput::MODE_SWITCHES) == k_EInputSourceMode_Switches);
static_assert(int(SteamController::LARGE) == k_ESteamInputGlyphSize_Large);

// Reject missing or oversized names before handing them to the SDK as C strings.
bool valid_action_name(const StringName &name) {
    if (name.is_empty()) {
        return false;
    }
    const CharString text = String(name).utf8();
    return text.length() <= 128 && std::strlen(text.get_data()) == static_cast<size_t>(text.length());
}

// Copy the SDK's bounded origins in order, without repeating the same origin.
template <typename Query>
Error append_origins(Query query, ISteamInput *input, InputHandle_t controller, InputActionSetHandle_t set,
        uint64_t action, std::vector<EInputActionOrigin> &origins) {
    if (!query || action == 0 || set == 0) {
        return OK;
    }
    std::array<EInputActionOrigin, STEAM_INPUT_MAX_ORIGINS> native{};
    const int count = query(input, controller, set, action, native.data());
    if (count < 0 || count > static_cast<int>(native.size())) {
        return ERR_INVALID_DATA;
    }
    for (int index = 0; index < count; ++index) {
        const auto origin = native[index];
        if (origin > k_EInputActionOrigin_None && std::find(origins.begin(), origins.end(), origin) == origins.end()) {
            origins.push_back(origin);
        }
    }
    return OK;
}

// Return a handle only when Steam reports that its action is available in the active set.
template <typename Query>
uint64_t active_action(const SteamInputSession &lease, const char *export_name, InputHandle_t controller,
        uint64_t action) {
    const auto query = lease.get_runtime().symbol<Query>(export_name);
    return query && action != 0 && query(lease.interface(), controller, action).bActive ? action : 0;
}
#endif

} // namespace

void SteamController::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_connected"), &SteamController::is_connected);
    ClassDB::bind_method(D_METHOD("type"), &SteamController::type);
    ClassDB::bind_method(D_METHOD("use_action_set", "name"), &SteamController::use_action_set);
    ClassDB::bind_method(D_METHOD("activate_layer", "name"), &SteamController::activate_layer);
    ClassDB::bind_method(D_METHOD("deactivate_layer", "name"), &SteamController::deactivate_layer);
    ClassDB::bind_method(D_METHOD("digital", "action"), &SteamController::digital);
    ClassDB::bind_method(D_METHOD("analog", "action"), &SteamController::analog);
    ClassDB::bind_method(D_METHOD("glyph", "action", "size"), &SteamController::glyph, DEFVAL(MEDIUM));
    ClassDB::bind_method(D_METHOD("glyphs", "action", "size"), &SteamController::glyphs, DEFVAL(MEDIUM));
    ClassDB::bind_method(D_METHOD("open_binding_panel"), &SteamController::open_binding_panel);
    ClassDB::bind_method(D_METHOD("vibrate", "low", "high"), &SteamController::vibrate);

    BIND_ENUM_CONSTANT(TYPE_UNKNOWN);
    BIND_ENUM_CONSTANT(TYPE_STEAM_CONTROLLER);
    BIND_ENUM_CONSTANT(TYPE_XBOX_360);
    BIND_ENUM_CONSTANT(TYPE_XBOX_ONE);
    BIND_ENUM_CONSTANT(TYPE_GENERIC_GAMEPAD);
    BIND_ENUM_CONSTANT(TYPE_PLAYSTATION_4);
    BIND_ENUM_CONSTANT(TYPE_APPLE_MFI);
    BIND_ENUM_CONSTANT(TYPE_ANDROID);
    BIND_ENUM_CONSTANT(TYPE_SWITCH_JOYCON_PAIR);
    BIND_ENUM_CONSTANT(TYPE_SWITCH_JOYCON_SINGLE);
    BIND_ENUM_CONSTANT(TYPE_SWITCH_PRO);
    BIND_ENUM_CONSTANT(TYPE_MOBILE_TOUCH);
    BIND_ENUM_CONSTANT(TYPE_PLAYSTATION_3);
    BIND_ENUM_CONSTANT(TYPE_PLAYSTATION_5);
    BIND_ENUM_CONSTANT(TYPE_STEAM_DECK);
    BIND_ENUM_CONSTANT(TYPE_STEAMOS_HANDHELD);
    BIND_ENUM_CONSTANT(TYPE_SWITCH_2_PRO);
    BIND_ENUM_CONSTANT(TYPE_STEAM_CONTROLLER_2026);
    BIND_ENUM_CONSTANT(TYPE_STEAM_FRAME_PAIR);
    BIND_ENUM_CONSTANT(SMALL);
    BIND_ENUM_CONSTANT(MEDIUM);
    BIND_ENUM_CONSTANT(LARGE);
}

Ref<SteamInput> SteamController::active_owner() const {
#ifdef CRIMSON_WITH_STEAM
    if (SteamRuntime::on_main_thread() && owner.is_valid()) {
        const Ref<SteamInput> service(Object::cast_to<SteamInput>(ObjectDB::get_instance(owner)));
        if (service.is_valid() && service->owns_controller(*this)) {
            return service;
        }
    }
#endif
    return {};
}

bool SteamController::is_connected() const {
    return active_owner().is_valid();
}

SteamController::Type SteamController::type() const {
#ifdef CRIMSON_WITH_STEAM
    const auto service = active_owner();
    if (service.is_valid()) {
        const auto &lease = *service->session;
        const auto get = lease.get_runtime().symbol<decltype(&SteamAPI_ISteamInput_GetInputTypeForHandle)>(
                "SteamAPI_ISteamInput_GetInputTypeForHandle");
        if (get) {
            const auto type = get(lease.interface(), handle);
            if (type >= k_ESteamInputType_Unknown && type < k_ESteamInputType_Count) {
                return static_cast<Type>(type);
            }
        }
    }
#endif
    return TYPE_UNKNOWN;
}

Error SteamController::change_action_set(const StringName &name, SetAction action) {
#ifdef CRIMSON_WITH_STEAM
    const auto service = active_owner();
    if (service.is_null()) {
        return ERR_UNAVAILABLE;
    }
    if (!valid_action_name(name)) {
        return ERR_INVALID_PARAMETER;
    }
    auto &lease = *service->session;
    const auto set = lease.name_handle(SteamInputSession::NameKind::Set, name);
    if (!set) {
        return ERR_UNAVAILABLE;
    }
    if (*set == 0) {
        return ERR_DOES_NOT_EXIST;
    }
    const char *symbol = nullptr;
    switch (action) {
        case SetAction::Select: symbol = "SteamAPI_ISteamInput_ActivateActionSet"; break;
        case SetAction::ActivateLayer: symbol = "SteamAPI_ISteamInput_ActivateActionSetLayer"; break;
        case SetAction::DeactivateLayer: symbol = "SteamAPI_ISteamInput_DeactivateActionSetLayer"; break;
    }
    const auto change = lease.get_runtime().symbol<decltype(&SteamAPI_ISteamInput_ActivateActionSet)>(symbol);
    if (!change) {
        return ERR_UNAVAILABLE;
    }
    change(lease.interface(), handle, *set);
    return OK;
#else
    return ERR_UNAVAILABLE;
#endif
}

Error SteamController::use_action_set(const StringName &name) {
    return change_action_set(name, SetAction::Select);
}

Error SteamController::activate_layer(const StringName &name) {
    return change_action_set(name, SetAction::ActivateLayer);
}

Error SteamController::deactivate_layer(const StringName &name) {
    return change_action_set(name, SetAction::DeactivateLayer);
}

Ref<SteamDigitalInputResult> SteamController::digital(const StringName &action) const {
#ifdef CRIMSON_WITH_STEAM
    const auto service = active_owner();
    if (service.is_valid()) {
        if (!valid_action_name(action)) {
            return SteamDigitalInputResult::create(ERR_INVALID_PARAMETER);
        }
        auto &lease = *service->session;
        const auto read = lease.get_runtime().symbol<decltype(&SteamAPI_ISteamInput_GetDigitalActionData)>(
                "SteamAPI_ISteamInput_GetDigitalActionData");
        const auto binding = lease.name_handle(SteamInputSession::NameKind::Digital, action);
        if (!read || !binding) {
            return SteamDigitalInputResult::create(ERR_UNAVAILABLE);
        }
        if (*binding == 0) {
            return SteamDigitalInputResult::create(ERR_DOES_NOT_EXIST);
        }
        const auto frame = read(lease.interface(), handle, *binding);
        Ref<SteamDigitalInput> value;
        value.instantiate();
        value->active = frame.bActive;
        value->pressed = frame.bActive && frame.bState;
        return SteamDigitalInputResult::create(OK, value);
    }
#endif
    return SteamDigitalInputResult::create(ERR_UNAVAILABLE);
}

Ref<SteamAnalogInputResult> SteamController::analog(const StringName &action) const {
#ifdef CRIMSON_WITH_STEAM
    const auto service = active_owner();
    if (service.is_valid()) {
        if (!valid_action_name(action)) {
            return SteamAnalogInputResult::create(ERR_INVALID_PARAMETER);
        }
        auto &lease = *service->session;
        const auto read = lease.get_runtime().symbol<decltype(&SteamAPI_ISteamInput_GetAnalogActionData)>(
                "SteamAPI_ISteamInput_GetAnalogActionData");
        const auto binding = lease.name_handle(SteamInputSession::NameKind::Analog, action);
        if (!read || !binding) {
            return SteamAnalogInputResult::create(ERR_UNAVAILABLE);
        }
        if (*binding == 0) {
            return SteamAnalogInputResult::create(ERR_DOES_NOT_EXIST);
        }
        const auto frame = read(lease.interface(), handle, *binding);
        Ref<SteamAnalogInput> value;
        value.instantiate();
        value->active = frame.bActive;
        if (frame.bActive) {
            if (!std::isfinite(frame.x) || !std::isfinite(frame.y)) {
                return SteamAnalogInputResult::create(ERR_INVALID_DATA);
            }
            value->value = Vector2(frame.x, frame.y);
            value->mode = frame.eMode >= k_EInputSourceMode_None && frame.eMode <= k_EInputSourceMode_Switches ?
                    static_cast<SteamAnalogInput::Mode>(frame.eMode) : SteamAnalogInput::MODE_UNKNOWN;
        }
        return SteamAnalogInputResult::create(OK, value);
    }
#endif
    return SteamAnalogInputResult::create(ERR_UNAVAILABLE);
}

Error SteamController::read_glyphs(const StringName &action, GlyphSize size, bool first_only,
        TypedArray<Texture2D> &textures) const {
#ifdef CRIMSON_WITH_STEAM
    const auto service = active_owner();
    if (service.is_null()) {
        return ERR_UNAVAILABLE;
    }
    if (!valid_action_name(action) || size < SMALL || size > LARGE) {
        return ERR_INVALID_PARAMETER;
    }
    auto &lease = *service->session;
    const auto &runtime = lease.get_runtime();
    ISteamInput *input = lease.interface();
    const auto current_set = runtime.symbol<decltype(&SteamAPI_ISteamInput_GetCurrentActionSet)>(
            "SteamAPI_ISteamInput_GetCurrentActionSet");
    const auto layers = runtime.symbol<decltype(&SteamAPI_ISteamInput_GetActiveActionSetLayers)>(
            "SteamAPI_ISteamInput_GetActiveActionSetLayers");
    const auto digital_origins = runtime.symbol<decltype(&SteamAPI_ISteamInput_GetDigitalActionOrigins)>(
            "SteamAPI_ISteamInput_GetDigitalActionOrigins");
    const auto analog_origins = runtime.symbol<decltype(&SteamAPI_ISteamInput_GetAnalogActionOrigins)>(
            "SteamAPI_ISteamInput_GetAnalogActionOrigins");
    const uint64_t digital_action = active_action<decltype(&SteamAPI_ISteamInput_GetDigitalActionData)>(
            lease, "SteamAPI_ISteamInput_GetDigitalActionData", handle,
            lease.name_handle(SteamInputSession::NameKind::Digital, action).value_or(0));
    const uint64_t analog_action = active_action<decltype(&SteamAPI_ISteamInput_GetAnalogActionData)>(
            lease, "SteamAPI_ISteamInput_GetAnalogActionData", handle,
            lease.name_handle(SteamInputSession::NameKind::Analog, action).value_or(0));
    if (!current_set || !layers || ((!digital_origins || digital_action == 0) && (!analog_origins || analog_action == 0))) {
        return ERR_UNAVAILABLE;
    }

    // Query the highest active layer with origins, falling back to the base action set.
    std::array<InputActionSetHandle_t, STEAM_INPUT_MAX_ACTIVE_LAYERS + 1> sets{};
    sets[0] = current_set(input, handle);
    const int layer_count = layers(input, handle, sets.data() + 1);
    if (layer_count < 0 || layer_count > STEAM_INPUT_MAX_ACTIVE_LAYERS) {
        return ERR_INVALID_DATA;
    }
    std::vector<EInputActionOrigin> origins;
    for (int index = layer_count; index >= 0; --index) {
        const Error digital_error = append_origins(digital_origins, input, handle, sets[index], digital_action, origins);
        const Error analog_error = append_origins(analog_origins, input, handle, sets[index], analog_action, origins);
        if (digital_error != OK || analog_error != OK) {
            return ERR_INVALID_DATA;
        }
        if (!origins.empty()) {
            break;
        }
    }
    if (origins.empty()) {
        return ERR_UNAVAILABLE;
    }
    for (const auto origin : origins) {
        const auto texture = service->glyph_texture(static_cast<int>(origin), static_cast<int>(size));
        if (texture.is_null()) {
            return ERR_UNAVAILABLE;
        }
        textures.push_back(texture);
        if (first_only) {
            break;
        }
    }
    return OK;
#else
    return ERR_UNAVAILABLE;
#endif
}

Ref<SteamTextureResult> SteamController::glyph(const StringName &action, GlyphSize size) const {
    TypedArray<Texture2D> textures;
    const Error error = read_glyphs(action, size, true, textures);
    if (error != OK) {
        return SteamTextureResult::create(error);
    }
    const Ref<Texture2D> first = textures[0];
    return SteamTextureResult::create(OK, first);
}

Ref<SteamTexturesResult> SteamController::glyphs(const StringName &action, GlyphSize size) const {
    TypedArray<Texture2D> textures;
    const Error error = read_glyphs(action, size, false, textures);
    return SteamTexturesResult::create(error, error == OK ? textures : TypedArray<Texture2D>());
}

Error SteamController::open_binding_panel() {
#ifdef CRIMSON_WITH_STEAM
    const auto service = active_owner();
    if (service.is_valid()) {
        const auto &lease = *service->session;
        const auto open = lease.get_runtime().symbol<decltype(&SteamAPI_ISteamInput_ShowBindingPanel)>(
                "SteamAPI_ISteamInput_ShowBindingPanel");
        if (open && open(lease.interface(), handle)) {
            return OK;
        }
    }
#endif
    return ERR_UNAVAILABLE;
}

Error SteamController::vibrate(double low, double high) {
#ifdef CRIMSON_WITH_STEAM
    const auto service = active_owner();
    if (service.is_null()) {
        return ERR_UNAVAILABLE;
    }
    if (!std::isfinite(low) || !std::isfinite(high) || low < 0.0 || low > 1.0 || high < 0.0 || high > 1.0) {
        return ERR_INVALID_PARAMETER;
    }
    const auto &lease = *service->session;
    const auto trigger = lease.get_runtime().symbol<decltype(&SteamAPI_ISteamInput_TriggerVibration)>(
            "SteamAPI_ISteamInput_TriggerVibration");
    if (!trigger) {
        return ERR_UNAVAILABLE;
    }
    trigger(lease.interface(), handle, static_cast<unsigned short>(std::lround(low * 65535.0)),
            static_cast<unsigned short>(std::lround(high * 65535.0)));
    return OK;
#else
    return ERR_UNAVAILABLE;
#endif
}

} // namespace crimson::steam
