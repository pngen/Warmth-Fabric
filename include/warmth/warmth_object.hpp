#pragma once

#include <cstdint>
#include <string>
#include <optional>
#include <unordered_map>

#include "warmth/identity.hpp"
#include "warmth/warmth_state.hpp"
#include "warmth/warmth_dimensions.hpp"
#include "warmth/time.hpp"
#include "warmth/detail/macros.hpp"

namespace warmth {

enum class WarmthCategory : std::uint8_t {
    MODEL_WEIGHTS = 0,
    ADAPTERS = 1,
    TOKENIZER_RUNTIME = 2,
    KV_PREFIX_STATE = 3,
    KERNEL_ARTIFACTS = 4,
    LOADED_KERNEL_MODULES = 5,
    EXECUTION_GRAPHS = 6,
    ALLOCATOR_POOLS = 7,
    CUDA_CONTEXT_DEVICE = 8,
    ENGINE_RUNTIME_PROCESS = 9,
    PERSISTENT_RUNTIME_CACHE = 10,
    APPLICATION_DEFINED = 11
};

constexpr const char* category_name(WarmthCategory c) noexcept {
    switch (c) {
        case WarmthCategory::MODEL_WEIGHTS:            return "MODEL_WEIGHTS";
        case WarmthCategory::ADAPTERS:                 return "ADAPTERS";
        case WarmthCategory::TOKENIZER_RUNTIME:        return "TOKENIZER_RUNTIME";
        case WarmthCategory::KV_PREFIX_STATE:          return "KV_PREFIX_STATE";
        case WarmthCategory::KERNEL_ARTIFACTS:         return "KERNEL_ARTIFACTS";
        case WarmthCategory::LOADED_KERNEL_MODULES:    return "LOADED_KERNEL_MODULES";
        case WarmthCategory::EXECUTION_GRAPHS:         return "EXECUTION_GRAPHS";
        case WarmthCategory::ALLOCATOR_POOLS:          return "ALLOCATOR_POOLS";
        case WarmthCategory::CUDA_CONTEXT_DEVICE:      return "CUDA_CONTEXT_DEVICE";
        case WarmthCategory::ENGINE_RUNTIME_PROCESS:   return "ENGINE_RUNTIME_PROCESS";
        case WarmthCategory::PERSISTENT_RUNTIME_CACHE: return "PERSISTENT_RUNTIME_CACHE";
        case WarmthCategory::APPLICATION_DEFINED:      return "APPLICATION_DEFINED";
    }
    return "UNKNOWN";
}

// Where the underlying resource currently resides. This is distinct from warmth
// state: an engine may keep a model on host while its GPU copy cools.
enum class ResidencyState : std::uint8_t {
    NONE = 0,
    STORAGE_ONLY = 1,   // on persistent storage only
    HOST_ONLY = 2,      // resident in host memory
    DEVICE_RESIDENT = 3,// resident on device memory
    MIRRORED = 4        // resident on host and device
};

constexpr const char* residency_name(ResidencyState r) noexcept {
    switch (r) {
        case ResidencyState::NONE:           return "NONE";
        case ResidencyState::STORAGE_ONLY:   return "STORAGE_ONLY";
        case ResidencyState::HOST_ONLY:      return "HOST_ONLY";
        case ResidencyState::DEVICE_RESIDENT:return "DEVICE_RESIDENT";
        case ResidencyState::MIRRORED:       return "MIRRORED";
    }
    return "UNKNOWN";
}

enum class Lifecycle : std::uint8_t {
    NOT_STARTED = 0,
    ADMITTED = 1,
    PREPARING = 2,
    READY = 3,
    COOLING = 4,
    EVICTING = 5,
    TERMINATED = 6
};

constexpr const char* lifecycle_name(Lifecycle l) noexcept {
    switch (l) {
        case Lifecycle::NOT_STARTED: return "NOT_STARTED";
        case Lifecycle::ADMITTED:    return "ADMITTED";
        case Lifecycle::PREPARING:   return "PREPARING";
        case Lifecycle::READY:       return "READY";
        case Lifecycle::COOLING:     return "COOLING";
        case Lifecycle::EVICTING:    return "EVICTING";
        case Lifecycle::TERMINATED:  return "TERMINATED";
    }
    return "UNKNOWN";
}

// Which of the twelve dimensions a caller wants to set.
enum class DimensionIndex : std::uint8_t {
    ArtifactAvailability = 0,
    ArtifactValidation = 1,
    ModelResidency = 2,
    AdapterResidency = 3,
    TokenizerReadiness = 4,
    CudaContextReadiness = 5,
    KernelReadiness = 6,
    GraphReadiness = 7,
    AllocatorReadiness = 8,
    PrefixKvReuse = 9,
    EngineReadiness = 10,
    LocalDependencyReadiness = 11
};

constexpr const char* dimension_name(DimensionIndex i) noexcept {
    switch (i) {
        case DimensionIndex::ArtifactAvailability:     return "artifact_availability";
        case DimensionIndex::ArtifactValidation:       return "artifact_validation";
        case DimensionIndex::ModelResidency:           return "model_residency";
        case DimensionIndex::AdapterResidency:         return "adapter_residency";
        case DimensionIndex::TokenizerReadiness:       return "tokenizer_readiness";
        case DimensionIndex::CudaContextReadiness:     return "cuda_context_readiness";
        case DimensionIndex::KernelReadiness:          return "kernel_readiness";
        case DimensionIndex::GraphReadiness:           return "graph_readiness";
        case DimensionIndex::AllocatorReadiness:       return "allocator_readiness";
        case DimensionIndex::PrefixKvReuse:            return "prefix_kv_reuse";
        case DimensionIndex::EngineReadiness:          return "engine_readiness";
        case DimensionIndex::LocalDependencyReadiness: return "local_dependency_readiness";
    }
    return "unknown";
}

// Provenance categories for a warmth observation or action.
enum class Provenance : std::uint8_t {
    LOCAL = 0,          // observed locally by a worker
    COORDINATOR = 1,    // authoritative in the coordinator
    DERIVED = 2,        // derived from other observations
    REPORTED = 3,       // reported by another worker
    MEASURED = 4        // measured directly
};

constexpr const char* provenance_name(Provenance p) noexcept {
    switch (p) {
        case Provenance::LOCAL:      return "LOCAL";
        case Provenance::COORDINATOR:return "COORDINATOR";
        case Provenance::DERIVED:    return "DERIVED";
        case Provenance::REPORTED:   return "REPORTED";
        case Provenance::MEASURED:   return "MEASURED";
    }
    return "UNKNOWN";
}

// A single authoritative opinion about whether a specific dependency generation
// is satisfied.
enum class DependencyKind : std::uint8_t {
    MODEL = 0,
    ADAPTER_SET = 1,
    MASK_TOKENIZER = 2,
    KERNEL_SET = 3,
    GRAPH_SET = 4,
    RUNTIME = 5,
    COMPILER = 6,
    COMPUTE_CAPABILITY = 7,
    DEVICE = 8,
    POLICY = 9,
    REPLICA = 10,
    WORKER_BOOT = 11
};

constexpr const char* dependency_name(DependencyKind k) noexcept {
    switch (k) {
        case DependencyKind::MODEL:             return "model";
        case DependencyKind::ADAPTER_SET:       return "adapter_set";
        case DependencyKind::MASK_TOKENIZER:    return "tokenizer";
        case DependencyKind::KERNEL_SET:        return "kernel_set";
        case DependencyKind::GRAPH_SET:         return "graph_set";
        case DependencyKind::RUNTIME:           return "runtime";
        case DependencyKind::COMPILER:          return "compiler";
        case DependencyKind::COMPUTE_CAPABILITY:return "compute_capability";
        case DependencyKind::DEVICE:            return "device";
        case DependencyKind::POLICY:            return "policy";
        case DependencyKind::REPLICA:           return "replica";
        case DependencyKind::WORKER_BOOT:       return "worker_boot";
    }
    return "dependency";
}

// ---------------------------------------------------------------------------
// WarmthObject - one operationally meaningful preparedness unit.
// ---------------------------------------------------------------------------
class WarmthObject {
public:
    WarmthObject() = default;

