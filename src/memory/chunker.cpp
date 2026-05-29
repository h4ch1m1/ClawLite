// ClawLite — 滑动窗口分块算法实现
// TODO: B 同学实现 — 数据结构课程重点！

#include "memory/chunker.h"
#include <sstream>
#include <algorithm>
#include <functional>

namespace clawlite {
namespace {

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

    int maxChars = static_cast<int>(config.chunkTokens * config.charsPerToken);
    int stepChars = static_cast<int>((config.chunkTokens - config.overlapTokens) * config.charsPerToken);

    // 滑动窗口算法
    // 参考：openclaw-main/packages/memory-host-sdk/host/internal.ts:chunkMarkdown
    int start = 0;
    while (start < (int)lines.size()) {
        int end = start;
        int charCount = 0;
        // 扩展窗口右边界
        while (end < (int)lines.size() && charCount < maxChars) {
            charCount += lineCost(lines[end], config);
            end++;
        }
        // 拼接文本
        std::string text;
        for (int i = start; i < end; i++) {
            if (i > start) text += '\n';
            text += lines[i];
        }
        MemoryChunk chunk;
        chunk.path = filePath;
        chunk.startLine = start + 1;  // 1-indexed
        chunk.endLine = end;
        chunk.text = text;
        chunk.hash = computeHash(text);
        chunks.push_back(chunk);
        // 滑动窗口前进
        int stepUsed = 0;
        while (stepUsed < stepChars && start < end) {
            stepUsed += lineCost(lines[start], config);
            start++;
        }
        // 防止无限循环：至少前进 1 行
        if (start == end && end < (int)lines.size()) start++;
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
