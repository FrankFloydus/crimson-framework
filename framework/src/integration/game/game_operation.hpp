// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace crimson::integration {

class GameOperation : public godot::RefCounted {
    GDCLASS(GameOperation, godot::RefCounted)

    // Detached terminal result, empty until completion.
    godot::Dictionary result;
    // True once the operation has accepted its single terminal result.
    bool finished = false;

protected:
    // Registers script accessors and the completion signal.
    static void _bind_methods();

public:
    // Reports whether the owning runtime has completed the operation.
    bool is_finished() const;
    // Returns a detached copy of the terminal result, or an empty dictionary while pending.
    godot::Dictionary get_result() const;
    // Returns a completed result or a signal suitable for awaiting the pending result.
    godot::Variant wait();
    // Stores the first terminal result and emits completion on the owning main thread.
    void finish(const godot::Dictionary &p_result);
};

} // namespace crimson::integration
