// RFC 0026 KR6.8 WH-4b: facade-level durable-resume end-to-end test.
//
// Drives the WasmWorkflowRuntime facade through the full
// suspend -> persist -> cold-start -> resume cycle, proving the
// WH-4b suspended-snapshot origination on the wasm lane:
//
//   1. e3 real AC1 cycle (P2-6): a capability workflow suspends on the
//      FIRST capability call (Echo returns PENDING). The resume uses ONLY
//      resume_pending_result_wire_json (P1-5a) and completes with zero
//      live calls. workflow_completed == 0 on suspend, == 1 on completion
//      (P2-7).
//   1b. WH-5b.3 AC2: hybrid P6+opaque resume. A P6 identity node feeds an
//       opaque capability-final node (Echo). The edge crosses the P4-D ->
//       wire-JSON boundary (P4D_TO_JSON transcode); the workflow return is
//       the opaque node (JSON_TO_P4D transcode). The suspend/resume cycle
//       proves the transcode calls are replay-safe.
//   1c. WH-5b.2: bridge PENDING suspend/resume. A hybrid P6-bridge + opaque
//       workflow suspends at the first bridge node (Bridge returns PENDING).
//       The resume uses wire JSON for the pending Bridge result; the bridge
//       frontier is injected (zero live calls for the frontier). The
//       workflow completes with the correct output. This proves the bridge
//       lane's graceful PENDING arm and the WH-5b.2 bridge-site table
//       survive the WH-4b suspend/resume cycle.
//   2. e4 memo round-trip: a 2-node opaque pipeline suspends on the
//      second capability (B returns PENDING). The first capability
//      (A) returned OK and was memoized. On resume (fresh facade,
//      cold-start from the on-disk snapshot), A is memo-supplied
//      (zero live calls) and B is frontier-injected (zero live
//      calls). The workflow completes deterministically. The snapshot's
//      memo tuple is asserted directly (P2-8).
//   3. Fail-closed family: a resume that supplies NO pending result
//      fails closed (the session rejects the resume, never silently
//      runs fresh).
//   4. Intent alignment: the durable_write_intent_sink fires for a
//      durable_write capability before dispatch, and fires ZERO
//      times on a resumed run (memo hits and frontier injections
//      never reach the invoker).
//   5. AC3 fail-closed cases (P1-3): corrupt snapshot JSON, workflow-id
//      mismatch, suspended node unknown, memo cap_id mismatch, memo
//      arg_hash mismatch, wrong-type wire JSON, pending identity
//      mismatch, replay divergence. Each asserts FAILED WorkflowResult,
//      actionable diagnostic, invoker never called.
//   6. Non-topological resume (P1-2): a workflow whose Kahn schedule
//      differs from declaration order. The memo classification must
//      compare schedule positions, not source-order node ids.
//   7. WH-4b P2 fail-closed family:
//      P2-A: shared capability, different args, resume identity (e6 fixture).
//      P2-B: frontier never hit on replay (fabricated complete memo +
//            tampered frontier ordinal).
//      P2-C: zero-length OK memo (replay-side defense; recording path is
//            provably unreachable on a deterministic guest).
//      P2-E: unresolvable pending cap_id (load-time validation fail-closed).
//      (P2-D is unreachable: the engine's alloc_then_write bounds check
//      is the reachable guard; see the reachability comment in
//      workflow_session.cpp origination.)
//
// The e4 fixture is tests/golden/wasm/e4_capability_workflow_resume_memo.ahfl.
// The e3 fixture is tests/golden/wasm/e3_capability_workflow_resume.ahfl.
// The e5 fixture is tests/golden/wasm/e5_non_topological_resume.ahfl.
// The e6 fixture is tests/golden/wasm/e6_shared_cap_resume.ahfl.
// The WH-5b.3 AC2 fixture is tests/golden/wasm/wh5b_hybrid_resume.ahfl.
// The WH-5b.2 fixture is tests/golden/wasm/wh5b_hybrid_p6_bridge.ahfl.

#include "runtime/wasm_runner/wasm_workflow_runtime.hpp"

#include "runtime/engine/workflow_recovery.hpp"
#include "runtime/engine/workflow_result.hpp"
#include "runtime/value/value.hpp"
#include "runtime/value/value_json.hpp"

#include "ahfl/runtime/execution_projection.hpp"
#include "conformance/compile_source.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

namespace wr = ahfl::runtime::wasm_runner;
namespace conf = ahfl::conformance;

using ahfl::runtime::CapabilityCallResult;
using ahfl::runtime::CapabilityCallStatus;
using ahfl::runtime::CapabilityInvocationContext;
using ahfl::runtime::Value;
using ahfl::runtime::WorkflowRecoverySnapshot;
using ahfl::runtime::WorkflowRecoveryStore;
using ahfl::runtime::WorkflowStatus;
using ahfl::runtime::value_from_json;

int g_checks = 0;
int g_pass = 0;

void check(bool condition, std::string_view label) {
    ++g_checks;
    if (condition) {
        ++g_pass;
    } else {
        std::cerr << "FAIL: " << label << "\n";
    }
}

// Shared invoker state (captured via shared_ptr so the test can inspect
// call counts and symbol ids after the invoker lambda is consumed).
struct InvokerState {
    int a_calls{0};
    int b_calls{0};
    int echo_calls{0};
    int bridge_calls{0};
    bool b_pending{true};
    bool echo_pending{true};
    bool bridge_pending{true};
    // WH-5b.2 §12.14.9 case 5: when set, the Bridge invoker returns Error
    // instead of Success/Pending. The workflow must fail with NodeFailed.
    bool bridge_error{false};
    // WH-5b.2 AC10: when set, the Bridge invoker returns Success on the first
    // call (ordinal 0, memoized) and Pending on the second (ordinal 1, the
    // suspend frontier). Used by the two-sequential-calls determinism test.
    bool bridge_success_then_pending{false};
    // The Frame struct type name for the Bridge result (module-dependent).
    std::string bridge_frame_type{"wasm::wh5b_hybrid_p6_bridge::Frame"};
    std::optional<std::size_t> a_symbol_id;
    std::optional<std::size_t> b_symbol_id;
    std::optional<std::size_t> echo_symbol_id;
    std::optional<std::size_t> bridge_symbol_id;
};

// A counting invoker for the e4/e5 memo round-trip and the e3 AC1 cycle.
// A returns Success (echo); B returns Pending on the first call
// (origination) and is never called on resume (frontier injection).
// Echo returns Pending on the first call (origination) and is never
// called on resume (frontier injection). The counters prove zero live
// side effects on the resumed run. The capability name is the fully
// qualified canonical name (e.g. "wasm::e4_...::A") or the short name
// "Echo" (when the e3 name_resolver returns it), so suffix matching on
// "::A" / "::B" / "::Echo" plus the bare-name fallback is unambiguous
// within these fixtures.
ahfl::runtime::ContextualCapabilityInvoker
make_counting_invoker(std::shared_ptr<InvokerState> state) {
    return [state = std::move(state)](
               const CapabilityInvocationContext &ctx,
               const std::string &name,
               const std::vector<Value> &args) -> CapabilityCallResult {
        CapabilityCallResult r;
        if (name == "A" || name.ends_with("::A")) {
            ++state->a_calls;
            state->a_symbol_id = ctx.source_capability_symbol_id;
            r.status = CapabilityCallStatus::Success;
            if (!args.empty()) {
                r.value = ahfl::runtime::clone_value(args[0]);
            }
        } else if (name == "B" || name.ends_with("::B")) {
            ++state->b_calls;
            state->b_symbol_id = ctx.source_capability_symbol_id;
            if (state->b_pending) {
                r.status = CapabilityCallStatus::Pending;
            } else {
                r.status = CapabilityCallStatus::Success;
                if (!args.empty()) {
                    r.value = ahfl::runtime::clone_value(args[0]);
                }
            }
        } else if (name == "Echo" || name.ends_with("::Echo")) {
            ++state->echo_calls;
            state->echo_symbol_id = ctx.source_capability_symbol_id;
            if (state->echo_pending) {
                r.status = CapabilityCallStatus::Pending;
            } else {
                r.status = CapabilityCallStatus::Success;
                if (!args.empty()) {
                    r.value = ahfl::runtime::clone_value(args[0]);
                }
            }
        } else if (name == "Bridge" || name.ends_with("::Bridge")) {
            // WH-5b.2: bridge-lane capability. Bridge(n: Int) -> Frame.
            // On Pending, the guest's graceful PENDING arm suspends the
            // workflow. On Success, echo back a Frame with the same n.
            // On Error (§12.14.9 case 5), the workflow must fail NodeFailed.
            ++state->bridge_calls;
            state->bridge_symbol_id = ctx.source_capability_symbol_id;
            if (state->bridge_error) {
                r.status = CapabilityCallStatus::Error;
                r.error_message = "bridge error injected by test";
            } else if (state->bridge_success_then_pending) {
                // AC10: first call Success (memoized), second call Pending.
                if (state->bridge_calls == 1) {
                    r.status = CapabilityCallStatus::Success;
                } else {
                    r.status = CapabilityCallStatus::Pending;
                }
            } else if (state->bridge_pending) {
                r.status = CapabilityCallStatus::Pending;
            } else {
                r.status = CapabilityCallStatus::Success;
            }
            if (r.status == CapabilityCallStatus::Success && !args.empty()) {
                if (const auto *int_val =
                        std::get_if<ahfl::runtime::IntValue>(
                            &args[0].node)) {
                    // Value is move-only (variant holds unique_ptr in
                    // MapValue); construct the field map explicitly.
                    std::unordered_map<std::string, Value> fields;
                    fields.emplace("n",
                                   ahfl::runtime::make_int(int_val->value));
                    r.value = ahfl::runtime::make_struct(
                        state->bridge_frame_type,
                        std::move(fields));
                }
            }
        } else {
            r.status = CapabilityCallStatus::Success;
            r.value = ahfl::runtime::make_none();
        }
        return r;
    };
}

// Extract the first error diagnostic message from a WorkflowResult.
[[nodiscard]] std::string
first_error_message(const ahfl::runtime::WorkflowResult &result) {
    for (const auto &d : result.diagnostics.entries()) {
        if (d.severity == ahfl::DiagnosticSeverity::Error) {
            return d.message;
        }
    }
    return {};
}

// Assert the workflow_completed audit projection (P2-7).
void check_workflow_completed(const ahfl::runtime::WorkflowResult &result,
                              std::size_t expected,
                              std::string_view tag) {
    auto audit = ahfl::runtime::build_execution_audit_projection(result);
    check(audit.has_value(), std::string(tag) + ".audit_projection");
    if (audit.has_value()) {
        check(audit->workflow_completed == expected,
              std::string(tag) + ".workflow_completed==" +
                  std::to_string(expected));
    }
}

// ==== 1. e3 real AC1 cycle (P2-6, P1-5a, P2-7) ====
//
// The e3 fixture has ONE capability call (Echo on `first`). The
// origination forces Echo to Pending, so the workflow suspends on the
// FIRST call. The resume uses ONLY resume_pending_result_wire_json (no
// native Value) and completes with zero live calls. This is the real
// AC1 cycle: suspend -> persist -> cold-start -> wire-JSON inject ->
// complete.

void test_e3_real_ac1_cycle(const std::filesystem::path &repo_root,
                            const std::filesystem::path &work_dir) {
    const auto source =
        repo_root / "tests/golden/wasm/e3_capability_workflow_resume.ahfl";
    std::string error;
    auto program = conf::compile_conformance_source(source, error);
    check(program.has_value(), "e3ac1.compile");
    if (!program.has_value()) {
        std::cerr << "  compile failed: " << error << "\n";
        return;
    }

    const auto snapshot_path = work_dir / "e3-ac1-snapshot.json";
    std::error_code ec;
    std::filesystem::remove(snapshot_path, ec);

    static constexpr std::string_view kWorkflow =
        "wasm::e3_capability_workflow_resume::CapabilityPipeline";
    static constexpr std::string_view kFrameJson =
        R"({"_type":"wasm::e3_capability_workflow_resume::Frame","value":"ac1-cycle"})";

    // ---- Process A: run until Echo returns PENDING, suspend, persist. ----
    auto state_a = std::make_shared<InvokerState>();
    {
        WorkflowRecoveryStore store(snapshot_path);
        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = make_counting_invoker(state_a);
        config.recovery_store = &store;
        config.name_resolver = [](std::uint64_t) -> std::optional<std::string> {
            return "Echo";
        };

        wr::WasmWorkflowRuntime runtime(*program, std::move(config));

        auto input = value_from_json(std::string(kFrameJson));
        check(input.has_value(), "e3ac1.input");
        if (!input.has_value()) {
            return;
        }

        auto suspended = runtime.run(std::string(kWorkflow), std::move(*input));

        check(suspended.status() == WorkflowStatus::Suspended,
              "e3ac1.suspended");
        check(!suspended.has_errors(), "e3ac1.no_errors");
        check(state_a->echo_calls == 1, "e3ac1.echo_called_once");
        check(suspended.suspended.has_value(), "e3ac1.has_resume_record");
        // P2-7: no WorkflowCompleted event on suspend.
        check_workflow_completed(suspended, 0, "e3ac1.suspend");
    }

    check(std::filesystem::exists(snapshot_path),
          "e3ac1.snapshot_file_on_disk");

    // ---- Process B: cold start. Fresh facade, on-disk snapshot is the
    // only link. Resume with ONLY wire JSON (P1-5a): no native Value.
    // Echo is frontier-injected (zero live calls). The identity node
    // `second` runs without any call. The workflow completes. ----
    auto state_b = std::make_shared<InvokerState>();
    state_b->echo_pending = false; // Echo would return Success if called (it must NOT be)
    {
        WorkflowRecoveryStore store(snapshot_path);
        auto loaded = store.load();
        check(loaded.has_value(), "e3ac1.snapshot_loaded");
        if (!loaded.has_value()) {
            return;
        }

        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = make_counting_invoker(state_b);
        config.recovery_snapshot = std::move(*loaded);
        // P1-5a: resume using ONLY wire JSON (no native resume_pending_result).
        config.resume_pending_result_wire_json = std::string(kFrameJson);
        config.name_resolver = [](std::uint64_t) -> std::optional<std::string> {
            return "Echo";
        };

        wr::WasmWorkflowRuntime runtime(*program, std::move(config));

        auto input = value_from_json(std::string(kFrameJson));
        check(input.has_value(), "e3ac1.resume_input");
        if (!input.has_value()) {
            return;
        }

        auto resumed = runtime.run(std::string(kWorkflow), std::move(*input));

        check(resumed.status() == WorkflowStatus::Completed,
              "e3ac1.completed");
        check(!resumed.has_errors(), "e3ac1.resume_no_errors");
        // Zero live side effects on resume: Echo is frontier-injected.
        check(state_b->echo_calls == 0, "e3ac1.zero_live_calls");
        // P2-7: exactly one WorkflowCompleted event on completion.
        check_workflow_completed(resumed, 1, "e3ac1.complete");

        const auto *output = resumed.output();
        check(output != nullptr, "e3ac1.has_output");
        if (output != nullptr) {
            const auto json = ahfl::runtime::value_to_json(*output);
            check(json == std::string(kFrameJson), "e3ac1.output_value");
        }
    }
}

