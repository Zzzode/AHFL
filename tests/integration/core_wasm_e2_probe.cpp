// KR6.5 E2 same-frontend producer. One checked program is forked to the native
// AgentRuntime capability seam and AHFL->Core->P4-D->wasm.

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/core_layout.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/validate.hpp"
#include "compiler/backends/wasm/core_wasm_codegen.hpp"
#include "base/json/json_value.hpp"
#include "runtime/engine/agent_runtime.hpp"
#include "runtime/engine/core_wasm_schema_transport.hpp"
#include "runtime/engine/core_wire_codec.hpp"
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

    // RFC 0026 E4-B1 C3 reference host: a generic host holds only the emitted
    // module bytes. Inspect them ONCE to mint the sole capability's Result binding
    // (ordinal 0), then prove the binding drives the codec: decode_json rebuilds
    // the exact Output native shape from its JSON DOM, and validate_value accepts
    // the original expected Output Value. Any mismatch is a nonzero exit.
    const auto binding_result =
        runtime::core_wasm_schema::make_wire_binding_from_core_wasm(
            bytes, /*capability_import_ordinal=*/0, ir::core::CoreWireRootKind::Result,
            /*param_index=*/0);
    if (!binding_result.ok() || !binding_result.binding.has_value()) {
        std::cerr << "E2 reference host failed to mint a wire binding from module bytes\n";
        return 1;
    }
    const auto &binding = *binding_result.binding;
    const auto &selector = binding.selector();
    if (selector.capability != ir::core::CoreCapabilityId{0} ||
        selector.expected_source_symbol != *core.program.capabilities[0].symbol_ref.id ||
        selector.kind != ir::core::CoreWireRootKind::Result || selector.param_index != 0) {
        std::cerr << "E2 minted binding selector does not match the Result capability identity\n";
        return 1;
    }

    auto expected_dom = json::parse_json(expected_json);
    if (!expected_dom.has_value() || *expected_dom == nullptr) {
        std::cerr << "E2 reference host could not parse the expected Output JSON\n";
        return 1;
    }
    const auto decoded = runtime::wire_codec::decode_json(**expected_dom, binding);
    if (!decoded.ok() || !decoded.value.has_value()) {
        std::cerr << "E2 reference host decode_json failed to produce a value\n";
        return 1;
    }
    // Assert the decoded native shape directly (not only canonical-JSON equality),
    // so a variant-folding regression cannot pass: it must be the OutputFrame
    // struct whose "value" field holds the echoed string.
    const auto *decoded_struct = std::get_if<evaluator::StructValue>(&decoded.value->node);
    const auto *decoded_field =
        decoded_struct != nullptr ? decoded_struct->fields.get("value") : nullptr;
    const auto *decoded_string =
        decoded_field != nullptr ? std::get_if<evaluator::StringValue>(&decoded_field->node)
                                 : nullptr;
    if (decoded_struct == nullptr ||
        decoded_struct->type_name != "wasm::e2_capability::OutputFrame" ||
        decoded_string == nullptr || decoded_string->value != "echo" ||
        evaluator::value_to_json(*decoded.value) != expected_json) {
        std::cerr << "E2 reference host decode_json did not rebuild the Output native shape\n";
        return 1;
    }
    const auto validated = runtime::wire_codec::validate_value(expected, binding);
    if (!validated.valid) {
        std::cerr << "E2 reference host validate_value rejected the expected Output: "
                  << validated.error << "\n";
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
