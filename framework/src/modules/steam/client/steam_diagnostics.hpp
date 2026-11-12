// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace crimson::steam::detail {

// Captures one bounded stream of Steam warning callbacks in native memory.
class SteamDiagnosticBuffer {
public:
    // Maximum number of warnings retained per drain.
    static constexpr std::size_t message_capacity = 32;
    // A detached FIFO batch and its overflow count since the previous drain.
    struct Batch {
        // Steam severity numbers for the first count callbacks.
        std::array<int, message_capacity> severities{};
        // Number of populated entries in severities.
        std::size_t count = 0;
        // New callbacks dropped while the queue was full.
        std::int64_t dropped = 0;
    };

    // Open a fresh capture epoch before installing the SDK hook.
    void start();
    // Close the epoch and discard its pending batch before uninstalling the hook.
    void stop();
    // Store an SDK severity on its calling thread without allocating message storage.
    void append(int severity) noexcept;
    // Detach the current FIFO batch and reopen its buffer capacity.
    Batch drain();

private:
    // Serializes the fixed buffer and its counters across callback and drain threads.
    std::mutex mutex;
    // Odd while capture is active, even while inactive. A callback must match its entry epoch.
    std::atomic<std::uint64_t> epoch{0};
    // Incoming messages and overflow count for the current capture.
    Batch pending;

    // Reset logical contents while holding the queue mutex.
    void clear_locked();
};

// Describe a Steam severity with fixed text that contains no SDK payload.
const char *warning_summary(int severity);

} // namespace crimson::steam::detail
