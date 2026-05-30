#pragma once

#include "core/types.h"
#include <functional>
#include <string>
#include <vector>

namespace clawlite {

struct LlmConfig {
    std::string baseUrl = "https://api.mimo-v2.com/v1";
    std::string apiKey;
    std::string model = "mimo-v2.5-pro";
    double temperature = 0.7;
    int maxTokens = 4096;
    int timeoutMs = 60000;
    std::string maxTokensField = "max_completion_tokens";
    bool sendApiKeyHeader = true;
};

using StreamCallback = std::function<void(const std::string& token)>;

struct LlmResponse {
    std::string content;
    std::vector<ToolCall> toolCalls;
    std::string finishReason;
    int promptTokens = 0;
    int completionTokens = 0;
    bool success = false;
    std::string error;
};

class LlmClient {
public:
    explicit LlmClient(const LlmConfig& config);

    LlmResponse chat(
        const std::vector<Message>& messages,
        const std::vector<Tool>& tools = {}
    );

    LlmResponse chatStream(
        const std::vector<Message>& messages,
        const std::vector<Tool>& tools,
        StreamCallback onToken
    );

private:
    LlmConfig m_config;

    std::string buildRequestJson(
        const std::vector<Message>& messages,
        const std::vector<Tool>& tools,
        bool stream
    );

    LlmResponse parseResponse(const std::string& json);
    LlmResponse parseSSEStream(const std::string& rawResponse, StreamCallback onToken);
};

} // namespace clawlite
