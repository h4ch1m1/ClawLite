// ClawLite — 技能资格过滤器实现

#include "skill/skill_filter.h"
#include <cstdlib>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <vector>

namespace clawlite {
namespace {

static std::string toLowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

static std::string trimCopy(const std::string& s) {
    auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

static std::vector<std::string> splitList(const std::string& raw) {
    std::vector<std::string> items;
    std::string s = trimCopy(raw);
    if (s.empty()) return items;

    // Remove surrounding JSON/YAML-ish brackets.
    if (s.size() >= 2 && ((s.front() == '[' && s.back() == ']') || (s.front() == '(' && s.back() == ')'))) {
        s = s.substr(1, s.size() - 2);
    }

    std::string current;
    bool inSingle = false, inDouble = false;
    for (char ch : s) {
        if (ch == '\'' && !inDouble) inSingle = !inSingle;
        else if (ch == '"' && !inSingle) inDouble = !inDouble;

        if (ch == ',' && !inSingle && !inDouble) {
            std::string item = trimCopy(current);
            if (!item.empty()) {
                if (item.size() >= 2 && ((item.front() == '"' && item.back() == '"') || (item.front() == '\'' && item.back() == '\''))) {
                    item = item.substr(1, item.size() - 2);
                }
                items.push_back(item);
            }
            current.clear();
        } else {
            current.push_back(ch);
        }
    }

    std::string item = trimCopy(current);
    if (!item.empty()) {
        if (item.size() >= 2 && ((item.front() == '"' && item.back() == '"') || (item.front() == '\'' && item.back() == '\''))) {
            item = item.substr(1, item.size() - 2);
        }
        items.push_back(item);
    }
    return items;
}

static bool hasPathKey(const SkillDefinition& skill, const std::vector<std::string>& keys, std::string& valueOut) {
    for (const auto& key : keys) {
        auto it = skill.frontmatter.find(key);
        if (it != skill.frontmatter.end()) {
            valueOut = it->second;
            return true;
        }
    }
    return false;
}

static bool envExists(const std::string& name) {
    return std::getenv(name.c_str()) != nullptr;
}

static bool binExistsOnPath(const std::string& bin) {
    if (bin.empty()) return false;
    namespace fs = std::filesystem;

    fs::path candidate(bin);
    if (candidate.is_absolute() || candidate.has_parent_path()) {
        std::error_code ec;
        return fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec);
    }

    const char* pathEnv = std::getenv("PATH");
    if (!pathEnv) return false;

    std::string pathValue = pathEnv;
#ifdef _WIN32
    const char sep = ';';
    const std::vector<std::string> exts = {".exe", ".bat", ".cmd", ""};
#else
    const char sep = ':';
    const std::vector<std::string> exts = {""};
#endif

    size_t start = 0;
    while (start <= pathValue.size()) {
        size_t end = pathValue.find(sep, start);
        if (end == std::string::npos) end = pathValue.size();
        std::string dir = pathValue.substr(start, end - start);
        if (!dir.empty()) {
            for (const auto& ext : exts) {
                fs::path p = fs::path(dir) / (bin + ext);
                std::error_code ec;
                if (fs::exists(p, ec) && fs::is_regular_file(p, ec)) {
                    return true;
                }
            }
        }
        start = end + 1;
    }
    return false;
}

} // namespace

bool SkillFilter::isEligible(const SkillDefinition& skill, const SkillFilterConfig& config) {
    return checkOs(skill, config.os)
        && checkBins(skill, config.availableBins)
        && checkEnvs(skill, config.availableEnvs);
}

std::vector<SkillEntry> SkillFilter::filterSkills(
    const std::vector<SkillEntry>& skills,
    const SkillFilterConfig& config
) {
    std::vector<SkillEntry> result;
    for (const auto& entry : skills) {
        if (isEligible(entry.definition, config)) {
            result.push_back(entry);
        }
    }
    return result;
}

SkillFilterConfig SkillFilter::detectSystem() {
    SkillFilterConfig config;

#if defined(_WIN32)
    config.os = "windows";
#elif defined(__APPLE__)
    config.os = "macos";
#elif defined(__linux__)
    config.os = "linux";
#else
    config.os = "unknown";
#endif

    const char* commonBins[] = {
        "curl", "git", "python", "python3", "node", "npm", "bash", "sh"
    };
    for (const char* bin : commonBins) {
        if (binExistsOnPath(bin)) {
            config.availableBins.insert(bin);
        }
    }

    const char* commonEnvs[] = {
        "HOME", "USERPROFILE", "PATH", "TMP", "TEMP", "SHELL", "COMSPEC"
    };
    for (const char* env : commonEnvs) {
        if (envExists(env)) {
            config.availableEnvs.insert(env);
        }
    }

    return config;
}

bool SkillFilter::checkOs(const SkillDefinition& skill, const std::string& currentOs) {
    std::string value;
    if (!hasPathKey(skill, {"os", "metadata.openclaw.os", "metadata.os"}, value)) {
        return true;
    }

    value = toLowerCopy(trimCopy(value));
    std::string osValue = toLowerCopy(trimCopy(currentOs));
    auto items = splitList(value);
    if (items.empty()) {
        items.push_back(value);
    }

    for (auto item : items) {
        item = toLowerCopy(trimCopy(item));
        if (item == osValue) return true;
        if (item == "darwin" && osValue == "macos") return true;
        if (item == "mac" && osValue == "macos") return true;
        if (item == "win" && osValue == "windows") return true;
        if (item == "unix" && (osValue == "linux" || osValue == "macos")) return true;
    }
    return false;
}

bool SkillFilter::checkBins(const SkillDefinition& skill, const std::set<std::string>& available) {
    std::string value;
    if (!hasPathKey(skill, {
        "requires.bins",
        "metadata.openclaw.requires.bins",
        "metadata.requires.bins"
    }, value)) {
        return true;
    }

    for (auto item : splitList(value)) {
        item = trimCopy(item);
        if (item.empty()) continue;
        if (available.find(item) == available.end()) {
            return false;
        }
    }
    return true;
}

bool SkillFilter::checkEnvs(const SkillDefinition& skill, const std::set<std::string>& available) {
    std::string value;
    if (!hasPathKey(skill, {
        "requires.env",
        "metadata.openclaw.requires.env",
        "metadata.requires.env"
    }, value)) {
        return true;
    }

    for (auto item : splitList(value)) {
        item = trimCopy(item);
        if (item.empty()) continue;
        if (available.find(item) == available.end()) {
            return false;
        }
    }
    return true;
}

} // namespace clawlite
