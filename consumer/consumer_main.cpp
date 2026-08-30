#include <cstdio>
#include "warmth/fabric.hpp"
using namespace warmth;
int main() {
    WarmthFabric::Config cfg; cfg.workload = WorkloadId::derive("consumer"); cfg.node = NodeId::derive("node");
    WarmthFabric f(cfg);
    const auto id = f.register_object(WarmthCategory::MODEL_WEIGHTS, "consumer.model");
    f.warm(id, WarmthAction::LOAD_MODEL);
    const auto o = f.get(id);
    std::printf("downstream consumer: state=%s ready=%s balanced=%s\n",
                o ? to_string(o->state()).data() : "absent", o && o->execution_ready() ? "yes":"no",
                f.budgets_balanced() ? "yes":"no");
    return 0;
}