    WarmthObject(WarmthObjectId id, WarmthCategory category, WorkloadId workload,
                 NodeId node, std::string device, std::string backend)
        : id_(id), category_(category), workload_(workload), node_(node),
          device_(std::move(device)), backend_(std::move(backend)) {}

    // ---- identity ----
    [[nodiscard]] WarmthObjectId id() const noexcept { return id_; }
    [[nodiscard]] WarmthCategory category() const noexcept { return category_; }
    [[nodiscard]] WorkloadId workload() const noexcept { return workload_; }
    [[nodiscard]] NodeId node() const noexcept { return node_; }
    [[nodiscard]] const std::string& device() const noexcept { return device_; }
    [[nodiscard]] const std::string& backend() const noexcept { return backend_; }

    // ---- optional ownership aspects ----
    void set_model(ModelId m) { model_ = m; }
    void set_artifact(ArtifactId a) { artifact_ = a; }
    void set_replica(ReplicaId r) { replica_ = r; }
    void set_engine(EngineId e) { engine_ = e; }
    void set_logical_owner(std::string o) { logical_owner_ = std::move(o); }
    [[nodiscard]] std::optional<ModelId> model() const noexcept { return model_; }
    [[nodiscard]] std::optional<ArtifactId> artifact() const noexcept { return artifact_; }
    [[nodiscard]] std::optional<ReplicaId> replica() const noexcept { return replica_; }
    [[nodiscard]] std::optional<EngineId> engine() const noexcept { return engine_; }
    [[nodiscard]] const std::string& logical_owner() const noexcept { return logical_owner_; }

