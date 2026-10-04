#include "tooling/repl/repl.hpp"

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/typed_hir.hpp"
#include "ahfl/compiler/semantics/types.hpp"
#include "runtime/value/value.hpp"
// RFC 0026 KR6.8 WH-7 (kr68 §12.8.10): the wasm3-backed agent runner. Included
// ungated exactly like src/tooling/cli/workflow_run.cpp (WH-6): the header
// chain is self-contained (pimpl engine, no vendored wasm3 headers), and only
// the eval body is #ifdef-gated so a WASM=OFF build links without the runner.
#include "runtime/wasm_runner/wasm_agent_runner.hpp"
#include "verification/formal/bmc.hpp"
#include "verification/formal/nuxmv_backend.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <queue>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace ahfl::repl {

namespace {

// The compiler pipeline phase that produced a failure. infer_repl_type adds a
// fourth Internal phase for post-typecheck extraction-invariant failures.
enum class PipelineFailurePhase { Parse, Resolve, Typecheck };

struct PipelineResult {
    bool success = false;
    std::string error;
    ahfl::ParseResult parse_result;
    ahfl::ResolveResult resolve_result;
    ahfl::TypeCheckResult typecheck_result;
    // Only meaningful when !success: the phase that produced `error`.
    PipelineFailurePhase failure_phase{PipelineFailurePhase::Parse};
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
        p.failure_phase = PipelineFailurePhase::Parse;
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
        p.failure_phase = PipelineFailurePhase::Resolve;
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
        p.failure_phase = PipelineFailurePhase::Typecheck;
        return p;
    }

    p.success = true;
    return p;
}

// Stage-1 type inference for a REPL expression (kr68 §12.8.10.2).
//
// Wraps the expression in a probe fn whose unannotated `let` binding captures
// the expression's inferred type on the TypedProgram let statement. A const
// wrapper cannot work: constDecl requires an explicit type annotation, and the
// leading-underscore name is an illegal IDENT. The fn-wrapper shape is the
// proven mechanism — every fn body is typechecked even when never called.
//
// The wrapper is newline-delimited after the expr (P1-1): a trailing line
// comment (`1 + 2 // c`) would otherwise eat the wrapper tail (`; return
// {}; }`) on the same physical line. WS skips newlines, so the template
// shape is unchanged.
//
// `prologue` (kr68 §12.8.11.1): accumulated session declarations prepended
// before the probe fn so types declared on earlier lines are visible.
//
// `failure_phase` is only meaningful when `type == nullptr`. Parse/Resolve
// mirror the pipeline phase; Internal means the probe fn compiled but the
// TypedProgram extraction invariants broke (a compiler-invariant violation).
// The eval handler uses it to decide whether the stage-1 error or the
// declaration-fallback parse dump is the honest face (P2-2).
enum class InferFailurePhase { Parse, Resolve, Typecheck, Internal };

struct ReplInferredType {
    ahfl::TypePtr type{nullptr};
    InferFailurePhase failure_phase{InferFailurePhase::Internal};
};

