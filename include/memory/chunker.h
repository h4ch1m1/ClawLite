#pragma once

#include "core/types.h"
#include <string>
#include <vector>

namespace clawlite {

struct ChunkerConfig {
    int chunkTokens = 200;
    int overlapTokens = 50;
    double charsPerToken = 4.0;
    bool cjkCharacterMode = false;
};

class Chunker {
public:
    static std::vector<MemoryChunk> chunkMarkdown(
        const std::string& filePath,
        const std::string& content,
        const ChunkerConfig& config = {}
    );

    static int estimateTokens(const std::string& text, double charsPerToken = 4.0);

private:
    static std::string computeHash(const std::string& text);
};

} // namespace clawlite
