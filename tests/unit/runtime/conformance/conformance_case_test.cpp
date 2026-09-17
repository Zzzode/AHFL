// KR6.7 (RFC 0026 P7): engine-independent conformance case manifest schema
// validator. Hand-rolled check()/main(), mirroring the other RFC 0026 slice
// tests. No engine is linked or invoked.
//
// Coverage (per slice spec):
//   (a) every committed sidecar under tests/conformance/cases loads and every
//       one references an existing AHFL source (the 4 tests/golden/wasm and 3
//       tests/golden/runtime fixtures);
//   (b) loaded manifests round-trip the expected name-only contract: kind,
//       entry, named scenarios with canonical wire input + expectations,
//       capability outcomes, and wasm eligibility metadata;
//   (c) malformed manifests are rejected with precise diagnostics:
//       missing entry, bad capability status enum, non-canonical
//       expect.output_json, absent engines block, unknown field, plus
//       format_version / kind / run_status / eligibility enum / pending+result
//       / duplicate capability / unconfigured invoked capability / workflow
//       state-sequence / source-path escapes / duplicate or anonymous
//       scenario / empty scenario list;
//   (d) the canonicality gate distinguishes a compact correctly-ordered wire
//       fragment from one carrying whitespace or a shuffled struct field.

#include "conformance/conformance_case.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#ifndef AHFL_SOURCE_DIR
#error "AHFL_SOURCE_DIR must be defined by the test target"
#endif

