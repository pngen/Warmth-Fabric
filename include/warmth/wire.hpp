#pragma once

#include <vector>
#include <set>
#include <string>
#include <utility>
#include <algorithm>

#include "warmth/protocol.hpp"
#include "warmth/warmth_object.hpp"

namespace warmth {

// Encode a WarmthObject into a wire encoder. Deterministic and compatible with
// the object field layout used by the coordinator/worker protocol.
inline void encode_object(proto::WireEncoder& w, const WarmthObject& o) {
    w.id(o.id());
    w.u8(static_cast<std::uint8_t>(o.category()));
    w.id(o.workload());
    w.id(o.node());
    w.str(o.device());
    w.str(o.backend());
    w.str(o.logical_owner());
    auto opt = [&](const std::optional<Id128>& id) { if (id && id->is_valid()) { w.u8(1); w.id(*id); } else w.u8(0); };
    opt(o.model()); opt(o.artifact()); opt(o.replica()); opt(o.engine());
    w.u8(static_cast<std::uint8_t>(o.state()));
    w.str(o.state_change_reason());
    for (int i = 0; i < 12; ++i) w.u8(static_cast<std::uint8_t>(o.dimension(static_cast<DimensionIndex>(i))));
    w.u64(static_cast<std::uint64_t>(o.last_prepared()));
    w.u64(static_cast<std::uint64_t>(o.last_used()));
    w.u64(o.estimated_cost_bytes());
    w.u64(o.measured_cost_bytes());
    w.f64(o.estimated_ttr_ms());
    w.f64(o.observed_ttr_ms());
    w.u8(static_cast<std::uint8_t>(o.residency()));
    w.u8(static_cast<std::uint8_t>(o.lifecycle()));
    w.f64(o.confidence());
    w.f64(o.freshness());
    w.u64(o.warmth_generation().value());
    const auto& dg = o.dependency_generations();
    w.u32(static_cast<std::uint32_t>(dg.size()));
    std::vector<std::pair<int, DependencyGeneration>> sorted;
    sorted.reserve(dg.size());
    for (const auto& kv : dg) sorted.emplace_back(static_cast<int>(kv.first), kv.second);
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b){ return a.first < b.first; });
    for (const auto& kv : sorted) { w.u8(static_cast<std::uint8_t>(kv.first)); w.u64(kv.second.value()); }
    if (o.invalidation_reason() && !o.invalidation_reason()->empty()) { w.u8(1); w.str(*o.invalidation_reason()); }
    else w.u8(0);
    w.u8(static_cast<std::uint8_t>(o.provenance()));
}