// ==== 1b. WH-5b.3 AC2: hybrid P6+opaque resume (suspend -> persist ->
// cold-start -> wire-JSON inject -> complete) ====
//
// The hybrid fixture has a P6 identity node (compute) feeding an opaque
// capability-final node (echo). The edge compute->echo crosses the P4-D ->
// wire-JSON encoding boundary (P4D_TO_JSON transcode). The workflow return
// is the opaque echo node (JSON_TO_P4D transcode for the workflow_output
// region). Echo returns Pending on the first call, so the workflow suspends.
// On resume (fresh facade, cold-start from the on-disk snapshot), Echo is
// frontier-injected (zero live calls). The P6 node re-runs deterministically
// and both transcode calls re-execute (they are pure deterministic codec
// adapters, not memoized). The workflow completes with the correct output.
// This proves the transcode calls are replay-safe and the hybrid workflow
// survives the WH-4b suspend/resume cycle.

void test_wh5b3_hybrid_resume(const std::filesystem::path &repo_root,
                              const std::filesystem::path &work_dir) {
    const auto source =
        repo_root / "tests/golden/wasm/wh5b_hybrid_resume.ahfl";
    std::string error;
    auto program = conf::compile_conformance_source(source, error);
    check(program.has_value(), "wh5b3hr.compile");
    if (!program.has_value()) {
        std::cerr << "  compile failed: " << error << "\n";
        return;
    }

    const auto snapshot_path = work_dir / "wh5b3-hr-snapshot.json";
    std::error_code ec;
    std::filesystem::remove(snapshot_path, ec);

    static constexpr std::string_view kWorkflow =
        "wasm::wh5b_hybrid_resume::HybridResume";
    static constexpr std::string_view kFrameJson =
        R"({"_type":"wasm::wh5b_hybrid_resume::Frame","value":"hybrid-resume"})";

    // ---- Process A: run until Echo returns PENDING, suspend, persist. ----
    auto state_a = std::make_shared<InvokerState>();
    {
        WorkflowRecoveryStore store(snapshot_path);
        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = make_counting_invoker(state_a);
        config.recovery_store = &store;
        config.name_resolver = [](std::uint64_t) -> std::optional<std::string> {
            return "Echo";
        };

        wr::WasmWorkflowRuntime runtime(*program, std::move(config));

        auto input = value_from_json(std::string(kFrameJson));
        check(input.has_value(), "wh5b3hr.input");
        if (!input.has_value()) {
            return;
        }

        auto suspended = runtime.run(std::string(kWorkflow), std::move(*input));

        check(suspended.status() == WorkflowStatus::Suspended,
              "wh5b3hr.suspended");
        check(!suspended.has_errors(), "wh5b3hr.no_errors");
        check(state_a->echo_calls == 1, "wh5b3hr.echo_called_once");
        check(suspended.suspended.has_value(), "wh5b3hr.has_resume_record");
        check_workflow_completed(suspended, 0, "wh5b3hr.suspend");
    }

    check(std::filesystem::exists(snapshot_path),
          "wh5b3hr.snapshot_file_on_disk");

    // ---- Process B: cold start. Fresh facade, on-disk snapshot is the
    // only link. Resume with ONLY wire JSON (P1-5a): no native Value.
    // Echo is frontier-injected (zero live calls). The P6 compute node
    // re-runs deterministically; both transcode calls re-execute (pure
    // deterministic codec adapters, not memoized). The workflow completes.
    auto state_b = std::make_shared<InvokerState>();
    state_b->echo_pending = false; // Echo would return Success if called (it must NOT be)
    {
        WorkflowRecoveryStore store(snapshot_path);
        auto loaded = store.load();
        check(loaded.has_value(), "wh5b3hr.snapshot_loaded");
        if (!loaded.has_value()) {
            return;
        }

        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = make_counting_invoker(state_b);
        config.recovery_snapshot = std::move(*loaded);
        config.resume_pending_result_wire_json = std::string(kFrameJson);
        config.name_resolver = [](std::uint64_t) -> std::optional<std::string> {
            return "Echo";
        };

        wr::WasmWorkflowRuntime runtime(*program, std::move(config));

        auto input = value_from_json(std::string(kFrameJson));
        check(input.has_value(), "wh5b3hr.resume_input");
        if (!input.has_value()) {
            return;
        }

        auto resumed = runtime.run(std::string(kWorkflow), std::move(*input));

        check(resumed.status() == WorkflowStatus::Completed,
              "wh5b3hr.completed");
        check(!resumed.has_errors(), "wh5b3hr.resume_no_errors");
        check(state_b->echo_calls == 0, "wh5b3hr.zero_live_calls");
        check_workflow_completed(resumed, 1, "wh5b3hr.complete");

        const auto *output = resumed.output();
        check(output != nullptr, "wh5b3hr.has_output");
        if (output != nullptr) {
            const auto json = ahfl::runtime::value_to_json(*output);
            check(json == std::string(kFrameJson), "wh5b3hr.output_value");
        }
    }
}

// ==== 1c. WH-5b.2: bridge PENDING suspend/resume e2e ====
//
// The wh5b_hybrid_p6_bridge fixture has two P6 bridge nodes (bridge_a,
// bridge_b) and one opaque node (echo). The workflow return is bridge_a.
// Bridge returns Pending on the first call, so the workflow suspends at
// bridge_a (the first bridge node). On resume (fresh facade, cold-start
// from the on-disk snapshot), bridge_a's Bridge call is frontier-injected
// (zero live calls for the frontier). bridge_b's Bridge call and echo's
// Echo call are PostFrontier (live). The workflow completes with the
// correct output. This proves the bridge lane's graceful PENDING arm and
// the WH-5b.2 bridge-site table survive the WH-4b suspend/resume cycle.

void test_wh5b2_bridge_pending_resume(
    const std::filesystem::path &repo_root,
    const std::filesystem::path &work_dir) {
    const auto source =
        repo_root / "tests/golden/wasm/wh5b_hybrid_p6_bridge.ahfl";
    std::string error;
    auto program = conf::compile_conformance_source(source, error);
    check(program.has_value(), "wh5b2.compile");
    if (!program.has_value()) {
        std::cerr << "  compile failed: " << error << "\n";
        return;
    }

    const auto snapshot_path = work_dir / "wh5b2-bridge-snapshot.json";
    std::error_code ec;
    std::filesystem::remove(snapshot_path, ec);

    static constexpr std::string_view kWorkflow =
        "wasm::wh5b_hybrid_p6_bridge::HybridBridgePipeline";
    static constexpr std::string_view kFrameJson =
        R"({"_type":"wasm::wh5b_hybrid_p6_bridge::Frame","n":42})";

    // ---- Process A: run until Bridge returns PENDING, suspend, persist. ----
    auto state_a = std::make_shared<InvokerState>();
    {
        WorkflowRecoveryStore store(snapshot_path);
        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = make_counting_invoker(state_a);
        config.recovery_store = &store;
        // No name_resolver: the fallback resolves canonical names from the
        // descriptor (Bridge and Echo have distinct source_symbols).

        wr::WasmWorkflowRuntime runtime(*program, std::move(config));

        auto input = value_from_json(std::string(kFrameJson));
        check(input.has_value(), "wh5b2.input");
        if (!input.has_value()) {
            return;
        }

        auto suspended = runtime.run(std::string(kWorkflow), std::move(*input));

        check(suspended.status() == WorkflowStatus::Suspended,
              "wh5b2.suspended");
        check(!suspended.has_errors(), "wh5b2.no_errors");
        check(state_a->bridge_calls == 1, "wh5b2.bridge_called_once");
        check(suspended.suspended.has_value(), "wh5b2.has_resume_record");
        check_workflow_completed(suspended, 0, "wh5b2.suspend");

        // P2-1: on suspend, the wasm lane's NodeSkipped events must carry
        // EMPTY blocking_dependencies, matching the expected semantics
        // (nodes never reached are not "blocked by" the suspended node).
        // The wh5b_hybrid_p6_bridge fixture has 3 nodes; bridge_a suspends,
        // so bridge_b and echo are NodeSkipped with empty deps.
        int skipped_count = 0;
        for (const auto &event : suspended.events.events()) {
            if (const auto *skipped =
                    std::get_if<ahfl::runtime::NodeSkipped>(
                        &event.payload)) {
                ++skipped_count;
                check(skipped->blocking_dependencies.empty(),
                      "wh5b2.skipped_empty_deps");
            }
        }
        check(skipped_count == 2, "wh5b2.two_skipped_nodes");
    }

    check(std::filesystem::exists(snapshot_path),
          "wh5b2.snapshot_file_on_disk");

    // ---- Process B: cold start. Fresh facade, on-disk snapshot is the
    // only link. Resume with wire JSON for the pending Bridge result.
    // bridge_a's Bridge call is frontier-injected (zero live calls for
    // the frontier). bridge_b's Bridge call and echo's Echo call are
    // PostFrontier (live, if reached by the DAG schedule). ----
    auto state_b = std::make_shared<InvokerState>();
    state_b->bridge_pending = false; // Bridge returns Success if called live
    state_b->echo_pending = false;   // Echo returns Success if called live
    {
        WorkflowRecoveryStore store(snapshot_path);
        auto loaded = store.load();
        check(loaded.has_value(), "wh5b2.snapshot_loaded");
        if (!loaded.has_value()) {
            return;
        }

        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = make_counting_invoker(state_b);
        config.recovery_snapshot = std::move(*loaded);
        config.resume_pending_result_wire_json = std::string(kFrameJson);

        wr::WasmWorkflowRuntime runtime(*program, std::move(config));

        auto input = value_from_json(std::string(kFrameJson));
        check(input.has_value(), "wh5b2.resume_input");
        if (!input.has_value()) {
            return;
        }

        auto resumed = runtime.run(std::string(kWorkflow), std::move(*input));

        check(resumed.status() == WorkflowStatus::Completed,
              "wh5b2.completed");
        check(!resumed.has_errors(), "wh5b2.resume_no_errors");
        // The frontier (bridge_a's Bridge) is injected, not live. The
        // PostFrontier bridge_b's Bridge call IS live on resume: the
        // invoker must have been called at least once (bridge_pending=false
        // so it returns Success). This replaces the former vacuous
        // `bridge_calls >= 0` check (P2-4): a non-negative count proves
        // nothing; proving the live invoker fired during resume AND the
        // workflow completed is the real PostFrontier-live guarantee.
        check(state_b->bridge_calls > 0, "wh5b2.bridge_live_called_during_resume");
        check_workflow_completed(resumed, 1, "wh5b2.complete");

        const auto *output = resumed.output();
        check(output != nullptr, "wh5b2.has_output");
        if (output != nullptr) {
            const auto json = ahfl::runtime::value_to_json(*output);
            check(json == std::string(kFrameJson), "wh5b2.output_value");
        }
    }
}

// ==== 1c-fc. WH-5b.2 §12.14.9 host-level fail-closed cases ================
//
// Case 5: ERROR status word. The Bridge invoker returns Error. The guest's
// bridge call must propagate the failure -> the workflow node fails
// (NodeFailed), the expected capability-error handling.
void test_wh5b2_bridge_error_fails(
    const std::filesystem::path &repo_root,
    const std::filesystem::path &work_dir) {
    const auto source =
        repo_root / "tests/golden/wasm/wh5b_hybrid_p6_bridge.ahfl";
    std::string error;
    auto program = conf::compile_conformance_source(source, error);
    check(program.has_value(), "wh5b2err.compile");
    if (!program.has_value()) {
        return;
    }

    auto state = std::make_shared<InvokerState>();
    state->bridge_error = true; // Bridge returns Error
    state->echo_pending = false;
    {
        WorkflowRecoveryStore store(work_dir / "wh5b2-err-snapshot.json");
        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = make_counting_invoker(state);
        config.recovery_store = &store;

        wr::WasmWorkflowRuntime runtime(*program, std::move(config));
        auto input = value_from_json(
            R"({"_type":"wasm::wh5b_hybrid_p6_bridge::Frame","n":42})");
        if (!input.has_value()) {
            check(false, "wh5b2err.input");
            return;
        }
        auto result = runtime.run(
            "wasm::wh5b_hybrid_p6_bridge::HybridBridgePipeline",
            std::move(*input));

        // §12.14.9 case 5: the workflow must NOT complete; the bridge ERROR
        // propagates as a node failure.
        check(result.status() != WorkflowStatus::Completed,
              "wh5b2err.not_completed");
        check(result.has_errors(), "wh5b2err.has_errors");
        const auto msg = first_error_message(result);
        check(!msg.empty(), "wh5b2err.actionable_diagnostic");

        // P2-2 (re-review): the failure path is the mirror of the suspend
        // path — nodes never reached because an UPSTREAM node failed carry
        // NON-empty blocking_dependencies (the failed node), unlike the
        // empty-dep NodeSkipped events emitted on suspend. bridge_a fails;
        // the remaining nodes must be skipped with a blocking dependency.
        int failure_skipped = 0;
        for (const auto &event : result.events.events()) {
            if (const auto *skipped =
                    std::get_if<ahfl::runtime::NodeSkipped>(&event.payload)) {
                ++failure_skipped;
                check(!skipped->blocking_dependencies.empty(),
                      "wh5b2err.skipped_has_blocking_dep");
            }
        }
        check(failure_skipped > 0, "wh5b2err.has_skipped_nodes");
    }
}