namespace {

using ahfl::conformance::CapabilityOutcomeStatus;
using ahfl::conformance::CaseKind;
using ahfl::conformance::ConformanceCase;
using ahfl::conformance::ExpectedRunStatus;
using ahfl::conformance::load_conformance_case;
using ahfl::conformance::parse_conformance_case_json;
using ahfl::conformance::WasmEligibility;
using ahfl::conformance::detail::is_conformance_case_sidecar;

int g_failures = 0;

void check(bool ok, std::string_view name) {
    if (!ok) {
        ++g_failures;
        std::cerr << "FAIL: " << name << "\n";
    }
}

[[nodiscard]] bool diagnostics_contain(const ahfl::DiagnosticBag &diagnostics,
                                       std::string_view needle) {
    for (const auto &entry : diagnostics.entries()) {
        if (entry.message.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// (a)+(b) committed case battery
// ---------------------------------------------------------------------------

struct ExpectedCase {
    std::string file_name;
    std::string source_suffix;
    CaseKind kind;
    std::string entry;
    std::size_t capability_count;
    std::size_t scenario_count;
    WasmEligibility wasm;
};

void test_committed_cases(const std::filesystem::path &repo_root) {
    const std::vector<ExpectedCase> expected = {
        {
            "e1_identity_agent.case.json",
            "tests/golden/wasm/e1_identity_agent.ahfl",
            CaseKind::Agent,
            "wasm::e1_identity::IdentityAgent",
            0,
            1,
            WasmEligibility::Orchestration,
        },
        {
            "e2_capability_agent.case.json",
            "tests/golden/wasm/e2_capability_agent.ahfl",
            CaseKind::Agent,
            "wasm::e2_capability::CapabilityAgent",
            1,
            1,
            WasmEligibility::Orchestration,
        },
        {
            "e3_identity_workflow.case.json",
            "tests/golden/wasm/e3_identity_workflow.ahfl",
            CaseKind::Workflow,
            "wasm::e3_workflow::IdentityPipeline",
            0,
            1,
            WasmEligibility::Orchestration,
        },
        {
            "e3_capability_workflow.case.json",
            "tests/golden/wasm/e3_capability_workflow.ahfl",
            CaseKind::Workflow,
            "wasm::e3_capability_workflow::CapabilityPipeline",
            1,
            1,
            WasmEligibility::Orchestration,
        },
        {
            "enum_variant_e2e.case.json",
            "tests/golden/runtime/enum_variant_e2e.ahfl",
            CaseKind::Workflow,
            "runtime::enum_variant_e2e::TicketWorkflow",
            0,
            1,
            WasmEligibility::Computation,
        },
        {
            "if_let_e2e.case.json",
            "tests/golden/runtime/if_let_e2e.ahfl",
            CaseKind::Workflow,
            "runtime::if_let_e2e::IfLetWorkflow",
            0,
            2,
            WasmEligibility::Computation,
        },
        {
            "e2e_multi_agent.case.json",
            "tests/golden/runtime/e2e_multi_agent.ahfl",
            CaseKind::Workflow,
            "runtime::e2e_multi_agent::CustomerSupportWorkflow",
            4,
            2,
            WasmEligibility::Computation,
        },
    };

    const auto cases_dir = repo_root / "tests" / "conformance" / "cases";

    // Self-validating catalogue: scan the committed directory rather than
    // iterate a hand-maintained list. Any sidecar present on disk that the
    // battery does not know about (or an expected one that is absent) fails,
    // so a malformed or dangling newly committed case can never slip past
    // ahfl.conformance_case.
    std::vector<std::string> discovered;
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(cases_dir, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const auto &path = entry.path();
        if (is_conformance_case_sidecar(path)) {
            discovered.push_back(path.filename().string());
        }
    }
    check(!ec, "conformance cases directory scanned without error");
    std::sort(discovered.begin(), discovered.end());

    std::vector<std::string> expected_names;
    expected_names.reserve(expected.size());
    for (const auto &expectation : expected) {
        expected_names.push_back(expectation.file_name);
    }
    std::sort(expected_names.begin(), expected_names.end());
    check(discovered == expected_names,
          "discovered case set equals the expected catalogue (every committed case is exercised)");

    std::size_t loaded = 0;
    for (const auto &file_name : discovered) {
        const auto expectation =
            std::find_if(expected.begin(), expected.end(), [&](const ExpectedCase &candidate) {
                return candidate.file_name == file_name;
            });
        check(expectation != expected.end(), "case carries field expectations: " + file_name);
        if (expectation == expected.end()) {
            continue;
        }

        const auto sidecar = cases_dir / file_name;
        check(is_conformance_case_sidecar(sidecar),
              "sidecar suffix recognized: " + file_name);

        auto result = load_conformance_case(sidecar, repo_root);
        if (!result.has_errors() && result.conformance_case.has_value()) {
            ++loaded;
        } else {
            check(false, "load committed case: " + file_name);
            result.diagnostics.render(std::cerr);
            continue;
        }

        const ConformanceCase &manifest = result.conformance_case->manifest;
        check(manifest.kind == expectation->kind, "kind matches: " + file_name);
        check(manifest.entry == expectation->entry, "entry matches: " + file_name);
        check(manifest.source == expectation->source_suffix,
              "source matches: " + file_name);
        check(manifest.capabilities.size() == expectation->capability_count,
              "capability count: " + file_name);
        check(manifest.scenarios.size() == expectation->scenario_count,
              "scenario count: " + file_name);
        check(manifest.engines.evaluator, "evaluator enabled: " + file_name);
        check(manifest.engines.wasm.eligibility == expectation->wasm,
              "wasm eligibility: " + file_name);
        check(!manifest.engines.wasm.reason.empty(),
              "wasm skip reason present: " + file_name);

        for (const auto &scenario : manifest.scenarios) {
            check(!scenario.name.empty(), "scenario carries a name: " + file_name);
            check(!scenario.input_json.empty(),
                  "scenario '" + scenario.name + "' carries canonical input: " + file_name);
            check(scenario.expect.run_status == ExpectedRunStatus::Completed,
                  "expected completed run: " + file_name + "/" + scenario.name);
            if (manifest.kind == CaseKind::Agent) {
                check(!scenario.expect.state_sequence.empty(),
                      "agent scenario carries state sequence: " + file_name + "/" +
                          scenario.name);
            } else {
                check(scenario.expect.state_sequence.empty(),
                      "workflow scenario leaves state sequence empty: " + file_name + "/" +
                          scenario.name);
            }
        }

        check(result.conformance_case->source_path == (repo_root / expectation->source_suffix),
              "source path resolved under repo root: " + file_name);
    }
    check(loaded == discovered.size(), "all committed cases loaded");

    // Spot-check the richest case field-by-field: two routing scenarios over a
    // shared four-capability mock table.
    const auto multi = load_conformance_case(cases_dir / "e2e_multi_agent.case.json", repo_root);
    check(!multi.has_errors(), "multi-agent case reloads");
    if (!multi.has_errors()) {
        const auto &m = multi.conformance_case->manifest;
        check(m.scenarios.size() == 2, "multi-agent carries two routing scenarios");
        check(m.scenarios[0].name == "priority_low", "first scenario is priority_low");
        check(m.scenarios[1].name == "priority_high", "second scenario is priority_high");
        check(
            m.scenarios[0].input_json ==
                R"({"_type":"runtime::e2e_multi_agent::SupportRequest","message":"My server is crashing","priority":{"_enum":"runtime::e2e_multi_agent::Priority","_variant":"Low"},"user_id":"user_123"})",
            "low scenario canonical input bytes preserved");
        check(
            m.scenarios[1].input_json ==
                R"({"_type":"runtime::e2e_multi_agent::SupportRequest","message":"My server is crashing","priority":{"_enum":"runtime::e2e_multi_agent::Priority","_variant":"High"},"user_id":"user_456"})",
            "high scenario canonical input bytes preserved");
        check(m.scenarios[0].expect.capability_sequence.size() == 3,
              "low scenario invokes three capabilities");
        check(m.scenarios[0].expect.capability_sequence[1] ==
                    "runtime::e2e_multi_agent::HandleGeneral",
              "low scenario routes through HandleGeneral");
        check(m.scenarios[1].expect.capability_sequence[1] ==
                    "runtime::e2e_multi_agent::HandleTechnical",
              "high scenario routes through HandleTechnical");
        check(m.scenarios[0].expect.output_json.has_value(), "low scenario output present");
        check(
            *m.scenarios[0].expect.output_json ==
                R"({"_type":"runtime::e2e_multi_agent::SummaryResult","category":{"_enum":"runtime::e2e_multi_agent::Category","_variant":"Technical"},"resolved":true,"summary":"Case resolved successfully"})",
            "multi-agent canonical output bytes preserved");
        check(m.capabilities[0].status == CapabilityOutcomeStatus::Ok,
              "first capability status ok");
        check(m.capabilities[0].result_json.has_value(), "ok capability carries result_json");
    }

    // load_conformance_case fails closed for a missing source and a missing
    // sidecar.
    const auto missing_sidecar =
        load_conformance_case(cases_dir / "does_not_exist.case.json", repo_root);
    check(missing_sidecar.has_errors(), "missing sidecar rejected");
    check(diagnostics_contain(missing_sidecar.diagnostics, "cannot open conformance case sidecar"),
          "missing sidecar diagnostic");
}

// ---------------------------------------------------------------------------
// (c) malformed manifest rejections
// ---------------------------------------------------------------------------

constexpr std::string_view kValidCase = R"({
  "format_version": "ahfl.conformance-case.v1",
  "source": "tests/golden/wasm/e1_identity_agent.ahfl",
  "kind": "agent",
  "entry": "wasm::e1_identity::IdentityAgent",
  "scenarios": [
    {
      "name": "identity",
      "input": {"_type":"wasm::e1_identity::Frame","value":"identity"},
      "expect": {
        "run_status": "completed",
        "state_sequence": ["Start", "Done"],
        "capability_sequence": [],
        "output_json": {"_type":"wasm::e1_identity::Frame","value":"identity"}
      }
    }
  ],
  "capabilities": [],
  "engines": {
    "evaluator": true,
    "wasm": {"eligible": "orchestration", "reason": "E1 identity subset"}
  }
})";

void expect_rejected(std::string_view label,
                     std::string_view manifest,
                     std::string_view diagnostic_needle) {
    auto result = parse_conformance_case_json(manifest, label);
    check(result.has_errors(), std::string{"rejected: "} + std::string{label});
    check(!result.conformance_case.has_value(),
          std::string{"no case on error: "} + std::string{label});
    check(diagnostics_contain(result.diagnostics, diagnostic_needle),
          std::string{"diagnostic '"} + std::string{diagnostic_needle} +
              "' for: " + std::string{label});
}

// A minimal scenario object embedded in the inline rejection manifests.
constexpr std::string_view kScenarioEcho = R"(
    {
      "name": "echo",
      "input": {"_type":"wasm::e2_capability::InputFrame","value":"input"},
      "expect": {
        "run_status": "completed",
        "state_sequence": ["Start", "Done"],
        "capability_sequence": ["wasm::e2_capability::Echo"],
        "output_json": {"_type":"wasm::e2_capability::OutputFrame","value":"echo"}
      }
    })";

constexpr std::string_view kScenarioIdentity = R"(
    {
      "name": "identity",
      "input": {"_type":"wasm::e1_identity::Frame","value":"identity"},
      "expect": {
        "run_status": "completed",
        "state_sequence": ["Start", "Done"],
        "capability_sequence": [],
        "output_json": {"_type":"wasm::e1_identity::Frame","value":"identity"}
      }
    })";

