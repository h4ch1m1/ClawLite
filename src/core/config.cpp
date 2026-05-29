#include "core/config.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace clawlite {
namespace {

std::string trim(std::string s) {
    auto notSpace = [](unsigned char c) { return c != ' ' && c != '\t' && c != '\r' && c != '\n'; };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

std::string unquote(std::string value) {
    value = trim(value);
    if (value.size() >= 2) {
        char first = value.front();
        char last = value.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            return value.substr(1, value.size() - 2);
        }
    }
    return value;
}

std::unordered_map<std::string, std::string> loadKeyValueFile(const std::string& path) {
    std::unordered_map<std::string, std::string> values;
    std::ifstream in(path);
    if (!in) return values;

    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string key = trim(line.substr(0, pos));
        std::string value = unquote(line.substr(pos + 1));
        if (!key.empty()) values[key] = value;
    }
    return values;
}

std::string pick(const std::unordered_map<std::string, std::string>& values,
                 const std::string& key,
                 const std::string& envKey,
                 const std::string& fallback) {
    auto it = values.find(key);
    if (it != values.end()) return it->second;
    if (const char* env = std::getenv(envKey.c_str())) return env;
    return fallback;
}

bool parseBool(const std::string& value) {
    std::string v = value;
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

} // namespace

AppConfig loadAppConfig(const std::string& path) {
    auto values = loadKeyValueFile(path);
    AppConfig config;
    config.llm.baseUrl = pick(values, "CLAWLITE_BASE_URL", "CLAWLITE_BASE_URL", "https://api.deepseek.com");
    config.llm.apiKey = pick(values, "CLAWLITE_API_KEY", "CLAWLITE_API_KEY", "");
    config.llm.model = pick(values, "CLAWLITE_MODEL", "CLAWLITE_MODEL", "deepseek-chat");
    config.llm.mockMode = parseBool(pick(values, "CLAWLITE_MOCK_LLM", "CLAWLITE_MOCK_LLM", "0"));

    auto itTemp = values.find("CLAWLITE_TEMPERATURE");
    if (itTemp != values.end()) config.llm.temperature = std::stod(itTemp->second);
    auto itMax = values.find("CLAWLITE_MAX_TOKENS");
    if (itMax != values.end()) config.llm.maxTokens = std::stoi(itMax->second);
    auto itTimeout = values.find("CLAWLITE_TIMEOUT_MS");
    if (itTimeout != values.end()) config.llm.timeoutMs = std::stoi(itTimeout->second);
    return config;
}

} // namespace clawlite
