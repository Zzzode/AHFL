#include "verification/formal/model_checker_backend.hpp"
#include "verification/formal/nuxmv_backend.hpp"
#include "verification/formal/smv_output.hpp"
#include "verification/formal/smv_source_script.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

using namespace ahfl::formal;

int test_count = 0;
int pass_count = 0;

void check(bool condition, const std::string &test_name) {
    ++test_count;
    if (condition) {
        ++pass_count;
    } else {
        std::cerr << "FAIL: " << test_name << "\n";
    }
}

BmcStateMachine make_sample_machine() {
    BmcStateMachine machine;
    machine.name = "TestAgent";
    machine.states = {"idle", "running", "done"};
    machine.initial_state = "idle";
    machine.final_states = {"done"};
    machine.transitions = {
        {"idle", "running"},
        {"running", "done"},
        {"done", "idle"},
    };
    machine.properties = {"never(error)"};
    return machine;
}

// ============================================================================
// Factory tests
// ============================================================================

void test_factory_creates_nuxmv() {
    auto backend = create_backend(ModelCheckerKind::NuXmv);
    check(backend != nullptr, "factory.nuxmv_not_null");
    check(backend->kind() == ModelCheckerKind::NuXmv, "factory.nuxmv_kind");
    check(!backend->name().empty(), "factory.nuxmv_has_name");
    check(backend->file_extension() == ".smv", "factory.nuxmv_ext");
}

void test_factory_creates_spin() {
    auto backend = create_backend(ModelCheckerKind::SPIN);
    check(backend != nullptr, "factory.spin_not_null");
    check(backend->kind() == ModelCheckerKind::SPIN, "factory.spin_kind");
    check(!backend->name().empty(), "factory.spin_has_name");
    check(backend->file_extension() == ".pml", "factory.spin_ext");
}

void test_factory_creates_tlaplus() {
    auto backend = create_backend(ModelCheckerKind::TLAPlus);
    check(backend != nullptr, "factory.tlaplus_not_null");
    check(backend->kind() == ModelCheckerKind::TLAPlus, "factory.tlaplus_kind");
    check(!backend->name().empty(), "factory.tlaplus_has_name");
    check(backend->file_extension() == ".tla", "factory.tlaplus_ext");
}

void test_factory_nusmv_maps_to_nuxmv() {
    auto backend = create_backend(ModelCheckerKind::NuSMV);
    check(backend != nullptr, "factory.nusmv_not_null");
    // NuSMV maps to NuXmv backend
    check(backend->kind() == ModelCheckerKind::NuXmv, "factory.nusmv_maps_to_nuxmv");
}

// ============================================================================
// Capability matrix tests
// ============================================================================

void test_nuxmv_capability_matrix() {
    auto backend = create_backend(ModelCheckerKind::NuXmv);
    auto capabilities = backend->capabilities();
    check(capabilities.emits_model, "capabilities.nuxmv.emits_model");
    check(capabilities.supports_external_verification,
          "capabilities.nuxmv.supports_external_verification");
    check(capabilities.supports_ahfl_smv_semantics,
          "capabilities.nuxmv.supports_ahfl_smv_semantics");
    check(capabilities.required_binary.find("NuSMV") != std::string::npos,
          "capabilities.nuxmv.required_binary");
    check(capabilities.property_semantics.size() >= 3, "capabilities.nuxmv.property_semantics");

    auto availability = backend->availability();
    if (availability.can_verify()) {
        check(!availability.binary_path.empty(), "availability.nuxmv.binary_path");
    } else {
        check(availability.status == ModelCheckerAvailabilityStatus::MissingBinary,
              "availability.nuxmv.missing_binary");
        check(!availability.reason.empty(), "availability.nuxmv.reason");
    }
}

