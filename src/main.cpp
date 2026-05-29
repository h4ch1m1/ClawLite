// ClawLite — 主程序入口
// 交互式 REPL 循环

#include "core/types.h"
#include "core/config.h"
#include "skill/skill_registry.h"
#include "skill/skill_filter.h"
#include "memory/context_engine.h"
#include "llm/llm_client.h"
#include "llm/harness.h"
#include "llm/tool_executor.h"
#include "llm/prompt_builder.h"
#include <fstream>
#include <iostream>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif

using namespace clawlite;

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::cout << "========================================\n";
    std::cout << "  ClawLite Agent Runtime v0.1\n";
    std::cout << "  Data Structures Course Project\n";
    std::cout << "  Emoji test: 👋 🚀 💻 📄 ✅\n";
    std::cout << "========================================\n";
    std::cout << "\n";

    // ── 初始化各模块 ──────────────────────────────────────

   // 自动寻找项目根目录：先尝试当前目录，再尝试父目录
    std::string workspaceDir = ".";
    {
        std::ifstream testFile("./skills/hello/SKILL.md");
        if (!testFile.is_open()) {
            testFile.open("../skills/hello/SKILL.md");
            if (testFile.is_open()) {
                workspaceDir = "..";
            }
        }
    }

    // ── A: 技能注册表 ─────────────────────────────────
    SkillRegistry skillRegistry;
    SkillFilterConfig filterCfg = SkillFilter::detectSystem();
    skillRegistry.loadFromWorkspace(workspaceDir, filterCfg);
    // B: 上下文引擎（TODO: 实例化具体实现）
    // auto memory = ...;

    // C: LLM 客户端
    LlmConfig llmConfig = loadAppConfig().llm;
    LlmClient llm(llmConfig);

    // C: 工具执行器
    ToolExecutor tools;
    tools.registerBuiltinTools();

    // C: Agent 主循环
    AgentHarness harness(llm, tools, nullptr);

    // 显示状态
    std::cout << "Skills: " << skillRegistry.size() << " loaded\n";
    std::cout << "Tools:  " << tools.size() << " registered\n";
    std::cout << "\nType /help for commands, /quit to exit.\n\n";

    // ── REPL 循环 ─────────────────────────────────────────

    //std::string workspaceDir = ".";
    while (true) {
        std::cout << "> ";
        std::string input;
        if (!std::getline(std::cin, input)) break;
        if (input.empty()) continue;

        // 命令处理
        if (input[0] == '/') {
            if (input == "/quit" || input == "/exit") {
                std::cout << "Goodbye!\n";
                break;
            }
            else if (input == "/help") {
                std::cout << "Commands:\n";
                std::cout << "  /skills          - List loaded skills\n";
                std::cout << "  /tools           - List registered tools\n";
                std::cout << "  /memory status   - Show memory status\n";
                std::cout << "  /memory load <f> - Load file into memory\n";
                std::cout << "  /memory search q - Search memory\n";
                std::cout << "  /sessions        - List sessions\n";
                std::cout << "  /quit            - Exit\n";
            }
            else if (input == "/skills") {
                auto skills = skillRegistry.getActiveSkills();
                std::cout << "Loaded " << skills.size() << " skills:\n";
                for (const auto& s : skills) {
                    std::cout << "  - " << s.definition.name
                              << ": " << s.definition.description << "\n";
                }
            }
            else if (input == "/tools") {
                auto allTools = tools.getAllTools();
                std::cout << "Registered " << allTools.size() << " tools:\n";
                for (const auto& t : allTools) {
                    std::cout << "  - " << t.name << ": " << t.description << "\n";
                }
            }
            else if (input == "/memory status") {
                std::cout << "Memory: not initialized\n";
                // TODO: 显示 memory->getIndexedFileCount() 等
            }
            else {
                std::cout << "Unknown command: " << input << "\n";
            }
            continue;
        }

        // 普通消息 → 发送给 Agent
        std::cout << "[Agent] Thinking...\n";

        PromptBuildContext promptCtx;
        promptCtx.basePrompt = "You are ClawLite, a helpful AI assistant.";
        promptCtx.workspaceDir = workspaceDir;
        promptCtx.model = llmConfig.model;
        promptCtx.os = "windows";

        std::string systemPrompt = PromptBuilder::buildSystemPrompt(
            promptCtx, skillRegistry, nullptr
        );

        const auto allTools = tools.getAllTools();
        if (!allTools.empty()) {
            systemPrompt += "\n\n";
            systemPrompt += PromptBuilder::buildToolsPrompt(allTools);
            systemPrompt += "\nTool-use policy:\n";
            systemPrompt += "- Use multiple tools in one assistant turn when the user asks for multiple independent tasks.\n";
            systemPrompt += "- If the user message contains both a greeting and another task, call the hello tool and the task tool(s).\n";
        }

        std::vector<Message> history;  // TODO: 从 session 获取
        auto result = harness.runTurn(systemPrompt, history, input);

        if (result.status == RunStatus::Success) {
            std::cout << "[Agent] " << result.reply << "\n";
            if (result.totalTurns > 0) {
                std::cout << "  (" << result.totalTurns << " tool calls)\n";
            }
        } else {
            std::cout << "[Error] " << result.error << "\n";
        }

        std::cout << "\n";
    }

    return 0;
}
