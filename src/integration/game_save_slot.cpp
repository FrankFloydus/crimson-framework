// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "game_save_slot.hpp"

#include "crimson_game.hpp"
#include "modules/save/save_presentation.hpp"

#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/object.hpp>

using namespace godot;

namespace crimson::integration {
namespace {

// Resolve the weak game reference while Godot can safely access its scene tree.
CrimsonGame *resolve_game(ObjectID id) {
    const OS *os = OS::get_singleton();
    if (id.is_null() || os->get_thread_caller_id() != os->get_main_thread_id()) {
        return nullptr;
    }
    CrimsonGame *game = Object::cast_to<CrimsonGame>(ObjectDB::get_instance(id));
    return game && game->is_inside_tree() && !game->is_queued_for_deletion() ? game : nullptr;
}

// Complete a facade operation with the same error fields as game operations.
Ref<CrimsonGameOperation> failed_operation(const String &code, const String &message, const CrimsonGame *game = nullptr) {
    Dictionary result;
    result["ok"] = false;
    result["code"] = code;
    result["message"] = message;
    result["world_id"] = game ? String(game->get_world_id()) : String();
    result["world_epoch"] = game ? game->get_world_epoch() : int64_t(0);
    Ref<CrimsonGameOperation> operation;
    operation.instantiate();
    operation->finish(result);
    return operation;
}

// Report a stale, detached, or otherwise inaccessible originating game.
Ref<CrimsonGameOperation> unavailable_operation() {
    return failed_operation("Unavailable", "The originating CrimsonGame must be active in the scene tree and accessed on the main thread.");
}

} // namespace

void CrimsonSaveRequest::_bind_methods() {
    ClassDB::bind_method(D_METHOD("named", "label"), &CrimsonSaveRequest::named);
    ClassDB::bind_method(D_METHOD("with_context", "context"), &CrimsonSaveRequest::with_context);
    ClassDB::bind_method(D_METHOD("with_preview", "image"), &CrimsonSaveRequest::with_preview);
    ClassDB::bind_method(D_METHOD("as_autosave"), &CrimsonSaveRequest::as_autosave);
    ClassDB::bind_method(D_METHOD("submit"), &CrimsonSaveRequest::submit);
}

Ref<CrimsonSaveRequest> CrimsonSaveRequest::copy_request() const {
    Ref<CrimsonSaveRequest> request;
    request.instantiate();
    request->game_id = game_id;
    request->profile = profile;
    request->slot = slot;
    request->label = label;
    request->context = context;
    request->preview = preview;
    request->autosave = autosave;
    return request;
}

Ref<CrimsonSaveRequest> CrimsonSaveRequest::named(const String &value) const {
    Ref<CrimsonSaveRequest> request = copy_request();
    request->label = value;
    return request;
}

Ref<CrimsonSaveRequest> CrimsonSaveRequest::with_context(const Dictionary &value) const {
    Ref<CrimsonSaveRequest> request = copy_request();
    const Dictionary bounded = save::SavePresentation::make_metadata({}, 0, value);
    request->context = bounded["context"];
    return request;
}

Ref<CrimsonSaveRequest> CrimsonSaveRequest::with_preview(const Ref<Image> &value) const {
    Ref<CrimsonSaveRequest> request = copy_request();
    request->preview.unref();
    if (value.is_valid() && !value->is_empty() && !value->is_compressed()) {
        request->preview = Image::create_from_data(value->get_width(), value->get_height(), value->has_mipmaps(),
                value->get_format(), value->get_data());
    }
    return request;
}

Ref<CrimsonSaveRequest> CrimsonSaveRequest::as_autosave() const {
    Ref<CrimsonSaveRequest> request = copy_request();
    request->autosave = true;
    return request;
}

Ref<CrimsonGameOperation> CrimsonSaveRequest::submit() const {
    CrimsonGame *game = resolve_game(game_id);
    if (!game) {
        return unavailable_operation();
    }
    const int64_t timestamp = static_cast<int64_t>(Time::get_singleton()->get_unix_time_from_system());
    const Dictionary metadata = save::SavePresentation::make_metadata(label, timestamp, context, preview);
    return game->save(String(profile), String(slot), metadata, autosave);
}

void CrimsonSaveSlot::_bind_methods() {
    ClassDB::bind_method(D_METHOD("save"), &CrimsonSaveSlot::save);
    ClassDB::bind_method(D_METHOD("restore_latest"), &CrimsonSaveSlot::restore_latest);
    ClassDB::bind_method(D_METHOD("restore", "sequence", "id"), &CrimsonSaveSlot::restore);
    ClassDB::bind_method(D_METHOD("history", "before_sequence", "before_id", "limit"),
            &CrimsonSaveSlot::history, DEFVAL(String()), DEFVAL(String()), DEFVAL(16));
}

void CrimsonSaveSlot::configure(CrimsonGame *game, const String &p_profile, const String &p_slot) {
    game_id = game ? ObjectID(game->get_instance_id()) : ObjectID();
    profile = p_profile;
    slot = p_slot;
}

Ref<CrimsonSaveRequest> CrimsonSaveSlot::save() const {
    Ref<CrimsonSaveRequest> request;
    request.instantiate();
    request->game_id = game_id;
    request->profile = profile;
    request->slot = slot;
    request->label = slot;
    return request;
}

Ref<CrimsonGameOperation> CrimsonSaveSlot::restore_latest() const {
    CrimsonGame *game = resolve_game(game_id);
    return game ? game->restore(String(profile), String(slot)) : unavailable_operation();
}

Ref<CrimsonGameOperation> CrimsonSaveSlot::restore(const String &sequence, const String &id) const {
    CrimsonGame *game = resolve_game(game_id);
    if (!game) {
        return unavailable_operation();
    }
    if (sequence.is_empty() || id.is_empty()) {
        return failed_operation("InvalidArgument", "Select a generation by both its sequence and ID.", game);
    }
    // Lifecycle signals can release the slot or its caller during restore admission.
    return game->restore(String(profile), String(slot), String(sequence), String(id));
}

Ref<CrimsonGameOperation> CrimsonSaveSlot::history(const String &before_sequence, const String &before_id, int64_t limit) const {
    CrimsonGame *game = resolve_game(game_id);
    return game ? game->save_history(String(profile), String(slot), String(before_sequence), String(before_id), limit) :
            unavailable_operation();
}

} // namespace crimson::integration
