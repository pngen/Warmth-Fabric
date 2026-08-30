#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "warmth/identity.hpp"
#include "warmth/warmth_object.hpp"
#include "warmth/policy.hpp"
#include "warmth/budget.hpp"
#include "warmth/invalidation.hpp"
#include "warmth/cost_model.hpp"
#include "warmth/scheduler.hpp"
#include "warmth/explanation.hpp"
#include "warmth/persistence.hpp"

namespace warmth {

// The in-process Warmth Fabric facade. Owns authoritative warmth metadata for a
// workload on a node, the budget tracker, generations and policy. All state is
// guarded by an internal mutex; no blocking backend/network work is performed
// while that lock is held.
class WarmthFabric {
public:
    struct Config {
        WorkloadId workload;
        NodeId node;
        std::string device = "cuda:0";
        std::string backend = "cuda";
        WarmthPolicy policy = WarmthPolicy::defaults(PolicyGeneration(1));
        std::uint64_t default_model_bytes = 1ULL << 30;
    };

    using WarmOperation = std::function<bool(WarmthAction, double&, std::uint64_t&)>;

    explicit WarmthFabric(Config cfg);
    WarmthFabric(WarmthFabric&& o) noexcept;
    WarmthFabric& operator=(WarmthFabric&& o) noexcept;
    WarmthFabric(const WarmthFabric&) = delete;
    WarmthFabric& operator=(const WarmthFabric&) = delete;

    WarmthObjectId register_object(WarmthCategory category, const std::string& logical_owner,
                                   std::optional<ModelId> model = std::nullopt,
                                   std::optional<ArtifactId> artifact = std::nullopt,
                                   std::optional<ReplicaId> replica = std::nullopt,
                                   std::optional<EngineId> engine = std::nullopt);
    bool upsert(const WarmthObject& obj);
    std::optional<WarmthObject> get(const WarmthObjectId& id) const;
    std::size_t object_count() const;
    std::vector<WarmthObject> objects() const;
    std::vector<WarmthObjectId> object_ids() const;

    bool transition(const WarmthObjectId& id, WarmthState to, std::string_view reason = {});
    bool set_dimension(const WarmthObjectId& id, DimensionIndex idx, DimensionStatus status);
    bool set_dimensions(const WarmthObjectId& id, const WarmthDimensions& dims);
    bool mark_used(const WarmthObjectId& id);
    bool invalidate(const WarmthObjectId& id, InvalidationReason reason);
    bool demote(const WarmthObjectId& id);
    bool evict(const WarmthObjectId& id);

    bool warm(const WarmthObjectId& id, WarmthAction action = WarmthAction::INIT_ENGINE,
              double* measured_ms = nullptr, std::uint64_t* measured_bytes = nullptr);
    bool warm_to_ready(const WarmthObjectId& id);

    [[nodiscard]] bool budgets_balanced() const;
    [[nodiscard]] std::uint64_t budget_usage(WarmthBudgetKind kind) const;
    [[nodiscard]] std::size_t outstanding_reservations() const;

    void decay(Timestamp now, bool memory_pressure = false, bool dependency_changed = false,
               bool device_reset = false, bool process_restart = false);
    void bump_dependency_generation();
    void bump_warmth_generation();

    WarmthPlan plan() const;
    ReadinessExplanation explain(const WarmthObjectId& id) const;
    PlanExplanation explain_plan() const;
    ReadinessCost cost_of(const WarmthObjectId& id) const;

    Snapshot snapshot() const;
    bool save(const std::string& path, std::string* err = nullptr) const;
    bool recover(const std::string& path, std::string* err = nullptr);

    [[nodiscard]] WarmthGeneration warmth_generation() const { return gen_.warmth; }
    [[nodiscard]] DependencyGeneration dependency_generation() const { return gen_.dependency; }

private:
    struct Generations {
        WarmthGeneration warmth{1};
        DependencyGeneration dependency{1};
        PolicyGeneration policy{1};
    };
    WarmthObject* find_locked(const WarmthObjectId& id);
    WarmthAction default_action_for(const WarmthObject& o, const ReadinessCost& cost) const;
    std::string missing_text(const WarmthObject& o) const;

    Config cfg_;
    mutable std::mutex mutex_;
    std::map<WarmthObjectId, WarmthObject> objects_;
    BudgetTracker budget_;
    Generations gen_;
    WarmOperation op_;
};

} // namespace warmth
