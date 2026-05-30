#include "memory/session_summary_tree.h"

#include <algorithm>
#include <sstream>

namespace clawlite {
namespace {

std::string rolePrefix(Role role) {
    switch (role) {
        case Role::User: return "U";
        case Role::Assistant: return "A";
        case Role::Tool: return "T";
        case Role::System: return "S";
    }
    return "?";
}

std::string summarizeRange(const std::vector<Message>& messages, int start, int end, size_t maxLen) {
    std::string out;
    for (int i = start; i < end && i < static_cast<int>(messages.size()); ++i) {
        if (!out.empty()) out += " | ";
        std::string content = messages[static_cast<size_t>(i)].content;
        if (content.size() > 80) content = content.substr(0, 80) + "...";
        out += rolePrefix(messages[static_cast<size_t>(i)].role) + ": " + content;
        if (out.size() >= maxLen) break;
    }
    if (out.size() > maxLen) out = out.substr(0, maxLen) + "...";
    return out;
}

int recentStartByTurns(const std::vector<Message>& messages, int keepRecentTurns) {
    if (keepRecentTurns <= 0) return static_cast<int>(messages.size());
    int turns = 0;
    for (int i = static_cast<int>(messages.size()) - 1; i >= 0; --i) {
        if (messages[static_cast<size_t>(i)].role == Role::User) {
            ++turns;
            if (turns >= keepRecentTurns) return i;
        }
    }
    return 0;
}

} // namespace

void SessionSummaryTree::rebuild(const std::vector<Message>& messages,
                                 int groupSize,
                                 int keepRecentTurns) {
    m_nodes.clear();
    m_recent.clear();
    if (messages.empty()) return;
    if (groupSize <= 0) groupSize = 6;

    int recentStart = recentStartByTurns(messages, keepRecentTurns);
    if (recentStart < static_cast<int>(messages.size())) {
        m_recent.assign(messages.begin() + recentStart, messages.end());
    }

    for (int start = 0; start < recentStart; start += groupSize) {
        int end = std::min(start + groupSize, recentStart);
        SummaryNode node;
        node.id = "summary:" + std::to_string(start) + "-" + std::to_string(end);
        node.startMessage = start;
        node.endMessage = end;
        node.depth = 1;
        node.summary = summarizeRange(messages, start, end, 360);
        node.tokenCost = estimateTokens(node.summary);
        m_nodes.push_back(std::move(node));
    }

    if (m_nodes.size() > 1) {
        SummaryNode root;
        root.id = "summary:root";
        root.startMessage = 0;
        root.endMessage = recentStart;
        root.depth = 0;
        root.summary = "Earlier conversation is compressed into " +
            std::to_string(m_nodes.size()) + " summary nodes.";
        root.tokenCost = estimateTokens(root.summary);
        for (int i = 0; i < static_cast<int>(m_nodes.size()); ++i) {
            root.children.push_back(i);
        }
        m_nodes.insert(m_nodes.begin(), std::move(root));
    }
}

std::vector<SummaryNode> SessionSummaryTree::selectSummaries(int tokenBudget) const {
    std::vector<SummaryNode> selected;
    int used = 0;
    for (const auto& node : m_nodes) {
        if (node.tokenCost > tokenBudget - used) continue;
        selected.push_back(node);
        used += node.tokenCost;
    }
    return selected;
}

std::string SessionSummaryTree::renderTree() const {
    std::ostringstream out;
    out << "summary nodes: " << m_nodes.size() << "\n";
    for (const auto& node : m_nodes) {
        out << std::string(static_cast<size_t>(node.depth) * 2, ' ')
            << "- " << node.id << " messages[" << node.startMessage
            << "," << node.endMessage << ") tokens=" << node.tokenCost << "\n";
    }
    out << "recent messages: " << m_recent.size() << "\n";
    return out.str();
}

} // namespace clawlite
