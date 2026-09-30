// RFC 0026 KR6.8 WH-4 fix-forward D-A: the wasm3-backed workflow runtime
// facade implementation.

#include "runtime/wasm_runner/wasm_workflow_runtime.hpp"

#include "runtime/wasm_host/wasm_workflow_runtime.hpp"
#include "runtime/wasm_host/wasm_error_codes.hpp"

#include "compiler/backends/wasm/core_wasm_codegen.hpp"

#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/core_layout.hpp"

#include <utility>

namespace ahfl::runtime::wasm_runner {

namespace irc = ahfl::ir::core;
namespace bd = ahfl::backends;

WasmWorkflowRuntime::WasmWorkflowRuntime(const ir::Program &program,
                                         WasmWorkflowRuntimeConfig config)
    : config_(std::move(config)) {
    // Lower AHFL-IR to Core-IR.
    auto core = irc::lower_ahfl_to_core(program);
    if (!core.ok()) {
        std::string msg = "wasm workflow runtime: core lowering failed";
        for (const auto &d : core.diagnostics) {
            msg += "\n  [" + d.code + "] " + d.message;
        }
        compile_error_ = std::move(msg);
        return;
    }

    // Compute the wasm32 physical layout side artifact.
    auto layouts = irc::compute_core_layouts(core.program);
    if (!layouts.ok() || !layouts.table.has_value()) {
        std::string msg = "wasm workflow runtime: layout computation failed";
        for (const auto &d : layouts.diagnostics) {
            msg += "\n  [" + d.code + "] " + d.message;
        }
        compile_error_ = std::move(msg);
        return;
    }

    // Emit wasm for every workflow in the program.
    for (std::size_t i = 0; i < core.program.workflows.size(); ++i) {
        const auto &wf = core.program.workflows[i];
        auto emitted = bd::emit_core_wasm(
            core.program, *layouts.table,
            {irc::CoreWorkflowId{static_cast<std::uint32_t>(i)},
             bd::WasmProfileKind::Wasi});
        if (!emitted.artifact.has_value() || !emitted.descriptor.has_value()) {
            std::string msg =
                "wasm workflow runtime: wasm emission failed for workflow '" +
                wf.name + "'";
            for (const auto &d : emitted.diagnostics) {
                msg += "\n  [" + d.code + "] " + d.message;
            }
            compile_error_ = std::move(msg);
            workflows_.clear();
            return;
        }
        workflows_.emplace(
            wf.name,
            CompiledWorkflow{
                .module_bytes = std::move(emitted.artifact->bytes),
                .descriptor = std::move(*emitted.descriptor),
            });
    }
}

WorkflowResult WasmWorkflowRuntime::run(const std::string &workflow_name,
                                        Value input) {
    // Compilation failure: every run returns a failed result.
    if (compile_error_.has_value()) {
        WorkflowResult result;
        result.report.status = RunTerminalStatus::Failed;
        result.report.failure_kind = WorkflowFailureKind::NodeFailed;
        result.diagnostics.error()
            .code(std::string{wasm_host::wasm_diag::kCompileFailed})
            .message(*compile_error_)
            .emit();
        return result;
    }

    // Look up the compiled workflow by name.
    auto it = workflows_.find(workflow_name);
    if (it == workflows_.end()) {
        WorkflowResult result;
        result.report.status = RunTerminalStatus::Failed;
        result.report.failure_kind = WorkflowFailureKind::NodeFailed;
        result.diagnostics.error()
            .code(std::string{wasm_host::wasm_diag::kWorkflowNotFound})
            .message("wasm workflow runtime: workflow '" + workflow_name +
                     "' not found")
            .emit();
        return result;
    }

    // Drive the wasm_host workflow session.
    auto session_result = wasm_host::run_wasm_workflow(
        it->second.module_bytes, it->second.descriptor, input,
        config_.hooks, config_.invoker, config_.name_resolver);

    if (!session_result.has_value()) {
        // Pre-run setup failure (admission, instantiation, pack). Map to a
        // failed WorkflowResult (D-D: never return bare std::string errors
        // to facade callers).
        WorkflowResult result;
        result.report.status = RunTerminalStatus::Failed;
        result.report.failure_kind = WorkflowFailureKind::NodeFailed;
        result.diagnostics.error()
            .code(std::string{wasm_host::wasm_diag::kSessionFailed})
            .message("wasm workflow runtime: " + session_result.error())
            .emit();
        return result;
    }

    return std::move(session_result->result);
}

} // namespace ahfl::runtime::wasm_runner
