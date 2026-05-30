// ClawLite — 运行时计划实现
// TODO: C 同学实现

#include "llm/runtime_plan.h"

namespace clawlite {

RuntimePlan RuntimePlan::defaultPlan() {
    RuntimePlan plan;
    plan.model.provider = "mimo";
    plan.model.modelId = "mimo-v2.5-pro";
    plan.prompt.thinkLevel = ThinkLevel::Medium;
    plan.prompt.contextTokenBudget = 12000;
    plan.transport.temperature = 0.7;
    plan.transport.maxTokens = 4096;
    plan.transport.timeoutMs = 60000;
    plan.transport.maxToolRounds = 10;
    return plan;
}

bool RuntimePlan::validate() const {
    // TODO: 实现验证逻辑
    // 检查：
    //   - model.modelId 非空
    //   - temperature 在 [0, 2] 范围内
    //   - maxTokens > 0
    //   - maxToolRounds > 0
    return !model.modelId.empty()
        && transport.temperature >= 0
        && transport.temperature <= 2
        && transport.maxTokens > 0
        && transport.maxToolRounds > 0
        && prompt.contextTokenBudget > 0;
}

} // namespace clawlite
