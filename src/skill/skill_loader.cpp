// ClawLite — 技能加载器实现

#include "skill/skill_loader.h"
#include <filesystem>
#include <unordered_map>
#include <algorithm>
#include <cstdlib>

namespace fs = std::filesystem;

namespace clawlite {
namespace {

static std::string homeDirectory() {
#ifdef _WIN32
    if (const char* profile = std::getenv("USERPROFILE")) return profile;
    const char* drive = std::getenv("HOMEDRIVE");
    const char* path = std::getenv("HOMEPATH");
    if (drive && path) return std::string(drive) + path;
#else
    if (const char* home = std::getenv("HOME")) return home;
#endif
    return {};
}

static bool startsWithPath(const std::string& path, const std::string& root) {
    if (root.empty()) return false;
    if (path.size() < root.size()) return false;
    if (path.compare(0, root.size(), root) != 0) return false;
    if (path.size() == root.size()) return true;
    const char sep = fs::path::preferred_separator;
    return path[root.size()] == sep;
}

} // namespace

std::vector<SkillEntry> SkillLoader::loadFromDir(const std::string& dir, SkillSource source) {
    std::vector<SkillEntry> result;

    if (dir.empty()) return result;

    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) {
        return result;
    }

    fs::path rootPath = fs::path(dir);
    rootPath = fs::weakly_canonical(rootPath, ec);
    if (ec) {
        rootPath = fs::path(dir);
    }
    const std::string root = rootPath.string();

    auto tryLoadSkillFile = [&](const fs::path& skillFile) {
        std::error_code sec;
        if (!fs::exists(skillFile, sec) || !fs::is_regular_file(skillFile, sec)) return;
        if (!isSafePath(skillFile.string(), root)) return;

        auto parsed = SkillParser::parse(skillFile.string());
        if (!parsed) return;

        SkillEntry entry;
        entry.definition = std::move(*parsed);
        entry.source = source;
        entry.sourceDir = dir;
        result.push_back(std::move(entry));
    };

    // Support both a direct SKILL.md under dir and the expected one-level layout:
    // dir/<skill_name>/SKILL.md
    tryLoadSkillFile(fs::path(dir) / "SKILL.md");

    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_directory()) continue;

        fs::path skillFile = entry.path() / "SKILL.md";
        tryLoadSkillFile(skillFile);
    }

    return result;
}

std::vector<SkillEntry> SkillLoader::loadAndMerge(
    const std::vector<std::pair<std::string, SkillSource>>& dirs
) {
    std::vector<std::pair<std::string, SkillSource>> ordered = dirs;
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
        return static_cast<int>(a.second) < static_cast<int>(b.second);
    });

    std::unordered_map<std::string, SkillEntry> merged;
    for (const auto& [dir, source] : ordered) {
        auto entries = loadFromDir(dir, source);
        for (auto& entry : entries) {
            auto it = merged.find(entry.definition.name);
            if (it == merged.end() || static_cast<int>(entry.source) >= static_cast<int>(it->second.source)) {
                merged[entry.definition.name] = std::move(entry);
            }
        }
    }

    std::vector<SkillEntry> result;
    result.reserve(merged.size());
    for (auto& [name, entry] : merged) {
        result.push_back(std::move(entry));
    }
    return result;
}

std::vector<std::pair<std::string, SkillSource>> SkillLoader::getDefaultDirs(
    const std::string& workspaceDir
) {
    std::vector<std::pair<std::string, SkillSource>> dirs;

    // Lowest priority first, highest priority last.
    dirs.push_back({workspaceDir + "/skills", SkillSource::Builtin});

    const std::string home = homeDirectory();
    if (!home.empty()) {
        dirs.push_back({home + "/.clawlite/skills", SkillSource::User});
    }

    // Project-local skills. In the provided template this is the same root as the
    // bundled skills directory, but it still participates in the priority merge.
    dirs.push_back({workspaceDir + "/skills", SkillSource::Project});

    return dirs;
}

bool SkillLoader::isSafePath(const std::string& path, const std::string& root) {
    try {
        std::error_code ec;
        fs::path canonicalPath = fs::weakly_canonical(fs::path(path), ec);
        if (ec) canonicalPath = fs::absolute(fs::path(path));
        fs::path canonicalRoot = fs::weakly_canonical(fs::path(root), ec);
        if (ec) canonicalRoot = fs::absolute(fs::path(root));

        std::string pathStr = canonicalPath.lexically_normal().string();
        std::string rootStr = canonicalRoot.lexically_normal().string();

        if (pathStr == rootStr) return true;
        return startsWithPath(pathStr, rootStr);
    } catch (...) {
        return false;
    }
}

} // namespace clawlite
