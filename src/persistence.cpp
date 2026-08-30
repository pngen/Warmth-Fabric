// Warmth Fabric - src/persistence.cpp
// Copyright 2026 Summon Software Labs
// SPDX-License-Identifier: Apache-2.0
#include "warmth/persistence.hpp"

#include <array>
#include <cstring>
#include <cmath>
#include <set>
#include <stdexcept>
#include <fstream>
#include <algorithm>
#include <utility>

#include "warmth/detail/hash.hpp"

namespace warmth {
namespace {

constexpr std::uint32_t kMagic = 0x57465442u; // "WFTB"
constexpr std::uint32_t kVersion = 1u;

// Big-endian writers.
class ByteWriter {
public:
    void u8(std::uint8_t v) { buf_.push_back(v); }
    void u16(std::uint16_t v) { u8(static_cast<std::uint8_t>(v >> 8)); u8(static_cast<std::uint8_t>(v >> 0)); }
    void u32(std::uint32_t v) { u8(static_cast<std::uint8_t>(v >> 24)); u8(static_cast<std::uint8_t>(v >> 16)); u8(static_cast<std::uint8_t>(v >> 8)); u8(static_cast<std::uint8_t>(v >> 0)); }
    void u64(std::uint64_t v) { for (int i = 7; i >= 0; --i) u8(static_cast<std::uint8_t>(v >> (i * 8))); }
    void f64(double d) {
        if (std::isnan(d) || std::isinf(d)) throw std::invalid_argument("persist: NaN/Inf not allowed");
        std::uint64_t bits = 0;
        std::memcpy(&bits, &d, sizeof(bits));
        u64(bits);
    }
    void bytes(const std::uint8_t* p, std::size_t n) { buf_.insert(buf_.end(), p, p + n); }
    void bytes(std::span<const std::uint8_t> s) { bytes(s.data(), s.size()); }
    // Bounded length prefix.
    void length_and_bytes(std::span<const std::uint8_t> s) {
        u32(static_cast<std::uint32_t>(s.size()));
        bytes(s);
    }
    void str(const std::string& s) {
        length_and_bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(s.data()), s.size()));
    }
    [[nodiscard]] std::vector<std::uint8_t>& data() { return buf_; }
private:
    std::vector<std::uint8_t> buf_;
};

class ByteReader {
public:
    ByteReader(std::span<const std::uint8_t> s) : s_(s) {}
    [[nodiscard]] std::size_t remaining() const { return s_.size() - i_; }
    [[nodiscard]] bool ok() const { return ok_; }

    bool u8(std::uint8_t& v) {
        if (!take(1)) return false;
        v = s_[i_ - 1];
        return true;
    }
    bool u16(std::uint16_t& v) {
        std::uint8_t a, b;
        if (!u8(a) || !u8(b)) return false;
        v = static_cast<std::uint16_t>((a << 8) | b); return true;
    }
    bool u32(std::uint32_t& v) {
        std::uint8_t b[4];
        if (!take(4)) return false;
        for (int k = 0; k < 4; ++k) b[k] = s_[i_ - 4 + k];
        v = (static_cast<std::uint32_t>(b[0]) << 24) | (static_cast<std::uint32_t>(b[1]) << 16) |
            (static_cast<std::uint32_t>(b[2]) << 8) | static_cast<std::uint32_t>(b[3]);
        return true;
    }
    bool u64(std::uint64_t& v) {
        std::uint8_t b[8];
        if (!take(8)) return false;
        for (int k = 0; k < 8; ++k) b[k] = s_[i_ - 8 + k];
        v = 0;
        for (int k = 0; k < 8; ++k) v = (v << 8) | b[k];
        return true;
    }
    bool f64(double& d) {
        std::uint64_t bits = 0;
        if (!u64(bits)) return false;
        std::memcpy(&d, &bits, sizeof(d));
        return !std::isnan(d) && !std::isinf(d); // reject NaN/Inf
    }
    bool bytes(std::uint8_t* out, std::size_t n) {
        if (!take(n)) return false;
        std::memcpy(out, s_.data() + (i_ - n), n);
        return true;
    }
    bool length_and_bytes(std::vector<std::uint8_t>& out) {
        std::uint32_t len = 0;
        if (!u32(len)) return false;
        if (static_cast<std::size_t>(len) > remaining()) return false;
        out.resize(len);
        return bytes(out.data(), len);
    }
    bool str(std::string& out) {
        std::vector<std::uint8_t> b;
        if (!length_and_bytes(b)) return false;
        out.assign(b.begin(), b.end());
        return true;
    }
private:
    bool take(std::size_t n) {
        if (n > remaining()) { ok_ = false; return false; }
        i_ += n;
        return true;
    }
    std::span<const std::uint8_t> s_;
    std::size_t i_ = 0;
    bool ok_ = true;
};

