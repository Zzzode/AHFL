// RFC 0026 P6-1 (KR6.6) same-frontend scalar-computation producer. One checked
// program is forked to native AgentRuntime and AHFL->Core->P4-D->wasm. Unlike
// the E1 identity probe, the non-final handlers carry real scalar computation
// (Literal/ValueRef/Unary/Binary) that drives computed gotos; the result is
// observable purely through the state ids step()/run transition through, with
// no frame encoding. The probe prints the native state-entry id sequence and
// final state so the Node embedded host can drive step() and assert the same
// ids and transition_count (real Node-engine evidence, NOT wasmtime).

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/core_layout.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/validate.hpp"
#include "compiler/backends/infra/core_wasm_codegen.hpp"
#include "runtime/engine/agent_runtime.hpp"
#include "runtime/evaluator/value.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace ahfl;

[[nodiscard]] std::optional<ir::Program> compile_fixture(const std::filesystem::path &path) {
    const Frontend frontend;
    const auto parse = frontend.parse_file(path);
    if (parse.has_errors() || parse.program == nullptr) {
        parse.diagnostics.render(std::cerr);
        return std::nullopt;
    }
    const Resolver resolver;
    const auto resolve = resolver.resolve(*parse.program);
    if (resolve.has_errors()) {
        resolve.diagnostics.render(std::cerr);
        return std::nullopt;
    }
    const TypeChecker checker;
    const auto typecheck = checker.check(*parse.program, resolve);
    if (typecheck.has_errors()) {
        typecheck.diagnostics.render(std::cerr);
        return std::nullopt;
    }
    const Validator validator;
    const auto validation = validator.validate(*parse.program, resolve, typecheck);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cerr);
        return std::nullopt;
    }
    return lower_program_ir(*parse.program, resolve, typecheck);
}

[[nodiscard]] evaluator::Value fixture_input() {
    evaluator::FieldMap fields;
    fields.set("value", std::make_unique<evaluator::Value>(evaluator::make_string("identity")));
    return evaluator::Value{evaluator::StructValue{"wasm::p6::Frame", std::move(fields)}};
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "usage: ahfl_core_wasm_p6_probe <source.ahfl> <output.wasm>\n";
        return 2;
    }
    auto program = compile_fixture(argv[1]);
    if (!program.has_value()) {
        return 1;
    }

    const ir::AgentDecl *agent = nullptr;
    const ir::FlowDecl *flow = nullptr;
    for (const auto &decl : program->declarations) {
        if (const auto *candidate = std::get_if<ir::AgentDecl>(&decl)) {
            if (agent != nullptr) {
                std::cerr << "expected exactly one agent\n";
                return 1;
            }
            agent = candidate;
        } else if (const auto *candidate = std::get_if<ir::FlowDecl>(&decl)) {
            if (flow != nullptr) {
                std::cerr << "expected exactly one flow\n";
                return 1;
            }
            flow = candidate;
        }
    }
    if (agent == nullptr || flow == nullptr) {
        std::cerr << "fixture does not contain one agent and flow\n";
        return 1;
    }

    // Native observation: collect every entered state NAME in order. The
    // observer requires a valid invocation agent id to fire.
    std::vector<std::string> entered_names;
    auto input = fixture_input();
    runtime::AgentRuntime native(*agent, *flow);
    runtime::CapabilityInvocationContext context;
    context.agent_id = runtime::AgentId{0};
    native.set_invocation_context(std::move(context));
    native.set_state_entered_observer(
        [&](runtime::AgentId, std::string_view state_name) -> runtime::AgentStateId {
            entered_names.emplace_back(state_name);
            return runtime::AgentStateId{entered_names.size() - 1};
        });
    const auto native_result = native.run(std::move(input));
    const char *native_status = "unknown";
    switch (native_result.status) {
    case runtime::AgentStatus::Completed:
        native_status = "completed";
        break;
    case runtime::AgentStatus::Failed:
        native_status = "failed";
        break;
    default:
        native_status = "other";
        break;
    }

    // Resolve entered state names to dense ids (the same index identity Core
    // and the wasm globals use; names are display-only).
    std::vector<std::uint32_t> entered_ids;
    for (const auto &name : entered_names) {
        const auto it = std::find(agent->states.begin(), agent->states.end(), name);
        if (it == agent->states.end()) {
            std::cerr << "native entered an undeclared state '" << name << "'\n";
            return 1;
        }
        entered_ids.push_back(static_cast<std::uint32_t>(it - agent->states.begin()));
    }
    const auto final_it =
        std::find(agent->states.begin(), agent->states.end(), native_result.current_state);
    const std::int64_t final_id = final_it == agent->states.end()
                                      ? -1
                                      : static_cast<std::int64_t>(final_it - agent->states.begin());

    const auto core = ir::core::lower_ahfl_to_core(*program);
    if (!core.ok()) {
        std::cerr << core.diagnostics.front().code << ": " << core.diagnostics.front().message
                  << "\n";
        return 1;
    }
    const auto layouts = ir::core::compute_core_layouts(core.program);
    if (!layouts.ok() || !layouts.table.has_value()) {
        if (!layouts.diagnostics.empty()) {
            std::cerr << layouts.diagnostics.front().code << ": "
                      << layouts.diagnostics.front().message << "\n";
        }
        return 1;
    }
    const auto emitted = backends::emit_core_wasm(
        core.program, *layouts.table, {ir::core::CoreAgentId{0}, backends::WasmProfileKind::Wasi});
    if (!emitted.ok()) {
        std::cerr << emitted.diagnostics.front().code << ": " << emitted.diagnostics.front().message
                  << "\n";
        return 1;
    }

    std::ofstream out(argv[2], std::ios::binary | std::ios::trunc);
    const auto &bytes = emitted.artifact->bytes;
    out.write(reinterpret_cast<const char *>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        std::cerr << "failed to write complete wasm artifact\n";
        return 1;
    }

    std::cout << "native_status=" << native_status << " entered_ids=";
    for (std::size_t i = 0; i < entered_ids.size(); ++i) {
        std::cout << (i == 0 ? "" : ",") << entered_ids[i];
    }
    std::cout << " final_state_id=" << final_id
              << " transition_count=" << native_result.stats.state_transitions
              << " initial_state_id="
              << (std::find(agent->states.begin(), agent->states.end(), agent->initial_state) -
                  agent->states.begin())
              << "\n";
    return 0;
}