void test_malformed_manifests() {
    expect_rejected("malformed json", "{ not json", "valid JSON");

    {
        std::string manifest(kValidCase);
        const std::string line =
            std::string{R"(  "entry": "wasm::e1_identity::IdentityAgent",)"} + "\n";
        const auto removed = manifest.find(line);
        check(removed != std::string::npos, "test harness found entry line");
        if (removed != std::string::npos) {
            manifest.erase(removed, line.size());
            expect_rejected("missing entry", manifest, "missing required field 'entry'");
        }
    }

    expect_rejected("bad capability status enum",
                    R"({
  "format_version": "ahfl.conformance-case.v1",
  "source": "tests/golden/wasm/e2_capability_agent.ahfl",
  "kind": "agent",
  "entry": "wasm::e2_capability::CapabilityAgent",
  "scenarios": [)" + std::string{kScenarioEcho} + R"(
  ],
  "capabilities": [{"name": "wasm::e2_capability::Echo", "status": "exploded"}],
  "engines": {
    "evaluator": true,
    "wasm": {"eligible": "orchestration", "reason": "E2"}
  }
})",
                    "capability status must be 'ok', 'error', or 'pending'");

    expect_rejected("output json not canonical (whitespace)",
                    R"({
  "format_version": "ahfl.conformance-case.v1",
  "source": "tests/golden/wasm/e1_identity_agent.ahfl",
  "kind": "agent",
  "entry": "wasm::e1_identity::IdentityAgent",
  "scenarios": [
    {
      "name": "identity",
      "input": {"_type":"wasm::e1_identity::Frame","value":"identity"},
      "expect": {
        "run_status": "completed",
        "state_sequence": ["Start", "Done"],
        "capability_sequence": [],
        "output_json": {"_type": "wasm::e1_identity::Frame", "value": "identity"}
      }
    }
  ],
  "capabilities": [],
  "engines": {
    "evaluator": true,
    "wasm": {"eligible": "orchestration", "reason": "E1"}
  }
})",
                    "expect.output_json' must be canonical compact wire JSON");

    expect_rejected("output json field order shuffled",
                    R"({
  "format_version": "ahfl.conformance-case.v1",
  "source": "tests/golden/wasm/e1_identity_agent.ahfl",
  "kind": "agent",
  "entry": "wasm::e1_identity::IdentityAgent",
  "scenarios": [
    {
      "name": "identity",
      "input": {"_type":"wasm::e1_identity::Frame","value":"identity"},
      "expect": {
        "run_status": "completed",
        "state_sequence": ["Start", "Done"],
        "capability_sequence": [],
        "output_json": {"value":"identity","_type":"wasm::e1_identity::Frame"}
      }
    }
  ],
  "capabilities": [],
  "engines": {
    "evaluator": true,
    "wasm": {"eligible": "orchestration", "reason": "E1"}
  }
})",
                    "expect.output_json' must be canonical compact wire JSON");

    expect_rejected("engines block absent",
                    R"({
  "format_version": "ahfl.conformance-case.v1",
  "source": "tests/golden/wasm/e1_identity_agent.ahfl",
  "kind": "agent",
  "entry": "wasm::e1_identity::IdentityAgent",
  "scenarios": [)" + std::string{kScenarioIdentity} + R"(
  ],
  "capabilities": []
})",
                    "missing required field 'engines'");

    expect_rejected("unknown top-level field",
                    R"({
  "format_version": "ahfl.conformance-case.v1",
  "source": "tests/golden/wasm/e1_identity_agent.ahfl",
  "kind": "agent",
  "entry": "wasm::e1_identity::IdentityAgent",
  "scenarios": [)" + std::string{kScenarioIdentity} + R"(
  ],
  "capabilities": [],
  "engines": {
    "evaluator": true,
    "wasm": {"eligible": "orchestration", "reason": "E1"}
  },
  "surprise": 1
})",
                    "unsupported conformance case field 'surprise'");

    expect_rejected("unsupported format version",
                    R"({
  "format_version": "ahfl.conformance-case.v9",
  "source": "tests/golden/wasm/e1_identity_agent.ahfl",
  "kind": "agent",
  "entry": "wasm::e1_identity::IdentityAgent",
  "scenarios": [
    {
      "name": "identity",
      "input": {"_type":"wasm::e1_identity::Frame","value":"identity"},
      "expect": {"run_status": "completed", "state_sequence": ["Start"],
                 "capability_sequence": []}
    }
  ],
  "capabilities": [],
  "engines": {"evaluator": true,
              "wasm": {"eligible": "orchestration", "reason": "E1"}}
})",
                    "unsupported conformance case format_version");

    expect_rejected("bad kind enum",
                    R"({
  "format_version": "ahfl.conformance-case.v1",
  "source": "tests/golden/wasm/e1_identity_agent.ahfl",
  "kind": "daemon",
  "entry": "x",
  "scenarios": [
    {"name": "s", "input": {},
     "expect": {"run_status": "completed", "state_sequence": [],
                "capability_sequence": []}}
  ],
  "capabilities": [],
  "engines": {"evaluator": true,
              "wasm": {"eligible": "none", "reason": "r"}}
})",
                    "field 'kind' must be 'agent' or 'workflow'");

    expect_rejected("bad run status enum",
                    R"({
  "format_version": "ahfl.conformance-case.v1",
  "source": "tests/golden/wasm/e1_identity_agent.ahfl",
  "kind": "agent",
  "entry": "x",
  "scenarios": [
    {"name": "s", "input": {},
     "expect": {"run_status": "wednesday", "state_sequence": ["Start"],
                "capability_sequence": []}}
  ],
  "capabilities": [],
  "engines": {"evaluator": true,
              "wasm": {"eligible": "none", "reason": "r"}}
})",
                    "expect.run_status' must be 'completed', 'suspended', or 'failed'");

    expect_rejected("bad wasm eligibility enum",
                    R"({
  "format_version": "ahfl.conformance-case.v1",
  "source": "tests/golden/wasm/e1_identity_agent.ahfl",
  "kind": "agent",
  "entry": "x",
  "scenarios": [
    {"name": "s", "input": {},
     "expect": {"run_status": "completed", "state_sequence": ["Start"],
                "capability_sequence": []}}
  ],
  "capabilities": [],
  "engines": {"evaluator": true,
              "wasm": {"eligible": "both", "reason": "r"}}
})",
                    "engines.wasm.eligible' must be 'orchestration', 'computation', or 'none'");

    expect_rejected("missing wasm reason",
                    R"({
  "format_version": "ahfl.conformance-case.v1",
  "source": "tests/golden/wasm/e1_identity_agent.ahfl",
  "kind": "agent",
  "entry": "x",
  "scenarios": [
    {"name": "s", "input": {},
     "expect": {"run_status": "completed", "state_sequence": ["Start"],
                "capability_sequence": []}}
  ],
  "capabilities": [],
  "engines": {"evaluator": true, "wasm": {"eligible": "none"}}
})",
                    "engines.wasm.reason' is required");

    expect_rejected("pending capability carries result",
                    R"({
  "format_version": "ahfl.conformance-case.v1",
  "source": "tests/golden/wasm/e2_capability_agent.ahfl",
  "kind": "agent",
  "entry": "x",
  "scenarios": [
    {"name": "s", "input": {},
     "expect": {"run_status": "suspended", "state_sequence": ["Start"],
                "capability_sequence": ["wasm::e2_capability::Echo"]}}
  ],
  "capabilities": [{"name": "wasm::e2_capability::Echo", "status": "pending",
                    "result_json": {"_type":"wasm::e2_capability::OutputFrame","value":"x"}}],
  "engines": {"evaluator": true,
              "wasm": {"eligible": "none", "reason": "r"}}
})",
                    "status 'pending' must not carry 'result_json'");

    expect_rejected("ok capability missing result frame",
                    R"({
  "format_version": "ahfl.conformance-case.v1",
  "source": "tests/golden/wasm/e2_capability_agent.ahfl",
  "kind": "agent",
  "entry": "x",
  "scenarios": [
    {"name": "s", "input": {},
     "expect": {"run_status": "completed", "state_sequence": ["Start"],
                "capability_sequence": ["wasm::e2_capability::Echo"]}}
  ],
  "capabilities": [{"name": "wasm::e2_capability::Echo", "status": "ok"}],
  "engines": {"evaluator": true,
              "wasm": {"eligible": "orchestration", "reason": "r"}}
})",
                    "status 'ok' must carry its result frame in 'result_json'");

    expect_rejected("duplicate capability entry",
                    R"({
  "format_version": "ahfl.conformance-case.v1",
  "source": "tests/golden/wasm/e2_capability_agent.ahfl",
  "kind": "agent",
  "entry": "x",
  "scenarios": [
    {"name": "s", "input": {},
     "expect": {"run_status": "completed", "state_sequence": ["Start"],
                "capability_sequence": ["wasm::e2_capability::Echo"]}}
  ],
  "capabilities": [
    {"name": "wasm::e2_capability::Echo", "status": "ok",
     "result_json": {"_type":"wasm::e2_capability::OutputFrame","value":"x"}},
    {"name": "wasm::e2_capability::Echo", "status": "error"}
  ],
  "engines": {"evaluator": true,
              "wasm": {"eligible": "orchestration", "reason": "r"}}
})",
                    "lists capability 'wasm::e2_capability::Echo' more than once");

    expect_rejected("invoked capability not configured",
                    R"({
  "format_version": "ahfl.conformance-case.v1",
  "source": "tests/golden/wasm/e2_capability_agent.ahfl",
  "kind": "agent",
  "entry": "x",
  "scenarios": [
    {"name": "s", "input": {},
     "expect": {"run_status": "completed", "state_sequence": ["Start"],
                "capability_sequence": ["wasm::e2_capability::Echo"]}}
  ],
  "capabilities": [],
  "engines": {"evaluator": true,
              "wasm": {"eligible": "orchestration", "reason": "r"}}
})",
                    "invokes capability 'wasm::e2_capability::Echo' that has no entry");

    expect_rejected("workflow must not carry a state sequence",
                    R"({
  "format_version": "ahfl.conformance-case.v1",
  "source": "tests/golden/wasm/e3_identity_workflow.ahfl",
  "kind": "workflow",
  "entry": "x",
  "scenarios": [
    {"name": "s", "input": {},
     "expect": {"run_status": "completed",
                "state_sequence": ["first", "second"],
                "capability_sequence": []}}
  ],
  "capabilities": [],
  "engines": {"evaluator": true,
              "wasm": {"eligible": "orchestration", "reason": "r"}}
})",
                    "must leave 'expect.state_sequence' empty");

    expect_rejected("source path escapes repository",
                    R"({
  "format_version": "ahfl.conformance-case.v1",
  "source": "../secret.ahfl",
  "kind": "agent",
  "entry": "x",
  "scenarios": [
    {"name": "s", "input": {},
     "expect": {"run_status": "completed", "state_sequence": ["Start"],
                "capability_sequence": []}}
  ],
  "capabilities": [],
  "engines": {"evaluator": true,
              "wasm": {"eligible": "none", "reason": "r"}}
})",
                    "must not escape the repository");

    expect_rejected("absolute source path rejected",
                    R"({
  "format_version": "ahfl.conformance-case.v1",
  "source": "/etc/passwd.ahfl",
  "kind": "agent",
  "entry": "x",
  "scenarios": [
    {"name": "s", "input": {},
     "expect": {"run_status": "completed", "state_sequence": ["Start"],
                "capability_sequence": []}}
  ],
  "capabilities": [],
  "engines": {"evaluator": true,
              "wasm": {"eligible": "none", "reason": "r"}}
})",
                    "must be a repo-relative path");

    expect_rejected("missing state sequence field",
                    R"({
  "format_version": "ahfl.conformance-case.v1",
  "source": "tests/golden/wasm/e1_identity_agent.ahfl",
  "kind": "agent",
  "entry": "wasm::e1_identity::IdentityAgent",
  "scenarios": [
    {
      "name": "identity",
      "input": {"_type":"wasm::e1_identity::Frame","value":"identity"},
      "expect": {"run_status": "completed", "capability_sequence": []}
    }
  ],
  "capabilities": [],
  "engines": {
    "evaluator": true,
    "wasm": {"eligible": "orchestration", "reason": "E1"}
  }
})",
                    "missing required field");

    expect_rejected("scenarios block absent",
                    R"({
  "format_version": "ahfl.conformance-case.v1",
  "source": "tests/golden/wasm/e1_identity_agent.ahfl",
  "kind": "agent",
  "entry": "wasm::e1_identity::IdentityAgent",
  "capabilities": [],
  "engines": {
    "evaluator": true,
    "wasm": {"eligible": "orchestration", "reason": "E1"}
  }
})",
                    "missing required field 'scenarios'");

    expect_rejected("empty scenarios array",
                    R"({
  "format_version": "ahfl.conformance-case.v1",
  "source": "tests/golden/wasm/e1_identity_agent.ahfl",
  "kind": "agent",
  "entry": "wasm::e1_identity::IdentityAgent",
  "scenarios": [],
  "capabilities": [],
  "engines": {
    "evaluator": true,
    "wasm": {"eligible": "orchestration", "reason": "E1"}
  }
})",
                    "must contain at least one scenario");

    expect_rejected("duplicate scenario name",
                    R"({
  "format_version": "ahfl.conformance-case.v1",
  "source": "tests/golden/wasm/e1_identity_agent.ahfl",
  "kind": "agent",
  "entry": "wasm::e1_identity::IdentityAgent",
  "scenarios": [
    {"name": "same", "input": {},
     "expect": {"run_status": "completed", "state_sequence": ["Start"],
                "capability_sequence": []}},
    {"name": "same", "input": {},
     "expect": {"run_status": "completed", "state_sequence": ["Done"],
                "capability_sequence": []}}
  ],
  "capabilities": [],
  "engines": {"evaluator": true,
              "wasm": {"eligible": "orchestration", "reason": "E1"}}
})",
                    "lists scenario 'same' more than once");

    expect_rejected("anonymous scenario",
                    R"({
  "format_version": "ahfl.conformance-case.v1",
  "source": "tests/golden/wasm/e1_identity_agent.ahfl",
  "kind": "agent",
  "entry": "wasm::e1_identity::IdentityAgent",
  "scenarios": [
    {"input": {},
     "expect": {"run_status": "completed", "state_sequence": ["Start"],
                "capability_sequence": []}}
  ],
  "capabilities": [],
  "engines": {"evaluator": true,
              "wasm": {"eligible": "orchestration", "reason": "E1"}}
})",
                    "scenario is missing required field 'name'");

    expect_rejected("scenario missing input",
                    R"({
  "format_version": "ahfl.conformance-case.v1",
  "source": "tests/golden/wasm/e1_identity_agent.ahfl",
  "kind": "agent",
  "entry": "wasm::e1_identity::IdentityAgent",
  "scenarios": [
    {"name": "s",
     "expect": {"run_status": "completed", "state_sequence": ["Start"],
                "capability_sequence": []}}
  ],
  "capabilities": [],
  "engines": {"evaluator": true,
              "wasm": {"eligible": "orchestration", "reason": "E1"}}
})",
                    "is missing required field 'input'");

    expect_rejected("unknown scenario field",
                    R"({
  "format_version": "ahfl.conformance-case.v1",
  "source": "tests/golden/wasm/e1_identity_agent.ahfl",
  "kind": "agent",
  "entry": "wasm::e1_identity::IdentityAgent",
  "scenarios": [
    {"name": "s", "input": {}, "bogus": 1,
     "expect": {"run_status": "completed", "state_sequence": ["Start"],
                "capability_sequence": []}}
  ],
  "capabilities": [],
  "engines": {"evaluator": true,
              "wasm": {"eligible": "orchestration", "reason": "E1"}}
})",
                    "unsupported conformance case field 'scenarios[].bogus'");
}

