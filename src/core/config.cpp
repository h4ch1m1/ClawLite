#include "core/config.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <cctype>

namespace clawlite {
namespace {

std::string trim(std::string s) {
    if (s.size() >= 3 &&
        static_cast<unsigned char>(s[0]) == 0xEF &&
        static_cast<unsigned char>(s[1]) == 0xBB &&
        static_cast<unsigned char>(s[2]) == 0xBF) {
        s.erase(0, 3);
    }
    auto notSpace = [](unsigned char c) {
        return c != ' ' && c != '\t' && c != '\r' && c != '\n';
    };
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
    std::ifstream in(path);
    if (!in) return {};

    std::unordered_map<std::string, std::string> values;
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

void overlayEnv(std::unordered_map<std::string, std::string>& values,
                const std::vector<std::string>& keys) {
    for (const auto& key : keys) {
        const char* raw = std::getenv(key.c_str());
        if (raw && *raw) values[key] = raw;
    }
}

std::string valueOr(const std::unordered_map<std::string, std::string>& values,
                    const std::string& key,
                    const std::string& fallback) {
    auto it = values.find(key);
    if (it == values.end() || it->second.empty()) return fallback;
    return it->second;
}

int intValueOr(const std::unordered_map<std::string, std::string>& values,
               const std::string& key,
               int fallback) {
    auto it = values.find(key);
    if (it == values.end() || it->second.empty()) return fallback;
    return std::stoi(it->second);
}

double doubleValueOr(const std::unordered_map<std::string, std::string>& values,
                     const std::string& key,
                     double fallback) {
    auto it = values.find(key);
    if (it == values.end() || it->second.empty()) return fallback;
    return std::stod(it->second);
}

bool boolValueOr(const std::unordered_map<std::string, std::string>& values,
                 const std::string& key,
                 bool fallback) {
    auto it = values.find(key);
    if (it == values.end() || it->second.empty()) return fallback;
    std::string v = it->second;
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

} // namespace

AppConfig loadAppConfig(const std::string& path) {
    auto values = loadKeyValueFile(path);
    overlayEnv(values, {
        "CLAWLITE_BASE_URL",
        "CLAWLITE_API_KEY",
        "CLAWLITE_MODEL",
        "CLAWLITE_TEMPERATURE",
        "CLAWLITE_MAX_TOKENS",
        "CLAWLITE_TIMEOUT_MS",
        "CLAWLITE_MAX_TOKENS_FIELD",
        "CLAWLITE_SEND_API_KEY_HEADER",
        "CLAWLITE_CONTEXT_TOKEN_BUDGET",
        "CLAWLITE_CHUNK_TOKENS",
        "CLAWLITE_OVERLAP_TOKENS",
        "CLAWLITE_SUMMARY_GROUP_SIZE",
        "CLAWLITE_KEEP_RECENT_TURNS",
        "CLAWLITE_FILE_CACHE_CAPACITY",
        "CLAWLITE_WORKSPACE_DIR",
        "CLAWLITE_DATA_DIR"
    });

    AppConfig config;
    config.llm.baseUrl = valueOr(values, "CLAWLITE_BASE_URL", config.llm.baseUrl);
    config.llm.apiKey = valueOr(values, "CLAWLITE_API_KEY", config.llm.apiKey);
    config.llm.model = valueOr(values, "CLAWLITE_MODEL", config.llm.model);
    config.llm.temperature = doubleValueOr(values, "CLAWLITE_TEMPERATURE", config.llm.temperature);
    config.llm.maxTokens = intValueOr(values, "CLAWLITE_MAX_TOKENS", config.llm.maxTokens);
    config.llm.timeoutMs = intValueOr(values, "CLAWLITE_TIMEOUT_MS", config.llm.timeoutMs);
    config.llm.maxTokensField = valueOr(values, "CLAWLITE_MAX_TOKENS_FIELD", config.llm.maxTokensField);
    config.llm.sendApiKeyHeader = boolValueOr(values, "CLAWLITE_SEND_API_KEY_HEADER", config.llm.sendApiKeyHeader);

    config.memory.contextTokenBudget = intValueOr(values, "CLAWLITE_CONTEXT_TOKEN_BUDGET", config.memory.contextTokenBudget);
    config.memory.chunkTokens = intValueOr(values, "CLAWLITE_CHUNK_TOKENS", config.memory.chunkTokens);
    config.memory.overlapTokens = intValueOr(values, "CLAWLITE_OVERLAP_TOKENS", config.memory.overlapTokens);
    config.memory.summaryGroupSize = intValueOr(values, "CLAWLITE_SUMMARY_GROUP_SIZE", config.memory.summaryGroupSize);
    config.memory.keepRecentTurns = intValueOr(values, "CLAWLITE_KEEP_RECENT_TURNS", config.memory.keepRecentTurns);
    config.memory.fileCacheCapacity = intValueOr(values, "CLAWLITE_FILE_CACHE_CAPACITY", config.memory.fileCacheCapacity);
    config.workspaceDir = valueOr(values, "CLAWLITE_WORKSPACE_DIR", config.workspaceDir);
    config.dataDir = valueOr(values, "CLAWLITE_DATA_DIR", config.dataDir);
    return config;
}

} // namespace clawlite
