#pragma once

#include "llm/llm_client.h"
#include <string>

namespace clawlite {

struct AppConfig {
    LlmConfig llm;
};

AppConfig loadAppConfig(const std::string& path = "clawlite_config.env");

} // namespace clawlite
