#include "httplib.h"
#include "llm/harness.h"
#include "llm/llm_client.h"
#include "llm/prompt_builder.h"
#include "llm/tool_executor.h"
#include "skill/skill_filter.h"
#include "skill/skill_registry.h"

#include <cstdlib>
#include <iostream>

using namespace clawlite;

namespace {

std::string htmlPage() {
    return R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <title>ClawLite GUI</title>
  <style>
    body { font-family: system-ui, sans-serif; margin: 0; background: #f7f7f8; color: #202124; }
    main { max-width: 980px; margin: 32px auto; padding: 0 20px; }
    textarea { width: 100%; height: 130px; box-sizing: border-box; font: 15px/1.5 ui-monospace, monospace; }
    button { margin-top: 10px; padding: 8px 14px; }
    pre { white-space: pre-wrap; background: white; border: 1px solid #ddd; padding: 16px; min-height: 220px; }
  </style>
</head>
<body>
<main>
  <h1>ClawLite</h1>
  <textarea id="input">Use the calculator tool to compute 2 + 3 * (4 - 1).</textarea>
  <br><button onclick="send()">Send</button>
  <button onclick="loadSkills()">Skills</button>
  <button onclick="loadMemory()">Memory</button>
  <pre id="out"></pre>
</main>
<script>
async function send() {
  const out = document.getElementById('out');
  out.textContent = 'Thinking...';
  const res = await fetch('/chat', {method:'POST', body: document.getElementById('input').value});
  out.textContent = await res.text();
}
async function loadSkills() { document.getElementById('out').textContent = await (await fetch('/skills')).text(); }
async function loadMemory() { document.getElementById('out').textContent = await (await fetch('/memory')).text(); }
</script>
</body>
</html>
)HTML";
}

} // namespace

int main() {
    SkillRegistry skills;
    skills.loadFromWorkspace(".", SkillFilter::detectSystem());

    ToolExecutor tools;
    tools.registerBuiltinTools();

    LlmConfig config;
    config.mockMode = true;
    if (const char* key = std::getenv("CLAWLITE_API_KEY")) {
        config.apiKey = key;
        config.mockMode = false;
    }
    if (const char* base = std::getenv("CLAWLITE_BASE_URL")) config.baseUrl = base;
    if (const char* model = std::getenv("CLAWLITE_MODEL")) config.model = model;

    LlmClient llm(config);
    AgentHarness harness(llm, tools, nullptr);

    httplib::Server server;
    server.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(htmlPage(), "text/html; charset=utf-8");
    });
    server.Post("/chat", [&](const httplib::Request& req, httplib::Response& res) {
        PromptBuildContext ctx;
        ctx.basePrompt = "You are ClawLite, a helpful AI assistant.";
        ctx.workspaceDir = ".";
        ctx.model = config.mockMode ? "mock" : config.model;
        ctx.os = "windows";
        std::string prompt = PromptBuilder::buildSystemPrompt(ctx, skills, nullptr);
        auto result = harness.runTurn(prompt, {}, req.body);
        res.set_content(result.status == RunStatus::Success ? result.reply : result.error,
                        "text/plain; charset=utf-8");
    });
    server.Get("/skills", [&](const httplib::Request&, httplib::Response& res) {
        std::string body;
        for (const auto& skill : skills.getActiveSkills()) {
            body += skill.definition.name + " - " + skill.definition.description + "\n";
        }
        res.set_content(body, "text/plain; charset=utf-8");
    });
    server.Get("/memory", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("Memory endpoint is available; wire a ContextEngine instance here for project demos.\n",
                        "text/plain; charset=utf-8");
    });

    std::cout << "ClawLite GUI listening on http://localhost:18080\n";
    server.listen("0.0.0.0", 18080);
}
