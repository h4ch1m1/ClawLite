#include "llm/harness.h"
#include "llm/llm_client.h"
#include "llm/runtime_plan.h"
#include "llm/tool_executor.h"
#include "test_helpers.h"

#include <cstdio>
#include <fstream>
#include <iostream>

using namespace clawlite;

void testRuntimePlanValidation() {
    RuntimePlan plan = RuntimePlan::defaultPlan();
    TEST_ASSERT(plan.validate());

    plan.transport.maxToolRounds = 0;
    TEST_ASSERT(!plan.validate());

    std::cout << "  [PASS] testRuntimePlanValidation\n";
}

void testToolRegistryAndMissingTool() {
    ToolExecutor tools;
    tools.registerBuiltinTools();

    TEST_ASSERT(tools.size() == 4);
    TEST_ASSERT(tools.hasTool("hello"));
    TEST_ASSERT(tools.hasTool("calculator"));

    ToolCall missing;
    missing.name = "does_not_exist";
    ToolResult result = tools.execute(missing);
    TEST_ASSERT(!result.success);
    TEST_ASSERT(result.error.find("tool not found") != std::string::npos);

    std::cout << "  [PASS] testToolRegistryAndMissingTool\n";
}

void testHelloTool() {
    ToolExecutor tools;
    tools.registerBuiltinTools();

    ToolCall call;
    call.id = "call_hello";
    call.name = "hello";
    call.arguments = "{}";

    ToolResult result = tools.execute(call);
    TEST_ASSERT(result.success);
    TEST_ASSERT(!result.output.empty());

    std::cout << "  [PASS] testHelloTool\n";
}

void testCalculatorTool() {
    ToolExecutor tools;
    tools.registerBuiltinTools();

    ToolCall call;
    call.id = "call_calc";
    call.name = "calculator";
    call.arguments = R"json({"expr":"2 + 3 * (4 - 1)"})json";

    ToolResult result = tools.execute(call);
    TEST_ASSERT(result.success);
    TEST_ASSERT(result.output == "11");

    std::cout << "  [PASS] testCalculatorTool\n";
}

void testReadFileTool() {
    const std::string path = "clawlite_test_read_file.txt";
    {
        std::ofstream out(path);
        out << "hello tool";
    }

    ToolExecutor tools;
    tools.registerBuiltinTools();

    ToolCall call;
    call.id = "call_read";
    call.name = "read_file";
    call.arguments = R"({"path":"clawlite_test_read_file.txt"})";

    ToolResult result = tools.execute(call);
    std::remove(path.c_str());

    TEST_ASSERT(result.success);
    TEST_ASSERT(result.output.find("hello tool") != std::string::npos);

    std::cout << "  [PASS] testReadFileTool\n";
}

void testHarnessReturnsLlmError() {
    LlmConfig config;
    config.apiKey = "";
    LlmClient llm(config);

    ToolExecutor tools;
    AgentHarness harness(llm, tools, nullptr);

    RunResult result = harness.runTurn("system", {}, "hello");
    TEST_ASSERT(result.status == RunStatus::Error);
    TEST_ASSERT(result.error.find("missing API key") != std::string::npos);

    std::cout << "  [PASS] testHarnessReturnsLlmError\n";
}

int run_llm_tests() {
    std::cout << "LLM Runtime Tests:\n";
    RESET_FAILURES();
    testRuntimePlanValidation();
    testToolRegistryAndMissingTool();
    testHelloTool();
    testCalculatorTool();
    testReadFileTool();
    testHarnessReturnsLlmError();
    int f = GET_FAILURES();
    if (f == 0) std::cout << "All LLM runtime tests passed.\n";
    else std::cout << f << " LLM runtime test(s) failed.\n";
    return f;
}
