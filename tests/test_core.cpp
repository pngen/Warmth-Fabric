#include <string>
#include <vector>
#include <cstring>
#include "wtest.hpp"
#include "warmth/identity.hpp"
#include "warmth/warmth_state.hpp"
#include "warmth/warmth_dimensions.hpp"
#include "warmth/warmth_object.hpp"
#include "warmth/decay.hpp"
#include "warmth/invalidation.hpp"
#include "warmth/cost_model.hpp"
#include "warmth/scheduler.hpp"
#include "warmth/explanation.hpp"
#include "warmth/budget.hpp"
#include "warmth/fabric.hpp"

using namespace warmth;

WTEST(identity_roundtrip) {
    Id128 a = Id128::derive("hello");
    CHECK(a.is_valid());
    const auto s = a.to_string();
    CHECK_EQ(s.size(), 32);
    const auto b = Id128::from_string(s);
    CHECK(b.has_value());
    CHECK(b->high() == a.high());
    CHECK(b->low() == a.low());
    CHECK(b->to_string() == s);
    // Different inputs -> different ids.
    CHECK(Id128::derive("a") != Id128::derive("b"));
    // Zero id is invalid.
    CHECK(!Id128{}.is_valid());
    // Generation monotonic.
    WarmthGeneration g(5); CHECK_EQ(g.next().value(), 6);
    CHECK(WarmthGeneration(1) < WarmthGeneration(2));
}

WTEST(transitions_guarded) {
    WarmthObject o(Id128::derive("t"), WarmthCategory::MODEL_WEIGHTS, WorkloadId::derive("w"), NodeId::derive("n"), "cuda:0", "cuda");
    CHECK(o.state() == WarmthState::COLD);
    // COLD -> WARM is invalid.
    CHECK(!o.transition(WarmthState::WARM));
    CHECK(o.state() == WarmthState::COLD);
    CHECK(o.transition(WarmthState::PREPARING));
    CHECK(o.transition(WarmthState::WARM));
    CHECK(o.execution_ready());
    CHECK(o.transition(WarmthState::STALE));
    CHECK(!o.execution_ready());
    CHECK(o.transition(WarmthState::INVALIDATED));
    CHECK(o.transition(WarmthState::PREPARING)); // INVALIDATED->PREPARING is allowed by the table
    CHECK(transition_allowed(WarmthState::HOT, WarmthState::WARM));
    CHECK(!transition_allowed(WarmthState::COLD, WarmthState::HOT));
}

WTEST(dimensions_composite) {
    WarmthDimensions d;
    CHECK(d.composite_level() == CompositeLevel::COLD);
    CHECK(!d.all_ready());
    d.artifact_availability = DimensionStatus::VALID; d.artifact_validation = DimensionStatus::VALID;
    d.model_residency = DimensionStatus::VALID; d.adapter_residency = DimensionStatus::VALID;
    d.tokenizer_readiness = DimensionStatus::VALID; d.cuda_context_readiness = DimensionStatus::VALID;
    d.kernel_readiness = DimensionStatus::VALID; d.graph_readiness = DimensionStatus::VALID;
    d.prefix_kv_reuse = DimensionStatus::VALID; d.engine_readiness = DimensionStatus::VALID;
    d.local_dependency_readiness = DimensionStatus::VALID; d.allocator_readiness = DimensionStatus::VALID;
    CHECK(d.all_ready());
    CHECK(d.composite_level() == CompositeLevel::HOT);
    d.allocator_readiness = DimensionStatus::COLD;
    CHECK(!d.all_ready());
}

