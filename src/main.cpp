// ClawLite — 主程序入口
// 精简最终版：围绕文件树、结构化分块、摘要树和上下文预算做 CLI 演示。

#include "core/config.h"
#include "core/types.h"
#include "llm/harness.h"
#include "llm/llm_client.h"
#include "llm/prompt_builder.h"
#include "llm/runtime_plan.h"
#include "llm/tool_executor.h"
#include "memory/context_engine.h"
#include "skill/skill_filter.h"
#include "skill/skill_registry.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace clawlite;

namespace {

ContextEngineOptions toContextOptions(const MemoryConfig& config) {
    ContextEngineOptions options;
    options.chunkTokens = config.chunkTokens;
    options.overlapTokens = config.overlapTokens;
    options.summaryGroupSize = config.summaryGroupSize;
    options.keepRecentTurns = config.keepRecentTurns;
    options.fileCacheCapacity = config.fileCacheCapacity;
    return options;
}

std::string detectWorkspace(const std::string& configured) {
    if (!configured.empty() && configured != ".") return configured;
    std::ifstream here("./skills/hello/SKILL.md");
    if (here.is_open()) return ".";
    std::ifstream parent("../skills/hello/SKILL.md");
    if (parent.is_open()) return "..";
    return ".";
}

void printHelp() {
    std::cout << "Commands:\n";
    std::cout << "  /skills          - List loaded skills\n";
    std::cout << "  /tools           - List registered tools\n";
    std::cout << "  /memory status   - Show tree/chunk/summary/cache stats\n";
    std::cout << "  /index <path>    - Index one file or a directory\n";
    std::cout << "  /search <q>      - Search indexed chunks\n";
    std::cout << "  /context <q>     - Show budgeted long-context assembly\n";
    std::cout << "  /compact         - Compact session into summary nodes\n";
    std::cout << "  /ask <prompt>    - Ask through the MiMo-compatible harness\n";
    std::cout << "  /quit            - Exit\n";
}

void printStats(IContextEngine& memory, const std::string& dataDir) {
    auto stats = memory.getMemoryStats();
    std::cout << "Memory:\n";
    std::cout << "  files:     " << stats.indexedFiles << "\n";
    std::cout << "  chunks:    " << stats.indexedChunks << "\n";
    std::cout << "  treeNodes: " << stats.treeNodes << "\n";
    std::cout << "  rootHash:  " << (stats.rootHash.empty() ? "(none)" : stats.rootHash) << "\n";
    std::cout << "  summaries: " << stats.summaryNodes << "\n";
    std::cout << "  cache:     hits=" << stats.cacheHits << " misses=" << stats.cacheMisses << "\n";
    std::cout << "  data:      " << dataDir << "\n";
}

std::string buildSystemPrompt(const std::string& workspaceDir,
                              const LlmConfig& llmConfig,
                              const SkillRegistry& skills,
                              const ToolExecutor& tools) {
    PromptBuildContext promptCtx;
    promptCtx.basePrompt = "You are ClawLite, a concise assistant for a data structures course project.";
    promptCtx.workspaceDir = workspaceDir;
    promptCtx.model = llmConfig.model;
    promptCtx.os = "windows";

    std::string systemPrompt = PromptBuilder::buildSystemPrompt(promptCtx, skills, nullptr);
    const auto allTools = tools.getAllTools();
    if (!allTools.empty()) {
        systemPrompt += "\n\n";
        systemPrompt += PromptBuilder::buildToolsPrompt(allTools);
        systemPrompt += "\nTool-use policy:\n";
        systemPrompt += "- Use tools only when they materially help the request.\n";
        systemPrompt += "- Keep answers concise and grounded in indexed context when available.\n";
    }
    return systemPrompt;
}

RunResult askAgent(AgentHarness& harness,
                   const std::string& systemPrompt,
                   const std::string& input,
                   const AppConfig& config) {
    RuntimePlan plan = RuntimePlan::defaultPlan();
    plan.prompt.contextTokenBudget = config.memory.contextTokenBudget;
    plan.transport.maxTokens = config.llm.maxTokens;
    plan.transport.timeoutMs = config.llm.timeoutMs;
    plan.transport.temperature = config.llm.temperature;
    return harness.runTurn(systemPrompt, {}, input, plan);
}

} // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    AppConfig appConfig = loadAppConfig();
    std::string workspaceDir = detectWorkspace(appConfig.workspaceDir);
    std::string dataDir = appConfig.dataDir.empty() ? workspaceDir + "/.clawlite" : appConfig.dataDir;

    std::cout << "========================================\n";
    std::cout << "  ClawLite Data Structures Runtime\n";
    std::cout << "  Core + CLI final edition\n";
    std::cout << "========================================\n\n";

    SkillRegistry skillRegistry;
    skillRegistry.loadFromWorkspace(workspaceDir, SkillFilter::detectSystem());

    auto memory = createContextEngine(toContextOptions(appConfig.memory));
    std::filesystem::create_directories(dataDir);
    memory->initialize(dataDir);
    memory->createSession("agent:main:cli:user:default");

    LlmClient llm(appConfig.llm);
    ToolExecutor tools;
    tools.registerBuiltinTools();
    AgentHarness harness(llm, tools, memory.get());

    std::cout << "Workspace: " << workspaceDir << "\n";
    std::cout << "Config:    clawlite_config.env + environment overrides\n";
    std::cout << "Model:     " << appConfig.llm.model << "\n";
    std::cout << "Skills:    " << skillRegistry.size() << "\n";
    std::cout << "Tools:     " << tools.size() << "\n";
    std::cout << "\nType /help for commands, /quit to exit.\n\n";

    while (true) {
        std::cout << "> ";
        std::string input;
        if (!std::getline(std::cin, input)) break;
        if (input.empty()) continue;

        if (input == "/quit" || input == "/exit") {
            std::cout << "Goodbye!\n";
            break;
        }
        if (input == "/help") {
            printHelp();
            continue;
        }
        if (input == "/skills") {
            auto skills = skillRegistry.getActiveSkills();
            std::cout << "Loaded " << skills.size() << " skills:\n";
            for (const auto& s : skills) {
                std::cout << "  - " << s.definition.name << ": " << s.definition.description << "\n";
            }
            continue;
        }
        if (input == "/tools") {
            auto allTools = tools.getAllTools();
            std::cout << "Registered " << allTools.size() << " tools:\n";
            for (const auto& t : allTools) {
                std::cout << "  - " << t.name << ": " << t.description << "\n";
            }
            continue;
        }
        if (input == "/memory status") {
            printStats(*memory, dataDir);
            continue;
        }
        if (input.rfind("/index ", 0) == 0 || input.rfind("/memory load ", 0) == 0) {
            std::string path = input.rfind("/index ", 0) == 0
                ? input.substr(std::string("/index ").size())
                : input.substr(std::string("/memory load ").size());
            memory->indexPath(path);
            std::cout << "Indexed " << path << "\n";
            printStats(*memory, dataDir);
            continue;
        }
        if (input.rfind("/search ", 0) == 0 || input.rfind("/memory search ", 0) == 0) {
            std::string query = input.rfind("/search ", 0) == 0
                ? input.substr(std::string("/search ").size())
                : input.substr(std::string("/memory search ").size());
            auto results = memory->search(query, 5);
            std::cout << "Search results: " << results.size() << "\n";
            for (const auto& r : results) {
                std::cout << "  - " << r.path << ":" << r.startLine << "-"
                          << r.endLine << " score=" << r.score << "\n";
                if (!r.headingPath.empty()) std::cout << "    heading: " << r.headingPath << "\n";
                std::cout << "    " << r.snippet << "\n";
            }
            continue;
        }
        if (input.rfind("/context ", 0) == 0) {
            std::string query = input.substr(std::string("/context ").size());
            auto assembled = memory->assembleForQuery(query, appConfig.memory.contextTokenBudget);
            std::cout << "Context tokens: " << assembled.estimatedTokens << "\n";
            for (const auto& line : assembled.trace) {
                std::cout << "  - " << line << "\n";
            }
            continue;
        }
        if (input == "/compact") {
            auto result = memory->compact(appConfig.memory.contextTokenBudget / 2);
            std::cout << "Compaction: " << result.reason << "\n";
            std::cout << "  before: " << result.tokensBefore << "\n";
            std::cout << "  after:  " << result.tokensAfter << "\n";
            if (!result.summary.empty()) std::cout << "  summary: " << result.summary << "\n";
            continue;
        }
        if (input.rfind("/ask ", 0) == 0) {
            input = input.substr(std::string("/ask ").size());
        } else if (!input.empty() && input[0] == '/') {
            std::cout << "Unknown command: " << input << "\n";
            continue;
        }

        std::cout << "[Agent] Thinking...\n";
        std::string systemPrompt = buildSystemPrompt(workspaceDir, appConfig.llm, skillRegistry, tools);
        auto result = askAgent(harness, systemPrompt, input, appConfig);
        if (result.status == RunStatus::Success) {
            std::cout << "[Agent] " << result.reply << "\n";
            if (result.totalTurns > 0) std::cout << "  (" << result.totalTurns << " tool calls)\n";
        } else {
            std::cout << "[Error] " << result.error << "\n";
        }
        std::cout << "\n";
    }

    return 0;
}
