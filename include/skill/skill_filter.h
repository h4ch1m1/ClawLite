#pragma once
// ClawLite — 技能资格过滤器
// 参考：openclaw-main/src/agents/skills/config.ts:shouldIncludeSkill
// 数据结构：set<string> 存储可用的二进制程序名

#include "skill/skill_loader.h"
#include <set>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>

namespace clawlite {

struct SkillFilterConfig {
    std::set<std::string> availableBins;   // 系统中可用的命令（如 curl, python）
    std::set<std::string> availableEnvs;   // 已设置的环境变量名
    std::string os;                         // 当前 OS（"windows", "linux", "macos"）
};

class SkillFilter {
public:
    // 检查一个技能是否符合资格
    // 参考：openclaw-main/src/agents/skills/config.ts:shouldIncludeSkill
    // 检查项：OS 兼容性、依赖二进制存在性、环境变量存在性
    static bool isEligible(const SkillDefinition& skill, const SkillFilterConfig& config);

    // 过滤技能列表，移除不符合资格的
    static std::vector<SkillEntry> filterSkills(
        const std::vector<SkillEntry>& skills,
        const SkillFilterConfig& config
    );

    // 自动检测当前系统的 filter config
    static SkillFilterConfig detectSystem();

private:
    // 检查 OS 兼容性
    static bool checkOs(const SkillDefinition& skill, const std::string& currentOs);

    // 检查依赖二进制
    static bool checkBins(const SkillDefinition& skill, const std::set<std::string>& available);

    // 检查环境变量
    static bool checkEnvs(const SkillDefinition& skill, const std::set<std::string>& available);
};

} // namespace clawlite
