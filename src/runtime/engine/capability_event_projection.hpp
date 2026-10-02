#pragma once

// RFC 0026 KR6.8 WH-5c.8 (GAP 8): the SINGLE host-side projection of ONE
// aggregate CapabilityCallResult into the lifecycle event stream. The
// evaluator invokes the capability invoker ONCE and synthesizes per-attempt
// events from the aggregate result (workflow_runtime.cpp); the wasm lanes
// collect the same aggregate result during invoke_run2 and project it
// post-run. Both lanes call THIS helper so the event stream is identical
// (same event kinds, same order, same payload fields) regardless of which
// execution engine produced the result.
//
// The helper is engine/ code (no wasm3 types) so it survives the WH-9
// evaluator deletion together with capability_bridge.hpp.
//
// Projection contract (mirrors the evaluator's synthesis loop exactly):
//   1. Pending -> emit nothing (the node-level NodeSuspended terminal records
//      the pause; a Started without a terminal would dangle).
//   2. attempts = max(call.attempts, 1); for each attempt: allocate an
//      invocation id (attempt 1 uses the caller-supplied first_invocation
//      when valid, else the helper allocates; retry attempts are always
//      allocated here), emit CapabilityRetryScheduled BETWEEN attempts,
//      emit CapabilityStarted; on the terminal attempt with usage emit
//      CapabilityUsageRecorded; on a non-terminal-success path emit
//      CapabilityFailed.
//   3. provider_degraded -> add_provider(degraded) + add_provider(selected),
//      emit ProviderDegraded.
//   4. success -> emit CapabilityCompleted (last-attempt invocation id,
//      caller-registered output_value_id, raw attempts, cache_hit);
//      failure -> NO CapabilityCompleted.
//   5. usage.notices -> each notice becomes a WARNING diagnostic in the
//      result's DiagnosticBag, with the same code/message the evaluator
//      used to emit at the capability_eval boundary.

#include <chrono>
#include <functional>
#include <optional>

#include "ahfl/runtime/execution_event.hpp"
#include "runtime/engine/capability_bridge.hpp"
#include "runtime/engine/workflow_result.hpp"

namespace ahfl::runtime {

/// Project one aggregate capability call result into the event stream.
///
/// \param result            WorkflowResult whose event store, metadata store,
///                          and diagnostic bag receive the projection.
/// \param monotonic_clock   Timestamp source for every emitted event. The
///                          evaluator passes the run's monotonic offset; the
///                          wasm lanes pass a zero-returning function.
/// \param node              The calling node's WorkflowNodeId.
/// \param capability        The capability's CapabilityId.
/// \param provider          The runtime provider id (the evaluator's
///                          "runtime" provider).
/// \param call              The aggregate CapabilityCallResult.
/// \param output_value_id   The caller-registered output value id (registered
///                          on success before calling). nullopt when the call
///                          produced no value.
/// \param first_invocation  The pre-allocated invocation id for attempt 1
///                          (evaluator pre-allocates for the idempotency key /
///                          invocation context). Pass an invalid id
///                          (InvocationId{}) to let the helper allocate it.
void project_capability_call_events(
    WorkflowResult &result,
    std::function<std::chrono::nanoseconds()> monotonic_clock,
    WorkflowNodeId node,
    CapabilityId capability,
    ProviderId provider,
    const CapabilityCallResult &call,
    std::optional<RuntimeValueId> output_value_id,
    InvocationId first_invocation);

} // namespace ahfl::runtime
