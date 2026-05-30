// ClawLite — 技能解析器测试

#include "skill/skill_parser.h"
#include "test_helpers.h"
#include <iostream>
#include <fstream>
#include <filesystem>

using namespace clawlite;

void testParseFrontmatter() {
    std::string block = R"(name: weather
description: Get weather information for a city
user-invocable: true
)";

    auto result = SkillParser::parseFrontmatter(block);
    TEST_ASSERT(result["name"] == "weather");
    TEST_ASSERT(result["description"] == "Get weather information for a city");
    TEST_ASSERT(result["user-invocable"] == "true");
    std::cout << "  [PASS] testParseFrontmatter\n";
}

void testExtractSections() {
    std::string content = R"(---
name: test
description: A test skill
---
This is the body content.
Multiple lines.
)";

    auto [fm, body] = SkillParser::extractSections(content);
    TEST_ASSERT(!fm.empty());
    TEST_ASSERT(body.find("This is the body content.") != std::string::npos);
    std::cout << "  [PASS] testExtractSections\n";
}

void testParseComplete() {
    namespace fs = std::filesystem;
    fs::path temp = fs::temp_directory_path() / "clawlite_skill_test";
    fs::create_directories(temp);

    fs::path skillFile = temp / "SKILL.md";
    {
        std::ofstream out(skillFile);
        out << "---\n";
        out << "name: temp-skill\n";
        out << "description: A temporary skill\n";
        out << "user-invocable: true\n";
        out << "---\n";
        out << "Body text here.\n";
    }

    auto parsed = SkillParser::parse(skillFile.string());
    TEST_ASSERT(parsed.has_value());
    TEST_ASSERT(parsed->name == "temp-skill");
    TEST_ASSERT(parsed->description == "A temporary skill");
    TEST_ASSERT(parsed->body.find("Body text here.") != std::string::npos);
    std::cout << "  [PASS] testParseComplete\n";
}

int run_skill_parser_tests() {
    std::cout << "Skill Parser Tests:\n";
    RESET_FAILURES();
    testParseFrontmatter();
    testExtractSections();
    testParseComplete();
    int f = GET_FAILURES();
    if (f == 0) std::cout << "All skill parser tests passed.\n";
    else std::cout << f << " skill parser test(s) failed.\n";
    return f;
}
