#include "tooling/repl/repl.hpp"

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "runtime/evaluator/evaluator.hpp"
#include "verification/formal/bmc.hpp"
#include "verification/formal/nuxmv_backend.hpp"

#include <sstream>
#include <utility>
#include <variant>

namespace ahfl::repl {

namespace {

struct PipelineResult {
    bool success = false;
    std::string error;
    ahfl::ParseResult parse_result;
    ahfl::ResolveResult resolve_result;
    ahfl::TypeCheckResult typecheck_result;
};

PipelineResult run_pipeline(const std::string &source) {
    PipelineResult p;
    ahfl::Frontend frontend;
    p.parse_result = frontend.parse_text("repl", source);
    if (p.parse_result.has_errors() || !p.parse_result.program) {
        std::ostringstream oss;
        for (const auto &diag : p.parse_result.diagnostics.entries()) {
            oss << diag.message << "\n";
        }
        p.error = oss.str().empty() ? "parse error" : oss.str();
        return p;
    }

    ahfl::Resolver resolver;
    p.resolve_result = resolver.resolve(*p.parse_result.program);
    if (p.resolve_result.has_errors()) {
        std::ostringstream oss;
        for (const auto &diag : p.resolve_result.diagnostics.entries()) {
            oss << diag.message << "\n";
        }
        p.error = oss.str().empty() ? "resolution error" : oss.str();
        return p;
    }

    ahfl::TypeChecker checker;
    p.typecheck_result = checker.check(*p.parse_result.program, p.resolve_result);
    if (p.typecheck_result.has_errors()) {
        std::ostringstream oss;
        for (const auto &diag : p.typecheck_result.diagnostics.entries()) {
            oss << diag.message << "\n";
        }
        p.error = oss.str().empty() ? "type error" : oss.str();
        return p;
    }

    p.success = true;
    return p;
}

std::string default_type_handler(const std::string &input) {
    // Wrap input as a const declaration to get type inference
    std::string wrapped = "const __repl_expr__ = " + input + ";";
    auto pipeline = run_pipeline(wrapped);
    if (!pipeline.success) {
        return "Error: " + pipeline.error;
    }

    // Extract type from typed_program expressions
    const auto &expressions = pipeline.typecheck_result.typed_program.expressions;
    if (!expressions.empty()) {
        // Return the last expression type found
        const auto &last = expressions.back();
        if (last.type) {
            std::ostringstream oss;
            oss << last.type->describe();
            return oss.str();
        }
    }
    return "(type could not be determined)";
}

std::string default_verify_handler(const std::string &input) {
    auto pipeline = run_pipeline(input);
    if (!pipeline.success) {
        return "Error: " + pipeline.error;
    }

    auto ir_program = ahfl::lower_program_ir(
        *pipeline.parse_result.program, pipeline.resolve_result, pipeline.typecheck_result);

    // Find agent declarations and verify
    std::ostringstream result;
    bool found_agent = false;

    for (const auto &decl : ir_program.declarations) {
        auto *agent = std::get_if<ahfl::ir::AgentDecl>(&decl);
        if (agent == nullptr)
            continue;
        found_agent = true;

        ahfl::formal::BmcStateMachine machine;
        machine.name = agent->name;
        machine.states = agent->states;
        machine.initial_state = agent->initial_state;
        machine.final_states = agent->final_states;
        for (const auto &t : agent->transitions) {
            machine.transitions.push_back({t.from_state, t.to_state});
        }

        ahfl::formal::NuXmvBackend backend;
        auto emit = backend.emit_model(machine);
        if (!emit.success) {
            result << "Agent " << agent->name << ": model emission failed\n";
            continue;
        }
        auto verify = backend.verify(emit.model_text);
        result << "Agent " << agent->name << ": " << (verify.all_passed ? "PASS" : "FAIL") << " ("
               << verify.properties_checked << " properties)\n";
    }

    if (!found_agent) {
        return "No agent declarations found to verify.";
    }
    return result.str();
}

std::string default_eval_handler(const std::string &input) {
    // Try to parse and evaluate as a program with a const expression
    std::string wrapped = "const __repl_result__ = " + input + ";";
    auto pipeline = run_pipeline(wrapped);
    if (!pipeline.success) {
        // Try direct pipeline
        auto direct = run_pipeline(input);
        if (!direct.success) {
            return "Error: " + direct.error;
        }
        // Successfully parsed - emit IR summary
        auto ir = ahfl::lower_program_ir(
            *direct.parse_result.program, direct.resolve_result, direct.typecheck_result);
        std::ostringstream oss;
        ahfl::print_program_ir(ir, oss);
        return oss.str();
    }

    auto ir = ahfl::lower_program_ir(
        *pipeline.parse_result.program, pipeline.resolve_result, pipeline.typecheck_result);

    // Try to find a const and evaluate its initializer
    for (const auto &decl : ir.declarations) {
        auto *constant = std::get_if<ahfl::ir::ConstDecl>(&decl);
        if (constant == nullptr)
            continue;
        if (constant->name == "__repl_result__" && constant->value) {
            ahfl::evaluator::EvalContext ctx;
            auto eval_result = ahfl::evaluator::eval_expr(*constant->value, ctx);
            if (!eval_result.has_errors()) {
                std::ostringstream val_oss;
                ahfl::evaluator::print_value(eval_result.value, val_oss);
                return val_oss.str();
            }
        }
    }
    return "(evaluation produced no result)";
}

} // namespace

ReplCommandKind parse_command(const std::string &input) {
    if (input.empty())
        return ReplCommandKind::Unknown;
    if (input == ":quit" || input == ":q")
        return ReplCommandKind::Quit;
    if (input == ":help" || input == ":h")
        return ReplCommandKind::Help;
    if (input.starts_with(":type ") || input.starts_with(":t "))
        return ReplCommandKind::Type;
    if (input.starts_with(":verify ") || input.starts_with(":v "))
        return ReplCommandKind::Verify;
    if (input.starts_with(":simulate ") || input.starts_with(":s "))
        return ReplCommandKind::Simulate;
    return ReplCommandKind::Eval;
}

std::string get_help_text() {
    return "AHFL REPL Commands:\n"
           "  :type <expr>     Show the inferred type of an expression\n"
           "  :verify <code>   Run formal verification on agent declarations\n"
           "  :simulate <code> Simulate agent state transitions\n"
           "  :help            Show this help text\n"
           "  :quit            Exit the REPL\n"
           "  <expr>           Evaluate an expression or declaration\n";
}

ReplResult execute_command(const std::string &input) {
    ReplResult result;
    result.command = parse_command(input);

    switch (result.command) {
    case ReplCommandKind::Help:
        result.success = true;
        result.output = get_help_text();
        break;
    case ReplCommandKind::Quit:
        result.success = true;
        result.output = "Goodbye.";
        break;
    case ReplCommandKind::Type: {
        auto expr = input.substr(input.find(' ') + 1);
        result.output = default_type_handler(expr);
        result.success = !result.output.starts_with("Error:");
        if (!result.success)
            result.error = result.output;
        break;
    }
    case ReplCommandKind::Verify: {
        auto code = input.substr(input.find(' ') + 1);
        result.output = default_verify_handler(code);
        result.success = true;
        break;
    }
    case ReplCommandKind::Simulate: {
        auto code = input.substr(input.find(' ') + 1);
        result.output = default_verify_handler(code); // simulate uses same verify path
        result.success = true;
        break;
    }
    case ReplCommandKind::Eval: {
        result.output = default_eval_handler(input);
        result.success = !result.output.starts_with("Error:");
        if (!result.success)
            result.error = result.output;
        break;
    }
    default:
        result.error = "Unknown command";
        break;
    }
    return result;
}

Repl::Repl(ReplConfig config) : config_(std::move(config)) {
    // Set default handlers connected to real pipeline
    eval_handler_ = default_eval_handler;
    type_handler_ = default_type_handler;
    verify_handler_ = default_verify_handler;
}

ReplResult Repl::process_input(const std::string &input) {
    history_.push_back(input);
    auto kind = parse_command(input);

    ReplResult result;
    result.command = kind;

    switch (kind) {
    case ReplCommandKind::Help:
        result.success = true;
        result.output = get_help_text();
        break;
    case ReplCommandKind::Quit:
        result.success = true;
        break;
    case ReplCommandKind::Type: {
        auto expr = input.substr(input.find(' ') + 1);
        if (type_handler_) {
            result.output = type_handler_(expr);
            result.success = !result.output.starts_with("Error:");
        } else {
            result.error = "type handler not set";
        }
        break;
    }
    case ReplCommandKind::Verify:
    case ReplCommandKind::Simulate: {
        auto code = input.substr(input.find(' ') + 1);
        if (verify_handler_) {
            result.output = verify_handler_(code);
            result.success = true;
        } else {
            result.error = "verify handler not set";
        }
        break;
    }
    case ReplCommandKind::Eval: {
        if (eval_handler_) {
            result.output = eval_handler_(input);
            result.success = !result.output.starts_with("Error:");
        } else {
            result.error = "eval handler not set";
        }
        break;
    }
    default:
        result.error = "Unknown command";
        break;
    }
    return result;
}

const ReplConfig &Repl::config() const {
    return config_;
}
size_t Repl::history_size() const {
    return history_.size();
}
const std::vector<std::string> &Repl::history() const {
    return history_;
}

void Repl::set_eval_handler(std::function<std::string(const std::string &)> handler) {
    eval_handler_ = std::move(handler);
}

void Repl::set_type_handler(std::function<std::string(const std::string &)> handler) {
    type_handler_ = std::move(handler);
}

void Repl::set_verify_handler(std::function<std::string(const std::string &)> handler) {
    verify_handler_ = std::move(handler);
}

} // namespace ahfl::repl
