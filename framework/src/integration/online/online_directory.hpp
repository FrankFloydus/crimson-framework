// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/session/session_transport.hpp"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>
#include <godot_cpp/core/object.hpp>

namespace crimson::integration {

class CrimsonGame;
class CrimsonOnlineDirectory;

// One typed, scene-thread completion for a directory publish or resolve request.
class CrimsonOnlineDirectoryRequest : public godot::RefCounted {
    GDCLASS(CrimsonOnlineDirectoryRequest, godot::RefCounted)

    friend class CrimsonGame;
    friend class CrimsonOnlineDirectory;

    // A request consumes one result of its declared kind.
    enum class Kind { Unconfigured, Publish, Resolve };
    // A cancelled publication can still require compensation when its provider completes.
    enum class State { Pending, Finished, Cancelled, CancelledSettled };

    // The operation started by the game's current directory.
    Kind kind = Kind::Unconfigured;
    // The terminal state, independent of the eventual game start outcome.
    State state = State::Pending;
    // Weak directory reference used to remove a listing completed after cancellation.
    godot::ObjectID directory_id;
    // Identifier of the new listing, when publication succeeds.
    godot::String listing;
    // Typed endpoint returned by a successful resolution.
    godot::Ref<session::CrimsonSessionEndpoint> endpoint;

    // Release an accepted listing, or arrange to compensate a late successful publication.
    void cancel();
    // Consume a successfully published listing into the active game's ownership.
    godot::String take_listing();

protected:
    // Register the trusted provider's typed completion methods.
    static void _bind_methods();

public:
    // Complete a publish with a unique listing ID, or remove a result received after cancellation.
    // ERR_UNAVAILABLE means the provider must dispose of the external listing itself.
    godot::Error publish(const godot::String &listing_id);
    // Complete a resolution request with an endpoint accepted by the selected transport.
    godot::Error resolve(const godot::Ref<session::CrimsonSessionEndpoint> &target);
    // Complete a pending request without a listing or endpoint.
    godot::Error fail();
    // Report whether the owning game cancelled or abandoned this request.
    bool is_cancelled() const;
};

// Optional trusted local backend for discovery and publication of ready Crimson games.
class CrimsonOnlineDirectory : public godot::Resource {
    GDCLASS(CrimsonOnlineDirectory, godot::Resource)

    friend class CrimsonGame;
    friend class CrimsonOnlineDirectoryRequest;

    // Begin a publication that can finish inline or later on the scene thread.
    godot::Ref<CrimsonOnlineDirectoryRequest> publish(const godot::Ref<session::CrimsonSessionEndpoint> &endpoint,
            const godot::StringName &world);
    // Begin a resolution that can finish inline or later on the scene thread.
    godot::Ref<CrimsonOnlineDirectoryRequest> resolve(const godot::String &listing_id);
    // Retire a listing owned by this provider.
    void unpublish(const godot::String &listing_id);

protected:
    // Begin publication in a native backend, or delegate to the scripted hook by default.
    virtual void begin_publish(const godot::Ref<session::CrimsonSessionEndpoint> &endpoint,
            const godot::StringName &world, const godot::Ref<CrimsonOnlineDirectoryRequest> &request);
    // Begin resolution in a native backend, or delegate to the scripted hook by default.
    virtual void begin_resolve(const godot::String &listing_id,
            const godot::Ref<CrimsonOnlineDirectoryRequest> &request);
    // Remove a native listing, or delegate to the scripted cleanup hook by default.
    virtual void remove_listing(const godot::String &listing_id);
    // Release directory membership associated with a resolved join, if this provider takes membership.
    virtual void leave_listing(const godot::String &listing_id);
    // Publish the mounted authority and complete the request on the scene thread.
    // A null endpoint lets the provider advertise a route it obtained from its transport.
    GDVIRTUAL3(_publish, godot::Ref<session::CrimsonSessionEndpoint>, godot::StringName,
            godot::Ref<CrimsonOnlineDirectoryRequest>)
    // Resolve a listing ID to its transport route and complete the request on the scene thread.
    GDVIRTUAL2(_resolve, godot::String, godot::Ref<CrimsonOnlineDirectoryRequest>)
    // Retire a published listing. Providers must also dispose of external state on shutdown.
    GDVIRTUAL1(_unpublish, godot::String)
    // Release any local membership from this lookup. The hook must tolerate a failed or pending lookup.
    GDVIRTUAL1(_leave, godot::String)
    // Register the Godot provider hooks.
    static void _bind_methods();
};

} // namespace crimson::integration