// Enum validity helpers.
bool state_ok(std::uint8_t b) {
    switch (static_cast<WarmthState>(b)) {
        case WarmthState::COLD: case WarmthState::DISCOVERED: case WarmthState::PREPARING:
        case WarmthState::PARTIALLY_WARM: case WarmthState::WARM: case WarmthState::HOT:
        case WarmthState::STALE: case WarmthState::INVALIDATED: case WarmthState::EVICTED:
        case WarmthState::FAILED: return true;
    }
    return false;
}
bool dim_ok(std::uint8_t b) {
    switch (static_cast<DimensionStatus>(b)) {
        case DimensionStatus::COLD: case DimensionStatus::DISCOVERED: case DimensionStatus::PARTIAL:
        case DimensionStatus::READY: case DimensionStatus::VALID: case DimensionStatus::SUSPECT: return true;
    }
    return false;
}
bool category_ok(std::uint8_t b) {
    switch (static_cast<WarmthCategory>(b)) {
        case WarmthCategory::MODEL_WEIGHTS: case WarmthCategory::ADAPTERS: case WarmthCategory::TOKENIZER_RUNTIME:
        case WarmthCategory::KV_PREFIX_STATE: case WarmthCategory::KERNEL_ARTIFACTS: case WarmthCategory::LOADED_KERNEL_MODULES:
        case WarmthCategory::EXECUTION_GRAPHS: case WarmthCategory::ALLOCATOR_POOLS: case WarmthCategory::CUDA_CONTEXT_DEVICE:
        case WarmthCategory::ENGINE_RUNTIME_PROCESS: case WarmthCategory::PERSISTENT_RUNTIME_CACHE:
        case WarmthCategory::APPLICATION_DEFINED: return true;
    }
    return false;
}
bool residency_ok(std::uint8_t b) {
    switch (static_cast<ResidencyState>(b)) {
        case ResidencyState::NONE: case ResidencyState::STORAGE_ONLY: case ResidencyState::HOST_ONLY:
        case ResidencyState::DEVICE_RESIDENT: case ResidencyState::MIRRORED: return true;
    }
    return false;
}
bool lifecycle_ok(std::uint8_t b) {
    switch (static_cast<Lifecycle>(b)) {
        case Lifecycle::NOT_STARTED: case Lifecycle::ADMITTED: case Lifecycle::PREPARING:
        case Lifecycle::READY: case Lifecycle::COOLING: case Lifecycle::EVICTING: case Lifecycle::TERMINATED: return true;
    }
    return false;
}
bool provenance_ok(std::uint8_t b) {
    switch (static_cast<Provenance>(b)) {
        case Provenance::LOCAL: case Provenance::COORDINATOR: case Provenance::DERIVED:
        case Provenance::REPORTED: case Provenance::MEASURED: return true;
    }
    return false;
}
bool dep_kind_ok(std::uint8_t b) {
    switch (static_cast<DependencyKind>(b)) {
        case DependencyKind::MODEL: case DependencyKind::ADAPTER_SET: case DependencyKind::MASK_TOKENIZER:
        case DependencyKind::KERNEL_SET: case DependencyKind::GRAPH_SET: case DependencyKind::RUNTIME:
        case DependencyKind::COMPILER: case DependencyKind::COMPUTE_CAPABILITY: case DependencyKind::DEVICE:
        case DependencyKind::POLICY: case DependencyKind::REPLICA: case DependencyKind::WORKER_BOOT: return true;
    }
    return false;
}

