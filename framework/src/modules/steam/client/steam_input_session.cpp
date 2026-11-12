// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/steam_input_session.hpp"

#ifdef CRIMSON_WITH_STEAM

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/variant/char_string.hpp>

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>

namespace crimson::steam {
using namespace godot;

namespace {

// Keep Steam's manifest override and frame guard across lease restarts within one runtime.
struct ProcessInputState {
    // Runtime for which the stored manifest and frame apply.
    std::weak_ptr<SteamRuntime> runtime;
    // Active lease without preventing final-scope shutdown.
    std::weak_ptr<SteamInputSession> lease;
    // Steam retains a successful manifest override after Input shutdown.
    std::string manifest;
    // Most recent Godot process frame synchronized through RunFrame.
    uint64_t last_frame = (std::numeric_limits<uint64_t>::max)();
};

// Access the process input state on the main thread.
ProcessInputState &process_input() {
    static ProcessInputState state;
    return state;
}

// Resolve a Steam Input action name only through the expected flat SDK export.
template <typename Function>
std::optional<uint64_t> lookup_name(const SteamRuntime &runtime, ISteamInput *input, const char *export_name,
        const char *name) {
    const auto lookup = runtime.symbol<Function>(export_name);
    return lookup ? std::optional<uint64_t>(lookup(input, name)) : std::nullopt;
}

} // namespace

std::shared_ptr<SteamInputSession> SteamInputSession::acquire(const std::shared_ptr<SteamRuntime> &runtime,
        const String &manifest_path, Error &error) {
    auto &process = process_input();
    if (const auto current = process.lease.lock()) {
        error = current->runtime == runtime && (manifest_path.is_empty() || current->manifest == manifest_path) ?
                OK : ERR_BUSY;
        return error == OK ? current : nullptr;
    }
    if (process.runtime.lock() != runtime) {
        process.runtime = runtime;
        process.manifest.clear();
        process.last_frame = (std::numeric_limits<uint64_t>::max)();
    }
    const CharString requested = manifest_path.utf8();
    const bool changes_manifest = !manifest_path.is_empty() && requested.get_data() != process.manifest;
    if (changes_manifest && !FileAccess::file_exists(manifest_path)) {
        error = ERR_FILE_NOT_FOUND;
        return {};
    }

    const auto get_input = runtime->symbol<decltype(&SteamAPI_SteamInput_v007)>("SteamAPI_SteamInput_v007");
    const auto init = runtime->symbol<decltype(&SteamAPI_ISteamInput_Init)>("SteamAPI_ISteamInput_Init");
    const auto enable_callbacks = runtime->symbol<decltype(&SteamAPI_ISteamInput_EnableDeviceCallbacks)>(
            "SteamAPI_ISteamInput_EnableDeviceCallbacks");
    const std::shared_ptr<SteamInputSession> lease(new SteamInputSession());
    lease->runtime = runtime;
    lease->input = get_input ? get_input() : nullptr;
    lease->shutdown = runtime->symbol<decltype(&SteamAPI_ISteamInput_Shutdown)>("SteamAPI_ISteamInput_Shutdown");
    lease->run_frame = runtime->symbol<decltype(&SteamAPI_ISteamInput_RunFrame)>("SteamAPI_ISteamInput_RunFrame");
    lease->get_controllers = runtime->symbol<decltype(&SteamAPI_ISteamInput_GetConnectedControllers)>(
            "SteamAPI_ISteamInput_GetConnectedControllers");
    if (!lease->input || !lease->shutdown || !lease->run_frame || !lease->get_controllers ||
            !init || !enable_callbacks || !init(lease->input, true)) {
        error = ERR_UNAVAILABLE;
        return {};
    }
    lease->initialized = true;
    if (!lease->subscribe(lease)) {
        error = ERR_BUSY;
        return {};
    }
    if (changes_manifest) {
        const auto set_manifest = runtime->symbol<decltype(&SteamAPI_ISteamInput_SetInputActionManifestFilePath)>(
                "SteamAPI_ISteamInput_SetInputActionManifestFilePath");
        if (!set_manifest) {
            error = ERR_UNAVAILABLE;
            return {};
        }
        if (!set_manifest(lease->input, requested.get_data())) {
            error = ERR_PARSE_ERROR;
            return {};
        }
        process.manifest = requested.get_data();
    }
    lease->manifest = String::utf8(process.manifest.c_str());
    enable_callbacks(lease->input);
    process.lease = lease;
    error = OK;
    return lease;
}

SteamInputSession::~SteamInputSession() {
    for (const auto subscription : subscriptions) {
        if (subscription != 0) {
            runtime->unsubscribe(subscription);
        }
    }
    if (initialized) {
        shutdown(input);
    }
}

bool SteamInputSession::subscribe(const std::shared_ptr<SteamInputSession> &owner) {
    const std::weak_ptr<SteamInputSession> weak = owner;
    subscriptions[0] = runtime->subscribe(SteamInputDeviceDisconnected_t::k_iCallback,
            [weak](const void *data, size_t size) {
                if (!data || size != sizeof(SteamInputDeviceDisconnected_t)) {
                    return;
                }
                SteamInputDeviceDisconnected_t event{};
                std::memcpy(&event, data, sizeof(event));
                if (const auto lease = weak.lock()) {
                    lease->devices.erase(event.m_ulDisconnectedDeviceHandle);
                }
            });
    subscriptions[1] = runtime->subscribe(SteamInputConfigurationLoaded_t::k_iCallback,
            [weak](const void *data, size_t size) {
                if (!data || size != sizeof(SteamInputConfigurationLoaded_t)) {
                    return;
                }
                SteamInputConfigurationLoaded_t event{};
                std::memcpy(&event, data, sizeof(event));
                const auto lease = weak.lock();
                if (!lease || lease->runtime->get_app_id() != event.m_unAppID) {
                    return;
                }
                const auto device = lease->devices.find(event.m_ulDeviceHandle);
                if (device != lease->devices.end()) {
                    ++device->second.binding_revision;
                }
            });
    return subscriptions[0] != 0 && subscriptions[1] != 0;
}

void SteamInputSession::poll() {
    const Engine *engine = Engine::get_singleton();
    if (!engine) {
        return;
    }
    const uint64_t frame = engine->get_process_frames();
    auto &process = process_input();
    if (process.last_frame == frame) {
        return;
    }
    process.last_frame = frame;
    run_frame(input, true);
    std::array<InputHandle_t, STEAM_INPUT_MAX_COUNT> handles{};
    const int count = get_controllers(input, handles.data());
    if (count < 0 || count > static_cast<int>(handles.size())) {
        return;
    }

    const auto end = handles.begin() + count;
    for (auto device = devices.begin(); device != devices.end();) {
        if (std::find(handles.begin(), end, device->first) == end) {
            device = devices.erase(device);
        } else {
            ++device;
        }
    }
    for (int index = 0; index < count; ++index) {
        const uint64_t handle = handles[index];
        if (handle == 0 || handle == STEAM_INPUT_HANDLE_ALL_CONTROLLERS || devices.count(handle)) {
            continue;
        }
        devices.emplace(handle, Connection{next_connection++, 0});
    }
}

const std::map<uint64_t, SteamInputSession::Connection> &SteamInputSession::connections() const {
    return devices;
}

ISteamInput *SteamInputSession::interface() const {
    return input;
}

const SteamRuntime &SteamInputSession::get_runtime() const {
    return *runtime;
}

std::optional<uint64_t> SteamInputSession::name_handle(NameKind kind, const StringName &name) {
    auto &cache = names[static_cast<size_t>(kind)];
    const auto cached = cache.find(name);
    if (cached != cache.end()) {
        return cached->second;
    }
    const CharString text = String(name).utf8();
    std::optional<uint64_t> handle;
    switch (kind) {
        case NameKind::Set:
            handle = lookup_name<decltype(&SteamAPI_ISteamInput_GetActionSetHandle)>(
                    *runtime, input, "SteamAPI_ISteamInput_GetActionSetHandle", text.get_data());
            break;
        case NameKind::Digital:
            handle = lookup_name<decltype(&SteamAPI_ISteamInput_GetDigitalActionHandle)>(
                    *runtime, input, "SteamAPI_ISteamInput_GetDigitalActionHandle", text.get_data());
            break;
        case NameKind::Analog:
            handle = lookup_name<decltype(&SteamAPI_ISteamInput_GetAnalogActionHandle)>(
                    *runtime, input, "SteamAPI_ISteamInput_GetAnalogActionHandle", text.get_data());
            break;
    }
    if (handle && *handle != 0 && cache.size() < 256) {
        cache.emplace(name, *handle);
    }
    return handle;
}

} // namespace crimson::steam

#endif
