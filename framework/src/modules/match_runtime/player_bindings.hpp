// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstdint>
#include <map>

namespace crimson::match_runtime {

class PlayerBindings : public godot::RefCounted {
    GDCLASS(PlayerBindings, godot::RefCounted)

public:
    // Bind an admitted peer to an unused player identity and return the new generation.
    int64_t bind_peer(int64_t peer_id, const godot::String &player_id);
    // Remove a binding only when the caller holds its current generation.
    bool unbind_peer(int64_t peer_id, int64_t generation);
    // Return the player associated with a peer, or an empty ID.
    godot::String get_player(int64_t peer_id) const;
    // Return the current binding generation, or zero for an unbound peer.
    int64_t get_binding_generation(int64_t peer_id) const;

protected:
    // Expose trusted player binding operations to Godot.
    static void _bind_methods();

private:
    struct Binding {
        // Stable player identity supplied by trusted ingress.
        godot::String player_id;
        // Unique generation assigned when the binding was created.
        int64_t generation;
    };

    // Current peer-to-player bindings.
    std::map<int64_t, Binding> bindings;
    // Reverse lookup enforcing one live peer per player.
    std::map<godot::String, int64_t> peers_by_player;
    // Last allocated generation, retained across unbinds.
    int64_t last_generation = 0;
};

} // namespace crimson::match_runtime