// Decode a WarmthObject from a wire decoder. Returns false on any malformed
// input (invalid enum, truncation, duplicate dependency kind, etc.).
inline bool decode_object(proto::WireDecoder& r, WarmthObject& o) {
    Id128 id, workload, node;
    // NOTE: field order must exactly match encode_object:
    //   id, category, workload, node, device, backend, owner, options, state, ...
    if (!r.id(id)) return false;
    std::uint8_t cat = 0; if (!r.u8(cat)) return false;
    if (!r.id(workload) || !r.id(node)) return false;
    auto valid_cat = [](std::uint8_t c) {
        switch (static_cast<WarmthCategory>(c)) {
            case WarmthCategory::MODEL_WEIGHTS: case WarmthCategory::ADAPTERS: case WarmthCategory::TOKENIZER_RUNTIME:
            case WarmthCategory::KV_PREFIX_STATE: case WarmthCategory::KERNEL_ARTIFACTS: case WarmthCategory::LOADED_KERNEL_MODULES:
            case WarmthCategory::EXECUTION_GRAPHS: case WarmthCategory::ALLOCATOR_POOLS: case WarmthCategory::CUDA_CONTEXT_DEVICE:
            case WarmthCategory::ENGINE_RUNTIME_PROCESS: case WarmthCategory::PERSISTENT_RUNTIME_CACHE:
            case WarmthCategory::APPLICATION_DEFINED: return true;
        }
        return false;
    };
    if (!valid_cat(cat)) return false;
    std::string device, backend, owner;
    if (!r.str(device) || !r.str(backend) || !r.str(owner)) return false;
    auto read_opt = [&](std::optional<Id128>& out) -> bool {
        std::uint8_t present = 0; if (!r.u8(present)) return false;
        if (present == 0) { out.reset(); return true; }
        if (present != 1) return false;
        Id128 v; if (!r.id(v)) return false; out = v; return true;
    };
    std::optional<Id128> model, artifact, replica, engine;
    if (!read_opt(model) || !read_opt(artifact) || !read_opt(replica) || !read_opt(engine)) return false;

    std::uint8_t st = 0; if (!r.u8(st)) return false;
    auto valid_state = [](std::uint8_t s) {
        switch (static_cast<WarmthState>(s)) {
            case WarmthState::COLD: case WarmthState::DISCOVERED: case WarmthState::PREPARING:
            case WarmthState::PARTIALLY_WARM: case WarmthState::WARM: case WarmthState::HOT:
            case WarmthState::STALE: case WarmthState::INVALIDATED: case WarmthState::EVICTED:
            case WarmthState::FAILED: return true;
        }
        return false;
    };
    if (!valid_state(st)) return false;
    std::string reason; if (!r.str(reason)) return false;

    WarmthObject obj(id, static_cast<WarmthCategory>(cat), workload, node, device, backend);
    obj.set_logical_owner(owner);
    if (model) obj.set_model(*model);
    if (artifact) obj.set_artifact(*artifact);
    if (replica) obj.set_replica(*replica);
    if (engine) obj.set_engine(*engine);
    obj.restore_state(static_cast<WarmthState>(st), reason);

    auto valid_dim = [](std::uint8_t d) {
        switch (static_cast<DimensionStatus>(d)) {
            case DimensionStatus::COLD: case DimensionStatus::DISCOVERED: case DimensionStatus::PARTIAL:
            case DimensionStatus::READY: case DimensionStatus::VALID: case DimensionStatus::SUSPECT: return true;
        }
        return false;
    };
    for (int i = 0; i < 12; ++i) { std::uint8_t d = 0; if (!r.u8(d)) return false; if (!valid_dim(d)) return false; obj.set_dimension(static_cast<DimensionIndex>(i), static_cast<DimensionStatus>(d)); }

    std::uint64_t lp = 0, lu = 0, ecb = 0, mcb = 0; double ettr = 0, ottr = 0;
    if (!r.u64(lp) || !r.u64(lu)) return false;
    obj.mark_prepared(static_cast<Timestamp>(lp)); obj.mark_used(static_cast<Timestamp>(lu));
    if (!r.u64(ecb) || !r.u64(mcb) || !r.f64(ettr) || !r.f64(ottr)) return false;
    obj.set_estimated_cost_bytes(ecb); obj.set_measured_cost_bytes(mcb);
    obj.set_estimated_ttr_ms(ettr); obj.set_observed_ttr_ms(ottr);

    std::uint8_t res = 0; if (!r.u8(res)) return false;
    auto valid_res = [](std::uint8_t v) {
        switch (static_cast<ResidencyState>(v)) {
            case ResidencyState::NONE: case ResidencyState::STORAGE_ONLY: case ResidencyState::HOST_ONLY:
            case ResidencyState::DEVICE_RESIDENT: case ResidencyState::MIRRORED: return true;
        }
        return false;
    };
    if (!valid_res(res)) return false;
    std::uint8_t lc = 0; if (!r.u8(lc)) return false;
    auto valid_lc = [](std::uint8_t v) {
        switch (static_cast<Lifecycle>(v)) {
            case Lifecycle::NOT_STARTED: case Lifecycle::ADMITTED: case Lifecycle::PREPARING:
            case Lifecycle::READY: case Lifecycle::COOLING: case Lifecycle::EVICTING: case Lifecycle::TERMINATED: return true;
        }
        return false;
    };
    if (!valid_lc(lc)) return false;
    double conf = 0, fresh = 0; if (!r.f64(conf) || !r.f64(fresh)) return false;
    obj.set_residency(static_cast<ResidencyState>(res)); obj.set_lifecycle(static_cast<Lifecycle>(lc));
    obj.set_confidence(conf); obj.set_freshness(fresh);

    std::uint64_t wg = 0; if (!r.u64(wg)) return false;
    obj.set_warmth_generation(WarmthGeneration(wg));

    std::uint32_t nd = 0; if (!r.u32(nd)) return false;
    if (nd > 64) return false;
    std::set<std::uint8_t> seen;
    auto valid_dep = [](std::uint8_t v) {
        switch (static_cast<DependencyKind>(v)) {
            case DependencyKind::MODEL: case DependencyKind::ADAPTER_SET: case DependencyKind::MASK_TOKENIZER:
            case DependencyKind::KERNEL_SET: case DependencyKind::GRAPH_SET: case DependencyKind::RUNTIME:
            case DependencyKind::COMPILER: case DependencyKind::COMPUTE_CAPABILITY: case DependencyKind::DEVICE:
            case DependencyKind::POLICY: case DependencyKind::REPLICA: case DependencyKind::WORKER_BOOT: return true;
        }
        return false;
    };
    for (std::uint32_t k = 0; k < nd; ++k) {
        std::uint8_t dk = 0; if (!r.u8(dk)) return false;
        if (!valid_dep(dk)) return false;
        if (!seen.insert(dk).second) return false;
        std::uint64_t g = 0; if (!r.u64(g)) return false;
        obj.set_dependency_generation(static_cast<DependencyKind>(dk), DependencyGeneration(g));
    }
    std::uint8_t hasInv = 0; if (!r.u8(hasInv)) return false;
    if (hasInv == 1) { std::string inv; if (!r.str(inv)) return false; obj.set_invalidation_reason(inv); }
    else if (hasInv != 0) return false;
    std::uint8_t prov = 0; if (!r.u8(prov)) return false;
    auto valid_prov = [](std::uint8_t v) {
        switch (static_cast<Provenance>(v)) {
            case Provenance::LOCAL: case Provenance::COORDINATOR: case Provenance::DERIVED:
            case Provenance::REPORTED: case Provenance::MEASURED: return true;
        }
        return false;
    };
    if (!valid_prov(prov)) return false;
    obj.set_provenance(static_cast<Provenance>(prov));
    o = std::move(obj);
    return true;
}

} // namespace warmth
