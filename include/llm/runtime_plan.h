#pragma once
// ClawLite — 运行时计划
// 参考：openclaw-main/src/agents/runtime-plan/types.ts — AgentRuntimePlan
// 设计模式：结构体组合模式 — 将所有运行时决策聚合到一个对象中
//
// AgentRuntimePlan 在每次 LLM 调用前构建，包含：
//   - 模型选择
//   - 提示词策略
//   - 工具配置
//   - 传输参数

#include <string>
#include <vector>

namespace clawlite {

// Think level 控制模型推理深度
// 参考：openclaw-main/src/agents/runtime-plan/types.ts:AgentRuntimeThinkLevel
enum class ThinkLevel {
    Off, Minimal, Low, Medium, High, Max
};

struct RuntimePlanModel {
    std::string provider;    // "openai", "anthropic", "local"
    std::string modelId;     // "gpt-4o-mini", "claude-3-5-sonnet"
};

struct RuntimePlanPrompt {
    ThinkLevel thinkLevel = ThinkLevel::Medium;
    std::string systemPromptOverride;  // 可选：覆盖自动生成的系统提示
    int contextTokenBudget = 12000;     // 给 Harness 组装历史和记忆的预算
};

struct RuntimePlanTransport {
    double temperature = 0.7;
    int maxTokens = 4096;
    int timeoutMs = 60000;
    int maxToolRounds = 10;            // 最大工具调用轮次（防止死循环）
};

// 运行时计划聚合体
// 参考：openclaw-main/src/agents/runtime-plan/types.ts:AgentRuntimePlan
struct RuntimePlan {
    RuntimePlanModel model;
    RuntimePlanPrompt prompt;
    RuntimePlanTransport transport;

    // 工厂方法：从配置创建默认计划
    static RuntimePlan defaultPlan();

    // 验证计划合法性
    bool validate() const;
};

} // namespace clawlite