void test_spin_capability_matrix_is_emit_only() {
    auto backend = create_backend(ModelCheckerKind::SPIN);
    auto capabilities = backend->capabilities();
    check(capabilities.emits_model, "capabilities.spin.emits_model");
    check(!capabilities.supports_external_verification,
          "capabilities.spin.external_verification_not_wired");
    check(!capabilities.supports_ahfl_smv_semantics, "capabilities.spin.no_ahfl_smv_semantics");
    check(capabilities.required_binary == "spin", "capabilities.spin.required_binary");
    check(!capabilities.skip_reason.empty(), "capabilities.spin.skip_reason");

    auto availability = backend->availability();
    check(!availability.can_verify(), "availability.spin.cannot_verify");
    check(availability.status == ModelCheckerAvailabilityStatus::VerificationUnsupported,
          "availability.spin.unsupported");
    check(availability.reason == capabilities.skip_reason, "availability.spin.reason");
}

void test_tlaplus_capability_matrix_is_emit_only() {
    auto backend = create_backend(ModelCheckerKind::TLAPlus);
    auto capabilities = backend->capabilities();
    check(capabilities.emits_model, "capabilities.tlaplus.emits_model");
    check(!capabilities.supports_external_verification,
          "capabilities.tlaplus.external_verification_not_wired");
    check(!capabilities.supports_ahfl_smv_semantics, "capabilities.tlaplus.no_ahfl_smv_semantics");
    check(capabilities.required_binary == "TLC", "capabilities.tlaplus.required_binary");
    check(!capabilities.skip_reason.empty(), "capabilities.tlaplus.skip_reason");

    auto availability = backend->availability();
    check(!availability.can_verify(), "availability.tlaplus.cannot_verify");
    check(availability.status == ModelCheckerAvailabilityStatus::VerificationUnsupported,
          "availability.tlaplus.unsupported");
    check(availability.reason == capabilities.skip_reason, "availability.tlaplus.reason");
}

// ============================================================================
// NuXmv emission tests
// ============================================================================

void test_nuxmv_emission() {
    auto backend = create_backend(ModelCheckerKind::NuXmv);
    auto machine = make_sample_machine();

    auto result = backend->emit_model(machine);
    check(result.success, "nuxmv_emit.success");
    check(!result.model_text.empty(), "nuxmv_emit.not_empty");
    check(result.model_text.find("MODULE main") != std::string::npos, "nuxmv_emit.has_module");
    check(result.model_text.find("VAR") != std::string::npos, "nuxmv_emit.has_var");
    check(result.model_text.find("idle") != std::string::npos, "nuxmv_emit.has_state_idle");
    check(result.model_text.find("INIT") != std::string::npos, "nuxmv_emit.has_init");
    check(result.model_text.find("TRANS") != std::string::npos, "nuxmv_emit.has_trans");
}

// ============================================================================
// NuXmv output parser fixture matrix
// ============================================================================

void test_nuxmv_output_parser_true_fixture() {
    constexpr std::string_view output = R"(-- specification G (state != bad)  is true
-- LTL specification F (state = done)  is true
)";

    const auto parsed = parse_nuxmv_verification_output(output);
    check(parsed.status == NuXmvOutputStatus::Passed, "nuxmv_parse_true.status_passed");
    check(parsed.properties_checked == 2, "nuxmv_parse_true.checked_2");
    check(parsed.properties_passed == 2, "nuxmv_parse_true.passed_2");
    check(parsed.error.empty(), "nuxmv_parse_true.no_error");
}

void test_nuxmv_output_parser_false_fixture() {
    constexpr std::string_view output = R"(-- specification G (state != bad)  is false
-- as demonstrated by the following execution sequence
Trace Description: LTL Counterexample
-> State: 1.1 <-
  state = bad
)";

    const auto parsed = parse_nuxmv_verification_output(output);
    check(parsed.status == NuXmvOutputStatus::Failed, "nuxmv_parse_false.status_failed");
    check(parsed.properties_checked == 1, "nuxmv_parse_false.checked_1");
    check(parsed.properties_passed == 0, "nuxmv_parse_false.passed_0");
    check(parsed.counterexample_trace.find("state = bad") != std::string::npos,
          "nuxmv_parse_false.counterexample");
}

