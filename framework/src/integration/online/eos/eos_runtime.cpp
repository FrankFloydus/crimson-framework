// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/online/eos/eos_runtime.hpp"

#include <godot_cpp/core/class_db.hpp>

#ifdef CRIMSON_WITH_EOS
#include <eos_init.h>
#include <eos_sdk.h>
#include <eos_version.h>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstring>
#include <filesystem>
#include <new>
#include <set>
#include <string>
#include <utility>
#endif

namespace crimson::integration {

using namespace godot;

#ifdef CRIMSON_WITH_EOS
namespace {

// Limit retained one-shot requests when network work stops completing.
constexpr size_t max_pending_callbacks = 256;

// Typed exports resolved before EOS_Initialize is called.
struct EosApi {
    // Module handle retained for the lifetime of all resolved functions.
    HMODULE module = nullptr;
    // Process-level SDK initialization.
    decltype(&EOS_Initialize) initialize = nullptr;
    // Process-level SDK shutdown.
    decltype(&EOS_Shutdown) shutdown = nullptr;
    // Read the loaded runtime's SDK version.
    decltype(&EOS_GetVersion) version = nullptr;
    // Open a scoped platform.
    decltype(&EOS_Platform_Create) create_platform = nullptr;
    // Release a scoped platform before shutting down the runtime.
    decltype(&EOS_Platform_Release) release_platform = nullptr;
    // Dispatch a scoped platform's pending SDK work.
    decltype(&EOS_Platform_Tick) tick_platform = nullptr;
};

// The SDK permits one initialization attempt and one shutdown per process owner.
enum class RuntimeState { Dormant, Owned, Failed, Stopped };

// Process state confined to the Godot scene thread.
struct EosRuntime {
    // Runtime function table valid only while Crimson owns initialization.
    EosApi api;
    // Platform handles to drain before process shutdown.
    std::set<EOS_HPlatform> platforms;
    // Vendor-provided version after the native SDK is initialized.
    std::string version;
    // Initialization and teardown state.
    RuntimeState state = RuntimeState::Dormant;
    // Error retained after an attempted native initialization fails.
    Error initialization_error = ERR_UNAVAILABLE;
};

// One process-owned runtime, shared by all configured game scopes.
EosRuntime runtime;

// EOS platform and callback access belongs on the Godot scene thread.
bool on_scene_thread() {
    const OS *os = OS::get_singleton();
    return os && os->get_thread_caller_id() == os->get_main_thread_id();
}

// Derive the runtime location from the loaded Crimson module, independent of the working directory.
std::filesystem::path runtime_path() {
    HMODULE addon = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&runtime), &addon)) {
        return {};
    }
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(addon, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) {
        return {};
    }
    path.resize(length);
    return std::filesystem::path(path).parent_path() / L"EOSSDK-Win64-Shipping.dll";
}

// Resolve a Win32 export through the calling convention declared by the SDK.
template <typename Function>
Function load_symbol(HMODULE module, const char *name) {
#pragma warning(push)
#pragma warning(disable : 4191)
    return reinterpret_cast<Function>(GetProcAddress(module, name));
#pragma warning(pop)
}

// Load the exact shipped dependency and verify all mandatory exports before transferring ownership.
Error load_api(EosApi &api) {
    const auto path = runtime_path();
    if (!path.is_absolute()) {
        return ERR_CANT_OPEN;
    }
    api.module = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!api.module) {
        return ERR_CANT_OPEN;
    }
    api.initialize = load_symbol<decltype(api.initialize)>(api.module, "EOS_Initialize");
    api.shutdown = load_symbol<decltype(api.shutdown)>(api.module, "EOS_Shutdown");
    api.version = load_symbol<decltype(api.version)>(api.module, "EOS_GetVersion");
    api.create_platform = load_symbol<decltype(api.create_platform)>(api.module, "EOS_Platform_Create");
    api.release_platform = load_symbol<decltype(api.release_platform)>(api.module, "EOS_Platform_Release");
    api.tick_platform = load_symbol<decltype(api.tick_platform)>(api.module, "EOS_Platform_Tick");
    if (!api.initialize || !api.shutdown || !api.version || !api.create_platform || !api.release_platform ||
            !api.tick_platform) {
        FreeLibrary(api.module);
        api = {};
        return ERR_INVALID_DATA;
    }
    return OK;
}

