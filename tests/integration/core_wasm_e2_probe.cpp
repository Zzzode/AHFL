// KR6.5 E2 same-frontend producer. One checked program is forked to the native
// AgentRuntime capability seam and AHFL->Core->P4-D->wasm.

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
#include "runtime/evaluator/value_json.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

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

[[nodiscard]] evaluator::Value frame(std::string type, std::string value) {
    evaluator::FieldMap fields;
    fields.set("value", std::make_unique<evaluator::Value>(evaluator::make_string(std::move(value))));
    return evaluator::Value{evaluator::StructValue{std::move(type), std::move(fields)}};
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "usage: ahfl_core_wasm_e2_probe <source.ahfl> <output.wasm>\n";
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

    auto input = frame("wasm::e2_capability::InputFrame", "input");
    const auto expected = frame("wasm::e2_capability::OutputFrame", "echo");
    const auto input_json = evaluator::value_to_json(input);
    const auto expected_json = evaluator::value_to_json(expected);
    std::size_t calls = 0;
    std::string called_name;
    std::string called_arg_json;
    runtime::AgentRuntime native(*agent, *flow);
    native.set_capability_invoker(
        [&](const std::string &name,
            const std::vector<evaluator::Value> &args) -> runtime::CapabilityCallResult {
            ++calls;
            called_name = name;
            called_arg_json = args.size() == 1 ? evaluator::value_to_json(args[0]) : "";
            if (args.size() != 1 || called_arg_json != input_json) {
                return {.status = runtime::CapabilityCallStatus::Error,
                        .value = std::nullopt,
                        .error_message = "unexpected E2 capability invocation"};
            }
            return {.status = runtime::CapabilityCallStatus::Success,
                    .value = frame("wasm::e2_capability::OutputFrame", "echo")};
        });
    const auto native_result = native.run(std::move(input));
    const auto final_it = std::find(agent->states.begin(), agent->states.end(),
                                    native_result.current_state);
    if (native_result.status != runtime::AgentStatus::Completed ||
        final_it == agent->states.end() || native_result.stats.state_transitions != 1 ||
        calls != 1 || called_name.find("Echo") == std::string::npos ||
        !native_result.output.has_value() ||
        evaluator::value_to_json(*native_result.output) != expected_json) {
        std::cerr << "native AgentRuntime observation does not match E2 capability contract"
                  << " status=" << static_cast<int>(native_result.status)
                  << " state=" << native_result.current_state
                  << " transitions=" << native_result.stats.state_transitions
                  << " calls=" << calls
                  << " called_name=" << called_name
                  << " called_arg=" << called_arg_json
                  << " output="
                  << (native_result.output.has_value()
                          ? evaluator::value_to_json(*native_result.output)
                          : std::string{"<none>"})
                  << " expected=" << expected_json << "\n";
        return 1;
    }

    const auto core = ir::core::lower_ahfl_to_core(*program);
    if (!core.ok()) {
        std::cerr << core.diagnostics.front().code << ": "
                  << core.diagnostics.front().message << "\n";
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
        core.program,
        *layouts.table,
        {ir::core::CoreAgentId{0}, backends::WasmProfileKind::Wasi});
    if (!emitted.ok()) {
        std::cerr << emitted.diagnostics.front().code << ": "
                  << emitted.diagnostics.front().message << "\n";
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
    if (core.program.capabilities.size() != 1 ||
        !core.program.capabilities[0].symbol_ref.id.has_value()) {
        std::cerr << "E2 Core capability identity is missing\n";
        return 1;
    }

    std::cout << "input_json=" << input_json << "\n"
              << "output_json=" << expected_json << "\n"
              << "final_state_id=" << (final_it - agent->states.begin())
              << " transition_count=" << native_result.stats.state_transitions
              << " capability_calls=" << calls
              << " symbol_id=" << *core.program.capabilities[0].symbol_ref.id << "\n";
    return 0;
}