    // ---- warm state ----
    [[nodiscard]] WarmthState state() const noexcept { return state_; }
    [[nodiscard]] WarmthLevel level() const noexcept { return to_level(state_); }

    // Guarded transition. Returns false (and leaves state unchanged) on an
    // invalid transition.
    bool transition(WarmthState to, std::string_view reason = {}) {
        if (!transition_allowed(state_, to)) return false;
        state_ = to;
        state_change_reason_ = std::string(reason);
        return true;
    }

    // Trusted restoration for deserialization/recovery only. Skips the guard so
    // that a previously-valid persisted state can be reconstructed on a fresh
    // COLD object. Never used to promote live execution-readiness.
    void restore_state(WarmthState to, std::string_view reason = {}) {
        state_ = to;
        state_change_reason_ = std::string(reason);
    }

    [[nodiscard]] const std::string& state_change_reason() const noexcept { return state_change_reason_; }

    // ---- dimensions ----
    void set_dimension(DimensionIndex idx, DimensionStatus status) {
        switch (idx) {
            case DimensionIndex::ArtifactAvailability:     dims_.artifact_availability = status; break;
            case DimensionIndex::ArtifactValidation:       dims_.artifact_validation = status; break;
            case DimensionIndex::ModelResidency:           dims_.model_residency = status; break;
            case DimensionIndex::AdapterResidency:         dims_.adapter_residency = status; break;
            case DimensionIndex::TokenizerReadiness:       dims_.tokenizer_readiness = status; break;
            case DimensionIndex::CudaContextReadiness:     dims_.cuda_context_readiness = status; break;
            case DimensionIndex::KernelReadiness:          dims_.kernel_readiness = status; break;
            case DimensionIndex::GraphReadiness:           dims_.graph_readiness = status; break;
            case DimensionIndex::AllocatorReadiness:       dims_.allocator_readiness = status; break;
            case DimensionIndex::PrefixKvReuse:            dims_.prefix_kv_reuse = status; break;
            case DimensionIndex::EngineReadiness:          dims_.engine_readiness = status; break;
            case DimensionIndex::LocalDependencyReadiness: dims_.local_dependency_readiness = status; break;
        }
    }
    [[nodiscard]] DimensionStatus dimension(DimensionIndex idx) const noexcept {
        switch (idx) {
            case DimensionIndex::ArtifactAvailability:     return dims_.artifact_availability;
            case DimensionIndex::ArtifactValidation:       return dims_.artifact_validation;
            case DimensionIndex::ModelResidency:           return dims_.model_residency;
            case DimensionIndex::AdapterResidency:         return dims_.adapter_residency;
            case DimensionIndex::TokenizerReadiness:       return dims_.tokenizer_readiness;
            case DimensionIndex::CudaContextReadiness:     return dims_.cuda_context_readiness;
            case DimensionIndex::KernelReadiness:          return dims_.kernel_readiness;
            case DimensionIndex::GraphReadiness:           return dims_.graph_readiness;
            case DimensionIndex::AllocatorReadiness:       return dims_.allocator_readiness;
            case DimensionIndex::PrefixKvReuse:            return dims_.prefix_kv_reuse;
            case DimensionIndex::EngineReadiness:          return dims_.engine_readiness;
            case DimensionIndex::LocalDependencyReadiness: return dims_.local_dependency_readiness;
        }
        return DimensionStatus::COLD;
    }
    [[nodiscard]] const WarmthDimensions& dimensions() const noexcept { return dims_; }
    void set_dimensions(const WarmthDimensions& d) { dims_ = d; }
    [[nodiscard]] CompositeLevel composite_level() const noexcept { return dims_.composite_level(); }

