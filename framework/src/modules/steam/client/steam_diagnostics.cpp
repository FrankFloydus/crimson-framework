// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/steam_diagnostics.hpp"

#include <algorithm>
#include <limits>

namespace crimson::steam::detail {

void SteamDiagnosticBuffer::start() {
    const std::lock_guard lock(mutex);
    clear_locked();
    epoch.fetch_add(1, std::memory_order_release);
}

void SteamDiagnosticBuffer::stop() {
    const std::lock_guard lock(mutex);
    epoch.fetch_add(1, std::memory_order_release);
    clear_locked();
}

void SteamDiagnosticBuffer::append(int severity) noexcept {
    const auto entered = epoch.load(std::memory_order_acquire);
    if ((entered & 1) == 0) {
        return;
    }

    const std::lock_guard lock(mutex);
    if (epoch.load(std::memory_order_relaxed) != entered) {
        return;
    }
    if (pending.count == pending.severities.size()) {
        if (pending.dropped < (std::numeric_limits<std::int64_t>::max)()) {
            ++pending.dropped;
        }
        return;
    }

    pending.severities[pending.count++] = severity;
}

SteamDiagnosticBuffer::Batch SteamDiagnosticBuffer::drain() {
    Batch batch;
    const std::lock_guard lock(mutex);
    batch.count = pending.count;
    batch.dropped = pending.dropped;
    std::copy_n(pending.severities.begin(), batch.count, batch.severities.begin());
    clear_locked();
    return batch;
}

void SteamDiagnosticBuffer::clear_locked() {
    pending.count = 0;
    pending.dropped = 0;
}

const char *warning_summary(int severity) {
    switch (severity) {
        case 0: return "Steam SDK information.";
        case 1: return "Steam SDK warning.";
        default: return "Steam SDK diagnostic.";
    }
}

} // namespace crimson::steam::detail
