#pragma once
// ClawLite — 技能注册表
// 参考：openclaw-main/src/agents/skills/workspace.ts:resolveWorkspaceSkillPromptState
// 数据结构：unordered_map<string, SkillEntry> 按 name 索引
// 算法：排序 + 二分搜索裁剪（字符预算内尽可能多的技能）

#include "skill/skill_loader.h"
#include "skill/skill_filter.h"          // <-- 新增，为过滤功能引入
#include <vector>
#include <string>
#include <functional>
#include <unordered_map>

namespace clawlite {

class SkillRegistry {
public:
    SkillRegistry() = default;

    // 加载并注册技能（调用 SkillLoader::loadAndMerge），可选过滤
    // 新重载：传入 filterConfig，会在合并后过滤掉不适用的技能
    void loadFromWorkspace(const std::string& workspaceDir,
                           const SkillFilterConfig& filterConfig);

    // 保持兼容的原有接口（无过滤）
    void loadFromWorkspace(const std::string& workspaceDir);

    // 手动注册一个技能
    void registerSkill(const SkillEntry& entry);

    // 获取所有活跃技能
    std::vector<SkillEntry> getActiveSkills() const;

    // 按 name 查找技能
    const SkillEntry* findSkill(const std::string& name) const;

    // Trie prefix completion for `/skills <prefix>`.
    std::vector<std::string> completeSkillNames(const std::string& prefix,
                                                size_t limit = 20) const;

    // 构建技能提示词（XML 格式，带字符预算裁剪）
    // 参考：openclaw-main/src/agents/skills/workspace.ts:resolveWorkspaceSkillPromptState
    std::string buildSkillPrompt(int charBudget = 18000) const;

    // 获取技能数量
    size_t size() const;

    // 清空
    void clear();

private:
    // 数据结构：HashMap 按 name 索引
    std::unordered_map<std::string, SkillEntry> m_skills;

    // 二分搜索：在排序后的技能列表中，找到不超过字符预算的最大前缀长度
    static int binarySearchPromptLimit(
        const std::vector<SkillEntry>& sorted,
        int charBudget
    );

    // 格式化单个技能为 XML
    static std::string formatSkillXml(const SkillEntry& entry, bool compact = false);
};

} // namespace clawlite
