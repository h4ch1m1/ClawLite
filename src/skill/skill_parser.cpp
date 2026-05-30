// ClawLite — SKILL.md 解析器实现

#include "skill/skill_parser.h"
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cctype>

namespace clawlite {
namespace {

static inline std::string trimCopy(const std::string& s) {
    auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

static inline std::string ltrimCopy(const std::string& s) {
    auto begin = s.find_first_not_of(" \t\r\n");
    return begin == std::string::npos ? std::string{} : s.substr(begin);
}

static inline std::string rtrimCopy(const std::string& s) {
    auto end = s.find_last_not_of(" \t\r\n");
    return end == std::string::npos ? std::string{} : s.substr(0, end + 1);
}

static inline bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), s.begin());
}

static inline bool isIndentedContinuation(const std::string& line) {
    return !line.empty() && (line[0] == ' ' || line[0] == '\t');
}

static std::string unquote(const std::string& value) {
    std::string v = trimCopy(value);
    if (v.size() >= 2) {
        if ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\'')) {
            return v.substr(1, v.size() - 2);
        }
    }
    return v;
}

static std::string toLowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

static std::string escapeXml(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char ch : s) {
        switch (ch) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out += ch; break;
        }
    }
    return out;
}

// A very small YAML-ish flattener for frontmatter.
// It supports:
//   key: value
//   key:
//     nested: value
//   list:
//     - a
//     - b
// The result uses dotted keys for nested maps.
struct Frame {
    int indent = -1;
    std::string prefix;
};

static std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::string current;
    std::istringstream iss(text);
    while (std::getline(iss, current)) {
        if (!current.empty() && current.back() == '\r') current.pop_back();
        lines.push_back(current);
    }
    if (!text.empty() && text.back() == '\n') {
        // keep getline semantics; no action needed
    }
    return lines;
}

static std::string stripInlineComment(const std::string& line) {
    bool inSingle = false, inDouble = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '\'' && !inDouble) inSingle = !inSingle;
        else if (c == '"' && !inSingle) inDouble = !inDouble;
        else if (c == '#' && !inSingle && !inDouble) {
            if (i == 0 || std::isspace(static_cast<unsigned char>(line[i - 1]))) {
                return rtrimCopy(line.substr(0, i));
            }
        }
    }
    return rtrimCopy(line);
}

static std::string joinPath(const std::string& prefix, const std::string& key) {
    if (prefix.empty()) return key;
    return prefix + "." + key;
}

static bool looksLikeListItem(const std::string& trimmed) {
    return startsWith(trimmed, "- ") || trimmed == "-";
}

static std::string normalizeScalar(const std::string& value) {
    std::string v = trimCopy(value);
    if (v == "true" || v == "false" || v == "null") return toLowerCopy(v);
    return unquote(v);
}

} // namespace

std::optional<SkillDefinition> SkillParser::parse(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        return std::nullopt;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string content = buffer.str();

    auto [frontmatterBlock, body] = extractSections(content);
    ParsedFrontmatter frontmatter = parseFrontmatter(frontmatterBlock);

    auto itName = frontmatter.find("name");
    auto itDesc = frontmatter.find("description");
    if (itName == frontmatter.end() || itDesc == frontmatter.end()) {
        return std::nullopt;
    }

    SkillDefinition def;
    def.name = trimCopy(itName->second);
    def.description = trimCopy(itDesc->second);
    def.frontmatter = std::move(frontmatter);
    def.body = std::move(body);
    def.filePath = filePath;

    if (def.name.empty() || def.description.empty()) {
        return std::nullopt;
    }
    return def;
}

ParsedFrontmatter SkillParser::parseFrontmatter(const std::string& rawBlock) {
    ParsedFrontmatter merged = parseByLine(rawBlock);

    // Best-effort second pass. If no richer parser is available, this still
    // keeps the API contract and preserves current behavior.
    ParsedFrontmatter secondary = parseByYaml(rawBlock);
    for (const auto& [k, v] : secondary) {
        merged[k] = v;
    }
    return merged;
}

std::pair<std::string, std::string> SkillParser::extractSections(const std::string& content) {
    const std::string marker = "---";
    std::vector<std::string> lines = splitLines(content);
    if (lines.empty()) return {"", content};

    size_t first = std::string::npos;
    size_t second = std::string::npos;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (trimCopy(lines[i]) == marker) {
            if (first == std::string::npos) first = i;
            else { second = i; break; }
        }
    }

    if (first == std::string::npos || second == std::string::npos || second <= first) {
        return {"", content};
    }

    std::string frontmatter;
    for (size_t i = first + 1; i < second; ++i) {
        frontmatter += lines[i];
        frontmatter.push_back('\n');
    }

    std::string body;
    for (size_t i = second + 1; i < lines.size(); ++i) {
        body += lines[i];
        if (i + 1 < lines.size()) body.push_back('\n');
    }
    return {frontmatter, body};
}

ParsedFrontmatter SkillParser::parseByLine(const std::string& block) {
    ParsedFrontmatter result;
    if (block.empty()) return result;

    auto lines = splitLines(block);
    std::vector<Frame> stack;
    stack.push_back(Frame{-1, ""});

    std::string currentKey;
    for (size_t i = 0; i < lines.size(); ++i) {
        std::string raw = lines[i];
        std::string stripped = stripInlineComment(raw);
        std::string trimmed = trimCopy(stripped);
        if (trimmed.empty()) continue;

        // List item continuation for the last key
        if (looksLikeListItem(trimmed)) {
            if (!currentKey.empty()) {
                auto& dst = result[currentKey];
                if (!dst.empty()) dst += ", ";
                dst += normalizeScalar(trimmed.substr(trimmed.find('-') + 1));
            }
            continue;
        }

        // continuation of a scalar or nested block
        if (isIndentedContinuation(raw) && !currentKey.empty() && trimmed.find(':') == std::string::npos) {
            auto& dst = result[currentKey];
            if (!dst.empty()) dst += "\n";
            dst += normalizeScalar(trimmed);
            continue;
        }

        const auto colonPos = trimmed.find(':');
        if (colonPos == std::string::npos) continue;

        std::string key = trimCopy(trimmed.substr(0, colonPos));
        std::string value = trimCopy(trimmed.substr(colonPos + 1));

        // Determine indentation level
        int indent = 0;
        while (indent < static_cast<int>(raw.size()) && (raw[indent] == ' ' || raw[indent] == '\t')) {
            ++indent;
        }

        while (!stack.empty() && stack.back().indent >= indent) {
            stack.pop_back();
        }

        std::string fullKey = joinPath(stack.back().prefix, key);

        if (value.empty()) {
            // Nested mapping / list root
            stack.push_back(Frame{indent, fullKey});
            currentKey = fullKey;
            // Preserve empty markers so callers can inspect presence if needed.
            result.emplace(fullKey, "");
            continue;
        }

        result[fullKey] = normalizeScalar(value);
        currentKey = fullKey;
        stack.push_back(Frame{indent, fullKey});
    }

    return result;
}

ParsedFrontmatter SkillParser::parseByYaml(const std::string& block) {
    // This project can be built without yaml-cpp. We still provide a best-effort
    // structured parser here so the API remains useful even in minimal setups.
    // It reuses the line parser result; callers receive a stable flat map.
    return parseByLine(block);
}

} // namespace clawlite