// Case 7: no tag-0 (node-completion event) published while suspended. When
// the runner pends on a Pending bridge call, the scheduler must NOT write a
// completion record for the suspended node. The persisted snapshot's
// completed_nodes must not contain the suspended node.
void test_wh5b2_no_tag0_on_suspend(
    const std::filesystem::path &repo_root,
    const std::filesystem::path &work_dir) {
    const auto source =
        repo_root / "tests/golden/wasm/wh5b_hybrid_p6_bridge.ahfl";
    std::string error;
    auto program = conf::compile_conformance_source(source, error);
    check(program.has_value(), "wh5b2tag0.compile");
    if (!program.has_value()) {
        return;
    }

    const auto snapshot_path = work_dir / "wh5b2-tag0-snapshot.json";
    std::error_code ec;
    std::filesystem::remove(snapshot_path, ec);

    auto state = std::make_shared<InvokerState>();
    {
        WorkflowRecoveryStore store(snapshot_path);
        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = make_counting_invoker(state);
        config.recovery_store = &store;

        wr::WasmWorkflowRuntime runtime(*program, std::move(config));
        auto input = value_from_json(
            R"({"_type":"wasm::wh5b_hybrid_p6_bridge::Frame","n":42})");
        if (!input.has_value()) {
            check(false, "wh5b2tag0.input");
            return;
        }
        auto suspended = runtime.run(
            "wasm::wh5b_hybrid_p6_bridge::HybridBridgePipeline",
            std::move(*input));

        check(suspended.status() == WorkflowStatus::Suspended,
              "wh5b2tag0.suspended");
        check(suspended.suspended.has_value(), "wh5b2tag0.has_suspended");

        // Direct event-stream pin (P2-3 re-review): a tag-0 record is exactly
        // what produces a NodeCompleted event. With the runner pending, the
        // scheduler returns before the tag-0 write, so the stream must contain
        // ZERO NodeCompleted events (and no NodeFailed), and exactly one
        // NodeSuspended for the pended bridge node.
        int node_completed_events = 0;
        int node_failed_events = 0;
        int node_suspended_events = 0;
        for (const auto &event : suspended.events.events()) {
            if (std::holds_alternative<ahfl::runtime::NodeCompleted>(
                    event.payload)) {
                ++node_completed_events;
            } else if (std::holds_alternative<ahfl::runtime::NodeFailed>(
                           event.payload)) {
                ++node_failed_events;
            } else if (std::holds_alternative<ahfl::runtime::NodeSuspended>(
                           event.payload)) {
                ++node_suspended_events;
            }
        }
        check(node_completed_events == 0,
              "wh5b2tag0.zero_completion_events");
        check(node_failed_events == 0, "wh5b2tag0.zero_failed_events");
        check(node_suspended_events == 1,
              "wh5b2tag0.one_suspended_event");
    }

    // Load the persisted snapshot and verify the suspended node is NOT in
    // completed_nodes (no tag-0 completion record was written for it).
    {
        WorkflowRecoveryStore store(snapshot_path);
        auto loaded = store.load();
        check(loaded.has_value(), "wh5b2tag0.snapshot_loaded");
        if (!loaded.has_value()) {
            return;
        }
        check(loaded->suspended.has_value(), "wh5b2tag0.snapshot_has_suspended");
        if (!loaded->suspended.has_value()) {
            return;
        }
        const auto suspended_node = loaded->suspended->node;
        for (const auto &completed : loaded->completed_nodes) {
            check(completed.node != suspended_node,
                  "wh5b2tag0.suspended_node_not_completed");
        }
    }
}

// ==== 1c-ac10. WH-5b.2 AC10: two sequential bridge calls determinism =====
//
// The wh5b2_two_bridge_calls fixture has ONE P6 agent with TWO sequential
// in-handler bridge calls: ordinal 0 (CallA) returns ok and is memoized;
// ordinal 1 (CallB) returns Pending and suspends. On resume (fresh-instance
// replay), ordinal 0 is a MemoHit (injected from the recorded memo) and
// ordinal 1 is the Frontier (injected with the host-supplied result). The
// workflow completes with zero live Bridge calls.
//
// This proves the codegen static ordinal (dense index in
// plan.workflow_bridge_sites, handler-planning/ANF order) equals the dynamic
// recorder ordinal (per_node_counters_ in execution order) for sequential
// calls: if they diverged, the memo replay cross-check would fail and the
// resume would not complete.
void test_wh5b2_ac10_two_bridge_calls(
    const std::filesystem::path &repo_root,
    const std::filesystem::path &work_dir) {
    const auto source =
        repo_root / "tests/golden/wasm/wh5b2_two_bridge_calls.ahfl";
    std::string error;
    auto program = conf::compile_conformance_source(source, error);
    check(program.has_value(), "ac10.compile");
    if (!program.has_value()) {
        std::cerr << "  compile failed: " << error << "\n";
        return;
    }

    const auto snapshot_path = work_dir / "ac10-snapshot.json";
    std::error_code ec;
    std::filesystem::remove(snapshot_path, ec);

    static constexpr std::string_view kWorkflow =
        "wasm::wh5b2_two_bridge_calls::TwoBridgeCalls";
    static constexpr std::string_view kFrameJson =
        R"({"_type":"wasm::wh5b2_two_bridge_calls::Frame","n":42})";

    // ---- Process A: ordinal 0 Success (memoized), ordinal 1 Pending. ----
    auto state_a = std::make_shared<InvokerState>();
    state_a->bridge_success_then_pending = true;
    state_a->bridge_frame_type = "wasm::wh5b2_two_bridge_calls::Frame";
    {
        WorkflowRecoveryStore store(snapshot_path);
        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = make_counting_invoker(state_a);
        config.recovery_store = &store;

        wr::WasmWorkflowRuntime runtime(*program, std::move(config));
        auto input = value_from_json(std::string(kFrameJson));
        check(input.has_value(), "ac10.input");
        if (!input.has_value()) {
            return;
        }

        auto suspended = runtime.run(std::string(kWorkflow), std::move(*input));

        check(suspended.status() == WorkflowStatus::Suspended, "ac10.suspended");
        check(!suspended.has_errors(), "ac10.no_errors");
        // Two live Bridge calls: ordinal 0 (Success) and ordinal 1 (Pending).
        check(state_a->bridge_calls == 2, "ac10.two_bridge_calls");
        check(suspended.suspended.has_value(), "ac10.has_resume_record");
        if (suspended.suspended.has_value() &&
            suspended.suspended->suspended.has_value()) {
            const auto &node_state = *suspended.suspended->suspended;
            // The pending frontier is ordinal 1 (the second call).
            check(node_state.pending_ordinal == 1, "ac10.pending_ordinal_1");
            // The memo has exactly one entry: ordinal 0 (CallA's result).
            check(node_state.memo.size() == 1, "ac10.memo_size_1");
            if (!node_state.memo.empty()) {
                check(node_state.memo[0].ordinal == 0, "ac10.memo_ordinal_0");
            }
        }
    }

    // ---- Process B: cold start. Ordinal 0 is MemoHit, ordinal 1 is
    // Frontier. Zero live Bridge calls. The workflow completes. ----
    auto state_b = std::make_shared<InvokerState>();
    state_b->bridge_pending = false; // Success if called live (should not be)
    state_b->bridge_frame_type = "wasm::wh5b2_two_bridge_calls::Frame";
    {
        WorkflowRecoveryStore store(snapshot_path);
        auto loaded = store.load();
        check(loaded.has_value(), "ac10.snapshot_loaded");
        if (!loaded.has_value()) {
            return;
        }

        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = make_counting_invoker(state_b);
        config.recovery_snapshot = std::move(*loaded);
        config.resume_pending_result_wire_json = std::string(kFrameJson);

        wr::WasmWorkflowRuntime runtime(*program, std::move(config));
        auto input = value_from_json(std::string(kFrameJson));
        check(input.has_value(), "ac10.resume_input");
        if (!input.has_value()) {
            return;
        }

        auto resumed = runtime.run(std::string(kWorkflow), std::move(*input));

        check(resumed.status() == WorkflowStatus::Completed, "ac10.completed");
        check(!resumed.has_errors(), "ac10.resume_no_errors");
        // Zero live Bridge calls: ordinal 0 memo-hit, ordinal 1 frontier.
        check(state_b->bridge_calls == 0, "ac10.zero_live_calls");
        check_workflow_completed(resumed, 1, "ac10.complete");

        const auto *output = resumed.output();
        check(output != nullptr, "ac10.has_output");
        if (output != nullptr) {
            const auto json = ahfl::runtime::value_to_json(*output);
            check(json == std::string(kFrameJson), "ac10.output_value");
        }
    }
}

// ==== 1c-fc1/2. WH-5b.2 §12.14.9 cases 1 and 2: bridge replay divergence ==
//
// Case 1 (ordinal tamper): the memo entry's ordinal is tampered to a wrong
// value. On resume, the host's (node, ordinal) memo lookup fails to match
// the live call -> replay divergence -> fail closed.
//
// Case 2 (arg_hash mismatch): the memo entry's arg_hash is tampered. On
// resume, the host's arg_hash cross-check fails -> replay divergence ->
// fail closed.
namespace {
void ac10_fail_closed(const std::filesystem::path &repo_root,
                      const std::filesystem::path &work_dir,
                      std::string_view tag,
                      std::function<void(WorkflowRecoverySnapshot &)> tamper,
                      std::string_view expect_msg) {
    const auto source =
        repo_root / "tests/golden/wasm/wh5b2_two_bridge_calls.ahfl";
    std::string error;
    auto program = conf::compile_conformance_source(source, error);
    check(program.has_value(), std::string(tag) + ".compile");
    if (!program.has_value()) {
        return;
    }

    const auto snapshot_path =
        work_dir / (std::string(tag) + "-snapshot.json");
    std::error_code ec;
    std::filesystem::remove(snapshot_path, ec);

    static constexpr std::string_view kWorkflow =
        "wasm::wh5b2_two_bridge_calls::TwoBridgeCalls";
    static constexpr std::string_view kFrameJson =
        R"({"_type":"wasm::wh5b2_two_bridge_calls::Frame","n":42})";

    // Suspend at ordinal 1.
    auto state_a = std::make_shared<InvokerState>();
    state_a->bridge_success_then_pending = true;
    state_a->bridge_frame_type = "wasm::wh5b2_two_bridge_calls::Frame";
    {
        WorkflowRecoveryStore store(snapshot_path);
        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = make_counting_invoker(state_a);
        config.recovery_store = &store;

        wr::WasmWorkflowRuntime runtime(*program, std::move(config));
        auto input = value_from_json(std::string(kFrameJson));
        if (!input.has_value()) {
            check(false, std::string(tag) + ".input");
            return;
        }
        auto suspended = runtime.run(std::string(kWorkflow), std::move(*input));
        if (suspended.status() != WorkflowStatus::Suspended) {
            check(false, std::string(tag) + ".suspend");
            return;
        }
    }

    // Load + tamper + resume.
    {
        WorkflowRecoveryStore store(snapshot_path);
        auto loaded = store.load();
        check(loaded.has_value(), std::string(tag) + ".load");
        if (!loaded.has_value()) {
            return;
        }
        auto snapshot = std::move(*loaded);
        tamper(snapshot);

        auto state_b = std::make_shared<InvokerState>();
        state_b->bridge_pending = false;
        state_b->bridge_frame_type = "wasm::wh5b2_two_bridge_calls::Frame";
        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = make_counting_invoker(state_b);
        config.recovery_snapshot = std::move(snapshot);
        config.resume_pending_result_wire_json = std::string(kFrameJson);

        wr::WasmWorkflowRuntime runtime(*program, std::move(config));
        auto input = value_from_json(std::string(kFrameJson));
        if (!input.has_value()) {
            check(false, std::string(tag) + ".resume_input");
            return;
        }
        auto resumed = runtime.run(std::string(kWorkflow), std::move(*input));

        check(resumed.status() != WorkflowStatus::Completed,
              std::string(tag) + ".not_completed");
        check(resumed.has_errors(), std::string(tag) + ".has_errors");
        const auto msg = first_error_message(resumed);
        check(!msg.empty(), std::string(tag) + ".actionable_diagnostic");
        if (!expect_msg.empty()) {
            check(msg.find(expect_msg) != std::string::npos,
                  std::string(tag) + ".message_pin");
        }
        // The invoker must never be called: the divergence aborts before
        // any live capability call.
        check(state_b->bridge_calls == 0,
              std::string(tag) + ".zero_live_calls");
    }
}
} // namespace

void test_wh5b2_fc1_ordinal_tamper(
    const std::filesystem::path &repo_root,
    const std::filesystem::path &work_dir) {
    ac10_fail_closed(
        repo_root, work_dir, "fc1",
        [](WorkflowRecoverySnapshot &snap) {
            // Tamper the memo entry's ordinal from 0 to 5. The load-time
            // validation rejects a memo ordinal that is not below the
            // pending ordinal (1), so the snapshot is refused before the
            // replay classifier runs.
            if (!snap.suspended->memo.empty()) {
                snap.suspended->memo[0].ordinal = 5;
            }
        },
        "memo ordinal is not below the pending ordinal");
}

