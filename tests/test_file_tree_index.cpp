#include "memory/file_tree_index.h"
#include "test_helpers.h"

#include <filesystem>
#include <fstream>
#include <iostream>

using namespace clawlite;

void testFileTreeBuildAndDirtyTracking() {
    const std::string root = "clawlite_tree_test";
    std::filesystem::create_directories(root + "/sub");
    {
        std::ofstream out(root + "/a.md");
        out << "# A\nhello";
    }
    {
        std::ofstream out(root + "/sub/b.md");
        out << "# B\nworld";
    }

    FileTreeIndex index;
    auto first = index.build(root);
    TEST_ASSERT(first.fileCount == 2);
    TEST_ASSERT(first.directoryCount >= 2);
    TEST_ASSERT(!first.rootHash.empty());
    TEST_ASSERT(first.changedFiles.size() == 2);

    auto second = index.build(root);
    TEST_ASSERT(second.changedFiles.empty());

    {
        std::ofstream out(root + "/sub/b.md", std::ios::app);
        out << "\nchanged";
    }
    auto third = index.build(root);
    TEST_ASSERT(third.changedFiles.size() == 1);
    TEST_ASSERT(third.changedFiles[0].find("b.md") != std::string::npos);

    std::filesystem::remove_all(root);
    std::cout << "  [PASS] testFileTreeBuildAndDirtyTracking\n";
}

int run_file_tree_index_tests() {
    std::cout << "FileTreeIndex Tests:\n";
    RESET_FAILURES();
    testFileTreeBuildAndDirtyTracking();
    int f = GET_FAILURES();
    if (f == 0) std::cout << "All FileTreeIndex tests passed.\n";
    else std::cout << f << " FileTreeIndex test(s) failed.\n";
    return f;
}
