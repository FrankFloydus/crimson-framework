// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/string.hpp>

#ifdef CRIMSON_WITH_EOS
#include <eos_types.h>
#include <map>
#include <memory>
#endif

namespace crimson::integration {

// Diagnostic entry point for the optional native Epic Online Services runtime.
class CrimsonEOS : public godot::Object {
    GDCLASS(CrimsonEOS, godot::Object)

protected:
    // Register the static diagnostics on the Godot-facing type.
    static void _bind_methods();

public:
    // Load the addon-local SDK and initialize it once on the scene thread.
    static godot::Error initialize();

    // Read the version of the initialized EOS runtime, or an empty string.
    static godot::String get_runtime_version();

    // Check whether Crimson owns an initialized EOS runtime on the scene thread.
    static bool is_initialized();

    // Release outstanding platforms and shut down the SDK during extension teardown.
    static void shutdown_runtime();
};

#ifdef CRIMSON_WITH_EOS
// A scene-thread-owned platform shared by one provider's identity, lobby, and transport services.
class EosPlatform final : public std::enable_shared_from_this<EosPlatform> {
public:
    // Create one configured platform and report the reason when it could not be opened.
    static std::shared_ptr<EosPlatform> create(const EOS_Platform_Options &options, godot::Error &error);

    // Release the platform while the EOS process runtime is still active.
    ~EosPlatform();

    // Prevent independent native objects from owning the same SDK handle.
    EosPlatform(const EosPlatform &) = delete;

    // Prevent overwriting the ownership of an existing SDK handle.
    EosPlatform &operator=(const EosPlatform &) = delete;

    // Read the active SDK platform handle for interface acquisition.
    EOS_HPlatform get_handle() const;

    // Dispatch SDK work and callbacks from the owning scene thread.
    void tick();

    // Resolve an SDK export while this platform and its process runtime remain active.
    void *get_proc_address(const char *symbol) const;

    // Retain a bounded one-shot callback context and return its SDK ClientData pointer.
    void *retain_callback(std::shared_ptr<void> data);

    // Consume the retained context once on the scene thread.
    std::shared_ptr<void> take_callback(void *client_data);

private:
    // Construct a platform shell before opening the SDK handle.
    EosPlatform() = default;

    // SDK handle registered with the process runtime for release before shutdown.
    EOS_HPlatform handle = nullptr;

    // Guard against reentrant ticks when an SDK callback calls into game code.
    bool ticking = false;

    // Keep unfinished SDK ClientData alive through platform release.
    std::map<void *, std::shared_ptr<void>> callbacks;
};
#endif

} // namespace crimson::integration
