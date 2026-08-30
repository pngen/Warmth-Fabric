#include <cstdio>
#include "warmth/fabric.hpp"
using namespace warmth;
int main() {
    WarmthFabric::Config cfg; cfg.workload = WorkloadId::derive("example"); cfg.node = NodeId::derive("node");
    WarmthFabric f(cfg);
    const auto id = f.register_object(WarmthCategory::MODEL_WEIGHTS, "model.weights");
    f.warm(id, WarmthAction::LOAD_MODEL);
    std::printf("setup: state=%s\n", to_string(f.get(id)->state()).data());
    // Trigger dependency-generation change -> warmth must decay to STALE.
    f.decay(now_ms(), false, true, false, false);
    std::printf("after dep change: state=%s ready=%s\n", to_string(f.get(id)->state()).data(), f.get(id)->execution_ready()?"yes":"no");
    // Idle decay.
    f.decay(now_ms() + 3*3600*1000, false, false, false, false);
    std::printf("after idle: state=%s\n", to_string(f.get(id)->state()).data());
    // Memory pressure.
    f.warm(id, WarmthAction::INIT_ENGINE);
    std::printf("rewarmed: state=%s\n", to_string(f.get(id)->state()).data());
    f.decay(now_ms(), true, false, false, false);
    std::printf("after memory pressure: state=%s\n", to_string(f.get(id)->state()).data());
    return 0;
}
