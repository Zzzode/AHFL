// RFC 0026 KR6.8 WH-4 fix-forward D-A: the wasm3-backed workflow runtime
// facade implementation.

#include "runtime/wasm_runner/wasm_workflow_runtime.hpp"

#include "runtime/wasm_host/wasm_error_codes.hpp"
#include "runtime/wasm_host/workflow_session.hpp"

#include "compiler/backends/wasm/core_wasm_codegen.hpp"

#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/core_layout.hpp"
#include "ahfl/compiler/ir/program_view.hpp"

#include <utility>

namespace ahfl::runtime::wasm_runner {

namespace irc = ahfl::ir::core;
namespace bd = ahfl::backends;

namespace {

// WH-6 (kr68 §12.7.8): record a compile-pipeline failure into a DiagnosticBag.
// A stage-level wasm.compile-failed diagnostic names the failing stage (and
// the workflow, for emission), then every pipeline diagnostic is recorded as
// a first-class entry carrying its own code + message + SourceRange (Principle
// 5) — replacing the old flattened string, which dropped SourceRanges.
void record_compile_failure(DiagnosticBag &bag, std::string stage,
                            const std::vector<irc::CoreLowerDiagnostic> &diags) {
    bag.error()
        .code(std::string{wasm_host::wasm_diag::kCompileFailed})
        .message(std::move(stage))
        .emit();
    for (const auto &d : diags) {
        auto builder = d.severity == irc::CoreDiagnosticSeverity::Error
                           ? bag.error()
                           : bag.warning();
        std::move(builder)
            .code(d.code)
            .message(d.message)
            .range(d.source_range)
            .emit();
    }
}

void record_compile_failure(DiagnosticBag &bag, std::string stage,
                            const std::vector<bd::CoreWasmDiagnostic> &diags) {
    bag.error()
        .code(std::string{wasm_host::wasm_diag::kCompileFailed})
        .message(std::move(stage))
        .emit();
    for (const auto &d : diags) {
        bag.error().code(d.code).message(d.message).range(d.source_range).emit();
    }
}

} // namespace

WasmWorkflowRuntime::WasmWorkflowRuntime(const ir::Program &program,
                                         WasmWorkflowRuntimeConfig config)
    : config_(std::move(config)) {
    // WH-4b: pre-compute the capability effect map for the intent-emitting
    // wrapper (avoids storing a ProgramIndex, which would require the program
    // to outlive the facade).
    ir::ProgramIndex prog_index(program);
    for (const auto *cap : prog_index.capabilities()) {
        if (cap != nullptr) {
            capability_effects_.emplace(cap->name, cap->effect.kind);
        }
    }

    // WH-5c.6: pre-compute the host-side range tables for the session's
    // failure-diagnostic range resolvers (design 12.15.17.1, Option A). The
    // facade is the only wasm-lane component holding the ir::Program; ranges
    // are NOT encoded in the wasm modules, they ride into the session via
    // the WorkflowSessionConfig resolver seam. Same lifetime pattern as
    // capability_effects_: no ProgramIndex stored.
    for (const auto *wf : prog_index.workflows()) {
        if (wf == nullptr) {
            continue;
        }
        auto &ranges = node_ranges_by_workflow_[wf->name];
        ranges.reserve(wf->nodes.size());
        for (const auto &node : wf->nodes) {
            ranges.push_back(node.source_range);
        }
    }
    for (const auto *cap : prog_index.capabilities()) {
        if (cap == nullptr || !cap->symbol_ref.id.has_value()) {
            continue;
        }
        capability_ranges_.emplace(
            static_cast<std::uint64_t>(*cap->symbol_ref.id),
            cap->provenance.source_range);
    }

    // Lower AHFL-IR to Core-IR.
    auto core = irc::lower_ahfl_to_core(program);
    if (!core.ok()) {
        record_compile_failure(compile_errors_,
                               "wasm workflow runtime: core lowering failed",
                               core.diagnostics);
        return;
    }

    // Compute the wasm32 physical layout side artifact.
    auto layouts = irc::compute_core_layouts(core.program);
    if (!layouts.ok() || !layouts.table.has_value()) {
        record_compile_failure(compile_errors_,
                               "wasm workflow runtime: layout computation failed",
                               layouts.diagnostics);
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
            record_compile_failure(
                compile_errors_,
                "wasm workflow runtime: wasm emission failed for workflow '" +
                    wf.name + "'",
                emitted.diagnostics);
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
    // Compilation failure: every run returns a failed result carrying the
    // compile diagnostics (§12.7.8: the bag holds the pipeline diagnostics
    // with their SourceRanges, not a flattened string).
    if (compile_errors_.has_error()) {
        WorkflowResult result;
        result.report.status = RunTerminalStatus::Failed;
        result.report.failure_kind = WorkflowFailureKind::NodeFailed;
        result.diagnostics = compile_errors_;
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

    // WH-4b: build the session config directly (the run_wasm_workflow
    // convenience function does not carry the recovery/intent fields).
    wasm_host::WorkflowSessionConfig session_config;
    session_config.state_entered_hook =
        std::move(config_.hooks.state_entered_hook);
    session_config.capability_invoked_hook =
        std::move(config_.hooks.capability_invoked_hook);
    session_config.capability_result_observer =
        std::move(config_.hooks.capability_result_observer);
    session_config.node_completed_hook =
        std::move(config_.hooks.node_completed_hook);
    session_config.post_run2_memory_mutator =
        std::move(config_.post_run2_memory_mutator);
    session_config.name_resolver = config_.name_resolver;

    // WH-5c.6: install the failure-diagnostic range resolvers (design
    // 12.15.17.1). Build the SCHEDULE-ORDER range vector for this run: the
    // descriptor's nodes array IS the schedule (invariant
    // nodes[i].schedule_pos == i), and each node's dense node_id indexes the
    // workflow's source-order range vector. An out-of-range node_id yields
    // nullopt -- range resolution must never crash on a corrupt descriptor.
    std::vector<ir::SourceRangeOpt> schedule_ranges;
    if (auto ranges_it = node_ranges_by_workflow_.find(workflow_name);
        ranges_it != node_ranges_by_workflow_.end()) {
        schedule_ranges.reserve(it->second.descriptor.nodes.size());
        for (const auto &node_desc : it->second.descriptor.nodes) {
            const auto &source_ranges = ranges_it->second;
            if (node_desc.node_id < source_ranges.size()) {
                schedule_ranges.push_back(source_ranges[node_desc.node_id]);
            } else {
                schedule_ranges.push_back(std::nullopt);
            }
        }
    }
    session_config.node_range_resolver =
        [schedule_ranges = std::move(schedule_ranges)](
            std::uint32_t schedule_pos) -> ir::SourceRangeOpt {
            if (schedule_pos < schedule_ranges.size()) {
                return schedule_ranges[schedule_pos];
            }
            return std::nullopt;
        };
    // The session completes synchronously within run(), so capturing the
    // facade's map by reference is safe (same pattern as the intent wrapper
    // capturing &capability_effects_).
    session_config.capability_range_resolver =
        [&cap_ranges = capability_ranges_](
            std::uint64_t source_symbol) -> ir::SourceRangeOpt {
            auto cap_it = cap_ranges.find(source_symbol);
            if (cap_it != cap_ranges.end()) {
                return cap_it->second;
            }
            return std::nullopt;
        };

    // WH-4b: intent-emitting wrapper. Mirrors the evaluator at
    // workflow_runtime.cpp:1050-1058: right BEFORE a durable_write /
    // financial_write capability is dispatched, fire the write-ahead intent
    // with the idempotency key stamped by the session. Memo hits and
    // frontier injections never reach the invoker, so a resumed run emits
    // zero intents.
    ContextualCapabilityInvoker session_invoker = config_.invoker;
    if (config_.durable_write_intent_sink) {
        auto inner = std::move(session_invoker);
        session_invoker =
            [inner = std::move(inner), &effects = capability_effects_,
             &sink = config_.durable_write_intent_sink](
                const CapabilityInvocationContext &ctx,
                const std::string &name,
                const std::vector<Value> &args) -> CapabilityCallResult {
            if (ctx.source_capability_symbol_id.has_value()) {
                auto it = effects.find(name);
                if (it != effects.end() &&
                    (it->second == ir::CapabilityEffectKind::DurableWrite ||
                     it->second ==
                         ir::CapabilityEffectKind::FinancialWrite)) {
                    sink(ctx.idempotency_key, name);
                }
            }
            return inner(ctx, name, args);
        };
    }
    session_config.invoker = std::move(session_invoker);

    // WH-4b: thread the recovery fields. The snapshot + pending result are
    // move-only and are consumed on the first resume run (reset after move
    // so a subsequent run() is a fresh run, not a stale resume).
    if (config_.recovery_snapshot.has_value()) {
        session_config.recovery_snapshot =
            std::move(config_.recovery_snapshot);
        config_.recovery_snapshot.reset();
    }
    session_config.recovery_store = config_.recovery_store;
    if (config_.resume_pending_result.has_value()) {
        session_config.resume_pending_result =
            std::move(config_.resume_pending_result);
        config_.resume_pending_result.reset();
    }
    if (config_.resume_pending_result_wire_json.has_value()) {
        session_config.resume_pending_result_wire_json =
            std::move(config_.resume_pending_result_wire_json);
        config_.resume_pending_result_wire_json.reset();
    }

    // Drive the wasm_host workflow session.
    auto session_result = wasm_host::run_workflow_session(
        it->second.module_bytes, it->second.descriptor, input,
        std::move(session_config));

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
