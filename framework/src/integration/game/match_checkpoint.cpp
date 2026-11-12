// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/game/match_checkpoint.hpp"

#include <godot_cpp/core/class_db.hpp>

#include <utility>

using namespace godot;

namespace crimson::integration {
namespace {

// Validate a stable world key before capture or restore staging.
bool valid_world_id(const String &id) {
    if (id.is_empty() || id.length() > 128 || id.strip_edges() != id) {
        return false;
    }
    for (int64_t index = 0; index < id.length(); ++index) {
        if (id[index] == U'\0') {
            return false;
        }
    }
    return true;
}

// Test the native coordinator's explicit result flag.
bool succeeded(const Dictionary &result) {
    const Variant ok = result.get("ok", false);
    return ok.get_type() == Variant::BOOL && bool(ok);
}

// Wrap a successful value in the SaveCoordinator callback response.
Dictionary saved_value(const Variant &value) {
    Dictionary result;
    result["ok"] = true;
    result["value"] = value;
    return result;
}

} // namespace

void MatchCheckpointCandidate::_bind_methods() {
}

void MatchCheckpoint::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_context", "coordinator", "world_id"), &MatchCheckpoint::set_context);
    ClassDB::bind_method(D_METHOD("save_capture"), &MatchCheckpoint::save_capture);
    ClassDB::bind_method(D_METHOD("save_create_candidate"), &MatchCheckpoint::save_create_candidate);
    ClassDB::bind_method(D_METHOD("save_stage", "dto", "candidate"), &MatchCheckpoint::save_stage);
    ClassDB::bind_method(D_METHOD("get_candidate_world", "candidate"), &MatchCheckpoint::get_candidate_world);
    ClassDB::bind_method(D_METHOD("save_publish", "candidate"), &MatchCheckpoint::save_publish);
    ClassDB::bind_method(D_METHOD("notify_publication"), &MatchCheckpoint::notify_publication);
}

bool MatchCheckpoint::on_owner_thread() const {
    return std::this_thread::get_id() == owner_thread;
}

void MatchCheckpoint::set_context(const Ref<match_runtime::TransactionCoordinator> &coordinator,
        const String &world_id) {
    if (!on_owner_thread() || (stores == coordinator && active_world == world_id)) {
        return;
    }
    stores = coordinator;
    active_world = world_id;
    ++context_version;
}

Ref<MatchCheckpointCandidate> MatchCheckpoint::owned_candidate(const Variant &value) const {
    if (!on_owner_thread() || stores.is_null() || value.get_type() != Variant::OBJECT) {
        return {};
    }
    const Ref<MatchCheckpointCandidate> candidate = value;
    if (candidate.is_null() || candidate->checkpoint_id != get_instance_id() ||
            candidate->context_version != context_version) {
        return {};
    }
    return candidate;
}

Dictionary MatchCheckpoint::save_capture() const {
    if (!on_owner_thread() || stores.is_null() || !valid_world_id(active_world)) {
        return {};
    }
    const Dictionary snapshot = stores->capture_snapshot();
    const Variant committed_stores = snapshot.get("stores", Variant());
    if (!succeeded(snapshot) || committed_stores.get_type() != Variant::DICTIONARY) {
        return {};
    }
    Dictionary dto;
    dto["world_id"] = active_world;
    dto["stores"] = committed_stores;
    return saved_value(dto);
}

Dictionary MatchCheckpoint::save_create_candidate() const {
    if (!on_owner_thread() || stores.is_null()) {
        return {};
    }
    Ref<MatchCheckpointCandidate> candidate;
    candidate.instantiate();
    candidate->checkpoint_id = get_instance_id();
    candidate->context_version = context_version;
    return saved_value(candidate);
}

bool MatchCheckpoint::save_stage(const Variant &raw, const Variant &value) const {
    const Ref<MatchCheckpointCandidate> candidate = owned_candidate(value);
    if (candidate.is_null() || !candidate->world_id.is_empty() || raw.get_type() != Variant::DICTIONARY) {
        return false;
    }
    const Dictionary dto = raw;
    const Variant raw_world = dto.get("world_id", Variant());
    const Variant raw_stores = dto.get("stores", Variant());
    if (dto.size() != 2 || raw_world.get_type() != Variant::STRING || raw_stores.get_type() != Variant::DICTIONARY) {
        return false;
    }
    const String world_id = raw_world;
    if (!valid_world_id(world_id)) {
        return false;
    }

    const Ref<match_runtime::TransactionCoordinator> coordinator = stores;
    Dictionary context;
    context["world_id"] = world_id;
    const Dictionary prepared = coordinator->prepare_restore(raw_stores, context);
    const Ref<match_runtime::GameTransaction> restore = prepared.get("candidate", Variant());
    if (!succeeded(prepared) || restore.is_null() || candidate->context_version != context_version) {
        return false;
    }
    candidate->world_id = world_id;
    candidate->restore = restore;
    return true;
}

String MatchCheckpoint::get_candidate_world(const Variant &value) const {
    const Ref<MatchCheckpointCandidate> candidate = owned_candidate(value);
    return candidate.is_valid() && candidate->restore.is_valid() ? candidate->world_id : String();
}

bool MatchCheckpoint::save_publish(const Variant &value) {
    const Ref<MatchCheckpointCandidate> candidate = owned_candidate(value);
    if (candidate.is_null() || candidate->restore.is_null() || deferred_publication) {
        return false;
    }
    const Ref<match_runtime::TransactionCoordinator> coordinator = stores;
    const Dictionary result = coordinator->publish_restore(candidate->restore, true);
    if (!succeeded(result)) {
        return false;
    }

    const int64_t published_revision = result["revision"];
    deferred_publication.emplace(DeferredPublication{coordinator, published_revision});
    candidate->restore.unref();
    return true;
}

Error MatchCheckpoint::notify_publication() {
    if (!on_owner_thread()) {
        return ERR_BUSY;
    }
    if (!deferred_publication) {
        return ERR_DOES_NOT_EXIST;
    }

    DeferredPublication pending = std::move(*deferred_publication);
    deferred_publication.reset();
    const Error error = pending.coordinator->notify_restore_published(pending.revision);
    if (error != OK) {
        deferred_publication.emplace(std::move(pending));
    }
    return error;
}

void MatchCheckpoint::discard_publication() {
    if (on_owner_thread()) {
        deferred_publication.reset();
    }
}

} // namespace crimson::integration
