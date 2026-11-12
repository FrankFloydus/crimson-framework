// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "integration/game/game_operation.hpp"

#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/classes/multiplayer_peer.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include <cstdint>
#include <map>

namespace crimson::integration {

// Carries bounded command receipts and authoritative world notices within one session scope.
class MatchNetwork : public godot::Node {
    GDCLASS(MatchNetwork, godot::Node)

public:
    // Keep transport deadlines running while the game tree is paused.
    MatchNetwork();

    // Capture the connected peer installed in this scope by the current session.
    godot::Error bind_transport();
    // Bind the authority callback that accepts a trusted sender and a detached command intent.
    void set_dispatcher(const godot::Callable &callback);
    // Send one client command and retain its receipt or terminal transport error.
    godot::Ref<GameOperation> submit_remote(const godot::Dictionary &intent);
    // Release the captured session and settle its pending commands as disconnected.
    void reset();
    // Send the pending destination and epoch to connected clients.
    godot::Error announce_world(const godot::StringName &world_id, int64_t epoch);
    // Send the terminal transition outcome for an announced epoch.
    godot::Error finish_world(int64_t epoch, bool ok, bool recovery);

    // Install matching reliable RPC declarations in the active multiplayer scope.
    void _enter_tree() override;
    // Settle outstanding work before this node leaves its multiplayer scope.
    void _exit_tree() override;
    // Detect transport replacement, disconnects, and expired receipts.
    void _process(double delta) override;

protected:
    // Register RPC entry points and the client world observation signals.
    static void _bind_methods();

private:
    // Stores the operation and deadline for one pending receipt on the captured connection.
    struct PendingCommand {
        // Operation returned to the caller when the command was submitted.
        godot::Ref<GameOperation> operation;
        // Monotonic time at which a missing receipt settles as a timeout.
        uint64_t deadline_msec = 0;
    };

    // MultiplayerAPI captured when the containing session starts.
    godot::Ref<godot::MultiplayerAPI> network;
    // Peer captured from the MultiplayerAPI at session start.
    godot::Ref<godot::MultiplayerPeer> transport;
    // Authority dispatch callback provided by the game flow.
    godot::Callable dispatcher;
    // Outstanding client submissions keyed by their wire receipt IDs.
    std::map<int64_t, PendingCommand> pending;
    // Last receipt ID allocated by this node, retained across resets.
    int64_t last_request_id = 0;
    // Changes when a captured session ends, including during an authority callback.
    uint64_t generation = 0;
    // Prevents a receipt callback from rebinding a node while it exits the tree.
    bool leaving_tree = false;

    // Check whether the captured API still owns the same connected peer.
    bool owns_transport() const;
    // Check whether this node can send control traffic for the bound authority.
    bool is_bound_authority() const;
    // Check whether the current RPC came from the authority on this client's session.
    bool is_incoming_authority() const;
    // Resolve the current RPC sender as a connected client of this bound authority.
    int64_t get_incoming_client() const;
    // Dispatch a bounded client command and send its receipt to the same connection.
    void rpc_submit(int64_t request_id, const godot::Dictionary &intent);
    // Settle the matching pending command from an authority-only receipt.
    void rpc_receipt(int64_t request_id, const godot::Dictionary &result);
    // Forward a valid authority destination notice to Game Flow.
    void rpc_world_announced(const godot::StringName &world_id, int64_t epoch);
    // Forward a valid authority transition outcome to Game Flow.
    void rpc_world_finished(int64_t epoch, bool ok, bool recovery);
};

} // namespace crimson::integration