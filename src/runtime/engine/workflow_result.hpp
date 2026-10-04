#pragma once

// RFC 0026 KR6.8 WH-4 (Decision 2, Option Z) + WH-4 fix-forward D-E: the
// neutral workflow result type. `WorkflowResult` + `WorkflowStatus` were moved
// here from `runtime/engine/workflow_runtime.hpp` so that the wasm3-backed
// `WasmWorkflowRuntime` produces the same neutral type without depending on
// any other implementation header. The renderer + projections consume only
// these neutral fields.
//
// The struct is a bag of neutral fields (event store / metadata / report /
// values / diagnostics / durable-resume snapshot). `WorkflowStatus::EvalError`
// stays in the enum: the wasm lane produces it through fail-closed paths
// (event-stream-invalid, output decode failure); see
// `wasm_lifecycle.cpp` build_report and `workflow_session.cpp` run-failure
// sites.
//
// Include note (D-E, 2026-09-30): this header is SRC-INTERNAL. It lived under
// include/ahfl/runtime/ until the fix-forward moved it here, because it
// includes two src-internal headers (`runtime/value/value.hpp`,
// `runtime/engine/workflow_recovery.hpp`) and no installed header may reference
// a src-internal path. Every in-tree consumer has the build-tree src/ dir on
// its include path (the engine target PRIVATE, the wasm host / wasm runner
// targets PUBLIC BUILD_INTERFACE, each tooling/test target explicitly), so
// this is a build-tree-neutral move, not a new edge. It is NOT installed SDK
// surface (the installed execution_renderer.hpp / execution_projection.hpp
// only forward-declare the type; an in-tree tool that needs the complete type
// links the engine target).

#include <cstddef>
#include <optional>
#include <vector>

#include "ahfl/base/support/diagnostics.hpp"
#include "ahfl/runtime/execution_event.hpp"
#include "ahfl/runtime/execution_metadata.hpp"
#include "ahfl/runtime/execution_report.hpp"
#include "runtime/engine/workflow_recovery.hpp"
#include "runtime/value/value.hpp"

namespace ahfl::runtime {

// Workflow execution status.
enum class WorkflowStatus {
    Completed,
    NodeFailed,
    DependencyFailed,
    EvalError,
    // RFC 0022 (durable resume): a node suspended on a pending capability call.
    // Not a failure — WorkflowResult::suspended holds the resume record.
    Suspended,
};

// Workflow execution result.
struct WorkflowResult {
    ExecutionMetadataStore metadata;
    ExecutionEventStore events;
    ExecutionReport report;
    std::vector<Value> values;
    DiagnosticBag diagnostics;
    // RFC 0022 (durable resume): present iff status() == Suspended. The resume
    // record for the suspended node — its input Value plus the memo table of
    // capability results already produced — so the run can be continued by
    // passing this snapshot back via WorkflowRuntimeConfig::recovery_snapshot
    // together with exactly one pending-result source: the native
    // resume_pending_result Value or the raw resume_pending_result_wire_json bytes
    // (mutually exclusive; the runtime rejects supplying both at the consume gate).
    std::optional<WorkflowRecoverySnapshot> suspended{};

    [[nodiscard]] bool has_errors() const;
    [[nodiscard]] WorkflowStatus status() const noexcept;
    [[nodiscard]] const Value *value(RuntimeValueId id) const noexcept;
    [[nodiscard]] const Value *output() const noexcept;
};

} // namespace ahfl::runtime
