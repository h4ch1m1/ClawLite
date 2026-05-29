#include "llm/harness.h"

#include <sstream>

namespace clawlite {
namespace {

std::string formatToolResult(const ToolResult& result) {
    if (result.success) return result.output;
    return std::string("Tool error: ") + result.error;
}

} // namespace

AgentHarness::AgentHarness(
    LlmClient& llm,
    ToolExecutor& tools,
    IContextEngine* memory
) : m_llm(llm), m_tools(tools), m_memory(memory) {}

void AgentHarness::resetConversation() {
    m_messages.clear();
    m_systemPrompt.clear();
    m_state = AgentState::Idle;
}

RunResult AgentHarness::runTurn(
    const std::string& systemPrompt,
    const std::vector<Message>& history,
    const std::string& userInput,
    const RuntimePlan& plan
) {
    RunResult result;
    if (!plan.validate()) {
        result.status = RunStatus::Error;
        result.error = "invalid runtime plan";
        return result;
    }

    m_state = AgentState::Idle;

    std::string effectiveSystemPrompt = plan.prompt.systemPromptOverride.empty()
        ? systemPrompt
        : plan.prompt.systemPromptOverride;
    if (m_messages.empty() || effectiveSystemPrompt != m_systemPrompt || !history.empty()) {
        m_messages.clear();
        m_systemPrompt = effectiveSystemPrompt;
        if (!effectiveSystemPrompt.empty()) {
            m_messages.push_back(Message::system(effectiveSystemPrompt));
        }
        m_messages.insert(m_messages.end(), history.begin(), history.end());
    }
    Message userMsg = Message::user(userInput);
    m_messages.push_back(userMsg);

    if (m_memory) {
        m_memory->ingest(userMsg);
    }

    int roundCount = 0;
    int toolCallCount = 0;
    int totalTokens = 0;
    LlmResponse lastResponse;

    while (m_state != AgentState::Done && m_state != AgentState::Error) {
        switch (m_state) {
            case AgentState::Idle:
                m_state = AgentState::Thinking;
                break;

            case AgentState::Thinking: {
                lastResponse = m_llm.chat(m_messages, m_tools.getAllTools());
                if (!lastResponse.success) {
                    result.status = RunStatus::Error;
                    result.error = lastResponse.error;
                    m_state = AgentState::Error;
                    break;
                }

                totalTokens += lastResponse.promptTokens + lastResponse.completionTokens;

                Message assistantMsg = Message::assistant(lastResponse.content);
                assistantMsg.toolCalls = lastResponse.toolCalls;
                m_messages.push_back(assistantMsg);
                if (m_memory) {
                    m_memory->ingest(assistantMsg);
                }

                if (!lastResponse.toolCalls.empty()) {
                    m_state = AgentState::ToolCalling;
                } else {
                    result.reply = lastResponse.content;
                    m_state = AgentState::Responding;
                }
                break;
            }

            case AgentState::ToolCalling: {
                if (roundCount >= plan.transport.maxToolRounds) {
                    result.status = RunStatus::MaxTurnsExceeded;
                    result.error = "max tool rounds exceeded";
                    m_state = AgentState::Error;
                    break;
                }
                ++roundCount;

                const auto toolCalls = m_messages.back().toolCalls;
                toolCallCount += static_cast<int>(toolCalls.size());
                for (const auto& tc : toolCalls) {
                    ToolResult toolResult = m_tools.execute(tc);
                    Message toolMsg = Message::toolResult(
                        tc.id,
                        tc.name,
                        formatToolResult(toolResult)
                    );
                    m_messages.push_back(toolMsg);
                    if (m_memory) {
                        m_memory->ingest(toolMsg);
                    }
                }

                m_state = AgentState::Thinking;
                break;
            }

            case AgentState::Responding:
                result.status = RunStatus::Success;
                result.totalTurns = toolCallCount;
                result.totalTokens = totalTokens;
                m_state = AgentState::Done;
                break;

            case AgentState::Done:
            case AgentState::Error:
                break;
        }
    }

    if (m_state == AgentState::Error && result.error.empty()) {
        result.status = RunStatus::Error;
        result.error = "agent harness entered error state";
    }

    result.totalTurns = toolCallCount;
    result.totalTurns = toolCallCount;
    result.totalTokens = totalTokens;
    m_state = AgentState::Idle;
    return result;
}

} // namespace clawlite
