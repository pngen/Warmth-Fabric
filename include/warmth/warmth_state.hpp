#pragma once

#include <cstdint>
#include <string_view>

namespace warmth {

// Arithmetic ordering of warmth states, used to compare "how warm". The
// enumerated order reflects increasing operational readiness for states that
// lie on the warm axis; non-warm states (STALE, INVALIDATED, EVICTED, FAILED)
// sit at the cold end regardless of where they appear in the enum.
enum class WarmthState : std::uint8_t {
    COLD = 0,
    DISCOVERED = 1,
    PREPARING = 2,
    PARTIALLY_WARM = 3,
    WARM = 4,
    HOT = 5,
    STALE = 6,
    INVALIDATED = 7,
    EVICTED = 8,
    FAILED = 9
};

constexpr bool is_execution_ready_state(int state) noexcept {
    // Execution-ready means the object can be bound to a served request without
    // further preparation: WARM or HOT. (PARTIALLY_WARM needs more work.)
    return state == static_cast<int>(WarmthState::WARM) || state == static_cast<int>(WarmthState::HOT);
}

constexpr bool is_execution_ready(WarmthState s) noexcept {
    return is_execution_ready_state(static_cast<int>(s));
}

// Return a canonical, stable string for a warmth state.
constexpr std::string_view to_string(WarmthState s) noexcept {
    switch (s) {
        case WarmthState::COLD:            return "COLD";
        case WarmthState::DISCOVERED:      return "DISCOVERED";
        case WarmthState::PREPARING:       return "PREPARING";
        case WarmthState::PARTIALLY_WARM:  return "PARTIALLY_WARM";
        case WarmthState::WARM:            return "WARM";
        case WarmthState::HOT:             return "HOT";
        case WarmthState::STALE:           return "STALE";
        case WarmthState::INVALIDATED:     return "INVALIDATED";
        case WarmthState::EVICTED:         return "EVICTED";
        case WarmthState::FAILED:          return "FAILED";
    }
    return "UNKNOWN";
}

// A monotonic "warmth level" for objects that follow the warm axis.
// Execution-ready threshold is at READY. Used for planning and decay.
enum class WarmthLevel : std::uint8_t {
    NONE = 0,      // COLD
    DISCOVERED = 1,
    PREPARING = 2,
    PARTIAL = 3,
    READY = 4,     // WARM
    HOT = 5        // HOT (frequently used, recently used, high confidence)
};

constexpr WarmthLevel to_level(WarmthState s) noexcept {
    switch (s) {
        case WarmthState::COLD:            return WarmthLevel::NONE;
        case WarmthState::DISCOVERED:      return WarmthLevel::DISCOVERED;
        case WarmthState::PREPARING:       return WarmthLevel::PREPARING;
        case WarmthState::PARTIALLY_WARM:  return WarmthLevel::PARTIAL;
        case WarmthState::WARM:            return WarmthLevel::READY;
        case WarmthState::HOT:             return WarmthLevel::HOT;
        case WarmthState::STALE:
        case WarmthState::INVALIDATED:
        case WarmthState::EVICTED:
        case WarmthState::FAILED:          return WarmthLevel::NONE;
    }
    return WarmthLevel::NONE;
}

constexpr bool is_live_warm_state(WarmthState s) noexcept {
    return s == WarmthState::PARTIALLY_WARM || s == WarmthState::WARM || s == WarmthState::HOT;
}

// ---------------------------------------------------------------------------
// Transition table. Explicit and deterministic: an invalid transition is
// rejected rather than silently accepted. A state may only be entered through
// a listed edge.
// ---------------------------------------------------------------------------
constexpr bool transition_allowed(WarmthState from, WarmthState to) noexcept {
    switch (from) {
        case WarmthState::COLD:
            return to == WarmthState::COLD || to == WarmthState::DISCOVERED || to == WarmthState::PREPARING ||
                   to == WarmthState::EVICTED || to == WarmthState::INVALIDATED || to == WarmthState::FAILED;
        case WarmthState::DISCOVERED:
            return to == WarmthState::COLD || to == WarmthState::DISCOVERED || to == WarmthState::PREPARING ||
                   to == WarmthState::PARTIALLY_WARM || to == WarmthState::WARM ||
                   to == WarmthState::INVALIDATED || to == WarmthState::EVICTED || to == WarmthState::FAILED;
        case WarmthState::PREPARING:
            return to == WarmthState::DISCOVERED || to == WarmthState::PREPARING || to == WarmthState::PARTIALLY_WARM ||
                   to == WarmthState::WARM || to == WarmthState::HOT || to == WarmthState::STALE ||
                   to == WarmthState::INVALIDATED || to == WarmthState::EVICTED || to == WarmthState::FAILED;
        case WarmthState::PARTIALLY_WARM:
            return to == WarmthState::PREPARING || to == WarmthState::PARTIALLY_WARM || to == WarmthState::WARM ||
                   to == WarmthState::STALE || to == WarmthState::INVALIDATED || to == WarmthState::EVICTED ||
                   to == WarmthState::FAILED || to == WarmthState::COLD;
        case WarmthState::WARM:
            return to == WarmthState::HOT || to == WarmthState::WARM || to == WarmthState::PARTIALLY_WARM ||
                   to == WarmthState::STALE || to == WarmthState::INVALIDATED || to == WarmthState::EVICTED ||
                   to == WarmthState::FAILED || to == WarmthState::DISCOVERED;
        case WarmthState::HOT:
            return to == WarmthState::WARM || to == WarmthState::HOT || to == WarmthState::STALE ||
                   to == WarmthState::INVALIDATED || to == WarmthState::EVICTED || to == WarmthState::FAILED;
        case WarmthState::STALE:
            return to == WarmthState::WARM || to == WarmthState::STALE || to == WarmthState::INVALIDATED ||
                   to == WarmthState::COLD || to == WarmthState::EVICTED || to == WarmthState::FAILED ||
                   to == WarmthState::DISCOVERED;
        case WarmthState::INVALIDATED:
            return to == WarmthState::COLD || to == WarmthState::DISCOVERED || to == WarmthState::PREPARING ||
                   to == WarmthState::INVALIDATED || to == WarmthState::FAILED || to == WarmthState::EVICTED;
        case WarmthState::EVICTED:
            return to == WarmthState::COLD || to == WarmthState::DISCOVERED || to == WarmthState::PREPARING ||
                   to == WarmthState::EVICTED;
        case WarmthState::FAILED:
            return to == WarmthState::COLD || to == WarmthState::DISCOVERED || to == WarmthState::INVALIDATED ||
                   to == WarmthState::FAILED;
    }
    return false;
}

} // namespace warmth
