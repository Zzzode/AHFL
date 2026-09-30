// RFC 0026 KR6.8 WH-4b: facade-level durable-resume end-to-end test.
//
// Drives the WasmWorkflowRuntime facade through the full
// suspend -> persist -> cold-start -> resume cycle, proving the
// WH-4b suspended-snapshot origination on the wasm lane:
//
//   1. e3 round-trip: a capability workflow completes synchronously
//      (no suspend; the facade is a pass-through to the session).
//   2. e4 memo round-trip: a 2-node opaque pipeline suspends on the
//      second capability (B returns PENDING). The first capability
//      (A) returned OK and was memoized. On resume (fresh facade,
//      cold-start from the on-disk snapshot), A is memo-supplied
//      (zero live calls) and B is frontier-injected (zero live
//      calls). The workflow completes deterministically.
//   3. Fail-closed family: a resume that supplies NO pending result
//      fails closed (the session rejects the resume, never silently
//      runs fresh).
//   4. Intent alignment: the durable_write_intent_sink fires for a
//      durable_write capability before dispatch, and fires ZERO
//      times on a resumed run (memo hits and frontier injections
//      never reach the invoker).
//
// The e4 fixture is tests/golden/wasm/e4_capability_workflow_resume_memo.ahfl.
// The e3 fixture is tests/golden/wasm/e3_capability_workflow.ahfl.

#include "runtime/wasm_runner/wasm_workflow_runtime.hpp"

#include "runtime/engine/workflow_recovery.hpp"
#include "runtime/value/value.hpp"
#include "runtime/value/value_json.hpp"

#include "conformance/compile_source.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
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

// A counting invoker for the e4 memo round-trip. A returns Success
// (echo); B returns Pending on the first call (origination) and is
// never called on resume (frontier injection). The counters prove
// zero live side effects on the resumed run. The capability name is
// the fully qualified canonical name (e.g.
// "wasm::e4_capability_workflow_resume_memo::A"), so suffix matching
// on "::A" / "::B" is unambiguous within this fixture.
struct CountingInvoker {
    int a_calls{0};
    int b_calls{0};
    bool b_pending{true};

    ahfl::runtime::ContextualCapabilityInvoker as_invoker() {
        return [this](const CapabilityInvocationContext &,
                      const std::string &name,
                      const std::vector<Value> &args) -> CapabilityCallResult {
            CapabilityCallResult r;
            if (name.ends_with("::A")) {
                ++a_calls;
                r.status = CapabilityCallStatus::Success;
                if (!args.empty()) {
                    r.value = ahfl::runtime::clone_value(args[0]);
                }
            } else if (name.ends_with("::B")) {
                ++b_calls;
                if (b_pending) {
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
};

// ==== 1. e3 round-trip (synchronous completion) ====

void test_e3_round_trip(const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/e3_capability_workflow.ahfl";
    std::string error;
    auto program = conf::compile_conformance_source(source, error);
    check(program.has_value(), "e3.compile");
    if (!program.has_value()) {
        std::cerr << "  compile failed: " << error << "\n";
        return;
    }

    wr::WasmWorkflowRuntimeConfig config;
    config.invoker = [](const CapabilityInvocationContext &,
                        const std::string &,
                        const std::vector<Value> &args) -> CapabilityCallResult {
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Success;
        if (!args.empty()) {
            r.value = ahfl::runtime::clone_value(args[0]);
        }
        return r;
    };
    config.name_resolver = [](std::uint64_t) -> std::optional<std::string> {
        return "Echo";
    };

    wr::WasmWorkflowRuntime runtime(*program, std::move(config));

    auto input = value_from_json(
        R"({"_type":"wasm::e3_capability_workflow::Frame","value":"echo"})");
    check(input.has_value(), "e3.input");
    if (!input.has_value()) {
        return;
    }

    auto result = runtime.run("wasm::e3_capability_workflow::CapabilityPipeline",
                              std::move(*input));
    check(result.status() == WorkflowStatus::Completed, "e3.completed");
    check(!result.has_errors(), "e3.no_errors");

    const auto *output = result.output();
    check(output != nullptr, "e3.has_output");
    if (output != nullptr) {
        const auto json = ahfl::runtime::value_to_json(*output);
        check(json ==
                  R"({"_type":"wasm::e3_capability_workflow::Frame","value":"echo"})",
              "e3.output_value");
    }
}

// ==== 2. e4 memo round-trip (suspend -> persist -> resume) ====

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
    CountingInvoker invoker_a;
    {
        WorkflowRecoveryStore store(snapshot_path);
        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = invoker_a.as_invoker();
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
        check(invoker_a.a_calls == 1, "e4.process_a_a_called_once");
        check(invoker_a.b_calls == 1, "e4.process_a_b_called_once");
        check(suspended.suspended.has_value(),
              "e4.process_a_has_resume_record");
    }

    check(std::filesystem::exists(snapshot_path),
          "e4.snapshot_file_on_disk");

    // ---- Process B: cold start. Fresh facade, on-disk snapshot is the
    // only link. A is memo-supplied (zero live calls); B is
    // frontier-injected (zero live calls). ----
    CountingInvoker invoker_b;
    invoker_b.b_pending = false; // B would return Success if called (it must NOT be)
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
        config.invoker = invoker_b.as_invoker();
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
        check(invoker_b.a_calls == 0, "e4.process_b_a_zero_live_calls");
        check(invoker_b.b_calls == 0, "e4.process_b_b_zero_live_calls");

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
        CountingInvoker invoker;
        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = invoker.as_invoker();
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

        CountingInvoker invoker;
        invoker.b_pending = false;
        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = invoker.as_invoker();
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
        CountingInvoker invoker;
        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = invoker.as_invoker();
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
        check(invoker.a_calls == 1, "intent.a_called_once");
        check(invoker.b_calls == 1, "intent.b_called_once");
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

        CountingInvoker invoker;
        invoker.b_pending = false; // B would return Success if called (it must NOT be)
        wr::WasmWorkflowRuntimeConfig config;
        config.invoker = invoker.as_invoker();
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
        check(invoker.a_calls == 0, "intent.resume_a_zero_live");
        check(invoker.b_calls == 0, "intent.resume_b_zero_live");
        check(intent_count_b == 0, "intent.process_b_zero_intents");
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

    test_e3_round_trip(repo);
    test_e4_memo_round_trip(repo, work);
    test_fail_closed_missing_pending_result(repo, work);
    test_intent_alignment(repo, work);

    std::cout << g_pass << "/" << g_checks << " e2e checks passed\n";
    return (g_pass == g_checks) ? EXIT_SUCCESS : EXIT_FAILURE;
}
