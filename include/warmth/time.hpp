#pragma once

#include <cstdint>
#include <chrono>

namespace warmth {

// Wall-clock milliseconds since the Unix epoch. Persisted and comparable across
// restarts. This is NOT a monotonic clock and must only be used for
// cross-restart/persisted timestamps.
using Timestamp = std::int64_t;

inline Timestamp now_ms() noexcept {
    return static_cast<Timestamp>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

// Monotonic milliseconds. Used only for in-process ordering and decay
// measurement; never persisted.
using MonotonicMs = std::int64_t;

inline MonotonicMs monotonic_ms() noexcept {
    return static_cast<MonotonicMs>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

// Seconds between two wall-clock timestamps (clamped at zero when reversed).
inline double elapsed_seconds(Timestamp start, Timestamp end) noexcept {
    const auto d = end - start;
    return d <= 0 ? 0.0 : static_cast<double>(d) / 1000.0;
}

} // namespace warmth
