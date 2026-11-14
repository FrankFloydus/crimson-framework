// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace crimson::integration {

// Immutable, typed outcome shared by direct and retained game actions.
class GameResult : public godot::RefCounted {
    GDCLASS(GameResult, godot::RefCounted)

    // Whether the owning action reached its success boundary.
    bool ok = false;
    // Stable machine-readable action result code.
    godot::String code;
    // Human-readable detail suitable for game presentation.
    godot::String message;
    // Optional transport or teardown reason.
    godot::StringName reason;
    // Published lobby or server locator on successful online hosting.
    godot::String listing_id;
    // World activated by a successful start or transition.
    godot::StringName world_id;
    // Epoch of the committed world.
    int64_t world_epoch = 0;

protected:
    // Bind the read-only typed result fields.
    static void _bind_methods();

public:
    // Construct a result from the existing terminal operation fields.
    static godot::Ref<GameResult> from_dictionary(const godot::Dictionary &value);
    // Construct a synchronous success or rejection.
    static godot::Ref<GameResult> make(bool ok, const godot::String &code, const godot::String &message);
    // Report whether the action succeeded.
    bool is_ok() const;
    // Return the stable result code.
    godot::String get_code() const;
    // Return the human-readable action detail.
    godot::String get_message() const;
    // Return the optional session or transport reason.
    godot::StringName get_reason() const;
    // Return the published online locator when present.
    godot::String get_listing_id() const;
    // Return the activated world identifier when present.
    godot::StringName get_world_id() const;
    // Return the activated world epoch when present.
    int64_t get_world_epoch() const;
};

class GameOperation : public godot::RefCounted {
    GDCLASS(GameOperation, godot::RefCounted)

    // Operation families that provide terminal diagnostics in the Godot console.
    enum class Diagnostic { None, WorldTravel, Save, Restore };

    // Detached terminal result, empty until completion.
    godot::Dictionary result;
    // Immutable projection of the same terminal outcome for typed callers.
    godot::Ref<GameResult> typed_result;
    // True once the operation has accepted its single terminal result.
    bool finished = false;
    // Selects the action and label for the terminal diagnostic.
    Diagnostic diagnostic = Diagnostic::None;
    // Bounded, validated identifier text for the requested destination or checkpoint.
    godot::String diagnostic_target;

    // Fill missing outcome details and report the terminal result before user callbacks.
    void report_diagnostic();

protected:
    // Registers script accessors and the completion signal.
    static void _bind_methods();

public:
    // Create a pending travel operation that reports its requested world.
    static godot::Ref<GameOperation> for_travel(const godot::StringName &world);
    // Create a pending checkpoint operation that reports the requested profile and slot.
    static godot::Ref<GameOperation> for_checkpoint(bool restore, const godot::String &profile,
            const godot::String &slot);
    // Identifies a storage operation whose write may already have crossed its commit boundary.
    bool is_checkpoint_save() const;
    // Reports whether the owning runtime has completed the operation.
    bool is_finished() const;
    // Returns a detached copy of the terminal result, or an empty dictionary while pending.
    godot::Dictionary get_result() const;
    // Returns a completed result or a signal suitable for awaiting the pending result.
    godot::Variant wait();
    // Return the typed outcome or a signal that settles to it.
    godot::Variant wait_result();
    // Return the typed outcome, or null while pending.
    godot::Ref<GameResult> get_typed_result() const;
    // Stores the first terminal result and emits completion on the owning main thread.
    void finish(const godot::Dictionary &p_result);
};

} // namespace crimson::integration