void write_object(ByteWriter& w, const WarmthObject& o) {
    const auto bytes16 = o.id().to_bytes();
    w.bytes(bytes16.data(), 16);
    w.u8(static_cast<std::uint8_t>(o.category()));
    const auto wl = o.workload().to_bytes(); w.bytes(wl.data(), 16);
    const auto nd = o.node().to_bytes(); w.bytes(nd.data(), 16);
    w.str(o.device());
    w.str(o.backend());
    w.str(o.logical_owner());
    auto opt32 = [&](const std::optional<Id128>& id) {
        if (id && id->is_valid()) { w.u8(1); const auto b = id->to_bytes(); w.bytes(b.data(), 16); }
        else w.u8(0);
    };
    opt32(o.model()); opt32(o.artifact()); opt32(o.replica()); opt32(o.engine());
    w.u8(static_cast<std::uint8_t>(o.state()));
    w.str(o.state_change_reason());
    for (int i = 0; i < 12; ++i) {
        w.u8(static_cast<std::uint8_t>(o.dimension(static_cast<DimensionIndex>(i))));
    }
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
    const auto& dgens = o.dependency_generations();
    w.u32(static_cast<std::uint32_t>(dgens.size()));
    // deterministic ordering of dependency kinds
    std::vector<std::pair<int, DependencyGeneration>> sorted;
    sorted.reserve(dgens.size());
    for (const auto& kv : dgens) sorted.emplace_back(static_cast<int>(kv.first), kv.second);
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b){ return a.first < b.first; });
    for (const auto& kv : sorted) {
        w.u8(static_cast<std::uint8_t>(kv.first));
        w.u64(kv.second.value());
    }
    if (o.invalidation_reason() && !o.invalidation_reason()->empty()) { w.u8(1); w.str(*o.invalidation_reason()); }
    else w.u8(0);
    w.u8(static_cast<std::uint8_t>(o.provenance()));
}

void write_policy(ByteWriter& w, const WarmthPolicy& p) {
    w.u64(p.generation.value());
    const auto& d = p.decay;
    auto p2 = [&](double x){ w.f64(x); };
    p2(d.hot_to_warm_idle_seconds); p2(d.warm_to_stale_idle_seconds); p2(d.stale_to_invalidated_seconds);
    p2(d.warm_to_partial_idle_seconds); p2(d.partial_to_cold_idle_seconds);
    w.u8(d.decay_on_memory_pressure ? 1 : 0); w.u8(d.decay_on_process_restart ? 1 : 0);
    w.u8(d.decay_on_device_reset ? 1 : 0); w.u8(d.decay_on_dependency_change ? 1 : 0);
    w.u8(d.decay_on_policy_change ? 1 : 0);
    const auto& b = p.budgets;
    auto u = [&](std::uint64_t x){ w.u64(x); };
    u(b.device_memory_bytes); u(b.pinned_host_memory_bytes); u(b.host_memory_bytes);
    u(b.storage_footprint_bytes); u(b.concurrent_warming_ops); u(b.transfer_bandwidth_bps);
    u(b.active_engines); u(b.warm_replicas);
    p2(p.weight_predicted_reuse); p2(p.weight_arrival_probability); p2(p.weight_latency_class);
    p2(p.weight_priority); p2(p.weight_tenant_fairness); p2(p.weight_warming_cost);
    p2(p.weight_memory_cost); p2(p.weight_transfer_cost); p2(p.weight_eviction_risk);
    p2(p.weight_dependency_availability); p2(p.weight_current_partial_warmth); p2(p.weight_expected_benefit);
}

