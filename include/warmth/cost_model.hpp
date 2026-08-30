#pragma once

#include <cstdint>

#include "warmth/warmth_object.hpp"
#include "warmth/detail/macros.hpp"

namespace warmth {

enum class CostKind : std::uint8_t {
    UNKNOWN = 0,
    ESTIMATED = 1,
    MEASURED = 2,
    REPORTED = 3,
    DERIVED = 4
};

constexpr const char* cost_kind_name(CostKind k) noexcept {
    switch (k) {
        case CostKind::UNKNOWN:   return "UNKNOWN";
        case CostKind::ESTIMATED: return "ESTIMATED";
        case CostKind::MEASURED:  return "MEASURED";
        case CostKind::REPORTED:  return "REPORTED";
        case CostKind::DERIVED:   return "DERIVED";
    }
    return "UNKNOWN";
}

struct CostBreakdown {
    std::uint64_t bytes_to_transfer = 0;
    std::uint64_t bytes_resident = 0;
    std::uint64_t artifacts_to_validate = 0;
    std::uint64_t kernels_to_prepare = 0;
    std::uint64_t graphs_to_prepare = 0;
    std::uint64_t context_init_steps = 0;
    std::uint64_t adapter_activation_bytes = 0;
    std::uint64_t tokenizer_init_steps = 0;
    std::uint64_t prefix_kv_bytes = 0;
    std::uint64_t allocator_init_steps = 0;
    std::uint64_t engine_startup_ms = 0;
    std::uint64_t sync_cost_ms = 0;
    std::uint64_t dependency_checks = 0;

    [[nodiscard]] std::uint64_t total_bytes() const noexcept {
        return bytes_to_transfer + bytes_resident + adapter_activation_bytes + prefix_kv_bytes;
    }
    [[nodiscard]] bool is_zero() const noexcept {
        return bytes_to_transfer == 0 && bytes_resident == 0 && artifacts_to_validate == 0 &&
               kernels_to_prepare == 0 && graphs_to_prepare == 0 && context_init_steps == 0 &&
               adapter_activation_bytes == 0 && tokenizer_init_steps == 0 && prefix_kv_bytes == 0 &&
               allocator_init_steps == 0 && engine_startup_ms == 0 && sync_cost_ms == 0 &&
               dependency_checks == 0;
    }
};

struct ReadinessCost {
    CostKind kind = CostKind::ESTIMATED;
    CostBreakdown estimated;
    CostBreakdown measured;
    double expected_ttfu_ms = 0.0;
    double observed_ttfu_ms = 0.0;

    [[nodiscard]] bool has_measurement() const noexcept { return !measured.is_zero() || observed_ttfu_ms > 0.0; }
};

namespace cost_model {

inline ReadinessCost estimate(const WarmthObject& obj, std::uint64_t model_bytes = 0) {
    ReadinessCost cost;
    cost.kind = CostKind::ESTIMATED;
    const auto& d = obj.dimensions();
    const WarmthState st = obj.state();
    const bool cold = st == WarmthState::COLD || st == WarmthState::DISCOVERED ||
                      st == WarmthState::EVICTED || st == WarmthState::INVALIDATED;
    const bool partial = st == WarmthState::PARTIALLY_WARM;
    const bool warm = st == WarmthState::WARM || st == WarmthState::HOT;

    if (d.artifact_availability == DimensionStatus::COLD || d.artifact_availability == DimensionStatus::DISCOVERED) {
        cost.estimated.bytes_to_transfer = model_bytes == 0 ? (1ULL << 30) : model_bytes;
    }
    if (d.model_residency == DimensionStatus::COLD) {
        cost.estimated.bytes_to_transfer += model_bytes == 0 ? (1ULL << 30) : model_bytes;
    }

    if (d.artifact_validation == DimensionStatus::COLD ||
        d.artifact_validation == DimensionStatus::DISCOVERED ||
        d.artifact_validation == DimensionStatus::PARTIAL) cost.estimated.artifacts_to_validate = 1;
    if (d.kernel_readiness == DimensionStatus::COLD || d.kernel_readiness == DimensionStatus::DISCOVERED)
        cost.estimated.kernels_to_prepare = 1;
    if (d.graph_readiness == DimensionStatus::COLD || d.graph_readiness == DimensionStatus::DISCOVERED)
        cost.estimated.graphs_to_prepare = 1;
    if (d.cuda_context_readiness == DimensionStatus::COLD || d.cuda_context_readiness == DimensionStatus::DISCOVERED)
        cost.estimated.context_init_steps = 1;
    if (d.adapter_residency == DimensionStatus::COLD || d.adapter_residency == DimensionStatus::DISCOVERED)
        cost.estimated.adapter_activation_bytes = 1ULL << 28;
    if (d.tokenizer_readiness == DimensionStatus::COLD) cost.estimated.tokenizer_init_steps = 1;
    if (d.allocator_readiness == DimensionStatus::COLD) cost.estimated.allocator_init_steps = 1;
    if (d.prefix_kv_reuse == DimensionStatus::PARTIAL) cost.estimated.prefix_kv_bytes = 1ULL << 26;
    if (d.engine_readiness == DimensionStatus::COLD || d.engine_readiness == DimensionStatus::DISCOVERED)
        cost.estimated.engine_startup_ms = cold ? 900 : 200;
    if (!d.all_ready()) cost.estimated.dependency_checks = 1;
    if (d.any_suspect()) cost.estimated.dependency_checks += 1;
    if (cost.estimated.dependency_checks > 0) cost.estimated.sync_cost_ms = warm ? 2 : (partial ? 60 : 250);

    double ttf = 0.0;
    if (cost.estimated.bytes_to_transfer > 0)
        ttf += static_cast<double>(cost.estimated.bytes_to_transfer) / (40e9 * 0.9);
    if (cost.estimated.context_init_steps > 0) ttf += 1200;
    if (cost.estimated.kernels_to_prepare > 0) ttf += 600;
    if (cost.estimated.graphs_to_prepare > 0) ttf += 400;
    if (cost.estimated.adapter_activation_bytes > 0) ttf += 300;
    if (cost.estimated.tokenizer_init_steps > 0) ttf += 30;
    if (cost.estimated.allocator_init_steps > 0) ttf += 150;
    if (cost.estimated.prefix_kv_bytes > 0) ttf += 200;
    ttf += static_cast<double>(cost.estimated.engine_startup_ms);
    ttf += static_cast<double>(cost.estimated.sync_cost_ms);
    if (cold) ttf += 3000;

    cost.expected_ttfu_ms = ttf;
    return cost;
}

} // namespace cost_model

} // namespace warmth
