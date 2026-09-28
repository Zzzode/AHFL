// KR6.5 E3 same-frontend producer. One checked program is forked to native
// WorkflowRuntime and AHFL->Core->P4-D->wasm.

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/core_layout.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/validate.hpp"
#include "compiler/backends/wasm/core_wasm_codegen.hpp"
#include "runtime/engine/workflow_runtime.hpp"
#include "runtime/evaluator/value.hpp"
#include "runtime/evaluator/value_json.hpp"

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
    return evaluator::Value{
        evaluator::StructValue{"wasm::e3_workflow::Frame", std::move(fields)}};
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "usage: ahfl_core_wasm_e3_probe <source.ahfl> <output.wasm>\n";
        return 2;
    }
    auto program = compile_fixture(argv[1]);
    if (!program.has_value()) {
        return 1;
    }

    std::vector<std::string> schedule;
    std::size_t completed_nodes = 0;
    std::size_t state_entries = 0;
    runtime::WorkflowRuntimeConfig config;
    config.agent_input_hook = [&](runtime::AgentId,
                                  std::string_view,
                                  std::string_view node_name,
                                  const runtime::Value &) {
        schedule.emplace_back(node_name);
    };
    config.node_completed_hook =
        [&](runtime::AgentId, std::string_view, const runtime::Value &) {
            ++completed_nodes;
        };
    config.state_entered_hook =
        [&](runtime::AgentId, std::string_view, std::string_view, std::string_view) {
            ++state_entries;
        };

    auto input = fixture_input();
    const auto input_json = evaluator::value_to_json(input);
    runtime::WorkflowRuntime native(*program, std::move(config));
    const auto native_result =
        native.run("wasm::e3_workflow::IdentityPipeline", std::move(input));
    const std::vector<std::string> expected_schedule{"first", "second"};
    if (native_result.status() != runtime::WorkflowStatus::Completed ||
        native_result.output() == nullptr ||
        evaluator::value_to_json(*native_result.output()) != input_json ||
        schedule != expected_schedule || completed_nodes != 2 || state_entries != 4) {
        std::cerr << "native WorkflowRuntime observation does not match E3 identity contract"
                  << " status=" << static_cast<int>(native_result.status())
                  << " completed=" << completed_nodes
                  << " state_entries=" << state_entries
                  << " schedule_size=" << schedule.size() << "\n";
        native_result.diagnostics.render(std::cerr);
        return 1;
    }

    const auto core = ir::core::lower_ahfl_to_core(*program);
    if (!core.ok() || core.program.workflows.size() != 1) {
        if (!core.diagnostics.empty()) {
            std::cerr << core.diagnostics.front().code << ": "
                      << core.diagnostics.front().message << "\n";
        }
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
        {ir::core::CoreWorkflowId{0}, backends::WasmProfileKind::Wasi});
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

    std::cout << "schedule=first,second"
              << " completed_nodes=" << completed_nodes
              << " transition_count=" << (state_entries - completed_nodes)
              << " identity_output=1\n";
    return 0;
}
