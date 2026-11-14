// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/network/wire_codec.hpp"

namespace crimson::integration::motion_wire {

// One input sample in tick order. The upload carries the newest tick separately.
struct InputRow {
    float move_x; ///< Horizontal input.
    float move_y; ///< Forward and backward input.
    float yaw; ///< World aim yaw in radians.
    float pitch; ///< World aim pitch in radians.
    bool jump_pressed; ///< One jump attempt at this command's tick.
    bool has_lock_heading; ///< Whether the lock-on controller supplies a facing direction.
    float lock_heading; ///< World lock yaw in radians, or canonical zero when absent.
};

// One complete authority sample. A roster entry also writes the assigned slot before this row.
struct PoseRow {
    int32_t peer; ///< Network peer assigned to this actor.
    float x; ///< World position in metres.
    float y; ///< World position in metres.
    float z; ///< World position in metres.
    float yaw; ///< World aim yaw in radians.
    float pitch; ///< World aim pitch in radians.
    float body_yaw; ///< Authority-resolved character yaw in radians.
};

// Wire sizes are explicit and independent of the compiler's struct layout.
inline constexpr size_t input_row_bytes = 22;
inline constexpr size_t pose_row_bytes = 28;
inline constexpr size_t roster_row_bytes = 4 + pose_row_bytes;

// Write input axes, view aim, jump flag, and optional lock yaw.
inline bool write_input(replication::WireWriter &writer, const InputRow &row) {
    return writer.write_f32(row.move_x) && writer.write_f32(row.move_y) &&
            writer.write_f32(row.yaw) && writer.write_f32(row.pitch) && writer.write_bool(row.jump_pressed) &&
            writer.write_bool(row.has_lock_heading) && writer.write_f32(row.lock_heading);
}

// Read a full input row without replacing the destination on failure.
inline bool read_input(replication::WireReader &reader, InputRow &row) {
    InputRow decoded;
    if (!reader.read_f32(decoded.move_x) || !reader.read_f32(decoded.move_y) ||
            !reader.read_f32(decoded.yaw) || !reader.read_f32(decoded.pitch) ||
            !reader.read_bool(decoded.jump_pressed) || !reader.read_bool(decoded.has_lock_heading) ||
            !reader.read_f32(decoded.lock_heading)) {
        return false;
    }
    row = decoded;
    return true;
}

// Write a complete authority pose as one peer ID and six finite float32 values.
inline bool write_pose(replication::WireWriter &writer, const PoseRow &row) {
    return writer.write_i32(row.peer) && writer.write_f32(row.x) && writer.write_f32(row.y) &&
            writer.write_f32(row.z) && writer.write_f32(row.yaw) && writer.write_f32(row.pitch) &&
            writer.write_f32(row.body_yaw);
}

// Read a full pose row without replacing the destination on failure.
inline bool read_pose(replication::WireReader &reader, PoseRow &row) {
    PoseRow decoded;
    if (!reader.read_i32(decoded.peer) || !reader.read_f32(decoded.x) || !reader.read_f32(decoded.y) ||
            !reader.read_f32(decoded.z) || !reader.read_f32(decoded.yaw) || !reader.read_f32(decoded.pitch) ||
            !reader.read_f32(decoded.body_yaw)) {
        return false;
    }
    row = decoded;
    return true;
}

} // namespace crimson::integration::motion_wire
