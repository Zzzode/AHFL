// RFC 0026 KR6.8 WH-4 (Decision 2, Option Z): the evaluator-free
// WorkflowResult method definitions, moved verbatim from
// workflow_runtime.cpp. The type is neutral (shared by the evaluator runtime
// and the wasm facade); only the method bodies live here, in the engine
// target — the survivor home that holds the resume/wire authorities and
// survives WH-9.

#include "ahfl/runtime/workflow_result.hpp"

namespace ahfl::runtime {

bool WorkflowResult::has_errors() const {
    return diagnostics.has_error();
}

WorkflowStatus WorkflowResult::status() const noexcept {
    if (report.status == RunTerminalStatus::Completed) {
        return WorkflowStatus::Completed;
    }
    if (report.status == RunTerminalStatus::Suspended) {
        return WorkflowStatus::Suspended;
    }
    if (!report.failure_kind.has_value()) {
        return WorkflowStatus::NodeFailed;
    }
    switch (*report.failure_kind) {
    case WorkflowFailureKind::DependencyFailed:
        return WorkflowStatus::DependencyFailed;
    case WorkflowFailureKind::EvaluationFailed:
        return WorkflowStatus::EvalError;
    case WorkflowFailureKind::NodeFailed:
    case WorkflowFailureKind::BudgetRejected:
    case WorkflowFailureKind::Cancelled:
    case WorkflowFailureKind::Interrupted:
        return WorkflowStatus::NodeFailed;
    }
    return WorkflowStatus::NodeFailed;
}

const Value *WorkflowResult::value(RuntimeValueId id) const noexcept {
    if (!id.valid() || id.index() >= values.size()) {
        return nullptr;
    }
    return &values[id.index()];
}

const Value *WorkflowResult::output() const noexcept {
    return report.output.has_value() ? value(*report.output) : nullptr;
}

} // namespace ahfl::runtime
