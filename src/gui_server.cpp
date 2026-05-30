#include "httplib.h"
#include "core/config.h"
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
#include <sstream>
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
    if (std::ifstream("./skills/hello/SKILL.md").is_open()) return ".";
    if (std::ifstream("../skills/hello/SKILL.md").is_open()) return "..";
    return ".";
}

std::string jsonEscape(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 16);
    for (unsigned char c : input) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    const char* hex = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(hex[(c >> 4) & 0xF]);
                    out.push_back(hex[c & 0xF]);
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    return out;
}

std::string jsonString(const std::string& value) {
    return "\"" + jsonEscape(value) + "\"";
}

std::string buildSystemPrompt(const std::string& workspaceDir,
                              const LlmConfig& llmConfig,
                              const SkillRegistry& skills,
                              const ToolExecutor& tools) {
    PromptBuildContext ctx;
    ctx.basePrompt =
        "You are ClawLite, a concise assistant for a data structures course project. "
        "Answer in the user's language. When indexed context is relevant, cite the structure "
        "you used briefly: file tree, heading chunks, budget queue, or summary tree.";
    ctx.workspaceDir = workspaceDir;
    ctx.model = llmConfig.model;
    ctx.os = "windows";

    std::string prompt = PromptBuilder::buildSystemPrompt(ctx, skills, nullptr);
    const auto allTools = tools.getAllTools();
    if (!allTools.empty()) {
        prompt += "\n\n" + PromptBuilder::buildToolsPrompt(allTools);
        prompt += "\nTool-use policy:\n";
        prompt += "- Use tools only when they materially help the request.\n";
        prompt += "- Prefer a short direct answer for ordinary chat.\n";
    }
    return prompt;
}

RuntimePlan makePlan(const AppConfig& config) {
    RuntimePlan plan = RuntimePlan::defaultPlan();
    plan.prompt.contextTokenBudget = config.memory.contextTokenBudget;
    plan.transport.maxTokens = config.llm.maxTokens;
    plan.transport.timeoutMs = config.llm.timeoutMs;
    plan.transport.temperature = config.llm.temperature;
    return plan;
}

std::string statusJson(IContextEngine& memory,
                       const AppConfig& config,
                       const std::string& workspaceDir,
                       const std::string& dataDir) {
    auto stats = memory.getMemoryStats();
    std::ostringstream out;
    out << "{"
        << "\"ok\":true,"
        << "\"model\":" << jsonString(config.llm.model) << ","
        << "\"baseUrl\":" << jsonString(config.llm.baseUrl) << ","
        << "\"apiKeyReady\":" << (config.llm.apiKey.empty() ? "false" : "true") << ","
        << "\"workspace\":" << jsonString(workspaceDir) << ","
        << "\"dataDir\":" << jsonString(dataDir) << ","
        << "\"files\":" << stats.indexedFiles << ","
        << "\"chunks\":" << stats.indexedChunks << ","
        << "\"treeNodes\":" << stats.treeNodes << ","
        << "\"summaries\":" << stats.summaryNodes << ","
        << "\"cacheHits\":" << stats.cacheHits << ","
        << "\"cacheMisses\":" << stats.cacheMisses << ","
        << "\"rootHash\":" << jsonString(stats.rootHash)
        << "}";
    return out.str();
}