// ---------------------------------------------------------------------------
// (d) canonicality gate semantics + load with a dangling source
// ---------------------------------------------------------------------------

void test_canonicality_gate() {
    auto accepted = parse_conformance_case_json(kValidCase, "canonical-ok");
    check(!accepted.has_errors(), "compact ordered manifest accepted");

    const std::string noncanonical_input = R"({
  "format_version": "ahfl.conformance-case.v1",
  "source": "tests/golden/wasm/e1_identity_agent.ahfl",
  "kind": "agent",
  "entry": "wasm::e1_identity::IdentityAgent",
  "scenarios": [
    {
      "name": "identity",
      "input": { "_type": "wasm::e1_identity::Frame", "value": "identity" },
      "expect": {
        "run_status": "completed",
        "state_sequence": ["Start", "Done"],
        "capability_sequence": [],
        "output_json": {"_type":"wasm::e1_identity::Frame","value":"identity"}
      }
    }
  ],
  "capabilities": [],
  "engines": {"evaluator": true,
              "wasm": {"eligible": "orchestration", "reason": "E1"}}
})";
    auto rejected = parse_conformance_case_json(noncanonical_input, "noncanonical-input");
    check(rejected.has_errors(), "whitespace inside input fragment rejected");
    check(diagnostics_contain(rejected.diagnostics,
                              "field 'scenarios[].input' must be canonical compact wire JSON"),
          "input canonicality diagnostic names the field");
}

