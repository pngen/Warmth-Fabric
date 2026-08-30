#include <cstdio>
#include <vector>
#include "warmth/fabric.hpp"
using namespace warmth;
int main() {
    WarmthFabric::Config cfg; cfg.workload = WorkloadId::derive("example"); cfg.node = NodeId::derive("node");
    cfg.policy.budgets.device_memory_bytes = 1ULL << 30; // 1 GiB budget
    cfg.policy.budgets.concurrent_warming_ops = 1;
    WarmthFabric f(cfg);
    const auto a = f.register_object(WarmthCategory::MODEL_WEIGHTS, "a.model");
    const auto b = f.register_object(WarmthCategory::MODEL_WEIGHTS, "b.model");
    double ms=0; std::uint64_t bytes=0;
    const bool wa = f.warm(a, WarmthAction::LOAD_MODEL, &ms, &bytes);
    std::printf("warm a: %s (budget usage=%llu)\n", wa?"ok":"infeasible", (unsigned long long)f.budget_usage(WarmthBudgetKind::DEVICE_MEMORY));
    const bool wb = f.warm(b, WarmthAction::LOAD_MODEL, &ms, &bytes);
    std::printf("warm b: %s (budget usage=%llu)\n", wb?"ok":"infeasible", (unsigned long long)f.budget_usage(WarmthBudgetKind::DEVICE_MEMORY));
    // Explain the warming plan (priority aware).
    auto plan = f.explain_plan();
    std::printf("%s", plan_to_text(plan).c_str());
    std::printf("balanced after all=%s\n", f.budgets_balanced()?"yes":"no");
    return 0;
}
