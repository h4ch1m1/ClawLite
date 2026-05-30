#pragma once

#include "llm/llm_client.h"
#include <string>

namespace clawlite {

struct MemoryConfig {
    int contextTokenBudget = 12000;
    int chunkTokens = 220;
    int overlapTokens = 50;
    int summaryGroupSize = 6;
    int keepRecentTurns = 4;
    int fileCacheCapacity = 32;
};

struct AppConfig {
    LlmConfig llm;
    MemoryConfig memory;
    std::string workspaceDir = ".";
    std::string dataDir = ".clawlite";
};

AppConfig loadAppConfig(const std::string& path = "clawlite_config.env");

} // namespace clawlite
