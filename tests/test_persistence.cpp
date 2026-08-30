#include <vector>
#include <string>
#include <stdexcept>
#include "wtest.hpp"
#include "warmth/persistence.hpp"
#include "warmth/warmth_object.hpp"

using namespace warmth;

static Snapshot make_snapshot() {
    Snapshot s;
    s.format_version = 1;
    s.epoch = CoordinatorEpoch(3);
    s.warmth_generation = WarmthGeneration(7);
    s.dependency_generation = DependencyGeneration(11);
    s.policy_generation = PolicyGeneration(2);
    s.policy = WarmthPolicy::defaults(PolicyGeneration(2));
    WarmthObject o(Id128::derive("snap-object"), WarmthCategory::MODEL_WEIGHTS, WorkloadId::derive("wl"), NodeId::derive("n"), "cuda:0", "cuda");
    o.set_logical_owner("model.weights");
    o.restore_state(WarmthState::WARM, "warm");
    o.mark_prepared(100); o.mark_used(200);
    o.set_measured_cost_bytes(4096);
    o.set_observed_ttr_ms(12.5);
    o.set_residency(ResidencyState::DEVICE_RESIDENT);
    o.set_warmth_generation(WarmthGeneration(7));
    o.set_dependency_generation(DependencyKind::MODEL, DependencyGeneration(11));
    o.set_confidence(0.9); o.set_freshness(0.8);
    o.set_provenance(Provenance::MEASURED);
    s.objects.push_back(o);
    WarmthObject o2(Id128::derive("snap-obj2"), WarmthCategory::APPLICATION_DEFINED, WorkloadId::derive("wl"), NodeId::derive("n"), "cpu", "cpu");
    o2.transition(WarmthState::COLD, "cold");
    s.objects.push_back(o2);
    return s;
}

WTEST(persistence_roundtrip) {
    const auto snap = make_snapshot();
    const auto bytes = encode_snapshot(snap);
    CHECK(!bytes.empty());
    std::string err;
    auto back = decode_snapshot(bytes, &err);
    CHECK(back.has_value());
    CHECK_EQ(back->objects.size(), 2);
    CHECK(back->objects[0].id() == Id128::derive("snap-object"));
    CHECK(back->objects[0].state() == WarmthState::WARM);
    CHECK(back->objects[0].measured_cost_bytes() == 4096);
    CHECK_EQ(back->objects[0].observed_ttr_ms(), 12.5);
    CHECK_EQ(back->objects[1].state(), WarmthState::COLD);
    CHECK(back->epoch == CoordinatorEpoch(3));
    CHECK(back->warmth_generation == WarmthGeneration(7));
}

WTEST(persistence_truncation_rejected) {
    const auto snap = make_snapshot();
    auto bytes = encode_snapshot(snap);
    bytes.resize(bytes.size() - 3); // truncate payload
    std::string err;
    CHECK(!decode_snapshot(bytes, &err).has_value());
    CHECK(err.find("truncated") != std::string::npos || err.find("trailing") != std::string::npos || err.find("checksum") != std::string::npos);
}

WTEST(persistence_checksum_rejected) {
    const auto snap = make_snapshot();
    auto bytes = encode_snapshot(snap);
    bytes[20] ^= 0xFF; // corrupt a payload byte -> checksum mismatch
    CHECK(!decode_snapshot(bytes, nullptr).has_value());
}

WTEST(persistence_bad_magic_rejected) {
    const auto snap = make_snapshot();
    auto bytes = encode_snapshot(snap);
    bytes[0] = 0x00; bytes[1] = 0x00; bytes[2] = 0x00; bytes[3] = 0x00;
    CHECK(!decode_snapshot(bytes, nullptr).has_value());
}

WTEST(persistence_bad_version_rejected) {
    const auto snap = make_snapshot();
    auto bytes = encode_snapshot(snap);
    bytes[4] = 0xFF; // version
    CHECK(!decode_snapshot(bytes, nullptr).has_value());
}

WTEST(persistence_trailing_garbage_rejected) {
    const auto snap = make_snapshot();
    auto bytes = encode_snapshot(snap);
    bytes.push_back(0x42); bytes.push_back(0x43);
    std::string err;
    CHECK(!decode_snapshot(bytes, &err).has_value());
    CHECK(err.find("trailing") != std::string::npos);
}

WTEST(persistence_duplicate_id_rejected) {
    auto snap = make_snapshot();
    // Force a duplicate object id.
    WarmthObject dup = snap.objects[0];
    dup.set_warmth_generation(WarmthGeneration(7));
    snap.objects.push_back(dup);
    const auto bytes = encode_snapshot(snap);
    std::string err;
    CHECK(!decode_snapshot(bytes, &err).has_value());
    CHECK(err.find("duplicate") != std::string::npos);
}

WTEST(persistence_nan_rejected_encode) {
    auto snap = make_snapshot();
    snap.objects[0].set_confidence(std::numeric_limits<double>::quiet_NaN());
    bool threw = false;
    try { (void)encode_snapshot(snap); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
}

WTEST(persistence_invalid_generation_relation) {
    auto snap = make_snapshot();
    // Object belongs to a later warmth generation than the snapshot's.
    snap.warmth_generation = WarmthGeneration(2);
    auto bytes = encode_snapshot(snap);
    std::string err;
    CHECK(!decode_snapshot(bytes, &err).has_value());
    CHECK(err.find("generation") != std::string::npos);
}

WTEST(persistence_file_roundtrip) {
    const auto snap = make_snapshot();
    std::string err;
    const std::string path = "wtest_snapshot.wfbin";
    CHECK(save_snapshot_file(path, snap, &err));
    auto back = load_snapshot_file(path, &err);
    CHECK(back.has_value());
    CHECK_EQ(back->objects.size(), 2);
    std::remove(path.c_str());
}

#include <limits>
#include <cstdio>
int main() { RUN_TESTS(); }