[[nodiscard]] ReplInferredType
infer_repl_type(const std::string &expr, const std::string &prologue, std::string &error) {
    ReplInferredType result;
    const std::string wrapped =
        prologue + "\nfn repl_probe() -> Unit { let repl_val = " + expr + "\n; return {}; }";
    auto pipeline = run_pipeline(wrapped);
    if (!pipeline.success) {
        error = pipeline.error;
        switch (pipeline.failure_phase) {
        case PipelineFailurePhase::Parse:
            result.failure_phase = InferFailurePhase::Parse;
            break;
        case PipelineFailurePhase::Resolve:
            result.failure_phase = InferFailurePhase::Resolve;
            break;
        case PipelineFailurePhase::Typecheck:
            result.failure_phase = InferFailurePhase::Typecheck;
            break;
        }
        return result;
    }

    const auto &program = pipeline.typecheck_result.typed_program;
    for (const auto &decl : program.declarations) {
        const auto *fn = std::get_if<ahfl::FnTypeInfo>(&decl.payload);
        if (fn == nullptr || fn->local_name != "repl_probe") {
            continue;
        }
        if (fn->body_block_index == UINT32_MAX ||
            fn->body_block_index >= program.blocks.size()) {
            error = "(internal) repl type extraction failed: repl_probe body block index is "
                    "out of range";
            return {};
        }
        const auto &block = program.blocks[fn->body_block_index];
        if (block.statement_indexes.empty() ||
            block.statement_indexes.front() >= program.statements.size()) {
            error = "(internal) repl type extraction failed: repl_probe body has no statements";
            return {};
        }
        const auto &stmt = program.statements[block.statement_indexes.front()];
        if (stmt.kind != ahfl::TypedStmtKind::Let || stmt.target_name != "repl_val" ||
            stmt.let_type_ref_strategy != ahfl::LetTypeRefStrategy::FromInitializerType) {
            error = "(internal) repl type extraction failed: first statement is not the "
                    "repl_val let binding";
            return {};
        }
        if (stmt.let_type == nullptr) {
            error = "(internal) repl type extraction failed: repl_val let has no inferred type";
            return {};
        }
        result.type = stmt.let_type;
        return result;
    }
    error = "(internal) repl type extraction failed: repl_probe declaration not found";
    return result;
}

std::string join_strings(const std::vector<std::string> &values, std::string_view separator) {
    std::ostringstream oss;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            oss << separator;
        }
        oss << values[index];
    }
    return oss.str();
}

std::size_t outgoing_count(const ahfl::ir::AgentDecl &agent, std::string_view state) {
    return static_cast<std::size_t>(std::count_if(agent.transitions.begin(),
                                                  agent.transitions.end(),
                                                  [&](const ahfl::ir::TransitionDecl &transition) {
                                                      return transition.from_state == state;
                                                  }));
}

std::vector<std::string> shortest_path_to_final(const ahfl::ir::AgentDecl &agent,
                                                std::string &reached_final) {
    std::unordered_set<std::string> final_states(agent.final_states.begin(),
                                                 agent.final_states.end());
    if (agent.initial_state.empty() || final_states.empty()) {
        return {};
    }

    std::queue<std::string> work;
    std::unordered_set<std::string> visited;
    std::unordered_map<std::string, std::string> predecessor;
    work.push(agent.initial_state);
    visited.insert(agent.initial_state);

    std::optional<std::string> found_final;
    while (!work.empty()) {
        const auto current = work.front();
        work.pop();
        if (final_states.contains(current)) {
            found_final = current;
            break;
        }

        for (const auto &transition : agent.transitions) {
            if (transition.from_state != current || visited.contains(transition.to_state)) {
                continue;
            }
            predecessor.emplace(transition.to_state, current);
            visited.insert(transition.to_state);
            work.push(transition.to_state);
        }
    }

    if (!found_final.has_value()) {
        return {};
    }

    reached_final = *found_final;
    std::vector<std::string> path;
    for (std::string cursor = *found_final;; cursor = predecessor.at(cursor)) {
        path.push_back(cursor);
        if (cursor == agent.initial_state) {
            break;
        }
    }
    std::reverse(path.begin(), path.end());
    return path;
}

