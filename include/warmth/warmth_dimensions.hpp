#pragma once

#include <cstdint>
#include <algorithm>
#include <array>

namespace warmth {

// Per-dimension readiness. Dimensions are tracked independently and are never
// flattened into a single opaque number without preserving the underlying
// structure.
enum class DimensionStatus : std::uint8_t {
    COLD = 0,        // nothing prepared for this dimension
    DISCOVERED = 1,  // metadata/plan known, resource not prepared
    PARTIAL = 2,     // partial preparation (some bytes/kernels/flags present)
    READY = 3,       // prepared but not yet validated this generation
    VALID = 4,       // prepared and validated fresh in the current generation
    SUSPECT = 5      // present but unverified / possibly stale
};

constexpr std::uint8_t dimension_score(DimensionStatus s) noexcept {
    switch (s) {
        case DimensionStatus::COLD:       return 0;
        case DimensionStatus::DISCOVERED: return 1;
        case DimensionStatus::PARTIAL:    return 2;
        case DimensionStatus::READY:      return 3;
        case DimensionStatus::VALID:      return 4;
        case DimensionStatus::SUSPECT:    return 1;
    }
    return 0;
}

constexpr const char* dimension_name(DimensionStatus s) noexcept {
    switch (s) {
        case DimensionStatus::COLD:       return "COLD";
        case DimensionStatus::DISCOVERED: return "DISCOVERED";
        case DimensionStatus::PARTIAL:    return "PARTIAL";
        case DimensionStatus::READY:      return "READY";
        case DimensionStatus::VALID:      return "VALID";
        case DimensionStatus::SUSPECT:    return "SUSPECT";
    }
    return "UNKNOWN";
}

// Composite warmth levels (explainable aggregate of the components).
enum class CompositeLevel : std::uint8_t {
    COLD = 0,
    DISCOVERED = 1,
    PARTIAL = 2,
    READY = 3,
    HOT = 4
};

struct WarmthDimensions {
    DimensionStatus artifact_availability     = DimensionStatus::COLD;
    DimensionStatus artifact_validation       = DimensionStatus::COLD;
    DimensionStatus model_residency           = DimensionStatus::COLD;
    DimensionStatus adapter_residency         = DimensionStatus::COLD;
    DimensionStatus tokenizer_readiness       = DimensionStatus::COLD;
    DimensionStatus cuda_context_readiness    = DimensionStatus::COLD;
    DimensionStatus kernel_readiness          = DimensionStatus::COLD;
    DimensionStatus graph_readiness           = DimensionStatus::COLD;
    DimensionStatus allocator_readiness       = DimensionStatus::COLD;
    DimensionStatus prefix_kv_reuse           = DimensionStatus::COLD;
    DimensionStatus engine_readiness          = DimensionStatus::COLD;
    DimensionStatus local_dependency_readiness = DimensionStatus::COLD;

    // Returns the count of dimensions currently in a usable (READY or VALID)
    // state and the count that are VALID.
    [[nodiscard]] std::size_t ready_count() const noexcept {
        std::size_t n = 0;
        for (const auto d : all()) if (dimension_score(d) >= 3) ++n;
        return n;
    }
    [[nodiscard]] std::size_t valid_count() const noexcept {
        std::size_t n = 0;
        for (const auto d : all()) if (d == DimensionStatus::VALID) ++n;
        return n;
    }
    [[nodiscard]] std::size_t component_count() const noexcept { return 12; }

    [[nodiscard]] std::array<DimensionStatus, 12> all() const noexcept {
        return {
            artifact_availability, artifact_validation, model_residency, adapter_residency,
            tokenizer_readiness, cuda_context_readiness, kernel_readiness, graph_readiness,
            allocator_readiness, prefix_kv_reuse, engine_readiness, local_dependency_readiness
        };
    }

    // Sum of per-dimension scores (0 .. 48). Always-valid dimensions that are
    // optional for a category can be excluded by callers.
    [[nodiscard]] std::uint32_t raw_score() const noexcept {
        std::uint32_t s = 0;
        for (const auto d : all()) s += dimension_score(d);
        return s;
    }

    // Composite level derived deterministically from component scores.
    [[nodiscard]] CompositeLevel composite_level() const noexcept {
        const std::size_t total = component_count();
        const std::size_t ready = ready_count();
        const std::size_t valid = valid_count();
        if (valid == total) return CompositeLevel::HOT;
        if (ready == total)  return CompositeLevel::READY;
        if (ready == 0) {
            const auto score = raw_score();
            if (score >= 12) return CompositeLevel::PARTIAL;
            if (score > 0)   return CompositeLevel::DISCOVERED;
            return CompositeLevel::COLD;
        }
        // Some ready, some not: partial if at least half are ready.
        if (ready * 2 >= total) return CompositeLevel::PARTIAL;
        return CompositeLevel::DISCOVERED;
    }

    [[nodiscard]] bool all_ready() const noexcept { return ready_count() == component_count(); }
    [[nodiscard]] bool any_suspect() const noexcept {
        for (const auto d : all()) if (d == DimensionStatus::SUSPECT) return true;
        return false;
    }
};

constexpr const char* composite_name(CompositeLevel l) noexcept {
    switch (l) {
        case CompositeLevel::COLD:       return "COLD";
        case CompositeLevel::DISCOVERED: return "DISCOVERED";
        case CompositeLevel::PARTIAL:    return "PARTIAL";
        case CompositeLevel::READY:      return "READY";
        case CompositeLevel::HOT:        return "HOT";
    }
    return "UNKNOWN";
}

} // namespace warmth
