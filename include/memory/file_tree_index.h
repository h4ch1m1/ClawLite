#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace clawlite {

struct FileTreeNode {
    std::string path;
    bool isDirectory = false;
    int parent = -1;
    std::vector<int> children;
    std::string hash;
    int64_t mtimeMs = 0;
    int64_t size = 0;
    bool dirty = true;
};

struct FileTreeStats {
    int fileCount = 0;
    int directoryCount = 0;
    int nodeCount = 0;
    std::string rootHash;
    std::vector<std::string> changedFiles;
};

class FileTreeIndex {
public:
    FileTreeStats build(const std::string& rootPath);

    const std::vector<FileTreeNode>& nodes() const { return m_nodes; }
    const FileTreeStats& stats() const { return m_stats; }
    const std::string& rootHash() const { return m_stats.rootHash; }

private:
    std::vector<FileTreeNode> m_nodes;
    FileTreeStats m_stats;
    std::unordered_map<std::string, std::string> m_previousHashes;

    int buildNode(const std::string& path, int parent, const std::string& rootPath);
};

} // namespace clawlite
