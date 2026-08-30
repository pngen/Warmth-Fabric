#pragma once

#include <string>
#include <vector>

#include "warmth/json.hpp"
#include "warmth/scheduler.hpp"
#include "warmth/cost_model.hpp"

namespace warmth {

// A single dimension's readiness, with a human and machine readable status.
struct DimensionReadiness {
    DimensionIndex index;
    DimensionStatus status;
    const char* name() const { return dimension_name(index); }
};

// Structured, explainable readiness for one object/workload/replica. Every
// decision input is exposed; nothing is hidden behind an opaque score.
struct ReadinessExplanation {
    WarmthObjectId object;
    WarmthCategory category;
    WarmthState state;
    CompositeLevel composite;
    std::vector<DimensionReadiness> dimensions;
    ReadinessCost cost;
    bool execution_ready = false;
    std::string invalidation_reason;
    std::string decay_reason;
    PolicyGeneration policy_generation;
    std::string residency;
    std::string chosen_action;
    std::vector<std::string> alternatives;
    std::string tie_break;
    std::string priority;
    std::string deadline;
    std::string fairness;
    std::string budget_constraints;
    std::string missing_preparation;
    double measured_warm_start_ms = 0.0;
    double measured_cold_start_ms = 0.0;
};

inline json::Value readiness_to_json(const ReadinessExplanation& e) {
    json::Value o = json::Value::object();
    o.set("object", e.object.to_string());
    o.set("state", std::string(to_string(e.state)));
    o.set("category", std::string(category_name(e.category)));
    o.set("composite", std::string(composite_name(e.composite)));
    o.set("execution_ready", e.execution_ready);

    json::Value dims = json::Value::array();
    for (const auto& d : e.dimensions) {
        json::Value dj = json::Value::object();
        dj.set("name", std::string(d.name()));
        dj.set("status", std::string(dimension_name(d.status)));
        dims.push(std::move(dj));
    }
    o.set("dimensions", std::move(dims));

    json::Value c = json::Value::object();
    c.set("kind", std::string(cost_kind_name(e.cost.kind)));
    c.set("bytes_to_transfer", static_cast<std::int64_t>(e.cost.estimated.bytes_to_transfer));
    c.set("bytes_resident", static_cast<std::int64_t>(e.cost.measured.bytes_resident));
    c.set("artifacts_to_validate", static_cast<std::int64_t>(e.cost.estimated.artifacts_to_validate));
    c.set("kernels_to_prepare", static_cast<std::int64_t>(e.cost.estimated.kernels_to_prepare));
    c.set("graphs_to_prepare", static_cast<std::int64_t>(e.cost.estimated.graphs_to_prepare));
    c.set("context_init_steps", static_cast<std::int64_t>(e.cost.estimated.context_init_steps));
    c.set("adapter_activation_bytes", static_cast<std::int64_t>(e.cost.estimated.adapter_activation_bytes));
    c.set("tokenizer_init_steps", static_cast<std::int64_t>(e.cost.estimated.tokenizer_init_steps));
    c.set("prefix_kv_bytes", static_cast<std::int64_t>(e.cost.estimated.prefix_kv_bytes));
    c.set("allocator_init_steps", static_cast<std::int64_t>(e.cost.estimated.allocator_init_steps));
    c.set("engine_startup_ms", static_cast<std::int64_t>(e.cost.estimated.engine_startup_ms));
    c.set("sync_cost_ms", static_cast<std::int64_t>(e.cost.estimated.sync_cost_ms));
    c.set("dependency_checks", static_cast<std::int64_t>(e.cost.estimated.dependency_checks));
    c.set("expected_ttfu_ms", e.cost.expected_ttfu_ms);
    c.set("observed_ttfu_ms", e.cost.observed_ttfu_ms);
    o.set("cost", std::move(c));

    o.set("residency", e.residency);
    o.set("chosen_action", e.chosen_action);
    o.set("tie_break", e.tie_break);
    o.set("priority", e.priority);
    o.set("deadline", e.deadline);
    o.set("fairness", e.fairness);
    o.set("budget_constraints", e.budget_constraints);
    o.set("missing_preparation", e.missing_preparation);
    o.set("measured_warm_start_ms", e.measured_warm_start_ms);
    o.set("measured_cold_start_ms", e.measured_cold_start_ms);
    if (!e.invalidation_reason.empty()) o.set("invalidation_reason", e.invalidation_reason);
    if (!e.decay_reason.empty()) o.set("decay_reason", e.decay_reason);

    json::Value alts = json::Value::array();
    for (const auto& a : e.alternatives) alts.push(json::Value(a));
    o.set("alternatives", std::move(alts));
    return o;
}

inline std::string readiness_to_text(const ReadinessExplanation& e) {
    std::string s;
    s += "object: " + e.object.to_string() + "\n";
    s += "state: " + std::string(to_string(e.state)) + "\n";
    s += "composite: " + std::string(composite_name(e.composite)) + "\n";
    s += "execution_ready: " + std::string(e.execution_ready ? "true" : "false") + "\n";
    for (const auto& d : e.dimensions) {
        s += "  dimension " + std::string(d.name()) + " = " + std::string(dimension_name(d.status)) + "\n";
    }
    s += "expected_ttfu_ms: " + std::to_string(e.cost.expected_ttfu_ms) + "\n";
    s += "bytes_to_transfer: " + std::to_string(e.cost.estimated.bytes_to_transfer) + "\n";
    if (!e.invalidation_reason.empty()) s += "invalidation_reason: " + e.invalidation_reason + "\n";
    if (!e.decay_reason.empty()) s += "decay_reason: " + e.decay_reason + "\n";
    if (!e.chosen_action.empty()) s += "chosen_action: " + e.chosen_action + "\n";
    if (!e.missing_preparation.empty()) s += "missing_preparation: " + e.missing_preparation + "\n";
    return s;
}

// PlanExplanation: explain a warming plan, including each step's rationale and
// the deterministic ordering used.
struct PlanExplanation {
    WorkloadId workload;
    ReplicaId replica;
    PolicyGeneration policy_generation;
    std::string tie_break;
    std::vector<PlanStep> steps;
};

inline json::Value plan_to_json(const PlanExplanation& p) {
    json::Value o = json::Value::object();
    o.set("workload", p.workload.to_string());
    o.set("replica", p.replica.to_string());
    o.set("policy_generation", static_cast<std::int64_t>(p.policy_generation.value()));
    o.set("tie_break", p.tie_break);
    json::Value steps = json::Value::array();
    for (const auto& s : p.steps) {
        json::Value sj = json::Value::object();
        sj.set("object", s.object.to_string());
        sj.set("action", std::string(action_name(s.action)));
        sj.set("score", s.score);
        sj.set("estimated_cost_ms", s.estimated_cost_ms);
        sj.set("rationale", s.rationale);
        steps.push(std::move(sj));
    }
    o.set("steps", std::move(steps));
    return o;
}

inline std::string plan_to_text(const PlanExplanation& p) {
    std::string s;
    s += "workload: " + p.workload.to_string() + "\n";
    s += "replica: " + p.replica.to_string() + "\n";
    s += "policy_generation: " + std::to_string(p.policy_generation.value()) + "\n";
    s += "tie_break: " + p.tie_break + "\n";
    for (const auto& st : p.steps) {
        s += "  " + st.object.to_string() + " " + std::string(action_name(st.action)) +
             " score=" + std::to_string(st.score) + " cost_ms=" + std::to_string(st.estimated_cost_ms) +
             " (" + st.rationale + ")\n";
    }
    return s;
}

} // namespace warmth
