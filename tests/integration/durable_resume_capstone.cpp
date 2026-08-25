// RFC 0022 durable-resume capstone (Q4 roadmap M2 north-star).
//
// Proves the end-to-end fact the roadmap defines: embed AHFL into a host, run a
// FORMALLY VERIFIED workflow, have a capability return PENDING, suspend, persist
// a resume record to disk, then in a FRESH runtime (cold start — nothing shared
// but the on-disk snapshot) resume with the host-supplied result and reach a
// deterministic final identical to the synchronous path.
//
// The workflow is examples/execution-demo, compiled here through the full
// frontend (parse -> resolve -> typecheck -> validate -> lower); "verified" is
// not decoration — a validation failure fails this test. The async capability is
// DraftIncidentSummary (the LLM draft in the `respond` node).

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/validate.hpp"
#include "common/project_input_support.hpp"
#include "compiler/syntax/frontend/project.hpp"
#include "runtime/engine/workflow_recovery.hpp"
#include "runtime/engine/workflow_runtime.hpp"
#include "runtime/evaluator/value.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>

namespace {

using ahfl::runtime::CapabilityCallResult;
using ahfl::runtime::CapabilityCallStatus;
using ahfl::runtime::CapabilityInvocationContext;
using ahfl::runtime::WorkflowRecoverySnapshot;
using ahfl::runtime::WorkflowRecoveryStore;
using ahfl::runtime::WorkflowRuntime;
using ahfl::runtime::WorkflowRuntimeConfig;
using ahfl::runtime::WorkflowStatus;
using Value = ahfl::evaluator::Value;

int test_count = 0;
int pass_count = 0;

void check(bool condition, const std::string &name) {
    ++test_count;
    if (condition) {
        ++pass_count;
    } else {
        std::cerr << "FAIL: " << name << "\n";
    }
}

void render(const ahfl::DiagnosticBag &diagnostics) { diagnostics.render(std::cerr); }

// Compile examples/execution-demo through the full verifying frontend. A
// resolve/typecheck/validate failure returns nullopt — the workflow is only
// "verified" if every stage passes.
[[nodiscard]] std::optional<ahfl::ir::Program>
compile_execution_demo(const std::filesystem::path &repo) {
    const auto project = repo / "examples" / "execution-demo";
    const auto entry = project / "src" / "main.ahfl";
    const auto input = ahfl::test_support::project_input_from_manifest(
        project / "ahfl.toml", entry, repo);
    if (input.has_errors()) {
        ahfl::test_support::print_package_graph_diagnostics(input.diagnostics, std::cerr);
        return std::nullopt;
    }
    const ahfl::Frontend frontend;
    auto parsed = ahfl::parse_project(frontend, *input.input);
    if (parsed.has_errors()) {
        render(parsed.diagnostics);
        return std::nullopt;
    }
    const ahfl::Resolver resolver;
    auto resolved = resolver.resolve(parsed.graph);
    if (resolved.has_errors()) {
        render(resolved.diagnostics);
        return std::nullopt;
    }
    const ahfl::TypeChecker checker;
    auto typed = checker.check(parsed.graph, resolved);
    if (typed.has_errors()) {
        render(typed.diagnostics);
        return std::nullopt;
    }
    const ahfl::Validator validator;
    auto validated = validator.validate(parsed.graph, resolved, typed);
    if (validated.has_errors()) {
        render(validated.diagnostics);
        return std::nullopt;
    }
    return ahfl::lower_program_ir(parsed.graph, resolved, typed);
}

// The IncidentRequest input used by the demo (mirrors inputs/high-severity.json).
[[nodiscard]] Value make_incident_input() {
    std::unordered_map<std::string, Value> fields;
    fields.emplace("ticket_id", ahfl::evaluator::make_string("INC-1001"));
    fields.emplace("service", ahfl::evaluator::make_string("checkout"));
    fields.emplace("severity",
                   ahfl::evaluator::make_enum("execution_demo::types::Severity", "High"));
    fields.emplace("customer_impact", ahfl::evaluator::make_bool(true));
    return ahfl::evaluator::make_struct("execution_demo::types::IncidentRequest",
                                        std::move(fields));
}

// The GeneratedSummary the "LLM" eventually produces for DraftIncidentSummary.
[[nodiscard]] Value make_summary(const std::string &text) {
    std::unordered_map<std::string, Value> fields;
    fields.emplace("summary", ahfl::evaluator::make_string(text));
    return ahfl::evaluator::make_struct("execution_demo::types::GeneratedSummary",
                                        std::move(fields));
}

[[nodiscard]] const std::string *
summary_of(const ahfl::runtime::WorkflowResult &result) {
    const auto *output = result.output();
    if (output == nullptr) {
        return nullptr;
    }
    const auto *sv = std::get_if<ahfl::evaluator::StructValue>(&output->node);
    if (sv == nullptr) {
        return nullptr;
    }
    const auto *field = sv->fields.get("summary");
    if (field == nullptr) {
        return nullptr;
    }
    const auto *str = std::get_if<ahfl::evaluator::StringValue>(&field->node);
    return str != nullptr ? &str->value : nullptr;
}

constexpr const char *kRecoveredText = "durable recovered summary";

// A synchronous baseline: DraftIncidentSummary returns the summary immediately.
[[nodiscard]] std::optional<std::string>
run_synchronous(const ahfl::ir::Program &program) {
    WorkflowRuntimeConfig config;
    config.contextual_capability_invoker =
        [](const CapabilityInvocationContext &, const std::string &name,
           const std::vector<Value> &) -> CapabilityCallResult {
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Success;
        r.value = name.ends_with("DraftIncidentSummary") ? make_summary(kRecoveredText)
                                                          : ahfl::evaluator::make_none();
        return r;
    };
    WorkflowRuntime runtime(program, std::move(config));
    auto result = runtime.run("execution_demo::main::IncidentWorkflow", make_incident_input());
    if (result.status() != WorkflowStatus::Completed) {
        return std::nullopt;
    }
    const auto *summary = summary_of(result);
    return summary != nullptr ? std::optional{*summary} : std::nullopt;
}

void test_capstone(const std::filesystem::path &repo, const std::filesystem::path &work) {
    // Step 0 — compile + verify.
    auto program = compile_execution_demo(repo);
    check(program.has_value(), "capstone.workflow_verified_and_compiled");
    if (!program.has_value()) {
        return;
    }

    // Baseline: what the synchronous path produces (the invariant resume must match).
    const auto sync_summary = run_synchronous(*program);
    check(sync_summary.has_value() && *sync_summary == kRecoveredText,
          "capstone.synchronous_baseline");

    const auto snapshot_path = work / "durable-resume-snapshot.json";
    std::error_code ec;
    std::filesystem::remove(snapshot_path, ec);

    // ---- Process A: run until the async capability returns PENDING, suspend,
    // persist the resume record to disk. Nothing else survives to Process B. ----
    {
        WorkflowRecoveryStore store(snapshot_path);
        std::size_t process_a_invocations = 0;
        WorkflowRuntimeConfig config;
        config.contextual_capability_invoker =
            [&process_a_invocations](const CapabilityInvocationContext &,
                                     const std::string &name,
                                     const std::vector<Value> &) -> CapabilityCallResult {
            CapabilityCallResult r;
            if (name.ends_with("DraftIncidentSummary")) {
                ++process_a_invocations;
                r.status = CapabilityCallStatus::Pending; // async: host will answer later
            } else {
                r.status = CapabilityCallStatus::Success;
                r.value = ahfl::evaluator::make_none();
            }
            return r;
        };
        WorkflowRuntime runtime(*program, std::move(config));
        auto suspended =
            runtime.run("execution_demo::main::IncidentWorkflow", make_incident_input());

        check(suspended.status() == WorkflowStatus::Suspended, "capstone.process_a_suspended");
        check(!suspended.has_errors(), "capstone.process_a_no_errors");
        check(process_a_invocations == 1, "capstone.process_a_called_async_once");
        check(suspended.suspended.has_value(), "capstone.process_a_has_resume_record");
        if (suspended.suspended.has_value()) {
            const auto saved = store.save(*suspended.suspended);
            check(saved.has_value(), "capstone.snapshot_persisted_to_disk");
        }
    }

    check(std::filesystem::exists(snapshot_path), "capstone.snapshot_file_on_disk");

    // ---- Process B: cold start. A brand-new runtime, recompiled program, whose
    // ONLY link to Process A is the on-disk snapshot. Load it, inject the awaited
    // result, resume. The async capability must NOT be re-invoked. ----
    {
        auto cold_program = compile_execution_demo(repo);
        check(cold_program.has_value(), "capstone.cold_start_recompiled");
        if (!cold_program.has_value()) {
            return;
        }
        WorkflowRecoveryStore store(snapshot_path);
        auto loaded = store.load();
        check(loaded.has_value(), "capstone.snapshot_loaded_from_disk");
        if (!loaded.has_value()) {
            return;
        }

        std::size_t process_b_invocations = 0;
        WorkflowRuntimeConfig config;
        config.recovery_snapshot = std::move(*loaded);
        config.resume_pending_result = make_summary(kRecoveredText);
        config.contextual_capability_invoker =
            [&process_b_invocations](const CapabilityInvocationContext &,
                                     const std::string &name,
                                     const std::vector<Value> &) -> CapabilityCallResult {
            if (name.ends_with("DraftIncidentSummary")) {
                ++process_b_invocations; // must stay 0: served from injected result
            }
            CapabilityCallResult r;
            r.status = CapabilityCallStatus::Success;
            r.value = ahfl::evaluator::make_none();
            return r;
        };
        WorkflowRuntime runtime(*cold_program, std::move(config));
        auto resumed =
            runtime.run("execution_demo::main::IncidentWorkflow", make_incident_input());

        check(resumed.status() == WorkflowStatus::Completed, "capstone.cold_start_completed");
        check(!resumed.has_errors(), "capstone.cold_start_no_errors");
        check(process_b_invocations == 0, "capstone.async_not_reinvoked_on_resume");
        const auto *summary = summary_of(resumed);
        check(summary != nullptr && *summary == kRecoveredText,
              "capstone.deterministic_final_matches_sync");
    }
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cerr << "usage: durable_resume_capstone <repo-root> <work-dir>\n";
        return 2;
    }
    const std::filesystem::path repo = argv[1];
    const std::filesystem::path work = argv[2];
    std::error_code ec;
    std::filesystem::create_directories(work, ec);

    test_capstone(repo, work);

    std::cout << pass_count << "/" << test_count << " capstone checks passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