// The canonical float spelling is the runtime value_to_json SSOT: an integral
// float keeps ".0" (the input wire codec rejects integer tokens at Float
// nodes), and there is exactly one canonical spelling per value.
void test_float_canonicality_gate() {
    const auto case_with_input = [](std::string_view input_fragment) {
        return std::string{R"({
  "format_version": "ahfl.conformance-case.v1",
  "source": "tests/golden/wasm/e1_identity_agent.ahfl",
  "kind": "agent",
  "entry": "wasm::e1_identity::IdentityAgent",
  "scenarios": [
    {
      "name": "s",
      "input": )"} + std::string{input_fragment} + R"(,
      "expect": {
        "run_status": "completed",
        "state_sequence": ["Start", "Done"],
        "capability_sequence": []
      }
    }
  ],
  "capabilities": [],
  "engines": {"evaluator": true,
              "wasm": {"eligible": "orchestration", "reason": "E1"}}
})";
    };

    check(!parse_conformance_case_json(case_with_input(R"({"x":1.0})"), "float-1.0").has_errors(),
          "integral float 1.0 accepted (SSOT keeps the decimal point)");
    check(!parse_conformance_case_json(case_with_input(R"({"x":100.0})"), "float-100.0")
               .has_errors(),
          "integral float 100.0 accepted");
    check(!parse_conformance_case_json(case_with_input(R"({"x":1.5})"), "float-1.5").has_errors(),
          "fractional float 1.5 accepted");
    check(!parse_conformance_case_json(case_with_input(R"({"x":1e+20})"), "float-exp").has_errors(),
          "exponential float 1e+20 accepted in its shortest-round-trip spelling");

    // 1e2 is a synonym of 100.0; the single-canonical-encoding guarantee pins
    // the SSOT spelling "100.0", so the exponent form is non-canonical here.
    {
        auto result = parse_conformance_case_json(case_with_input(R"({"x":1e2})"), "float-1e2");
        check(result.has_errors(), "1e2 rejected: canonical spelling of 100.0 is 100.0");
        check(diagnostics_contain(result.diagnostics, "must be canonical compact wire JSON"),
              "1e2 canonicality diagnostic");
    }
}

