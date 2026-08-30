#pragma once

#include <string>

#include "warmth/warmth_object.hpp"

namespace warmth {

enum class InvalidationReason : std::uint8_t {
    MODEL_REVISION_CHANGE = 0,
    ADAPTER_SET_CHANGE = 1,
    TOKENIZER_CHANGE = 2,
    KERNEL_GENERATION_CHANGE = 3,
    GRAPH_GENERATION_CHANGE = 4,
    RUNTIME_COMPILER_CHANGE = 5,
    COMPUTE_CAPABILITY_CHANGE = 6,
    CUDA_DRIVER_RUNTIME_CHANGE = 7,
    DEVICE_CHANGE = 8,
    POLICY_GENERATION_CHANGE = 9,
    REPLICA_GENERATION_CHANGE = 10,
    PROCESS_BOOT_CHANGE = 11,
    DEPENDENCY_CORRUPTION = 12,
    INTEGRITY_FAILURE = 13
};

constexpr const char* invalidation_reason_name(InvalidationReason r) noexcept {
    switch (r) {
        case InvalidationReason::MODEL_REVISION_CHANGE:     return "MODEL_REVISION_CHANGE";
        case InvalidationReason::ADAPTER_SET_CHANGE:        return "ADAPTER_SET_CHANGE";
        case InvalidationReason::TOKENIZER_CHANGE:          return "TOKENIZER_CHANGE";
        case InvalidationReason::KERNEL_GENERATION_CHANGE:  return "KERNEL_GENERATION_CHANGE";
        case InvalidationReason::GRAPH_GENERATION_CHANGE:   return "GRAPH_GENERATION_CHANGE";
        case InvalidationReason::RUNTIME_COMPILER_CHANGE:   return "RUNTIME_COMPILER_CHANGE";
        case InvalidationReason::COMPUTE_CAPABILITY_CHANGE: return "COMPUTE_CAPABILITY_CHANGE";
        case InvalidationReason::CUDA_DRIVER_RUNTIME_CHANGE:return "CUDA_DRIVER_RUNTIME_CHANGE";
        case InvalidationReason::DEVICE_CHANGE:             return "DEVICE_CHANGE";
        case InvalidationReason::POLICY_GENERATION_CHANGE:  return "POLICY_GENERATION_CHANGE";
        case InvalidationReason::REPLICA_GENERATION_CHANGE: return "REPLICA_GENERATION_CHANGE";
        case InvalidationReason::PROCESS_BOOT_CHANGE:       return "PROCESS_BOOT_CHANGE";
        case InvalidationReason::DEPENDENCY_CORRUPTION:     return "DEPENDENCY_CORRUPTION";
        case InvalidationReason::INTEGRITY_FAILURE:         return "INTEGRITY_FAILURE";
    }
    return "UNKNOWN";
}

inline std::string invalidation_reason_text(InvalidationReason r) { return std::string(invalidation_reason_name(r)); }

// Soft invalidation marks the object STALE (recoverable by revalidation).
// Hard invalidation marks it INVALIDATED (requires fresh full preparation).
enum class InvalidationSeverity : std::uint8_t { SOFT = 0, HARD = 1 };

struct InvalidateResult {
    bool changed = false;
    WarmthState state = WarmthState::COLD;
    InvalidationReason reason = InvalidationReason::MODEL_REVISION_CHANGE;
    InvalidationSeverity severity = InvalidationSeverity::HARD;
};

inline const char* invalidation_severity_name(InvalidationSeverity s) noexcept {
    return s == InvalidationSeverity::SOFT ? "SOFT" : "HARD";
}

// Apply an invalidation trigger. Any live/usable state becomes INVALIDATED.
inline InvalidateResult evaluate_invalidation(WarmthState state) {
    InvalidateResult out;
    if (state == WarmthState::INVALIDATED) { out.state = WarmthState::INVALIDATED; out.changed = false; return out; }
    out.changed = true;
    out.state = WarmthState::INVALIDATED;
    out.reason = InvalidationReason::INTEGRITY_FAILURE;
    return out;
}

} // namespace warmth
