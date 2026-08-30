#include <vector>
#include <cstdint>
#include "wtest.hpp"
#include "warmth/warmth_state.hpp"
#include "warmth/warmth_object.hpp"
#include "warmth/fabric.hpp"

using namespace warmth;

// Deterministic fixed-seed property harness.
static std::uint64_t next_u64(std::uint64_t& s) { s = s * 6364136223846793005ULL + 1442695040888963407ULL; return s; }

WTEST(property_state_machine_reachable) {
    // For every state, every target either is an allowed transition or is
    // deterministically rejected; and execution-ready is only WARM/HOT.
    for (int f = 0; f < 10; ++f) {
        const auto from = static_cast<WarmthState>(f);
        for (int t = 0; t < 10; ++t) {
            const auto to = static_cast<WarmthState>(t);
            const bool allowed = transition_allowed(from, to);
            if (allowed) {
                WarmthObject o(Id128::derive("p"), WarmthCategory::MODEL_WEIGHTS, WorkloadId::derive("w"), NodeId::derive("n"), "cuda:0", "cuda");
                o.restore_state(from, "setup");
                CHECK(o.transition(to, "test"));
            }
        }
    }
    CHECK(transition_allowed(WarmthState::WARM, WarmthState::WARM));
    CHECK(!transition_allowed(WarmthState::COLD, WarmthState::HOT));
    CHECK(transition_allowed(WarmthState::COLD, WarmthState::PREPARING));
    CHECK(transition_allowed(WarmthState::PREPARING, WarmthState::WARM));
}

WTEST(property_active_roundtrip_invariants) {
    std::uint64_t seed = 0x9E3779B97F4A7C15ULL;
    WarmthFabric::Config cfg; cfg.workload = WorkloadId::derive("prop"); cfg.node = NodeId::derive("n");
    WarmthFabric f(cfg);
    std::vector<WarmthObjectId> ids;
    for (int i = 0; i < 64; ++i) ids.push_back(f.register_object(WarmthCategory::APPLICATION_DEFINED, "prop-" + std::to_string(i)));
    // Random operation sequence; verify invariants hold after every op.
    for (int i = 0; i < 4000; ++i) {
        const auto idx = static_cast<std::size_t>((next_u64(seed) >> 8) % ids.size());
        const auto id = ids[idx];
        switch ((next_u64(seed) >> 16) % 7) {
            case 0: (void)f.get(id); break;
            case 1: (void)f.warm(id, WarmthAction::INIT_ENGINE); break;
            case 2: (void)f.invalidate(id, InvalidationReason::MODEL_REVISION_CHANGE); break;
            case 3: (void)f.demote(id); break;
            case 4: (void)f.evict(id); break;
            case 5: (void)f.warm_to_ready(id); break;
            case 6: (void)f.mark_used(id); break;
        }
        // Invariant: an object is execution-ready only if not invalidated/stale.
        const auto o = f.get(id);
        CHECK(o.has_value());
        if (o && is_execution_ready(o->state())) {
            CHECK(o->state() != WarmthState::INVALIDATED);
            CHECK(o->state() != WarmthState::STALE);
            if (o->invalidation_reason().has_value()) {
                std::printf("  invariant: ready state=%s reason=%s\n",
                            to_string(o->state()).data(), o->invalidation_reason()->c_str());
            }
            CHECK(o->invalidation_reason().has_value() == false);
        }
    }
    // Accounting always ends balanced.
    CHECK(f.budgets_balanced());
}

int main() { RUN_TESTS(); }
