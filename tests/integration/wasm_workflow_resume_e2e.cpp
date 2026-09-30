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
//
// The e4 fixture is tests/golden/wasm/e4_capability_workflow_resume_memo.ahfl.
// The e3 fixture is tests/golden/wasm/e3_capability_workflow_resume.ahfl.
// The e5 fixture is tests/golden/wasm/e5_non_topological_resume.ahfl.

#include "runtime/wasm_runner/wasm_workflow_runtime.hpp"

#include "runtime/engine/workflow_recovery.hpp"
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
    bool b_pending{true};
    bool echo_pending{true};
    std::optional<std::size_t> a_symbol_id;
    std::optional<std::size_t> b_symbol_id;
    std::optional<std::size_t> echo_symbol_id;
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
                        std::optional<std::string> wire_json = std::nullopt) {
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
        });
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
        });
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
        });
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
        });
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
        std::string("\"not-a-frame\""));
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
        });
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

    std::cout << g_pass << "/" << g_checks << " e2e checks passed\n";
    return (g_pass == g_checks) ? EXIT_SUCCESS : EXIT_FAILURE;
}