void print_agent_simulation(const ahfl::ir::AgentDecl &agent, std::ostream &out) {
    out << "Agent " << agent.name << " simulation:\n";
    if (agent.initial_state.empty()) {
        out << "  result: cannot simulate because no initial state is declared\n";
        return;
    }
    if (agent.final_states.empty()) {
        out << "  step 0: enter " << agent.initial_state << '\n';
        out << "  result: cannot simulate because no final states are declared\n";
        return;
    }

    std::string reached_final;
    const auto path = shortest_path_to_final(agent, reached_final);
    if (path.empty()) {
        out << "  step 0: enter " << agent.initial_state << '\n';
        out << "  result: no path to final state(s) [" << join_strings(agent.final_states, ", ")
            << "]\n";
        return;
    }

    out << "  step 0: enter " << path.front() << '\n';
    for (std::size_t index = 1; index < path.size(); ++index) {
        const auto branches = outgoing_count(agent, path[index - 1]);
        out << "  step " << index << ": " << path[index - 1] << " -> " << path[index];
        if (branches > 1) {
            out << " (selected shortest path among " << branches << " outgoing transition(s))";
        }
        out << '\n';
    }
    out << "  result: reached final state " << reached_final << " in " << (path.size() - 1)
        << " transition(s)\n";
}

// Map a WorkflowStatus to its enumerator name (P2-1). The REPL renders the
// name, not the raw int, so a non-Completed run is actionable without a
// header lookup. [[maybe_unused]]: only the WASM=ON eval lane reaches a
// non-Completed run; WASM=OFF refuses before running.
[[maybe_unused]] std::string_view
workflow_status_name(ahfl::runtime::WorkflowStatus status) noexcept {
    switch (status) {
    case ahfl::runtime::WorkflowStatus::Completed:
        return "Completed";
    case ahfl::runtime::WorkflowStatus::NodeFailed:
        return "NodeFailed";
    case ahfl::runtime::WorkflowStatus::DependencyFailed:
        return "DependencyFailed";
    case ahfl::runtime::WorkflowStatus::EvalError:
        return "EvalError";
    case ahfl::runtime::WorkflowStatus::Suspended:
        return "Suspended";
    }
    return "Unknown";
}

// Render the first diagnostic's code + message with the CLI's punctuation
// (workflow_run.cpp renders `error [code]: message`; the REPL already carries
// the Error: severity prefix, so only the code + message tail is appended).
// [[maybe_unused]]: only the WASM=ON eval lane reads run diagnostics.
[[maybe_unused]] std::string
render_first_diagnostic(const ahfl::DiagnosticBag &diagnostics) {
    const auto &entries = diagnostics.entries();
    if (entries.empty()) {
        return {};
    }
    const auto &diag = entries.front();
    std::string rendered;
    if (diag.code.has_value()) {
        rendered += "[";
        rendered += *diag.code;
        rendered += "]: ";
    }
    rendered += diag.message;
    return rendered;
}

} // namespace

// --- Free functions ---

ReplCommandKind parse_command(const std::string &input) {
    if (input.empty())
        return ReplCommandKind::Unknown;
    if (input == ":quit" || input == ":q")
        return ReplCommandKind::Quit;
    if (input == ":help" || input == ":h")
        return ReplCommandKind::Help;
    if (input == ":clear")
        return ReplCommandKind::Clear;
    if (input.starts_with(":load "))
        return ReplCommandKind::Load;
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
           "  :clear           Clear the accumulated session declarations\n"
           "  :load <path>     Load declarations from a file into the session\n"
           "  :help            Show this help text\n"
           "  :quit            Exit the REPL\n"
           "  <expr>           Evaluate an expression or declaration\n";
}

// --- Repl member functions ---

Repl::Repl(ReplConfig config) : config_(std::move(config)) {
    // Default handlers are member functions so they can see session_source_.
    // The closures capture `this`; Repl is non-copyable/non-movable so the
    // pointer stays valid for the object's lifetime (kr68 §12.8.11.5).
    eval_handler_ = [this](const std::string &input) { return default_eval(input); };
    type_handler_ = [this](const std::string &input) { return default_type(input); };
    verify_handler_ = [this](const std::string &input) { return default_verify(input); };
    simulate_handler_ = [this](const std::string &input) { return default_simulate(input); };

    // Load the prelude at startup. A failure prints a warning and starts with
    // an empty session (GHCi :load semantics, kr68 §12.8.11.2).
    if (!config_.prelude_path.empty()) {
        auto result = default_load(config_.prelude_path);
        if (result.starts_with("Error:")) {
            std::cerr << "warning: " << result << "\n";
        }
    }
}