    // ---- timestamps ----
    void mark_prepared(Timestamp t) { last_prepared_ = t; }
    void mark_used(Timestamp t) { last_used_ = t; }
    [[nodiscard]] Timestamp last_prepared() const noexcept { return last_prepared_; }
    [[nodiscard]] Timestamp last_used() const noexcept { return last_used_; }

    // ---- cost / time-to-ready ----
    void set_estimated_cost_bytes(std::uint64_t b) { estimated_cost_bytes_ = b; }
    void set_measured_cost_bytes(std::uint64_t b) { measured_cost_bytes_ = b; }
    void set_estimated_ttr_ms(double ms) { estimated_ttr_ms_ = ms; }
    void set_observed_ttr_ms(double ms) { observed_ttr_ms_ = ms; }
    [[nodiscard]] std::uint64_t estimated_cost_bytes() const noexcept { return estimated_cost_bytes_; }
    [[nodiscard]] std::uint64_t measured_cost_bytes() const noexcept { return measured_cost_bytes_; }
    [[nodiscard]] double estimated_ttr_ms() const noexcept { return estimated_ttr_ms_; }
    [[nodiscard]] double observed_ttr_ms() const noexcept { return observed_ttr_ms_; }

    // ---- residency / lifecycle ----
    void set_residency(ResidencyState r) { residency_ = r; }
    [[nodiscard]] ResidencyState residency() const noexcept { return residency_; }
    void set_lifecycle(Lifecycle l) { lifecycle_ = l; }
    [[nodiscard]] Lifecycle lifecycle() const noexcept { return lifecycle_; }

    // ---- freshness / confidence ----
    void set_confidence(double c) { confidence_ = clamp_unit(c); }
    [[nodiscard]] double confidence() const noexcept { return confidence_; }
    void set_freshness(double f) { freshness_ = clamp_unit(f); }
    [[nodiscard]] double freshness() const noexcept { return freshness_; }

    // ---- generations ----
    void set_warmth_generation(WarmthGeneration g) { warmth_gen_ = g; }
    [[nodiscard]] WarmthGeneration warmth_generation() const noexcept { return warmth_gen_; }
    void set_dependency_generation(DependencyKind kind, DependencyGeneration g) { dep_gens_[kind] = g; }
    [[nodiscard]] std::optional<DependencyGeneration> dependency_generation(DependencyKind kind) const noexcept {
        const auto it = dep_gens_.find(kind);
        if (it == dep_gens_.end()) return std::nullopt;
        return it->second;
    }
    [[nodiscard]] const std::unordered_map<DependencyKind, DependencyGeneration>& dependency_generations() const noexcept { return dep_gens_; }

    // ---- invalidation ----
    void set_invalidation_reason(std::string reason) { invalidation_reason_ = std::move(reason); }
    [[nodiscard]] const std::optional<std::string>& invalidation_reason() const noexcept { return invalidation_reason_; }
    void clear_invalidation_reason() { invalidation_reason_.reset(); }

    // ---- provenance ----
    void set_provenance(Provenance p) { provenance_ = p; }
    [[nodiscard]] Provenance provenance() const noexcept { return provenance_; }

    [[nodiscard]] bool execution_ready() const noexcept { return is_execution_ready(state_); }

private:
    static double clamp_unit(double v) noexcept { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

    WarmthObjectId id_{};
    WarmthCategory category_ = WarmthCategory::APPLICATION_DEFINED;
    WorkloadId workload_{};
    NodeId node_{};
    std::string device_;
    std::string backend_;
    std::string logical_owner_;

    std::optional<ModelId> model_;
    std::optional<ArtifactId> artifact_;
    std::optional<ReplicaId> replica_;
    std::optional<EngineId> engine_;

    WarmthState state_ = WarmthState::COLD;
    std::string state_change_reason_;
    WarmthDimensions dims_;

    Timestamp last_prepared_ = 0;
    Timestamp last_used_ = 0;
    std::uint64_t estimated_cost_bytes_ = 0;
    std::uint64_t measured_cost_bytes_ = 0;
    double estimated_ttr_ms_ = 0.0;
    double observed_ttr_ms_ = 0.0;

    ResidencyState residency_ = ResidencyState::NONE;
    Lifecycle lifecycle_ = Lifecycle::NOT_STARTED;
    double confidence_ = 0.0;
    double freshness_ = 0.0;

    WarmthGeneration warmth_gen_;
    std::unordered_map<DependencyKind, DependencyGeneration> dep_gens_;
    std::optional<std::string> invalidation_reason_;
    Provenance provenance_ = Provenance::LOCAL;
};

} // namespace warmth
