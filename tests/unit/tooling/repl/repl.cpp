#include "tooling/repl/repl.hpp"
#include <cstdio>
#include <string>

static int test_count = 0;
static int pass_count = 0;

static void check(bool condition, const char *name) {
    test_count++;
    if (condition) {
        pass_count++;
    } else {
        std::printf("FAIL: %s\n", name);
    }
}

int main() {
    // Test 1: Parse commands
    {
        check(ahfl::repl::parse_command(":quit") == ahfl::repl::ReplCommandKind::Quit,
              "parse :quit");
        check(ahfl::repl::parse_command(":q") == ahfl::repl::ReplCommandKind::Quit, "parse :q");
        check(ahfl::repl::parse_command(":help") == ahfl::repl::ReplCommandKind::Help,
              "parse :help");
        check(ahfl::repl::parse_command(":type x") == ahfl::repl::ReplCommandKind::Type,
              "parse :type");
        check(ahfl::repl::parse_command(":verify agent") == ahfl::repl::ReplCommandKind::Verify,
              "parse :verify");
        check(ahfl::repl::parse_command(":simulate agent") == ahfl::repl::ReplCommandKind::Simulate,
              "parse :simulate");
        check(ahfl::repl::parse_command("1 + 2") == ahfl::repl::ReplCommandKind::Eval,
              "parse expression");
    }

    // Test 2: Execute help
    {
        auto result = ahfl::repl::execute_command(":help");
        check(result.success, "help succeeds");
        check(!result.output.empty(), "help has output");
        check(result.command == ahfl::repl::ReplCommandKind::Help, "help command kind");
    }

    // Test 3: REPL with custom handler
    {
        ahfl::repl::Repl repl;
        repl.set_eval_handler([](const std::string &input) { return "result: " + input; });

        auto result = repl.process_input("2 + 3");
        check(result.success, "eval with handler succeeds");
        check(result.output == "result: 2 + 3", "eval handler called");
    }

    // Test 4: History tracking
    {
        ahfl::repl::Repl repl;
        (void)repl.process_input("first");
        (void)repl.process_input("second");
        (void)repl.process_input("third");

        check(repl.history_size() == 3, "history tracks inputs");
        check(repl.history()[0] == "first", "history preserves order");
    }

    // Test 5: Type handler
    {
        ahfl::repl::Repl repl;
        repl.set_type_handler([](const std::string & /*expr*/) { return "String"; });

        auto result = repl.process_input(":type \"hello\"");
        check(result.success, "type command succeeds");
        check(result.output == "String", "type handler returns type");
    }

    // Test 6: Unknown command
    {
        check(ahfl::repl::parse_command("") == ahfl::repl::ReplCommandKind::Unknown,
              "empty is unknown");
    }

    // Test 7: Quit command result
    {
        auto result = ahfl::repl::execute_command(":quit");
        check(result.success, "quit succeeds");
        check(result.output == "Goodbye.", "quit message");
    }

    // Test 8: Simulate command uses real state transitions
    {
        const std::string source = "module repl::sim;\n"
                                   "struct Request { value: String; }\n"
                                   "struct Context { value: String = \"seed\"; }\n"
                                   "struct Response { value: String; }\n"
                                   "agent SimAgent {\n"
                                   "  input: Request;\n"
                                   "  context: Context;\n"
                                   "  output: Response;\n"
                                   "  states: [Init, Done];\n"
                                   "  initial: Init;\n"
                                   "  final: [Done];\n"
                                   "  capabilities: [];\n"
                                   "  transition Init -> Done;\n"
                                   "}\n";
        auto result = ahfl::repl::execute_command(":simulate " + source);
        check(result.success, "simulate succeeds");
        check(result.output.find("simulation") != std::string::npos, "simulate reports simulation");
        check(result.output.find("Init -> Done") != std::string::npos,
              "simulate reports transition");
        check(result.output.find("properties") == std::string::npos,
              "simulate does not use verify output");
    }

    // Test 9: Simulate command has an independent handler
    {
        ahfl::repl::Repl repl;
        repl.set_verify_handler([](const std::string &input) { return "verify: " + input; });
        repl.set_simulate_handler([](const std::string &input) { return "simulate: " + input; });

        auto verify = repl.process_input(":verify agent A {}");
        check(verify.success, "verify handler still succeeds");
        check(verify.output == "verify: agent A {}", "verify handler called");

        auto simulate = repl.process_input(":simulate agent A {}");
        check(simulate.success, "simulate handler succeeds");
        check(simulate.output == "simulate: agent A {}", "simulate handler called");
    }

    std::printf("%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
