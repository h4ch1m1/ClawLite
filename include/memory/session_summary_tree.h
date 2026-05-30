#pragma once

#include "core/types.h"
#include <string>
#include <vector>

namespace clawlite {

struct SummaryNode {
    std::string id;
    std::string summary;
    int startMessage = 0;
    int endMessage = 0;
    int depth = 0;
    int tokenCost = 0;
    std::vector<int> children;
};

class SessionSummaryTree {
public:
    void rebuild(const std::vector<Message>& messages, int groupSize = 6, int keepRecentTurns = 4);

    std::vector<SummaryNode> selectSummaries(int tokenBudget) const;
    std::vector<Message> recentMessages() const { return m_recent; }
    int summaryCount() const { return static_cast<int>(m_nodes.size()); }
    std::string renderTree() const;

private:
    std::vector<SummaryNode> m_nodes;
    std::vector<Message> m_recent;
};

} // namespace clawlite
