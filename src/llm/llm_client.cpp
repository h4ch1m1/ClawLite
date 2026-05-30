#include "llm/llm_client.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#endif

namespace clawlite {
namespace {

struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolValue = false;
    double numberValue = 0.0;
    std::string stringValue;
    std::vector<Json> arrayValue;
    std::map<std::string, Json> objectValue;

    bool isObject() const { return type == Type::Object; }
    bool isArray() const { return type == Type::Array; }

    const Json& at(const std::string& key) const {
        static const Json nullJson;
        if (!isObject()) return nullJson;
        auto it = objectValue.find(key);
        return it == objectValue.end() ? nullJson : it->second;
    }

    std::string asString(const std::string& defaultValue = "") const {
        if (type == Type::String) return stringValue;
        if (type == Type::Number) {
            std::ostringstream oss;
            oss << numberValue;
            return oss.str();
        }
        if (type == Type::Bool) return boolValue ? "true" : "false";
        return defaultValue;
    }

    int asInt(int defaultValue = 0) const {
        if (type == Type::Number) return static_cast<int>(numberValue);
        return defaultValue;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : m_text(text) {}

    Json parse() {
        skipWhitespace();
        Json value = parseValue();
        skipWhitespace();
        return value;
    }

private:
    const std::string& m_text;
    size_t m_pos = 0;

    void skipWhitespace() {
        while (m_pos < m_text.size()) {
            char c = m_text[m_pos];
            if (c != ' ' && c != '\n' && c != '\r' && c != '\t') break;
            ++m_pos;
        }
    }

    bool consume(char expected) {
        skipWhitespace();
        if (m_pos < m_text.size() && m_text[m_pos] == expected) {
            ++m_pos;
            return true;
        }
        return false;
    }

    Json parseValue() {
        skipWhitespace();
        if (m_pos >= m_text.size()) throw std::runtime_error("unexpected end of json");
        char c = m_text[m_pos];
        if (c == '"') return parseString();
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
        if (m_text.compare(m_pos, 4, "true") == 0) {
            m_pos += 4;
            Json v;
            v.type = Json::Type::Bool;
            v.boolValue = true;
            return v;
        }
        if (m_text.compare(m_pos, 5, "false") == 0) {
            m_pos += 5;
            Json v;
            v.type = Json::Type::Bool;
            return v;
        }
        if (m_text.compare(m_pos, 4, "null") == 0) {
            m_pos += 4;
            return Json{};
        }
        throw std::runtime_error("invalid json value");
    }

    Json parseString() {
        if (!consume('"')) throw std::runtime_error("expected string");
        Json v;
        v.type = Json::Type::String;
        while (m_pos < m_text.size()) {
            char c = m_text[m_pos++];
            if (c == '"') return v;
            if (c == '\\') {
                if (m_pos >= m_text.size()) break;
                char e = m_text[m_pos++];
                switch (e) {
                    case '"': v.stringValue += '"'; break;
                    case '\\': v.stringValue += '\\'; break;
                    case '/': v.stringValue += '/'; break;
                    case 'b': v.stringValue += '\b'; break;
                    case 'f': v.stringValue += '\f'; break;
                    case 'n': v.stringValue += '\n'; break;
                    case 'r': v.stringValue += '\r'; break;
                    case 't': v.stringValue += '\t'; break;
                    case 'u':
                        v.stringValue += "\\u";
                        for (int i = 0; i < 4 && m_pos < m_text.size(); ++i) {
                            v.stringValue += m_text[m_pos++];
                        }
                        break;
                    default:
                        v.stringValue += e;
                }
            } else {
                v.stringValue += c;
            }
        }
        throw std::runtime_error("unterminated json string");
    }

    Json parseNumber() {
        size_t start = m_pos;
        if (m_text[m_pos] == '-') ++m_pos;
        while (m_pos < m_text.size() && std::isdigit(static_cast<unsigned char>(m_text[m_pos]))) ++m_pos;
        if (m_pos < m_text.size() && m_text[m_pos] == '.') {
            ++m_pos;
            while (m_pos < m_text.size() && std::isdigit(static_cast<unsigned char>(m_text[m_pos]))) ++m_pos;
        }
        if (m_pos < m_text.size() && (m_text[m_pos] == 'e' || m_text[m_pos] == 'E')) {
            ++m_pos;
            if (m_pos < m_text.size() && (m_text[m_pos] == '+' || m_text[m_pos] == '-')) ++m_pos;
            while (m_pos < m_text.size() && std::isdigit(static_cast<unsigned char>(m_text[m_pos]))) ++m_pos;
        }
        Json v;
        v.type = Json::Type::Number;
        v.numberValue = std::stod(m_text.substr(start, m_pos - start));
        return v;
    }

    Json parseArray() {
        consume('[');
        Json v;
        v.type = Json::Type::Array;
        skipWhitespace();
        if (consume(']')) return v;
        while (true) {
            v.arrayValue.push_back(parseValue());
            skipWhitespace();
            if (consume(']')) return v;
            if (!consume(',')) throw std::runtime_error("expected ',' in array");
        }
    }

    Json parseObject() {
        consume('{');
        Json v;
        v.type = Json::Type::Object;
        skipWhitespace();
        if (consume('}')) return v;
        while (true) {
            Json key = parseString();
            if (!consume(':')) throw std::runtime_error("expected ':' in object");
            v.objectValue[key.stringValue] = parseValue();
            skipWhitespace();
            if (consume('}')) return v;
            if (!consume(',')) throw std::runtime_error("expected ',' in object");
        }
    }
};

std::string jsonEscape(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 16);
    for (char c : input) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

std::string trimTrailingSlash(std::string url) {
    while (!url.empty() && url.back() == '/') url.pop_back();
    return url;
}

std::string chatCompletionsEndpoint(const std::string& baseUrl) {
    std::string base = trimTrailingSlash(baseUrl);
    if (base.size() >= 3 && base.substr(base.size() - 3) == "/v1") {
        return base + "/chat/completions";
    }
    return base + "/v1/chat/completions";
}

std::string quoteShellArg(const std::string& value) {
#ifdef _WIN32
    std::string out = "\"";
    for (char c : value) {
        if (c == '"') out += "\\\"";
        else out += c;
    }
    out += "\"";
    return out;
#else
    std::string out = "'";
    for (char c : value) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
#endif
}

std::string runCommandCapture(const std::string& command, int& exitCode) {
#ifdef _WIN32
    FILE* pipe = _popen(command.c_str(), "r");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif
    if (!pipe) {
        exitCode = -1;
        return "";
    }

    std::string output;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        output += buffer;
    }

#ifdef _WIN32
    exitCode = _pclose(pipe);
#else
    exitCode = pclose(pipe);
#endif
    return output;
}

std::string makeTempPath(const std::string& suffix) {
#ifdef _WIN32
    char tempDir[MAX_PATH + 1];
    DWORD len = GetTempPathA(MAX_PATH, tempDir);
    std::string base = (len > 0) ? std::string(tempDir, len) : ".\\";
    return base + "clawlite_llm_" + std::to_string(nowMs()) + "_" + suffix;
#else
    return "/tmp/clawlite_llm_" + std::to_string(nowMs()) + "_" + suffix;
#endif
}

} // namespace

