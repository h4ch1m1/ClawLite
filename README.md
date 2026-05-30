# ClawLite

ClawLite 是一个面向数据结构课程作业的轻量级 Agent Runtime。最终版不追求堆叠复杂 agent 平台功能，而是聚焦一个问题：

> 如何用基础数据结构支撑大文件读入和长上下文管理？

核心交付是 CLI、memory 结构、测试、报告和设计图。GUI 作为轻量演示入口保留。

## 核心能力

- 文件树索引：把工作区建成 FileTree，目录 hash 由子节点合成，文件 hash 由内容、mtime、size 合成。
- 结构化分块：Markdown 先按标题树分 section，超长 section 再用滑动窗口切 chunk。
- 上下文预算：候选上下文进入优先队列，按重要度、时间近度、token 成本贪心选择。
- 会话摘要树：旧对话按固定轮次聚合成 summary node，最近消息保留原文。
- 辅助检索：SQLite FTS5 和手写倒排表只用于从大文件中找候选块。
- MiMo 测试：兼容 OpenAI-style chat completions，默认模型 `mimo-v2.5-pro`。

## 数据结构映射

| 数据结构 | 用途 | 代码位置 |
| --- | --- | --- |
| 树 | 文件树 / Markdown 标题树 / 会话摘要树 | `file_tree_index.*`, `chunker.*`, `session_summary_tree.*` |
| 哈希表 | 文件 hash 缓存、技能注册、倒排表、去重 | `types.*`, `skill_registry.*`, `search_manager.*` |
| 优先队列/堆 | token 预算内选择最值得注入的上下文 | `context_budgeter.*` |
| 双向链表 + 哈希表 | LRU 文件内容缓存 | `lru_cache.h` |
| 倒排索引 | FTS5 / 手写 inverted index 查询候选 chunk | `memory_store.*`, `search_manager.*` |
| 滑动窗口 | 长 section 分块并保留 overlap | `chunker.*` |
| 状态机 | Agent harness 工具调用循环 | `harness.*` |

## 目录结构

```text
ClawLite/
├── include/
│   ├── core/       # 公共类型、配置系统、事件总线
│   ├── skill/      # SKILL.md 解析、注册、预算裁剪
│   ├── memory/     # 文件树、分块、预算器、摘要树、SQLite 存储
│   └── llm/        # MiMo/OpenAI-compatible client、harness、工具执行
├── src/
├── tests/
├── skills/
├── third_party/
├── DESIGN_REPORT.md
└── clawlite_config.example.env
```

## 配置

复制示例配置，填入自己的 key：

```powershell
Copy-Item clawlite_config.example.env clawlite_config.env
```

`clawlite_config.env` 已在 `.gitignore` 中，不会提交。也可以用环境变量覆盖同名配置。

关键配置：

```env
CLAWLITE_BASE_URL=https://api.mimo-v2.com/v1
CLAWLITE_API_KEY=replace-with-your-key
CLAWLITE_MODEL=mimo-v2.5-pro
CLAWLITE_MAX_TOKENS_FIELD=max_completion_tokens
CLAWLITE_CONTEXT_TOKEN_BUDGET=12000
CLAWLITE_CHUNK_TOKENS=220
CLAWLITE_OVERLAP_TOKENS=50
```

## CLI 演示

```text
/index README.md
/search 数据结构
/context 数据结构
/compact
/memory status
/ask 你好，请用一句话介绍 ClawLite
```

普通输入也会进入 agent harness；`/ask` 只是显式端到端调用。

## 构建与测试

推荐 CMake：

```powershell
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

如果本机没有 CMake，也可以用 MinGW fallback：

```powershell
gcc -std=c11 -Ithird_party/sqlite3 -DSQLITE_ENABLE_FTS5 -DSQLITE_ENABLE_JSON1 -c third_party/sqlite3/sqlite3.c -o build-plan-check\sqlite3.o
g++ -std=c++17 -D_WIN32_WINNT=0x0A00 -Iinclude -Ithird_party/sqlite3 -Ithird_party/httplib <sources...> build-plan-check\sqlite3.o -lws2_32 -o build-plan-check\clawlite_tests.exe
```

本次验证结果：

- MinGW fallback 编译通过。
- 全部离线单元测试通过。

## 更多设计图

见 [DESIGN_REPORT.md](DESIGN_REPORT.md)。
