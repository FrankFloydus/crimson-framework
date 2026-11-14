// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "rebinding.hpp"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/input_event_joypad_button.hpp>
#include <godot_cpp/classes/input_event_joypad_motion.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/time.hpp>

#include <cmath>
#include <map>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace crimson::rebinding {
using namespace godot;
using input::InputBinding;

namespace {

// One compact local profile version with bounded UTF-8 data and row count.
constexpr int64_t profile_version = 1;
// Limit the bytes accepted before handing untrusted data to the JSON parser.
constexpr int64_t max_profile_bytes = 65536;
// Limit overridden slots independently of the input layout size.
constexpr int64_t max_profile_slots = 256;
// Limit a serialized slot identifier before interning it.
constexpr int64_t max_slot_id = 256;

// Limit profile I/O to an explicit relative file inside Godot's user directory.
bool valid_path(const String &path) {
    if (!path.begins_with("user://") || path.length() > 240 || path.contains("\\") || path.substr(7).contains(":")) {
        return false;
    }
    const PackedStringArray segments = path.substr(7).split("/");
    for (const String &part : segments) {
        if (part.is_empty() || part == "." || part == ".." || part.ends_with(".") || part.ends_with(" ") ||
                part.contains("*") || part.contains("?") || part.contains("<") || part.contains(">") ||
                part.contains("\"") || part.contains("|")) {
            return false;
        }
        for (int64_t i = 0; i < part.length(); ++i) {
            if (part[i] < 0x20) {
                return false;
            }
        }
    }
    return segments.size() != 0;
}

// Store only the modifier flags interpreted by the local Controls matcher.
int64_t modifier_flags(const InputEventWithModifiers &event) {
    return (event.is_shift_pressed() ? 1 : 0) | (event.is_alt_pressed() ? 2 : 0) |
            (event.is_ctrl_pressed() ? 4 : 0) | (event.is_meta_pressed() ? 8 : 0);
}

// Restore the known chord flags without importing arbitrary event properties.
void apply_flags(InputEventWithModifiers &event, int64_t flags) {
    event.set_shift_pressed((flags & 1) != 0);
    event.set_alt_pressed((flags & 2) != 0);
    event.set_ctrl_pressed((flags & 4) != 0);
    event.set_meta_pressed((flags & 8) != 0);
}

// Encode normalized editable controls, keeping joystick device selection in the authored layout.
Variant encode_event(const Ref<InputEvent> &event) {
    Dictionary encoded;
    if (const Ref<InputEventKey> key = event; key.is_valid()) {
        encoded["kind"] = "key";
        encoded["physical"] = int64_t(key->get_physical_keycode());
        encoded["logical"] = int64_t(key->get_keycode());
        encoded["flags"] = modifier_flags(*key.ptr());
        return encoded;
    }
    if (const Ref<InputEventMouseButton> button = event; button.is_valid()) {
        encoded["kind"] = "mouse";
        encoded["button"] = int64_t(button->get_button_index());
        encoded["flags"] = modifier_flags(*button.ptr());
        return encoded;
    }
    if (const Ref<InputEventJoypadButton> button = event; button.is_valid()) {
        encoded["kind"] = "pad";
        encoded["button"] = int64_t(button->get_button_index());
        return encoded;
    }
    if (const Ref<InputEventJoypadMotion> axis = event; axis.is_valid()) {
        encoded["kind"] = "axis";
        encoded["axis"] = int64_t(axis->get_axis());
        encoded["sign"] = axis->get_axis_value() < 0.0f ? -1 : 1;
        return encoded;
    }
    return {};
}

// Validate a bounded integral field independent of how JSON represents numbers.
bool integer_field(const Variant &field, int64_t lower, int64_t upper, int64_t &value) {
    if (field.get_type() != Variant::FLOAT && field.get_type() != Variant::INT) {
        return false;
    }
    const double number = field;
    if (!std::isfinite(number) || number != std::trunc(number) || number < lower || number > upper) {
        return false;
    }
    value = static_cast<int64_t>(number);
    return true;
}

// Decode a bounded schema into one fresh event compatible with the authored device family.
bool decode_event(const Variant &encoded, const Ref<InputEvent> &authored, Ref<InputEvent> &event) {
    if (encoded.get_type() == Variant::NIL) {
        event.unref();
        return true;
    }
    if (encoded.get_type() != Variant::DICTIONARY || authored.is_null()) {
        return false;
    }
    const Dictionary source = encoded;
    const Variant tag = source.get("kind", Variant());
    if (tag.get_type() != Variant::STRING) {
        return false;
    }
    const String kind = tag;
    const bool gamepad = Ref<InputEventJoypadButton>(authored).is_valid() || Ref<InputEventJoypadMotion>(authored).is_valid();
    if (gamepad != (kind == "pad" || kind == "axis")) {
        return false;
    }
    int64_t first = 0;
    int64_t second = 0;
    if (kind == "key") {
        int64_t flags = 0;
        if (source.size() != 4 || !integer_field(source.get("physical", Variant()), 0, KEY_UNKNOWN - 1, first) ||
                !integer_field(source.get("logical", Variant()), 0, KEY_UNKNOWN - 1, second) ||
                (first == 0) == (second == 0) || first == KEY_ESCAPE || second == KEY_ESCAPE ||
                !integer_field(source.get("flags", Variant()), 0, 15, flags)) {
            return false;
        }
        Ref<InputEventKey> key;
        key.instantiate();
        key->set_physical_keycode(static_cast<Key>(first));
        key->set_keycode(static_cast<Key>(second));
        apply_flags(*key.ptr(), flags);
        event = key;
        return true;
    }
    if (kind == "mouse") {
        if (source.size() != 3 || !integer_field(source.get("button", Variant()), MOUSE_BUTTON_LEFT, MOUSE_BUTTON_XBUTTON2, first) ||
                !integer_field(source.get("flags", Variant()), 0, 15, second)) {
            return false;
        }
        Ref<InputEventMouseButton> button;
        button.instantiate();
        button->set_button_index(static_cast<MouseButton>(first));
        apply_flags(*button.ptr(), second);
        event = button;
        return true;
    }
    if (kind == "pad") {
        if (source.size() != 2 || !integer_field(source.get("button", Variant()), 0, JOY_BUTTON_MAX - 1, first) ||
                first == JOY_BUTTON_BACK) {
            return false;
        }
        Ref<InputEventJoypadButton> button;
        button.instantiate();
        button->set_button_index(static_cast<JoyButton>(first));
        button->set_device(authored->get_device());
        event = button;
        return true;
    }
    if (kind == "axis") {
        if (source.size() != 3 || !integer_field(source.get("axis", Variant()), 0, JOY_AXIS_MAX - 1, first) ||
                !integer_field(source.get("sign", Variant()), -1, 1, second) || second == 0) {
            return false;
        }
        Ref<InputEventJoypadMotion> axis;
        axis.instantiate();
        axis->set_axis(static_cast<JoyAxis>(first));
        axis->set_axis_value(static_cast<float>(second));
        axis->set_device(authored->get_device());
        event = axis;
        return true;
    }
    return false;
}

#ifdef _WIN32
// Resolve a validated Godot path for same-directory Windows publication.
std::wstring wide_path(const String &path) {
    const auto wide = ProjectSettings::get_singleton()->globalize_path(path).replace("/", "\\").utf16();
    return std::wstring(wide.get_data(), wide.get_data() + wide.length());
}

// Write a private sibling and atomically publish it without truncating the prior file on error.
Error publish_profile(const String &path, const PackedByteArray &bytes) {
    const String temp = path + String(".tmp-") + String::num_int64(OS::get_singleton()->get_process_id()) +
            String("-") + String::num_uint64(Time::get_singleton()->get_ticks_usec());
    const auto temp_path = wide_path(temp);
    const auto target_path = wide_path(path);
    const HANDLE file = CreateFileW(temp_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return ERR_CANT_CREATE;
    }
    const auto expected = static_cast<DWORD>(bytes.size());
    DWORD written = 0;
    const bool staged = WriteFile(file, bytes.ptr(), expected, &written, nullptr) && written == expected &&
            FlushFileBuffers(file);
    const bool closed = CloseHandle(file);
    if (!staged || !closed || !MoveFileExW(temp_path.c_str(), target_path.c_str(),
                                 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp_path.c_str());
        return ERR_FILE_CANT_WRITE;
    }
    return OK;
}
#endif

} // namespace

