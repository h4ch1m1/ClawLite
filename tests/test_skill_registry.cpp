// ClawLite — 技能注册表测试

#include "skill/skill_registry.h"
#include "test_helpers.h"
#include <iostream>

using namespace clawlite;

void testRegisterAndFind() {
    SkillRegistry registry;

    SkillEntry entry;
    entry.definition.name = "test_skill";
    entry.definition.description = "A test skill";
    entry.definition.filePath = "skills/test/SKILL.md";
    entry.source = SkillSource::Builtin;

    registry.registerSkill(entry);
    TEST_ASSERT(registry.size() == 1);

    auto found = registry.findSkill("test_skill");
    TEST_ASSERT(found != nullptr);
    TEST_ASSERT(found->definition.name == "test_skill");

    TEST_ASSERT(registry.findSkill("nonexistent") == nullptr);
    std::cout << "  [PASS] testRegisterAndFind\n";
}

void testBuildSkillPrompt() {
    SkillRegistry registry;

    for (int i = 0; i < 5; i++) {
        SkillEntry entry;
        entry.definition.name = "skill_" + std::to_string(i);
        entry.definition.description = "Description " + std::to_string(i);
        entry.definition.filePath = "skills/skill_" + std::to_string(i) + "/SKILL.md";
        entry.source = SkillSource::Builtin;
        registry.registerSkill(entry);
    }

    std::string prompt = registry.buildSkillPrompt(18000);
    TEST_ASSERT(!prompt.empty());
    TEST_ASSERT(prompt.find("<available_skills>") != std::string::npos);
    TEST_ASSERT(prompt.find("skill_0") != std::string::npos);
    std::cout << "  [PASS] testBuildSkillPrompt\n";
}

void testBinarySearchPromptLimit() {
    SkillRegistry registry;

    for (int i = 0; i < 50; ++i) {
        SkillEntry entry;
        entry.definition.name = "long_skill_" + std::to_string(i);
        entry.definition.description = std::string(100, 'x');
        entry.definition.filePath = "skills/long_skill_" + std::to_string(i) + "/SKILL.md";
        entry.source = SkillSource::Builtin;
        registry.registerSkill(entry);
    }

    std::string prompt = registry.buildSkillPrompt(500);
    TEST_ASSERT(prompt.size() <= 500);
    TEST_ASSERT(prompt.find("<available_skills>") != std::string::npos);
    std::cout << "  [PASS] testBinarySearchPromptLimit\n";
}

void testTriePrefixCompletion() {
    SkillRegistry registry;
    for (const auto& name : {"calculator", "calendar", "file-reader", "hello"}) {
        SkillEntry entry;
        entry.definition.name = name;
        entry.definition.description = "desc";
        entry.definition.filePath = std::string("skills/") + name + "/SKILL.md";
        registry.registerSkill(entry);
    }

    auto matches = registry.completeSkillNames("cal", 10);
    TEST_ASSERT(matches.size() == 2);
    TEST_ASSERT(matches[0] == "calculator");
    TEST_ASSERT(matches[1] == "calendar");
    std::cout << "  [PASS] testTriePrefixCompletion\n";
}

int run_skill_registry_tests() {
    std::cout << "Skill Registry Tests:\n";
    RESET_FAILURES();
    testRegisterAndFind();
    testBuildSkillPrompt();
    testBinarySearchPromptLimit();
    testTriePrefixCompletion();
    int f = GET_FAILURES();
    if (f == 0) std::cout << "All skill registry tests passed.\n";
    else std::cout << f << " skill registry test(s) failed.\n";
    return f;
}
