// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/game/crimson_game.hpp"

#include <godot_cpp/classes/time.hpp>

#include <utility>

using namespace godot;

namespace crimson::integration {
namespace {

// Bound one provider request so a stalled directory cannot hold a world indefinitely.
constexpr uint64_t directory_timeout_ms = 15000;

// Directory work is consumed by the scene thread against a monotonic deadline.
uint64_t directory_deadline() {
    return Time::get_singleton()->get_ticks_msec() + directory_timeout_ms;
}

} // namespace

Ref<CrimsonGameOperation> CrimsonGame::host_online(const String &public_address, const String &bind_address) {
    return begin_online_host(ImmediateMode::Host, public_address, bind_address);
}

Ref<CrimsonGameOperation> CrimsonGame::start_dedicated_online(const String &public_address, const String &bind_address) {
    return begin_online_host(ImmediateMode::Dedicated, public_address, bind_address);
}

Ref<CrimsonGameOperation> CrimsonGame::check_online_start() const {
    if (!session_node || !is_inside_tree() || definition.is_null() || online_directory.is_null()) {
        return completed(false, "OnlineUnavailable", "Configure an online directory before starting an online game.");
    }
    if (phase != IDLE || immediate_active || cleaning || publishing || online_start.operation.is_valid() ||
            session_node->get_state() != session::CrimsonGameSession::IDLE) {
        return completed(false, "GameBusy", "Leave the current game before starting another.");
    }
    if (session_node->get_authenticator().is_null()) {
        return completed(false, "OnlineAuthenticationRequired", "Configure peer authentication for online games.");
    }
    return {};
}

Ref<CrimsonGameOperation> CrimsonGame::begin_online_host(
        ImmediateMode mode, const String &public_address, const String &bind_address) {
    if (const auto rejected = check_online_start(); rejected.is_valid()) {
        return rejected;
    }
    const Ref<CrimsonMultiplayerRules> rules = definition->get_multiplayer_rules();
    if (rules.is_null()) {
        return completed(false, "MultiplayerDisabled", "Configure multiplayer rules before hosting online.");
    }
    const String address = public_address.strip_edges();
    const auto endpoint = session::CrimsonSessionEndpoint::direct(address, rules->get_port());
    if (!address.is_empty() && (endpoint.is_null() || address == "*" || address == "0.0.0.0" || address == "::" ||
            address.length() > 255)) {
        return completed(false, "InvalidAddress", "Advertise a host address guests can connect to.");
    }
    Ref<CrimsonGameOperation> operation;
    operation.instantiate();
    online_start.operation = operation;
    online_start.authenticator = session_node->get_authenticator();
    online_start.publication = endpoint;
    online_start.hosting = true;
    const auto world_start = start_immediate(mode, bind_address.strip_edges(), {}, true);
    if (online_start.operation == operation) {
        online_start.world_start = world_start;
    }
    return operation;
}

Ref<CrimsonGameOperation> CrimsonGame::join_online(const String &listing_id) {
    if (const auto rejected = check_online_start(); rejected.is_valid()) {
        return rejected;
    }
    const String id = listing_id.strip_edges();
    if (id.is_empty() || id.length() > 192) {
        return completed(false, "InvalidListing", "Enter a valid online listing ID.");
    }
    Ref<CrimsonGameOperation> operation;
    operation.instantiate();
    online_start.operation = operation;
    online_start.authenticator = session_node->get_authenticator();
    online_start.deadline = directory_deadline();
    online_join = id;
    const auto directory = online_directory;
    const auto request = directory->resolve(id);
    if (online_start.operation == operation) {
        online_start.request = request;
    } else {
        request->cancel();
    }
    return operation;
}

String CrimsonGame::get_online_listing() const {
    return immediate_active && phase == RUNNING ? online_listing : String();
}

void CrimsonGame::process_online_start() {
    const auto operation = online_start.operation;
    if (online_start.world_start.is_valid()) {
        if (!online_start.world_start->is_finished()) {
            return;
        }
        const Dictionary result = online_start.world_start->get_result();
        if (!bool(result.get("ok", false))) {
            fail_online_start(result);
            return;
        }
        if (!online_start.hosting) {
            finish_online_start();
            return;
        }
        if (online_start.request.is_null()) {
            online_start.deadline = directory_deadline();
            const auto directory = online_directory;
            const auto endpoint = online_start.publication;
            const auto request = directory->publish(endpoint, world_id);
            if (online_start.operation == operation) {
                online_start.request = request;
            } else {
                request->cancel();
            }
            return;
        }
    }

    const auto request = online_start.request;
    if (Time::get_singleton()->get_ticks_msec() >= online_start.deadline) {
        fail_online_start(outcome(false, "DirectoryTimeout", "The online directory did not respond."));
        return;
    }
    if (request.is_null() || request->state != CrimsonOnlineDirectoryRequest::State::Finished) {
        return;
    }
    if (online_start.hosting) {
        online_listing = request->take_listing();
        if (online_listing.is_empty()) {
            fail_online_start(outcome(false, "ListingPublishFailed", "The online directory could not publish this game."));
            return;
        }
        const Error admission = session_node->set_accepting_connections(true);
        if (online_start.operation != operation) {
            return;
        }
        if (admission != OK) {
            fail_online_start(outcome(false, "StartFailed", "The published game could not open admission."));
        } else {
            finish_online_start();
        }
        return;
    }

    const auto endpoint = request->endpoint;
    if (endpoint.is_null()) {
        fail_online_start(outcome(false, "ListingUnavailable", "The online listing could not be resolved."));
        return;
    }
    online_start.request.unref();
    online_start.deadline = 0;
    const auto world_start = start_immediate(ImmediateMode::Join, {}, endpoint, true);
    if (online_start.operation == operation) {
        online_start.world_start = world_start;
    }
}

Ref<CrimsonGameOperation> CrimsonGame::retire_online_start() {
    const OnlineStart retired = std::exchange(online_start, {});
    if (retired.request.is_valid()) {
        retired.request->cancel();
    }
    return retired.operation;
}

void CrimsonGame::retire_online_presence() {
    const String listing = std::exchange(online_listing, String());
    const String joined = std::exchange(online_join, String());
    const auto directory = online_directory;
    if (!listing.is_empty() && directory.is_valid()) {
        directory->unpublish(listing);
    }
    if (!joined.is_empty() && directory.is_valid()) {
        directory->leave_listing(joined);
    }
}

void CrimsonGame::fail_online_start(const Dictionary &result) {
    publishing = true;
    const auto operation = retire_online_start();
    retire_online_presence();
    publishing = false;
    if (immediate_active) {
        immediate->leave();
    }
    operation->finish(result);
}

void CrimsonGame::finish_online_start() {
    const auto operation = std::exchange(online_start, {}).operation;
    const int64_t ready_epoch = world_epoch;
    Dictionary result = outcome(true, "Ok");
    if (!online_listing.is_empty()) {
        result["listing_id"] = online_listing;
    }
    set_phase(RUNNING);
    if (immediate_active && phase == RUNNING && world_epoch == ready_epoch) {
        emit_signal("world_ready", world_id, world_epoch);
    }
    operation->finish(result);
}

} // namespace crimson::integration
