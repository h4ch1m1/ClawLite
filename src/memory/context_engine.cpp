// ClawLite — 上下文引擎实现
// 组合 FileTreeIndex + Chunker + SearchManager + ContextBudgeter + SessionSummaryTree

#include "memory/context_engine.h"
#include "memory/chunker.h"
#include "memory/context_budgeter.h"
#include "memory/embedding.h"
#include "memory/file_tree_index.h"
#include "memory/lru_cache.h"
#include "memory/memory_store.h"
#include "memory/search_manager.h"
#include "memory/session_store.h"
#include "memory/session_summary_tree.h"
#include "memory/compaction.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>

namespace clawlite {
namespace {

std::string hashText(const std::string& text) {
    size_t h = std::hash<std::string>{}(text);
    char buf[20];
    snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
    return std::string(buf);
}

bool looksLikeText(const std::string& content) {
    return content.find('\0') == std::string::npos;
}

int64_t fileMtimeMs(const std::string& filePath) {
    try {
        return std::filesystem::last_write_time(filePath).time_since_epoch().count() / 10000;
    } catch (...) {
        return 0;
    }
}

std::string roleLabel(Role role) {
    switch (role) {
        case Role::System: return "system";
        case Role::User: return "user";
        case Role::Assistant: return "assistant";
        case Role::Tool: return "tool";
    }
    return "unknown";
}

} // namespace

// 具体实现类
class DefaultContextEngine : public IContextEngine {
public:
    explicit DefaultContextEngine(ContextEngineOptions options = {})
        : m_options(options), m_fileCache(static_cast<size_t>(std::max(0, options.fileCacheCapacity))) {}

    void initialize(const std::string& dataDir) override {
        m_dataDir = dataDir;
        std::filesystem::create_directories(dataDir);
        std::string dbPath = dataDir + "/clawlite.db";
        m_store.open(dbPath);
        m_embedding = std::make_unique<LocalMockEmbedding>(128);
        m_search = std::make_unique<SearchManager>(m_store, std::make_unique<LocalMockEmbedding>(128));
    }

    void dispose() override {
        m_store.close();
    }

    void indexPath(const std::string& path) override {
        std::error_code ec;
        if (std::filesystem::is_directory(path, ec)) {
            auto stats = m_fileTree.build(path);
            std::filesystem::path root = std::filesystem::absolute(path);
            for (const auto& rel : stats.changedFiles) {
                std::filesystem::path candidate = root / rel;
                if (std::filesystem::is_regular_file(candidate, ec)) {
                    indexFile(candidate.string());
                }
            }
            return;
        }
        indexFile(path);
        m_fileTree.build(path);
    }

    void indexFile(const std::string& filePath) override {
        auto content = readTextFile(filePath);
        if (!content.has_value() || !looksLikeText(*content)) return;

        ChunkerConfig chunkCfg;
        chunkCfg.chunkTokens = m_options.chunkTokens;
        chunkCfg.overlapTokens = m_options.overlapTokens;
        auto chunks = Chunker::chunkMarkdown(filePath, *content, chunkCfg);

        std::vector<std::string> texts;
        texts.reserve(chunks.size());
        for (const auto& chunk : chunks) {
            texts.push_back(chunk.text);
        }
        auto embeddings = m_embedding->embedBatch(texts);

        m_store.beginTransaction();
        m_store.removeChunksByFile(filePath);

        FileEntry entry;
        entry.path = filePath;
        entry.absPath = std::filesystem::absolute(filePath).string();
        entry.hash = hashText(*content);
        entry.size = static_cast<int64_t>(content->size());
        entry.mtimeMs = fileMtimeMs(filePath);
        m_store.upsertFile(entry);

        for (size_t i = 0; i < chunks.size(); ++i) {
            MemoryChunk chunk = chunks[i];
            if (i < embeddings.size()) {
                chunk.embedding = embeddings[i];
            }
            m_store.upsertChunk(chunk);
        }
        m_store.commit();
    }

    void removeFile(const std::string& filePath) override {
        m_store.removeChunksByFile(filePath);
        m_store.removeFile(filePath);
    }

    bool needsReindex(const std::string& filePath) override {
        auto existing = m_store.getFile(filePath);
        if (!existing.has_value()) return true;
        auto content = readTextFile(filePath);
        if (!content.has_value()) return true;
        return existing->hash != hashText(*content);
    }

    void ingest(const Message& msg) override {
        m_sessions.appendMessage(m_sessionKey, msg);
    }

    void ingestBatch(const std::vector<Message>& messages) override {
        for (const auto& msg : messages) {
            m_sessions.appendMessage(m_sessionKey, msg);
        }
    }

    AssembleResult assemble(int tokenBudget) override {
        std::string query;
        auto all = m_sessions.getAllMessages(m_sessionKey);
        for (int i = static_cast<int>(all.size()) - 1; i >= 0; --i) {
            if (all[static_cast<size_t>(i)].role == Role::User) {
                query = all[static_cast<size_t>(i)].content;
                break;
            }
        }
        return assembleForQuery(query, tokenBudget);
    }