LlmClient::LlmClient(const LlmConfig& config) : m_config(config) {}

LlmResponse LlmClient::chat(
    const std::vector<Message>& messages,
    const std::vector<Tool>& tools
) {
    LlmResponse resp;
    if (m_config.apiKey.empty()) {
        resp.success = false;
        resp.error = "missing API key";
        return resp;
    }
    if (m_config.baseUrl.empty()) {
        resp.success = false;
        resp.error = "missing base URL";
        return resp;
    }
    if (m_config.model.empty()) {
        resp.success = false;
        resp.error = "missing model";
        return resp;
    }

    std::string requestBody = buildRequestJson(messages, tools, false);
    std::string requestPath = makeTempPath("request.json");
    {
        std::ofstream out(requestPath, std::ios::binary);
        if (!out) {
            resp.success = false;
            resp.error = "failed to create temporary request file";
            return resp;
        }
        out << requestBody;
    }

    std::string endpoint = chatCompletionsEndpoint(m_config.baseUrl);
    std::ostringstream cmd;
    cmd << "curl -sS --max-time " << (m_config.timeoutMs / 1000)
#ifdef _WIN32
        << " --ssl-no-revoke"
#endif
        << " -X POST " << quoteShellArg(endpoint)
        << " -H " << quoteShellArg("Content-Type: application/json")
        << " -H " << quoteShellArg("Authorization: Bearer " + m_config.apiKey)
        << (m_config.sendApiKeyHeader ? " -H " + quoteShellArg("api-key: " + m_config.apiKey) : "")
        << " --data-binary @" << quoteShellArg(requestPath);

    int exitCode = 0;
    std::string body = runCommandCapture(cmd.str(), exitCode);
    std::remove(requestPath.c_str());

    if (exitCode != 0) {
        resp.success = false;
        resp.error = "curl request failed: " + body;
        return resp;
    }

    return parseResponse(body);
}