void test_nuxmv_output_parser_error_fixture() {
    constexpr std::string_view output = R"(file model.smv: line 12: syntax error
*** PARSE ERROR *** at token "LTLSPEC"
)";

    const auto parsed = parse_nuxmv_verification_output(output);
    check(parsed.status == NuXmvOutputStatus::Error, "nuxmv_parse_error.status_error");
    check(parsed.properties_checked == 0, "nuxmv_parse_error.checked_0");
    check(parsed.properties_passed == 0, "nuxmv_parse_error.passed_0");
    check(parsed.error.find("PARSE ERROR") != std::string::npos, "nuxmv_parse_error.message");
}

void test_nuxmv_output_parser_timeout_fixture() {
    const auto parsed = parse_nuxmv_verification_output("", true);
    check(parsed.status == NuXmvOutputStatus::Timeout, "nuxmv_parse_timeout.status_timeout");
    check(parsed.properties_checked == 0, "nuxmv_parse_timeout.checked_0");
    check(parsed.properties_passed == 0, "nuxmv_parse_timeout.passed_0");
    check(parsed.error.find("timed out") != std::string::npos, "nuxmv_parse_timeout.message");
}

// ============================================================================
// Shared SMV specification-output parser: bounded-model-checking verdicts
// ============================================================================

void test_smv_parser_bdd_true_false() {
    constexpr std::string_view output = R"(-- specification  G state != bad    is true
-- invariant (state != bad)   is true
-- specification  F state = done    is false
-- as demonstrated by the following execution sequence
)";
    const auto parsed = parse_smv_specification_results(output);
    check(parsed.specifications.size() == 3, "smv_parse_bdd.count_3");
    check(parsed.specifications[0].kind == SmvSpecificationVerdictKind::Proven,
          "smv_parse_bdd.first_proven");
    check(parsed.specifications[0].bound == 0, "smv_parse_bdd.first_unbounded");
    check(parsed.specifications[1].kind == SmvSpecificationVerdictKind::Proven,
          "smv_parse_bdd.invar_proven");
    check(parsed.specifications[2].kind == SmvSpecificationVerdictKind::Refuted,
          "smv_parse_bdd.ltl_refuted");
}

void test_smv_parser_bounded_ltl_pass() {
    constexpr std::string_view output = R"(-- no counterexample found with bound 0
-- no counterexample found with bound 1
-- no counterexample found with bound 2
-- no counterexample found with bound 0
-- no counterexample found with bound 1
-- no counterexample found with bound 2
)";
    const auto parsed = parse_smv_specification_results(output);
    check(parsed.specifications.size() == 2, "smv_parse_bounded_pass.count_2");
    check(parsed.specifications[0].kind == SmvSpecificationVerdictKind::BoundedPass,
          "smv_parse_bounded_pass.first_kind");
    check(parsed.specifications[0].bound == 2, "smv_parse_bounded_pass.first_bound_2");
    check(parsed.specifications[0].summary.find("bound 0..2") != std::string::npos,
          "smv_parse_bounded_pass.first_summary");
    check(parsed.specifications[1].kind == SmvSpecificationVerdictKind::BoundedPass,
          "smv_parse_bounded_pass.second_kind");
    check(parsed.specifications[1].bound == 2, "smv_parse_bounded_pass.second_bound_2");
}

void test_smv_parser_bounded_ltl_refuted() {
    constexpr std::string_view output = R"(-- no counterexample found with bound 0
-- no counterexample found with bound 1
-- specification  G state != done    is false
-- as demonstrated by the following execution sequence
Trace Description: BMC Counterexample
  -> State: 1.1 <-
    state = done
)";
    const auto parsed = parse_smv_specification_results(output);
    check(parsed.specifications.size() == 1, "smv_parse_bounded_fail.count_1");
    check(parsed.specifications[0].kind == SmvSpecificationVerdictKind::Refuted,
          "smv_parse_bounded_fail.kind_refuted");
    check(parsed.specifications[0].bound == 1, "smv_parse_bounded_fail.bound_1");
    check(parsed.specifications[0].summary.find("is false") != std::string::npos,
          "smv_parse_bounded_fail.summary");
}