bool read_object(ByteReader& r, WarmthObject& o, std::string& err, const Snapshot& snap) {
    std::uint8_t idb[16];
    if (!r.bytes(idb, 16)) { err = "truncated object id"; return false; }
    Id128 id = Id128::from_bytes(idb);
    std::uint8_t cat = 0; if (!r.u8(cat)) { err = "truncated category"; return false; }
    if (!category_ok(cat)) { err = "invalid category enum"; return false; }
    std::uint8_t wlb[16]; if (!r.bytes(wlb, 16)) { err = "truncated workload"; return false; }
    std::uint8_t ndb[16]; if (!r.bytes(ndb, 16)) { err = "truncated node"; return false; }
    std::string device, backend, owner;
    if (!r.str(device) || !r.str(backend) || !r.str(owner)) { err = "truncated string"; return false; }
    auto read_opt = [&](std::optional<Id128>& out) -> bool {
        std::uint8_t present = 0; if (!r.u8(present)) { err = "truncated optional"; return false; }
        if (present == 0) { out.reset(); return true; }
        if (present != 1) { err = "invalid optional flag"; return false; }
        std::uint8_t b[16]; if (!r.bytes(b, 16)) { err = "truncated optional id"; return false; }
        out = Id128::from_bytes(b); return true;
    };
    std::optional<Id128> model, artifact, replica, engine;
    if (!read_opt(model) || !read_opt(artifact) || !read_opt(replica) || !read_opt(engine)) return false;

    std::uint8_t st = 0; if (!r.u8(st)) { err = "truncated state"; return false; }
    if (!state_ok(st)) { err = "invalid state enum"; return false; }
    std::string reason; if (!r.str(reason)) { err = "truncated state reason"; return false; }

    WarmthObject obj(id, static_cast<WarmthCategory>(cat),
                     Id128::from_bytes(wlb), Id128::from_bytes(ndb), device, backend);
    obj.set_logical_owner(owner);
    if (model) obj.set_model(*model);
    if (artifact) obj.set_artifact(*artifact);
    if (replica) obj.set_replica(*replica);
    if (engine) obj.set_engine(*engine);
    obj.restore_state(static_cast<WarmthState>(st), reason);

    for (int i = 0; i < 12; ++i) {
        std::uint8_t d = 0; if (!r.u8(d)) { err = "truncated dimension"; return false; }
        if (!dim_ok(d)) { err = "invalid dimension enum"; return false; }
        obj.set_dimension(static_cast<DimensionIndex>(i), static_cast<DimensionStatus>(d));
    }

    std::uint64_t lp = 0; if (!r.u64(lp)) { err = "truncated last_prepared"; return false; }
    std::uint64_t lu = 0; if (!r.u64(lu)) { err = "truncated last_used"; return false; }
    obj.mark_prepared(static_cast<Timestamp>(lp));
    obj.mark_used(static_cast<Timestamp>(lu));

    std::uint64_t ecb = 0, mcb = 0; double ettr = 0, ottr = 0;
    if (!r.u64(ecb) || !r.u64(mcb) || !r.f64(ettr) || !r.f64(ottr)) { err = "truncated cost"; return false; }
    obj.set_estimated_cost_bytes(ecb); obj.set_measured_cost_bytes(mcb);
    obj.set_estimated_ttr_ms(ettr); obj.set_observed_ttr_ms(ottr);

    std::uint8_t res = 0; if (!r.u8(res)) { err = "truncated residency"; return false; }
    if (!residency_ok(res)) { err = "invalid residency enum"; return false; }
    std::uint8_t lc = 0; if (!r.u8(lc)) { err = "truncated lifecycle"; return false; }
    if (!lifecycle_ok(lc)) { err = "invalid lifecycle enum"; return false; }
    double conf = 0, fresh = 0; if (!r.f64(conf) || !r.f64(fresh)) { err = "truncated confidence"; return false; }
    obj.set_residency(static_cast<ResidencyState>(res));
    obj.set_lifecycle(static_cast<Lifecycle>(lc));
    obj.set_confidence(conf); obj.set_freshness(fresh);

    std::uint64_t wgen = 0; if (!r.u64(wgen)) { err = "truncated warmth generation"; return false; }
    obj.set_warmth_generation(WarmthGeneration(wgen));

    std::uint32_t nd = 0; if (!r.u32(nd)) { err = "truncated dependency count"; return false; }
    if (nd > 64) { err = "dependency count too large"; return false; }
    std::set<std::uint8_t> seen;
    for (std::uint32_t k = 0; k < nd; ++k) {
        std::uint8_t dk = 0; if (!r.u8(dk)) { err = "truncated dep kind"; return false; }
        if (!dep_kind_ok(dk)) { err = "invalid dependency kind"; return false; }
        if (!seen.insert(dk).second) { err = "duplicate dependency kind"; return false; }
        std::uint64_t g = 0; if (!r.u64(g)) { err = "truncated dep generation"; return false; }
        obj.set_dependency_generation(static_cast<DependencyKind>(dk), DependencyGeneration(g));
    }

    std::uint8_t hasInv = 0; if (!r.u8(hasInv)) { err = "truncated invalidation flag"; return false; }
    if (hasInv == 1) { std::string inv; if (!r.str(inv)) { err = "truncated invalidation"; return false; } obj.set_invalidation_reason(inv); }
    else if (hasInv != 0) { err = "invalid invalidation flag"; return false; }

    std::uint8_t prov = 0; if (!r.u8(prov)) { err = "truncated provenance"; return false; }
    if (!provenance_ok(prov)) { err = "invalid provenance enum"; return false; }
    obj.set_provenance(static_cast<Provenance>(prov));

    // Generation relations: an object can never belong to a later warmth
    // generation than the snapshot's authoritative warmth generation.
    if (wgen > snap.warmth_generation.value()) { err = "invalid generation relation"; return false; }

    o = std::move(obj);
    return true;
}

