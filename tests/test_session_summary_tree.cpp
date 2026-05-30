#include "memory/session_summary_tree.h"
#include "test_helpers.h"

#include <iostream>

using namespace clawlite;

void testSummaryTreeKeepsRecentMessages() {
    std::vector<Message> messages;
    for (int i = 0; i < 8; ++i) {
        messages.push_back(Message::user("question " + std::to_string(i)));
        messages.push_back(Message::assistant("answer " + std::to_string(i)));
    }

    SessionSummaryTree tree;
    tree.rebuild(messages, 4, 2);
    TEST_ASSERT(tree.summaryCount() > 0);
    auto recent = tree.recentMessages();
    TEST_ASSERT(!recent.empty());
    TEST_ASSERT(recent.front().content.find("question 6") != std::string::npos);

    auto summaries = tree.selectSummaries(1000);
    TEST_ASSERT(!summaries.empty());
    TEST_ASSERT(!tree.renderTree().empty());
    std::cout << "  [PASS] testSummaryTreeKeepsRecentMessages\n";
}

int run_session_summary_tree_tests() {
    std::cout << "SessionSummaryTree Tests:\n";
    RESET_FAILURES();
    testSummaryTreeKeepsRecentMessages();
    int f = GET_FAILURES();
    if (f == 0) std::cout << "All SessionSummaryTree tests passed.\n";
    else std::cout << f << " SessionSummaryTree test(s) failed.\n";
    return f;
}
