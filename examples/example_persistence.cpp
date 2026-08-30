#include <cstdio>
#include "warmth/fabric.hpp"
using namespace warmth;
int main() {
    WarmthFabric::Config cfg; cfg.workload = WorkloadId::derive("example"); cfg.node = NodeId::derive("node");
    WarmthFabric f(cfg);
    const auto id = f.register_object(WarmthCategory::MODEL_WEIGHTS, "model.weights");
    f.warm(id, WarmthAction::LOAD_MODEL);
    const std::string path = "example.wfbin";
    std::string err;
    if (!f.save(path, &err)) { std::printf("save failed: %s\n", err.c_str()); return 1; }
    std::printf("saved snapshot: %zu objects\n", f.object_count());
    WarmthFabric::Config cfg2; cfg2.workload = WorkloadId::derive("example"); cfg2.node = NodeId::derive("node");
    WarmthFabric f2(cfg2);
    if (!f2.recover(path, &err)) { std::printf("recover failed: %s\n", err.c_str()); return 1; }
    std::printf("recovered: %zu objects; live dims must be revalidated\n", f2.object_count());
    const auto o = f2.get(id);
    std::printf("recovered object state=%s ready=%s (ephemeral revalidation required)\n",
                o ? to_string(o->state()).data() : "absent", o && o->execution_ready() ? "yes":"no");
    std::remove(path.c_str());
    return 0;
}
