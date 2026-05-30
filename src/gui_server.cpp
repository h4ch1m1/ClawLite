#include "httplib.h"
#include "core/config.h"
#include "llm/harness.h"
#include "llm/llm_client.h"
#include "llm/prompt_builder.h"
#include "llm/tool_executor.h"
#include "memory/context_engine.h"
#include "skill/skill_filter.h"
#include "skill/skill_registry.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

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
    * { box-sizing: border-box; }
    body { font-family: system-ui, sans-serif; margin: 0; background: #f5f6f8; color: #202124; }
    main { max-width: 1440px; margin: 0 auto; padding: 20px; }
    header { display: flex; align-items: center; justify-content: space-between; gap: 16px; margin-bottom: 14px; }
    h1 { font-size: 24px; margin: 0; }
    .layout { display: grid; grid-template-columns: minmax(0, 1.4fr) 360px; gap: 16px; align-items: start; }
    .panel { background: white; border: 1px solid #d9dde4; border-radius: 8px; padding: 14px; }
    textarea {
      width: 100%;
      min-height: 58vh;
      max-height: 72vh;
      resize: vertical;
      border: 1px solid #c8ced8;
      border-radius: 8px;
      padding: 14px;
      font: 14px/1.55 ui-monospace, SFMono-Regular, Consolas, monospace;
      background: #fbfcfe;
    }
    button, .file-label {
      border: 1px solid #b8c0cc;
      background: #fff;
      border-radius: 6px;
      padding: 8px 12px;
      cursor: pointer;
      font-size: 14px;
    }
    button.primary { background: #111827; color: #fff; border-color: #111827; }
    .toolbar { display: flex; flex-wrap: wrap; gap: 8px; margin-top: 10px; }
    input[type="file"] { display: none; }
    pre { white-space: pre-wrap; overflow: auto; margin: 0; font: 13px/1.5 ui-monospace, Consolas, monospace; }
    .out { min-height: 260px; max-height: 54vh; }
    .commands { display: grid; gap: 8px; }
    .cmd { text-align: left; display: block; width: 100%; }
    .cmd small { display: block; color: #667085; margin-top: 2px; }
    .stats { color: #344054; margin: 0 0 10px; }
    @media (max-width: 900px) {
      .layout { grid-template-columns: 1fr; }
      textarea { min-height: 48vh; }
    }
  </style>
</head>
<body>
<main>
  <header>
    <h1>ClawLite</h1>
    <div id="stats" class="stats">Loading memory...</div>
  </header>
  <div class="layout">
    <section class="panel">
      <textarea id="input" spellcheck="false" placeholder="Paste a large file, long prompt, or type / to open tools.">Use the calculator tool to compute 2 + 3 * (4 - 1).</textarea>
      <div class="toolbar">
        <button class="primary" onclick="send()">Send</button>
        <label class="file-label" for="file">Import text</label>
        <input id="file" type="file" onchange="loadLocalFile(event)">
        <button onclick="indexCurrentInput()">Index input</button>
        <button onclick="loadSkills()">Skills</button>
        <button onclick="loadMemory()">Memory</button>
      </div>
    </section>
    <aside class="panel">
      <div class="commands">
        <button class="cmd" onclick="insertCommand('/memory search ')">/memory search<small>Search indexed large files</small></button>
        <button class="cmd" onclick="indexCurrentInput()">/memory index-input<small>Write current large input to the memory index</small></button>
        <button class="cmd" onclick="loadSkills()">/skills<small>Show loaded skills</small></button>
        <button class="cmd" onclick="loadMemory()">/memory status<small>Show file and chunk counts</small></button>
      </div>
      <hr>
      <pre id="out" class="out"></pre>
    </aside>
  </div>
</main>
<script>
let importedName = 'pasted-input.md';

async function send() {
  const out = document.getElementById('out');
  out.textContent = 'Thinking...';
  const res = await fetch('/chat', {method:'POST', body: document.getElementById('input').value});
  out.textContent = await res.text();
  loadMemory();
}
async function loadSkills() { document.getElementById('out').textContent = await (await fetch('/skills')).text(); }
async function loadMemory() {
  const text = await (await fetch('/memory')).text();
  document.getElementById('stats').textContent = text.replace(/\n/g, ' | ');
  document.getElementById('out').textContent = text;
}
async function indexCurrentInput() {
  const out = document.getElementById('out');
  out.textContent = 'Indexing current input...';
  const res = await fetch('/memory/index-text?name=' + encodeURIComponent(importedName), {
    method: 'POST',
    body: document.getElementById('input').value
  });
  out.textContent = await res.text();
  loadMemory();
}
async function loadLocalFile(event) {
  const file = event.target.files[0];
  if (!file) return;
  importedName = file.name || 'imported.txt';
  document.getElementById('input').value = await file.text();
}
function insertCommand(text) {
  const input = document.getElementById('input');
  input.value = text;
  input.focus();
}
document.getElementById('input').addEventListener('keydown', async (event) => {
  if (event.key === 'Enter' && event.ctrlKey) send();
});
loadMemory();
</script>
</body>
</html>
)HTML";
}

} // namespace

int main() {
    std::filesystem::create_directories(".clawlite");

    SkillRegistry skills;
    skills.loadFromWorkspace(".", SkillFilter::detectSystem());

    ToolExecutor tools;
    tools.registerBuiltinTools();

    LlmConfig config = loadAppConfig().llm;

    auto memory = createContextEngine();
    memory->initialize(".clawlite");
    memory->createSession("agent:main:gui:user:default");

    LlmClient llm(config);
    AgentHarness harness(llm, tools, memory.get());

    httplib::Server server;
    server.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(htmlPage(), "text/html; charset=utf-8");
    });
    server.Post("/chat", [&](const httplib::Request& req, httplib::Response& res) {
        PromptBuildContext ctx;
        ctx.basePrompt = "You are ClawLite, a helpful AI assistant.";
        ctx.workspaceDir = ".";
        ctx.model = config.model;
        ctx.os = "windows";
        std::string prompt = PromptBuilder::buildSystemPrompt(ctx, skills, nullptr);
        const auto allTools = tools.getAllTools();
        if (!allTools.empty()) {
            prompt += "\n\n" + PromptBuilder::buildToolsPrompt(allTools);
        }
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
    server.Get("/memory", [&](const httplib::Request&, httplib::Response& res) {
        std::ostringstream body;
        body << "files: " << memory->getIndexedFileCount() << "\n";
        body << "chunks: " << memory->getIndexedChunkCount() << "\n";
        body << "data: .clawlite\n";
        res.set_content(body.str(), "text/plain; charset=utf-8");
    });
    server.Post("/memory/index-text", [&](const httplib::Request& req, httplib::Response& res) {
        std::string name = req.has_param("name") ? req.get_param_value("name") : "pasted-input.md";
        for (char& ch : name) {
            if (ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?' || ch == '"' ||
                ch == '<' || ch == '>' || ch == '|') {
                ch = '_';
            }
        }
        std::filesystem::create_directories(".clawlite/uploads");
        std::string path = ".clawlite/uploads/" + name;
        {
            std::ofstream out(path, std::ios::binary);
            out << req.body;
        }
        memory->indexFile(path);
        std::ostringstream body;
        body << "Indexed " << path << "\n";
        body << "bytes: " << req.body.size() << "\n";
        body << "files: " << memory->getIndexedFileCount() << "\n";
        body << "chunks: " << memory->getIndexedChunkCount() << "\n";
        res.set_content(body.str(), "text/plain; charset=utf-8");
    });
    server.Post("/memory/load", [&](const httplib::Request& req, httplib::Response& res) {
        memory->indexFile(req.body);
        std::ostringstream body;
        body << "Indexed " << req.body << "\n";
        body << "files: " << memory->getIndexedFileCount() << "\n";
        body << "chunks: " << memory->getIndexedChunkCount() << "\n";
        res.set_content(body.str(), "text/plain; charset=utf-8");
    });
    server.Get("/memory/search", [&](const httplib::Request& req, httplib::Response& res) {
        std::string query = req.has_param("q") ? req.get_param_value("q") : "";
        auto results = memory->search(query, 8);
        std::ostringstream body;
        body << "query: " << query << "\n";
        body << "results: " << results.size() << "\n\n";
        for (const auto& r : results) {
            body << r.path << ":" << r.startLine << "-" << r.endLine
                 << " score=" << r.score << "\n";
            body << r.snippet << "\n\n";
        }
        res.set_content(body.str(), "text/plain; charset=utf-8");
    });

    std::cout << "ClawLite GUI listening on http://localhost:18080\n";
    server.listen("0.0.0.0", 18080);
}
