#include <cstdio>
#include "warmth/fabric.hpp"
#include "warmth/explanation.hpp"
using namespace warmth;
int main() {
    WarmthFabric::Config cfg; cfg.workload = WorkloadId::derive("example"); cfg.node = NodeId::derive("node");
    WarmthFabric f(cfg);
    const auto id = f.register_object(WarmthCategory::MODEL_WEIGHTS, "model.weights");
    WarmthDimensions d; d.model_residency = DimensionStatus::PARTIAL; d.artifact_availability = DimensionStatus::VALID;
    d.artifact_validation = DimensionStatus::VALID; d.local_dependency_readiness = DimensionStatus::VALID;
    f.set_dimensions(id, d);
    auto e = f.explain(id);
    std::printf("====================\n");
    std::printf("%s", readiness_to_text(e).c_str());
    std::printf("====================\n");
    auto plan = f.explain_plan();
    std::printf("%s", plan_to_text(plan).c_str());
    return 0;
}