void test_wh5b2_fc2_arg_hash_mismatch(
    const std::filesystem::path &repo_root,
    const std::filesystem::path &work_dir) {
    ac10_fail_closed(
        repo_root, work_dir, "fc2",
        [](WorkflowRecoverySnapshot &snap) {
            // Tamper the memo entry's arg_hash. The live call's arg_hash
            // will not match.
            if (!snap.suspended->memo.empty()) {
                snap.suspended->memo[0].arg_hash = 999;
            }
        },
        "durable resume replay diverged");
}

// ==== 2. e4 memo round-trip (suspend -> persist -> resume) ====
//
// Enhanced with P2-7 (workflow_completed assertions) and P2-8 (direct
// memo tuple assertions).

void test_e4_memo_round_trip(const std::filesystem::path &repo_root,
                             const std::filesystem::path &work_dir) {
    const auto source =
        repo_root / "tests/golden/wasm/e4_capability_workflow_resume_memo.ahfl";
    std::string error;
    auto program = conf::compile_conformance_source(source, error);
    check(program.has_value(), "e4.compile");
    if (!program.has_value()) {
        std::cerr << "  compile failed: " << error << "\n";
        return;
    }

    const auto snapshot_path = work_dir / "e4-snapshot.json";
    std::error_code ec;
    std::filesystem::remove(snapshot_path, ec);

    // ---- Process A: run until B returns PENDING, suspend, persist. ----
    auto state_a = std::make_shared<InvokerState>();
    WorkflowRecoverySnapshot snapshot_for_assert;
    {
        WorkflowRecoveryStore store(snapshot_path);
        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = make_counting_invoker(state_a);
        config.recovery_store = &store;

        wr::WasmWorkflowRuntime runtime(*program, std::move(config));

        auto input = value_from_json(
            R"({"_type":"wasm::e4_capability_workflow_resume_memo::Frame","value":"memo-test"})");
        check(input.has_value(), "e4.input");
        if (!input.has_value()) {
            return;
        }

        auto suspended = runtime.run(
            "wasm::e4_capability_workflow_resume_memo::MemoPipeline",
            std::move(*input));

        check(suspended.status() == WorkflowStatus::Suspended,
              "e4.process_a_suspended");
        check(!suspended.has_errors(), "e4.process_a_no_errors");
        check(state_a->a_calls == 1, "e4.process_a_a_called_once");
        check(state_a->b_calls == 1, "e4.process_a_b_called_once");
        check(suspended.suspended.has_value(),
              "e4.process_a_has_resume_record");
        // P2-7: no WorkflowCompleted event on suspend.
        check_workflow_completed(suspended, 0, "e4.suspend");

        if (suspended.suspended.has_value()) {
            // WorkflowRecoverySnapshot is move-only (Value holds
            // unique_ptr-backed alternatives); move for the P2-8 assertions.
            snapshot_for_assert = std::move(*suspended.suspended);
        }
    }

    // P2-8: directly assert the e4 snapshot memo tuple contents.
    {
        check(snapshot_for_assert.suspended.has_value(),
              "e4.tuple.has_suspended");
        if (snapshot_for_assert.suspended.has_value()) {
            const auto &susp = *snapshot_for_assert.suspended;
            check(susp.memo.size() == 1, "e4.tuple.memo_size_1");
            if (susp.memo.size() == 1) {
                const auto &entry = susp.memo[0];
                // Node coordinate: the memo entry is for `first` (node_id 0).
                check(entry.node.has_value(), "e4.tuple.has_node");
                if (entry.node.has_value()) {
                    check(entry.node->index() == 0, "e4.tuple.node_is_first");
                }
                // Ordinal 0 (the first call on `first`).
                check(entry.ordinal == 0, "e4.tuple.ordinal_0");
                // Cap A identity: the cap_id matches A's source_symbol
                // (captured by the invoker during origination).
                check(state_a->a_symbol_id.has_value(),
                      "e4.tuple.a_symbol_captured");
                if (state_a->a_symbol_id.has_value()) {
                    check(entry.cap_id == *state_a->a_symbol_id,
                          "e4.tuple.cap_id_is_A");
                }
                // arg_hash is set (non-zero for a non-empty arg vector).
                check(entry.arg_hash != 0, "e4.tuple.arg_hash_nonzero");
                // ExactSidecar source tag (the wasm lane always sets it).
                check(entry.source ==
                          ahfl::runtime::PersistedMemoResultSource::ExactSidecar,
                      "e4.tuple.exact_sidecar");
                // The authoritative wire JSON is present and non-empty.
                check(entry.authoritative_json.has_value(),
                      "e4.tuple.has_authoritative_json");
                if (entry.authoritative_json.has_value()) {
                    check(!entry.authoritative_json->empty(),
                          "e4.tuple.authoritative_json_nonempty");
                }
                // The result presence bit is set.
                check(entry.result_present.has_value(),
                      "e4.tuple.has_result_present");
                if (entry.result_present.has_value()) {
                    check(*entry.result_present,
                          "e4.tuple.result_present_true");
                }
            }
        }
    }

    check(std::filesystem::exists(snapshot_path),
          "e4.snapshot_file_on_disk");

    // ---- Process B: cold start. Fresh facade, on-disk snapshot is the
    // only link. A is memo-supplied (zero live calls); B is
    // frontier-injected (zero live calls). ----
    auto state_b = std::make_shared<InvokerState>();
    state_b->b_pending = false; // B would return Success if called (it must NOT be)
    {
        WorkflowRecoveryStore store(snapshot_path);
        auto loaded = store.load();
        check(loaded.has_value(), "e4.snapshot_loaded_from_disk");
        if (!loaded.has_value()) {
            return;
        }

        // The pending result for B: the echoed input.
        auto pending_result = value_from_json(
            R"({"_type":"wasm::e4_capability_workflow_resume_memo::Frame","value":"memo-test"})");
        check(pending_result.has_value(), "e4.pending_result");
        if (!pending_result.has_value()) {
            return;
        }

        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = make_counting_invoker(state_b);
        config.recovery_snapshot = std::move(*loaded);
        config.resume_pending_result = std::move(*pending_result);

        wr::WasmWorkflowRuntime runtime(*program, std::move(config));

        auto input = value_from_json(
            R"({"_type":"wasm::e4_capability_workflow_resume_memo::Frame","value":"memo-test"})");
        check(input.has_value(), "e4.resume_input");
        if (!input.has_value()) {
            return;
        }

        auto resumed = runtime.run(
            "wasm::e4_capability_workflow_resume_memo::MemoPipeline",
            std::move(*input));

        check(resumed.status() == WorkflowStatus::Completed,
              "e4.process_b_completed");
        check(!resumed.has_errors(), "e4.process_b_no_errors");
        // Zero live side effects on resume: A served from memo, B injected.
        check(state_b->a_calls == 0, "e4.process_b_a_zero_live_calls");
        check(state_b->b_calls == 0, "e4.process_b_b_zero_live_calls");
        // P2-7: exactly one WorkflowCompleted event on completion.
        check_workflow_completed(resumed, 1, "e4.complete");

        const auto *output = resumed.output();
        check(output != nullptr, "e4.process_b_has_output");
        if (output != nullptr) {
            const auto json = ahfl::runtime::value_to_json(*output);
            check(json ==
                      R"({"_type":"wasm::e4_capability_workflow_resume_memo::Frame","value":"memo-test"})",
                  "e4.process_b_output_value");
        }
    }
}

// ==== 3. Fail-closed: resume with NO pending result ====

void test_fail_closed_missing_pending_result(
    const std::filesystem::path &repo_root,
    const std::filesystem::path &work_dir) {
    const auto source =
        repo_root / "tests/golden/wasm/e4_capability_workflow_resume_memo.ahfl";
    std::string error;
    auto program = conf::compile_conformance_source(source, error);
    check(program.has_value(), "fc.compile");
    if (!program.has_value()) {
        return;
    }

    const auto snapshot_path = work_dir / "e4-fc-snapshot.json";
    std::error_code ec;
    std::filesystem::remove(snapshot_path, ec);

    // Suspend first (same as Process A above).
    {
        WorkflowRecoveryStore store(snapshot_path);
        auto state = std::make_shared<InvokerState>();
        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = make_counting_invoker(state);
        config.recovery_store = &store;

        wr::WasmWorkflowRuntime runtime(*program, std::move(config));
        auto input = value_from_json(
            R"({"_type":"wasm::e4_capability_workflow_resume_memo::Frame","value":"fc"})");
        if (!input.has_value()) {
            return;
        }
        auto suspended = runtime.run(
            "wasm::e4_capability_workflow_resume_memo::MemoPipeline",
            std::move(*input));
        check(suspended.status() == WorkflowStatus::Suspended,
              "fc.suspended");
    }

    // Resume with NO pending result: must fail closed.
    {
        WorkflowRecoveryStore store(snapshot_path);
        auto loaded = store.load();
        check(loaded.has_value(), "fc.snapshot_loaded");
        if (!loaded.has_value()) {
            return;
        }

        auto state = std::make_shared<InvokerState>();
        state->b_pending = false;
        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = make_counting_invoker(state);
        config.recovery_snapshot = std::move(*loaded);
        // Deliberately NO resume_pending_result.

        wr::WasmWorkflowRuntime runtime(*program, std::move(config));
        auto input = value_from_json(
            R"({"_type":"wasm::e4_capability_workflow_resume_memo::Frame","value":"fc"})");
        if (!input.has_value()) {
            return;
        }
        auto resumed = runtime.run(
            "wasm::e4_capability_workflow_resume_memo::MemoPipeline",
            std::move(*input));

        // The session fails closed: the frontier is reached but no
        // injection result is supplied, so the run fails (never silently
        // runs fresh, never completes).
        check(resumed.status() != WorkflowStatus::Completed,
              "fc.missing_pending_result_fails_closed");
        check(resumed.status() == WorkflowStatus::NodeFailed,
              "fc.missing_pending_result_node_failed");
        check(resumed.has_errors(), "fc.missing_pending_result_has_errors");
        const auto fc_msg = first_error_message(resumed);
        check(!fc_msg.empty(), "fc.missing_pending_result_actionable");
        check(fc_msg.find("durable resume is missing the pending "
                          "capability result") != std::string::npos,
              "fc.missing_pending_result_message_pin");
        check(state->a_calls == 0 && state->b_calls == 0,
              "fc.missing_pending_result_zero_live");
    }
}

// ==== 4. Intent alignment: durable_write_intent_sink fires on
//         origination, ZERO on resume ====

void test_intent_alignment(const std::filesystem::path &repo_root,
                           const std::filesystem::path &work_dir) {
    // Use the e4 fixture: capability B is durable_write. The intent sink
    // must fire once for B on origination (before dispatch), and ZERO
    // times on resume (A is memo-supplied, B is frontier-injected; neither
    // reaches the invoker).
    const auto source =
        repo_root / "tests/golden/wasm/e4_capability_workflow_resume_memo.ahfl";
    std::string error;
    auto program = conf::compile_conformance_source(source, error);
    check(program.has_value(), "intent.compile");
    if (!program.has_value()) {
        std::cerr << "  compile failed: " << error << "\n";
        return;
    }

    const auto snapshot_path = work_dir / "intent-snapshot.json";
    std::error_code ec;
    std::filesystem::remove(snapshot_path, ec);

    // Process A: B returns PENDING. The intent sink must fire once for B
    // (before dispatch). A is not durable_write, so no intent for A.
    int intent_count_a = 0;
    std::vector<std::uint64_t> intent_keys_a;
    {
        WorkflowRecoveryStore store(snapshot_path);
        auto state = std::make_shared<InvokerState>();
        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = make_counting_invoker(state);
        config.recovery_store = &store;
        config.durable_write_intent_sink =
            [&intent_count_a, &intent_keys_a](std::uint64_t key,
                                              std::string_view) {
                ++intent_count_a;
                intent_keys_a.push_back(key);
            };

        wr::WasmWorkflowRuntime runtime(*program, std::move(config));
        auto input = value_from_json(
            R"({"_type":"wasm::e4_capability_workflow_resume_memo::Frame","value":"intent"})");
        check(input.has_value(), "intent.input");
        if (!input.has_value()) {
            return;
        }
        auto suspended = runtime.run(
            "wasm::e4_capability_workflow_resume_memo::MemoPipeline",
            std::move(*input));
        check(suspended.status() == WorkflowStatus::Suspended,
              "intent.suspended");
        check(!suspended.has_errors(), "intent.no_errors");
        check(state->a_calls == 1, "intent.a_called_once");
        check(state->b_calls == 1, "intent.b_called_once");
        // One intent: B (durable_write). A is not durable_write.
        check(intent_count_a == 1, "intent.process_a_one_intent");
        check(!intent_keys_a.empty() && intent_keys_a[0] != 0,
              "intent.process_a_nonzero_key");
    }

    // Process B: resume with the pending result. The intent sink must
    // fire ZERO times (memo hit for A, frontier injection for B; neither
    // reaches the invoker, so the intent-emitting wrapper never fires).
    int intent_count_b = 0;
    {
        WorkflowRecoveryStore store(snapshot_path);
        auto loaded = store.load();
        check(loaded.has_value(), "intent.snapshot_loaded");
        if (!loaded.has_value()) {
            return;
        }

        auto pending_result = value_from_json(
            R"({"_type":"wasm::e4_capability_workflow_resume_memo::Frame","value":"intent"})");
        check(pending_result.has_value(), "intent.pending_result");
        if (!pending_result.has_value()) {
            return;
        }

        auto state = std::make_shared<InvokerState>();
        state->b_pending = false; // B would return Success if called (it must NOT be)
        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = make_counting_invoker(state);
        config.recovery_snapshot = std::move(*loaded);
        config.resume_pending_result = std::move(*pending_result);
        config.durable_write_intent_sink =
            [&intent_count_b](std::uint64_t, std::string_view) {
                ++intent_count_b;
            };

        wr::WasmWorkflowRuntime runtime(*program, std::move(config));
        auto input = value_from_json(
            R"({"_type":"wasm::e4_capability_workflow_resume_memo::Frame","value":"intent"})");
        check(input.has_value(), "intent.resume_input");
        if (!input.has_value()) {
            return;
        }
        auto resumed = runtime.run(
            "wasm::e4_capability_workflow_resume_memo::MemoPipeline",
            std::move(*input));
        check(resumed.status() == WorkflowStatus::Completed,
              "intent.resumed_completed");
        check(!resumed.has_errors(), "intent.resume_no_errors");
        check(state->a_calls == 0, "intent.resume_a_zero_live");
        check(state->b_calls == 0, "intent.resume_b_zero_live");
        check(intent_count_b == 0, "intent.process_b_zero_intents");
    }
}

