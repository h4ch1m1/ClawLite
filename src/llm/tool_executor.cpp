#include "llm/tool_executor.h"

#include <cctype>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace clawlite {
namespace {

std::string extractJsonString(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t keyPos = json.find(needle);
    if (keyPos == std::string::npos) return "";

    size_t colon = json.find(':', keyPos + needle.size());
    if (colon == std::string::npos) return "";

    size_t quote = json.find('"', colon + 1);
    if (quote == std::string::npos) return "";

    std::string value;
    for (size_t i = quote + 1; i < json.size(); ++i) {
        char c = json[i];
        if (c == '"') return value;
        if (c == '\\' && i + 1 < json.size()) {
            char e = json[++i];
            switch (e) {
                case '"': value += '"'; break;
                case '\\': value += '\\'; break;
                case 'n': value += '\n'; break;
                case 'r': value += '\r'; break;
                case 't': value += '\t'; break;
                default: value += e; break;
            }
        } else {
            value += c;
        }
    }
    return "";
}

class ExpressionParser {
public:
    explicit ExpressionParser(const std::string& expr) : m_expr(expr) {}

    double parse() {
        double value = parseExpression();
        skipSpaces();
        if (m_pos != m_expr.size()) {
            throw std::runtime_error("unexpected token in expression");
        }
        return value;
    }

private:
    const std::string& m_expr;
    size_t m_pos = 0;

    void skipSpaces() {
        while (m_pos < m_expr.size() && std::isspace(static_cast<unsigned char>(m_expr[m_pos]))) {
            ++m_pos;
        }
    }

    bool consume(char c) {
        skipSpaces();
        if (m_pos < m_expr.size() && m_expr[m_pos] == c) {
            ++m_pos;
            return true;
        }
        return false;
    }

    double parseExpression() {
        double value = parseTerm();
        while (true) {
            if (consume('+')) value += parseTerm();
            else if (consume('-')) value -= parseTerm();
            else return value;
        }
    }

    double parseTerm() {
        double value = parseFactor();
        while (true) {
            if (consume('*')) value *= parseFactor();
            else if (consume('/')) {
                double rhs = parseFactor();
                if (rhs == 0.0) throw std::runtime_error("division by zero");
                value /= rhs;
            } else {
                return value;
            }
        }
    }

    double parseFactor() {
        skipSpaces();
        if (consume('+')) return parseFactor();
        if (consume('-')) return -parseFactor();
        if (consume('(')) {
            double value = parseExpression();
            if (!consume(')')) throw std::runtime_error("missing closing parenthesis");
            return value;
        }
        return parseNumber();
    }

    double parseNumber() {
        skipSpaces();
        size_t start = m_pos;
        bool sawDigit = false;
        while (m_pos < m_expr.size() && std::isdigit(static_cast<unsigned char>(m_expr[m_pos]))) {
            sawDigit = true;
            ++m_pos;
        }
        if (m_pos < m_expr.size() && m_expr[m_pos] == '.') {
            ++m_pos;
            while (m_pos < m_expr.size() && std::isdigit(static_cast<unsigned char>(m_expr[m_pos]))) {
                sawDigit = true;
                ++m_pos;
            }
        }
        if (!sawDigit) throw std::runtime_error("expected number");
        return std::stod(m_expr.substr(start, m_pos - start));
    }
};

std::string formatDouble(double value) {
    std::ostringstream out;
    out << std::setprecision(12) << value;
    std::string s = out.str();
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    return s;
}

} // namespace

void ToolExecutor::registerTool(const Tool& tool) {
    if (tool.name.empty()) {
        throw std::invalid_argument("tool name cannot be empty");
    }
    m_tools[tool.name] = tool;
}

void ToolExecutor::registerBuiltinTools() {
    Tool hello;
    hello.name = "hello";
    hello.description = "Return a friendly greeting.";
    hello.parametersJson = R"({"type":"object","properties":{}})";
    hello.handler = [](const std::string&) {
        static const std::string replies[] = {"Hello!", "Hi!", "Bonjour!", "Hola!", "Hallo!"};
        static thread_local size_t index = 0;
        const std::string& reply = replies[index % (sizeof(replies) / sizeof(replies[0]))];
        ++index;
        return reply;
    };
    registerTool(hello);

    Tool calc;
    calc.name = "calculator";
    calc.description = "Evaluate an arithmetic expression with +, -, *, /, and parentheses.";
    calc.parametersJson = R"({"type":"object","properties":{"expr":{"type":"string","description":"Arithmetic expression to evaluate"}},"required":["expr"]})";
    calc.handler = toolCalculator;
    registerTool(calc);

    Tool time;
    time.name = "current_time";
    time.description = "Get the current local date and time.";
    time.parametersJson = R"({"type":"object","properties":{}})";
    time.handler = toolCurrentTime;
    registerTool(time);

    Tool readFile;
    readFile.name = "read_file";
    readFile.description = "Read a UTF-8 text file from the workspace or an absolute path. Output is capped.";
    readFile.parametersJson = R"({"type":"object","properties":{"path":{"type":"string","description":"File path to read"}},"required":["path"]})";
    readFile.handler = toolReadFile;
    registerTool(readFile);
}

ToolResult ToolExecutor::execute(const ToolCall& toolCall) {
    auto it = m_tools.find(toolCall.name);
    if (it == m_tools.end()) {
        return {false, "", "tool not found: " + toolCall.name};
    }
    if (!it->second.handler) {
        return {false, "", "tool has no handler: " + toolCall.name};
    }

    try {
        return {true, it->second.handler(toolCall.arguments), ""};
    } catch (const std::exception& e) {
        return {false, "", e.what()};
    } catch (...) {
        return {false, "", "tool failed with unknown error"};
    }
}

std::vector<Tool> ToolExecutor::getAllTools() const {
    std::vector<Tool> tools;
    tools.reserve(m_tools.size());
    for (const auto& pair : m_tools) {
        tools.push_back(pair.second);
    }
    return tools;
}

bool ToolExecutor::hasTool(const std::string& name) const {
    return m_tools.find(name) != m_tools.end();
}

size_t ToolExecutor::size() const {
    return m_tools.size();
}

std::string toolCalculator(const std::string& argsJson) {
    std::string expr = extractJsonString(argsJson, "expr");
    if (expr.empty()) expr = extractJsonString(argsJson, "expression");
    if (expr.empty()) throw std::runtime_error("calculator requires string field 'expr'");
    return formatDouble(ExpressionParser(expr).parse());
}

std::string toolCurrentTime(const std::string&) {
    time_t now = time(nullptr);
    char buf[64];
#ifdef _WIN32
    tm localTime;
    localtime_s(&localTime, &now);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &localTime);
#else
    tm localTime;
    localtime_r(&now, &localTime);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &localTime);
#endif
    return std::string(buf);
}

std::string toolReadFile(const std::string& argsJson) {
    std::string path = extractJsonString(argsJson, "path");
    if (path.empty()) throw std::runtime_error("read_file requires string field 'path'");

    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("failed to open file: " + path);

    constexpr std::streamsize kMaxBytes = 10 * 1024;
    std::string content(static_cast<size_t>(kMaxBytes), '\0');
    in.read(&content[0], kMaxBytes);
    content.resize(static_cast<size_t>(in.gcount()));
    if (in.peek() != EOF) {
        content += "\n[truncated after 10240 bytes]";
    }
    return content;
}

} // namespace clawlite
