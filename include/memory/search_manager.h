#pragma once
// ClawLite — 混合检索管理器
// 参考：openclaw-main/packages/memory-host-sdk/host/types.ts — MemorySearchManager
// 数据结构：
//   - 向量搜索：遍历 chunks，计算余弦相似度，排序取 top-k
//   - 全文搜索：FTS5 MATCH 查询，BM25 排序
//   - 分数合并：归一化后加权求和
//
// 算法：
//   vectorResults = cosine search(query_embedding, all_chunks) → top-k
//   textResults   = fts5 search(query_text) → top-k
//   merged        = weighted_merge(vectorResults, textResults, alpha)
//
// 这是数据结构课程重点！要展示排序合并过程。

#include "core/types.h"
#include "memory/memory_store.h"
#include "memory/embedding.h"
#include <vector>
#include <string>
#include <memory>

namespace clawlite {

struct SearchConfig {
    int topK = 5;                    // 返回结果数
    double vectorWeight = 0.7;       // 向量分数权重 (alpha)
    double textWeight = 0.3;         // 全文分数权重 (1-alpha)
    int vectorCandidateLimit = 50;   // 向量搜索候选数
    int ftsCandidateLimit = 20;      // FTS 搜索候选数
};

class SearchManager {
public:
    SearchManager(MemoryStore& store, std::unique_ptr<IEmbeddingProvider> embedding);

    // 混合搜索：向量 + 全文
    // 参考：openclaw-main/packages/memory-host-sdk/host/types.ts:search
    // 算法步骤：
    //   1. 用 query 计算嵌入向量
    //   2. 向量搜索：遍历所有 chunk 的 embedding，计算余弦相似度
    //   3. 全文搜索：FTS5 MATCH 查询
    //   4. 分数归一化到 [0, 1]
    //   5. 加权合并：score = alpha * vectorScore + (1-alpha) * textScore
    //   6. 排序取 top-k
    std::vector<SearchResult> search(
        const std::string& query,
        const SearchConfig& config = {}
    );

    // 仅向量搜索
    std::vector<SearchResult> vectorSearch(
        const std::string& query,
        int topK = 10
    );

    // 仅全文搜索
    std::vector<SearchResult> ftsSearch(
        const std::string& query,
        int topK = 10
    );

    // Hand-written inverted index search used for coursework comparison with
    // SQLite FTS5. It builds a term -> posting-list map over the stored chunks.
    std::vector<SearchResult> invertedSearch(
        const std::string& query,
        int topK = 10
    );

private:
    MemoryStore& m_store;
    std::unique_ptr<IEmbeddingProvider> m_embedding;

    // 合并两个搜索结果列表（按 path+line 去重，加权求和分数）
    static std::vector<SearchResult> mergeResults(
        const std::vector<SearchResult>& vectorResults,
        const std::vector<SearchResult>& textResults,
        double vectorWeight,
        double textWeight
    );

    // 归一化分数到 [0, 1]
    static void normalizeScores(std::vector<SearchResult>& results);
};

} // namespace clawlite