// ==== 5. AC3 fail-closed cases (P1-3) ====
//
// Shared helper: compile e4, suspend, load snapshot, tamper, resume,
// assert FAILED + actionable diagnostic + zero live calls.

using TamperFn =
    std::function<void(WorkflowRecoverySnapshot &, const InvokerState &)>;

void ac3_e4_fail_closed(const std::filesystem::path &repo_root,
                        const std::filesystem::path &work_dir,
                        std::string_view tag, TamperFn tamper,
                        std::optional<std::string> wire_json = std::nullopt,
                        std::optional<std::string> expect_msg = std::nullopt) {
    const auto source =
        repo_root / "tests/golden/wasm/e4_capability_workflow_resume_memo.ahfl";
    std::string error;
    auto program = conf::compile_conformance_source(source, error);
    check(program.has_value(), std::string(tag) + ".compile");
    if (!program.has_value()) {
        std::cerr << "  compile failed: " << error << "\n";
        return;
    }

    const auto snapshot_path =
        work_dir / (std::string(tag) + "-snapshot.json");
    std::error_code ec;
    std::filesystem::remove(snapshot_path, ec);

    // Suspend.
    auto state_a = std::make_shared<InvokerState>();
    {
        WorkflowRecoveryStore store(snapshot_path);
        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = make_counting_invoker(state_a);
        config.recovery_store = &store;

        wr::WasmWorkflowRuntime runtime(*program, std::move(config));
        auto input = value_from_json(
            R"({"_type":"wasm::e4_capability_workflow_resume_memo::Frame","value":"ac3"})");
        if (!input.has_value()) {
            check(false, std::string(tag) + ".input");
            return;
        }
        auto suspended = runtime.run(
            "wasm::e4_capability_workflow_resume_memo::MemoPipeline",
            std::move(*input));
        if (suspended.status() != WorkflowStatus::Suspended) {
            check(false, std::string(tag) + ".suspend");
            return;
        }
    }

    // Load + tamper + resume.
    {
        WorkflowRecoveryStore store(snapshot_path);
        auto loaded = store.load();
        check(loaded.has_value(), std::string(tag) + ".load");
        if (!loaded.has_value()) {
            return;
        }
        auto snapshot = std::move(*loaded);
        tamper(snapshot, *state_a);

        auto state_b = std::make_shared<InvokerState>();
        state_b->b_pending = false;
        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = make_counting_invoker(state_b);
        config.recovery_snapshot = std::move(snapshot);
        if (wire_json.has_value()) {
            config.resume_pending_result_wire_json = std::move(*wire_json);
        }

        wr::WasmWorkflowRuntime runtime(*program, std::move(config));
        auto input = value_from_json(
            R"({"_type":"wasm::e4_capability_workflow_resume_memo::Frame","value":"ac3"})");
        if (!input.has_value()) {
            check(false, std::string(tag) + ".resume_input");
            return;
        }
        auto resumed = runtime.run(
            "wasm::e4_capability_workflow_resume_memo::MemoPipeline",
            std::move(*input));

        check(resumed.status() != WorkflowStatus::Completed,
              std::string(tag) + ".not_completed");
        check(resumed.status() == WorkflowStatus::NodeFailed,
              std::string(tag) + ".node_failed");
        check(resumed.has_errors(), std::string(tag) + ".has_errors");
        const auto msg = first_error_message(resumed);
        check(!msg.empty(), std::string(tag) + ".actionable_diagnostic");
        if (expect_msg.has_value()) {
            check(msg.find(*expect_msg) != std::string::npos,
                  std::string(tag) + ".message_pin");
        }
        // The invoker must never be called: every fail-closed path aborts
        // before the live invoker is reached.
        check(state_b->a_calls == 0 && state_b->b_calls == 0,
              std::string(tag) + ".zero_live_calls");
    }
}

// AC3 case 1: corrupt snapshot JSON. The store load must fail (the
// corrupt JSON is rejected before reaching the facade).
void test_ac3_corrupt_snapshot_json(
    const std::filesystem::path &repo_root,
    const std::filesystem::path &work_dir) {
    const auto source =
        repo_root / "tests/golden/wasm/e4_capability_workflow_resume_memo.ahfl";
    std::string error;
    auto program = conf::compile_conformance_source(source, error);
    check(program.has_value(), "ac3x1.compile");
    if (!program.has_value()) {
        return;
    }

    const auto snapshot_path = work_dir / "ac3x1-snapshot.json";
    std::error_code ec;
    std::filesystem::remove(snapshot_path, ec);

    // Suspend (produce a real snapshot).
    {
        WorkflowRecoveryStore store(snapshot_path);
        auto state = std::make_shared<InvokerState>();
        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = make_counting_invoker(state);
        config.recovery_store = &store;

        wr::WasmWorkflowRuntime runtime(*program, std::move(config));
        auto input = value_from_json(
            R"({"_type":"wasm::e4_capability_workflow_resume_memo::Frame","value":"x1"})");
        if (!input.has_value()) {
            return;
        }
        auto suspended = runtime.run(
            "wasm::e4_capability_workflow_resume_memo::MemoPipeline",
            std::move(*input));
        check(suspended.status() == WorkflowStatus::Suspended,
              "ac3x1.suspended");
    }

    // Corrupt the snapshot file with invalid JSON.
    {
        std::ofstream out(snapshot_path, std::ios::trunc);
        out << "{corrupt json";
    }

    // The store load must fail (InvalidSnapshot). The facade never runs.
    {
        WorkflowRecoveryStore store(snapshot_path);
        auto loaded = store.load();
        check(!loaded.has_value(), "ac3x1.load_fails");
    }
}

// AC3 case 2: workflow-id mismatch. The snapshot's workflow id does not
// match the descriptor's workflow index -> validation fail-closed.
void test_ac3_workflow_id_mismatch(
    const std::filesystem::path &repo_root,
    const std::filesystem::path &work_dir) {
    ac3_e4_fail_closed(
        repo_root, work_dir, "ac3x2",
        [](WorkflowRecoverySnapshot &snap, const InvokerState &) {
            snap.workflow = ahfl::runtime::WorkflowId{999};
        },
        std::nullopt,
        "recovery snapshot workflow ID does not match the selected workflow");
}

// AC3 case 3: suspended node not in descriptor. The snapshot's suspended
// node id is unknown to the module -> validation fail-closed.
void test_ac3_suspended_node_unknown(
    const std::filesystem::path &repo_root,
    const std::filesystem::path &work_dir) {
    ac3_e4_fail_closed(
        repo_root, work_dir, "ac3x3",
        [](WorkflowRecoverySnapshot &snap, const InvokerState &) {
            snap.suspended->node = ahfl::runtime::WorkflowNodeId{999};
        },
        std::nullopt,
        "recovery snapshot suspended node ID is unknown to this module");
}

// AC3 case 4: memo cap_id mismatch. The memo entry's cap_id does not
// match the live import's cap_id -> replay divergence.
void test_ac3_memo_cap_id_mismatch(
    const std::filesystem::path &repo_root,
    const std::filesystem::path &work_dir) {
    ac3_e4_fail_closed(
        repo_root, work_dir, "ac3x4",
        [](WorkflowRecoverySnapshot &snap, const InvokerState &) {
            snap.suspended->memo[0].cap_id = 999;
        },
        std::nullopt,
        "durable resume replay diverged from the recorded memo");
}

// AC3 case 5: memo arg_hash mismatch. The memo entry's arg_hash does not
// match the live import's arg_hash -> replay divergence.
void test_ac3_memo_arg_hash_mismatch(
    const std::filesystem::path &repo_root,
    const std::filesystem::path &work_dir) {
    ac3_e4_fail_closed(
        repo_root, work_dir, "ac3x5",
        [](WorkflowRecoverySnapshot &snap, const InvokerState &) {
            snap.suspended->memo[0].arg_hash = 999;
        },
        std::nullopt,
        "durable resume replay diverged from the recorded memo");
}

// AC3 case 7 (P1-5b): wrong-type wire JSON. The injected wire JSON is
// valid JSON but has the wrong type for the pending capability's result
// binding -> frontier type gate fail-closed.
void test_ac3_wire_json_wrong_type(
    const std::filesystem::path &repo_root,
    const std::filesystem::path &work_dir) {
    // A bare JSON string is not a Frame struct -> the decode under B's
    // result binding fails.
    ac3_e4_fail_closed(
        repo_root, work_dir, "ac3x7",
        [](WorkflowRecoverySnapshot &, const InvokerState &) {
            // No snapshot tamper: the failure is the wrong-type wire JSON.
        },
        std::string("\"not-a-frame\""),
        "durable resume pending-result type mismatch");
}

// AC3 case 8: pending identity mismatch. The pending cap_id is resolvable
// (P2-5 passes) but does not match the live import's cap_id at the
// frontier -> frontier identity gate fail-closed.
void test_ac3_pending_identity_mismatch(
    const std::filesystem::path &repo_root,
    const std::filesystem::path &work_dir) {
    ac3_e4_fail_closed(
        repo_root, work_dir, "ac3x8",
        [](WorkflowRecoverySnapshot &snap, const InvokerState &state) {
            // Tamper the pending cap_id to A's SymbolId (resolvable, so
            // P2-5 passes, but != B's SymbolId at the frontier).
            if (state.a_symbol_id.has_value()) {
                snap.suspended->pending_cap_id = *state.a_symbol_id;
            } else {
                snap.suspended->pending_cap_id = 999;
            }
        },
        std::nullopt,
        "durable resume pending-call identity mismatch");
}

// AC3 case 9: replay divergence. The suspended node is tampered to the
// identity node `second` (no capability). On resume, `first`'s Echo call
// is before the frontier but the memo is empty -> PreFrontierMiss ->
// divergence fail-closed. The invoker is never called.
//
// Uses the e3 fixture (one capability on `first`, identity `second`).
void test_ac3_replay_divergence(
    const std::filesystem::path &repo_root,
    const std::filesystem::path &work_dir) {
    const auto source =
        repo_root / "tests/golden/wasm/e3_capability_workflow_resume.ahfl";
    std::string error;
    auto program = conf::compile_conformance_source(source, error);
    check(program.has_value(), "ac3x9.compile");
    if (!program.has_value()) {
        std::cerr << "  compile failed: " << error << "\n";
        return;
    }

    const auto snapshot_path = work_dir / "ac3x9-snapshot.json";
    std::error_code ec;
    std::filesystem::remove(snapshot_path, ec);

    static constexpr std::string_view kWorkflow =
        "wasm::e3_capability_workflow_resume::CapabilityPipeline";
    static constexpr std::string_view kFrameJson =
        R"({"_type":"wasm::e3_capability_workflow_resume::Frame","value":"divergence"})";

    // Suspend: Echo returns Pending on `first`.
    {
        WorkflowRecoveryStore store(snapshot_path);
        auto state = std::make_shared<InvokerState>();
        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = make_counting_invoker(state);
        config.recovery_store = &store;
        config.name_resolver = [](std::uint64_t) -> std::optional<std::string> {
            return "Echo";
        };

        wr::WasmWorkflowRuntime runtime(*program, std::move(config));
        auto input = value_from_json(std::string(kFrameJson));
        if (!input.has_value()) {
            return;
        }
        auto suspended = runtime.run(std::string(kWorkflow), std::move(*input));
        check(suspended.status() == WorkflowStatus::Suspended,
              "ac3x9.suspended");
    }

    // Load + tamper + resume.
    {
        WorkflowRecoveryStore store(snapshot_path);
        auto loaded = store.load();
        check(loaded.has_value(), "ac3x9.load");
        if (!loaded.has_value()) {
            return;
        }
        auto snapshot = std::move(*loaded);
        // Tamper the suspended node to `second` (node_id=1, the identity
        // node with no capability). Also fix the agent so the load-time
        // agent-match check passes (SecondAgent is runner 1).
        snapshot.suspended->node = ahfl::runtime::WorkflowNodeId{1};
        snapshot.suspended->agent = ahfl::runtime::AgentId{1};

        auto state = std::make_shared<InvokerState>();
        state->echo_pending = false;
        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = make_counting_invoker(state);
        config.recovery_snapshot = std::move(snapshot);
        config.resume_pending_result_wire_json = std::string(kFrameJson);
        config.name_resolver = [](std::uint64_t) -> std::optional<std::string> {
            return "Echo";
        };

        wr::WasmWorkflowRuntime runtime(*program, std::move(config));
        auto input = value_from_json(std::string(kFrameJson));
        if (!input.has_value()) {
            return;
        }
        auto resumed = runtime.run(std::string(kWorkflow), std::move(*input));

        // The resume diverges: `first`'s Echo call is before the frontier
        // (schedule_pos 0 < frontier schedule_pos 1) but the memo is empty
        // -> PreFrontierMiss -> fail-closed.
        check(resumed.status() != WorkflowStatus::Completed,
              "ac3x9.not_completed");
        check(resumed.status() == WorkflowStatus::NodeFailed,
              "ac3x9.node_failed");
        check(resumed.has_errors(), "ac3x9.has_errors");
        const auto msg = first_error_message(resumed);
        check(!msg.empty(), "ac3x9.actionable_diagnostic");
        check(msg.find("durable resume replay diverged: pre-frontier "
                       "import not in memo") != std::string::npos,
              "ac3x9.message_pin");
        // The invoker is never called: the PreFrontierMiss aborts before
        // the live invoker is reached.
        check(state->echo_calls == 0, "ac3x9.zero_live_calls");
    }
}

