#include "memory/search_manager.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <queue>
#include <unordered_map>

namespace clawlite {
namespace {

std::string resultKey(const SearchResult& r) {
    return r.path + ":" + std::to_string(r.startLine) + ":" + std::to_string(r.endLine);
}

std::vector<std::string> tokenizeAscii(const std::string& text) {
    std::vector<std::string> terms;
    std::string current;
    for (unsigned char ch : text) {
        if (std::isalnum(ch)) {
            current.push_back(static_cast<char>(std::tolower(ch)));
        } else {
            if (!current.empty()) {
                terms.push_back(current);
                current.clear();
            }
        }
    }
    if (!current.empty()) terms.push_back(current);
    return terms;
}

} // namespace

SearchManager::SearchManager(MemoryStore& store, std::unique_ptr<IEmbeddingProvider> embedding)
    : m_store(store), m_embedding(std::move(embedding)) {}

std::vector<SearchResult> SearchManager::search(
    const std::string& query,
    const SearchConfig& config
) {
    auto vectorResults = vectorSearch(query, config.vectorCandidateLimit);
    auto textResults = ftsSearch(query, config.ftsCandidateLimit);
    if (textResults.empty()) {
        textResults = invertedSearch(query, config.ftsCandidateLimit);
    }

    auto merged = mergeResults(vectorResults, textResults, config.vectorWeight, config.textWeight);
    std::sort(merged.begin(), merged.end(),
        [](const SearchResult& a, const SearchResult& b) { return a.score > b.score; });
    if ((int)merged.size() > config.topK) merged.resize(config.topK);
    return merged;
}

std::vector<SearchResult> SearchManager::vectorSearch(const std::string& query, int topK) {
    std::vector<SearchResult> results;
    if (query.empty() || topK <= 0 || !m_embedding) return results;

    auto queryEmbedding = m_embedding->embedQuery(query);
    auto allChunks = m_store.getAllChunks();

    auto worseFirst = [](const SearchResult& a, const SearchResult& b) {
        return a.score > b.score;
    };
    std::priority_queue<SearchResult, std::vector<SearchResult>, decltype(worseFirst)> heap(worseFirst);

    for (const auto& chunk : allChunks) {
        if (chunk.embedding.empty()) continue;
        double score = cosineSimilarity(queryEmbedding, chunk.embedding);

        SearchResult r;
        r.path = chunk.path;
        r.startLine = chunk.startLine;
        r.endLine = chunk.endLine;
        r.score = score;
        r.vectorScore = score;
        r.snippet = chunk.text.substr(0, 200);
        r.source = SearchSource::Memory;

        if ((int)heap.size() < topK) {
            heap.push(r);
        } else if (score > heap.top().score) {
            heap.pop();
            heap.push(r);
        }
    }

    while (!heap.empty()) {
        results.push_back(heap.top());
        heap.pop();
    }
    std::sort(results.begin(), results.end(),
        [](const SearchResult& a, const SearchResult& b) { return a.score > b.score; });
    return results;
}

std::vector<SearchResult> SearchManager::ftsSearch(const std::string& query, int topK) {
    std::vector<SearchResult> results;
    if (query.empty() || topK <= 0) return results;

    auto chunks = m_store.getAllChunks();
    std::unordered_map<std::string, MemoryChunk> byHash;
    for (const auto& chunk : chunks) {
        byHash[chunk.hash] = chunk;
    }

    auto ftsResults = m_store.ftsSearch(query, topK);
    for (const auto& pair : ftsResults) {
        auto it = byHash.find(pair.first);
        if (it == byHash.end()) continue;

        const auto& chunk = it->second;
        SearchResult r;
        r.path = chunk.path;
        r.startLine = chunk.startLine;
        r.endLine = chunk.endLine;
        r.score = pair.second;
        r.textScore = pair.second;
        r.snippet = chunk.text.substr(0, 200);
        r.source = SearchSource::Memory;
        results.push_back(r);
    }
    return results;
}

std::vector<SearchResult> SearchManager::invertedSearch(const std::string& query, int topK) {
    std::vector<SearchResult> results;
    if (query.empty() || topK <= 0) return results;

    auto chunks = m_store.getAllChunks();
    std::unordered_map<std::string, std::vector<std::pair<size_t, int>>> postings;
    std::vector<std::unordered_map<std::string, int>> termFreqs(chunks.size());

    for (size_t i = 0; i < chunks.size(); ++i) {
        for (const auto& term : tokenizeAscii(chunks[i].text)) {
            termFreqs[i][term]++;
        }
        for (const auto& pair : termFreqs[i]) {
            postings[pair.first].push_back({i, pair.second});
        }
    }

    std::unordered_map<size_t, double> scores;
    double n = static_cast<double>(std::max<size_t>(chunks.size(), 1));
    for (const auto& term : tokenizeAscii(query)) {
        auto it = postings.find(term);
        if (it == postings.end()) continue;
        double idf = std::log((n + 1.0) / (static_cast<double>(it->second.size()) + 1.0)) + 1.0;
        for (const auto& posting : it->second) {
            scores[posting.first] += static_cast<double>(posting.second) * idf;
        }
    }

    for (const auto& pair : scores) {
        const auto& chunk = chunks[pair.first];
        SearchResult r;
        r.path = chunk.path;
        r.startLine = chunk.startLine;
        r.endLine = chunk.endLine;
        r.score = pair.second;
        r.textScore = pair.second;
        r.snippet = chunk.text.substr(0, 200);
        r.source = SearchSource::Memory;
        results.push_back(r);
    }

    std::sort(results.begin(), results.end(),
        [](const SearchResult& a, const SearchResult& b) { return a.score > b.score; });
    if ((int)results.size() > topK) results.resize(topK);
    return results;
}

std::vector<SearchResult> SearchManager::mergeResults(
    const std::vector<SearchResult>& vectorResults,
    const std::vector<SearchResult>& textResults,
    double vectorWeight,
    double textWeight
) {
    std::unordered_map<std::string, size_t> index;
    std::vector<SearchResult> merged;

    auto normVec = vectorResults;
    auto normText = textResults;
    normalizeScores(normVec);
    normalizeScores(normText);

    for (auto r : normVec) {
        r.score *= vectorWeight;
        index[resultKey(r)] = merged.size();
        merged.push_back(r);
    }

    for (auto r : normText) {
        r.score *= textWeight;
        auto key = resultKey(r);
        auto it = index.find(key);
        if (it == index.end()) {
            index[key] = merged.size();
            merged.push_back(r);
        } else {
            merged[it->second].score += r.score;
            merged[it->second].textScore = r.textScore;
        }
    }
    return merged;
}

void SearchManager::normalizeScores(std::vector<SearchResult>& results) {
    if (results.empty()) return;
    double maxScore = 0.0;
    for (const auto& r : results) {
        maxScore = std::max(maxScore, r.score);
    }
    if (maxScore <= 0.0) return;
    for (auto& r : results) {
        r.score /= maxScore;
    }
}

} // namespace clawlite