bool read_policy(ByteReader& r, WarmthPolicy& p, std::string& err) {
    std::uint64_t gen = 0; if (!r.u64(gen)) { err = "truncated policy gen"; return false; }
    p.generation = PolicyGeneration(gen);
    double v[5];
    for (int i = 0; i < 5; ++i) if (!r.f64(v[i])) { err = "truncated decay"; return false; }
    p.decay.hot_to_warm_idle_seconds = v[0]; p.decay.warm_to_stale_idle_seconds = v[1];
    p.decay.stale_to_invalidated_seconds = v[2]; p.decay.warm_to_partial_idle_seconds = v[3];
    p.decay.partial_to_cold_idle_seconds = v[4];
    std::uint8_t flags[5]; for (int i = 0; i < 5; ++i) { if (!r.u8(flags[i])) return false; if (flags[i] > 1) { err = "invalid policy flag"; return false; } }
    p.decay.decay_on_memory_pressure = flags[0] != 0; p.decay.decay_on_process_restart = flags[1] != 0;
    p.decay.decay_on_device_reset = flags[2] != 0; p.decay.decay_on_dependency_change = flags[3] != 0;
    p.decay.decay_on_policy_change = flags[4] != 0;
    std::uint64_t b[8]; for (int i = 0; i < 8; ++i) if (!r.u64(b[i])) { err = "truncated budgets"; return false; }
    p.budgets.device_memory_bytes = b[0]; p.budgets.pinned_host_memory_bytes = b[1];
    p.budgets.host_memory_bytes = b[2]; p.budgets.storage_footprint_bytes = b[3];
    p.budgets.concurrent_warming_ops = b[4]; p.budgets.transfer_bandwidth_bps = b[5];
    p.budgets.active_engines = b[6]; p.budgets.warm_replicas = b[7];
    double w[12]; for (int i = 0; i < 12; ++i) if (!r.f64(w[i])) { err = "truncated weights"; return false; }
    p.weight_predicted_reuse = w[0]; p.weight_arrival_probability = w[1]; p.weight_latency_class = w[2];
    p.weight_priority = w[3]; p.weight_tenant_fairness = w[4]; p.weight_warming_cost = w[5];
    p.weight_memory_cost = w[6]; p.weight_transfer_cost = w[7]; p.weight_eviction_risk = w[8];
    p.weight_dependency_availability = w[9]; p.weight_current_partial_warmth = w[10]; p.weight_expected_benefit = w[11];
    return true;
}

} // namespace

