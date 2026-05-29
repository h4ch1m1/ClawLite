#include "memory/compaction.h"
#include "test_helpers.h"
#include <iostream>

using namespace clawlite;

void testTruncateBaseline() {
    std::vector<Message> messages;
    for (int i = 0; i < 20; ++i) {
        messages.push_back(Message::user(std::string(200, 'x')));
    }
    int before = Compactor::countTotalTokens(messages);
    auto result = Compactor::truncateBaseline(messages, before / 2);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(result.compacted);
    TEST_ASSERT(Compactor::countTotalTokens(messages) <= before / 2);
    std::cout << "  [PASS] testTruncateBaseline\n";
}

void testGreedyCompaction() {
    std::vector<Message> messages;
    for (int i = 0; i < 12; ++i) {
        messages.push_back(Message::user("question " + std::to_string(i) + std::string(120, 'q')));
        messages.push_back(Message::assistant("answer " + std::to_string(i) + std::string(120, 'a')));
    }
    CompactionConfig config;
    config.targetTokens = 200;
    config.keepTurns = 2;
    auto result = Compactor::compact(messages, config);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(result.compacted);
    TEST_ASSERT(!result.summary.empty());
    std::cout << "  [PASS] testGreedyCompaction\n";
}

int run_compaction_tests() {
    std::cout << "Compaction Tests:\n";
    RESET_FAILURES();
    testTruncateBaseline();
    testGreedyCompaction();
    int f = GET_FAILURES();
    if (f == 0) std::cout << "All compaction tests passed.\n";
    else std::cout << f << " compaction test(s) failed.\n";
    return f;
}