// value_to_json omits an enum's `_payload` / `_named_payload` when empty, so
// an explicitly-present empty container is a second, non-canonical spelling
// and must be rejected; the bare unit-enum form is the canonical one.
void test_enum_empty_payload_gate() {
    const auto case_with_input = [](std::string_view input_fragment) {
        return std::string{R"({
  "format_version": "ahfl.conformance-case.v1",
  "source": "tests/golden/wasm/e1_identity_agent.ahfl",
  "kind": "agent",
  "entry": "wasm::e1_identity::IdentityAgent",
  "scenarios": [
    {
      "name": "s",
      "input": )"} + std::string{input_fragment} + R"(,
      "expect": {
        "run_status": "completed",
        "state_sequence": ["Start", "Done"],
        "capability_sequence": []
      }
    }
  ],
  "capabilities": [],
  "engines": {"evaluator": true,
              "wasm": {"eligible": "orchestration", "reason": "E1"}}
})";
    };

    check(!parse_conformance_case_json(
                   case_with_input(R"({"_enum":"E","_variant":"V"})"), "enum-bare")
               .has_errors(),
          "unit enum without payload containers accepted");
    check(!parse_conformance_case_json(
                   case_with_input(R"({"_enum":"E","_variant":"V","_payload":[1.0]})"),
                   "enum-payload")
               .has_errors(),
          "non-empty positional payload accepted");

    for (const auto *fragment : {R"({"_enum":"E","_variant":"V","_payload":[]})",
                                 R"({"_enum":"E","_variant":"V","_named_payload":{}})"}) {
        auto result = parse_conformance_case_json(case_with_input(fragment), "enum-empty");
        check(result.has_errors(),
              std::string{"empty enum payload container rejected: "} + fragment);
        check(diagnostics_contain(result.diagnostics, "must be canonical compact wire JSON"),
              "empty enum container canonicality diagnostic");
    }
}

