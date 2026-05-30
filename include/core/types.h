#pragma once
// ClawLite — 公共类型定义
// 所有模块共享的基础数据结构

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <ctime>

namespace clawlite {

// ── 消息 ──────────────────────────────────────────────────

enum class Role { System, User, Assistant, Tool };

struct ToolCall {
    std::string id;          // 工具调用 ID
    std::string name;        // 工具名称
    std::string arguments;   // JSON 字符串形式的参数
};

struct Message {
    Role role;
    std::string content;
    std::string name;                // 可选：tool name（Role::Tool 时使用）
    std::string toolCallId;          // 可选：tool_call_id（Role::Tool 时使用）
    std::vector<ToolCall> toolCalls; // 可选：assistant 发出的工具调用
    int64_t timestamp = 0;           // Unix 时间戳（毫秒）

    // 工厂方法
    static Message system(const std::string& content);
    static Message user(const std::string& content);
    static Message assistant(const std::string& content);
    static Message toolResult(const std::string& toolCallId,
                              const std::string& toolName,
                              const std::string& content);
};

// ── 工具 ──────────────────────────────────────────────────

using ToolHandler = std::string(*)(const std::string& argumentsJson);

struct Tool {
    std::string name;
    std::string description;
    std::string parametersJson;  // JSON Schema 格式的参数描述
    ToolHandler handler = nullptr;
};

// ── 搜索结果 ──────────────────────────────────────────────

enum class SearchSource { Memory, Session };

struct SearchResult {
    std::string chunkId;     // chunk 主键，用于快速回表/去重
    std::string path;        // 文件路径
    int startLine = 0;
    int endLine = 0;
    double score = 0.0;      // 最终合并分数
    double vectorScore = 0.0;
    double textScore = 0.0;
    std::string snippet;     // 匹配的文本片段
    std::string headingPath; // Markdown 标题路径
    std::string sourceLabel; // 展示用来源标签
    int tokenCost = 0;       // 估算 token 成本
    SearchSource source = SearchSource::Memory;
};

// ── 分块 ──────────────────────────────────────────────────

struct MemoryChunk {
    std::string id;          // 稳定块 ID：path + line range + hash
    std::string parentId;    // Markdown 标题树父节点/标题路径 hash
    std::string path;        // 来源文件路径
    int startLine = 0;
    int endLine = 0;
    std::string text;        // 块的文本内容
    std::string hash;        // 内容 hash（用于去重/缓存）
    std::string headingPath; // 例如：README > 模块 B > 检索
    int depth = 0;           // Markdown 标题层级，普通文本为 0
    int tokenCost = 0;       // 估算 token 成本
    std::vector<double> embedding;  // 向量嵌入（可选）
};

// ── 文件条目 ──────────────────────────────────────────────

struct FileEntry {
    std::string path;        // 相对路径
    std::string absPath;     // 绝对路径
    int64_t mtimeMs = 0;     // 修改时间（毫秒）
    int64_t size = 0;        // 文件大小
    std::string hash;        // 内容 hash
};

// ── 会话 ──────────────────────────────────────────────────

struct SessionEntry {
    std::string sessionKey;  // 层级键：agent:id:channel:peer
    std::string sessionId;   // UUID
    std::vector<Message> messages;
    int64_t createdAt = 0;
    int64_t updatedAt = 0;
};

// ── 压缩结果 ──────────────────────────────────────────────

struct CompactResult {
    bool ok = false;
    bool compacted = false;
    std::string reason;
    std::string summary;          // 生成的摘要文本
    int tokensBefore = 0;
    int tokensAfter = 0;
};

// ── Agent 运行结果 ────────────────────────────────────────

enum class RunStatus { Success, Error, Timeout, MaxTurnsExceeded };

struct RunResult {
    RunStatus status = RunStatus::Success;
    std::string reply;             // 最终回复文本
    int totalTurns = 0;            // 工具调用轮次数
    int totalTokens = 0;           // 消耗的 token 数
    std::string error;             // 错误信息（status != Success 时）
};

// ── 工具执行结果 ──────────────────────────────────────────

struct ToolResult {
    bool success = false;
    std::string output;            // 工具输出文本
    std::string error;             // 错误信息
};

// ── 工具函数 ──────────────────────────────────────────────

// 简单的 token 估算（1 token ≈ 4 字符英文，1 token ≈ 1.5 字符中文）
int estimateTokens(const std::string& text);

// 角色转字符串
std::string roleToString(Role role);
Role stringToRole(const std::string& s);

// 当前时间戳（毫秒）
int64_t nowMs();

} // namespace clawlite
