#pragma once

#include <optional>

#include "warmth/warmth_object.hpp"

namespace warmth {

enum class DecayReason : std::uint8_t {
    IDLE_ELAPSED = 0,
    MEMORY_PRESSURE = 1,
    RESOURCE_RECLAMATION = 2,
    PROCESS_RESTART = 3,
    DEVICE_RESET = 4,
    DEPENDENCY_GENERATION_CHANGE = 5,
    ARTIFACT_REPLACEMENT = 6,
    ADAPTER_CHANGE = 7,
    RUNTIME_UPGRADE = 8,
    KERNEL_INVALIDATION = 9,
    GRAPH_INVALIDATION = 10,
    TOPOLOGY_CHANGE = 11,
    POLICY_GENERATION_CHANGE = 12
};

constexpr const char* decay_reason_name(DecayReason r) noexcept {
    switch (r) {
        case DecayReason::IDLE_ELAPSED:               return "IDLE_ELAPSED";
        case DecayReason::MEMORY_PRESSURE:            return "MEMORY_PRESSURE";
        case DecayReason::RESOURCE_RECLAMATION:       return "RESOURCE_RECLAMATION";
        case DecayReason::PROCESS_RESTART:            return "PROCESS_RESTART";
        case DecayReason::DEVICE_RESET:               return "DEVICE_RESET";
        case DecayReason::DEPENDENCY_GENERATION_CHANGE: return "DEPENDENCY_GENERATION_CHANGE";
        case DecayReason::ARTIFACT_REPLACEMENT:       return "ARTIFACT_REPLACEMENT";
        case DecayReason::ADAPTER_CHANGE:             return "ADAPTER_CHANGE";
        case DecayReason::RUNTIME_UPGRADE:            return "RUNTIME_UPGRADE";
        case DecayReason::KERNEL_INVALIDATION:        return "KERNEL_INVALIDATION";
        case DecayReason::GRAPH_INVALIDATION:         return "GRAPH_INVALIDATION";
        case DecayReason::TOPOLOGY_CHANGE:            return "TOPOLOGY_CHANGE";
        case DecayReason::POLICY_GENERATION_CHANGE:   return "POLICY_GENERATION_CHANGE";
    }
    return "UNKNOWN";
}

struct DecayPolicy {
    double hot_to_warm_idle_seconds         = 300.0;
    double warm_to_stale_idle_seconds       = 1200.0;
    double stale_to_invalidated_seconds     = 600.0;
    double warm_to_partial_idle_seconds     = 1800.0;
    double partial_to_cold_idle_seconds     = 3600.0;

    bool decay_on_memory_pressure           = true;
    bool decay_on_process_restart           = true;
    bool decay_on_device_reset              = true;
    bool decay_on_dependency_change         = true;
    bool decay_on_policy_change             = true;
};

struct DecayResult {
    bool changed = false;
    WarmthState state = WarmthState::COLD;
    DecayReason reason = DecayReason::IDLE_ELAPSED;
};

// Compute the deterministic next state given the object, policy, current time
// and external flags. Does not mutate the object. Identical inputs always
// produce identical outputs.
inline DecayResult decay_object(
        const WarmthObject& obj,
        const DecayPolicy& policy,
        Timestamp now,
        bool memory_pressure = false,
        bool dependency_changed = false,
        bool device_reset = false,
        bool process_restart = false) {

    DecayResult out;
    if (obj.state() == WarmthState::COLD || obj.state() == WarmthState::DISCOVERED ||
        obj.state() == WarmthState::EVICTED || obj.state() == WarmthState::FAILED ||
        obj.state() == WarmthState::INVALIDATED) {
        out.state = obj.state();
        return out;
    }

    if (dependency_changed) {
        if (obj.state() != WarmthState::STALE) {
            out.changed = true; out.state = WarmthState::STALE; out.reason = DecayReason::DEPENDENCY_GENERATION_CHANGE;
            return out;
        }
        out.state = WarmthState::STALE; return out;
    }
    if (device_reset && policy.decay_on_device_reset) {
        if (obj.state() != WarmthState::STALE) {
            out.changed = true; out.state = WarmthState::STALE; out.reason = DecayReason::DEVICE_RESET;
            return out;
        }
        out.state = WarmthState::STALE; return out;
    }
    if (process_restart && policy.decay_on_process_restart) {
        if (obj.state() != WarmthState::STALE) {
            out.changed = true; out.state = WarmthState::STALE; out.reason = DecayReason::PROCESS_RESTART;
            return out;
        }
        out.state = WarmthState::STALE; return out;
    }

    const Timestamp baseline = obj.last_used() != 0 ? obj.last_used() : obj.last_prepared();
    const double idle = elapsed_seconds(baseline, now);

    if (memory_pressure && policy.decay_on_memory_pressure) {
        if (obj.state() == WarmthState::HOT) {
            out.changed = true; out.state = WarmthState::WARM; out.reason = DecayReason::MEMORY_PRESSURE; return out;
        }
        if (obj.state() == WarmthState::WARM) {
            out.changed = true; out.state = WarmthState::PARTIALLY_WARM; out.reason = DecayReason::MEMORY_PRESSURE; return out;
        }
        if (obj.state() == WarmthState::PARTIALLY_WARM) {
            out.changed = true; out.state = WarmthState::COLD; out.reason = DecayReason::MEMORY_PRESSURE; return out;
        }
    }

    switch (obj.state()) {
        case WarmthState::HOT:
            if (policy.hot_to_warm_idle_seconds > 0.0 && idle >= policy.hot_to_warm_idle_seconds) {
                out.changed = true; out.state = WarmthState::WARM; out.reason = DecayReason::IDLE_ELAPSED; return out;
            }
            break;
        case WarmthState::WARM:
            if (policy.warm_to_stale_idle_seconds > 0.0 && idle >= policy.warm_to_stale_idle_seconds) {
                out.changed = true; out.state = WarmthState::STALE; out.reason = DecayReason::IDLE_ELAPSED; return out;
            }
            if (policy.warm_to_partial_idle_seconds > 0.0 && policy.warm_to_partial_idle_seconds < policy.warm_to_stale_idle_seconds &&
                idle >= policy.warm_to_partial_idle_seconds) {
                out.changed = true; out.state = WarmthState::PARTIALLY_WARM; out.reason = DecayReason::IDLE_ELAPSED; return out;
            }
            break;
        case WarmthState::PARTIALLY_WARM:
            if (policy.partial_to_cold_idle_seconds > 0.0 && idle >= policy.partial_to_cold_idle_seconds) {
                out.changed = true; out.state = WarmthState::COLD; out.reason = DecayReason::IDLE_ELAPSED; return out;
            }
            break;
        case WarmthState::STALE:
            if (policy.stale_to_invalidated_seconds > 0.0 && idle >= policy.stale_to_invalidated_seconds) {
                out.changed = true; out.state = WarmthState::INVALIDATED; out.reason = DecayReason::IDLE_ELAPSED; return out;
            }
            break;
        default: break;
    }

    out.state = obj.state();
    return out;
}

} // namespace warmth