std::string Repl::default_type(const std::string &input) {
    std::string error;
    auto inferred = infer_repl_type(input, session_source_, error);
    if (inferred.type == nullptr) {
        return "Error: " + error;
    }
    return inferred.type->describe();
}

std::string Repl::default_verify(const std::string &input) {
    auto pipeline = run_pipeline(with_session(input));
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
        ahfl::formal::BackendVerificationOptions bvopts;
        auto verify = backend.verify(emit.model_text, bvopts);
        result << "Agent " << agent->name << ": " << (verify.all_passed ? "PASS" : "FAIL") << " ("
               << verify.properties_checked << " properties)\n";
    }

    if (!found_agent) {
        return "No agent declarations found to verify.";
    }
    return result.str();
}

std::string Repl::default_simulate(const std::string &input) {
    auto pipeline = run_pipeline(with_session(input));
    if (!pipeline.success) {
        return "Error: " + pipeline.error;
    }

    const auto ir_program = ahfl::lower_program_ir(
        *pipeline.parse_result.program, pipeline.resolve_result, pipeline.typecheck_result);

    std::ostringstream result;
    bool found_agent = false;
    for (const auto &decl : ir_program.declarations) {
        const auto *agent = std::get_if<ahfl::ir::AgentDecl>(&decl);
        if (agent == nullptr) {
            continue;
        }
        found_agent = true;
        print_agent_simulation(*agent, result);
    }

    if (!found_agent) {
        return "No agent declarations found to simulate.";
    }
    return result.str();
}

std::string Repl::default_load(const std::string &path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return "Error: cannot load file: " + path;
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    file.close();

    // The detached pipeline cannot handle module/import/pub-use (kr68
    // §12.8.11.2). Reject them with an honest error rather than letting the
    // parser produce a confusing diagnostic.
    if (has_module_or_import(content)) {
        return "Error: module declarations, imports, and pub use are not supported "
               "in the REPL session";
    }

    // Validate the content compiles with the current session before accepting.
    auto pipeline = run_pipeline(with_session(content));
    if (!pipeline.success) {
        return "Error: " + pipeline.error;
    }

    const auto loaded_count =
        static_cast<size_t>(std::count(content.begin(), content.end(), '\n'));
    append_to_session(content);
    return "Loaded " + std::to_string(loaded_count) + " declaration(s) into session.";
}

