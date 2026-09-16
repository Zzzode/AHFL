// KR6.7 (RFC 0026 P7): engine-independent conformance case manifest schema
// validator. Hand-rolled check()/main(), mirroring the other RFC 0026 slice
// tests. No engine is linked or invoked.
//
// Coverage (per slice spec):
//   (a) every committed sidecar under tests/conformance/cases loads and every
//       one references an existing AHFL source (the 4 tests/golden/wasm and 3
//       tests/golden/runtime fixtures);
//   (b) loaded manifests round-trip the expected name-only contract: kind,
//       entry, canonical wire input, capability outcomes, expectations, and
//       wasm eligibility metadata;
//   (c) malformed manifests are rejected with precise diagnostics:
//       missing entry, bad capability status enum, non-canonical
//       expect.output_json, absent engines block, unknown field, plus
//       format_version / kind / run_status / eligibility enum / pending+result
//       / duplicate capability / unconfigured invoked capability / workflow
//       state-sequence / source-path escapes;
//   (d) the canonicality gate distinguishes a compact correctly-ordered wire
//       fragment from one carrying whitespace or a shuffled struct field.

#include "conformance/conformance_case.hpp"

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
    std::size_t capability_sequence_size;
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
            0,
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
            0,
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
            0,
            WasmEligibility::Computation,
        },
        {
            "if_let_e2e.case.json",
            "tests/golden/runtime/if_let_e2e.ahfl",
            CaseKind::Workflow,
            "runtime::if_let_e2e::IfLetWorkflow",
            0,
            0,
            WasmEligibility::Computation,
        },
        {
            "e2e_multi_agent.case.json",
            "tests/golden/runtime/e2e_multi_agent.ahfl",
            CaseKind::Workflow,
            "runtime::e2e_multi_agent::CustomerSupportWorkflow",
            3,
            3,
            WasmEligibility::Computation,
        },
    };

    const auto cases_dir = repo_root / "tests" / "conformance" / "cases";
    std::size_t loaded = 0;
    for (const auto &expectation : expected) {
        const auto sidecar = cases_dir / expectation.file_name;
        check(is_conformance_case_sidecar(sidecar),
              "sidecar suffix recognized: " + expectation.file_name);

        auto result = load_conformance_case(sidecar, repo_root);
        if (!result.has_errors() && result.conformance_case.has_value()) {
            ++loaded;
        } else {
            check(false, "load committed case: " + expectation.file_name);
            result.diagnostics.render(std::cerr);
            continue;
        }

        const ConformanceCase &manifest = result.conformance_case->manifest;
        check(manifest.kind == expectation.kind, "kind matches: " + expectation.file_name);
        check(manifest.entry == expectation.entry, "entry matches: " + expectation.file_name);
        check(manifest.source == expectation.source_suffix,
              "source matches: " + expectation.file_name);
        check(manifest.capabilities.size() == expectation.capability_count,
              "capability count: " + expectation.file_name);
        check(manifest.expect.capability_sequence.size() == expectation.capability_sequence_size,
              "capability sequence size: " + expectation.file_name);
        check(manifest.engines.evaluator, "evaluator enabled: " + expectation.file_name);
        check(manifest.engines.wasm.eligibility == expectation.wasm,
              "wasm eligibility: " + expectation.file_name);
        check(!manifest.engines.wasm.reason.empty(),
              "wasm skip reason present: " + expectation.file_name);
        check(manifest.expect.run_status == ExpectedRunStatus::Completed,
              "expected completed run: " + expectation.file_name);

        if (manifest.kind == CaseKind::Agent) {
            check(!manifest.expect.state_sequence.empty(),
                  "agent carries state sequence: " + expectation.file_name);
        } else {
            check(manifest.expect.state_sequence.empty(),
                  "workflow leaves state sequence empty: " + expectation.file_name);
        }

        check(result.conformance_case->source_path == (repo_root / expectation.source_suffix),
              "source path resolved under repo root: " + expectation.file_name);
    }
    check(loaded == expected.size(), "all seven committed cases loaded");

    // Spot-check the richest case field-by-field.
    const auto multi = load_conformance_case(cases_dir / "e2e_multi_agent.case.json", repo_root);
    check(!multi.has_errors(), "multi-agent case reloads");
    if (!multi.has_errors()) {
        const auto &m = multi.conformance_case->manifest;
        check(
            m.input_json ==
                R"({"_type":"runtime::e2e_multi_agent::SupportRequest","message":"My server is crashing","priority":{"_enum":"runtime::e2e_multi_agent::Priority","_variant":"Low"},"user_id":"user_123"})",
            "multi-agent canonical input bytes preserved");
        check(m.expect.output_json.has_value(), "multi-agent output present");
        check(
            *m.expect.output_json ==
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
  "input": {"_type":"wasm::e1_identity::Frame","value":"identity"},
  "capabilities": [],
  "expect": {
    "run_status": "completed",
    "state_sequence": ["Start", "Done"],
    "capability_sequence": [],
    "output_json": {"_type":"wasm::e1_identity::Frame","value":"identity"}
  },
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
  "input": {"_type":"wasm::e2_capability::InputFrame","value":"input"},
  "capabilities": [{"name": "wasm::e2_capability::Echo", "status": "exploded"}],
  "expect": {
    "run_status": "completed",
    "state_sequence": ["Start", "Done"],
    "capability_sequence": ["wasm::e2_capability::Echo"],
    "output_json": {"_type":"wasm::e2_capability::OutputFrame","value":"echo"}
  },
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
  "input": {"_type":"wasm::e1_identity::Frame","value":"identity"},
  "capabilities": [],
  "expect": {
    "run_status": "completed",
    "state_sequence": ["Start", "Done"],
    "capability_sequence": [],
    "output_json": {"_type": "wasm::e1_identity::Frame", "value": "identity"}
  },
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
  "input": {"_type":"wasm::e1_identity::Frame","value":"identity"},
  "capabilities": [],
  "expect": {
    "run_status": "completed",
    "state_sequence": ["Start", "Done"],
    "capability_sequence": [],
    "output_json": {"value":"identity","_type":"wasm::e1_identity::Frame"}
  },
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
  "input": {"_type":"wasm::e1_identity::Frame","value":"identity"},
  "capabilities": [],
  "expect": {
    "run_status": "completed",
    "state_sequence": ["Start", "Done"],
    "capability_sequence": [],
    "output_json": {"_type":"wasm::e1_identity::Frame","value":"identity"}
  }
})",
                    "missing required field 'engines'");

    expect_rejected("unknown top-level field",
                    R"({
  "format_version": "ahfl.conformance-case.v1",
  "source": "tests/golden/wasm/e1_identity_agent.ahfl",
  "kind": "agent",
  "entry": "wasm::e1_identity::IdentityAgent",
  "input": {"_type":"wasm::e1_identity::Frame","value":"identity"},
  "capabilities": [],
  "expect": {
    "run_status": "completed",
    "state_sequence": ["Start", "Done"],
    "capability_sequence": [],
    "output_json": {"_type":"wasm::e1_identity::Frame","value":"identity"}
  },
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
  "input": {"_type":"wasm::e1_identity::Frame","value":"identity"},
  "capabilities": [],
  "expect": {"run_status": "completed", "state_sequence": ["Start"],
             "capability_sequence": []},
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
  "input": {},
  "capabilities": [],
  "expect": {"run_status": "completed", "state_sequence": [],
             "capability_sequence": []},
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
  "input": {},
  "capabilities": [],
  "expect": {"run_status": "wednesday", "state_sequence": ["Start"],
             "capability_sequence": []},
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
  "input": {},
  "capabilities": [],
  "expect": {"run_status": "completed", "state_sequence": ["Start"],
             "capability_sequence": []},
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
  "input": {},
  "capabilities": [],
  "expect": {"run_status": "completed", "state_sequence": ["Start"],
             "capability_sequence": []},
  "engines": {"evaluator": true, "wasm": {"eligible": "none"}}
})",
                    "engines.wasm.reason' is required");

    expect_rejected("pending capability carries result",
                    R"({
  "format_version": "ahfl.conformance-case.v1",
  "source": "tests/golden/wasm/e2_capability_agent.ahfl",
  "kind": "agent",
  "entry": "x",
  "input": {},
  "capabilities": [{"name": "wasm::e2_capability::Echo", "status": "pending",
                    "result_json": {"_type":"wasm::e2_capability::OutputFrame","value":"x"}}],
  "expect": {"run_status": "suspended", "state_sequence": ["Start"],
             "capability_sequence": ["wasm::e2_capability::Echo"]},
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
  "input": {},
  "capabilities": [{"name": "wasm::e2_capability::Echo", "status": "ok"}],
  "expect": {"run_status": "completed", "state_sequence": ["Start"],
             "capability_sequence": ["wasm::e2_capability::Echo"]},
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
  "input": {},
  "capabilities": [
    {"name": "wasm::e2_capability::Echo", "status": "ok",
     "result_json": {"_type":"wasm::e2_capability::OutputFrame","value":"x"}},
    {"name": "wasm::e2_capability::Echo", "status": "error"}
  ],
  "expect": {"run_status": "completed", "state_sequence": ["Start"],
             "capability_sequence": ["wasm::e2_capability::Echo"]},
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
  "input": {},
  "capabilities": [],
  "expect": {"run_status": "completed", "state_sequence": ["Start"],
             "capability_sequence": ["wasm::e2_capability::Echo"]},
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
  "input": {},
  "capabilities": [],
  "expect": {"run_status": "completed",
             "state_sequence": ["first", "second"],
             "capability_sequence": []},
  "engines": {"evaluator": true,
              "wasm": {"eligible": "orchestration", "reason": "r"}}
})",
                    "kind 'workflow' must leave 'expect.state_sequence' empty");

    expect_rejected("source path escapes repository",
                    R"({
  "format_version": "ahfl.conformance-case.v1",
  "source": "../secret.ahfl",
  "kind": "agent",
  "entry": "x",
  "input": {},
  "capabilities": [],
  "expect": {"run_status": "completed", "state_sequence": ["Start"],
             "capability_sequence": []},
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
  "input": {},
  "capabilities": [],
  "expect": {"run_status": "completed", "state_sequence": ["Start"],
             "capability_sequence": []},
  "engines": {"evaluator": true,
              "wasm": {"eligible": "none", "reason": "r"}}
})",
                    "must be a repo-relative path");

    expect_rejected("missing state sequence field",
                    R"({
  "format_version": "ahfl.conformance-case.v1",
  "source": "tests/golden/wasm/e1_identity_agent.ahfl",
  "kind": "agent",
  "entry": "x",
  "input": {},
  "capabilities": [],
  "expect": {"run_status": "completed", "capability_sequence": []},
  "engines": {"evaluator": true,
              "wasm": {"eligible": "none", "reason": "r"}}
})",
                    "missing required field");
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
  "input": { "_type": "wasm::e1_identity::Frame", "value": "identity" },
  "capabilities": [],
  "expect": {
    "run_status": "completed",
    "state_sequence": ["Start", "Done"],
    "capability_sequence": [],
    "output_json": {"_type":"wasm::e1_identity::Frame","value":"identity"}
  },
  "engines": {"evaluator": true,
              "wasm": {"eligible": "orchestration", "reason": "E1"}}
})";
    auto rejected = parse_conformance_case_json(noncanonical_input, "noncanonical-input");
    check(rejected.has_errors(), "whitespace inside input fragment rejected");
    check(diagnostics_contain(rejected.diagnostics,
                              "field 'input' must be canonical compact wire JSON"),
          "input canonicality diagnostic names the field");
}