    AssembleResult assembleForQuery(const std::string& query, int tokenBudget) override {
        AssembleResult result;
        std::vector<ContextCandidate> candidates;

        auto all = m_sessions.getAllMessages(m_sessionKey);
        m_summaryTree.rebuild(all, m_options.summaryGroupSize, m_options.keepRecentTurns);

        int recency = 0;
        for (const auto& node : m_summaryTree.selectSummaries(tokenBudget / 4)) {
            ContextCandidate c;
            c.id = node.id;
            c.source = "summary-tree";
            c.label = node.id;
            c.text = "[Conversation summary] " + node.summary;
            c.role = Role::System;
            c.tokenCost = node.tokenCost;
            c.importance = 0.55;
            c.recency = recency++;
            candidates.push_back(std::move(c));
        }

        for (const auto& msg : m_summaryTree.recentMessages()) {
            ContextCandidate c;
            c.id = "history:" + std::to_string(recency);
            c.source = "recent-history";
            c.label = roleLabel(msg.role);
            c.text = msg.content;
            c.role = msg.role;
            c.tokenCost = estimateTokens(msg.content);
            c.importance = 0.85;
            c.recency = recency++;
            candidates.push_back(std::move(c));
        }

        if (!query.empty() && m_search) {
            SearchConfig cfg;
            cfg.topK = 8;
            cfg.vectorCandidateLimit = 8;
            cfg.ftsCandidateLimit = 12;
            auto hits = m_search->search(query, cfg);
            for (const auto& hit : hits) {
                ContextCandidate c;
                c.id = hit.chunkId.empty()
                    ? hit.path + ":" + std::to_string(hit.startLine)
                    : hit.chunkId;
                c.source = "chunk-index";
                c.label = hit.path + ":" + std::to_string(hit.startLine) + "-" +
                    std::to_string(hit.endLine);
                c.text = "[File chunk] " + c.label;
                if (!hit.headingPath.empty()) c.text += "\nHeading: " + hit.headingPath;
                c.text += "\n" + hit.snippet;
                c.role = Role::System;
                c.tokenCost = hit.tokenCost > 0 ? hit.tokenCost : estimateTokens(c.text);
                c.importance = 0.70 + hit.score;
                c.recency = recency++;
                candidates.push_back(std::move(c));
            }
        }

        auto budgeted = ContextBudgeter::select(std::move(candidates), tokenBudget);
        for (const auto& item : budgeted.selected) {
            Message msg;
            msg.role = item.role;
            msg.content = item.role == Role::System
                ? "[" + item.source + " / " + item.label + "] " + item.text
                : item.text;
            result.messages.push_back(std::move(msg));
            result.trace.push_back(item.source + " | " + item.label +
                " | tokens=" + std::to_string(item.tokenCost));
        }
        result.estimatedTokens = budgeted.tokensUsed;
        if (budgeted.skipped > 0) {
            result.trace.push_back("skipped by budget: " + std::to_string(budgeted.skipped));
        }
        return result;
    }

    CompactResult compact(int targetTokens) override {
        auto history = m_sessions.getAllMessages(m_sessionKey);
        CompactionConfig config;
        config.targetTokens = targetTokens;
        config.keepTurns = m_options.keepRecentTurns;
        auto compactResult = Compactor::compact(history, config);
        if (compactResult.compacted) {
            m_sessions.clear();
            m_sessions.createSession(m_sessionKey);
            for (const auto& msg : history) {
                m_sessions.appendMessage(m_sessionKey, msg);
            }
        }
        m_summaryTree.rebuild(history, m_options.summaryGroupSize, m_options.keepRecentTurns);
        return compactResult;
    }

    std::vector<SearchResult> search(const std::string& query, int topK) override {
        SearchConfig cfg;
        cfg.topK = topK;
        return m_search ? m_search->search(query, cfg) : std::vector<SearchResult>{};
    }

    void createSession(const std::string& sessionKey) override {
        m_sessionKey = sessionKey;
        m_sessions.createSession(sessionKey);
    }

    std::vector<Message> getSessionHistory(const std::string& sessionKey, int maxTurns) override {
        return m_sessions.getHistory(sessionKey, maxTurns);
    }

    int getIndexedFileCount() const override {
        return m_store.fileCount();
    }

    int getIndexedChunkCount() const override {
        return m_store.chunkCount();
    }

    MemoryStats getMemoryStats() const override {
        MemoryStats stats;
        stats.indexedFiles = m_store.fileCount();
        stats.indexedChunks = m_store.chunkCount();
        stats.treeNodes = static_cast<int>(m_fileTree.nodes().size());
        stats.summaryNodes = m_summaryTree.summaryCount();
        stats.cacheHits = m_cacheHits;
        stats.cacheMisses = m_cacheMisses;
        stats.rootHash = m_fileTree.rootHash();
        return stats;
    }

private:
    std::optional<std::string> readTextFile(const std::string& filePath) {
        std::error_code ec;
        int64_t size = std::filesystem::is_regular_file(filePath, ec)
            ? static_cast<int64_t>(std::filesystem::file_size(filePath, ec))
            : 0;
        std::string cacheKey = filePath + ":" + std::to_string(fileMtimeMs(filePath)) +
            ":" + std::to_string(size);
        auto cached = m_fileCache.get(cacheKey);
        if (cached.has_value()) {
            ++m_cacheHits;
            return cached;
        }
        ++m_cacheMisses;

        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) return std::nullopt;
        std::ostringstream ss;
        ss << file.rdbuf();
        std::string content = ss.str();
        m_fileCache.put(cacheKey, content);
        return content;
    }

    ContextEngineOptions m_options;
    std::string m_dataDir;
    std::string m_sessionKey = "default";
    MemoryStore m_store;
    SessionStore m_sessions;
    FileTreeIndex m_fileTree;
    SessionSummaryTree m_summaryTree;
    LruCache<std::string, std::string> m_fileCache;
    int m_cacheHits = 0;
    int m_cacheMisses = 0;
    std::unique_ptr<IEmbeddingProvider> m_embedding;
    std::unique_ptr<SearchManager> m_search;
};

std::unique_ptr<IContextEngine> createContextEngine() {
    return std::make_unique<DefaultContextEngine>();
}

std::unique_ptr<IContextEngine> createContextEngine(const ContextEngineOptions& options) {
    return std::make_unique<DefaultContextEngine>(options);
}

} // namespace clawlite