void test_smv_parser_bounded_passes_then_proven_invariant() {
    // Real NuSMV 2.6.0 BMC output shape: two bounded LTL passes (no terminal
    // line, counter resets to 0) followed by an inductively proven invariant.
    constexpr std::string_view output = R"(-- no counterexample found with bound 0
-- no counterexample found with bound 1
-- no counterexample found with bound 0
-- no counterexample found with bound 1
-- invariant (state != bad)   is true
)";
    const auto parsed = parse_smv_specification_results(output);
    check(parsed.specifications.size() == 3, "smv_parse_mixed.count_3");
    check(parsed.specifications[0].kind == SmvSpecificationVerdictKind::BoundedPass,
          "smv_parse_mixed.first_bounded");
    check(parsed.specifications[0].bound == 1, "smv_parse_mixed.first_bound_1");
    check(parsed.specifications[1].kind == SmvSpecificationVerdictKind::BoundedPass,
          "smv_parse_mixed.second_bounded");
    check(parsed.specifications[1].bound == 1, "smv_parse_mixed.second_bound_1");
    check(parsed.specifications[2].kind == SmvSpecificationVerdictKind::Proven,
          "smv_parse_mixed.invar_proven");
}

void test_smv_parser_bounded_invariant_inconclusive() {
    constexpr std::string_view output = R"(-- no proof or counterexample found with bound 0
-- no proof or counterexample found with bound 1
-- cannot prove the invariant state != s3  is true or false.
)";
    const auto parsed = parse_smv_specification_results(output);
    check(parsed.specifications.size() == 1, "smv_parse_inconclusive.count_1");
    check(parsed.specifications[0].kind == SmvSpecificationVerdictKind::Inconclusive,
          "smv_parse_inconclusive.kind");
    check(parsed.specifications[0].bound == 1, "smv_parse_inconclusive.bound_1");
}

void test_smv_parser_real_bmc_pass_fail_pass() {
    // Mirrors the real three-LTLSPEC probe: pass, fail, pass.
    constexpr std::string_view output = R"(-- no counterexample found with bound 0
-- no counterexample found with bound 1
-- no counterexample found with bound 0
-- no counterexample found with bound 1
-- specification  G state != done    is false
-- as demonstrated by the following execution sequence
-- no counterexample found with bound 0
-- no counterexample found with bound 1
)";
    const auto parsed = parse_smv_specification_results(output);
    check(parsed.specifications.size() == 3, "smv_parse_real_mix.count_3");
    check(parsed.specifications[0].kind == SmvSpecificationVerdictKind::BoundedPass,
          "smv_parse_real_mix.first_pass");
    check(parsed.specifications[1].kind == SmvSpecificationVerdictKind::Refuted,
          "smv_parse_real_mix.middle_fail");
    check(parsed.specifications[2].kind == SmvSpecificationVerdictKind::BoundedPass,
          "smv_parse_real_mix.last_pass");
}

void test_nuxmv_parser_bounded_pass_fixture() {
    constexpr std::string_view output = R"(-- no counterexample found with bound 0
-- no counterexample found with bound 1
-- no counterexample found with bound 0
-- no counterexample found with bound 1
)";
    const auto parsed = parse_nuxmv_verification_output(output);
    check(parsed.status == NuXmvOutputStatus::Passed, "nuxmv_parse_bounded.status_passed");
    check(parsed.properties_checked == 2, "nuxmv_parse_bounded.checked_2");
    check(parsed.properties_passed == 2, "nuxmv_parse_bounded.passed_2");
}

// ============================================================================
// Shared NuSMV/nuXmv batch-source script builder
// ============================================================================

