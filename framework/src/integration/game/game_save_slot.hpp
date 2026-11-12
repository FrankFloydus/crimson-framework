// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "integration/game/game_operation.hpp"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/string.hpp>

namespace crimson::integration {

class GameRuntime;
class SaveSlot;

// An immutable set of options for capturing a save in one game slot.
class SaveRequest : public godot::RefCounted {
    GDCLASS(SaveRequest, godot::RefCounted)

public:
    // Return a request with a replacement display label.
    godot::Ref<SaveRequest> named(const godot::String &value) const;
    // Return a request with detached replacement display fields.
    godot::Ref<SaveRequest> with_context(const godot::Dictionary &value) const;
    // Return a request with a detached screenshot, or clear the preview when the image is unavailable.
    godot::Ref<SaveRequest> with_preview(const godot::Ref<godot::Image> &value) const;
    // Return a request for an autosave generation.
    godot::Ref<SaveRequest> as_autosave() const;
    // Capture and write the request through its originating game on the main thread.
    godot::Ref<GameOperation> submit() const;

protected:
    // Bind the immutable builder and operation submission methods to Godot.
    static void _bind_methods();

private:
    friend class SaveSlot;

    // Weak instance ID of the game that owns this request's save services.
    godot::ObjectID game_id;
    // Profile namespace for this save.
    godot::String profile;
    // Slot identifier for this save.
    godot::String slot;
    // Display name stored with the generation, initially the slot identifier.
    godot::String label;
    // Bounded display fields, containing only sanitized strings.
    godot::Dictionary context;
    // Private pixel snapshot shared between builder copies without mutation.
    godot::Ref<godot::Image> preview;
    // Select the save coordinator's autosave admission path when true.
    bool autosave = false;

    // Copy the request while sharing its private immutable metadata values.
    godot::Ref<SaveRequest> copy_request() const;
};

// Addresses one profile and slot on a game without retaining the game node.
class SaveSlot : public godot::RefCounted {
    GDCLASS(SaveSlot, godot::RefCounted)

public:
    // Bind the handle to the game and address selected by the composition facade.
    void configure(GameRuntime *game, const godot::String &profile, const godot::String &slot);
    // Start an immutable manual-save request for this address.
    godot::Ref<SaveRequest> save() const;
    // Restore the newest generation through the owning game's transition pipeline.
    godot::Ref<GameOperation> restore_latest() const;
    // Restore one exact generation using the sequence and ID returned in slot history.
    godot::Ref<GameOperation> restore(const godot::String &sequence, const godot::String &id) const;
    // List a page of generations, optionally before the supplied exclusive cursor.
    godot::Ref<GameOperation> history(const godot::String &before_sequence = {},
            const godot::String &before_id = {}, int64_t limit = 16) const;

protected:
    // Bind the slot's save, restore, and history operations to Godot.
    static void _bind_methods();

private:
    // Weak instance ID of the game that owns this address.
    godot::ObjectID game_id;
    // Profile namespace for this slot.
    godot::String profile;
    // Slot identifier used by the checkpoint coordinator.
    godot::String slot;
};

} // namespace crimson::integration
