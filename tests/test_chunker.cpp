#include "memory/chunker.h"
#include "test_helpers.h"
#include <iostream>

using namespace clawlite;

void testBasicChunking() {
    std::string content;
    for (int i = 0; i < 100; i++) {
        content += "Line " + std::to_string(i) + ": This is a test line with some content.\n";
    }

    ChunkerConfig config;
    config.chunkTokens = 50;
    config.overlapTokens = 10;

    auto chunks = Chunker::chunkMarkdown("test.md", content, config);

    TEST_ASSERT(!chunks.empty());
    for (size_t i = 1; i < chunks.size(); i++) {
        TEST_ASSERT(chunks[i].startLine >= chunks[i - 1].startLine);
    }
    std::cout << "  [PASS] testBasicChunking (" << chunks.size() << " chunks)\n";
    std::cout << "  [PASS] testChunkContinuity\n";
}

void testEmptyContent() {
    auto chunks = Chunker::chunkMarkdown("empty.md", "");
    TEST_ASSERT(chunks.empty());
    std::cout << "  [PASS] testEmptyContent\n";
}

void testSmallContent() {
    std::string content = "Hello world";
    auto chunks = Chunker::chunkMarkdown("small.md", content);
    TEST_ASSERT(chunks.size() == 1);
    if (!chunks.empty()) {
        TEST_ASSERT(chunks[0].text == "Hello world");
    }
    std::cout << "  [PASS] testSmallContent\n";
}

void testOverlap() {
    std::string content;
    for (int i = 0; i < 30; ++i) {
        content += "line " + std::to_string(i) + " abcdefghijklmnopqrstuvwxyz\n";
    }
    ChunkerConfig config;
    config.chunkTokens = 12;
    config.overlapTokens = 6;
    auto chunks = Chunker::chunkMarkdown("overlap.md", content, config);
    TEST_ASSERT(chunks.size() > 1);
    TEST_ASSERT(chunks[1].startLine <= chunks[0].endLine);
    std::cout << "  [PASS] testOverlap\n";
}

void testCjkCharacterMode() {
    std::string content = "第一行中文内容\n第二行中文内容\n第三行中文内容\n第四行中文内容\n";
    ChunkerConfig config;
    config.chunkTokens = 3;
    config.overlapTokens = 1;
    config.charsPerToken = 1.0;
    config.cjkCharacterMode = true;
    auto chunks = Chunker::chunkMarkdown("cjk.md", content, config);
    TEST_ASSERT(!chunks.empty());
    TEST_ASSERT(chunks[0].text.find("第一行") != std::string::npos);
    std::cout << "  [PASS] testCjkCharacterMode\n";
}

int run_chunker_tests() {
    std::cout << "Chunker Tests:\n";
    RESET_FAILURES();
    testBasicChunking();
    testEmptyContent();
    testSmallContent();
    testOverlap();
    testCjkCharacterMode();
    int f = GET_FAILURES();
    if (f == 0) std::cout << "All chunker tests passed.\n";
    else std::cout << f << " chunker test(s) failed.\n";
    return f;
}