// ==== 6. Non-topological resume (P1-2) ====
//
// The e5 fixture declares `second` BEFORE `first`, but `second` depends
// on `first`. The Kahn schedule is [first, second] != declaration order
// [second, first]. On resume, `first`'s A call is at schedule_pos 0
// (before the frontier at schedule_pos 1) -> MemoHit. A source-order
// comparison would see first (node_id=1) > second (node_id=0) and
// misclassify it as PostFrontier (live), which the zero-live-calls
// assertion catches.

void test_non_topological_resume(
    const std::filesystem::path &repo_root,
    const std::filesystem::path &work_dir) {
    const auto source =
        repo_root / "tests/golden/wasm/e5_non_topological_resume.ahfl";
    std::string error;
    auto program = conf::compile_conformance_source(source, error);
    check(program.has_value(), "e5.compile");
    if (!program.has_value()) {
        std::cerr << "  compile failed: " << error << "\n";
        return;
    }

    const auto snapshot_path = work_dir / "e5-snapshot.json";
    std::error_code ec;
    std::filesystem::remove(snapshot_path, ec);

    static constexpr std::string_view kWorkflow =
        "wasm::e5_non_topological_resume::NonTopological";
    static constexpr std::string_view kFrameJson =
        R"({"_type":"wasm::e5_non_topological_resume::Frame","value":"non-topo"})";

    // ---- Process A: A returns OK (memoized), B returns PENDING. ----
    auto state_a = std::make_shared<InvokerState>();
    {
        WorkflowRecoveryStore store(snapshot_path);
        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = make_counting_invoker(state_a);
        config.recovery_store = &store;

        wr::WasmWorkflowRuntime runtime(*program, std::move(config));

        auto input = value_from_json(std::string(kFrameJson));
        check(input.has_value(), "e5.input");
        if (!input.has_value()) {
            return;
        }

        auto suspended = runtime.run(std::string(kWorkflow), std::move(*input));

        check(suspended.status() == WorkflowStatus::Suspended,
              "e5.suspended");
        check(!suspended.has_errors(), "e5.no_errors");
        check(state_a->a_calls == 1, "e5.a_called_once");
        check(state_a->b_calls == 1, "e5.b_called_once");
        check(suspended.suspended.has_value(), "e5.has_resume_record");
        check_workflow_completed(suspended, 0, "e5.suspend");
    }

    check(std::filesystem::exists(snapshot_path),
          "e5.snapshot_file_on_disk");

    // ---- Process B: cold start. A is memo-supplied (zero live calls);
    // B is frontier-injected (zero live calls). ----
    auto state_b = std::make_shared<InvokerState>();
    state_b->b_pending = false;
    {
        WorkflowRecoveryStore store(snapshot_path);
        auto loaded = store.load();
        check(loaded.has_value(), "e5.snapshot_loaded");
        if (!loaded.has_value()) {
            return;
        }

        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = make_counting_invoker(state_b);
        config.recovery_snapshot = std::move(*loaded);
        config.resume_pending_result_wire_json = std::string(kFrameJson);

        wr::WasmWorkflowRuntime runtime(*program, std::move(config));

        auto input = value_from_json(std::string(kFrameJson));
        check(input.has_value(), "e5.resume_input");
        if (!input.has_value()) {
            return;
        }

        auto resumed = runtime.run(std::string(kWorkflow), std::move(*input));

        check(resumed.status() == WorkflowStatus::Completed,
              "e5.completed");
        check(!resumed.has_errors(), "e5.resume_no_errors");
        // P1-2: A is memo-supplied (schedule_pos 0 < frontier schedule_pos
        // 1), NOT live. A source-order comparison would misclassify it as
        // PostFrontier and call the invoker.
        check(state_b->a_calls == 0, "e5.a_zero_live_calls");
        check(state_b->b_calls == 0, "e5.b_zero_live_calls");
        check_workflow_completed(resumed, 1, "e5.complete");

        const auto *output = resumed.output();
        check(output != nullptr, "e5.has_output");
        if (output != nullptr) {
            const auto json = ahfl::runtime::value_to_json(*output);
            check(json == std::string(kFrameJson), "e5.output_value");
        }
    }
}

// ==== 7. WH-4b P2 fail-closed family ====
//
// P2-A: shared capability, different args, resume identity. Two nodes call
// the SAME capability with DIFFERENT arguments. The memo identity tuple
// (node, ordinal, cap_id, arg_hash) must distinguish them: cap_id-only
// matching must NOT confuse the two nodes' memos.
//
// P2-B: frontier never hit on replay. A fabricated complete memo (including
// a fabricated OK memo entry for the pending call itself) plus a tampered
// frontier ordinal makes the replay complete without hitting the frontier.
// The post-run guard must fail closed ("the pending call was never
// reached").
//
// P2-C: zero-length OK memo. The recording path is provably unreachable on
// a deterministic guest (handle_opaque always serializes to non-empty JSON;
// the guest classifies OK-empty as ERROR). The replay-side defense (empty
// authoritative_json rejected at MemoHit) is exercised here.
//
// P2-D: bogus guest result pointer. The session-level OOB guard is
// defense-in-depth behind the engine's own alloc_then_write bounds check
// (wasm3_engine.cpp), which rejects an out-of-bounds allocation pointer
// before handle_opaque returns it. The session guard is therefore
// unreachable through the production opaque lane and is not tested here;
// see the reachability comment at workflow_session.cpp origination.
//
// P2-E: unresolvable pending cap_id. A pending SymbolId that no node in
// the module calls must fail closed at load time (before any run/invoker).

// --- P2-A: shared-capability resume identity ---

struct SharedCapState {
    int echo_calls{0};
    std::optional<std::size_t> echo_symbol_id;
    std::optional<std::uint64_t> first_arg_hash;
    std::optional<std::uint64_t> second_arg_hash;
};

// Invoker for the e6 shared-capability fixture. Call 1 (node 1) returns
// Success with a transformed frame (so node 2's arg differs); call 2
// (node 2) returns Pending. The arg_hash is captured for both calls so
// the test can assert the memo identity tuple distinguishes them.
ahfl::runtime::ContextualCapabilityInvoker
make_shared_cap_invoker(std::shared_ptr<SharedCapState> state) {
    return [state = std::move(state)](
               const CapabilityInvocationContext &ctx,
               const std::string & /*name*/,
               const std::vector<Value> &args) -> CapabilityCallResult {
        ++state->echo_calls;
        state->echo_symbol_id = ctx.source_capability_symbol_id;
        auto hash = ahfl::runtime::hash_values(args);
        CapabilityCallResult r;
        if (state->echo_calls == 1) {
            // Node 1: Success with a transformed frame (so node 2's arg
            // differs from node 1's).
            state->first_arg_hash = hash;
            r.status = CapabilityCallStatus::Success;
            auto transformed = value_from_json(
                R"({"_type":"wasm::e6_shared_cap_resume::Frame","value":"shared-cap-ok"})");
            if (transformed.has_value()) {
                r.value = std::move(*transformed);
            }
        } else {
            // Node 2: Pending.
            state->second_arg_hash = hash;
            r.status = CapabilityCallStatus::Pending;
        }
        return r;
    };
}

void test_p2a_shared_cap_resume_identity(
    const std::filesystem::path &repo_root,
    const std::filesystem::path &work_dir) {
    const auto source =
        repo_root / "tests/golden/wasm/e6_shared_cap_resume.ahfl";
    std::string error;
    auto program = conf::compile_conformance_source(source, error);
    check(program.has_value(), "p2a.compile");
    if (!program.has_value()) {
        std::cerr << "  compile failed: " << error << "\n";
        return;
    }

    const auto snapshot_path = work_dir / "p2a-snapshot.json";
    std::error_code ec;
    std::filesystem::remove(snapshot_path, ec);

    static constexpr std::string_view kWorkflow =
        "wasm::e6_shared_cap_resume::SharedCapPipeline";
    static constexpr std::string_view kFrameJson =
        R"({"_type":"wasm::e6_shared_cap_resume::Frame","value":"shared-cap"})";
    static constexpr std::string_view kFinalJson =
        R"({"_type":"wasm::e6_shared_cap_resume::Frame","value":"shared-cap-final"})";

    // ---- Process A: node 1 OK (transformed), node 2 Pending. ----
    auto state_a = std::make_shared<SharedCapState>();
    WorkflowRecoverySnapshot snapshot_for_assert;
    {
        WorkflowRecoveryStore store(snapshot_path);
        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = make_shared_cap_invoker(state_a);
        config.recovery_store = &store;

        wr::WasmWorkflowRuntime runtime(*program, std::move(config));

        auto input = value_from_json(std::string(kFrameJson));
        check(input.has_value(), "p2a.input");
        if (!input.has_value()) {
            return;
        }

        auto suspended =
            runtime.run(std::string(kWorkflow), std::move(*input));

        check(suspended.status() == WorkflowStatus::Suspended,
              "p2a.suspended");
        check(!suspended.has_errors(), "p2a.no_errors");
        check(state_a->echo_calls == 2, "p2a.echo_called_twice");
        check(suspended.suspended.has_value(), "p2a.has_resume_record");
        check_workflow_completed(suspended, 0, "p2a.suspend");

        if (suspended.suspended.has_value()) {
            snapshot_for_assert = std::move(*suspended.suspended);
        }
    }

    // Assert the 7-field memo tuple: node 1's OK call with its own
    // arg_hash (distinct from node 2's).
    {
        check(snapshot_for_assert.suspended.has_value(),
              "p2a.tuple.has_suspended");
        if (snapshot_for_assert.suspended.has_value()) {
            const auto &susp = *snapshot_for_assert.suspended;
            check(susp.memo.size() == 1, "p2a.tuple.memo_size_1");
            if (susp.memo.size() == 1) {
                const auto &entry = susp.memo[0];
                // 1. Node coordinate: node 1 (first).
                check(entry.node.has_value(), "p2a.tuple.has_node");
                if (entry.node.has_value()) {
                    check(entry.node->index() == 0,
                          "p2a.tuple.node_is_first");
                }
                // 2. Ordinal 0.
                check(entry.ordinal == 0, "p2a.tuple.ordinal_0");
                // 3. cap_id: Echo's SymbolId.
                check(state_a->echo_symbol_id.has_value(),
                      "p2a.tuple.echo_symbol_captured");
                if (state_a->echo_symbol_id.has_value()) {
                    check(entry.cap_id == *state_a->echo_symbol_id,
                          "p2a.tuple.cap_id_is_echo");
                }
                // 4. arg_hash: node 1's (non-zero, differs from node 2's).
                check(entry.arg_hash != 0, "p2a.tuple.arg_hash_nonzero");
                check(state_a->first_arg_hash.has_value(),
                      "p2a.tuple.first_arg_hash_captured");
                if (state_a->first_arg_hash.has_value()) {
                    check(entry.arg_hash == *state_a->first_arg_hash,
                          "p2a.tuple.arg_hash_is_first");
                }
                check(state_a->second_arg_hash.has_value(),
                      "p2a.tuple.second_arg_hash_captured");
                if (state_a->second_arg_hash.has_value()) {
                    check(entry.arg_hash != *state_a->second_arg_hash,
                          "p2a.tuple.arg_hash_differs_from_second");
                }
                // 5. Source: ExactSidecar.
                check(entry.source ==
                          ahfl::runtime::PersistedMemoResultSource::
                              ExactSidecar,
                      "p2a.tuple.exact_sidecar");
                // 6. authoritative_json: present, non-empty.
                check(entry.authoritative_json.has_value(),
                      "p2a.tuple.has_authoritative_json");
                if (entry.authoritative_json.has_value()) {
                    check(!entry.authoritative_json->empty(),
                          "p2a.tuple.authoritative_json_nonempty");
                }
                // 7. result_present: present, true.
                check(entry.result_present.has_value(),
                      "p2a.tuple.has_result_present");
                if (entry.result_present.has_value()) {
                    check(*entry.result_present,
                          "p2a.tuple.result_present_true");
                }
            }
        }
    }

    // ---- Process B: cold start. Node 1 is memo-supplied (zero live
    // calls); node 2 is frontier-injected (zero live calls). ----
    auto state_b = std::make_shared<SharedCapState>();
    {
        WorkflowRecoveryStore store(snapshot_path);
        auto loaded = store.load();
        check(loaded.has_value(), "p2a.snapshot_loaded");
        if (!loaded.has_value()) {
            return;
        }

        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = make_shared_cap_invoker(state_b);
        config.recovery_snapshot = std::move(*loaded);
        config.resume_pending_result_wire_json = std::string(kFinalJson);

        wr::WasmWorkflowRuntime runtime(*program, std::move(config));

        auto input = value_from_json(std::string(kFrameJson));
        check(input.has_value(), "p2a.resume_input");
        if (!input.has_value()) {
            return;
        }

        auto resumed =
            runtime.run(std::string(kWorkflow), std::move(*input));

        check(resumed.status() == WorkflowStatus::Completed,
              "p2a.completed");
        check(!resumed.has_errors(), "p2a.resume_no_errors");
        // Zero live side effects on resume: node 1 memo-supplied, node 2
        // frontier-injected.
        check(state_b->echo_calls == 0, "p2a.zero_live_calls");
        check_workflow_completed(resumed, 1, "p2a.complete");

        const auto *output = resumed.output();
        check(output != nullptr, "p2a.has_output");
        if (output != nullptr) {
            const auto json = ahfl::runtime::value_to_json(*output);
            check(json == std::string(kFinalJson), "p2a.output_value");
        }
    }

    // ---- Process C: cross-injection fails closed. Tamper the memo's
    // arg_hash to node 2's arg_hash. On resume, node 1's MemoHit
    // cross-check must reject it (node 1's live arg_hash != node 2's).
    // This proves the arg_hash field distinguishes two nodes that share
    // a cap_id. ----
    {
        WorkflowRecoveryStore store(snapshot_path);
        auto loaded = store.load();
        check(loaded.has_value(), "p2a.xinj.load");
        if (!loaded.has_value()) {
            return;
        }
        auto snapshot = std::move(*loaded);
        if (snapshot.suspended.has_value() &&
            !snapshot.suspended->memo.empty() &&
            state_a->second_arg_hash.has_value()) {
            snapshot.suspended->memo[0].arg_hash =
                *state_a->second_arg_hash;
        } else {
            check(false, "p2a.xinj.tamper_preconditions");
            return;
        }

        auto state_c = std::make_shared<SharedCapState>();
        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = make_shared_cap_invoker(state_c);
        config.recovery_snapshot = std::move(snapshot);
        config.resume_pending_result_wire_json = std::string(kFinalJson);

        wr::WasmWorkflowRuntime runtime(*program, std::move(config));

        auto input = value_from_json(std::string(kFrameJson));
        check(input.has_value(), "p2a.xinj.resume_input");
        if (!input.has_value()) {
            return;
        }

        auto resumed =
            runtime.run(std::string(kWorkflow), std::move(*input));

        check(resumed.status() != WorkflowStatus::Completed,
              "p2a.xinj.not_completed");
        check(resumed.status() == WorkflowStatus::NodeFailed,
              "p2a.xinj.node_failed");
        check(resumed.has_errors(), "p2a.xinj.has_errors");
        const auto msg = first_error_message(resumed);
        check(!msg.empty(), "p2a.xinj.actionable_diagnostic");
        check(msg.find("diverged from the recorded memo") !=
                  std::string::npos,
              "p2a.xinj.divergence_wording");
        // The invoker is never called: the MemoHit cross-check aborts
        // before the live invoker is reached.
        check(state_c->echo_calls == 0, "p2a.xinj.zero_live_calls");
    }
}

