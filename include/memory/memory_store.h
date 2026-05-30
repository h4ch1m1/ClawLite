#pragma once
// ClawLite — SQLite 存储层
// 参考：openclaw-main/packages/memory-host-sdk/host/memory-schema.ts
// 数据结构：SQLite B+Tree（files/chunks/embedding_cache 表）
//          SQLite FTS5 倒排索引（全文搜索）
//
// 表结构设计（4 张表 + 1 虚拟表）：
//   meta:             key TEXT PK, value TEXT         — 元数据 KV
//   files:            path TEXT PK, hash, mtime, size — 已索引文件跟踪
//   chunks:           id TEXT PK, path, start_line, end_line, hash, text, embedding TEXT — 文本块+向量
//   embedding_cache:  provider, model, hash, embedding TEXT, dims — 嵌入缓存
//   chunks_fts:       FTS5(text, id, path)            — 全文搜索虚拟表

#include "core/types.h"
#include <string>
#include <vector>
#include <optional>

// SQLite 前向声明（放在 namespace 外面，避免创建 clawlite::sqlite3）
struct sqlite3;

namespace clawlite {

class MemoryStore {
public:
    MemoryStore();
    ~MemoryStore();

    // 打开/创建数据库
    // 参考：openclaw-main/packages/memory-host-sdk/host/memory-schema.ts:ensureMemoryIndexSchema
    bool open(const std::string& dbPath);
    void close();

    // ── 文件索引 ──────────────────────────────────────────

    // 记录文件已索引
    void upsertFile(const FileEntry& entry);

    // 删除文件索引
    void removeFile(const std::string& path);

    // 查询文件索引
    std::optional<FileEntry> getFile(const std::string& path);

    // 获取所有已索引文件
    std::vector<FileEntry> getAllFiles();

    // ── 块存储 ────────────────────────────────────────────

    // 插入或更新块
    void upsertChunk(const MemoryChunk& chunk);

    // 删除指定文件的所有块
    void removeChunksByFile(const std::string& path);

    // 获取所有块
    std::vector<MemoryChunk> getAllChunks();

    // 获取指定文件的块
    std::vector<MemoryChunk> getChunksByFile(const std::string& path);

    // 按 chunk id/hash 获取单个块
    std::optional<MemoryChunk> getChunkById(const std::string& id);

    // ── 全文搜索（FTS5）──────────────────────────────────

    // FTS5 全文搜索，返回匹配的 chunk ID 和 BM25 分数
    // 参考：openclaw-main/packages/memory-host-sdk/host/types.ts — search()
    std::vector<std::pair<std::string, double>> ftsSearch(
        const std::string& query,
        int limit = 20
    );

    // 优化版：FTS5 直接 join chunks 表，避免查询后全量扫 chunks。
    std::vector<SearchResult> ftsSearchDetailed(
        const std::string& query,
        int limit = 20
    );

    // ── 嵌入缓存 ─────────────────────────────────────────

    // 缓存嵌入向量
    void cacheEmbedding(const std::string& hash,
                        const std::string& provider,
                        const std::string& model,
                        const std::vector<double>& embedding);

    // 查询嵌入缓存
    std::optional<std::vector<double>> getCachedEmbedding(
        const std::string& hash,
        const std::string& provider,
        const std::string& model
    );

    // ── 元数据 ────────────────────────────────────────────

    void setMeta(const std::string& key, const std::string& value);
    std::optional<std::string> getMeta(const std::string& key);

    // ── 事务 ──────────────────────────────────────────────

    void beginTransaction();
    void commit();
    void rollback();

    // 统计
    int fileCount() const;
    int chunkCount() const;

private:
    sqlite3* m_db = nullptr;

    // 创建表和索引
    void createSchema();
    void createFtsTable();
};

} // namespace clawlite
