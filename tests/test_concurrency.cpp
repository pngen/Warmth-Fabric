#include <thread>
#include <atomic>
#include <vector>
#include "wtest.hpp"
#include "warmth/fabric.hpp"

using namespace warmth;

WTEST(concurrency_high_contention) {
    WarmthFabric::Config cfg; cfg.workload = WorkloadId::derive("cc"); cfg.node = NodeId::derive("n");
    WarmthFabric f(cfg);
    const int N = 32;
    std::vector<WarmthObjectId> ids;
    for (int i = 0; i < N; ++i) ids.push_back(f.register_object(WarmthCategory::MODEL_WEIGHTS, "cc-" + std::to_string(i)));
    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};
    const int T = 8;
    std::vector<std::thread> threads;
    for (int t = 0; t < T; ++t) {
        threads.emplace_back([&, t]() {
            while (!start.load()) std::this_thread::yield();
            unsigned seed = 1234567u + t;
            while (!stop.load()) {
                seed = seed * 1103515245u + 12345u;
                const int idx = static_cast<int>((seed >> 8) % N);
                const auto id = ids[static_cast<std::size_t>(idx)];
                switch ((seed >> 16) % 6) {
                    case 0: (void)f.get(id); break;
                    case 1: (void)f.warm(id, WarmthAction::INIT_ENGINE); break;
                    case 2: (void)f.invalidate(id, InvalidationReason::MODEL_REVISION_CHANGE); break;
                    case 3: (void)f.demote(id); break;
                    case 4: (void)f.evict(id); break;
                    case 5: (void)f.mark_used(id); break;
                }
            }
        });
    }
    start.store(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    stop.store(true);
    for (auto& th : threads) th.join();
    // No leak: all reservations released for completed operations.
    CHECK(f.budgets_balanced());
    // All objects still present.
    CHECK_EQ(f.object_count(), static_cast<std::size_t>(N));
}

int main() { RUN_TESTS(); }
