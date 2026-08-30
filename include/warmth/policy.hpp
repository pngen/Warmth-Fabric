#pragma once

#include "warmth/identity.hpp"
#include "warmth/decay.hpp"
#include "warmth/budget.hpp"

namespace warmth {

struct WarmthPolicy {
    PolicyGeneration generation;
    DecayPolicy decay;
    BudgetLimits budgets;

    double weight_predicted_reuse        = 1.0;
    double weight_arrival_probability    = 2.0;
    double weight_latency_class          = 1.5;
    double weight_priority               = 3.0;
    double weight_tenant_fairness        = 1.0;
    double weight_warming_cost           = -1.0;
    double weight_memory_cost            = -1.0;
    double weight_transfer_cost          = -1.5;
    double weight_eviction_risk          = -1.0;
    double weight_dependency_availability = 2.0;
    double weight_current_partial_warmth = 2.0;
    double weight_expected_benefit       = 2.5;

    static WarmthPolicy defaults(PolicyGeneration gen = PolicyGeneration::initial()) {
        WarmthPolicy p;
        p.generation = gen;
        return p;
    }
};

} // namespace warmth