WTEST(decay_deterministic) {
    DecayPolicy p;
    WarmthObject o(Id128::derive("d"), WarmthCategory::MODEL_WEIGHTS, WorkloadId::derive("w"), NodeId::derive("n"), "cuda:0", "cuda");
    o.transition(WarmthState::PREPARING, "p");
    o.transition(WarmthState::WARM, "warm");
    o.mark_used(now_ms());
    // Immediate: no decay.
    auto r0 = decay_object(o, p, now_ms(), false, false, false, false);
    CHECK(!r0.changed);
    CHECK(r0.state == WarmthState::WARM);
    // Dependency change -> STALE.
    auto r1 = decay_object(o, p, now_ms(), false, true, false, false);
    CHECK(r1.changed); CHECK(r1.state == WarmthState::STALE);
    // Memory pressure from HOT -> WARM.
    WarmthObject h(Id128::derive("h"), WarmthCategory::MODEL_WEIGHTS, WorkloadId::derive("w"), NodeId::derive("n"), "cuda:0", "cuda");
    h.transition(WarmthState::PREPARING, "p");
    h.transition(WarmthState::WARM, "warm");
    h.transition(WarmthState::HOT, "hot"); h.mark_used(now_ms());
    auto r2 = decay_object(h, p, now_ms(), true, false, false, false);
    CHECK(r2.changed); CHECK(r2.state == WarmthState::WARM);
    // Determinism: identical inputs give identical outputs.
    auto r3 = decay_object(o, p, now_ms(), false, true, false, false);
    CHECK(r3.state == r1.state); CHECK(r3.reason == r1.reason);
    // Idle decay WARM -> STALE after threshold.
    WarmthObject w2(Id128::derive("w2"), WarmthCategory::MODEL_WEIGHTS, WorkloadId::derive("w"), NodeId::derive("n"), "cuda:0", "cuda");
    w2.transition(WarmthState::PREPARING, "p");
    w2.transition(WarmthState::WARM, "warm"); w2.mark_used(now_ms() - 2000 * 1000); // 2000s ago
    auto r4 = decay_object(w2, p, now_ms(), false, false, false, false);
    CHECK(r4.changed); CHECK(r4.state == WarmthState::STALE);
}

WTEST(invalidation_soft_hard) {
    const auto inv = evaluate_invalidation(WarmthState::WARM);
    CHECK(inv.changed); CHECK(inv.state == WarmthState::INVALIDATED);
    const auto inv2 = evaluate_invalidation(WarmthState::INVALIDATED);
    CHECK(!inv2.changed);
    CHECK(std::string(invalidation_reason_name(InvalidationReason::MODEL_REVISION_CHANGE)) == "MODEL_REVISION_CHANGE");
}

WTEST(cost_model_explainable) {
    WarmthObject o(Id128::derive("c"), WarmthCategory::MODEL_WEIGHTS, WorkloadId::derive("w"), NodeId::derive("n"), "cuda:0", "cuda");
    WarmthDimensions d;
    d.model_residency = DimensionStatus::COLD;
    d.kernel_readiness = DimensionStatus::COLD;
    o.set_dimensions(d);
    const auto cost = cost_model::estimate(o, 1ULL << 30);
    CHECK(cost.estimated.bytes_to_transfer > 0);
    CHECK(cost.estimated.kernels_to_prepare > 0);
    CHECK(cost.expected_ttfu_ms > 0);
    CHECK(cost.kind == CostKind::ESTIMATED);
    CHECK(cost.has_measurement() == false);
}

WTEST(scheduler_plan_deterministic) {
    WarmthPolicy policy = WarmthPolicy::defaults(PolicyGeneration(1));
    PlanBuilder builder(policy);
    std::vector<PlanBuilder::Candidate> cands;
    PlanBuilder::Candidate c1; c1.id = Id128::derive("obj1");
    c1.preference.predicted_reuse = 0.9; c1.preference.arrival_probability = 0.9; c1.preference.priority = 1.0;
    PlanBuilder::Candidate c2; c2.id = Id128::derive("obj2");
    c2.preference.predicted_reuse = 0.1; c2.preference.arrival_probability = 0.1; c2.preference.priority = 0.0;
    cands.push_back(c1); cands.push_back(c2);
    auto plan = builder.build(WorkloadId::derive("wl"), ReplicaId{}, std::move(cands));
    CHECK_EQ(plan.steps.size(), 2);
    // Deterministic ordering: high-priority first.
    CHECK(plan.steps[0].object == Id128::derive("obj1"));
    CHECK(plan.steps[1].object == Id128::derive("obj2"));
    // Tie-break determinism: identical preferences -> id ascending.
    // Tie-break determinism: identical preferences -> deterministic ID order.
    auto mk = [] { std::vector<PlanBuilder::Candidate> t;
        PlanBuilder::Candidate a; a.id = Id128::derive("zzz"); a.preference.priority = 0.5;
        PlanBuilder::Candidate b; b.id = Id128::derive("aaa"); b.preference.priority = 0.5;
        t.push_back(a); t.push_back(b); return t; };
    auto plan2 = builder.build(WorkloadId::derive("wl"), ReplicaId{}, mk());
    auto plan3 = builder.build(WorkloadId::derive("wl"), ReplicaId{}, mk());
    CHECK_EQ(plan2.steps.size(), 2);
    CHECK(plan2.steps[0].object == plan3.steps[0].object);
    CHECK(plan2.steps[1].object == plan3.steps[1].object);
    CHECK(plan2.tie_break == "score_desc_then_id_asc");
}

