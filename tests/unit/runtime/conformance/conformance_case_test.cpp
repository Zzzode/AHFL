// KR6.7 (RFC 0026 P7): engine-independent conformance case manifest schema
// validator. Hand-rolled check()/main(), mirroring the other RFC 0026 slice
// tests. No engine is linked or invoked.
//
// Coverage (per slice spec):
//   (a) every committed sidecar under tests/conformance/cases loads and every
//       one references an existing AHFL source (the 4 tests/golden/wasm and 4
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
//       scenario / empty scenario list / path-unsafe scenario name;
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
using ahfl::conformance::WasmNodeObservationSkip;
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
    // KR6.7: a raw-P4-D-frame P6 case (aggregate/collection) emits on the wasm
    // lane but its inline raw input frame cannot be materialized from canonical
    // wire JSON by the evaluator adapter, so it runs the wasm/Node lane only.
    bool evaluator{true};
    // Pinned Node-observation skip: None for compared cases; the two raw-frame
    // cases and the KR6.6-blocked cases declare their exact skip reason.
    WasmNodeObservationSkip node_observation_skip{WasmNodeObservationSkip::None};
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
            WasmEligibility::Orchestration,
            /*evaluator=*/true,
            WasmNodeObservationSkip::None,
        },
        {
            "if_let_e2e.case.json",
            "tests/golden/runtime/if_let_e2e.ahfl",
            CaseKind::Workflow,
            "runtime::if_let_e2e::IfLetWorkflow",
            0,
            2,
            WasmEligibility::Orchestration,
            /*evaluator=*/true,
            WasmNodeObservationSkip::None,
        },
        {
            "e2e_multi_agent.case.json",
            "tests/golden/runtime/e2e_multi_agent.ahfl",
            CaseKind::Workflow,
            "runtime::e2e_multi_agent::CustomerSupportWorkflow",
            4,
            2,
            WasmEligibility::Orchestration,
            /*evaluator=*/true,
            WasmNodeObservationSkip::None,
        },
        {
            "float_output_e2e.case.json",
            "tests/golden/runtime/float_output_e2e.ahfl",
            CaseKind::Workflow,
            "runtime::float_output_e2e::FloatPipeline",
            0,
            1,
            // The classifier (ahfl.conformance.wasm_eligibility) proved this
            // f64 identity passthrough emits on the orchestration lane; the
            // former `computation` claim was a stale hand-curated skip.
            WasmEligibility::Orchestration,
        },
        // KR6.7 (RFC 0026 P7): landed KR6.6 (P6) computation slices, expressed
        // as manifest-driven cases. Each completes with an identity final that
        // forwards the borrowed wire frame, so it runs on BOTH the evaluator and
        // the orchestration wasm/Node lane and differentially compares.
        {
            "p6_scalar_cond.case.json",
            "tests/golden/wasm/p6_scalar_cond.ahfl",
            CaseKind::Agent,
            "wasm::p6_scalar_cond::ScalarCondAgent",
            0, 1, WasmEligibility::Orchestration,
        },
        {
            "p6_neg_compare.case.json",
            "tests/golden/wasm/p6_neg_compare.ahfl",
            CaseKind::Agent,
            "wasm::p6_neg_cmp::NegCmpAgent",
            0, 1, WasmEligibility::Orchestration,
        },
        {
            "p6_cascade.case.json",
            "tests/golden/wasm/p6_cascade.ahfl",
            CaseKind::Agent,
            "wasm::p6_cascade::CascadeAgent",
            0, 1, WasmEligibility::Orchestration,
        },
        {
            "p6_match_enum.case.json",
            "tests/golden/wasm/p6_match_enum.ahfl",
            CaseKind::Agent,
            "wasm::p6_match_enum::MatchAgent",
            0, 1, WasmEligibility::Orchestration,
        },
        {
            "v2c_multi_arg_bridge.case.json",
            "tests/golden/wasm/v2c_multi_arg_bridge.ahfl",
            CaseKind::Agent,
            "wasm::v2c_multi_arg_bridge::RoutingAgent",
            1, 1, WasmEligibility::Orchestration,
        },
        // RFC 0026 P6-7 frame-bridge v2 V2-C fix-forward regressions: a chain
        // of two computed bridge handlers sharing one import ordinal, a
        // producing match followed by an ordered bridge statement, and a
        // single tag-only-enum bridge argument pinning the SSOT
        // {"value":..} envelope through the per-call argument differential.
        {
            "v2c_bridge_chain.case.json",
            "tests/golden/wasm/v2c_bridge_chain.ahfl",
            CaseKind::Agent,
            "wasm::v2c_bridge_chain::RoutingAgent",
            1, 1, WasmEligibility::Orchestration,
        },
        {
            "v2c_route_then_bridge.case.json",
            "tests/golden/wasm/v2c_route_then_bridge.ahfl",
            CaseKind::Agent,
            "wasm::v2c_route_then_bridge::RoutingAgent",
            1, 1, WasmEligibility::Orchestration,
        },
        {
            "v2c_single_enum_bridge.case.json",
            "tests/golden/wasm/v2c_single_enum_bridge.ahfl",
            CaseKind::Agent,
            "wasm::v2c_single_enum_bridge::RoutingAgent",
            1, 1, WasmEligibility::Orchestration,
        },
        {
            "p6_coerce.case.json",
            "tests/golden/wasm/p6_coerce.ahfl",
            CaseKind::Agent,
            "wasm::p6_coerce::CoerceAgent",
            0, 1, WasmEligibility::Orchestration,
        },
        {
            "p6_coerce_bounds.case.json",
            "tests/golden/wasm/p6_coerce_bounds.ahfl",
            CaseKind::Agent,
            "wasm::p6_coerce_bounds::CoerceBoundsAgent",
            0, 1, WasmEligibility::Orchestration,
        },
        {
            "p6_nested_depth3.case.json",
            "tests/golden/wasm/p6_nested_depth3.ahfl",
            CaseKind::Agent,
            "wasm::p6_nested_depth3::NestedDepth3Agent",
            0, 1, WasmEligibility::Orchestration,
        },
        {
            "p6_nested_fallthrough.case.json",
            "tests/golden/wasm/p6_nested_fallthrough.ahfl",
            CaseKind::Agent,
            "wasm::p6_nested_fallthrough::NestedFallthroughAgent",
            0, 1, WasmEligibility::Orchestration,
        },
        // KR6.7 corpus widening (FB-5): P6 expression/control-flow companion
        // fixtures, migrated from bespoke Node probes to manifest-driven cases
        // that run under BOTH engines with differential agreement. Each is an
        // identity-final agent (the borrowed Frame is returned) whose computed
        // non-final handler exercises the named construct.
        {
            "p6_cascade_high.case.json",
            "tests/golden/wasm/p6_cascade_high.ahfl",
            CaseKind::Agent,
            "wasm::p6_cascade_high::CascadeHighAgent",
            0, 1, WasmEligibility::Orchestration,
        },
        {
            "p6_elseless_fallthrough.case.json",
            "tests/golden/wasm/p6_elseless_fallthrough.ahfl",
            CaseKind::Agent,
            "wasm::p6_elseless_fallthrough::ElselessFallthroughAgent",
            0, 1, WasmEligibility::Orchestration,
        },
        {
            "p6_elseless_taken.case.json",
            "tests/golden/wasm/p6_elseless_taken.ahfl",
            CaseKind::Agent,
            "wasm::p6_elseless_taken::ElselessTakenAgent",
            0, 1, WasmEligibility::Orchestration,
        },
        {
            "p6_implies.case.json",
            "tests/golden/wasm/p6_implies.ahfl",
            CaseKind::Agent,
            "wasm::p6_implies::ImpliesAgent",
            0, 1, WasmEligibility::Orchestration,
        },
        {
            "p6_match_binding_payload.case.json",
            "tests/golden/wasm/p6_match_binding_payload.ahfl",
            CaseKind::Agent,
            "wasm::p6_match_binding_payload::MatchBindingAgent",
            0, 1, WasmEligibility::Orchestration,
        },
        {
            "p6_match_expr.case.json",
            "tests/golden/wasm/p6_match_expr.ahfl",
            CaseKind::Agent,
            "wasm::p6_match_expr::MatchAgent",
            0, 1, WasmEligibility::Orchestration,
        },
        {
            "p6_match_fallthrough.case.json",
            "tests/golden/wasm/p6_match_fallthrough.ahfl",
            CaseKind::Agent,
            "wasm::p6_match_fallthrough::MatchAgent",
            0, 1, WasmEligibility::Orchestration,
        },
        {
            "p6_match_guard.case.json",
            "tests/golden/wasm/p6_match_guard.ahfl",
            CaseKind::Agent,
            "wasm::p6_match_guard::MatchAgent",
            0, 1, WasmEligibility::Orchestration,
        },
        {
            "p6_match_or.case.json",
            "tests/golden/wasm/p6_match_or.ahfl",
            CaseKind::Agent,
            "wasm::p6_match_or::MatchAgent",
            0, 1, WasmEligibility::Orchestration,
        },
        {
            "p6_match_result_i64.case.json",
            "tests/golden/wasm/p6_match_result_i64.ahfl",
            CaseKind::Agent,
            "wasm::p6_match_result_i64::MatchResultAgent",
            0, 1, WasmEligibility::Orchestration,
        },
        {
            "p6_nested_depth3_taken.case.json",
            "tests/golden/wasm/p6_nested_depth3_taken.ahfl",
            CaseKind::Agent,
            "wasm::p6_nested_depth3_taken::NestedDepth3TakenAgent",
            0, 1, WasmEligibility::Orchestration,
        },
        {
            "p6_nested_elseless.case.json",
            "tests/golden/wasm/p6_nested_elseless.ahfl",
            CaseKind::Agent,
            "wasm::p6_nested_elseless::NestedElselessAgent",
            0, 1, WasmEligibility::Orchestration,
        },
        {
            "p6_nested_elseless_taken.case.json",
            "tests/golden/wasm/p6_nested_elseless_taken.ahfl",
            CaseKind::Agent,
            "wasm::p6_nested_elseless_taken::NestedElselessTakenAgent",
            0, 1, WasmEligibility::Orchestration,
        },
        {
            "p6_nested_projection.case.json",
            "tests/golden/wasm/p6_nested_projection.ahfl",
            CaseKind::Agent,
            "wasm::p6_nested_projection::NestedProjectionAgent",
            0, 1, WasmEligibility::Orchestration,
        },
        {
            "p6_nested_taken_high.case.json",
            "tests/golden/wasm/p6_nested_taken_high.ahfl",
            CaseKind::Agent,
            "wasm::p6_nested_taken_high::NestedTakenHighAgent",
            0, 1, WasmEligibility::Orchestration,
        },
        {
            "p6_unwrap_some.case.json",
            "tests/golden/wasm/p6_unwrap_some.ahfl",
            CaseKind::Agent,
            "std::option::UnwrapAgent",
            0, 1, WasmEligibility::Orchestration,
        },
        // KR6.7 corpus widening (FB-5): V2-A computed-final agents migrated
        // from bespoke Node probes. Their finals construct the output frame
        // (scalar word, nested aggregate expansion, tag-only / payload-bearing
        // enum), so the cases run under both engines as p6-frame modules.
        {
            "v2a_computed_scalar.case.json",
            "tests/golden/wasm/v2a_computed_scalar.ahfl",
            CaseKind::Agent,
            "wasm::v2a::computed_scalar::ComputedScalarAgent",
            0, 1, WasmEligibility::Orchestration,
        },
        {
            "v2a_computed_aggregate.case.json",
            "tests/golden/wasm/v2a_computed_aggregate.ahfl",
            CaseKind::Agent,
            "wasm::v2a::computed_aggregate::ComputedAggregateAgent",
            0, 1, WasmEligibility::Orchestration,
        },
        {
            "v2a_computed_enum.case.json",
            "tests/golden/wasm/v2a_computed_enum.ahfl",
            CaseKind::Agent,
            "wasm::v2a::computed_enum::ComputedEnumAgent",
            0, 2, WasmEligibility::Orchestration,
        },
        {
            "v2a_computed_if_let_return.case.json",
            "tests/golden/wasm/v2a_computed_if_let_return.ahfl",
            CaseKind::Agent,
            "wasm::v2a::computed_if_let_return::IfLetReturnAgent",
            0, 3, WasmEligibility::Orchestration,
        },
        {
            "v2a_computed_payload_enum.case.json",
            "tests/golden/wasm/v2a_computed_payload_enum.ahfl",
            CaseKind::Agent,
            "wasm::v2a::computed_payload_enum::ComputedPayloadEnumAgent",
            0, 2, WasmEligibility::Orchestration,
        },
        // KR6.7 corpus widening (FB-5): V2-B String/computed-final and
        // bounded-arena agents migrated from bespoke Node probes. The String
        // tuple-variant fixture v2b_enum_string is deliberately absent: the
        // evaluator misroutes Hit("hit") enum construction through its
        // capability invoker while the wasm run is correct (tracked defect).
        {
            "v2b_string_passthrough.case.json",
            "tests/golden/wasm/v2b_string_passthrough.ahfl",
            CaseKind::Agent,
            "wasm::v2b::string_passthrough::StringFinalAgent",
            0, 1, WasmEligibility::Orchestration,
        },
        {
            "v2b_computed_string.case.json",
            "tests/golden/wasm/v2b_computed_string.ahfl",
            CaseKind::Agent,
            "wasm::v2b::computed_string::ComputedStringAgent",
            0, 2, WasmEligibility::Orchestration,
        },
        {
            "v2b_bounded_string.case.json",
            "tests/golden/wasm/v2b_bounded_string.ahfl",
            CaseKind::Agent,
            "wasm::v2b::bounded_string::BoundedStringAgent",
            0, 2, WasmEligibility::Orchestration,
        },
        {
            "v2b_list_string_arena.case.json",
            "tests/golden/wasm/v2b_list_string_arena.ahfl",
            CaseKind::Agent,
            "std::collections::ListStringArenaAgent",
            0, 1, WasmEligibility::Orchestration,
        },
        {
            "v2b_list_nested_string_arena.case.json",
            "tests/golden/wasm/v2b_list_nested_string_arena.ahfl",
            CaseKind::Agent,
            "std::collections::ListNestedStringArenaAgent",
            0, 1, WasmEligibility::Orchestration,
        },
        // KR6.5 F5 bounded-String durable-resume replay workflow, now also a
        // manifest-driven two-node capability DAG case.
        {
            "e3_capability_workflow_resume.case.json",
            "tests/golden/wasm/e3_capability_workflow_resume.ahfl",
            CaseKind::Workflow,
            "wasm::e3_capability_workflow_resume::CapabilityPipeline",
            1, 1, WasmEligibility::Orchestration,
        },
        // P4-D input-frame handlers: rung E packs their input frame, calls
        // runv, and encodes the output, so the Node lane now DIFFERENTIALLY
        // COMPARES them (engines.evaluator=true and blessed observations
        // exist); no node_observation_skip.
        {
            "p6_aggregate.case.json",
            "tests/golden/wasm/p6_aggregate.ahfl",
            CaseKind::Agent,
            "wasm::p6_aggregate::AggregateAgent",
            0, 1, WasmEligibility::Orchestration, /*evaluator=*/true,
            WasmNodeObservationSkip::None,
        },
        {
            "p6_collection.case.json",
            "tests/golden/wasm/p6_collection.ahfl",
            CaseKind::Agent,
            "std::collections::CollectionAgent",
            0, 1, WasmEligibility::Orchestration, /*evaluator=*/true,
            WasmNodeObservationSkip::None,
        },
        // RFC 0026 P6-7: two same-typed bounded lists in one input struct.
        // Each fixed-edge occurrence gets its own disjoint backing placement;
        // the Node lane packs and round-trips both lists independently.
        {
            "p6_frame_two_containers.case.json",
            "tests/golden/wasm/p6_frame_two_containers.ahfl",
            CaseKind::Agent,
            "std::collections::TwoContainersAgent",
            0, 1, WasmEligibility::Orchestration, /*evaluator=*/true,
            WasmNodeObservationSkip::None,
        },
        // RFC 0026 FB-3b: higher-order lambda / call_indirect. Emits on the
        // orchestration lane with the funcref table; the in-process evaluator
        // has no user-fn / closure surface yet (node-only until KR6.8).
        {
            "fb3_higher_order.case.json",
            "tests/golden/wasm/fb3_higher_order.ahfl",
            CaseKind::Agent,
            "wasm::fb3_higher_order::HigherOrderAgent",
            0, 1, WasmEligibility::Orchestration, /*evaluator=*/false,
            WasmNodeObservationSkip::EvaluatorSurfaceAwaitsKr68,
        },
        // KR6.7 corpus widening (FB-5): more node-only user pure-fn / closure
        // cases from the FB-1 direct-call and FB-3b closure ladders. The Node
        // embedded-engine observation is blessed directly; each module emits on
        // the orchestration lane while the evaluator surface awaits KR6.8.
        {
            "fb1_direct_call.case.json",
            "tests/golden/wasm/fb1_direct_call.ahfl",
            CaseKind::Agent,
            "wasm::fb1_direct_call::DirectCallAgent",
            0, 1, WasmEligibility::Orchestration, /*evaluator=*/false,
            WasmNodeObservationSkip::EvaluatorSurfaceAwaitsKr68,
        },
        {
            "fb1_aggregate_direct_call.case.json",
            "tests/golden/wasm/fb1_aggregate_direct_call.ahfl",
            CaseKind::Agent,
            "wasm::fb1_aggregate_direct_call::Fb1AggregateAgent",
            0, 1, WasmEligibility::Orchestration, /*evaluator=*/false,
            WasmNodeObservationSkip::EvaluatorSurfaceAwaitsKr68,
        },
        {
            "fb3_byvalue_capture.case.json",
            "tests/golden/wasm/fb3_byvalue_capture.ahfl",
            CaseKind::Agent,
            "wasm::fb3_byvalue_capture::ByValueCaptureAgent",
            0, 1, WasmEligibility::Orchestration, /*evaluator=*/false,
            WasmNodeObservationSkip::EvaluatorSurfaceAwaitsKr68,
        },
        {
            "fb3_nested_activation.case.json",
            "tests/golden/wasm/fb3_nested_activation.ahfl",
            CaseKind::Agent,
            "wasm::fb3_nested_activation::NestedActivationAgent",
            0, 1, WasmEligibility::Orchestration, /*evaluator=*/false,
            WasmNodeObservationSkip::EvaluatorSurfaceAwaitsKr68,
        },
        {
            "fb3_nested_lambda_flow.case.json",
            "tests/golden/wasm/fb3_nested_lambda_flow.ahfl",
            CaseKind::Agent,
            "wasm::fb3_nested_lambda_flow::NestedLambdaFlowAgent",
            0, 1, WasmEligibility::Orchestration, /*evaluator=*/false,
            WasmNodeObservationSkip::EvaluatorSurfaceAwaitsKr68,
        },
        // KR6.7 corpus widening (FB-5) slice E.
        {
            "fb4_cross_agent_capability_leak.case.json",
            "tests/golden/wasm/fb4_cross_agent_capability_leak.ahfl",
            CaseKind::Agent,
            "wasm::fb4_cross_agent_capability_leak::PureAgent",
            0, 1, WasmEligibility::Orchestration,
        },
        {
            "fb4_effect_clause_pure_body.case.json",
            "tests/golden/wasm/fb4_effect_clause_pure_body.ahfl",
            CaseKind::Agent,
            "wasm::fb4_effect_clause_pure_body::DeclEffectAgent",
            0, 1, WasmEligibility::Orchestration, /*evaluator=*/false,
            WasmNodeObservationSkip::EvaluatorSurfaceAwaitsKr68,
        },
        // WH-5b.2: hybrid P6-bridge + opaque workflow that suspends at the
        // first bridge node's PENDING arm. The scenario expects run_status
        // suspended (not completed), so the run_status pin below admits both.
        {
            "wh5b_bridge_pending_suspend.case.json",
            "tests/golden/wasm/wh5b_hybrid_p6_bridge.ahfl",
            CaseKind::Workflow,
            "wasm::wh5b_hybrid_p6_bridge::HybridBridgePipeline",
            2, 1, WasmEligibility::Orchestration,
        },
        // WH-5c.1: P6-packaged agent whose non-final handler constructs an
        // aggregate with a String field from the input frame and passes it to
        // a bridge capability. The relocated workflow-lane runner names the
        // construct root scratch_base()+offset so the host decodes the
        // descriptor instead of aborting with ArgDecodeFailed.
        {
            "wh5c_construct_bridge.case.json",
            "tests/golden/wasm/wh5c_construct_bridge.ahfl",
            CaseKind::Workflow,
            "wasm::wh5c_construct_bridge::ConstructPipeline",
            1, 1, WasmEligibility::Orchestration,
        },
        // WH-5c.2: two P6 nodes (first, second) reusing ONE packaged agent
        // instance. Approach B assigns one node-frame block + runner function
        // per P6 node, so the fan-out lowers to wasm and both nodes run.
        {
            "wh5c_instance_reuse_fanout.case.json",
            "tests/golden/wasm/wh5c_instance_reuse_fanout.ahfl",
            CaseKind::Workflow,
            "wasm::wh5c_instance_reuse_fanout::ReuseFanout",
            0, 1, WasmEligibility::Orchestration,
        },
        // WH-5c.4 (GAP 2): an opaque-lane capability-final agent that
        // constructs the capability argument in-module. The wasm codegen emits
        // a P4D_TO_JSON self-transcode for the constructed O_k and a
        // JSON_TO_P4D workflow-output crossing. The native wasm3 lane agrees
        // with the evaluator; the Node observation withholds on
        // host_transcode_awaits_node_port (the C++ WH-5b.3 transcode landed,
        // but the Node oracle JS port of ahfl_xcode is still a stub).
        {
            "wh5c4_construct_cap_final.case.json",
            "tests/golden/wasm/wh5c4_construct_cap_final.ahfl",
            CaseKind::Workflow,
            "wasm::wh5c4_construct_cap_final::ReplyWorkflow",
            1, 1, WasmEligibility::Orchestration,
            /*evaluator=*/true,
            WasmNodeObservationSkip::HostTranscodeAwaitsNodePort,
        },
        // WH-5c.4 P0-3: a construct-capability terminal whose upstream node is
        // opaque (Echo capability-final). The construct node carries BOTH a
        // JSON_TO_P4D input crossing (opaque producer output -> P4-D input)
        // AND a P4D_TO_JSON self-transcode (constructed Request -> wire-JSON);
        // the two-slot scheduler table keeps both. The native wasm3 lane
        // agrees with the evaluator; the Node observation withholds on
        // host_transcode_awaits_node_port.
        {
            "wh5c4_p03_opaque_upstream.case.json",
            "tests/golden/wasm/wh5c4_p03_opaque_upstream.ahfl",
            CaseKind::Workflow,
            "wasm::wh5c4_p03_opaque_upstream::OpaqueUpstreamWorkflow",
            2, 1, WasmEligibility::Orchestration,
            /*evaluator=*/true,
            WasmNodeObservationSkip::HostTranscodeAwaitsNodePort,
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
        check(manifest.engines.evaluator == expectation->evaluator,
              "evaluator enabled: " + file_name);
        check(manifest.engines.wasm.eligibility == expectation->wasm,
              "wasm eligibility: " + file_name);
        check(!manifest.engines.wasm.reason.empty(),
              "wasm skip reason present: " + file_name);
        check(manifest.engines.wasm.node_observation_skip ==
                  expectation->node_observation_skip,
              "wasm node_observation_skip pin: " + file_name);

        for (const auto &scenario : manifest.scenarios) {
            check(!scenario.name.empty(), "scenario carries a name: " + file_name);
            check(!scenario.input_json.empty(),
                  "scenario '" + scenario.name + "' carries canonical input: " + file_name);
            // A census scenario either runs to completion or suspends at a
            // pending capability arm (WH-5b.2 bridge-pending case); both are
            // pinned expected outcomes, not failures.
            check(scenario.expect.run_status == ExpectedRunStatus::Completed ||
                      scenario.expect.run_status == ExpectedRunStatus::Suspended,
                  "expected completed or suspended run: " + file_name + "/" + scenario.name);
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

    expect_rejected("bad node_observation_skip enum",
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
              "wasm": {"eligible": "orchestration", "reason": "r",
                       "node_observation_skip": "bogus"}}
})",
                    "engines.wasm.node_observation_skip' must be 'none'");

    // RFC 0026 P6-7 rung E removed the raw_p6_frame_awaits_p67 skip (a p6-frame
    // module now packs/runs runv/encodes with differential agreement), so the
    // spelling is rejected as an unknown node_observation_skip value by the
    // generic "must be 'none'" gate above.

    expect_rejected("blocked kr66 skip on an orchestration lane",
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
              "wasm": {"eligible": "orchestration", "reason": "r",
                       "node_observation_skip": "blocked_kr66"}}
})",
                    "engines.wasm.eligible 'computation'");

    expect_rejected("host transcode skip on a computation lane",
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
              "wasm": {"eligible": "computation", "reason": "r",
                       "node_observation_skip": "host_transcode_awaits_node_port"}}
})",
                    "engines.wasm.eligible 'orchestration'");

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

    // A scenario name is concatenated into the blessing filename
    // (<stem>.<name>.json), so anything outside [A-Za-z0-9_-] is rejected -
    // this closes path traversal (a name such as "../x") from a manifest data
    // field into the bless/verify/observation-path sink.
    expect_rejected("path-traversing scenario name",
                    R"({
  "format_version": "ahfl.conformance-case.v1",
  "source": "tests/golden/wasm/e1_identity_agent.ahfl",
  "kind": "agent",
  "entry": "wasm::e1_identity::IdentityAgent",
  "scenarios": [
    {"name": "../escape", "input": {},
     "expect": {"run_status": "completed", "state_sequence": ["Start"],
                "capability_sequence": []}}
  ],
  "capabilities": [],
  "engines": {"evaluator": true,
              "wasm": {"eligible": "orchestration", "reason": "E1"}}
})",
                    "must be a path-safe name matching [A-Za-z0-9_-]+");

    expect_rejected("scenario name with a path separator",
                    R"({
  "format_version": "ahfl.conformance-case.v1",
  "source": "tests/golden/wasm/e1_identity_agent.ahfl",
  "kind": "agent",
  "entry": "wasm::e1_identity::IdentityAgent",
  "scenarios": [
    {"name": "a/b", "input": {},
     "expect": {"run_status": "completed", "state_sequence": ["Start"],
                "capability_sequence": []}}
  ],
  "capabilities": [],
  "engines": {"evaluator": true,
              "wasm": {"eligible": "orchestration", "reason": "E1"}}
})",
                    "must be a path-safe name matching [A-Za-z0-9_-]+");

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
