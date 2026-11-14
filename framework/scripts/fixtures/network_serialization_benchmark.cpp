// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "integration/world_session/movement_wire.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <new>
#include <vector>

namespace {

// Allocation tracking is enabled only during the single-packet correctness pass.
thread_local bool count_allocations = false;
// Calls to the fixture's replacement C++ allocation operators.
thread_local size_t allocations = 0;

// Count regular scalar and array C++ allocations on the benchmark thread.
void *allocate(size_t size) {
    if (count_allocations) {
        ++allocations;
    }
    if (void *pointer = std::malloc(size ? size : 1)) {
        return pointer;
    }
    throw std::bad_alloc();
}

} // namespace

// Observe standard C++ allocation calls in the native codec.
void *operator new(size_t size) { return allocate(size); }
// Observe standard C++ array allocation calls in the native codec.
void *operator new[](size_t size) { return allocate(size); }
// Release memory obtained by the fixture's replacement allocators.
void operator delete(void *pointer) noexcept { std::free(pointer); }
// Release array memory obtained by the fixture's replacement allocators.
void operator delete[](void *pointer) noexcept { std::free(pointer); }
// Support sized scalar deallocation on MSVC.
void operator delete(void *pointer, size_t) noexcept { std::free(pointer); }
// Support sized array deallocation on MSVC.
void operator delete[](void *pointer, size_t) noexcept { std::free(pointer); }

