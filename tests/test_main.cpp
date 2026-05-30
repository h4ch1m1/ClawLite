// ClawLite — 统一测试入口

#include <iostream>

// 各测试模块的入口函数声明
extern int run_skill_parser_tests();
extern int run_skill_registry_tests();
extern int run_chunker_tests();
extern int run_memory_store_tests();
extern int run_file_tree_index_tests();
extern int run_context_budgeter_tests();
extern int run_session_summary_tree_tests();
extern int run_search_tests();
extern int run_session_store_tests();
extern int run_prompt_builder_tests();
extern int run_llm_tests();
extern int run_compaction_tests();

int main() {
    std::cout << "========== ClawLite Tests ==========\n\n" << std::flush;

    int failures = 0;
    failures += run_skill_parser_tests();
    std::cout << "[DEBUG] skill tests done, failures=" << failures << "\n" << std::flush;
    failures += run_skill_registry_tests();
    std::cout << "[DEBUG] registry tests done, failures=" << failures << "\n" << std::flush;
    failures += run_chunker_tests();
    std::cout << "[DEBUG] chunker tests done, failures=" << failures << "\n" << std::flush;
    failures += run_memory_store_tests();
    std::cout << "[DEBUG] memory_store tests done, failures=" << failures << "\n" << std::flush;
    failures += run_file_tree_index_tests();
    std::cout << "[DEBUG] file_tree tests done, failures=" << failures << "\n" << std::flush;
    failures += run_context_budgeter_tests();
    std::cout << "[DEBUG] context_budgeter tests done, failures=" << failures << "\n" << std::flush;
    failures += run_session_summary_tree_tests();
    std::cout << "[DEBUG] summary_tree tests done, failures=" << failures << "\n" << std::flush;
    failures += run_search_tests();
    std::cout << "[DEBUG] search tests done, failures=" << failures << "\n" << std::flush;
    failures += run_session_store_tests();
    std::cout << "[DEBUG] session tests done, failures=" << failures << "\n" << std::flush;
    failures += run_compaction_tests();
    std::cout << "[DEBUG] compaction tests done, failures=" << failures << "\n" << std::flush;
    failures += run_prompt_builder_tests();
    std::cout << "[DEBUG] prompt tests done, failures=" << failures << "\n" << std::flush;
    failures += run_llm_tests();
    std::cout << "[DEBUG] llm tests done, failures=" << failures << "\n" << std::flush;

    std::cout << "\n========== Results ==========\n";
    if (failures == 0) {
        std::cout << "All tests passed!\n";
    } else {
        std::cout << failures << " test(s) failed.\n";
    }
    return failures;
}
