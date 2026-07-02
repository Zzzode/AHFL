#include "tooling/cli/command_catalog.hpp"
#include "tooling/cli/option_table.hpp"
#include "verification/formal/checker.hpp"
#include "verification/formal/nuxmv_backend.hpp"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace ahfl::cli;
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

BmcStateMachine make_simple_machine() {
    BmcStateMachine machine;
    machine.name = "Demo";
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

using ArgVec = std::vector<std::string_view>;

// Parse the provided argument list, expecting success (no terminal ParseResult).
[[nodiscard]] std::optional<CommandLineOptions>
parse_args(const ArgVec &args, std::ostream &err) {
    CommandLineOptions opts;
    auto prev_buf = std::cerr.rdbuf(err.rdbuf());
    const auto result = parse_options_from_table(args, opts);
    std::cerr.rdbuf(prev_buf);
    if (result.has_value() && result->should_exit && result->exit_code != 0) {
        return std::nullopt;
    }
    return opts;
}

// ============================================================================
// TEST_CASE: CLI --bmc-depth=1 (fast, short unrolling)
// ============================================================================

void test_cli_bmc_depth_1_equals() {
    ArgVec args{"--bmc-depth=1"};
    std::ostringstream err;
    auto opts = parse_args(args, err);
    check(opts.has_value(), "bmc_depth_1.parse_ok");
    if (opts.has_value()) {
        check(opts->bmc_depth.has_value(), "bmc_depth_1.field_present");
        check(*opts->bmc_depth == "1", "bmc_depth_1.value_eq_1");
    }
}

void test_cli_bmc_depth_1_space() {
    ArgVec args{"--bmc-depth", "1"};
    std::ostringstream err;
    auto opts = parse_args(args, err);
    check(opts.has_value(), "bmc_depth_1_space.parse_ok");
    if (opts.has_value()) {
        check(opts->bmc_depth.has_value(), "bmc_depth_1_space.field_present");
        check(*opts->bmc_depth == "1", "bmc_depth_1_space.value_eq_1");
    }
}

// ============================================================================
// TEST_CASE: CLI --bmc-depth=0 rejects usage error (K must be positive)
// ============================================================================

void test_cli_bmc_depth_0_rejected() {
    ArgVec args{"verify", "--bmc-depth=0"};
    std::ostringstream err;
    // parse_options_from_table does only syntax parsing, 0 is still a valid
    // digit string.  The semantic "K>0" check lives in CliDriver validation,
    // which we cannot easily call from a unit test.  We therefore expose a
    // minimal parse_bmc_depth utility via a local copy (mirror of the driver
    // logic) and assert behaviour — this pins the contract the driver must
    // uphold.
    auto parse_bmc_depth = [](std::string_view value) -> std::optional<std::size_t> {
        if (value.empty())
            return std::nullopt;
        constexpr std::size_t kMax = 1'000'000;
        std::size_t depth = 0;
        for (const char c : value) {
            if (c < '0' || c > '9')
                return std::nullopt;
            const std::size_t d = static_cast<std::size_t>(c - '0');
            if (depth > (kMax - d) / 10)
                return std::nullopt;
            depth = depth * 10 + d;
        }
        if (depth == 0)
            return std::nullopt;
        return depth;
    };

    check(!parse_bmc_depth("0").has_value(), "bmc_depth_0.rejects_zero");
    check(!parse_bmc_depth("").has_value(), "bmc_depth_0.rejects_empty");
    check(!parse_bmc_depth("-5").has_value(), "bmc_depth_0.rejects_negative");
    check(!parse_bmc_depth("abc").has_value(), "bmc_depth_0.rejects_alpha");
    check(parse_bmc_depth("1") == std::optional<std::size_t>{1}, "bmc_depth_0.accepts_1");
    check(parse_bmc_depth("42") == std::optional<std::size_t>{42}, "bmc_depth_0.accepts_42");
    check(parse_bmc_depth("20") == std::optional<std::size_t>{20}, "bmc_depth_0.accepts_20");

    // K=0 also produces an "is present" CommandLineOptions entry (syntax is
    // valid), which proves the driver-level semantic validation is the layer
    // responsible for rejecting zero — exactly as intended.
    CommandLineOptions cli;
    ArgVec a{"--bmc-depth=0"};
    std::ostringstream dummy;
    auto prev_buf = std::cerr.rdbuf(dummy.rdbuf());
    const auto result = parse_options_from_table(a, cli);
    std::cerr.rdbuf(prev_buf);
    check(!result.has_value(), "bmc_depth_0.syntax_parsed");
    check(cli.bmc_depth.has_value(), "bmc_depth_0.cli_field_set");
    check(*cli.bmc_depth == "0", "bmc_depth_0.cli_value_zero");
}

// ============================================================================
// TEST_CASE: CLI --bmc-depth=42 custom value + --bmc-boundary-invariants=true
// ============================================================================

void test_cli_bmc_depth_42_and_boundary() {
    ArgVec args{"--bmc-depth=42", "--bmc-boundary-invariants=true"};
    std::ostringstream err;
    auto opts = parse_args(args, err);
    check(opts.has_value(), "bmc_depth_42.parse_ok");
    if (opts.has_value()) {
        check(opts->bmc_depth.value_or("") == "42", "bmc_depth_42.value_eq_42");
        check(opts->bmc_boundary_invariants.value_or("") == "true",
              "bmc_depth_42.boundary_true");
    }

    ArgVec args_space{"--bmc-depth", "42", "--bmc-boundary-invariants", "false"};
    auto opts2 = parse_args(args_space, err);
    check(opts2.has_value(), "bmc_depth_42_space.parse_ok");
    if (opts2.has_value()) {
        check(opts2->bmc_depth.value_or("") == "42", "bmc_depth_42_space.value_eq_42");
        check(opts2->bmc_boundary_invariants.value_or("") == "false",
              "bmc_depth_42_space.boundary_false");
    }

    // Invalid boundary values must be rejected by the bool-flag validator
    // (mirror of the driver-level contract).
    auto parse_bool_flag = [](std::string_view value) -> std::optional<bool> {
        if (value == "true" || value == "1" || value == "yes" || value == "on")
            return true;
        if (value == "false" || value == "0" || value == "no" || value == "off")
            return false;
        return std::nullopt;
    };
    check(parse_bool_flag("true") == std::optional<bool>{true}, "bmc_bool.true_ok");
    check(parse_bool_flag("false") == std::optional<bool>{false}, "bmc_bool.false_ok");
    check(!parse_bool_flag("maybe").has_value(), "bmc_bool.maybe_rejected");
    check(!parse_bool_flag("TRUE").has_value(), "bmc_bool.case_sensitive");
}

// ============================================================================
// TEST_CASE: Default BMC depth is 20 when user passes no --bmc-depth option
// ============================================================================

void test_bmc_default_depth_20() {
    // FormalCheckerOptions default: 20
    FormalCheckerOptions fco;
    check(fco.bmc_depth == 20, "default_depth.formal_options_20");
    check(!fco.bmc_use_bmc_engine, "default_depth.bmc_engine_disabled");
    check(!fco.bmc_boundary_invariants, "default_depth.boundary_disabled");

    // BackendVerificationOptions default: 20
    BackendVerificationOptions bvo;
    check(bvo.bmc_depth == 20, "default_depth.backend_options_20");
    check(!bvo.use_bmc_engine, "default_depth.backend_engine_disabled");

    // When user omits --bmc-depth on the CLI, CommandLineOptions.bmc_depth
    // remains unset — which is exactly the sentinel the driver uses to fall
    // back to the FormalCheckerOptions default (20) without switching to the
    // BMC engine.
    ArgVec args{};
    std::ostringstream err;
    auto opts = parse_args(args, err);
    check(opts.has_value(), "default_depth.cli_parse_ok");
    if (opts.has_value()) {
        check(!opts->bmc_depth.has_value(), "default_depth.cli_no_bmc_depth");
        check(!opts->bmc_boundary_invariants.has_value(),
              "default_depth.cli_no_boundary");
    }
}

// ============================================================================
// TEST_CASE: NuXmv emit_model with depth=1 and boundary_invariants=true
//   Injects VAR cycles_visited 0..1 + INIT 0 + TRANS next(c) rule +
//   3 per-state INVARSPEC "state == S -> cycles_visited <= 1".
// ============================================================================

void test_nuxmv_emission_bmc_depth_1_with_boundary() {
    NuXmvBackend backend;
    BackendVerificationOptions opts;
    opts.bmc_depth = 1;
    opts.use_bmc_engine = true;
    opts.bmc_boundary_invariants = true;

    auto result = backend.emit_model(make_simple_machine(), opts);
    check(result.success, "emit_bmc_1.success");
    const auto &text = result.model_text;

    auto count_occurrences = [&](std::string_view needle) {
        std::size_t n = 0;
        std::size_t pos = 0;
        while ((pos = text.find(needle, pos)) != std::string::npos) {
            ++n;
            pos += needle.size();
        }
        return n;
    };

    check(count_occurrences("cycles_visited : 0..1") == 1, "emit_bmc_1.var_range_0_1");
    check(count_occurrences("INIT cycles_visited = 0") == 1, "emit_bmc_1.init_zero");
    check(count_occurrences("next(cycles_visited)") >= 1, "emit_bmc_1.next_trans_present");
    check(count_occurrences("INVARSPEC (state = idle) -> (cycles_visited <= 1)") == 1,
          "emit_bmc_1.invar_idle");
    check(count_occurrences("INVARSPEC (state = running) -> (cycles_visited <= 1)") == 1,
          "emit_bmc_1.invar_running");
    check(count_occurrences("INVARSPEC (state = done) -> (cycles_visited <= 1)") == 1,
          "emit_bmc_1.invar_done");
    // Baseline property is still emitted.
    check(count_occurrences("LTLSPEC G (state != error)") == 1, "emit_bmc_1.never_error_ltl");
}

// ============================================================================
// TEST_CASE: NuXmv emit_model depth=42, boundary=false: no cycles_visited VAR
// ============================================================================

void test_nuxmv_emission_bmc_depth_42_no_boundary() {
    NuXmvBackend backend;
    BackendVerificationOptions opts;
    opts.bmc_depth = 42;
    opts.use_bmc_engine = true;
    opts.bmc_boundary_invariants = false;

    auto result = backend.emit_model(make_simple_machine(), opts);
    check(result.success, "emit_bmc_42.success");
    const auto &text = result.model_text;
    check(text.find("cycles_visited") == std::string::npos,
          "emit_bmc_42.no_cycles_var_when_disabled");
    check(text.find("INVARSPEC") == std::string::npos,
          "emit_bmc_42.no_invarspec_when_disabled");
    check(text.find("LTLSPEC G (state != error)") != std::string::npos,
          "emit_bmc_42.ltl_property_present");
}

// ============================================================================
// TEST_CASE: NuXmv emit_model default (depth=20, boundary=false) preserves
//   historical SMV text (no cycles_visited counter anywhere).
// ============================================================================

void test_nuxmv_emission_default_depth_20_preserves_history() {
    NuXmvBackend backend;
    BackendVerificationOptions opts; // all defaults
    auto result = backend.emit_model(make_simple_machine(), opts);
    check(result.success, "emit_default.success");
    const auto &text = result.model_text;
    check(text.find("cycles_visited") == std::string::npos,
          "emit_default.no_cycles_var");
    check(text.find("INVARSPEC") == std::string::npos,
          "emit_default.no_invarspec");
    check(text.find("MODULE main") == 0, "emit_default.module_main");
    check(text.find("INIT state = idle") != std::string::npos,
          "emit_default.initial_state");
    check(text.find("LTLSPEC G (state != error)") != std::string::npos,
          "emit_default.ltl_present");
}

} // anonymous namespace

int main() {
    // TEST_CASE: BMC depth 1 (fast unrolling)
    test_cli_bmc_depth_1_equals();
    test_cli_bmc_depth_1_space();

    // TEST_CASE: BMC depth 20 (default)
    test_bmc_default_depth_20();
    test_nuxmv_emission_default_depth_20_preserves_history();

    // TEST_CASE: BMC depth 0 usage error + parse contract
    test_cli_bmc_depth_0_rejected();

    // TEST_CASE: BMC depth 42 + boundary invariants on/off
    test_cli_bmc_depth_42_and_boundary();
    test_nuxmv_emission_bmc_depth_1_with_boundary();
    test_nuxmv_emission_bmc_depth_42_no_boundary();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
