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

    // Test 10: :type via the fn-wrapper inference path (kr68 §12.8.10.2).
    // Pure frontend; works in both WASM=ON and WASM=OFF builds.
    {
        auto r = ahfl::repl::execute_command(":type 1 + 2");
        check(r.success && r.output == "Int", ":type 1 + 2 -> Int");
    }
    {
        auto r = ahfl::repl::execute_command(":type \"hello\"");
        check(r.success && r.output == "String", ":type \"hello\" -> String");
    }
    {
        auto r = ahfl::repl::execute_command(":type true");
        check(r.success && r.output == "Bool", ":type true -> Bool");
    }
    {
        auto r = ahfl::repl::execute_command(":type {}");
        check(r.success && r.output == "Unit", ":type {} -> Unit");
    }
    {
        auto r = ahfl::repl::execute_command(":type 1.5+2.5");
        check(r.success && r.output == "Float", ":type 1.5+2.5 -> Float");
    }

    // Test 11: Unit short-circuit (kr68 §12.8.10.3 item 6). A Unit-typed
    // expression renders as {} without a wasm run, so it works in both
    // WASM=ON and WASM=OFF builds.
    {
        auto r = ahfl::repl::execute_command("{}");
        check(r.success && r.output == "{}", "{} -> {} (Unit short-circuit)");
    }

    // Test 12: declaration fallback survives (kr68 §12.8.10.5). A
    // declaration-bearing input cannot be wrapped in the probe fn, so it
    // routes to the direct pipeline + IR dump.
    {
        auto r = ahfl::repl::execute_command("const x: Int = 5;");
        check(r.success, "declaration fallback succeeds");
        check(r.output.find("const x: Int = 5") != std::string::npos,
              "declaration fallback emits IR dump");
    }

    // Test 16: trailing line comment does not swallow the stage-1 wrapper
    // tail (P1-1). The probe fn is newline-delimited after the expr, so a
    // LINE_COMMENT / DOC_COMMENT cannot eat `; return {}; }`.
    {
        auto r = ahfl::repl::execute_command(":type 1 + 2 // c");
        check(r.success && r.output == "Int", ":type 1 + 2 // c -> Int");
    }

    // Test 17: nominal failure face (kr68 §12.8.10.12 §3 item 10). Without a
    // prologue the detached pipeline cannot see enum/option constructors; the
    // errors are honest strings, not traps, not evaluator fallbacks.
    {
        auto r = ahfl::repl::execute_command("Option::Some(3)");
        check(!r.success, "Option::Some(3) eval fails");
        check(r.output.find("mismatched input 'Option'") != std::string::npos,
              "Option::Some(3) eval top-level parse error");
    }
    {
        auto r = ahfl::repl::execute_command(":type Option::Some(3)");
        check(!r.success, ":type Option::Some(3) fails");
        check(r.output.find("unknown callable 'Option::Some'") != std::string::npos,
              ":type Option::Some(3) resolve error");
    }
    {
        auto r = ahfl::repl::execute_command(":type Color::Green");
        check(!r.success, ":type Color::Green fails");
        check(r.output.find("unknown type 'Color'") != std::string::npos,
              ":type Color::Green resolve error");
    }

    // Test 18: type-error precedence (P2-2). A well-formed expression that
    // fails typecheck surfaces the stage-1 type error, never the fallback
    // top-level parse dump.
    {
        auto r = ahfl::repl::execute_command("1 + true");
        check(!r.success, "1 + true eval fails");
        check(r.output.find("operator '+' is not defined for Int and Bool") != std::string::npos,
              "1 + true shows the stage-1 type error");
        check(r.output.find("expecting {<EOF>") == std::string::npos,
              "1 + true does not show the fallback parse dump");
    }

#ifdef AHFL_ENABLE_BACKEND_WASM
    // Test 13: P6 arithmetic via the wasm3-backed agent runner.
    {
        auto r = ahfl::repl::execute_command("1 + 2");
        check(r.success && r.output == "3", "1 + 2 -> 3");
    }
    {
        auto r = ahfl::repl::execute_command("1 + 2 * 3");
        check(r.success && r.output == "7", "1 + 2 * 3 -> 7");
    }
    {
        auto r = ahfl::repl::execute_command("true");
        check(r.success && r.output == "true", "true -> true");
    }
    {
        auto r = ahfl::repl::execute_command("1 < 2");
        check(r.success && r.output == "true", "1 < 2 -> true");
    }
    {
        auto r = ahfl::repl::execute_command("true && false");
        check(r.success && r.output == "false", "true && false -> false");
    }

    // Test 14: string literal rodata (Data segment) with print_value quotes.
    {
        auto r = ahfl::repl::execute_command("\"hello\"");
        check(r.success && r.output == "\"hello\"", "\"hello\" -> \"hello\" (quoted)");
    }

    // Test 15: unsupported expressions surface actionable codegen diagnostics
    // (kr68 §12.8.10.4), never wasm3 trap text, never an evaluator fallback.
    {
        auto r = ahfl::repl::execute_command("1.5 + 2.5");
        check(!r.success, "1.5 + 2.5 fails");
        check(r.output.find("float literals need the f64 opcode ladder") != std::string::npos,
              "1.5 + 2.5 codegen diagnostic");
        check(r.output.find("trap") == std::string::npos &&
                  r.output.find("m3_") == std::string::npos,
              "1.5 + 2.5 has no trap text");
    }
    {
        auto r = ahfl::repl::execute_command("\"a\" + \"b\"");
        check(!r.success, "\"a\" + \"b\" fails");
        check(r.output.find("binary arithmetic/comparison is defined for Int/Bool") !=
                  std::string::npos,
              "\"a\" + \"b\" codegen diagnostic");
        check(r.output.find("trap") == std::string::npos &&
                  r.output.find("m3_") == std::string::npos,
              "\"a\" + \"b\" has no trap text");
    }

    // Test 19: trailing line comment survives the wasm eval lane (P1-1). The
    // stage-2 synthetic state Done line is newline-delimited after the expr.
    {
        auto r = ahfl::repl::execute_command("1 + 2 // c");
        check(r.success && r.output == "3", "1 + 2 // c -> 3");
    }

    // Test 20: a string literal containing slashes is left intact (P1-1
    // inserts the newline AFTER the expr, never inside it).
    {
        auto r = ahfl::repl::execute_command("\"http://x\"");
        check(r.success && r.output == "\"http://x\"", "\"http://x\" -> \"http://x\"");
    }

    // Test 21: non-Completed rendering names the status enumerator and appends
    // the first diagnostic's code + message (P2-1), never the raw int.
    {
        auto r = ahfl::repl::execute_command("4 / 0");
        check(!r.success, "4 / 0 fails");
        check(r.output.find("NodeFailed") != std::string::npos, "4 / 0 names NodeFailed");
        check(r.output.find("(status: 1)") == std::string::npos,
              "4 / 0 does not render the raw int");
        check(r.output.find("[wasm.trap]") != std::string::npos,
              "4 / 0 appends the wasm.trap code");
        check(r.output.find("runv trapped") != std::string::npos,
              "4 / 0 appends the trap message");
    }
#else
    // WASM=OFF (kr68 §12.8.5): eval refuses with the byte-exact actionable
    // diagnostic; the pure-frontend paths above stay functional.
    {
        auto r = ahfl::repl::execute_command("1 + 2");
        check(!r.success, "WASM=OFF eval refuses");
        check(r.output.find("requires the embedded wasm engine") != std::string::npos,
              "WASM=OFF refusal message");
    }
#endif

    std::printf("%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