std::string Repl::default_eval(const std::string &input) {
    // Stage-1: infer the expression type via the fn-wrapper probe.
    // The session source is prepended as a prologue so types declared on
    // earlier lines are visible (kr68 §12.8.11.1).
    std::string error;
    auto inferred = infer_repl_type(input, session_source_, error);
    if (inferred.type == nullptr) {
        // Declaration fallback: declaration-bearing inputs (const/struct/fn/...)
        // cannot be wrapped in the probe fn, so a stage-1 failure on a
        // declaration input is expected. Run the full pipeline with the
        // session as prologue; on success, accumulate the declaration.
        auto direct = run_pipeline(with_session(input));
        if (!direct.success) {
            // P2-2: a stage-1 Typecheck (or Internal) failure means the input
            // IS a well-formed expression whose real diagnostic is the
            // stage-1 type error; the fallback top-level parse dump would
            // mask it. Parse/Resolve stage-1 failures defer to the fallback
            // error (the pinned nominal/resolve face, §12.8.10.12 1.4).
            if (inferred.failure_phase != InferFailurePhase::Parse &&
                inferred.failure_phase != InferFailurePhase::Resolve) {
                return "Error: " + error;
            }
            return "Error: " + direct.error;
        }
        // Accepted declaration: accumulate it so later lines can see it.
        append_to_session(input);
        auto ir = ahfl::lower_program_ir(
            *direct.parse_result.program, direct.resolve_result, direct.typecheck_result);
        std::ostringstream oss;
        ahfl::print_program_ir(ir, oss);
        return oss.str();
    }

    // Unit short-circuit: a Unit-typed expression renders as {} without a
    // wasm run — the codegen lane rejects a Unit output struct field.
    if (inferred.type->holds<ahfl::types::UnitT>()) {
        std::ostringstream oss;
        ahfl::runtime::print_value(ahfl::runtime::Value{ahfl::runtime::UnitValue{}}, oss);
        return oss.str();
    }

#ifdef AHFL_ENABLE_BACKEND_WASM
    // Stage-2: synthetic two-state agent wrapping the expression as its
    // output. Zero-field input struct (proven to survive the full pack/run/
    // read pipeline), one-field output struct carrying the expression value.
    // No module declaration (would prefix nominal types with repl::), no pub
    // (prologue types are package-internal). The session source is prepended
    // as the prologue so session types are visible (kr68 §12.8.11.1).
    //
    // P1-1: the state Done line is newline-delimited after the expr so a
    // trailing line comment (`1 + 2 // c`) cannot eat the closing `};`.
    const std::string synthetic =
        with_session(
            "struct ReplIn {}\n"
            "struct ReplOut {\n"
            "    value: " + inferred.type->describe() + ";\n"
            "}\n"
            "agent ReplAgent {\n"
            "    input: ReplIn;\n"
            "    context: Unit;\n"
            "    output: ReplOut;\n"
            "    states: [Init, Done];\n"
            "    initial: Init;\n"
            "    final: [Done];\n"
            "    capabilities: [];\n"
            "    transition Init -> Done;\n"
            "}\n"
            "flow for ReplAgent {\n"
            "    state Init { goto Done; }\n"
            "    state Done { return ReplOut { value: " + input + "\n        }; }\n"
            "}\n");

    auto pipeline = run_pipeline(synthetic);
    if (!pipeline.success) {
        return "Error: " + pipeline.error;
    }
    auto ir = ahfl::lower_program_ir(
        *pipeline.parse_result.program, pipeline.resolve_result, pipeline.typecheck_result);

    ahfl::runtime::wasm_runner::WasmAgentRunnerConfig config;
    // Defensive: the synthetic agent declares zero capabilities, so the runner
    // never reaches an import. This invoker only fires on a compiler invariant
    // violation, and fails closed.
    config.invoker = [](const ahfl::runtime::CapabilityInvocationContext &,
                        const std::string &,
                        const std::vector<ahfl::runtime::Value> &)
        -> ahfl::runtime::CapabilityCallResult {
        return ahfl::runtime::CapabilityCallResult{
            .status = ahfl::runtime::CapabilityCallStatus::Error,
            .error_message = "REPL evaluation agent has no capabilities",
        };
    };

    const ahfl::runtime::Value repl_input{
        ahfl::runtime::StructValue{.type_name = "", .fields = ahfl::runtime::FieldMap{}}};

    auto run_result = ahfl::runtime::wasm_runner::run_wasm_agent(
        ir, "ReplAgent", repl_input, std::move(config));
    if (!run_result.has_value()) {
        return "Error: " + run_result.error();
    }

    const auto &workflow_result = run_result->result;
    if (workflow_result.status() != ahfl::runtime::WorkflowStatus::Completed) {
        // P2-1: render the enumerator NAME (not the raw int) and append the
        // first diagnostic's code + message in the CLI's punctuation, so the
        // failure is actionable (e.g. a div-by-zero trap surfaces
        // `[wasm.trap]: run_wasm_agent: runv trapped`).
        std::string message =
            "Error: agent did not complete (status: " +
            std::string(workflow_status_name(workflow_result.status())) + ")";
        if (const auto diagnostic = render_first_diagnostic(workflow_result.diagnostics);
            !diagnostic.empty()) {
            message += ": ";
            message += diagnostic;
        }
        return message;
    }
    const ahfl::runtime::Value *output = workflow_result.output();
    if (output == nullptr) {
        return "Error: agent produced no output";
    }
    const auto *frame = std::get_if<ahfl::runtime::StructValue>(&output->node);
    if (frame == nullptr) {
        return "Error: agent output is not a struct";
    }
    const ahfl::runtime::Value *value = frame->fields.get("value");
    if (value == nullptr) {
        return "Error: agent output struct has no value field";
    }
    std::ostringstream oss;
    ahfl::runtime::print_value(*value, oss);
    return oss.str();
#else
    // WASM=OFF (kr68 §12.8.5): the eval path refuses with an actionable
    // diagnostic. Stage-1 inference, the Unit short-circuit, and the
    // declaration fallback above stay functional (pure compiler/value code).
    return "Error: evaluation requires the embedded wasm engine; this build was "
           "configured with -DAHFL_ENABLE_BACKEND_WASM=OFF. Rebuild with the default "
           "(ON) to evaluate expressions.";
#endif
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
        result.output = "Goodbye.";
        break;
    case ReplCommandKind::Clear:
        clear_session();
        result.success = true;
        result.output = "Session cleared.";
        break;
    case ReplCommandKind::Load: {
        auto path = input.substr(input.find(' ') + 1);
        // Trim leading and trailing whitespace.
        const auto first = path.find_first_not_of(" \t");
        if (first == std::string::npos) {
            result.output = "Error: :load requires a file path";
            result.success = false;
            result.error = result.output;
            break;
        }
        const auto last = path.find_last_not_of(" \t");
        path = path.substr(first, last - first + 1);
        result.output = default_load(path);
        result.success = !result.output.starts_with("Error:");
        if (!result.success)
            result.error = result.output;
        break;
    }
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
    case ReplCommandKind::Verify: {
        auto code = input.substr(input.find(' ') + 1);
        if (verify_handler_) {
            result.output = verify_handler_(code);
            result.success = true;
        } else {
            result.error = "verify handler not set";
        }
        break;
    }
    case ReplCommandKind::Simulate: {
        auto code = input.substr(input.find(' ') + 1);
        if (simulate_handler_) {
            result.output = simulate_handler_(code);
            result.success = !result.output.starts_with("Error:");
        } else {
            result.error = "simulate handler not set";
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

size_t Repl::session_declarations() const {
    return static_cast<size_t>(std::count(session_source_.begin(), session_source_.end(), '\n'));
}

void Repl::clear_session() {
    session_source_.clear();
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

void Repl::set_simulate_handler(std::function<std::string(const std::string &)> handler) {
    simulate_handler_ = std::move(handler);
}

std::string Repl::with_session(const std::string &input) const {
    if (session_source_.empty()) {
        return input;
    }
    return session_source_ + "\n" + input;
}

void Repl::append_to_session(const std::string &input) {
    session_source_ += input;
    session_source_ += '\n';
}

bool Repl::has_module_or_import(const std::string &source) const {
    std::istringstream stream(source);
    std::string line;
    while (std::getline(stream, line)) {
        const auto start = line.find_first_not_of(" \t");
        if (start == std::string::npos)
            continue;
        // Tokenize the first keyword so tab-separated forms (e.g.
        // "module\tfoo;") are caught, not just space-separated ones.
        std::istringstream word_stream(line.substr(start));
        std::string keyword;
        word_stream >> keyword;
        if (keyword == "module" || keyword == "import" || keyword == "pub") {
            return true;
        }
    }
    return false;
}

// --- Stateless free function (kr68 §12.8.11.5) ---

ReplResult execute_command(const std::string &input) {
    // Creates a fresh Repl (default config, no prelude, no session) so each
    // call is stateless. All command dispatch lives in Repl::process_input.
    Repl repl;
    return repl.process_input(input);
}

} // namespace ahfl::repl
