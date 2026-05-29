#pragma once
// ClawLite — LLM HTTP 客户端
// 参考：openclaw-main/src/agents/pi-embedded-runner/run/attempt.ts
// 数据结构：
//   - vector<ChatMessage> 构建请求消息列表
//   - SSE 解析状态机（逐行读取流式响应）
//
// 使用 OpenAI 兼容 API 格式：
//   POST /v1/chat/completions
//   { "model": "...", "messages": [...], "tools": [...], "stream": true }

#include "core/types.h"
#include <string>
#include <vector>
#include <functional>

namespace clawlite {

struct LlmConfig {
    std::string baseUrl = "https://api.openai.com";  // API 基础 URL
    std::string apiKey;                                // API Key
    std::string model = "gpt-4o-mini";                 // 模型名
    double temperature = 0.7;
    int maxTokens = 4096;
    int timeoutMs = 60000;                             // 请求超时
    bool mockMode = false;                             // local demo path, no external API
};

// 流式回调：每收到一个 token 片段时调用
using StreamCallback = std::function<void(const std::string& token)>;

// LLM 响应
struct LlmResponse {
    std::string content;               // 助手回复文本
    std::vector<ToolCall> toolCalls;   // 工具调用（function calling）
    std::string finishReason;          // "stop", "tool_calls", "length"
    int promptTokens = 0;
    int completionTokens = 0;
    bool success = false;
    std::string error;
};

class LlmClient {
public:
    explicit LlmClient(const LlmConfig& config);

    // 同步调用（非流式）
    // 参考：OpenAI chat completions API
    LlmResponse chat(
        const std::vector<Message>& messages,
        const std::vector<Tool>& tools = {}
    );

    // 流式调用（SSE）
    // 参考：openclaw-main/src/agents/pi-embedded-runner/run/attempt.ts — SSE 解析
    // 数据结构：状态机解析 SSE 帧
    //   状态：READING_HEADER → READING_DATA → READING_DONE
    //   每行以 "data: " 开头是数据帧
    //   "data: [DONE]" 是终止信号
    LlmResponse chatStream(
        const std::vector<Message>& messages,
        const std::vector<Tool>& tools,
        StreamCallback onToken
    );

private:
    LlmConfig m_config;

    // 构建请求 JSON
    std::string buildRequestJson(
        const std::vector<Message>& messages,
        const std::vector<Tool>& tools,
        bool stream
    );

    // 解析响应 JSON
    LlmResponse parseResponse(const std::string& json);

    // 解析 SSE 流式响应
    // 参考：openclaw-main/src/agents/pi-embedded-runner/run/attempt.ts
    LlmResponse parseSSEStream(const std::string& rawResponse, StreamCallback onToken);
};

} // namespace clawlite