void test_build_smv_source_script_bmc() {
    const std::filesystem::path model = "/tmp/ahfl-unit/model.smv";
    const auto script = build_smv_source_script(model, SmvCheckerEngine::Bmc, 7);
    check(script.find("read_model -i /tmp/ahfl-unit/model.smv\n") != std::string::npos,
          "smv_script_bmc.read_model");
    check(script.find("go_bmc\n") != std::string::npos, "smv_script_bmc.go_bmc");
    check(script.find("check_ltlspec_bmc -k 7\n") != std::string::npos,
          "smv_script_bmc.ltl_bound");
    // NuSMV 2.6.0 rejects -k unless een-sorensson is selected explicitly.
    check(script.find("check_invar_bmc -a een-sorensson -k 7\n") != std::string::npos,
          "smv_script_bmc.invar_een_sorensson_bound");
    check(script.find("build_model") == std::string::npos, "smv_script_bmc.no_bdd");
    check(script.find("quit\n") != std::string::npos, "smv_script_bmc.quit");
}

void test_build_smv_source_script_bdd() {
    const std::filesystem::path model = "/tmp/ahfl-unit/model.smv";
    const auto script = build_smv_source_script(model, SmvCheckerEngine::Bdd, 7);
    check(script.find("go_bmc") == std::string::npos, "smv_script_bdd.no_bmc");
    check(script.find("check_ltlspec_bmc") == std::string::npos, "smv_script_bdd.no_bmc_ltl");
    check(script.find("flatten_hierarchy\n") != std::string::npos, "smv_script_bdd.flatten");
    check(script.find("build_model\n") != std::string::npos, "smv_script_bdd.build");
    check(script.find("check_ltlspec\n") != std::string::npos, "smv_script_bdd.ltl");
    check(script.find("check_invar\n") != std::string::npos, "smv_script_bdd.invar");
}

// ============================================================================
// SPIN emission tests
// ============================================================================

void test_spin_emission() {
    auto backend = create_backend(ModelCheckerKind::SPIN);
    auto machine = make_sample_machine();

    auto result = backend->emit_model(machine);
    check(result.success, "spin_emit.success");
    check(!result.model_text.empty(), "spin_emit.not_empty");
    check(result.model_text.find("proctype") != std::string::npos, "spin_emit.has_proctype");
    check(result.model_text.find("byte state") != std::string::npos, "spin_emit.has_byte_state");
    check(result.model_text.find("init") != std::string::npos, "spin_emit.has_init");
}

// ============================================================================
// TLA+ emission tests
// ============================================================================

void test_tlaplus_emission() {
    auto backend = create_backend(ModelCheckerKind::TLAPlus);
    auto machine = make_sample_machine();

    auto result = backend->emit_model(machine);
    check(result.success, "tlaplus_emit.success");
    check(!result.model_text.empty(), "tlaplus_emit.not_empty");
    check(result.model_text.find("MODULE") != std::string::npos, "tlaplus_emit.has_module");
    check(result.model_text.find("VARIABLES") != std::string::npos, "tlaplus_emit.has_variables");
    check(result.model_text.find("Init") != std::string::npos, "tlaplus_emit.has_init");
    check(result.model_text.find("Next") != std::string::npos, "tlaplus_emit.has_next");
}

} // anonymous namespace

int main() {
    test_factory_creates_nuxmv();
    test_factory_creates_spin();
    test_factory_creates_tlaplus();
    test_factory_nusmv_maps_to_nuxmv();
    test_nuxmv_capability_matrix();
    test_spin_capability_matrix_is_emit_only();
    test_tlaplus_capability_matrix_is_emit_only();
    test_nuxmv_emission();
    test_nuxmv_output_parser_true_fixture();
    test_nuxmv_output_parser_false_fixture();
    test_nuxmv_output_parser_error_fixture();
    test_nuxmv_output_parser_timeout_fixture();
    test_smv_parser_bdd_true_false();
    test_smv_parser_bounded_ltl_pass();
    test_smv_parser_bounded_ltl_refuted();
    test_smv_parser_bounded_passes_then_proven_invariant();
    test_smv_parser_bounded_invariant_inconclusive();
    test_smv_parser_real_bmc_pass_fail_pass();
    test_nuxmv_parser_bounded_pass_fixture();
    test_build_smv_source_script_bmc();
    test_build_smv_source_script_bdd();
    test_spin_emission();
    test_tlaplus_emission();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
