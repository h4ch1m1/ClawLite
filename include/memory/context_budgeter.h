#pragma once

#include "core/types.h"
#include <string>
#include <vector>

namespace clawlite {

struct ContextCandidate {
    std::string id;
    std::string source;
    std::string label;
    std::string text;
    Role role = Role::System;
    int tokenCost = 0;
    double importance = 0.0;
    int recency = 0;
};

struct BudgetedContext {
    std::vector<ContextCandidate> selected;
    int tokensUsed = 0;
    int skipped = 0;
};

class ContextBudgeter {
public:
    static BudgetedContext select(std::vector<ContextCandidate> candidates, int tokenBudget);
};

} // namespace clawlite
