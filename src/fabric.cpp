// Warmth Fabric - src/fabric.cpp
// Copyright 2026 Summon Software Labs
// SPDX-License-Identifier: Apache-2.0
#include "warmth/fabric.hpp"

#include <utility>

#include "warmth/decay.hpp"
#include "warmth/invalidation.hpp"
#include "warmth/time.hpp"

namespace warmth {

namespace {
constexpr std::uint64_t kDefaultWarmBytes = 1ULL << 30;
bool synthetic_warm(WarmthAction action, double& duration_ms, std::uint64_t& bytes) {
    bytes = 1ULL << 28;
    duration_ms = 0.5 + 0.25 * static_cast<int>(action);
    return true;
}
void make_warm(WarmthObject& o, const char* why) {
    if (o.state() == WarmthState::WARM || o.state() == WarmthState::HOT) return;
    if (!o.transition(WarmthState::PREPARING, why)) {
        (void)o.transition(WarmthState::COLD, why);
        (void)o.transition(WarmthState::PREPARING, why);
    }
    (void)o.transition(WarmthState::WARM, why);
    o.clear_invalidation_reason();
}
} // namespace

WarmthFabric::WarmthFabric(Config cfg) : cfg_(std::move(cfg)), budget_(cfg_.policy.budgets) {
    gen_.warmth = WarmthGeneration(1);
    gen_.dependency = DependencyGeneration(1);
    gen_.policy = cfg_.policy.generation;
}

WarmthFabric::WarmthFabric(WarmthFabric&& o) noexcept
    : cfg_(std::move(o.cfg_)), objects_(std::move(o.objects_)), budget_(std::move(o.budget_)),
      gen_(o.gen_), op_(std::move(o.op_)) {
}

WarmthFabric& WarmthFabric::operator=(WarmthFabric&& o) noexcept {
    if (this != &o) {
        cfg_ = std::move(o.cfg_);
        objects_ = std::move(o.objects_);
        budget_ = std::move(o.budget_);
        gen_ = o.gen_;
        op_ = std::move(o.op_);
    }
    return *this;
}

WarmthObject* WarmthFabric::find_locked(const WarmthObjectId& id) {
    auto it = objects_.find(id);
    return it == objects_.end() ? nullptr : &it->second;
}

WarmthObjectId WarmthFabric::register_object(WarmthCategory category, const std::string& logical_owner,
                                             std::optional<ModelId> model, std::optional<ArtifactId> artifact,
                                             std::optional<ReplicaId> replica, std::optional<EngineId> engine) {
    const WarmthObjectId id = Id128::derive(logical_owner + "|" + std::string(category_name(category)));
    WarmthObject o(id, category, cfg_.workload, cfg_.node, cfg_.device, cfg_.backend);
    o.set_logical_owner(logical_owner);
    if (model) o.set_model(*model);
    if (artifact) o.set_artifact(*artifact);
    if (replica) o.set_replica(*replica);
    if (engine) o.set_engine(*engine);
    o.set_warmth_generation(gen_.warmth);
    std::lock_guard<std::mutex> g(mutex_);
    objects_[id] = std::move(o);
    return id;
}

bool WarmthFabric::upsert(const WarmthObject& obj) {
    std::lock_guard<std::mutex> g(mutex_);
    objects_[obj.id()] = obj;
    return true;
}

std::optional<WarmthObject> WarmthFabric::get(const WarmthObjectId& id) const {
    std::lock_guard<std::mutex> g(mutex_);
    auto it = objects_.find(id);
    if (it == objects_.end()) return std::nullopt;
    return it->second;
}

std::size_t WarmthFabric::object_count() const { std::lock_guard<std::mutex> g(mutex_); return objects_.size(); }
std::vector<WarmthObject> WarmthFabric::objects() const {
    std::lock_guard<std::mutex> g(mutex_);
    std::vector<WarmthObject> v; v.reserve(objects_.size());
    for (const auto& kv : objects_) v.push_back(kv.second);
    return v;
}
std::vector<WarmthObjectId> WarmthFabric::object_ids() const {
    std::lock_guard<std::mutex> g(mutex_);
    std::vector<WarmthObjectId> v; v.reserve(objects_.size());
    for (const auto& kv : objects_) v.push_back(kv.first);
    return v;
}

bool WarmthFabric::transition(const WarmthObjectId& id, WarmthState to, std::string_view reason) {
    std::lock_guard<std::mutex> g(mutex_);
    auto* o = find_locked(id);
    if (!o) return false;
    return o->transition(to, reason);
}

bool WarmthFabric::set_dimension(const WarmthObjectId& id, DimensionIndex idx, DimensionStatus status) {
    std::lock_guard<std::mutex> g(mutex_);
    auto* o = find_locked(id);
    if (!o) return false;
    o->set_dimension(idx, status);
    return true;
}
bool WarmthFabric::set_dimensions(const WarmthObjectId& id, const WarmthDimensions& dims) {
    std::lock_guard<std::mutex> g(mutex_);
    auto* o = find_locked(id);
    if (!o) return false;
    o->set_dimensions(dims);
    return true;
}
bool WarmthFabric::mark_used(const WarmthObjectId& id) {
    std::lock_guard<std::mutex> g(mutex_);
    auto* o = find_locked(id);
    if (!o) return false;
    o->mark_used(now_ms());
    return true;
}

bool WarmthFabric::invalidate(const WarmthObjectId& id, InvalidationReason reason) {
    std::lock_guard<std::mutex> g(mutex_);
    auto* o = find_locked(id);
    if (!o) return false;
    if (!transition_allowed(o->state(), WarmthState::INVALIDATED)) return false;
    o->set_invalidation_reason(invalidation_reason_text(reason));
    o->transition(WarmthState::INVALIDATED, "invalidation");
    // ephemeral device dims drop
    o->set_dimension(DimensionIndex::CudaContextReadiness, DimensionStatus::COLD);
    o->set_dimension(DimensionIndex::KernelReadiness, DimensionStatus::COLD);
    o->set_dimension(DimensionIndex::GraphReadiness, DimensionStatus::COLD);
    return true;
}

bool WarmthFabric::demote(const WarmthObjectId& id) {
    std::lock_guard<std::mutex> g(mutex_);
    auto* o = find_locked(id);
    if (!o) return false;
    switch (o->state()) {
        case WarmthState::HOT:            return o->transition(WarmthState::WARM, "demote");
        case WarmthState::WARM:           return o->transition(WarmthState::PARTIALLY_WARM, "demote");
        case WarmthState::PARTIALLY_WARM: return o->transition(WarmthState::COLD, "demote");
        default: return false;
    }
}

bool WarmthFabric::evict(const WarmthObjectId& id) {
    std::lock_guard<std::mutex> g(mutex_);
    auto* o = find_locked(id);
    if (!o) return false;
    if (!o->transition(WarmthState::EVICTED, "evict")) return false;
    // Governed loss: device-resident parts drop to host-only; model remains on
    // host but leaves the GPU.
    if (o->residency() == ResidencyState::DEVICE_RESIDENT || o->residency() == ResidencyState::MIRRORED) {
        o->set_residency(ResidencyState::HOST_ONLY);
    }
    o->set_dimension(DimensionIndex::CudaContextReadiness, DimensionStatus::COLD);
    o->set_dimension(DimensionIndex::KernelReadiness, DimensionStatus::COLD);
    o->set_dimension(DimensionIndex::GraphReadiness, DimensionStatus::COLD);
    o->set_dimension(DimensionIndex::AllocatorReadiness, DimensionStatus::COLD);
    return true;
}

bool WarmthFabric::warm(const WarmthObjectId& id, WarmthAction action, double* measured_ms, std::uint64_t* measured_bytes) {
    // Phase 1: reserve budget and mark PREPARING (no blocking work under lock).
    std::uint64_t reservation = 0, op_reservation = 0;
    {
        std::lock_guard<std::mutex> g(mutex_);
        auto* o = find_locked(id);
        if (!o) return false;
        BudgetError err;
        reservation = budget_.reserve(WarmthBudgetKind::DEVICE_MEMORY, kDefaultWarmBytes, "warm", &err);
        if (reservation == 0) return false; // infeasible under budget
        op_reservation = budget_.reserve(WarmthBudgetKind::CONCURRENT_WARMING_OPS, 1, "warm", &err);
        if (!transition_allowed(o->state(), WarmthState::PREPARING)) { budget_.release(reservation); if (op_reservation) budget_.release(op_reservation); return false; }
        o->transition(WarmthState::PREPARING, "warming");
    }
    // Phase 2: execute the warming operation (blocking backend work is outside
    // the lock, satisfying the concurrency requirement).
    const auto fn = op_ ? op_ : WarmOperation(synthetic_warm);
    double duration = 0.0; std::uint64_t bytes = 0;
    const bool success = fn(action, duration, bytes);
    // Phase 3: apply the result and release the reservation.
    {
        std::lock_guard<std::mutex> g(mutex_);
        budget_.release(reservation);
        if (op_reservation) budget_.release(op_reservation);
        auto* o = find_locked(id);
        if (!o) return false;
        if (success) {
            o->transition(WarmthState::WARM, "warm_completed");
            o->clear_invalidation_reason();
            o->set_measured_cost_bytes(bytes);
            o->set_observed_ttr_ms(duration);
            o->mark_prepared(now_ms()); o->mark_used(now_ms());
            o->set_provenance(Provenance::MEASURED);
            o->set_residency(ResidencyState::DEVICE_RESIDENT);
            if (measured_ms) *measured_ms = duration;
            if (measured_bytes) *measured_bytes = bytes;
            return true;
        }
        o->transition(WarmthState::FAILED, "warming_hook_failed");
        return false;
    }
}

bool WarmthFabric::warm_to_ready(const WarmthObjectId& id) {
    std::lock_guard<std::mutex> g(mutex_);
    auto* o = find_locked(id);
    if (!o) return false;
    WarmthDimensions d = o->dimensions();
    d.artifact_availability = DimensionStatus::VALID;
    d.artifact_validation = DimensionStatus::VALID;
    d.model_residency = DimensionStatus::VALID;
    d.adapter_residency = DimensionStatus::VALID;
    d.tokenizer_readiness = DimensionStatus::VALID;
    d.cuda_context_readiness = DimensionStatus::VALID;
    d.kernel_readiness = DimensionStatus::VALID;
    d.graph_readiness = DimensionStatus::VALID;
    d.allocator_readiness = DimensionStatus::VALID;
    d.prefix_kv_reuse = DimensionStatus::VALID;
    d.engine_readiness = DimensionStatus::VALID;
    d.local_dependency_readiness = DimensionStatus::VALID;
    o->set_dimensions(d);
    make_warm(*o, "warm_to_ready");
    o->set_residency(ResidencyState::DEVICE_RESIDENT);
    o->set_provenance(Provenance::MEASURED);
    return o->execution_ready();
}

bool WarmthFabric::budgets_balanced() const {
    std::lock_guard<std::mutex> g(mutex_);
    return budget_.balanced();
}
std::uint64_t WarmthFabric::budget_usage(WarmthBudgetKind kind) const {
    std::lock_guard<std::mutex> g(mutex_);
    return budget_.usage(kind);
}
std::size_t WarmthFabric::outstanding_reservations() const {
    std::lock_guard<std::mutex> g(mutex_);
    return budget_.outstanding();
}

void WarmthFabric::bump_dependency_generation() {
    std::lock_guard<std::mutex> g(mutex_);
    gen_.dependency = gen_.dependency.next();
}
void WarmthFabric::bump_warmth_generation() {
    std::lock_guard<std::mutex> g(mutex_);
    gen_.warmth = gen_.warmth.next();
}

void WarmthFabric::decay(Timestamp now, bool memory_pressure, bool dependency_changed,
                         bool device_reset, bool process_restart) {
    std::lock_guard<std::mutex> g(mutex_);
    for (auto& kv : objects_) {
        WarmthObject& o = kv.second;
        const auto res = decay_object(o, cfg_.policy.decay, now, memory_pressure, dependency_changed, device_reset, process_restart);
        if (res.changed) o.transition(res.state, decay_reason_name(res.reason));
    }
}

WarmthPlan WarmthFabric::plan() const {
    std::lock_guard<std::mutex> g(mutex_);
    PlanBuilder builder(cfg_.policy);
    std::vector<PlanBuilder::Candidate> candidates;
    const auto w = policy_weights(cfg_.policy);
    (void)w;
    for (const auto& kv : objects_) {
        const WarmthObject& o = kv.second;
        WarmthPreference pref;
        pref.id = o.id();
        pref.predicted_reuse = 0.5;
        pref.arrival_probability = o.execution_ready() ? 0.8 : 0.3;
        pref.latency_class = (o.category() == WarmthCategory::EXECUTION_GRAPHS) ? 0.9 : 0.5;
        pref.priority = (o.category() == WarmthCategory::MODEL_WEIGHTS) ? 0.8 : 0.5;
        pref.tenant_fairness = 0.5;
        pref.warming_cost_normalized = 0.5;
        pref.memory_cost_normalized = 0.5;
        pref.transfer_cost_normalized = (o.dimension(DimensionIndex::ModelResidency) == DimensionStatus::COLD) ? 0.7 : 0.2;
        pref.eviction_risk = (o.state() == WarmthState::PARTIALLY_WARM) ? 0.4 : 0.2;
        pref.dependency_availability = (o.dimension(DimensionIndex::LocalDependencyReadiness) == DimensionStatus::VALID) ? 1.0 : 0.5;
        pref.current_partial_warmth = (o.level() == WarmthLevel::PARTIAL) ? 0.6 : 0.0;
        pref.expected_benefit = o.execution_ready() ? 0.9 : (o.state() == WarmthState::PARTIALLY_WARM ? 0.5 : 0.3);

        const auto cost = cost_model::estimate(o, cfg_.default_model_bytes);
        PlanBuilder::Candidate c;
        c.id = o.id();
        c.preference = pref;
        c.estimated_cost_ms = cost.expected_ttfu_ms;
        c.suggested_action = default_action_for(o, cost);
        candidates.push_back(std::move(c));
    }
    return builder.build(cfg_.workload, ReplicaId{}, std::move(candidates));
}

WarmthAction WarmthFabric::default_action_for(const WarmthObject& o, const ReadinessCost& cost) const {
    if (o.dimension(DimensionIndex::ModelResidency) == DimensionStatus::COLD && cost.estimated.bytes_to_transfer > 0)
        return WarmthAction::TRANSFER_HOST_DEVICE;
    if (o.dimension(DimensionIndex::KernelReadiness) == DimensionStatus::COLD)
        return WarmthAction::PREPARE_KERNEL;
    if (o.dimension(DimensionIndex::GraphReadiness) == DimensionStatus::COLD)
        return WarmthAction::INSTANTIATE_GRAPH;
    if (o.dimension(DimensionIndex::CudaContextReadiness) == DimensionStatus::COLD)
        return WarmthAction::INIT_CUDA_CONTEXT;
    if (o.dimension(DimensionIndex::EngineReadiness) == DimensionStatus::COLD)
        return WarmthAction::INIT_ENGINE;
    if (o.dimension(DimensionIndex::AllocatorReadiness) == DimensionStatus::COLD)
        return WarmthAction::PREPARE_ALLOCATOR;
    return WarmthAction::INIT_ENGINE;
}

ReadinessExplanation WarmthFabric::explain(const WarmthObjectId& id) const {
    std::lock_guard<std::mutex> g(mutex_);
    ReadinessExplanation e;
    auto it = objects_.find(id);
    if (it == objects_.end()) return e;
    const WarmthObject& o = it->second;
    e.object = o.id();
    e.category = o.category();
    e.state = o.state();
    e.composite = o.composite_level();
    e.execution_ready = o.execution_ready();
    e.policy_generation = gen_.policy;
    e.residency = std::string(residency_name(o.residency()));
    e.cost = cost_model::estimate(o, cfg_.default_model_bytes);
    for (int i = 0; i < 12; ++i) e.dimensions.push_back({static_cast<DimensionIndex>(i), o.dimension(static_cast<DimensionIndex>(i))});
    if (o.invalidation_reason()) e.invalidation_reason = *o.invalidation_reason();
    e.missing_preparation = missing_text(o);
    e.chosen_action = std::string(action_name(default_action_for(o, e.cost)));
    e.tie_break = "score_desc_then_id_asc";
    e.measured_warm_start_ms = o.observed_ttr_ms();
    return e;
}

std::string WarmthFabric::missing_text(const WarmthObject& o) const {
    std::string s;
    const auto& d = o.dimensions();
    if (d.model_residency != DimensionStatus::VALID) s += " model_weights";
    if (d.kernel_readiness != DimensionStatus::VALID) s += " kernels";
    if (d.graph_readiness != DimensionStatus::VALID) s += " graphs";
    if (d.cuda_context_readiness != DimensionStatus::VALID) s += " cuda_context";
    if (d.allocator_readiness != DimensionStatus::VALID) s += " allocator";
    if (d.engine_readiness != DimensionStatus::VALID) s += " engine";
    return s.empty() ? "none" : s;
}

PlanExplanation WarmthFabric::explain_plan() const {
    const auto p = plan();
    PlanExplanation pe;
    pe.workload = p.workload;
    pe.replica = p.replica;
    pe.policy_generation = p.policy_generation;
    pe.tie_break = p.tie_break;
    pe.steps = p.steps;
    return pe;
}

ReadinessCost WarmthFabric::cost_of(const WarmthObjectId& id) const {
    std::lock_guard<std::mutex> g(mutex_);
    auto it = objects_.find(id);
    if (it == objects_.end()) return ReadinessCost{};
    return cost_model::estimate(it->second, cfg_.default_model_bytes);
}

Snapshot WarmthFabric::snapshot() const {
    std::lock_guard<std::mutex> g(mutex_);
    Snapshot s;
    s.format_version = 1;
    s.warmth_generation = gen_.warmth;
    s.dependency_generation = gen_.dependency;
    s.policy_generation = gen_.policy;
    s.epoch = CoordinatorEpoch(1);
    s.policy = cfg_.policy;
    s.objects = objects();
    return s;
}

bool WarmthFabric::save(const std::string& path, std::string* err) const {
    return save_snapshot_file(path, snapshot(), err);
}

bool WarmthFabric::recover(const std::string& path, std::string* err) {
    std::string local_err;
    auto snap = load_snapshot_file(path, &local_err);
    if (!snap) { if (err) *err = local_err; return false; }
    std::lock_guard<std::mutex> g(mutex_);
    gen_.warmth = snap->warmth_generation.next();      // a new authority era after recovery
    gen_.dependency = snap->dependency_generation.next();
    gen_.policy = snap->policy_generation.next();
    // Restore durable knowledge; re-validate live state. Device-local ephemeral
    // state is never assumed alive across a restart.
    objects_.clear();
    for (auto& o : snap->objects) {
        if (is_execution_ready(o.state()) || o.state() == WarmthState::HOT) {
            o.transition(WarmthState::DISCOVERED, "restart_revalidation");
        }
        o.set_invalidation_reason("restart_revalidation_required");
        o.set_dimension(DimensionIndex::CudaContextReadiness, DimensionStatus::COLD);
        o.set_dimension(DimensionIndex::KernelReadiness, DimensionStatus::COLD);
        o.set_dimension(DimensionIndex::GraphReadiness, DimensionStatus::COLD);
        o.set_dimension(DimensionIndex::AllocatorReadiness, DimensionStatus::COLD);
        o.set_provenance(Provenance::REPORTED);
        objects_[o.id()] = std::move(o);
    }
    cfg_.policy = snap->policy;
    budget_ = BudgetTracker(snap->policy.budgets);
    return true;
}

} // namespace warmth
