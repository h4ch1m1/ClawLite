#include "memory/memory_store.h"
#include "memory/lru_cache.h"
#include "test_helpers.h"
#include <iostream>

using namespace clawlite;

void testOpenClose() {
    MemoryStore store;
    TEST_ASSERT(store.open(":memory:"));
    store.close();
    std::cout << "  [PASS] testOpenClose\n";
}

void testFileCrud() {
    MemoryStore store;
    store.open(":memory:");

    FileEntry entry;
    entry.path = "test.md";
    entry.hash = "abc123";
    entry.mtimeMs = 1000;
    entry.size = 500;

    store.upsertFile(entry);
    TEST_ASSERT(store.fileCount() == 1);

    auto got = store.getFile("test.md");
    TEST_ASSERT(got.has_value());
    if (got.has_value()) {
        TEST_ASSERT(got->path == "test.md");
        TEST_ASSERT(got->hash == "abc123");
    }

    store.removeFile("test.md");
    TEST_ASSERT(store.fileCount() == 0);

    store.close();
    std::cout << "  [PASS] testFileCrud\n";
}

void testChunkCrud() {
    MemoryStore store;
    store.open(":memory:");

    MemoryChunk chunk;
    chunk.path = "test.md";
    chunk.startLine = 1;
    chunk.endLine = 10;
    chunk.text = "Hello world";
    chunk.hash = "def456";

    store.upsertChunk(chunk);
    TEST_ASSERT(store.chunkCount() == 1);

    auto chunks = store.getChunksByFile("test.md");
    TEST_ASSERT(chunks.size() == 1);
    if (!chunks.empty()) {
        TEST_ASSERT(chunks[0].text == "Hello world");
    }

    store.removeChunksByFile("test.md");
    TEST_ASSERT(store.chunkCount() == 0);

    store.close();
    std::cout << "  [PASS] testChunkCrud\n";
}

void testFtsSearch() {
    MemoryStore store;
    store.open(":memory:");

    MemoryChunk chunk;
    chunk.path = "fts.md";
    chunk.startLine = 1;
    chunk.endLine = 1;
    chunk.text = "needle haystack";
    chunk.hash = "fts1";
    store.upsertChunk(chunk);

    auto results = store.ftsSearch("needle", 5);
    TEST_ASSERT(!results.empty());
    TEST_ASSERT(results[0].first == "fts1");

    store.close();
    std::cout << "  [PASS] testFtsSearch\n";
}

void testEmbeddingCache() {
    MemoryStore store;
    store.open(":memory:");

    std::vector<double> embedding = {0.1, 0.2, 0.3};
    store.cacheEmbedding("hash1", "local", "mock", embedding);

    auto cached = store.getCachedEmbedding("hash1", "local", "mock");
    TEST_ASSERT(cached.has_value());
    if (cached.has_value()) {
        TEST_ASSERT(cached->size() == 3);
        TEST_ASSERT((*cached)[0] == 0.1);
    }

    store.close();
    std::cout << "  [PASS] testEmbeddingCache\n";
}

void testLruCacheEvictsLeastRecentlyUsed() {
    LruCache<std::string, int> cache(2);
    cache.put("a", 1);
    cache.put("b", 2);
    TEST_ASSERT(cache.get("a").value_or(0) == 1);
    cache.put("c", 3);
    TEST_ASSERT(cache.contains("a"));
    TEST_ASSERT(!cache.contains("b"));
    TEST_ASSERT(cache.contains("c"));
    std::cout << "  [PASS] testLruCacheEvictsLeastRecentlyUsed\n";
}

int run_memory_store_tests() {
    std::cout << "Memory Store Tests:\n";
    RESET_FAILURES();
    testOpenClose();
    testFileCrud();
    testChunkCrud();
    testFtsSearch();
    testEmbeddingCache();
    testLruCacheEvictsLeastRecentlyUsed();
    int f = GET_FAILURES();
    if (f == 0) std::cout << "All memory store tests passed.\n";
    else std::cout << f << " memory store test(s) failed.\n";
    return f;
}
