// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/game/game_operation.hpp"

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/signal.hpp>

namespace crimson::integration {

using namespace godot;

void CrimsonGameOperation::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_finished"), &CrimsonGameOperation::is_finished);
    ClassDB::bind_method(D_METHOD("get_result"), &CrimsonGameOperation::get_result);
    ClassDB::bind_method(D_METHOD("wait"), &CrimsonGameOperation::wait);

    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "result", PROPERTY_HINT_NONE, "",
                         PROPERTY_USAGE_NO_INSTANCE_STATE | PROPERTY_USAGE_READ_ONLY),
            "", "get_result");
    ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::DICTIONARY, "result")));
}

bool CrimsonGameOperation::is_finished() const {
    return finished;
}

Dictionary CrimsonGameOperation::get_result() const {
    return result.duplicate(true);
}

Variant CrimsonGameOperation::wait() {
    if (finished) {
        return get_result();
    }
    return Signal(this, "completed");
}

void CrimsonGameOperation::finish(const Dictionary &p_result) {
    if (finished) {
        return;
    }

    // A completion listener can release the last external reference to this operation.
    const Ref<CrimsonGameOperation> keep_alive(this);
    result = p_result.duplicate(true);
    finished = true;
    emit_signal("completed", get_result());
}

} // namespace crimson::integration
