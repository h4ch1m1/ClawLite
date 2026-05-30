// ClawLite — SQLite 存储层实现
// TODO: B 同学实现
// 这是工作量最大的文件，需要调用 SQLite C API

#include "memory/memory_store.h"
#include "sqlite3.h"
#include <sstream>
#include <cstdio>

namespace clawlite {

// ── 辅助函数 ──────────────────────────────────────────────

// 执行无返回值的 SQL
static bool execSql(sqlite3* db, const char* sql) {
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }
    return true;
}

// vector<double> → JSON 字符串 "[0.1,0.2,...]"
static std::string embeddingToJson(const std::vector<double>& vec) {
    std::ostringstream ss;
    ss << "[";
    for (size_t i = 0; i < vec.size(); i++) {
        if (i > 0) ss << ",";
        char buf[32];
        snprintf(buf, sizeof(buf), "%.6f", vec[i]);
        ss << buf;
    }
    ss << "]";
    return ss.str();
}

// JSON 字符串 → vector<double>
static std::vector<double> jsonToEmbedding(const std::string& json) {
    std::vector<double> result;
    size_t pos = json.find('[');
    if (pos == std::string::npos) return result;
    pos++;
    while (pos < json.size()) {
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == ',')) pos++;
        if (pos >= json.size() || json[pos] == ']') break;
        size_t end = pos;
        while (end < json.size() && json[end] != ',' && json[end] != ']') end++;
        try {
            result.push_back(std::stod(json.substr(pos, end - pos)));
        } catch (...) {
            return {};
        }
        pos = end;
    }
    return result;
}

MemoryStore::MemoryStore() = default;
MemoryStore::~MemoryStore() { close(); }

bool MemoryStore::open(const std::string& dbPath) {
    // 参考：openclaw-main/packages/memory-host-sdk/host/memory-schema.ts:ensureMemoryIndexSchema
    int rc = sqlite3_open(dbPath.c_str(), &m_db);
    if (rc != SQLITE_OK) {
        m_db = nullptr;
        return false;
    }
    // 启用 WAL 模式（更好的并发读性能）
    execSql(m_db, "PRAGMA journal_mode=WAL");
    execSql(m_db, "PRAGMA foreign_keys=ON");
    createSchema();
    createFtsTable();
    return true;
}

void MemoryStore::close() {
    if (m_db) { sqlite3_close(m_db); m_db = nullptr; }
}

void MemoryStore::createSchema() {
    // 参考：openclaw-main/packages/memory-host-sdk/host/memory-schema.ts:ensureMemoryIndexSchema
    // 表结构：meta / files / chunks / embedding_cache + 2 个索引

    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS meta (
            key TEXT PRIMARY KEY,
            value TEXT NOT NULL
        )
    )");

    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS files (
            path TEXT PRIMARY KEY,
            source TEXT NOT NULL DEFAULT 'memory',
            hash TEXT NOT NULL,
            mtime INTEGER NOT NULL,
            size INTEGER NOT NULL
        )
    )");

    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS chunks (
            id TEXT PRIMARY KEY,
            parent_id TEXT,
            path TEXT NOT NULL,
            source TEXT NOT NULL DEFAULT 'memory',
            start_line INTEGER,
            end_line INTEGER,
            hash TEXT NOT NULL,
            text TEXT NOT NULL,
            heading_path TEXT NOT NULL DEFAULT '',
            depth INTEGER NOT NULL DEFAULT 0,
            token_cost INTEGER NOT NULL DEFAULT 0,
            embedding TEXT NOT NULL DEFAULT '[]',
            updated_at INTEGER NOT NULL
        )
    )");

    execSql(m_db, "ALTER TABLE chunks ADD COLUMN parent_id TEXT");
    execSql(m_db, "ALTER TABLE chunks ADD COLUMN heading_path TEXT NOT NULL DEFAULT ''");
    execSql(m_db, "ALTER TABLE chunks ADD COLUMN depth INTEGER NOT NULL DEFAULT 0");
    execSql(m_db, "ALTER TABLE chunks ADD COLUMN token_cost INTEGER NOT NULL DEFAULT 0");

    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS embedding_cache (
            provider TEXT NOT NULL,
            model TEXT NOT NULL,
            hash TEXT NOT NULL,
            embedding TEXT NOT NULL,
            dims INTEGER NOT NULL,
            updated_at INTEGER NOT NULL,
            PRIMARY KEY (provider, model, hash)
        )
    )");

    execSql(m_db, "CREATE INDEX IF NOT EXISTS idx_chunks_path ON chunks(path)");
    execSql(m_db, "CREATE INDEX IF NOT EXISTS idx_chunks_source ON chunks(source)");
}

