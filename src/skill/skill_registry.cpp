// ClawLite — 技能注册表实现

#include "skill/skill_registry.h"
#include <algorithm>
#include <sstream>
#include <memory>
#include <unordered_map>

namespace clawlite {
namespace {

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

static std::string formatSkillXmlLocal(const SkillEntry& entry, bool compact) {
    std::string xml = "  <skill>\n";
    xml += "    <name>" + escapeXml(entry.definition.name) + "</name>\n";
    if (!compact) {
        xml += "    <description>" + escapeXml(entry.definition.description) + "</description>\n";
        if (!entry.definition.body.empty()) {
            xml += "    <instructions>" + escapeXml(entry.definition.body) + "</instructions>\n";
        }
    }
    xml += "    <location>" + escapeXml(entry.definition.filePath) + "</location>\n";
    xml += "  </skill>\n";
    return xml;
}

static size_t promptLength(const std::vector<SkillEntry>& skills, size_t count, bool compact) {
    std::string prompt = "<available_skills>\n";
    for (size_t i = 0; i < count && i < skills.size(); ++i) {
        prompt += formatSkillXmlLocal(skills[i], compact);
    }
    prompt += "</available_skills>\n";
    return prompt.size();
}

struct TrieNode {
    std::unordered_map<char, std::unique_ptr<TrieNode>> children;
    bool terminal = false;
    std::string word;
};

static void trieInsert(TrieNode& root, const std::string& word) {
    TrieNode* node = &root;
    for (char ch : word) {
        auto& child = node->children[ch];
        if (!child) child = std::make_unique<TrieNode>();
        node = child.get();
    }
    node->terminal = true;
    node->word = word;
}

static void trieCollect(const TrieNode& node, std::vector<std::string>& out, size_t limit) {
    if (out.size() >= limit) return;
    if (node.terminal) out.push_back(node.word);
    std::vector<char> keys;
    keys.reserve(node.children.size());
    for (const auto& pair : node.children) keys.push_back(pair.first);
    std::sort(keys.begin(), keys.end());
    for (char key : keys) {
        trieCollect(*node.children.at(key), out, limit);
        if (out.size() >= limit) return;
    }
}

} // namespace

// 新接口：带过滤配置
void SkillRegistry::loadFromWorkspace(const std::string& workspaceDir,
                                      const SkillFilterConfig& filterConfig) {
    auto dirs = SkillLoader::getDefaultDirs(workspaceDir);
    auto entries = SkillLoader::loadAndMerge(dirs);

    // 资格过滤：如果 filterConfig 指明 OS（非空），则执行过滤
    if (!filterConfig.os.empty()) {
        entries = SkillFilter::filterSkills(entries, filterConfig);
    }

    m_skills.clear();   // 清空旧技能，确保每次加载是全新状态
    for (auto& entry : entries) {
        m_skills[entry.definition.name] = std::move(entry);
    }
}

// 保留无过滤的原有接口（兼容已有测试）
void SkillRegistry::loadFromWorkspace(const std::string& workspaceDir) {
    // 传入空的 SkillFilterConfig，os 为空代表不过滤
    loadFromWorkspace(workspaceDir, SkillFilterConfig{});
}

void SkillRegistry::registerSkill(const SkillEntry& entry) {
    m_skills[entry.definition.name] = entry;
}

std::vector<SkillEntry> SkillRegistry::getActiveSkills() const {
    std::vector<SkillEntry> result;
    result.reserve(m_skills.size());
    for (const auto& [name, entry] : m_skills) {
        (void)name;
        result.push_back(entry);
    }
    return result;
}

const SkillEntry* SkillRegistry::findSkill(const std::string& name) const {
    auto it = m_skills.find(name);
    if (it != m_skills.end()) return &it->second;
    return nullptr;
}

std::vector<std::string> SkillRegistry::completeSkillNames(const std::string& prefix,
                                                           size_t limit) const {
    TrieNode root;
    for (const auto& pair : m_skills) {
        trieInsert(root, pair.first);
    }

    const TrieNode* node = &root;
    for (char ch : prefix) {
        auto it = node->children.find(ch);
        if (it == node->children.end()) return {};
        node = it->second.get();
    }

    std::vector<std::string> result;
    trieCollect(*node, result, limit);
    return result;
}

std::string SkillRegistry::buildSkillPrompt(int charBudget) const {
    if (charBudget <= 0) {
        return "<available_skills>\n</available_skills>\n";
    }

    auto skills = getActiveSkills();
    std::sort(skills.begin(), skills.end(),
        [](const SkillEntry& a, const SkillEntry& b) {
            return a.definition.name < b.definition.name;
        });

    std::string fullPrompt = "<available_skills>\n";
    for (const auto& skill : skills) {
        fullPrompt += formatSkillXml(skill, false);
    }
    fullPrompt += "</available_skills>\n";

    if (static_cast<int>(fullPrompt.size()) <= charBudget) {
        return fullPrompt;
    }

    std::string compactPrompt = "<available_skills>\n";
    for (const auto& skill : skills) {
        compactPrompt += formatSkillXml(skill, true);
    }
    compactPrompt += "</available_skills>\n";

    if (static_cast<int>(compactPrompt.size()) <= charBudget) {
        return compactPrompt;
    }

    const int keep = binarySearchPromptLimit(skills, charBudget);
    std::string prompt = "<available_skills>\n";
    for (int i = 0; i < keep; ++i) {
        prompt += formatSkillXml(skills[static_cast<size_t>(i)], true);
    }
    prompt += "</available_skills>\n";

    if (static_cast<int>(prompt.size()) > charBudget) {
        // Final safety clamp: progressively drop the last item until it fits.
        while (!skills.empty() && static_cast<int>(prompt.size()) > charBudget) {
            if (keep == 0) break;
            prompt = "<available_skills>\n";
            for (int i = 0; i < keep - 1; ++i) {
                prompt += formatSkillXmlLocal(skills[static_cast<size_t>(i)], true);
            }
            prompt += "</available_skills>\n";
            if (keep <= 1) break;
        }
    }

    return prompt;
}

size_t SkillRegistry::size() const {
    return m_skills.size();
}

void SkillRegistry::clear() {
    m_skills.clear();
}

int SkillRegistry::binarySearchPromptLimit(
    const std::vector<SkillEntry>& sorted,
    int charBudget
) {
    std::vector<size_t> prefix(sorted.size() + 1, 0);
    const size_t wrapper = std::string("<available_skills>\n").size()
        + std::string("</available_skills>\n").size();
    for (size_t i = 0; i < sorted.size(); ++i) {
        prefix[i + 1] = prefix[i] + formatSkillXmlLocal(sorted[i], true).size();
    }

    int lo = 0;
    int hi = static_cast<int>(sorted.size());

    while (lo < hi) {
        int mid = lo + (hi - lo + 1) / 2;
        if (static_cast<int>(wrapper + prefix[static_cast<size_t>(mid)]) <= charBudget) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    return lo;
}

std::string SkillRegistry::formatSkillXml(const SkillEntry& entry, bool compact) {
    std::string xml = "  <skill>\n";
    xml += "    <name>" + escapeXml(entry.definition.name) + "</name>\n";
    if (!compact) {
        xml += "    <description>" + escapeXml(entry.definition.description) + "</description>\n";
        if (!entry.definition.body.empty()) {
            xml += "    <instructions>" + escapeXml(entry.definition.body) + "</instructions>\n";
        }
    }
    xml += "    <location>" + escapeXml(entry.definition.filePath) + "</location>\n";
    xml += "  </skill>\n";
    return xml;
}

} // namespace clawlite