std::string htmlPage() {
    return R"HTML(
<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ClawLite</title>
  <style>
    * { box-sizing: border-box; }
    :root {
      color-scheme: light;
      --bg: #f7f4ee;
      --surface: #fbfaf7;
      --line: #ded8cc;
      --text: #28241f;
      --muted: #756e63;
      --accent: #8f4f2a;
      --accent-strong: #6e351e;
      --bubble: #ffffff;
      --assistant: #f0ebe3;
      --danger: #9f2d20;
    }
    body {
      margin: 0;
      min-height: 100vh;
      font-family: ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      background: var(--bg);
      color: var(--text);
    }
    .app {
      display: grid;
      grid-template-columns: 260px minmax(0, 1fr);
      height: 100vh;
    }
    aside {
      border-right: 1px solid var(--line);
      background: #eee8dd;
      padding: 18px 14px;
      display: flex;
      flex-direction: column;
      gap: 14px;
    }
    .brand {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 10px;
      padding: 0 4px 10px;
      border-bottom: 1px solid var(--line);
    }
    .brand strong { font-size: 18px; }
    .pill {
      font-size: 12px;
      color: var(--muted);
      border: 1px solid var(--line);
      border-radius: 999px;
      padding: 3px 8px;
      background: rgba(255,255,255,.45);
    }
    .side-title {
      margin: 6px 4px 2px;
      font-size: 12px;
      color: var(--muted);
      text-transform: uppercase;
      letter-spacing: .06em;
    }
    .cmd, .icon-btn {
      width: 100%;
      border: 1px solid transparent;
      background: transparent;
      color: var(--text);
      border-radius: 8px;
      padding: 9px 10px;
      cursor: pointer;
      text-align: left;
      font: inherit;
    }
    .cmd:hover, .icon-btn:hover { background: rgba(255,255,255,.55); border-color: var(--line); }
    .cmd small { display: block; color: var(--muted); margin-top: 2px; line-height: 1.35; }
    .status {
      margin-top: auto;
      border-top: 1px solid var(--line);
      padding: 12px 4px 0;
      color: var(--muted);
      font-size: 13px;
      line-height: 1.6;
    }
    main {
      min-width: 0;
      height: 100vh;
      display: flex;
      flex-direction: column;
      background: var(--surface);
    }
    .topbar {
      height: 58px;
      border-bottom: 1px solid var(--line);
      display: flex;
      align-items: center;
      justify-content: space-between;
      padding: 0 22px;
      gap: 14px;
    }
    .topbar h1 { margin: 0; font-size: 16px; font-weight: 650; }
    .topbar span { color: var(--muted); font-size: 13px; }
    .thread {
      flex: 1;
      overflow: auto;
      padding: 28px 18px 18px;
    }
    .messages {
      width: min(840px, 100%);
      margin: 0 auto;
      display: grid;
      gap: 18px;
    }
    .message {
      display: grid;
      grid-template-columns: 34px minmax(0, 1fr);
      gap: 12px;
      align-items: start;
    }
    .avatar {
      width: 34px;
      height: 34px;
      border-radius: 50%;
      display: grid;
      place-items: center;
      font-weight: 700;
      color: #fff;
      background: var(--accent);
    }
    .message.user .avatar { background: #2f5d62; }
    .bubble {
      border: 1px solid var(--line);
      border-radius: 8px;
      padding: 13px 15px;
      background: var(--bubble);
      line-height: 1.65;
      white-space: pre-wrap;
      overflow-wrap: anywhere;
    }
    .message.assistant .bubble { background: var(--assistant); }
    .message.error .avatar { background: var(--danger); }
    .message.error .bubble { border-color: #e3aaa3; color: var(--danger); background: #fff7f5; }
    .composer-wrap {
      border-top: 1px solid var(--line);
      background: rgba(251,250,247,.94);
      padding: 14px 18px 18px;
    }
    .composer {
      width: min(840px, 100%);
      margin: 0 auto;
      border: 1px solid var(--line);
      border-radius: 8px;
      background: #fff;
      overflow: hidden;
    }
    textarea {
      width: 100%;
      display: block;
      min-height: 78px;
      max-height: 220px;
      resize: vertical;
      border: 0;
      padding: 14px 15px;
      outline: none;
      font: 15px/1.55 inherit;
      color: var(--text);
      background: transparent;
    }
    .composer-actions {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 10px;
      border-top: 1px solid var(--line);
      padding: 8px;
    }
    .left-actions, .right-actions { display: flex; align-items: center; gap: 8px; }
    button {
      border: 1px solid var(--line);
      background: #fff;
      color: var(--text);
      border-radius: 8px;
      padding: 8px 11px;
      cursor: pointer;
      font: inherit;
    }
    button.primary {
      background: var(--accent);
      border-color: var(--accent);
      color: #fff;
      min-width: 76px;
    }
    button.primary:hover { background: var(--accent-strong); }
    button:disabled { opacity: .55; cursor: wait; }
    input[type=file] { display: none; }
    .toast {
      position: fixed;
      right: 18px;
      bottom: 18px;
      max-width: 420px;
      border: 1px solid var(--line);
      border-radius: 8px;
      padding: 10px 12px;
      background: #fff;
      color: var(--muted);
      box-shadow: 0 10px 30px rgba(40,36,31,.12);
      display: none;
    }
    @media (max-width: 820px) {
      .app { grid-template-columns: 1fr; }
      aside { display: none; }
      .topbar { padding: 0 14px; }
      .thread { padding: 18px 12px 12px; }
      .composer-wrap { padding: 10px 12px 12px; }
    }
  </style>
</head>
<body>
<div class="app">
  <aside>
    <div class="brand"><strong>ClawLite</strong><span class="pill" id="ready">checking</span></div>
    <div>
      <div class="side-title">Memory</div>
      <button class="cmd" onclick="indexCurrentInput()">Index input<small>写入结构化 chunk 和文件树</small></button>
      <button class="cmd" onclick="showContext()">Context view<small>展示预算队列选中的上下文</small></button>
      <button class="cmd" onclick="compact()">Compact<small>把旧对话折叠进摘要树</small></button>
    </div>
    <div>
      <div class="side-title">Session</div>
      <button class="cmd" onclick="resetChat()">New chat<small>清空页面和本轮 harness 状态</small></button>
      <label class="cmd" for="file">Import text<small>导入本地文本到输入框</small></label>
      <input id="file" type="file" onchange="loadLocalFile(event)">
    </div>
    <div class="status" id="status">Loading...</div>
  </aside>
  <main>
    <div class="topbar">
      <div>
        <h1>ClawLite Chat</h1>
        <span id="subtitle">长上下文数据结构演示</span>
      </div>
      <button onclick="refreshStatus()">Status</button>
    </div>
    <section class="thread" id="thread">
      <div class="messages" id="messages">
        <div class="message assistant">
          <div class="avatar">C</div>
          <div class="bubble">你好，我是 ClawLite。你可以直接提问，也可以先导入/索引文本，再让我基于文件树、标题 chunk 和上下文预算器回答。</div>
        </div>
      </div>
    </section>
    <section class="composer-wrap">
      <div class="composer">
        <textarea id="input" spellcheck="false" placeholder="输入问题，Ctrl+Enter 发送"></textarea>
        <div class="composer-actions">
          <div class="left-actions">
            <button type="button" onclick="document.getElementById('file').click()">Import</button>
            <button onclick="indexCurrentInput()">Index</button>
          </div>
          <div class="right-actions">
            <button class="primary" id="sendBtn" onclick="send()">Send</button>
          </div>
        </div>
      </div>
    </section>
  </main>
</div>
<div class="toast" id="toast"></div>
<script>
let importedName = 'pasted-input.md';
let busy = false;

function escapeHtml(text) {
  return String(text).replace(/[&<>"']/g, ch => ({
    '&':'&amp;', '<':'&lt;', '>':'&gt;', '"':'&quot;', "'":'&#39;'
  }[ch]));
}
function addMessage(role, text) {
  const messages = document.getElementById('messages');
  const node = document.createElement('div');
  node.className = 'message ' + role;
  const label = role === 'user' ? 'U' : (role === 'error' ? '!' : 'C');
  node.innerHTML = '<div class="avatar">' + label + '</div><div class="bubble">' + escapeHtml(text) + '</div>';
  messages.appendChild(node);
  document.getElementById('thread').scrollTop = document.getElementById('thread').scrollHeight;
  return node;
}
function toast(text) {
  const el = document.getElementById('toast');
  el.textContent = text;
  el.style.display = 'block';
  clearTimeout(window.toastTimer);
  window.toastTimer = setTimeout(() => el.style.display = 'none', 3600);
}
async function refreshStatus() {
  const data = await (await fetch('/api/status')).json();
  document.getElementById('ready').textContent = data.apiKeyReady ? 'api ready' : 'no api key';
  document.getElementById('subtitle').textContent = data.model + ' · ' + data.workspace;
  document.getElementById('status').innerHTML =
    'files: ' + data.files + '<br>chunks: ' + data.chunks +
    '<br>tree nodes: ' + data.treeNodes + '<br>summaries: ' + data.summaries +
    '<br>cache: ' + data.cacheHits + '/' + data.cacheMisses +
    '<br>hash: ' + (data.rootHash || '(none)');
}
async function send() {
  if (busy) return;
  const input = document.getElementById('input');
  const text = input.value.trim();
  if (!text) return;
  busy = true;
  document.getElementById('sendBtn').disabled = true;
  addMessage('user', text);
  input.value = '';
  const pending = addMessage('assistant', 'Thinking...');
  try {
    const res = await fetch('/api/chat', {method:'POST', body:text});
    const data = await res.json();
    pending.remove();
    if (data.ok) addMessage('assistant', data.reply || '(empty response)');
    else addMessage('error', data.error || 'request failed');
  } catch (err) {
    pending.remove();
    addMessage('error', String(err));
  } finally {
    busy = false;
    document.getElementById('sendBtn').disabled = false;
    refreshStatus();
  }
}
async function indexCurrentInput() {
  const text = document.getElementById('input').value;
  if (!text.trim()) { toast('输入框为空，无法索引'); return; }
  const res = await fetch('/api/index-text?name=' + encodeURIComponent(importedName), {method:'POST', body:text});
  const data = await res.json();
  toast(data.ok ? ('Indexed ' + data.chunks + ' chunks') : data.error);
  refreshStatus();
}
async function showContext() {
  const query = document.getElementById('input').value.trim() || '数据结构';
  const data = await (await fetch('/api/context?q=' + encodeURIComponent(query))).json();
  addMessage(data.ok ? 'assistant' : 'error', data.ok ? data.text : data.error);
}
async function compact() {
  const data = await (await fetch('/api/compact', {method:'POST'})).json();
  addMessage(data.ok ? 'assistant' : 'error', data.ok ? data.text : data.error);
  refreshStatus();
}
async function resetChat() {
  await fetch('/api/reset', {method:'POST'});
  document.getElementById('messages').innerHTML = '';
  addMessage('assistant', '新对话已开始。');
  refreshStatus();
}
async function loadLocalFile(event) {
  const file = event.target.files[0];
  if (!file) return;
  importedName = file.name || 'imported.txt';
  document.getElementById('input').value = await file.text();
  toast('Loaded ' + importedName);
}
document.getElementById('input').addEventListener('keydown', event => {
  if (event.key === 'Enter' && event.ctrlKey) send();
});
refreshStatus();
</script>
</body>
</html>
)HTML";
}

} // namespace

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    AppConfig appConfig = loadAppConfig();
    std::string workspaceDir = detectWorkspace(appConfig.workspaceDir);
    std::string dataDir = appConfig.dataDir.empty() ? workspaceDir + "/.clawlite" : appConfig.dataDir;
    std::filesystem::create_directories(dataDir);

    SkillRegistry skills;
    skills.loadFromWorkspace(workspaceDir, SkillFilter::detectSystem());

    ToolExecutor tools;
    tools.registerBuiltinTools();

    auto memory = createContextEngine(toContextOptions(appConfig.memory));
    memory->initialize(dataDir);
    memory->createSession("agent:main:gui:user:default");

    LlmClient llm(appConfig.llm);
    AgentHarness harness(llm, tools, memory.get());

    httplib::Server server;
    server.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(htmlPage(), "text/html; charset=utf-8");
    });

    server.Get("/api/status", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content(statusJson(*memory, appConfig, workspaceDir, dataDir),
                        "application/json; charset=utf-8");
    });

    server.Post("/api/chat", [&](const httplib::Request& req, httplib::Response& res) {
        std::string systemPrompt = buildSystemPrompt(workspaceDir, appConfig.llm, skills, tools);
        auto result = harness.runTurn(systemPrompt, {}, req.body, makePlan(appConfig));
        std::ostringstream body;
        body << "{";
        if (result.status == RunStatus::Success) {
            body << "\"ok\":true,\"reply\":" << jsonString(result.reply)
                 << ",\"turns\":" << result.totalTurns
                 << ",\"tokens\":" << result.totalTokens;
        } else {
            body << "\"ok\":false,\"error\":" << jsonString(result.error);
        }
        body << "}";
        res.set_content(body.str(), "application/json; charset=utf-8");
    });

    server.Post("/api/reset", [&](const httplib::Request&, httplib::Response& res) {
        harness.resetConversation();
        memory->createSession("agent:main:gui:user:default");
        res.set_content("{\"ok\":true}", "application/json; charset=utf-8");
    });

    server.Post("/api/index-text", [&](const httplib::Request& req, httplib::Response& res) {
        std::string name = req.has_param("name") ? req.get_param_value("name") : "pasted-input.md";
        for (char& ch : name) {
            if (ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?' ||
                ch == '"' || ch == '<' || ch == '>' || ch == '|') {
                ch = '_';
            }
        }
        std::filesystem::create_directories(dataDir + "/uploads");
        std::string path = dataDir + "/uploads/" + name;
        {
            std::ofstream out(path, std::ios::binary);
            out << req.body;
        }
        memory->indexFile(path);
        auto stats = memory->getMemoryStats();
        std::ostringstream body;
        body << "{\"ok\":true,\"path\":" << jsonString(path)
             << ",\"bytes\":" << req.body.size()
             << ",\"files\":" << stats.indexedFiles
             << ",\"chunks\":" << stats.indexedChunks << "}";
        res.set_content(body.str(), "application/json; charset=utf-8");
    });

    server.Get("/api/context", [&](const httplib::Request& req, httplib::Response& res) {
        std::string query = req.has_param("q") ? req.get_param_value("q") : "";
        auto assembled = memory->assembleForQuery(query, appConfig.memory.contextTokenBudget);
        std::ostringstream text;
        text << "Query: " << query << "\n";
        text << "Estimated tokens: " << assembled.estimatedTokens << "\n\n";
        for (const auto& line : assembled.trace) text << "- " << line << "\n";
        std::ostringstream body;
        body << "{\"ok\":true,\"text\":" << jsonString(text.str()) << "}";
        res.set_content(body.str(), "application/json; charset=utf-8");
    });

    server.Post("/api/compact", [&](const httplib::Request&, httplib::Response& res) {
        auto result = memory->compact(appConfig.memory.contextTokenBudget / 2);
        std::ostringstream text;
        text << "Compaction: " << result.reason << "\n";
        text << "before: " << result.tokensBefore << "\n";
        text << "after: " << result.tokensAfter << "\n";
        if (!result.summary.empty()) text << "\n" << result.summary;
        std::ostringstream body;
        body << "{\"ok\":true,\"text\":" << jsonString(text.str()) << "}";
        res.set_content(body.str(), "application/json; charset=utf-8");
    });

    server.Get("/memory", [&](const httplib::Request&, httplib::Response& res) {
        auto stats = memory->getMemoryStats();
        std::ostringstream body;
        body << "files: " << stats.indexedFiles << "\n";
        body << "chunks: " << stats.indexedChunks << "\n";
        body << "treeNodes: " << stats.treeNodes << "\n";
        body << "data: " << dataDir << "\n";
        res.set_content(body.str(), "text/plain; charset=utf-8");
    });

    std::cout << "ClawLite GUI listening on http://localhost:18080\n";
    server.listen("0.0.0.0", 18080);
}