void test_dangling_source_rejected(const std::filesystem::path &scratch_dir) {
    const std::string manifest = R"({
  "format_version": "ahfl.conformance-case.v1",
  "source": "no_such_source.ahfl",
  "kind": "agent",
  "entry": "ghost::Agent",
  "scenarios": [
    {"name": "s", "input": {},
     "expect": {"run_status": "completed", "state_sequence": ["Start"],
                "capability_sequence": []}}
  ],
  "capabilities": [],
  "engines": {"evaluator": true,
              "wasm": {"eligible": "none", "reason": "host-only"}}
})";
    auto parsed = parse_conformance_case_json(manifest, "dangling");
    check(!parsed.has_errors(), "dangling source is schema-valid");
    check(parsed.conformance_case.has_value(), "dangling manifest parses");

    // load_conformance_case adds the filesystem existence gate. The scratch
    // sidecar lives under the per-test build directory (never in the committed
    // cases catalogue, which is globbed as the case set), so an interrupted
    // run cannot leave a broken extra case inside the source tree.
    std::error_code error;
    std::filesystem::create_directories(scratch_dir, error);
    check(!error, "scratch directory created for dangling-source sidecar");
    const auto sidecar = scratch_dir / "dangling.tmp.case.json";
    {
        std::ofstream out(sidecar, std::ios::binary | std::ios::trunc);
        out << manifest;
    }
    // The manifest's repo-relative source is resolved against the scratch
    // directory (which contains no such file): the load must fail closed.
    auto loaded = load_conformance_case(sidecar, scratch_dir);
    check(loaded.has_errors(), "load rejects dangling source");
    check(diagnostics_contain(loaded.diagnostics, "does not exist relative to the repository root"),
          "dangling source diagnostic");
    std::filesystem::remove(sidecar, error);
}

