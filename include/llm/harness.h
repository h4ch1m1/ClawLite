#pragma once
// ClawLite — Agent 主循环（状态机）
// 参考：openclaw-main/src/agents/pi-embedded-runner/run/attempt.ts — Agent 主循环
//       openclaw-main/src/agents/harness/types.ts — AgentHarness 接口
// 数据结构：状态机（enum + switch 循环）
//          vector<Message> 对话历史
//
// 状态机状态转换：
//   IDLE → 构建消息 → 调用 LLM → 收到响应
//                                    ├── 有 tool_call → 执行工具 → 结果加入消息 → THINKING（再调用）
//                                    └── 纯文本 → RESPONDING（返回结果）
//   最多循环 maxToolRounds 次，超过则返回错误

#include "core/types.h"
#include "llm/llm_client.h"
#include "llm/runtime_plan.h"
#include "llm/tool_executor.h"
#include "memory/context_engine.h"
#include <vector>
#include <string>

namespace clawlite {

// Agent 循环状态
enum class AgentState {
    Idle,
    Thinking,      // 等待 LLM 响应
    ToolCalling,   // 执行工具调用
    Responding,    // 生成最终回复
    Done,          // 完成
    Error          // 出错
};

class AgentHarness {
public:
    AgentHarness(
        LlmClient& llm,
        ToolExecutor& tools,
        IContextEngine* memory = nullptr
    );

    // 执行一轮完整的 Agent 对话
    // 这是核心方法，实现状态机循环
    //
    // 参考：openclaw-main/src/agents/pi-embedded-runner/run/attempt.ts
    //
    // 算法：
    //   state = Idle
    //   messages = context（包含系统提示 + 历史 + 用户输入）
    //   while state != Done && state != Error:
    //     switch state:
    //       Idle:
    //         state = Thinking
    //       Thinking:
    //         response = llm.chat(messages, tools)
    //         if response has toolCalls:
    //           state = ToolCalling
    //         else:
    //           state = Responding
    //       ToolCalling:
    //         for each toolCall in response.toolCalls:
    //           result = tools.execute(toolCall)
    //           messages.append(tool_result)
    //         roundCount++
    //         if roundCount >= maxRounds:
    //           state = Error (max rounds exceeded)
    //         else:
    //           state = Thinking (继续循环)
    //       Responding:
    //         reply = response.content
    //         state = Done
    RunResult runTurn(
        const std::string& systemPrompt,
        const std::vector<Message>& history,
        const std::string& userInput,
        const RuntimePlan& plan = RuntimePlan::defaultPlan()
    );

    // 获取当前状态
    AgentState getState() const { return m_state; }
    const std::vector<Message>& messages() const { return m_messages; }
    void resetConversation();

private:
    LlmClient& m_llm;
    ToolExecutor& m_tools;
    IContextEngine* m_memory;
    AgentState m_state = AgentState::Idle;
    std::vector<Message> m_messages;
    std::string m_systemPrompt;
};

} // namespace clawlite
