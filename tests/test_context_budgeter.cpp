#include "memory/context_budgeter.h"
#include "test_helpers.h"

#include <iostream>

using namespace clawlite;

void testBudgeterUsesPriorityQueueAndBudget() {
    std::vector<ContextCandidate> candidates;
    candidates.push_back({"a", "chunk", "A", "small important", Role::System, 5, 0.9, 1});
    candidates.push_back({"b", "chunk", "B", std::string(200, 'b'), Role::System, 80, 0.2, 2});
    candidates.push_back({"c", "history", "C", "recent", Role::User, 5, 0.8, 30});

    auto selected = ContextBudgeter::select(candidates, 12);
    TEST_ASSERT(selected.tokensUsed <= 12);
    TEST_ASSERT(selected.selected.size() == 2);
    TEST_ASSERT(selected.selected[0].id == "c" || selected.selected[1].id == "c");
    TEST_ASSERT(selected.skipped == 1);
    std::cout << "  [PASS] testBudgeterUsesPriorityQueueAndBudget\n";
}

void testBudgeterDeduplicatesIds() {
    std::vector<ContextCandidate> candidates;
    candidates.push_back({"same", "chunk", "A", "one", Role::System, 2, 1.0, 1});
    candidates.push_back({"same", "chunk", "B", "two", Role::System, 2, 0.9, 2});
    auto selected = ContextBudgeter::select(candidates, 10);
    TEST_ASSERT(selected.selected.size() == 1);
    std::cout << "  [PASS] testBudgeterDeduplicatesIds\n";
}

int run_context_budgeter_tests() {
    std::cout << "ContextBudgeter Tests:\n";
    RESET_FAILURES();
    testBudgeterUsesPriorityQueueAndBudget();
    testBudgeterDeduplicatesIds();
    int f = GET_FAILURES();
    if (f == 0) std::cout << "All ContextBudgeter tests passed.\n";
    else std::cout << f << " ContextBudgeter test(s) failed.\n";
    return f;
}
