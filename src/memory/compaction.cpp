// ClawLite — 上下文压缩实现
// TODO: B 同学实现

#include "memory/compaction.h"
#include "core/types.h"
#include <algorithm>

namespace clawlite {

CompactResult Compactor::compact(
    std::vector<Message>& messages,
    const CompactionConfig& config
) {
    // TODO: 实现上下文压缩
    //
    // 参考：openclaw-main/src/agents/pi-embedded-runner/compact.ts
    //
    // 算法：
    //   1. 计算总 token 数
    //   2. 如果 <= targetTokens，返回不需压缩
    //   3. 保留最近 keepTurns 轮（从末尾往前数）
    //   4. 被移除的消息生成摘要
    //   5. 在保留消息前插入摘要消息
    //   6. 更新 messages 向量
    //
    // 简化实现（不调用 LLM）：
    //   摘要 = 取被移除消息的前 summaryMaxLen 个字符

    int totalTokens = countTotalTokens(messages);
    if (totalTokens <= config.targetTokens) {
        return {true, false, "no compaction needed", "", totalTokens, totalTokens};
    }

    // 找到保留点：从末尾往前数 keepTurns 轮
    int keepFrom = (int)messages.size();
    int turnCount = 0;
    for (int i = (int)messages.size() - 1; i >= 0; i--) {
        if (messages[i].role == Role::User) {
            turnCount++;
            if (turnCount >= config.keepTurns) {
                keepFrom = i;
                break;
            }
        }
    }

    if (keepFrom <= 0) {
        return {true, false, "nothing to compact", "", totalTokens, totalTokens};
    }

    // 生成摘要
    std::vector<Message> removed(messages.begin(), messages.begin() + keepFrom);
    std::string summary = generateSummary(removed, config.summaryMaxLen);

    // 构造压缩后的消息列表
    std::vector<Message> compacted;
    compacted.push_back(Message::system("[Summary of earlier conversation]: " + summary));
    compacted.insert(compacted.end(), messages.begin() + keepFrom, messages.end());

    int afterTokens = countTotalTokens(compacted);

    messages = std::move(compacted);

    return {true, true, "compacted", summary, totalTokens, afterTokens};
}

CompactResult Compactor::truncateBaseline(
    std::vector<Message>& messages,
    int targetTokens
) {
    int before = countTotalTokens(messages);
    if (before <= targetTokens) {
        return {true, false, "baseline no truncation needed", "", before, before};
    }

    while (!messages.empty() && countTotalTokens(messages) > targetTokens) {
        messages.erase(messages.begin());
    }

    int after = countTotalTokens(messages);
    return {true, true, "baseline truncated oldest messages", "", before, after};
}

int Compactor::countTotalTokens(const std::vector<Message>& messages) {
    int total = 0;
    for (const auto& msg : messages) {
        total += estimateTokens(msg.content);
    }
    return total;
}

std::string Compactor::generateSummary(const std::vector<Message>& removed, int maxLen) {
    // 简单摘要：拼接被移除消息的前 maxLen 个字符
    std::string summary;
    for (const auto& msg : removed) {
        if (msg.role == Role::User || msg.role == Role::Assistant) {
            if (!summary.empty()) summary += " | ";
            summary += msg.content;
        }
    }
    if ((int)summary.size() > maxLen) {
        summary = summary.substr(0, maxLen) + "...";
    }
    return summary;
}

} // namespace clawlite