// Match the version shipped in the headers, allowing the SDK's build or platform suffix.
bool compatible_version(const char *version) {
    constexpr char expected[] = EOS_VERSION_STRING_BASE;
    return version && std::strncmp(version, expected, sizeof(expected) - 1) == 0 &&
            (version[sizeof(expected) - 1] == '\0' || version[sizeof(expected) - 1] == '-' ||
                    version[sizeof(expected) - 1] == '+');
}

// Map the SDK's ownership and ABI failures without taking over another initializer.
Error initialization_error(EOS_EResult result) {
    if (result == EOS_EResult::EOS_AlreadyConfigured) {
        return ERR_ALREADY_IN_USE;
    }
    if (result == EOS_EResult::EOS_InvalidParameters || result == EOS_EResult::EOS_IncompatibleVersion) {
        return ERR_INVALID_DATA;
    }
    return FAILED;
}

// Check required configuration without reading or reporting credential values.
bool valid_option(const char *value, size_t maximum) {
    return value && value[0] && std::strlen(value) <= maximum;
}

} // namespace
#endif

void CrimsonEOS::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("initialize"), &CrimsonEOS::initialize);
    ClassDB::bind_static_method(get_class_static(), D_METHOD("get_runtime_version"), &CrimsonEOS::get_runtime_version);
    ClassDB::bind_static_method(get_class_static(), D_METHOD("is_initialized"), &CrimsonEOS::is_initialized);
}

Error CrimsonEOS::initialize() {
#ifdef CRIMSON_WITH_EOS
    if (!on_scene_thread() || runtime.state == RuntimeState::Stopped) {
        return ERR_UNAVAILABLE;
    }
    if (runtime.state == RuntimeState::Owned) {
        return OK;
    }
    if (runtime.state == RuntimeState::Failed) {
        return runtime.initialization_error;
    }

    EosApi api;
    const Error loaded = load_api(api);
    if (loaded != OK) {
        return loaded;
    }
    EOS_InitializeOptions options{};
    options.ApiVersion = EOS_INITIALIZE_API_LATEST;
    options.ProductName = "Crimson Framework";
    options.ProductVersion = "1";

    runtime.state = RuntimeState::Failed;
    const EOS_EResult result = api.initialize(&options);
    if (result != EOS_EResult::EOS_Success) {
        runtime.initialization_error = initialization_error(result);
        FreeLibrary(api.module);
        return runtime.initialization_error;
    }

    const char *version = api.version();
    if (!compatible_version(version)) {
        runtime.initialization_error = ERR_INVALID_DATA;
        api.shutdown();
        FreeLibrary(api.module);
        return runtime.initialization_error;
    }
    runtime.api = api;
    runtime.version = version;
    runtime.state = RuntimeState::Owned;
    return OK;
#else
    return ERR_UNAVAILABLE;
#endif
}

String CrimsonEOS::get_runtime_version() {
#ifdef CRIMSON_WITH_EOS
    return is_initialized() ? String::utf8(runtime.version.c_str()) : String();
#else
    return {};
#endif
}

bool CrimsonEOS::is_initialized() {
#ifdef CRIMSON_WITH_EOS
    return on_scene_thread() && runtime.state == RuntimeState::Owned;
#else
    return false;
#endif
}

