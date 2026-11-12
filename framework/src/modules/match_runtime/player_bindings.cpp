// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "player_bindings.hpp"

#include <godot_cpp/core/class_db.hpp>

#include <limits>

namespace crimson::match_runtime {

using namespace godot;

void PlayerBindings::_bind_methods() {
    ClassDB::bind_method(D_METHOD("bind_peer", "peer_id", "player_id"), &PlayerBindings::bind_peer);
    ClassDB::bind_method(D_METHOD("unbind_peer", "peer_id", "generation"), &PlayerBindings::unbind_peer);
    ClassDB::bind_method(D_METHOD("get_player", "peer_id"), &PlayerBindings::get_player);
    ClassDB::bind_method(D_METHOD("get_binding_generation", "peer_id"), &PlayerBindings::get_binding_generation);
}

int64_t PlayerBindings::bind_peer(int64_t peer_id, const String &player_id) {
    if (peer_id <= 0 || player_id.strip_edges().is_empty() ||
            bindings.count(peer_id) || peers_by_player.count(player_id) ||
            last_generation == (std::numeric_limits<int64_t>::max)()) {
        return 0;
    }
    const int64_t generation = ++last_generation;
    bindings.emplace(peer_id, Binding{player_id, generation});
    peers_by_player.emplace(player_id, peer_id);
    return generation;
}

bool PlayerBindings::unbind_peer(int64_t peer_id, int64_t generation) {
    const auto found = bindings.find(peer_id);
    if (found == bindings.end() || found->second.generation != generation) {
        return false;
    }
    peers_by_player.erase(found->second.player_id);
    bindings.erase(found);
    return true;
}

String PlayerBindings::get_player(int64_t peer_id) const {
    const auto found = bindings.find(peer_id);
    return found == bindings.end() ? String() : found->second.player_id;
}

int64_t PlayerBindings::get_binding_generation(int64_t peer_id) const {
    const auto found = bindings.find(peer_id);
    return found == bindings.end() ? 0 : found->second.generation;
}

} // namespace crimson::match_runtime
