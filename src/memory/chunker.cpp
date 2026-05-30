// ClawLite — 滑动窗口分块算法实现
// TODO: B 同学实现 — 数据结构课程重点！

#include "memory/chunker.h"
#include <sstream>
#include <algorithm>
#include <functional>
#include <cctype>

namespace clawlite {
namespace {

struct Section {
    int startLine = 1;
    int endLine = 1;
    int depth = 0;
    std::string headingPath;
    std::vector<std::string> lines;
};

int utf8Codepoints(const std::string& text) {
    int count = 0;
    for (unsigned char ch : text) {
        if ((ch & 0xC0) != 0x80) ++count;
    }
    return count;
}

int lineCost(const std::string& line, const ChunkerConfig& config) {
    return (config.cjkCharacterMode ? utf8Codepoints(line) : static_cast<int>(line.size())) + 1;
}

int headingLevel(const std::string& line) {
    int count = 0;
    while (count < static_cast<int>(line.size()) && count < 6 &&
           line[static_cast<size_t>(count)] == '#') {
        ++count;
    }
    if (count == 0 || count >= static_cast<int>(line.size())) return 0;
    return std::isspace(static_cast<unsigned char>(line[static_cast<size_t>(count)])) ? count : 0;
}

std::string headingTitle(const std::string& line, int level) {
    std::string title = line.substr(static_cast<size_t>(level));
    while (!title.empty() && std::isspace(static_cast<unsigned char>(title.front()))) {
        title.erase(title.begin());
    }
    while (!title.empty() && std::isspace(static_cast<unsigned char>(title.back()))) {
        title.pop_back();
    }
    return title;
}

std::string joinHeadings(const std::vector<std::string>& headings) {
    std::string result;
    for (const auto& h : headings) {
        if (h.empty()) continue;
        if (!result.empty()) result += " > ";
        result += h;
    }
    return result;
}

std::vector<Section> buildSections(const std::vector<std::string>& lines) {
    std::vector<Section> sections;
    std::vector<std::string> headings(6);
    Section current;
    current.startLine = 1;

    auto flush = [&](int endLine) {
        if (current.lines.empty()) return;
        current.endLine = endLine;
        sections.push_back(current);
        current = Section{};
    };

    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        const auto& line = lines[static_cast<size_t>(i)];
        int level = headingLevel(line);
        if (level > 0) {
            flush(i);
            headings[static_cast<size_t>(level - 1)] = headingTitle(line, level);
            for (int j = level; j < 6; ++j) headings[static_cast<size_t>(j)].clear();
            current.startLine = i + 1;
            current.depth = level;
            current.headingPath = joinHeadings(headings);
        } else if (current.lines.empty()) {
            current.startLine = i + 1;
            current.headingPath = joinHeadings(headings);
            current.depth = 0;
            for (const auto& h : headings) {
                if (!h.empty()) ++current.depth;
            }
        }
        current.lines.push_back(line);
    }
    flush(static_cast<int>(lines.size()));
    return sections;
}

} // namespace

std::vector<MemoryChunk> Chunker::chunkMarkdown(
    const std::string& filePath,
    const std::string& content,
    const ChunkerConfig& config
) {
    // TODO: 实现 — 滑动窗口算法
    //
    // 参考：openclaw-main/packages/memory-host-sdk/host/internal.ts:chunkMarkdown
    //
    // 伪代码：
    //   lines = split(content, '\n')
    //   maxChars = chunkTokens * charsPerToken
    //   stepChars = (chunkTokens - overlapTokens) * charsPerToken
    //   start = 0
    //   chunks = []
    //   while start < lines.size():
    //       end = start
    //       charCount = 0
    //       // 扩展窗口右边界
    //       while end < lines.size() && charCount < maxChars:
    //           charCount += len(lines[end]) + 1  // +1 for newline
    //           end++
    //       // 生成 chunk
    //       text = join(lines[start:end], '\n')
    //       chunk = MemoryChunk {
    //           path: filePath,
    //           startLine: start + 1,  // 1-indexed
    //           endLine: end,
    //           text: text,
    //           hash: computeHash(text)
    //       }
    //       chunks.push_back(chunk)
    //       // 滑动窗口：起点前进 stepSize 行
    //       stepLines = 0
    //       stepCharsUsed = 0
    //       while stepCharsUsed < stepChars && start < end:
    //           stepCharsUsed += len(lines[start]) + 1
    //           start++
    //           stepLines++
    //   return chunks
    //
    // 关键点：
    //   - 滑动窗口的大小由字符数决定（近似 token 数）
    //   - 重叠由 stepSize < windowSize 实现
    //   - CJK 文本特殊处理（按字符而非空格分割）

    std::vector<MemoryChunk> chunks;

    // 按行分割
    std::vector<std::string> lines;
    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }

    if (lines.empty()) return chunks;

    int maxChars = std::max(1, static_cast<int>(config.chunkTokens * config.charsPerToken));
    int stepChars = static_cast<int>((config.chunkTokens - config.overlapTokens) * config.charsPerToken);
    if (stepChars <= 0) stepChars = std::max(1, maxChars / 2);

    auto sections = buildSections(lines);
    for (const auto& section : sections) {
        int start = 0;
        while (start < static_cast<int>(section.lines.size())) {
            int end = start;
            int charCount = 0;
            while (end < static_cast<int>(section.lines.size()) && charCount < maxChars) {
                charCount += lineCost(section.lines[static_cast<size_t>(end)], config);
                ++end;
            }

            std::string text;
            for (int i = start; i < end; ++i) {
                if (i > start) text += '\n';
                text += section.lines[static_cast<size_t>(i)];
            }

            MemoryChunk chunk;
            chunk.path = filePath;
            chunk.startLine = section.startLine + start;
            chunk.endLine = section.startLine + end - 1;
            chunk.text = text;
            chunk.hash = computeHash(filePath + ":" + std::to_string(chunk.startLine) + ":" + text);
            chunk.id = filePath + ":" + std::to_string(chunk.startLine) + "-" +
                std::to_string(chunk.endLine) + ":" + chunk.hash;
            chunk.headingPath = section.headingPath;
            chunk.depth = section.depth;
            chunk.parentId = computeHash(section.headingPath.empty() ? filePath : section.headingPath);
            chunk.tokenCost = estimateTokens(chunk.text, config.charsPerToken);
            chunks.push_back(std::move(chunk));

            int stepUsed = 0;
            while (stepUsed < stepChars && start < end) {
                stepUsed += lineCost(section.lines[static_cast<size_t>(start)], config);
                ++start;
            }
            if (start == end && end < static_cast<int>(section.lines.size())) ++start;
        }
    }

    return chunks;
}

int Chunker::estimateTokens(const std::string& text, double charsPerToken) {
    return static_cast<int>(text.size() / charsPerToken) + 1;
}

std::string Chunker::computeHash(const std::string& text) {
    // 简单的 hash 实现（std::hash）
    size_t h = std::hash<std::string>{}(text);
    // 转为 hex 字符串
    char buf[20];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return std::string(buf);
}

} // namespace clawlite
