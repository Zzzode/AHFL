#include "verification/formal/model_checker_backend.hpp"
#include "verification/formal/nuxmv_backend.hpp"

#include <cstdlib>
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
    test_spin_emission();
    test_tlaplus_emission();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
