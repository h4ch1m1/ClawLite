#include "memory/context_budgeter.h"

#include <queue>
#include <unordered_set>

namespace clawlite {
namespace {

double priorityOf(const ContextCandidate& candidate) {
    int cost = candidate.tokenCost > 0 ? candidate.tokenCost : estimateTokens(candidate.text);
    return (candidate.importance * 1000.0 + static_cast<double>(candidate.recency)) /
           static_cast<double>(cost > 0 ? cost : 1);
}

} // namespace

BudgetedContext ContextBudgeter::select(std::vector<ContextCandidate> candidates, int tokenBudget) {
    BudgetedContext result;
    if (tokenBudget <= 0) {
        result.skipped = static_cast<int>(candidates.size());
        return result;
    }

    auto worseFirst = [](const ContextCandidate& a, const ContextCandidate& b) {
        return priorityOf(a) < priorityOf(b);
    };
    std::priority_queue<ContextCandidate, std::vector<ContextCandidate>, decltype(worseFirst)> queue(worseFirst);
    for (auto& candidate : candidates) {
        if (candidate.tokenCost <= 0) candidate.tokenCost = estimateTokens(candidate.text);
        queue.push(candidate);
    }

    std::unordered_set<std::string> used;
    while (!queue.empty()) {
        auto candidate = queue.top();
        queue.pop();
        if (!candidate.id.empty() && used.find(candidate.id) != used.end()) {
            result.skipped++;
            continue;
        }
        if (candidate.tokenCost > tokenBudget - result.tokensUsed) {
            result.skipped++;
            continue;
        }
        if (!candidate.id.empty()) used.insert(candidate.id);
        result.tokensUsed += candidate.tokenCost;
        result.selected.push_back(std::move(candidate));
    }
    return result;
}

} // namespace clawlite