void MemoryStore::createFtsTable() {
    // FTS5 虚拟表，text 列建索引，其他列 UNINDEXED
    // 需要 SQLite 编译时启用 -DSQLITE_ENABLE_FTS5（CMakeLists.txt 已配置）
    execSql(m_db, R"(
        CREATE VIRTUAL TABLE IF NOT EXISTS chunks_fts USING fts5(
            text, id, path, source,
            tokenize='unicode61'
        )
    )");
}

// ── 文件索引 ──────────────────────────────────────────────

void MemoryStore::upsertFile(const FileEntry& entry) {
    if (!m_db) return;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(m_db,
        "INSERT OR REPLACE INTO files (path, hash, mtime, size) VALUES (?, ?, ?, ?)",
        -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, entry.path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, entry.hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, entry.mtimeMs);
    sqlite3_bind_int64(stmt, 4, entry.size);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void MemoryStore::removeFile(const std::string& path) {
    if (!m_db) return;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(m_db, "DELETE FROM files WHERE path = ?", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::optional<FileEntry> MemoryStore::getFile(const std::string& path) {
    if (!m_db) return std::nullopt;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(m_db,
        "SELECT path, hash, mtime, size FROM files WHERE path = ?",
        -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<FileEntry> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        FileEntry entry;
        entry.path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        entry.hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        entry.mtimeMs = sqlite3_column_int64(stmt, 2);
        entry.size = sqlite3_column_int64(stmt, 3);
        result = entry;
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<FileEntry> MemoryStore::getAllFiles() {
    std::vector<FileEntry> results;
    if (!m_db) return results;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(m_db,
        "SELECT path, hash, mtime, size FROM files", -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        FileEntry entry;
        entry.path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        entry.hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        entry.mtimeMs = sqlite3_column_int64(stmt, 2);
        entry.size = sqlite3_column_int64(stmt, 3);
        results.push_back(entry);
    }
    sqlite3_finalize(stmt);
    return results;
}

// ── 块存储 ────────────────────────────────────────────────

void MemoryStore::upsertChunk(const MemoryChunk& chunk) {
    if (!m_db) return;
    // 写 chunks 表，embedding 存为 JSON 字符串
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(m_db, R"(
        INSERT OR REPLACE INTO chunks
        (id, parent_id, path, source, start_line, end_line, hash, text,
         heading_path, depth, token_cost, embedding, updated_at)
        VALUES (?, ?, ?, 'memory', ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )", -1, &stmt, nullptr);

    std::string embJson = embeddingToJson(chunk.embedding);
    int64_t ts = nowMs();
    std::string id = chunk.id.empty() ? chunk.hash : chunk.id;
    int tokenCost = chunk.tokenCost > 0 ? chunk.tokenCost : estimateTokens(chunk.text);

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, chunk.parentId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, chunk.path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, chunk.startLine);
    sqlite3_bind_int(stmt, 5, chunk.endLine);
    sqlite3_bind_text(stmt, 6, chunk.hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, chunk.text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, chunk.headingPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 9, chunk.depth);
    sqlite3_bind_int(stmt, 10, tokenCost);
    sqlite3_bind_text(stmt, 11, embJson.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 12, ts);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    // 同步 FTS5 索引：先删旧的，再插新的
    sqlite3_stmt* ftsDel = nullptr;
    sqlite3_prepare_v2(m_db, "DELETE FROM chunks_fts WHERE id = ?", -1, &ftsDel, nullptr);
    sqlite3_bind_text(ftsDel, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(ftsDel);
    sqlite3_finalize(ftsDel);

    sqlite3_stmt* ftsIns = nullptr;
    sqlite3_prepare_v2(m_db,
        "INSERT INTO chunks_fts (text, id, path, source) VALUES (?, ?, ?, 'memory')",
        -1, &ftsIns, nullptr);
    sqlite3_bind_text(ftsIns, 1, chunk.text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ftsIns, 2, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ftsIns, 3, chunk.path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(ftsIns);
    sqlite3_finalize(ftsIns);
}

void MemoryStore::removeChunksByFile(const std::string& path) {
    if (!m_db) return;
    // 先获取该文件的所有 chunk id，用于清理 FTS
    std::vector<std::string> ids;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(m_db, "SELECT id FROM chunks WHERE path = ?", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ids.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    }
    sqlite3_finalize(stmt);

    // 删除 chunks 表
    sqlite3_prepare_v2(m_db, "DELETE FROM chunks WHERE path = ?", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    // 删除 FTS 索引
    for (const auto& id : ids) {
        sqlite3_prepare_v2(m_db, "DELETE FROM chunks_fts WHERE id = ?", -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

std::vector<MemoryChunk> MemoryStore::getAllChunks() {
    std::vector<MemoryChunk> results;
    if (!m_db) return results;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(m_db,
        "SELECT id, parent_id, path, start_line, end_line, hash, text, heading_path, depth, token_cost, embedding FROM chunks",
        -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MemoryChunk chunk;
        chunk.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const char* parent = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (parent) chunk.parentId = parent;
        chunk.path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        chunk.startLine = sqlite3_column_int(stmt, 3);
        chunk.endLine = sqlite3_column_int(stmt, 4);
        chunk.hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        chunk.text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        const char* heading = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        if (heading) chunk.headingPath = heading;
        chunk.depth = sqlite3_column_int(stmt, 8);
        chunk.tokenCost = sqlite3_column_int(stmt, 9);
        const char* embStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
        if (embStr) chunk.embedding = jsonToEmbedding(embStr);
        results.push_back(chunk);
    }
    sqlite3_finalize(stmt);
    return results;
}

std::vector<MemoryChunk> MemoryStore::getChunksByFile(const std::string& path) {
    std::vector<MemoryChunk> results;
    if (!m_db) return results;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(m_db,
        "SELECT id, parent_id, path, start_line, end_line, hash, text, heading_path, depth, token_cost, embedding FROM chunks WHERE path = ?",
        -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MemoryChunk chunk;
        chunk.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const char* parent = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (parent) chunk.parentId = parent;
        chunk.path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        chunk.startLine = sqlite3_column_int(stmt, 3);
        chunk.endLine = sqlite3_column_int(stmt, 4);
        chunk.hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        chunk.text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        const char* heading = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        if (heading) chunk.headingPath = heading;
        chunk.depth = sqlite3_column_int(stmt, 8);
        chunk.tokenCost = sqlite3_column_int(stmt, 9);
        const char* embStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
        if (embStr) chunk.embedding = jsonToEmbedding(embStr);
        results.push_back(chunk);
    }
    sqlite3_finalize(stmt);
    return results;
}

std::optional<MemoryChunk> MemoryStore::getChunkById(const std::string& id) {
    if (!m_db) return std::nullopt;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(m_db,
        "SELECT id, parent_id, path, start_line, end_line, hash, text, heading_path, depth, token_cost, embedding "
        "FROM chunks WHERE id = ? OR hash = ?",
        -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, id.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<MemoryChunk> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        MemoryChunk chunk;
        chunk.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const char* parent = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (parent) chunk.parentId = parent;
        chunk.path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        chunk.startLine = sqlite3_column_int(stmt, 3);
        chunk.endLine = sqlite3_column_int(stmt, 4);
        chunk.hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        chunk.text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        const char* heading = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        if (heading) chunk.headingPath = heading;
        chunk.depth = sqlite3_column_int(stmt, 8);
        chunk.tokenCost = sqlite3_column_int(stmt, 9);
        const char* embStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
        if (embStr) chunk.embedding = jsonToEmbedding(embStr);
        result = chunk;
    }
    sqlite3_finalize(stmt);
    return result;
}

// ── 全文搜索 ──────────────────────────────────────────────

std::vector<std::pair<std::string, double>> MemoryStore::ftsSearch(
    const std::string& query, int limit
) {
    // FTS5 搜索，BM25 排序
    // BM25 分数越小越好（负数），取反后越大越好
    std::vector<std::pair<std::string, double>> results;
    if (!m_db || query.empty()) return results;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db,
        "SELECT id, bm25(chunks_fts) as score "
        "FROM chunks_fts "
        "WHERE chunks_fts MATCH ? "
        "ORDER BY score "
        "LIMIT ?",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return results;

    sqlite3_bind_text(stmt, 1, query.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        double bm25Score = sqlite3_column_double(stmt, 1);
        // BM25 越小越好（负数），取反使越大越好
        results.emplace_back(id, -bm25Score);
    }
    sqlite3_finalize(stmt);
    return results;
}

std::vector<SearchResult> MemoryStore::ftsSearchDetailed(
    const std::string& query, int limit
) {
    std::vector<SearchResult> results;
    if (!m_db || query.empty()) return results;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db,
        "SELECT c.id, c.path, c.start_line, c.end_line, c.text, c.heading_path, c.token_cost, "
        "bm25(chunks_fts) as score "
        "FROM chunks_fts JOIN chunks c ON c.id = chunks_fts.id "
        "WHERE chunks_fts MATCH ? "
        "ORDER BY score LIMIT ?",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return results;

    sqlite3_bind_text(stmt, 1, query.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        SearchResult r;
        r.chunkId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        r.path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        r.startLine = sqlite3_column_int(stmt, 2);
        r.endLine = sqlite3_column_int(stmt, 3);
        const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        r.snippet = text ? std::string(text).substr(0, 240) : "";
        const char* heading = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        if (heading) r.headingPath = heading;
        r.tokenCost = sqlite3_column_int(stmt, 6);
        r.score = -sqlite3_column_double(stmt, 7);
        r.textScore = r.score;
        r.source = SearchSource::Memory;
        r.sourceLabel = "fts5";
        results.push_back(std::move(r));
    }
    sqlite3_finalize(stmt);
    return results;
}

// ── 嵌入缓存 ─────────────────────────────────────────────

void MemoryStore::cacheEmbedding(const std::string& hash,
                                  const std::string& provider,
                                  const std::string& model,
                                  const std::vector<double>& embedding) {
    if (!m_db) return;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(m_db, R"(
        INSERT OR REPLACE INTO embedding_cache
        (provider, model, hash, embedding, dims, updated_at)
        VALUES (?, ?, ?, ?, ?, ?)
    )", -1, &stmt, nullptr);

    std::string embJson = embeddingToJson(embedding);
    int64_t ts = nowMs();

    sqlite3_bind_text(stmt, 1, provider.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, model.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, embJson.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, static_cast<int>(embedding.size()));
    sqlite3_bind_int64(stmt, 6, ts);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::optional<std::vector<double>> MemoryStore::getCachedEmbedding(
    const std::string& hash,
    const std::string& provider,
    const std::string& model
) {
    if (!m_db) return std::nullopt;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(m_db,
        "SELECT embedding FROM embedding_cache WHERE provider=? AND model=? AND hash=?",
        -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, provider.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, model.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, hash.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<std::vector<double>> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* embStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (embStr) {
            auto vec = jsonToEmbedding(embStr);
            if (!vec.empty()) result = vec;
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

// ── 元数据 ────────────────────────────────────────────────

void MemoryStore::setMeta(const std::string& key, const std::string& value) {
    if (!m_db) return;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(m_db,
        "INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)",
        -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, value.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::optional<std::string> MemoryStore::getMeta(const std::string& key) {
    if (!m_db) return std::nullopt;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(m_db,
        "SELECT value FROM meta WHERE key = ?", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<std::string> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    }
    sqlite3_finalize(stmt);
    return result;
}

// ── 事务 ──────────────────────────────────────────────────

void MemoryStore::beginTransaction() {
    if (m_db) execSql(m_db, "BEGIN TRANSACTION");
}

void MemoryStore::commit() {
    if (m_db) execSql(m_db, "COMMIT");
}

void MemoryStore::rollback() {
    if (m_db) execSql(m_db, "ROLLBACK");
}

int MemoryStore::fileCount() const {
    if (!m_db) return 0;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM files", -1, &stmt, nullptr);
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

int MemoryStore::chunkCount() const {
    if (!m_db) return 0;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM chunks", -1, &stmt, nullptr);
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

} // namespace clawlite