Error Rebinding::save(const String &path) const {
    if (!get_owner()) {
        return ERR_UNAVAILABLE;
    }
    if (pending.is_valid() || controls->is_capturing()) {
        return ERR_BUSY;
    }
    if (!valid_path(path)) {
        return ERR_INVALID_PARAMETER;
    }
    Dictionary overrides;
    const auto snapshots = slots();
    for (int64_t i = 0; i < snapshots.size(); ++i) {
        const Ref<BindingSlot> slot = snapshots[i];
        const auto original = controls->default_binding(slot->id);
        const Variant current = encode_event(slot->event);
        const Variant authored = original.is_valid() ? encode_event(original->get_event()) : Variant();
        if (current == authored) {
            continue;
        }
        if (overrides.size() == max_profile_slots || String(slot->id).length() > max_slot_id) {
            return ERR_PARAMETER_RANGE_ERROR;
        }
        overrides[String(slot->id)] = current;
    }
    Dictionary document;
    document["version"] = profile_version;
    document["overrides"] = overrides;
    const auto bytes = JSON::stringify(document).to_utf8_buffer();
    if (bytes.size() > max_profile_bytes) {
        return ERR_PARAMETER_RANGE_ERROR;
    }
#ifdef _WIN32
    return publish_profile(path, bytes);
#else
    return ERR_UNAVAILABLE;
#endif
}

