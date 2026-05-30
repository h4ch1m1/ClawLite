cmake_minimum_required(VERSION 3.16)
# 强制指定 Windows 版本为 Win10，避免 httplib 误判
if(WIN32)
    add_definitions(-D_WIN32_WINNT=0x0A00)
endif()
project(ClawLite VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# UTF-8 编译支持（源文件和字符串字面量都用 UTF-8）
add_compile_options(-finput-charset=UTF-8 -fexec-charset=UTF-8)

# ── 第三方库 ──────────────────────────────────────────────

# SQLite3 (amalgamation: 把 sqlite3.c 和 sqlite3.h 放到 third_party/sqlite3/)
add_library(sqlite3 STATIC third_party/sqlite3/sqlite3.c)
set_target_properties(sqlite3 PROPERTIES LINKER_LANGUAGE C)
target_include_directories(sqlite3 PUBLIC third_party/sqlite3)
target_compile_definitions(sqlite3 PRIVATE
    SQLITE_ENABLE_FTS5
    SQLITE_ENABLE_JSON1
)

# yaml-cpp (如果用 vcpkg 则 find_package，否则手动放到 third_party/yaml-cpp/)
# find_package(yaml-cpp REQUIRED)
# 或者：add_subdirectory(third_party/yaml-cpp)

# cpp-httplib (header-only，放到 third_party/httplib/httplib.h)
# target_include_directories 已在各模块中处理

# ── 核心库 ────────────────────────────────────────────────

add_library(clawlite_core STATIC
    src/core/types.cpp
    src/core/event_bus.cpp
    src/core/config.cpp
)
target_include_directories(clawlite_core PUBLIC include)

# ── 模块 A：Skill ─────────────────────────────────────────

add_library(clawlite_skill STATIC
    src/skill/skill_parser.cpp
    src/skill/skill_loader.cpp
    src/skill/skill_registry.cpp
    src/skill/skill_filter.cpp
)
target_include_directories(clawlite_skill PUBLIC include)
target_link_libraries(clawlite_skill PUBLIC clawlite_core)

# ── 模块 B：Memory ────────────────────────────────────────

add_library(clawlite_memory STATIC
    src/memory/context_engine.cpp
    src/memory/file_tree_index.cpp
    src/memory/context_budgeter.cpp
    src/memory/session_summary_tree.cpp
    src/memory/memory_store.cpp
    src/memory/chunker.cpp
    src/memory/embedding.cpp
    src/memory/search_manager.cpp
    src/memory/session_store.cpp
    src/memory/compaction.cpp
)
target_include_directories(clawlite_memory PUBLIC include)
target_link_libraries(clawlite_memory PUBLIC clawlite_core sqlite3)
# httplib（用于 RemoteEmbedding）
target_include_directories(clawlite_memory PUBLIC third_party/httplib)
if(WIN32)
    target_link_libraries(clawlite_memory PUBLIC ws2_32)
endif()

# ── 模块 C：LLM ───────────────────────────────────────────

add_library(clawlite_llm STATIC
    src/llm/llm_client.cpp
    src/llm/prompt_builder.cpp
    src/llm/runtime_plan.cpp
    src/llm/harness.cpp
    src/llm/tool_executor.cpp
)
target_include_directories(clawlite_llm PUBLIC include)
target_link_libraries(clawlite_llm PUBLIC clawlite_core)

# ── 主程序 ────────────────────────────────────────────────

add_executable(clawlite src/main.cpp)
target_link_libraries(clawlite PRIVATE
    clawlite_core
    clawlite_skill
    clawlite_memory
    clawlite_llm
)
# Windows 下需要链接 Winsock
if(WIN32)
    target_link_libraries(clawlite PRIVATE ws2_32)
endif()

add_executable(clawlite_gui src/gui_server.cpp)
target_include_directories(clawlite_gui PRIVATE third_party/httplib)
target_link_libraries(clawlite_gui PRIVATE
    clawlite_core
    clawlite_skill
    clawlite_memory
    clawlite_llm
)
if(WIN32)
    target_link_libraries(clawlite_gui PRIVATE ws2_32)
endif()

# ── 测试 ──────────────────────────────────────────────────

enable_testing()

set(TEST_SOURCES
    tests/test_main.cpp
    tests/test_skill_parser.cpp
    tests/test_skill_registry.cpp
    tests/test_memory_store.cpp
    tests/test_file_tree_index.cpp
    tests/test_context_budgeter.cpp
    tests/test_session_summary_tree.cpp
    tests/test_chunker.cpp
    tests/test_search.cpp
    tests/test_session_store.cpp
    tests/test_compaction.cpp
    tests/test_prompt_builder.cpp
    tests/test_llm.cpp
)

add_executable(clawlite_tests ${TEST_SOURCES})
target_link_libraries(clawlite_tests PRIVATE
    clawlite_core
    clawlite_skill
    clawlite_memory
    clawlite_llm
)
if(WIN32)
    target_link_libraries(clawlite_tests PRIVATE ws2_32)
endif()

add_test(NAME all_tests COMMAND clawlite_tests)