LlmResponse LlmClient::chatStream(
    const std::vector<Message>& messages,
    const std::vector<Tool>& tools,
    StreamCallback onToken
) {
    LlmResponse resp;
    if (m_config.apiKey.empty()) {
        resp.success = false;
        resp.error = "missing API key";
        return resp;
    }
    if (m_config.baseUrl.empty()) {
        resp.success = false;
        resp.error = "missing base URL";
        return resp;
    }
    if (m_config.model.empty()) {
        resp.success = false;
        resp.error = "missing model";
        return resp;
    }

    std::string requestBody = buildRequestJson(messages, tools, true);
    std::string requestPath = makeTempPath("stream_request.json");
    {
        std::ofstream out(requestPath, std::ios::binary);
        if (!out) {
            resp.success = false;
            resp.error = "failed to create temporary request file";
            return resp;
        }
        out << requestBody;
    }

    std::string endpoint = chatCompletionsEndpoint(m_config.baseUrl);
    std::ostringstream cmd;
    cmd << "curl -sS --no-buffer --max-time " << (m_config.timeoutMs / 1000)
#ifdef _WIN32
        << " --ssl-no-revoke"
#endif
        << " -X POST " << quoteShellArg(endpoint)
        << " -H " << quoteShellArg("Content-Type: application/json")
        << " -H " << quoteShellArg("Authorization: Bearer " + m_config.apiKey)
        << (m_config.sendApiKeyHeader ? " -H " + quoteShellArg("api-key: " + m_config.apiKey) : "")
        << " --data-binary @" << quoteShellArg(requestPath);

    int exitCode = 0;
    std::string raw = runCommandCapture(cmd.str(), exitCode);
    std::remove(requestPath.c_str());

    if (exitCode != 0) {
        resp.success = false;
        resp.error = "curl stream request failed: " + raw;
        return resp;
    }

    return parseSSEStream(raw, onToken);
}

std::string LlmClient::buildRequestJson(
    const std::vector<Message>& messages,
    const std::vector<Tool>& tools,
    bool stream
) {
    std::ostringstream out;
    out << "{";
    out << "\"model\":\"" << jsonEscape(m_config.model) << "\",";
    out << "\"temperature\":" << m_config.temperature << ",";
    const std::string maxField = m_config.maxTokensField.empty()
        ? "max_tokens"
        : m_config.maxTokensField;
    out << "\"" << jsonEscape(maxField) << "\":" << m_config.maxTokens << ",";
    out << "\"stream\":" << (stream ? "true" : "false") << ",";
    out << "\"messages\":[";

    for (size_t i = 0; i < messages.size(); ++i) {
        const auto& msg = messages[i];
        if (i > 0) out << ",";
        out << "{";
        out << "\"role\":\"" << roleToString(msg.role) << "\"";
        if (msg.role == Role::Tool) {
            out << ",\"tool_call_id\":\"" << jsonEscape(msg.toolCallId) << "\"";
            if (!msg.name.empty()) out << ",\"name\":\"" << jsonEscape(msg.name) << "\"";
        }
        out << ",\"content\":\"" << jsonEscape(msg.content) << "\"";
        if (!msg.toolCalls.empty()) {
            out << ",\"tool_calls\":[";
            for (size_t j = 0; j < msg.toolCalls.size(); ++j) {
                const auto& tc = msg.toolCalls[j];
                if (j > 0) out << ",";
                out << "{";
                out << "\"id\":\"" << jsonEscape(tc.id) << "\",";
                out << "\"type\":\"function\",";
                out << "\"function\":{";
                out << "\"name\":\"" << jsonEscape(tc.name) << "\",";
                out << "\"arguments\":\"" << jsonEscape(tc.arguments) << "\"";
                out << "}}";
            }
            out << "]";
        }
        out << "}";
    }

    out << "]";
    if (!tools.empty()) {
        out << ",\"tools\":[";
        for (size_t i = 0; i < tools.size(); ++i) {
            const auto& tool = tools[i];
            if (i > 0) out << ",";
            out << "{";
            out << "\"type\":\"function\",";
            out << "\"function\":{";
            out << "\"name\":\"" << jsonEscape(tool.name) << "\",";
            out << "\"description\":\"" << jsonEscape(tool.description) << "\",";
            out << "\"parameters\":"
                << (tool.parametersJson.empty() ? "{\"type\":\"object\",\"properties\":{}}" : tool.parametersJson);
            out << "}}";
        }
        out << "]";
        out << ",\"tool_choice\":\"auto\"";
    }
    out << "}";
    return out.str();
}

