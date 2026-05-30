// ClawLite — 系统提示词构建器实现
// TODO: C 同学实现

#include "llm/prompt_builder.h"
#include <sstream>

namespace clawlite {

std::string PromptBuilder::buildSystemPrompt(
    const PromptBuildContext& ctx,
    const SkillRegistry& skills,
    IContextEngine* memory
) {
    // TODO: 实现多段拼接
    //
    // 参考：openclaw-main/src/agents/pi-embedded-runner/system-prompt.ts:buildEmbeddedSystemPrompt
    //
    // 算法：
    //   sections = []
    //   sections.push_back(buildBaseSection(ctx))      — 身份描述
    //   sections.push_back(buildRuntimeSection(ctx))    — 运行时信息
    //   sections.push_back(buildSkillsSection(skills))  — 技能列表
    //   if (memory):
    //     sections.push_back(buildMemorySection(memory)) — 内存上下文
    //
    //   return join(sections, "\n\n")

    std::vector<std::string> sections;

    sections.push_back(buildBaseSection(ctx));
    sections.push_back(buildRuntimeSection(ctx));
    sections.push_back(buildSkillsSection(skills, ctx.skillCharBudget));

    (void)memory;

    // 拼接所有段落
    std::string prompt;
    for (size_t i = 0; i < sections.size(); i++) {
        if (!sections[i].empty()) {
            if (!prompt.empty()) prompt += "\n\n";
            prompt += sections[i];
        }
    }
    return prompt;
}

std::string PromptBuilder::buildToolsPrompt(const std::vector<Tool>& tools) {
    // TODO: 实现工具描述拼接
    // 格式：
    //   Available tools:
    //   - calculator: Evaluate mathematical expressions. Parameters: {"expr": "string"}
    //   - read_file: Read file contents. Parameters: {"path": "string"}
    std::string prompt = "Available tools:\n";
    for (const auto& tool : tools) {
        prompt += "- " + tool.name + ": " + tool.description + "\n";
    }
    return prompt;
}

std::string PromptBuilder::buildBaseSection(const PromptBuildContext& ctx) {
    // TODO: 实现基础身份描述
    if (!ctx.basePrompt.empty()) return ctx.basePrompt;
    return "You are ClawLite, a helpful AI assistant powered by " + ctx.model + ".";
}

std::string PromptBuilder::buildSkillsSection(const SkillRegistry& skills, int charBudget) {
    // 直接调用 SkillRegistry.buildSkillPrompt()
    return skills.buildSkillPrompt(charBudget);
}

std::string PromptBuilder::buildToolsSection(const std::vector<Tool>& tools) {
    return buildToolsPrompt(tools);
}

std::string PromptBuilder::buildMemorySection(IContextEngine* memory, int tokenBudget) {
    // TODO: 实现内存上下文注入
    //
    // 算法：
    //   1. auto result = memory->assemble(tokenBudget)
    //   2. 将 result.messages 格式化为文本
    //   3. 包装在 "Relevant context from memory:" 标签下
    if (!memory) return "";

    auto result = memory->assemble(tokenBudget);
    if (result.messages.empty()) return "";

    std::string section = "Relevant context from memory:\n";
    for (const auto& msg : result.messages) {
        section += "- " + msg.content + "\n";
    }
    return section;
}

std::string PromptBuilder::buildRuntimeSection(const PromptBuildContext& ctx) {
    // TODO: 实现运行时信息
    // 格式：
    //   Runtime info:
    //   - OS: windows
    //   - Workspace: D:\projects\myapp
    //   - Model: gpt-4o-mini
    //   - Current time: 2026-05-12 10:30:00
    std::string section = "Runtime info:\n";
    section += "- OS: " + ctx.os + "\n";
    section += "- Workspace: " + ctx.workspaceDir + "\n";
    section += "- Model: " + ctx.model + "\n";
    return section;
}

} // namespace clawlite
