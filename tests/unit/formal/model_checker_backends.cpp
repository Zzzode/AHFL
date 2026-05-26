#include "formal/model_checker_backend.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

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
    test_nuxmv_emission();
    test_spin_emission();
    test_tlaplus_emission();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
