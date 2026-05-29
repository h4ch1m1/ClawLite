#pragma once
// ClawLite — 上下文压缩
// 参考：openclaw-main/src/agents/pi-embedded-runner/compact.ts
// 算法：当对话 token 数超过 budget 时，截断旧对话，保留最近 N 轮 + 摘要
//
// 简化实现（不依赖 LLM 调用）：
//   1. 计算当前对话的总 token 数
//   2. 如果超过 budget，从最旧的消息开始移除
//   3. 保留最近 keepTurns 轮对话
//   4. 在截断点插入一条摘要消息（简单拼接被移除消息的前 N 个字符）

#include "core/types.h"
#include <vector>
#include <string>

namespace clawlite {

struct CompactionConfig {
    int targetTokens = 4000;    // 压缩目标 token 数
    int keepTurns = 10;         // 至少保留的最近对话轮数
    int summaryMaxLen = 200;    // 摘要最大字符数
};

class Compactor {
public:
    // 压缩消息列表
    // 参考：openclaw-main/src/agents/pi-embedded-runner/compact.ts
    // 返回压缩后的消息列表
    static CompactResult compact(
        std::vector<Message>& messages,
        const CompactionConfig& config = {}
    );

    // Baseline for benchmarks: drop oldest messages until the token budget fits.
    static CompactResult truncateBaseline(
        std::vector<Message>& messages,
        int targetTokens
    );

    // 计算消息列表的总 token 数
    static int countTotalTokens(const std::vector<Message>& messages);

private:
    // 生成简单摘要（取被移除消息的前 N 个字符）
    static std::string generateSummary(
        const std::vector<Message>& removed,
        int maxLen
    );
};

} // namespace clawlite