Error Rebinding::load(const String &path) {
    if (!get_owner()) {
        return ERR_UNAVAILABLE;
    }
    if (pending.is_valid() || controls->is_capturing()) {
        return ERR_BUSY;
    }
    if (!valid_path(path)) {
        return ERR_INVALID_PARAMETER;
    }
    const Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
    if (file.is_null()) {
        return FileAccess::get_open_error();
    }
    const uint64_t length = file->get_length();
    if (length > max_profile_bytes || length == 0) {
        return ERR_FILE_CORRUPT;
    }
    const auto bytes = file->get_buffer(static_cast<int64_t>(length));
    if (bytes.size() != static_cast<int64_t>(length)) {
        return ERR_FILE_CANT_READ;
    }
    const Variant document = JSON::parse_string(bytes.get_string_from_utf8());
    if (document.get_type() != Variant::DICTIONARY) {
        return ERR_FILE_CORRUPT;
    }
    const Dictionary profile = document;
    int64_t version = 0;
    const Variant data = profile.get("overrides", Variant());
    if (profile.size() != 2 || !integer_field(profile.get("version", Variant()), profile_version, profile_version, version) ||
            data.get_type() != Variant::DICTIONARY) {
        return ERR_FILE_CORRUPT;
    }
    const Dictionary overrides = data;
    if (overrides.size() > max_profile_slots) {
        return ERR_FILE_CORRUPT;
    }

    TypedArray<InputBinding> replacements;
    std::map<String, Ref<InputBinding>> by_id;
    const auto snapshots = slots();
    for (int64_t i = 0; i < snapshots.size(); ++i) {
        const Ref<BindingSlot> slot = snapshots[i];
        const auto original = controls->default_binding(slot->id);
        if (original.is_null()) {
            return ERR_UNCONFIGURED;
        }
        replacements.push_back(original);
        by_id.emplace(String(slot->id), original);
    }
    for (const Variant &key : overrides.keys()) {
        if (key.get_type() != Variant::STRING || String(key).length() > max_slot_id) {
            return ERR_FILE_CORRUPT;
        }
        const auto found = by_id.find(String(key));
        if (found == by_id.end()) {
            return ERR_DOES_NOT_EXIST;
        }
        Ref<InputEvent> event;
        if (!decode_event(overrides[key], found->second->get_event(), event)) {
            return ERR_FILE_CORRUPT;
        }
        found->second->set_event(event);
    }
    const Error changed = controls->replace_bindings(replacements);
    if (changed == OK) {
        emit_signal("changed", Ref<BindingSlot>());
    }
    return changed;
}

} // namespace crimson::rebinding