namespace {

using crimson::integration::motion_wire::InputRow;
using crimson::integration::motion_wire::PoseRow;
using crimson::integration::motion_wire::read_input;
using crimson::integration::motion_wire::read_pose;
using crimson::integration::motion_wire::write_input;
using crimson::integration::motion_wire::write_pose;
using crimson::replication::WireReader;
using crimson::replication::WireWriter;

constexpr size_t rows_per_packet = 24;
constexpr size_t packet_variants = 64;
constexpr size_t samples = 20;
constexpr size_t warmups = 2;
constexpr size_t iterations = 40000;

// Hold one exact-length caller-owned payload and its corresponding rows.
template <typename Row, size_t RowBytes>
struct Fixture {
    std::array<Row, rows_per_packet> rows; ///< Values written by the codec.
    std::array<uint8_t, rows_per_packet * RowBytes> bytes; ///< Packet bytes for the decode workload.
};

// Encode every row into the exact-size buffer used by the motion protocol.
template <typename Row, size_t RowBytes, bool (*Write)(WireWriter &, const Row &)>
__declspec(noinline) bool encode(const std::array<Row, rows_per_packet> &rows,
        std::array<uint8_t, rows_per_packet * RowBytes> &bytes) {
    WireWriter writer(bytes.data(), bytes.size());
    for (const Row &row : rows) {
        if (!Write(writer, row)) {
            return false;
        }
    }
    return writer.ok() && writer.size() == bytes.size();
}

// Decode the complete packet into caller-owned rows.
template <typename Row, size_t RowBytes, bool (*Read)(WireReader &, Row &)>
__declspec(noinline) bool decode(const std::array<uint8_t, rows_per_packet * RowBytes> &bytes,
        std::array<Row, rows_per_packet> &rows) {
    WireReader reader(bytes.data(), bytes.size());
    for (Row &row : rows) {
        if (!Read(reader, row)) {
            return false;
        }
    }
    return reader.finished();
}

// Compare every decoded input field.
bool same_input(const InputRow &left, const InputRow &right) {
    return left.move_x == right.move_x && left.move_y == right.move_y &&
            left.yaw == right.yaw && left.pitch == right.pitch && left.jump_pressed == right.jump_pressed &&
            left.has_lock_heading == right.has_lock_heading && left.lock_heading == right.lock_heading;
}

// Compare every decoded pose field.
bool same_pose(const PoseRow &left, const PoseRow &right) {
    return left.peer == right.peer && left.x == right.x && left.y == right.y &&
            left.z == right.z && left.yaw == right.yaw && left.pitch == right.pitch && left.body_yaw == right.body_yaw;
}

// Form the reference field bytes for the benchmark's Windows x64 little-endian host.
template <typename Value>
void reference_field(uint8_t *&output, Value value) {
    std::memcpy(output, &value, sizeof(value));
    output += sizeof(value);
}

// Verify the order and width expected by the equivalent engine argument fixture.
void reference_input(uint8_t *output, const InputRow &row) {
    reference_field(output, row.move_x);
    reference_field(output, row.move_y);
    reference_field(output, row.yaw);
    reference_field(output, row.pitch);
    reference_field(output, static_cast<uint8_t>(row.jump_pressed));
    reference_field(output, static_cast<uint8_t>(row.has_lock_heading));
    reference_field(output, row.lock_heading);
}

// Verify that the actor identifier precedes six pose floats, with body yaw last.
void reference_pose(uint8_t *output, const PoseRow &row) {
    reference_field(output, row.peer);
    reference_field(output, row.x);
    reference_field(output, row.y);
    reference_field(output, row.z);
    reference_field(output, row.yaw);
    reference_field(output, row.pitch);
    reference_field(output, row.body_yaw);
}

// Read a decode output so the optimizer must retain the full packet call.
uint64_t consume_input(const InputRow &row) { return static_cast<uint64_t>(row.move_x * 64.0f); }

// Read a decode output so the optimizer must retain the full packet call.
uint64_t consume_pose(const PoseRow &row) { return static_cast<uint64_t>(row.peer); }

// Generate representative finite input values with different first rows in each packet.
InputRow input_sample(size_t packet, size_t row) {
    const float x = static_cast<float>(packet + row) / 128.0f;
    const bool locked = packet % 2 == 0;
    return { x, 0.25f - x, 0.25f + x, -0.125f * x, (packet + row) % rows_per_packet == 0, locked,
        locked ? 0.25f - x : 0.0f };
}

// Generate finite positions, angles, and IDs that vary across packets.
PoseRow pose_sample(size_t packet, size_t row) {
    const float x = static_cast<float>(packet + row) / 16.0f;
    return { static_cast<int32_t>(packet * rows_per_packet + row + 2), x, -x, 3.25f + x, 0.5f - x, -0.125f * x,
        0.125f * x - 0.5f };
}

// Obtain the CPU cycles the OS attributed to this benchmark thread.
uint64_t thread_cycles() {
    ULONG64 value = 0;
    if (!QueryThreadCycleTime(GetCurrentThread(), &value)) {
        std::cerr << "QueryThreadCycleTime failed.\n";
        std::exit(EXIT_FAILURE);
    }
    return value;
}

// Publish warmed steady-clock and thread-cycle statistics per complete packet.
void report(const char *workload, const char *operation, size_t bytes, size_t cpp_allocations,
        std::array<double, samples> elapsed, std::array<double, samples> cycles, uint64_t checksum) {
    std::sort(elapsed.begin(), elapsed.end());
    std::sort(cycles.begin(), cycles.end());
    std::cout << "CRIMSON_NATIVE_CODEC_BENCHMARK {\"workload\":\"" << workload
              << "\",\"operation\":\"" << operation << "\",\"payload_bytes\":" << bytes
              << ",\"rows\":" << rows_per_packet << ",\"cpp_allocations_per_packet\":" << cpp_allocations
              << ",\"median_ns\":" << (elapsed[samples / 2 - 1] + elapsed[samples / 2]) / 2.0
              << ",\"p95_ns\":" << elapsed[18]
              << ",\"median_thread_cycles\":" << (cycles[samples / 2 - 1] + cycles[samples / 2]) / 2.0
              << ",\"p95_thread_cycles\":" << cycles[18]
              << ",\"iterations_per_sample\":" << iterations << ",\"sample_count\":" << samples
              << ",\"checksum\":" << checksum << "}\n";
}

// Verify round trips and allocation behavior, then time encode and decode independently.
template <typename Row, size_t RowBytes, bool (*Write)(WireWriter &, const Row &), bool (*Read)(WireReader &, Row &)>
bool benchmark(const char *name, Row (*generate)(size_t, size_t), bool (*same)(const Row &, const Row &),
        uint64_t (*consume)(const Row &), void (*reference)(uint8_t *, const Row &)) {
    std::array<Fixture<Row, RowBytes>, packet_variants> fixtures;
    std::array<Row, rows_per_packet> decoded;
    std::array<uint8_t, rows_per_packet * RowBytes> encoded;
    size_t encode_allocations = 0;
    size_t decode_allocations = 0;
    for (size_t packet = 0; packet < fixtures.size(); ++packet) {
        auto &fixture = fixtures[packet];
        for (size_t row = 0; row < fixture.rows.size(); ++row) {
            fixture.rows[row] = generate(packet, row);
            reference(encoded.data() + row * RowBytes, fixture.rows[row]);
        }
        allocations = 0;
        count_allocations = true;
        const bool wrote = encode<Row, RowBytes, Write>(fixture.rows, fixture.bytes);
        encode_allocations = std::max(encode_allocations, allocations);
        allocations = 0;
        const bool read = decode<Row, RowBytes, Read>(fixture.bytes, decoded);
        decode_allocations = std::max(decode_allocations, allocations);
        count_allocations = false;
        if (!wrote || !read || fixture.bytes != encoded ||
                !std::equal(fixture.rows.begin(), fixture.rows.end(), decoded.begin(), same)) {
            std::cerr << "Native codec round trip failed for " << name << ".\n";
            return false;
        }
    }
    for (const bool read : { false, true }) {
        std::array<double, samples> elapsed;
        std::array<double, samples> cycles;
        uint64_t checksum = 0;
        for (size_t sample = 0; sample < warmups + samples; ++sample) {
            const auto start = std::chrono::steady_clock::now();
            const uint64_t cpu_start = thread_cycles();
            for (size_t iteration = 0; iteration < iterations; ++iteration) {
                const auto &fixture = fixtures[iteration % fixtures.size()];
                if (read) {
                    if (!decode<Row, RowBytes, Read>(fixture.bytes, decoded)) {
                        return false;
                    }
                    checksum += consume(decoded[iteration % rows_per_packet]);
                } else {
                    if (!encode<Row, RowBytes, Write>(fixture.rows, encoded)) {
                        return false;
                    }
                    checksum += encoded[iteration % encoded.size()];
                }
            }
            const uint64_t cpu_end = thread_cycles();
            const auto end = std::chrono::steady_clock::now();
            if (sample >= warmups) {
                elapsed[sample - warmups] = std::chrono::duration<double, std::nano>(end - start).count() / iterations;
                cycles[sample - warmups] = static_cast<double>(cpu_end - cpu_start) / iterations;
            }
        }
        report(name, read ? "decode" : "encode", encoded.size(),
                read ? decode_allocations : encode_allocations, elapsed, cycles, checksum);
    }
    return true;
}

} // namespace

// Benchmark both production movement row codecs in one optimized native executable.
int main() {
    std::cout << std::fixed << std::setprecision(2);
    using crimson::integration::motion_wire::input_row_bytes;
    using crimson::integration::motion_wire::pose_row_bytes;
    if (!benchmark<InputRow, input_row_bytes, write_input, read_input>(
                "movement_input", input_sample, same_input, consume_input, reference_input) ||
            !benchmark<PoseRow, pose_row_bytes, write_pose, read_pose>(
                    "movement_pose", pose_sample, same_pose, consume_pose, reference_pose)) {
        return EXIT_FAILURE;
    }
    std::cout << "CRIMSON_NATIVE_CODEC_BENCHMARK_PASS\n";
    return EXIT_SUCCESS;
}
