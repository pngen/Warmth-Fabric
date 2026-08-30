#include <cstdio>
#include "warmth/fabric.hpp"
using namespace warmth;
int main() {
    WarmthFabric::Config cfg; cfg.workload = WorkloadId::derive("example"); cfg.node = NodeId::derive("node");
    WarmthFabric f(cfg);
    const auto id = f.register_object(WarmthCategory::MODEL_WEIGHTS, "model.weights");
    std::printf("COLD: state=%s ready=%s\n", to_string(f.get(id)->state()).data(), f.get(id)->execution_ready() ? "yes":"no");
    double ms=0; std::uint64_t bytes=0;
    f.warm(id, WarmthAction::LOAD_MODEL, &ms, &bytes);
    std::printf("WARM: state=%s ready=%s measured=%gms\n", to_string(f.get(id)->state()).data(), f.get(id)->execution_ready()?"yes":"no", ms);
    f.invalidate(id, InvalidationReason::MODEL_REVISION_CHANGE);
    std::printf("INVALIDATED: state=%s\n", to_string(f.get(id)->state()).data());
    f.warm(id, WarmthAction::INIT_ENGINE, &ms, &bytes);
    std::printf("REWARMED: state=%s ready=%s balanced=%s\n", to_string(f.get(id)->state()).data(), f.get(id)->execution_ready()?"yes":"no", f.budgets_balanced()?"yes":"no");
    return 0;
}
