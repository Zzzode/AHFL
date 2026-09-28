// RFC 0026 KR6.5 E4-B2-C capability-workflow producer (EMIT-ONLY). One checked
// program is driven through the real frontend -> resolve -> typecheck -> validate
// -> IR -> Core -> P4-D layout -> emit_core_wasm path and the bytes are written
// out. Per the B2-C Q-P1 ruling this probe does NOT call the A2 runtime module
// context and does NOT link ahfl_runtime_engine; genuine emitter->A2 admission
// is owned by tests/unit/runtime/engine/core_wasm_schema_module.cpp. It prints a
// small structural summary of the emitted capability-workflow module.

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/core_layout.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/validate.hpp"
#include "compiler/backends/wasm/core_wasm_codegen.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
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

} // namespace

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "usage: ahfl_core_wasm_capability_workflow_probe "
                     "<source.ahfl> <output.wasm>\n";
        return 2;
    }
    auto program = compile_fixture(argv[1]);
    if (!program.has_value()) {
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

    const auto &bytes = emitted.artifact->bytes;
    std::ofstream out(argv[2], std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char *>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        std::cerr << "failed to write complete wasm artifact\n";
        return 1;
    }

    // A capability workflow carries a non-empty import table (the E2 ahfl_cap ABI)
    // reflected in the artifact's imports summary. Report it structurally.
    std::cout << "capability_workflow_bytes=" << bytes.size()
              << " import_count=" << emitted.artifact->imports.size()
              << " packaged_instances=" << emitted.artifact->packaged_agent_instances.size()
              << "\n";
    return 0;
}