void test_dangling_source_rejected(const std::filesystem::path &repo_root) {
    const std::string manifest = R"({
  "format_version": "ahfl.conformance-case.v1",
  "source": "tests/conformance/cases/no_such_source.ahfl",
  "kind": "agent",
  "entry": "ghost::Agent",
  "input": {},
  "capabilities": [],
  "expect": {"run_status": "completed", "state_sequence": ["Start"],
             "capability_sequence": []},
  "engines": {"evaluator": true,
              "wasm": {"eligible": "none", "reason": "host-only"}}
})";
    auto parsed = parse_conformance_case_json(manifest, "dangling");
    check(!parsed.has_errors(), "dangling source is schema-valid");
    check(parsed.conformance_case.has_value(), "dangling manifest parses");

    // load_conformance_case adds the filesystem existence gate.
    const auto sidecar = repo_root / "tests" / "conformance" / "cases" / "dangling.tmp.case.json";
    {
        std::ofstream out(sidecar, std::ios::binary | std::ios::trunc);
        out << manifest;
    }
    auto loaded = load_conformance_case(sidecar, repo_root);
    check(loaded.has_errors(), "load rejects dangling source");
    check(diagnostics_contain(loaded.diagnostics, "does not exist relative to the repository root"),
          "dangling source diagnostic");
    std::error_code error;
    std::filesystem::remove(sidecar, error);
}

} // namespace

int main() {
    const std::filesystem::path repo_root{AHFL_SOURCE_DIR};

    test_committed_cases(repo_root);
    test_malformed_manifests();
    test_canonicality_gate();
    test_dangling_source_rejected(repo_root);

    if (g_failures != 0) {
        std::cerr << g_failures << " conformance case test(s) failed\n";
        return 1;
    }
    std::cout << "all conformance case manifest tests passed\n";
    return 0;
}
