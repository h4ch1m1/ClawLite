#include "core/config.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <unordered_map>

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
    if (!in) {
        throw std::runtime_error("missing config file: " + path);
    }

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

std::string requireValue(const std::unordered_map<std::string, std::string>& values,
                         const std::string& key) {
    auto it = values.find(key);
    if (it == values.end() || it->second.empty()) {
        throw std::runtime_error("missing required config key: " + key);
    }
    return it->second;
}

} // namespace

AppConfig loadAppConfig(const std::string& path) {
    auto values = loadKeyValueFile(path);

    AppConfig config;
    config.llm.baseUrl = requireValue(values, "CLAWLITE_BASE_URL");
    config.llm.apiKey = requireValue(values, "CLAWLITE_API_KEY");
    config.llm.model = requireValue(values, "CLAWLITE_MODEL");

    auto itTemp = values.find("CLAWLITE_TEMPERATURE");
    if (itTemp != values.end()) config.llm.temperature = std::stod(itTemp->second);
    auto itMax = values.find("CLAWLITE_MAX_TOKENS");
    if (itMax != values.end()) config.llm.maxTokens = std::stoi(itMax->second);
    auto itTimeout = values.find("CLAWLITE_TIMEOUT_MS");
    if (itTimeout != values.end()) config.llm.timeoutMs = std::stoi(itTimeout->second);
    return config;
}

} // namespace clawlite
