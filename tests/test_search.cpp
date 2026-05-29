#include "memory/search_manager.h"
#include "memory/memory_store.h"
#include "memory/embedding.h"
#include "test_helpers.h"
#include <iostream>

using namespace clawlite;

void testCosineSimilarity() {
    std::vector<double> a = {1.0, 0.0, 0.0};
    std::vector<double> b = {1.0, 0.0, 0.0};
    std::vector<double> c = {0.0, 1.0, 0.0};

    TEST_ASSERT(cosineSimilarity(a, b) > 0.99);
    TEST_ASSERT(cosineSimilarity(a, c) < 0.01);
    std::cout << "  [PASS] testCosineSimilarity\n";
}

void testVectorSearch() {
    MemoryStore store;
    store.open(":memory:");
    LocalMockEmbedding embedder(128);

    MemoryChunk chunk1;
    chunk1.path = "doc.md";
    chunk1.startLine = 1;
    chunk1.endLine = 5;
    chunk1.text = "The quick brown fox jumps over the lazy dog";
    chunk1.hash = "h1";
    chunk1.embedding = embedder.embedQuery(chunk1.text);
    store.upsertChunk(chunk1);

    MemoryChunk chunk2;
    chunk2.path = "doc.md";
    chunk2.startLine = 6;
    chunk2.endLine = 10;
    chunk2.text = "Machine learning is a subset of artificial intelligence";
    chunk2.hash = "h2";
    chunk2.embedding = embedder.embedQuery(chunk2.text);
    store.upsertChunk(chunk2);

    SearchManager search(store, std::make_unique<LocalMockEmbedding>(128));
    auto results = search.vectorSearch("fox", 1);
    TEST_ASSERT(results.size() == 1);
    std::cout << "  [PASS] testVectorSearch\n";

    store.close();
}

void testHybridSearch() {
    MemoryStore store;
    store.open(":memory:");
    LocalMockEmbedding embedder(128);

    MemoryChunk chunk;
    chunk.path = "manual.md";
    chunk.startLine = 1;
    chunk.endLine = 1;
    chunk.text = "alpha beta beta";
    chunk.hash = "ih1";
    chunk.embedding = embedder.embedQuery(chunk.text);
    store.upsertChunk(chunk);

    MemoryChunk chunk2;
    chunk2.path = "manual.md";
    chunk2.startLine = 2;
    chunk2.endLine = 2;
    chunk2.text = "gamma delta";
    chunk2.hash = "ih2";
    chunk2.embedding = embedder.embedQuery(chunk2.text);
    store.upsertChunk(chunk2);

    SearchManager search(store, std::make_unique<LocalMockEmbedding>(128));
    auto inverted = search.invertedSearch("beta", 5);
    TEST_ASSERT(!inverted.empty());
    TEST_ASSERT(inverted[0].startLine == 1);

    auto hybrid = search.search("beta", SearchConfig{});
    TEST_ASSERT(!hybrid.empty());

    store.close();
    std::cout << "  [PASS] testHybridSearch\n";
}

int run_search_tests() {
    std::cout << "Search Tests:\n";
    RESET_FAILURES();
    testCosineSimilarity();
    testVectorSearch();
    testHybridSearch();
    int f = GET_FAILURES();
    if (f == 0) std::cout << "All search tests passed.\n";
    else std::cout << f << " search test(s) failed.\n";
    return f;
}
