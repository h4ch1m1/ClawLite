#include "memory/file_tree_index.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace clawlite {
namespace {

uint64_t fnv1a(const std::string& text) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char ch : text) {
        h ^= ch;
        h *= 1099511628211ULL;
    }
    return h;
}

std::string toHex(uint64_t value) {
    char buf[17];
    snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(value));
    return std::string(buf);
}

std::string stableHash(const std::string& text) {
    return toHex(fnv1a(text));
}

bool shouldSkipDir(const std::filesystem::path& path) {
    const auto name = path.filename().string();
    return name == ".git" || name == ".clawlite" || name == "build" ||
           name.rfind("build-", 0) == 0;
}

int64_t fileTimeMs(const std::filesystem::path& path) {
    try {
        return std::filesystem::last_write_time(path).time_since_epoch().count() / 10000;
    } catch (...) {
        return 0;
    }
}

std::string readFileHash(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "0";
    std::ostringstream ss;
    ss << in.rdbuf();
    return stableHash(ss.str());
}

std::string relativeDisplayPath(const std::filesystem::path& path,
                                const std::filesystem::path& root) {
    try {
        if (path == root) return path.filename().string();
        return std::filesystem::relative(path, root).generic_string();
    } catch (...) {
        return path.generic_string();
    }
}

} // namespace

FileTreeStats FileTreeIndex::build(const std::string& rootPath) {
    std::unordered_map<std::string, std::string> previous = std::move(m_previousHashes);
    m_previousHashes = previous;
    m_nodes.clear();
    m_stats = FileTreeStats{};

    std::filesystem::path root = std::filesystem::absolute(rootPath);
    int rootIndex = buildNode(root.string(), -1, root.string());
    if (rootIndex >= 0) {
        m_stats.rootHash = m_nodes[static_cast<size_t>(rootIndex)].hash;
    }
    m_stats.nodeCount = static_cast<int>(m_nodes.size());

    std::unordered_map<std::string, std::string> fresh;
    for (const auto& node : m_nodes) {
        fresh[node.path] = node.hash;
    }
    for (auto& node : m_nodes) {
        auto it = previous.find(node.path);
        node.dirty = (it == previous.end() || it->second != node.hash);
        if (node.dirty && !node.isDirectory) {
            m_stats.changedFiles.push_back(node.path);
        }
    }
    m_previousHashes = std::move(fresh);
    return m_stats;
}

int FileTreeIndex::buildNode(const std::string& rawPath, int parent, const std::string& rawRoot) {
    std::filesystem::path path(rawPath);
    std::filesystem::path root(rawRoot);
    std::error_code ec;
    bool isDir = std::filesystem::is_directory(path, ec);
    bool isFile = std::filesystem::is_regular_file(path, ec);
    if (ec || (!isDir && !isFile)) return -1;
    if (isDir && shouldSkipDir(path)) return -1;

    FileTreeNode node;
    node.path = relativeDisplayPath(path, root);
    node.isDirectory = isDir;
    node.parent = parent;
    node.mtimeMs = fileTimeMs(path);
    if (isFile) {
        node.size = static_cast<int64_t>(std::filesystem::file_size(path, ec));
        if (ec) node.size = 0;
    }

    const int index = static_cast<int>(m_nodes.size());
    m_nodes.push_back(node);

    if (isDir) {
        m_stats.directoryCount++;
        std::vector<std::filesystem::path> children;
        for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
            if (!ec) children.push_back(entry.path());
        }
        std::sort(children.begin(), children.end());

        std::string combined = "D:" + m_nodes[static_cast<size_t>(index)].path;
        for (const auto& child : children) {
            int childIndex = buildNode(child.string(), index, rawRoot);
            if (childIndex < 0) continue;
            m_nodes[static_cast<size_t>(index)].children.push_back(childIndex);
            combined += "|" + m_nodes[static_cast<size_t>(childIndex)].hash;
        }
        m_nodes[static_cast<size_t>(index)].hash = stableHash(combined);
    } else {
        m_stats.fileCount++;
        std::string contentHash = readFileHash(path);
        std::string combined = "F:" + m_nodes[static_cast<size_t>(index)].path +
            ":" + std::to_string(m_nodes[static_cast<size_t>(index)].mtimeMs) +
            ":" + std::to_string(m_nodes[static_cast<size_t>(index)].size) +
            ":" + contentHash;
        m_nodes[static_cast<size_t>(index)].hash = stableHash(combined);
    }

    return index;
}

} // namespace clawlite