// --- P2-B: frontier never hit on replay ---
//
// Construct a recovery snapshot whose frontier identity doesn't occur
// during replay. This is done by fabricating a COMPLETE memo (including
// a fabricated OK memo entry for the pending call itself) and tampering
// the pending_ordinal to a non-occurring value (1 when the node only
// calls the capability once at ordinal 0). On replay, all calls are
// MemoHit (served from the fabricated memo), the replay completes, and
// the post-run guard fires: "the pending call was never reached".
//
// Uses the e4 fixture where A and B both echo the same input, so node 2's
// arg_hash == node 1's arg_hash (reusable from the snapshot's memo[0]).

void test_p2b_frontier_never_hit(
    const std::filesystem::path &repo_root,
    const std::filesystem::path &work_dir) {
    const auto source =
        repo_root /
        "tests/golden/wasm/e4_capability_workflow_resume_memo.ahfl";
    std::string error;
    auto program = conf::compile_conformance_source(source, error);
    check(program.has_value(), "p2b.compile");
    if (!program.has_value()) {
        std::cerr << "  compile failed: " << error << "\n";
        return;
    }

    const auto snapshot_path = work_dir / "p2b-snapshot.json";
    std::error_code ec;
    std::filesystem::remove(snapshot_path, ec);

    static constexpr std::string_view kWorkflow =
        "wasm::e4_capability_workflow_resume_memo::MemoPipeline";
    static constexpr std::string_view kFrameJson =
        R"({"_type":"wasm::e4_capability_workflow_resume_memo::Frame","value":"p2b"})";

    // ---- Process A: A OK (memoized), B Pending. ----
    auto state_a = std::make_shared<InvokerState>();
    {
        WorkflowRecoveryStore store(snapshot_path);
        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = make_counting_invoker(state_a);
        config.recovery_store = &store;

        wr::WasmWorkflowRuntime runtime(*program, std::move(config));

        auto input = value_from_json(std::string(kFrameJson));
        check(input.has_value(), "p2b.input");
        if (!input.has_value()) {
            return;
        }

        auto suspended =
            runtime.run(std::string(kWorkflow), std::move(*input));

        check(suspended.status() == WorkflowStatus::Suspended,
              "p2b.suspended");
        check(!suspended.has_errors(), "p2b.no_errors");
        check(state_a->a_calls == 1, "p2b.a_called_once");
        check(state_a->b_calls == 1, "p2b.b_called_once");
    }

    // ---- Load + fabricate + tamper + resume. ----
    {
        WorkflowRecoveryStore store(snapshot_path);
        auto loaded = store.load();
        check(loaded.has_value(), "p2b.load");
        if (!loaded.has_value()) {
            return;
        }
        auto snapshot = std::move(*loaded);

        check(snapshot.suspended.has_value(), "p2b.has_suspended");
        if (!snapshot.suspended.has_value()) {
            return;
        }
        auto &susp = *snapshot.suspended;
        check(susp.memo.size() == 1, "p2b.memo_size_1");
        if (susp.memo.size() != 1) {
            return;
        }
        check(state_a->b_symbol_id.has_value(),
              "p2b.b_symbol_captured");
        if (!state_a->b_symbol_id.has_value()) {
            return;
        }

        // In e4, A and B both echo the same input, so node 2's arg_hash
        // == node 1's arg_hash (memo[0].arg_hash). Reuse it for the
        // fabricated memo entry.
        const auto node2_arg_hash = susp.memo[0].arg_hash;
        const auto node2_json =
            susp.memo[0].authoritative_json.value_or("");

        // Fabricate an OK memo entry for node 2 (the suspended node).
        // This makes node 2's call a MemoHit on replay instead of the
        // frontier.
        ahfl::runtime::CapabilityMemoEntry fabricated;
        fabricated.ordinal = 0;
        fabricated.cap_id = *state_a->b_symbol_id;
        fabricated.arg_hash = node2_arg_hash;
        fabricated.source =
            ahfl::runtime::PersistedMemoResultSource::ExactSidecar;
        fabricated.authoritative_json = node2_json;
        fabricated.result_present = true;
        fabricated.node = susp.node;
        susp.memo.push_back(std::move(fabricated));

        // Tamper the frontier ordinal to 1 (node 2 only calls B once at
        // ordinal 0, so ordinal 1 never occurs).
        susp.pending_ordinal = 1;

        auto state_b = std::make_shared<InvokerState>();
        state_b->b_pending = false;
        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = make_counting_invoker(state_b);
        config.recovery_snapshot = std::move(snapshot);
        // Supply a pending result (it will never be used: the frontier
        // is never hit).
        auto pending_result = value_from_json(std::string(kFrameJson));
        if (pending_result.has_value()) {
            config.resume_pending_result = std::move(*pending_result);
        }

        wr::WasmWorkflowRuntime runtime(*program, std::move(config));

        auto input = value_from_json(std::string(kFrameJson));
        check(input.has_value(), "p2b.resume_input");
        if (!input.has_value()) {
            return;
        }

        auto resumed =
            runtime.run(std::string(kWorkflow), std::move(*input));

        // The replay completed (all calls MemoHit) but the frontier was
        // never hit -> post-run guard fails closed.
        check(resumed.status() != WorkflowStatus::Completed,
              "p2b.not_completed");
        check(resumed.status() == WorkflowStatus::NodeFailed,
              "p2b.node_failed");
        check(resumed.has_errors(), "p2b.has_errors");
        const auto msg = first_error_message(resumed);
        check(!msg.empty(), "p2b.actionable_diagnostic");
        check(msg.find("the pending call was never reached") !=
                  std::string::npos,
              "p2b.divergence_wording");
        // The invoker is never called: every call is a MemoHit.
        check(state_b->a_calls == 0 && state_b->b_calls == 0,
              "p2b.zero_live_calls");
        // No WorkflowCompleted event: the run failed.
        check_workflow_completed(resumed, 0, "p2b.no_complete");
    }
}

// --- P2-C: zero-length OK memo (replay-side defense) ---
//
// The recording path (zero-length OK memo) is provably unreachable on a
// deterministic guest: handle_opaque always serializes the result through
// serialize_value_for_wire_json (non-empty even for NoneValue -> "null"),
// and the guest classifies an OK reply with a null pointer or zero length
// as ERROR (core_wasm_codegen.cpp cap-status dispatch). The guard at
// workflow_session.cpp origination is retained as defense-in-depth (see
// the reachability comment there).
//
// The reachable replay-side defense is exercised here: a memo entry with
// an empty authoritative_json is rejected at MemoHit (the session refuses
// to serve empty bytes as a capability result).

void test_p2c_zero_length_memo_replay(
    const std::filesystem::path &repo_root,
    const std::filesystem::path &work_dir) {
    const auto source =
        repo_root /
        "tests/golden/wasm/e4_capability_workflow_resume_memo.ahfl";
    std::string error;
    auto program = conf::compile_conformance_source(source, error);
    check(program.has_value(), "p2c.compile");
    if (!program.has_value()) {
        std::cerr << "  compile failed: " << error << "\n";
        return;
    }

    const auto snapshot_path = work_dir / "p2c-snapshot.json";
    std::error_code ec;
    std::filesystem::remove(snapshot_path, ec);

    static constexpr std::string_view kWorkflow =
        "wasm::e4_capability_workflow_resume_memo::MemoPipeline";
    static constexpr std::string_view kFrameJson =
        R"({"_type":"wasm::e4_capability_workflow_resume_memo::Frame","value":"p2c"})";

    // Suspend (A OK, B Pending).
    {
        WorkflowRecoveryStore store(snapshot_path);
        auto state = std::make_shared<InvokerState>();
        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = make_counting_invoker(state);
        config.recovery_store = &store;

        wr::WasmWorkflowRuntime runtime(*program, std::move(config));
        auto input = value_from_json(std::string(kFrameJson));
        if (!input.has_value()) {
            check(false, "p2c.input");
            return;
        }
        auto suspended =
            runtime.run(std::string(kWorkflow), std::move(*input));
        check(suspended.status() == WorkflowStatus::Suspended,
              "p2c.suspended");
    }

    // Load + tamper + resume.
    {
        WorkflowRecoveryStore store(snapshot_path);
        auto loaded = store.load();
        check(loaded.has_value(), "p2c.load");
        if (!loaded.has_value()) {
            return;
        }
        auto snapshot = std::move(*loaded);
        // Tamper the memo's authoritative_json to empty. The MemoHit
        // cross-check must reject it (the session refuses to serve empty
        // bytes as a capability result).
        if (snapshot.suspended.has_value() &&
            !snapshot.suspended->memo.empty()) {
            snapshot.suspended->memo[0].authoritative_json = "";
        } else {
            check(false, "p2c.tamper_preconditions");
            return;
        }

        auto state = std::make_shared<InvokerState>();
        state->b_pending = false;
        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = make_counting_invoker(state);
        config.recovery_snapshot = std::move(snapshot);
        auto pending_result = value_from_json(std::string(kFrameJson));
        if (pending_result.has_value()) {
            config.resume_pending_result = std::move(*pending_result);
        }

        wr::WasmWorkflowRuntime runtime(*program, std::move(config));
        auto input = value_from_json(std::string(kFrameJson));
        if (!input.has_value()) {
            check(false, "p2c.resume_input");
            return;
        }
        auto resumed =
            runtime.run(std::string(kWorkflow), std::move(*input));

        check(resumed.status() != WorkflowStatus::Completed,
              "p2c.not_completed");
        check(resumed.status() == WorkflowStatus::NodeFailed,
              "p2c.node_failed");
        check(resumed.has_errors(), "p2c.has_errors");
        const auto msg = first_error_message(resumed);
        check(!msg.empty(), "p2c.actionable_diagnostic");
        check(msg.find("diverged from the recorded memo") !=
                  std::string::npos,
              "p2c.divergence_wording");
        check(state->a_calls == 0 && state->b_calls == 0,
              "p2c.zero_live_calls");
    }
}

// --- P2-E: unresolvable pending cap_id ---
//
// A pending SymbolId that no node in the module calls must fail closed at
// load time (validate_wasm_recovery_snapshot), before any run/invoker.
// The diagnostic must pin the wording "pending capability is not
// resolvable in this module".