std::vector<std::uint8_t> encode_snapshot(const Snapshot& s) {
    ByteWriter payload;
    payload.u64(s.epoch.value());
    payload.u64(s.warmth_generation.value());
    payload.u64(s.dependency_generation.value());
    payload.u64(s.policy_generation.value());
    write_policy(payload, s.policy);
    payload.u32(static_cast<std::uint32_t>(s.objects.size()));
    for (const auto& o : s.objects) write_object(payload, o);

    ByteWriter out;
    out.u32(kMagic);
    out.u32(kVersion);
    out.u32(static_cast<std::uint32_t>(payload.data().size()));
    out.u32(detail::crc32(payload.data().data(), payload.data().size()));
    out.bytes(payload.data().data(), payload.data().size());
    return std::move(out.data());
}

std::optional<Snapshot> decode_snapshot(std::span<const std::uint8_t> bytes, std::string* out_error) {
    auto fail = [&](const std::string& e) -> std::optional<Snapshot> {
        if (out_error) *out_error = e;
        return std::nullopt;
    };
    if (bytes.size() < 16) return fail("truncated header");
    ByteReader header(bytes);
    std::uint32_t magic = 0, version = 0, payload_size = 0, checksum = 0;
    if (!header.u32(magic) || magic != kMagic) return fail("bad magic");
    if (!header.u32(version) || version != kVersion) return fail("incompatible version");
    if (!header.u32(payload_size)) return fail("truncated payload size");
    if (!header.u32(checksum)) return fail("truncated checksum");
    if (static_cast<std::size_t>(payload_size) != bytes.size() - 16) {
        return fail(payload_size > bytes.size() - 16 ? "truncated payload" : "trailing garbage");
    }
    const auto payload = bytes.subspan(16);
    const auto actual = detail::crc32(payload.data(), payload.size());
    if (actual != checksum) return fail("checksum mismatch");

    ByteReader r(payload);
    std::uint64_t epoch = 0, wgen = 0, dgen = 0, pgen = 0;
    if (!r.u64(epoch) || !r.u64(wgen) || !r.u64(dgen) || !r.u64(pgen)) return fail("truncated authority");
    Snapshot snap;
    snap.epoch = CoordinatorEpoch(epoch);
    snap.warmth_generation = WarmthGeneration(wgen);
    snap.dependency_generation = DependencyGeneration(dgen);
    snap.policy_generation = PolicyGeneration(pgen);
    std::string perr;
    if (!read_policy(r, snap.policy, perr))
        return fail("policy: " + perr);
    // Policy generation must agree with the top-level authority metadata.
    if (snap.policy.generation != snap.policy_generation)
        return fail("invalid generation relation (policy)");

    std::uint32_t n = 0;
    if (!r.u32(n)) return fail("truncated object count");
    if (n > 1'000'000u) return fail("object count too large");
    std::set<std::string> ids;
    for (std::uint32_t i = 0; i < n; ++i) {
        WarmthObject o;
        std::string err;
        if (!read_object(r, o, err, snap)) return fail("object " + std::to_string(i) + ": " + err);
        const auto idstr = o.id().to_string();
        if (!ids.insert(idstr).second) return fail("duplicate object id");
        snap.objects.push_back(std::move(o));
    }
    if (r.remaining() != 0) return fail("trailing garbage");
    return snap;
}

bool save_snapshot_file(const std::string& path, const Snapshot& snapshot, std::string* out_error) {
    try {
        const auto bytes = encode_snapshot(snapshot);
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) { if (out_error) *out_error = "cannot open " + path; return false; }
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!f) { if (out_error) *out_error = "write failed"; return false; }
        return true;
    } catch (const std::exception& e) {
        if (out_error) *out_error = e.what();
        return false;
    }
}

std::optional<Snapshot> load_snapshot_file(const std::string& path, std::string* out_error) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { if (out_error) *out_error = "cannot open " + path; return std::nullopt; }
    const auto size = f.tellg();
    if (size < 0) { if (out_error) *out_error = "tell failed"; return std::nullopt; }
    f.seekg(0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) f.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!f && !bytes.empty()) { if (out_error) *out_error = "read failed"; return std::nullopt; }
    return decode_snapshot(bytes, out_error);
}

} // namespace warmth