WTEST(explanation_json) {
    WarmthFabric::Config cfg; cfg.workload = WorkloadId::derive("e"); cfg.node = NodeId::derive("n");
    WarmthFabric f(cfg);
    const auto id = f.register_object(WarmthCategory::MODEL_WEIGHTS, "e.model");
    auto e = f.explain(id);
    CHECK(e.execution_ready == false);
    CHECK_EQ(e.dimensions.size(), 12);
    const auto js = readiness_to_json(e);
    CHECK(js.is_object());
    CHECK(js.find("object") != nullptr);
    const auto text = readiness_to_text(e);
    CHECK(text.find("state: COLD") != std::string::npos);
}

WTEST(budget_accounting_to_zero) {
    BudgetTracker t;
    CHECK(t.balanced());
    auto r1 = t.reserve(WarmthBudgetKind::DEVICE_MEMORY, 100, "a");
    auto r2 = t.reserve(WarmthBudgetKind::CONCURRENT_WARMING_OPS, 1, "b");
    CHECK(r1 != 0); CHECK(r2 != 0);
    CHECK(!t.balanced());
    CHECK_EQ(t.usage(WarmthBudgetKind::DEVICE_MEMORY), 100);
    // Double release must be detected.
    CHECK(t.release(r1) == BudgetError::NONE);
    CHECK(t.release(r1) == BudgetError::DOUBLE_RELEASE);
    CHECK(t.release(r2) == BudgetError::NONE);
    CHECK(t.balanced());
    CHECK_EQ(t.outstanding(), 0);
    // Budget limit enforced.
    BudgetTracker limited;
    limited.set_limit(WarmthBudgetKind::DEVICE_MEMORY, 50);
    auto ok = limited.reserve(WarmthBudgetKind::DEVICE_MEMORY, 100);
    CHECK_EQ(ok, 0);
    auto ok2 = limited.reserve(WarmthBudgetKind::DEVICE_MEMORY, 30);
    CHECK(ok2 != 0);
}

WTEST(fabric_cold_warm_invalidate_rewarm) {
    WarmthFabric::Config cfg; cfg.workload = WorkloadId::derive("f"); cfg.node = NodeId::derive("n");
    WarmthFabric f(cfg);
    const auto id = f.register_object(WarmthCategory::MODEL_WEIGHTS, "f.model");
    CHECK(!f.get(id)->execution_ready());
    CHECK(f.warm_to_ready(id));
    CHECK(f.get(id)->execution_ready());
    CHECK(f.mark_used(id));
    CHECK(f.invalidate(id, InvalidationReason::MODEL_REVISION_CHANGE));
    CHECK(!f.get(id)->execution_ready());
    CHECK(f.get(id)->invalidation_reason().has_value());
    // Rewarm requires going through preparation.
    double ms = 0; std::uint64_t bytes = 0;
    CHECK(f.warm(id, WarmthAction::INIT_ENGINE, &ms, &bytes));
    CHECK(f.get(id)->execution_ready());
    // Accounting returns to zero.
    CHECK(f.budgets_balanced());
}

WTEST(fabric_evict_demote) {
    WarmthFabric::Config cfg; cfg.workload = WorkloadId::derive("f2"); cfg.node = NodeId::derive("n");
    WarmthFabric f(cfg);
    const auto id = f.register_object(WarmthCategory::MODEL_WEIGHTS, "f2.model");
    CHECK(f.warm_to_ready(id));
    CHECK(f.demote(id));
    CHECK(f.get(id)->state() == WarmthState::PARTIALLY_WARM);
    CHECK(f.warm_to_ready(id));
    CHECK(f.evict(id));
    CHECK(f.get(id)->state() == WarmthState::EVICTED);
    CHECK(f.get(id)->residency() == ResidencyState::HOST_ONLY);
}

WTEST(generation_relations) {
    // Never silently reuse identity across authority generations.
    WarmthGeneration g1(10), g2(20);
    CHECK(g1 < g2);
    CHECK(g1.next().value() == 11);
    CHECK(g2 > g1);
}

int main() { RUN_TESTS(); }