// Every top-level field in the design-doc contract is mandatory; a manifest
// omitting `capabilities` (which the design marks required) must be rejected.
void test_capabilities_required() {
    expect_rejected("capabilities block absent",
                    R"({
  "format_version": "ahfl.conformance-case.v1",
  "source": "tests/golden/wasm/e1_identity_agent.ahfl",
  "kind": "agent",
  "entry": "wasm::e1_identity::IdentityAgent",
  "scenarios": [
    {
      "name": "identity",
      "input": {"_type":"wasm::e1_identity::Frame","value":"identity"},
      "expect": {
        "run_status": "completed",
        "state_sequence": ["Start", "Done"],
        "capability_sequence": [],
        "output_json": {"_type":"wasm::e1_identity::Frame","value":"identity"}
      }
    }
  ],
  "engines": {
    "evaluator": true,
    "wasm": {"eligible": "orchestration", "reason": "E1"}
  }
})",
                    "missing required field 'capabilities'");
}

} // namespace

int main(int argc, char **argv) {
    const std::filesystem::path repo_root{AHFL_SOURCE_DIR};
    if (argc < 2) {
        std::cerr << "usage: conformance_case_tests <scratch-dir>\n";
        return 2;
    }
    const std::filesystem::path scratch_dir{argv[1]};

    test_committed_cases(repo_root);
    test_malformed_manifests();
    test_canonicality_gate();
    test_float_canonicality_gate();
    test_enum_empty_payload_gate();
    test_capabilities_required();
    test_dangling_source_rejected(scratch_dir);

    if (g_failures != 0) {
        std::cerr << g_failures << " conformance case test(s) failed\n";
        return 1;
    }
    std::cout << "all conformance case manifest tests passed\n";
    return 0;
}