LlmResponse LlmClient::parseResponse(const std::string& json) {
    LlmResponse resp;
    try {
        Json root = JsonParser(json).parse();
        const Json& error = root.at("error");
        if (error.isObject()) {
            resp.success = false;
            resp.error = error.at("message").asString("llm api error");
            return resp;
        }

        const Json& choices = root.at("choices");
        if (!choices.isArray() || choices.arrayValue.empty()) {
            resp.success = false;
            resp.error = "response missing choices";
            return resp;
        }

        const Json& choice = choices.arrayValue.front();
        const Json& message = choice.at("message");
        resp.content = message.at("content").asString("");
        resp.finishReason = choice.at("finish_reason").asString("");

        const Json& toolCalls = message.at("tool_calls");
        if (toolCalls.isArray()) {
            for (const auto& item : toolCalls.arrayValue) {
                ToolCall tc;
                tc.id = item.at("id").asString();
                const Json& fn = item.at("function");
                tc.name = fn.at("name").asString();
                tc.arguments = fn.at("arguments").asString("{}");
                if (!tc.name.empty()) resp.toolCalls.push_back(tc);
            }
        }

        const Json& usage = root.at("usage");
        resp.promptTokens = usage.at("prompt_tokens").asInt(0);
        resp.completionTokens = usage.at("completion_tokens").asInt(0);
        resp.success = true;
    } catch (const std::exception& e) {
        resp.success = false;
        resp.error = std::string("failed to parse response: ") + e.what();
    }
    return resp;
}

LlmResponse LlmClient::parseSSEStream(const std::string& rawResponse, StreamCallback onToken) {
    LlmResponse resp;
    std::unordered_map<int, ToolCall> partialToolCalls;
    std::istringstream in(rawResponse);
    std::string line;

    try {
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.rfind("data:", 0) != 0) continue;

            std::string data = line.substr(5);
            if (!data.empty() && data.front() == ' ') data.erase(data.begin());
            if (data == "[DONE]") break;
            if (data.empty()) continue;

            Json root = JsonParser(data).parse();
            const Json& choices = root.at("choices");
            if (!choices.isArray() || choices.arrayValue.empty()) continue;

            const Json& choice = choices.arrayValue.front();
            resp.finishReason = choice.at("finish_reason").asString(resp.finishReason);
            const Json& delta = choice.at("delta");
            std::string token = delta.at("content").asString("");
            if (!token.empty()) {
                resp.content += token;
                if (onToken) onToken(token);
            }

            const Json& toolCalls = delta.at("tool_calls");
            if (toolCalls.isArray()) {
                for (const auto& item : toolCalls.arrayValue) {
                    int index = item.at("index").asInt(static_cast<int>(partialToolCalls.size()));
                    ToolCall& tc = partialToolCalls[index];
                    std::string id = item.at("id").asString("");
                    if (!id.empty()) tc.id = id;
                    const Json& fn = item.at("function");
                    tc.name += fn.at("name").asString("");
                    tc.arguments += fn.at("arguments").asString("");
                }
            }
        }

        for (auto& pair : partialToolCalls) {
            if (!pair.second.name.empty()) resp.toolCalls.push_back(pair.second);
        }
        resp.success = true;
    } catch (const std::exception& e) {
        resp.success = false;
        resp.error = std::string("failed to parse sse stream: ") + e.what();
    }
    return resp;
}

} // namespace clawlite
