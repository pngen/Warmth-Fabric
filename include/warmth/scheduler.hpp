#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
#include <utility>
#include <array>

#include "warmth/identity.hpp"
#include "warmth/policy.hpp"
#include "warmth/warmth_object.hpp"

namespace warmth {

enum class WarmthAction : std::uint8_t {
    VALIDATE_ARTIFACT = 0,
    LOAD_MODEL = 1,
    TRANSFER_HOST_DEVICE = 2,
    ACTIVATE_ADAPTER = 3,
    INIT_TOKENIZER = 4,
    INIT_CUDA_CONTEXT = 5,
    LOAD_MODULE = 6,
    PREPARE_KERNEL = 7,
    CAPTURE_GRAPH = 8,
    INSTANTIATE_GRAPH = 9,
    PREPARE_ALLOCATOR = 10,
    WARMUP_EXECUTION = 11,
    ACQUIRE_PREFIX_KV = 12,
    INIT_ENGINE = 13
};

constexpr const char* action_name(WarmthAction a) noexcept {
    switch (a) {
        case WarmthAction::VALIDATE_ARTIFACT:    return "VALIDATE_ARTIFACT";
        case WarmthAction::LOAD_MODEL:           return "LOAD_MODEL";
        case WarmthAction::TRANSFER_HOST_DEVICE: return "TRANSFER_HOST_DEVICE";
        case WarmthAction::ACTIVATE_ADAPTER:     return "ACTIVATE_ADAPTER";
        case WarmthAction::INIT_TOKENIZER:       return "INIT_TOKENIZER";
        case WarmthAction::INIT_CUDA_CONTEXT:    return "INIT_CUDA_CONTEXT";
        case WarmthAction::LOAD_MODULE:          return "LOAD_MODULE";
        case WarmthAction::PREPARE_KERNEL:       return "PREPARE_KERNEL";
        case WarmthAction::CAPTURE_GRAPH:        return "CAPTURE_GRAPH";
        case WarmthAction::INSTANTIATE_GRAPH:    return "INSTANTIATE_GRAPH";
        case WarmthAction::PREPARE_ALLOCATOR:    return "PREPARE_ALLOCATOR";
        case WarmthAction::WARMUP_EXECUTION:     return "WARMUP_EXECUTION";
        case WarmthAction::ACQUIRE_PREFIX_KV:    return "ACQUIRE_PREFIX_KV";
        case WarmthAction::INIT_ENGINE:          return "INIT_ENGINE";
    }
    return "UNKNOWN";
}

// The explicit, explainable components of a warming preference. The score is
// derived from the components and the policy weights; it is transparent and
// never opaque. All probabilities/priorities are normalized to [0,1].
struct WarmthPreference {
    double predicted_reuse         = 0.0;
    double arrival_probability     = 0.0;
    double latency_class           = 0.0;   // 1 = lowest latency class
    double priority                = 0.0;
    double tenant_fairness         = 0.0;
    double warming_cost_normalized = 0.5;   // lower is cheaper
    double memory_cost_normalized  = 0.5;
    double transfer_cost_normalized= 0.5;
    double eviction_risk           = 0.0;   // higher is worse
    double dependency_availability = 1.0;   // 1 = all deps available
    double current_partial_warmth  = 0.0;
    double expected_benefit        = 0.0;
    double available_capacity      = 1.0;

    // Positional line in the deterministic tie-break: lower is preferred.
    WarmthObjectId id;   // used only as the deterministic tie-break key
};

inline double clamp01(double v) noexcept { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

// Compute the composite score from the preference and policy weights. The
// result is a single convenience number but it remains explainable through the
// components and weights.
inline double preference_score(const WarmthPreference& p, const double* w) noexcept {
    // w has 12 entries matching the policy weight fields in order.
    double s = 0.0;
    s += w[0]  * clamp01(p.predicted_reuse);
    s += w[1]  * clamp01(p.arrival_probability);
    s += w[2]  * clamp01(p.latency_class);
    s += w[3]  * clamp01(p.priority);
    s += w[4]  * clamp01(p.tenant_fairness);
    s += w[5]  * clamp01(p.warming_cost_normalized);
    s += w[6]  * clamp01(p.memory_cost_normalized);
    s += w[7]  * clamp01(p.transfer_cost_normalized);
    s += w[8]  * clamp01(p.eviction_risk);
    s += w[9]  * clamp01(p.dependency_availability);
    s += w[10] * clamp01(p.current_partial_warmth);
    s += w[11] * clamp01(p.expected_benefit);
    return s;
}

inline std::array<double, 12> policy_weights(const WarmthPolicy& pol) noexcept {
    return { pol.weight_predicted_reuse, pol.weight_arrival_probability, pol.weight_latency_class,
             pol.weight_priority, pol.weight_tenant_fairness, pol.weight_warming_cost,
             pol.weight_memory_cost, pol.weight_transfer_cost, pol.weight_eviction_risk,
             pol.weight_dependency_availability, pol.weight_current_partial_warmth,
             pol.weight_expected_benefit };
}

// A single chosen warming work item.
struct PlanStep {
    WarmthObjectId object;
    WarmthAction action;
    double score = 0.0;
    double estimated_cost_ms = 0.0;
    std::string rationale;   // explainable why this step was scheduled
};

// An ordered warming plan for one workload or replica.
struct WarmthPlan {
    WorkloadId workload;
    ReplicaId replica;
    std::vector<PlanStep> steps;
    PolicyGeneration policy_generation;
    std::string tie_break;   // describing the deterministic ordering used

    [[nodiscard]] bool empty() const noexcept { return steps.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return steps.size(); }
};

// Builds an ordered, deterministic warming plan from a set of candidate
// objects, each with an explicit preference.
class PlanBuilder {
public:
    struct Candidate {
        WarmthObjectId id;
        WarmthPreference preference;
        double estimated_cost_ms = 0.0;
        WarmthAction suggested_action = WarmthAction::INIT_ENGINE;
    };

    explicit PlanBuilder(const WarmthPolicy& policy) : policy_(policy) {}

    // Reorder candidates deterministically: descending score, then ascending id.
    void sort(std::vector<Candidate>& candidates) const {
        const auto w = policy_weights(policy_);
        std::sort(candidates.begin(), candidates.end(),
            [&w](const Candidate& a, const Candidate& b) {
                const double sa = preference_score(a.preference, w.data());
                const double sb = preference_score(b.preference, w.data());
                if (sa != sb) return sa > sb;
                return a.id < b.id;   // deterministic tie-break
            });
    }

    // Build a plan for a workload/replica. Objects listed first are preferred.
    WarmthPlan build(WorkloadId workload, ReplicaId replica,
                     std::vector<Candidate> candidates) const {
        WarmthPlan plan;
        plan.workload = workload;
        plan.replica = replica;
        plan.policy_generation = policy_.generation;
        sort(candidates);
        const auto w = policy_weights(policy_);
        plan.tie_break = "score_desc_then_id_asc";
        for (const auto& c : candidates) {
            PlanStep step;
            step.object = c.id;
            step.action = c.suggested_action;
            step.score = preference_score(c.preference, w.data());
            step.estimated_cost_ms = c.estimated_cost_ms;
            step.rationale = "reuse=" + std::to_string(c.preference.predicted_reuse) +
                             " prob=" + std::to_string(c.preference.arrival_probability) +
                             " priority=" + std::to_string(c.preference.priority) +
                             " benefit=" + std::to_string(c.preference.expected_benefit);
            plan.steps.push_back(std::move(step));
        }
        return plan;
    }

private:
    const WarmthPolicy& policy_;
};

} // namespace warmth
