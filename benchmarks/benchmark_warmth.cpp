#include <cstdio>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include "warmth/fabric.hpp"
#include "warmth/persistence.hpp"
using namespace warmth;
static double ms(std::chrono::steady_clock::time_point a) { return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-a).count(); }
int main() {
    WarmthFabric::Config cfg; cfg.workload = WorkloadId::derive("bench"); cfg.node = NodeId::derive("n");
    const int N = 20000;
    auto t0 = std::chrono::steady_clock::now();
    WarmthFabric f(cfg);
    std::vector<WarmthObjectId> ids;
    for (int i=0;i<N;++i) ids.push_back(f.register_object(WarmthCategory::APPLICATION_DEFINED, "o"+std::to_string(i)));
    std::printf("fabric creation: %d ops in %.2f ms\n", N, ms(t0));
    t0 = std::chrono::steady_clock::now();
    std::size_t sum=0;
    for (int r=0;r<5;++r) for (const auto& id: ids) sum += f.get(id)?1:0;
    std::printf("readiness queries: %d ops in %.2f ms\n", 5*N, ms(t0));
    t0 = std::chrono::steady_clock::now();
    for (const auto& id : ids) { (void)id; (void)f.plan(); }
    std::printf("plan generation: %d ops in %.2f ms\n", N, ms(t0));
    t0 = std::chrono::steady_clock::now();
    for (const auto& id : ids) (void)f.explain(id);
    std::printf("explanation: %d ops in %.2f ms\n", N, ms(t0));
    // 1-thread mutation
    t0 = std::chrono::steady_clock::now();
    for (int r=0;r<3;++r) for (const auto& id : ids) (void)f.mark_used(id);
    std::printf("1-thread mutation: %d ops in %.2f ms\n", 3*N, ms(t0));
    // multi-thread mutation
    for (int T : {4, 8}) {
        std::atomic<bool> go{false}; std::atomic<bool> done{false};
        std::vector<std::thread> ths;
        t0 = std::chrono::steady_clock::now();
        for (int t=0;t<T;++t) ths.emplace_back([&,t]{ while(!go.load()) std::this_thread::yield();
            for (int r=0;r<3;++r) for (const auto& id : ids) (void)f.get(id); done.store(true); });
        go.store(true);
        for (auto& th : ths) th.join();
        std::printf("%d-thread mutation: %d ops in %.2f ms\n", T, 3*N, ms(t0));
    }
    const auto snap = f.snapshot();
    t0 = std::chrono::steady_clock::now();
    const auto bytes = encode_snapshot(snap);
    std::printf("persistence encode: %zu objects (%zu bytes) in %.2f ms\n", snap.objects.size(), bytes.size(), ms(t0));
    t0 = std::chrono::steady_clock::now();
    auto back = decode_snapshot(bytes, nullptr);
    std::printf("persistence decode: %zu objects in %.2f ms, valid=%d\n", back?back->objects.size():0, ms(t0), back?1:0);
    (void)sum;
    return 0;
}