void test_p2e_pending_cap_unresolvable(
    const std::filesystem::path &repo_root,
    const std::filesystem::path &work_dir) {
    const auto source =
        repo_root /
        "tests/golden/wasm/e4_capability_workflow_resume_memo.ahfl";
    std::string error;
    auto program = conf::compile_conformance_source(source, error);
    check(program.has_value(), "p2e.compile");
    if (!program.has_value()) {
        std::cerr << "  compile failed: " << error << "\n";
        return;
    }

    const auto snapshot_path = work_dir / "p2e-snapshot.json";
    std::error_code ec;
    std::filesystem::remove(snapshot_path, ec);

    static constexpr std::string_view kWorkflow =
        "wasm::e4_capability_workflow_resume_memo::MemoPipeline";
    static constexpr std::string_view kFrameJson =
        R"({"_type":"wasm::e4_capability_workflow_resume_memo::Frame","value":"p2e"})";

    // Suspend (A OK, B Pending).
    {
        WorkflowRecoveryStore store(snapshot_path);
        auto state = std::make_shared<InvokerState>();
        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = make_counting_invoker(state);
        config.recovery_store = &store;

        wr::WasmWorkflowRuntime runtime(*program, std::move(config));
        auto input = value_from_json(std::string(kFrameJson));
        if (!input.has_value()) {
            check(false, "p2e.input");
            return;
        }
        auto suspended =
            runtime.run(std::string(kWorkflow), std::move(*input));
        check(suspended.status() == WorkflowStatus::Suspended,
              "p2e.suspended");
    }

    // Load + tamper + resume.
    {
        WorkflowRecoveryStore store(snapshot_path);
        auto loaded = store.load();
        check(loaded.has_value(), "p2e.load");
        if (!loaded.has_value()) {
            return;
        }
        auto snapshot = std::move(*loaded);
        // Tamper the pending cap_id to an unresolvable SymbolId.
        if (snapshot.suspended.has_value()) {
            snapshot.suspended->pending_cap_id = 999;
        } else {
            check(false, "p2e.tamper_preconditions");
            return;
        }

        auto state = std::make_shared<InvokerState>();
        state->b_pending = false;
        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = make_counting_invoker(state);
        config.recovery_snapshot = std::move(snapshot);
        auto pending_result = value_from_json(std::string(kFrameJson));
        if (pending_result.has_value()) {
            config.resume_pending_result = std::move(*pending_result);
        }

        wr::WasmWorkflowRuntime runtime(*program, std::move(config));
        auto input = value_from_json(std::string(kFrameJson));
        if (!input.has_value()) {
            check(false, "p2e.resume_input");
            return;
        }
        auto resumed =
            runtime.run(std::string(kWorkflow), std::move(*input));

        // The load-time validation fails closed: the pending cap_id is
        // not resolvable in the module.
        check(resumed.status() != WorkflowStatus::Completed,
              "p2e.not_completed");
        check(resumed.status() == WorkflowStatus::NodeFailed,
              "p2e.node_failed");
        check(resumed.has_errors(), "p2e.has_errors");
        const auto msg = first_error_message(resumed);
        check(!msg.empty(), "p2e.actionable_diagnostic");
        check(msg.find("pending capability is not resolvable") !=
                  std::string::npos,
              "p2e.wording");
        // The invoker is never called: the validation aborts before any
        // run/invoker.
        check(state->a_calls == 0 && state->b_calls == 0,
              "p2e.zero_live_calls");
        // No WorkflowCompleted event: the run failed at load time.
        check_workflow_completed(resumed, 0, "p2e.no_complete");
    }
}

// ==== 8. WH-5c.5 GAP 4: replay stash-rebuild parity ====
//
// The wh5c5_gap4_stash_parity fixture is a 3-node opaque pipeline:
// lead (identity) -> middle (Echo capability) -> tail (identity).
// Echo returns PENDING on the first call, so the workflow suspends
// AFTER lead completes (lead's output is in the stash table at
// suspend time). On resume (fresh facade, cold-start from the on-disk
// snapshot), lead re-runs deterministically, Echo is frontier-injected
// (zero live calls), and tail runs. The per-node output stash table is
// rebuilt during the resume run, so the host decodes every node output
// and the output_value_id sequence is contiguous (no gaps, no
// NoneValue fallbacks).
//
// Value-ID accounting: on a fresh run the wasm lane emits a
// CapabilityCompleted for Echo (cap output -> value ID 1), so the node
// output_value_ids are [0, 2, 3] and the workflow output is 4. On a
// resumed run the frontier injection bypasses the invoker wrapper (no
// CapabilityStarted/CapabilityCompleted), so
// the node output_value_ids are [0, 1, 2] and the workflow output is
// 3. Both sequences are contiguous; the fresh-vs-resumed delta is
// exactly the skipped capability event. The test pins both sequences
// and verifies the output VALUES are identical.

void test_wh5c5_gap4_replay_stash_parity(
    const std::filesystem::path &repo_root,
    const std::filesystem::path &work_dir) {
    const auto source =
        repo_root / "tests/golden/wasm/wh5c5_gap4_stash_parity.ahfl";
    std::string error;
    auto program = conf::compile_conformance_source(source, error);
    check(program.has_value(), "wh5c5rp.compile");
    if (!program.has_value()) {
        std::cerr << "  compile failed: " << error << "\n";
        return;
    }

    static constexpr std::string_view kWorkflow =
        "wasm::wh5c5_gap4_stash::StashParityPipeline";
    static constexpr std::string_view kFrameJson =
        R"({"_type":"wasm::wh5c5_gap4_stash::Frame","value":"stash-parity"})";

    auto make_resolver = [] {
        return [](std::uint64_t) -> std::optional<std::string> {
            return "wasm::wh5c5_gap4_stash::Echo";
        };
    };

    // Collect node output_value_id indices from a completed WorkflowResult.
    auto collect_node_ids =
        [](const ahfl::runtime::WorkflowResult &result)
        -> std::vector<std::size_t> {
        std::vector<std::size_t> ids;
        for (const auto &node : result.report.nodes) {
            ids.push_back(node.output.has_value() ? node.output->index()
                                                  : std::size_t{0});
        }
        return ids;
    };

    // ---- Fresh run: Echo returns Success, all nodes complete. ----
    std::vector<std::size_t> fresh_node_ids;
    std::optional<std::size_t> fresh_wf_id;
    {
        auto state = std::make_shared<InvokerState>();
        state->echo_pending = false;
        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = make_counting_invoker(state);
        config.name_resolver = make_resolver();

        wr::WasmWorkflowRuntime runtime(*program, std::move(config));
        auto input = value_from_json(std::string(kFrameJson));
        check(input.has_value(), "wh5c5rp.fresh_input");
        if (!input.has_value()) {
            return;
        }

        auto result =
            runtime.run(std::string(kWorkflow), std::move(*input));
        check(result.status() == WorkflowStatus::Completed,
              "wh5c5rp.fresh_completed");
        check(!result.has_errors(), "wh5c5rp.fresh_no_errors");
        fresh_node_ids = collect_node_ids(result);
        fresh_wf_id = result.report.output.has_value()
                          ? std::optional<std::size_t>(
                                result.report.output->index())
                          : std::nullopt;
    }

    // Fresh-run expectation: cap output is value 1, so node ids are
    // [0, 2, 3] and the workflow output is 4.
    check(fresh_node_ids.size() == 3, "wh5c5rp.fresh_3_nodes");
    if (fresh_node_ids.size() == 3) {
        check(fresh_node_ids[0] == 0, "wh5c5rp.fresh_node0_id0");
        check(fresh_node_ids[1] == 2, "wh5c5rp.fresh_node1_id2");
        check(fresh_node_ids[2] == 3, "wh5c5rp.fresh_node2_id3");
    }
    check(fresh_wf_id.has_value() && *fresh_wf_id == 4,
          "wh5c5rp.fresh_wf_id4");

    // ---- Suspend: Echo returns PENDING, workflow suspends after lead. ----
    const auto snapshot_path = work_dir / "wh5c5rp-snapshot.json";
    std::error_code ec;
    std::filesystem::remove(snapshot_path, ec);
    {
        auto state = std::make_shared<InvokerState>();
        state->echo_pending = true;
        WorkflowRecoveryStore store(snapshot_path);
        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = make_counting_invoker(state);
        config.recovery_store = &store;
        config.name_resolver = make_resolver();

        wr::WasmWorkflowRuntime runtime(*program, std::move(config));
        auto input = value_from_json(std::string(kFrameJson));
        check(input.has_value(), "wh5c5rp.suspend_input");
        if (!input.has_value()) {
            return;
        }

        auto suspended =
            runtime.run(std::string(kWorkflow), std::move(*input));
        check(suspended.status() == WorkflowStatus::Suspended,
              "wh5c5rp.suspended");
        check(!suspended.has_errors(), "wh5c5rp.suspend_no_errors");
        check(state->echo_calls == 1, "wh5c5rp.echo_called_once");
        check(suspended.suspended.has_value(),
              "wh5c5rp.has_resume_record");
    }

    check(std::filesystem::exists(snapshot_path),
          "wh5c5rp.snapshot_on_disk");

    // ---- Resume: cold-start, inject Echo result via wire JSON. ----
    std::vector<std::size_t> resumed_node_ids;
    std::optional<std::size_t> resumed_wf_id;
    {
        auto state = std::make_shared<InvokerState>();
        state->echo_pending = false; // Echo would return Success if called
        WorkflowRecoveryStore store(snapshot_path);
        auto loaded = store.load();
        check(loaded.has_value(), "wh5c5rp.snapshot_loaded");
        if (!loaded.has_value()) {
            return;
        }

        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = make_counting_invoker(state);
        config.recovery_snapshot = std::move(*loaded);
        config.resume_pending_result_wire_json = std::string(kFrameJson);
        config.name_resolver = make_resolver();

        wr::WasmWorkflowRuntime runtime(*program, std::move(config));
        auto input = value_from_json(std::string(kFrameJson));
        check(input.has_value(), "wh5c5rp.resume_input");
        if (!input.has_value()) {
            return;
        }

        auto resumed =
            runtime.run(std::string(kWorkflow), std::move(*input));
        check(resumed.status() == WorkflowStatus::Completed,
              "wh5c5rp.resumed_completed");
        check(!resumed.has_errors(), "wh5c5rp.resume_no_errors");
        // Zero live side effects: Echo is frontier-injected.
        check(state->echo_calls == 0, "wh5c5rp.zero_live_calls");

        resumed_node_ids = collect_node_ids(resumed);
        resumed_wf_id = resumed.report.output.has_value()
                            ? std::optional<std::size_t>(
                                  resumed.report.output->index())
                            : std::nullopt;

        // The workflow output value must be correct.
        const auto *output = resumed.output();
        check(output != nullptr, "wh5c5rp.resumed_has_output");
        if (output != nullptr) {
            const auto json = ahfl::runtime::value_to_json(*output);
            check(json == std::string(kFrameJson),
                  "wh5c5rp.resumed_output_value");
        }
    }

    // Resumed-run expectation: no CapabilityCompleted on the frontier
    // path, so node ids are [0, 1, 2] and the
    // workflow output is 3. The stash table was rebuilt: every node has
    // a decoded output (no NoneValue fallback).
    check(resumed_node_ids.size() == 3, "wh5c5rp.resumed_3_nodes");
    if (resumed_node_ids.size() == 3) {
        check(resumed_node_ids[0] == 0, "wh5c5rp.resumed_node0_id0");
        check(resumed_node_ids[1] == 1, "wh5c5rp.resumed_node1_id1");
        check(resumed_node_ids[2] == 2, "wh5c5rp.resumed_node2_id2");
    }
    check(resumed_wf_id.has_value() && *resumed_wf_id == 3,
          "wh5c5rp.resumed_wf_id3");

    // The fresh-vs-resumed delta is exactly the skipped capability
    // event: resumed ids are fresh ids shifted down by 1 for nodes at
    // or after the capability node (index 1).
    if (fresh_node_ids.size() == 3 && resumed_node_ids.size() == 3) {
        check(resumed_node_ids[0] == fresh_node_ids[0],
              "wh5c5rp.node0_id_stable");
        check(resumed_node_ids[1] == fresh_node_ids[1] - 1,
              "wh5c5rp.node1_id_shifted");
        check(resumed_node_ids[2] == fresh_node_ids[2] - 1,
              "wh5c5rp.node2_id_shifted");
    }
    if (fresh_wf_id.has_value() && resumed_wf_id.has_value()) {
        check(*resumed_wf_id == *fresh_wf_id - 1,
              "wh5c5rp.wf_id_shifted");
    }
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cerr << "usage: wasm_workflow_resume_e2e <repo-root> <work-dir>\n";
        return 2;
    }
    const std::filesystem::path repo = argv[1];
    const std::filesystem::path work = argv[2];
    std::error_code ec;
    std::filesystem::create_directories(work, ec);

    test_e3_real_ac1_cycle(repo, work);
    test_wh5b3_hybrid_resume(repo, work);
    test_wh5b2_bridge_pending_resume(repo, work);
    // WH-5b.2 §12.14.9 host-level fail-closed cases 5 and 7.
    test_wh5b2_bridge_error_fails(repo, work);
    test_wh5b2_no_tag0_on_suspend(repo, work);
    // WH-5b.2 AC10: two sequential bridge calls determinism.
    test_wh5b2_ac10_two_bridge_calls(repo, work);
    // WH-5b.2 §12.14.9 host-level fail-closed cases 1 and 2.
    test_wh5b2_fc1_ordinal_tamper(repo, work);
    test_wh5b2_fc2_arg_hash_mismatch(repo, work);
    test_e4_memo_round_trip(repo, work);
    test_fail_closed_missing_pending_result(repo, work);
    test_intent_alignment(repo, work);

    // AC3 fail-closed cases (P1-3).
    test_ac3_corrupt_snapshot_json(repo, work);
    test_ac3_workflow_id_mismatch(repo, work);
    test_ac3_suspended_node_unknown(repo, work);
    test_ac3_memo_cap_id_mismatch(repo, work);
    test_ac3_memo_arg_hash_mismatch(repo, work);
    test_ac3_wire_json_wrong_type(repo, work);
    test_ac3_pending_identity_mismatch(repo, work);
    test_ac3_replay_divergence(repo, work);

    // P1-2: non-topological schedule.
    test_non_topological_resume(repo, work);

    // WH-4b P2 fail-closed family.
    test_p2a_shared_cap_resume_identity(repo, work);
    test_p2b_frontier_never_hit(repo, work);
    test_p2c_zero_length_memo_replay(repo, work);
    test_p2e_pending_cap_unresolvable(repo, work);

    // WH-5c.5 GAP 4: replay stash-rebuild parity.
    test_wh5c5_gap4_replay_stash_parity(repo, work);

    std::cout << g_pass << "/" << g_checks << " e2e checks passed\n";
    return (g_pass == g_checks) ? EXIT_SUCCESS : EXIT_FAILURE;
}