void CrimsonEOS::shutdown_runtime() {
#ifdef CRIMSON_WITH_EOS
    if (runtime.state != RuntimeState::Owned) {
        runtime.state = RuntimeState::Stopped;
        return;
    }
    runtime.state = RuntimeState::Stopped;
    while (!runtime.platforms.empty()) {
        const auto platform = runtime.platforms.begin();
        const EOS_HPlatform handle = *platform;
        runtime.platforms.erase(platform);
        runtime.api.release_platform(handle);
    }
    if (runtime.api.shutdown() != EOS_EResult::EOS_Success) {
        UtilityFunctions::push_error("The EOS SDK reported a shutdown error.");
    }
    FreeLibrary(runtime.api.module);
    runtime.api = {};
    runtime.version.clear();
#endif
}

#ifdef CRIMSON_WITH_EOS
std::shared_ptr<EosPlatform> EosPlatform::create(const EOS_Platform_Options &options, Error &error) {
    if (!on_scene_thread()) {
        error = ERR_UNAVAILABLE;
        return {};
    }
    if (options.ApiVersion != EOS_PLATFORM_OPTIONS_API_LATEST ||
            !valid_option(options.ProductId, EOS_PLATFORM_OPTIONS_PRODUCTID_MAX_LENGTH) ||
            !valid_option(options.SandboxId, EOS_PLATFORM_OPTIONS_SANDBOXID_MAX_LENGTH) ||
            !valid_option(options.DeploymentId, EOS_PLATFORM_OPTIONS_DEPLOYMENTID_MAX_LENGTH) ||
            !valid_option(options.ClientCredentials.ClientId, EOS_PLATFORM_CLIENTCREDENTIALS_CLIENTID_MAX_LENGTH) ||
            !valid_option(options.ClientCredentials.ClientSecret, EOS_PLATFORM_CLIENTCREDENTIALS_CLIENTSECRET_MAX_LENGTH)) {
        error = ERR_INVALID_PARAMETER;
        return {};
    }
    error = CrimsonEOS::initialize();
    if (error != OK) {
        return {};
    }

    std::shared_ptr<EosPlatform> platform;
    try {
        platform.reset(new EosPlatform);
    } catch (const std::bad_alloc &) {
        error = ERR_OUT_OF_MEMORY;
        return {};
    }
    const EOS_HPlatform handle = runtime.api.create_platform(&options);
    if (!handle) {
        error = ERR_CANT_CREATE;
        return {};
    }
    try {
        runtime.platforms.insert(handle);
    } catch (const std::bad_alloc &) {
        runtime.api.release_platform(handle);
        error = ERR_OUT_OF_MEMORY;
        return {};
    }
    platform->handle = handle;
    return platform;
}

EosPlatform::~EosPlatform() {
    if (get_handle()) {
        runtime.platforms.erase(handle);
        runtime.api.release_platform(handle);
    }
}

EOS_HPlatform EosPlatform::get_handle() const {
    return CrimsonEOS::is_initialized() && runtime.platforms.count(handle) ? handle : nullptr;
}

void EosPlatform::tick() {
    if (!get_handle() || ticking) {
        return;
    }
    const auto keep_alive = shared_from_this();
    ticking = true;
    runtime.api.tick_platform(handle);
    ticking = false;
}

void *EosPlatform::get_proc_address(const char *symbol) const {
    return get_handle() && symbol && symbol[0]
            ? reinterpret_cast<void *>(GetProcAddress(runtime.api.module, symbol))
            : nullptr;
}

void *EosPlatform::retain_callback(std::shared_ptr<void> data) {
    if (!get_handle() || !data || callbacks.size() >= max_pending_callbacks) {
        return nullptr;
    }
    void *client_data = data.get();
    try {
        return callbacks.emplace(client_data, std::move(data)).second ? client_data : nullptr;
    } catch (const std::bad_alloc &) {
        return nullptr;
    }
}

std::shared_ptr<void> EosPlatform::take_callback(void *client_data) {
    if (!on_scene_thread()) {
        return {};
    }
    const auto entry = callbacks.find(client_data);
    if (entry == callbacks.end()) {
        return {};
    }
    auto data = std::move(entry->second);
    callbacks.erase(entry);
    return data;
}
#endif

} // namespace crimson::integration
