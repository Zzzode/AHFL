// RFC 0017 (BMC Contract Semantics) slice 4 unit tests: SMT solver seam.
//
// Exercises the pure output parser (parse_solver_output) and availability
// resolution (resolve_smt_solver) without requiring a real solver binary.
// The full run_smt_solver path is covered by the integration test only when
// AHFL_Z3_PATH points at a real binary.

#include "verification/formal/smt_solver.hpp"

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

void test_parse_sat() {
    auto r = parse_solver_output("sat\n", 0, false);
    check(r.status == SmtSolverStatus::Sat && r.ran(), "parses sat");
}

void test_parse_unsat() {
    auto r = parse_solver_output("unsat\n", 0, false);
    check(r.status == SmtSolverStatus::Unsat && r.ran(), "parses unsat");
}

void test_parse_unknown() {
    auto r = parse_solver_output("unknown\n", 0, false);
    check(r.status == SmtSolverStatus::Unknown && r.ran(), "parses unknown");
}

void test_parse_verdict_after_error_lines() {
    // Z3 prints (error ...) lines to stdout before the verdict.
    auto r = parse_solver_output("(error \"line 1: unsupported\")\nunsat\n", 0, false);
    check(r.status == SmtSolverStatus::Unsat, "verdict found after error lines");
}

void test_parse_timeout() {
    auto r = parse_solver_output("", 0, true);
    check(r.status == SmtSolverStatus::Timeout && !r.ran(), "timeout maps to Timeout");
}

void test_parse_no_verdict_clean_exit() {
    auto r = parse_solver_output("(error \"boom\")\n", 0, false);
    check(r.status == SmtSolverStatus::SolverError && !r.ran(),
          "no verdict on clean exit is an error, never a pass");
}

void test_parse_no_verdict_nonzero_exit() {
    auto r = parse_solver_output("", 1, false);
    check(r.status == SmtSolverStatus::SolverError && !r.error_message.empty(),
          "non-zero exit with no verdict is an error");
}

void test_parse_leading_whitespace() {
    auto r = parse_solver_output("   sat  \n", 0, false);
    check(r.status == SmtSolverStatus::Sat, "verdict token is trimmed");
}

void test_availability_bad_explicit_path() {
    SmtSolverOptions options;
    options.solver_path = "/nonexistent/path/to/z3";
    auto availability = resolve_smt_solver(options);
    check(!availability.available && !availability.reason.empty(),
          "bad explicit solver path is unavailable with a reason");
    check(availability.required_binary == "z3", "required_binary names z3");
}

void test_run_unavailable_never_passes() {
    // With a bogus explicit path the solver is unavailable — must be
    // SolverUnavailable, never Unsat (which would falsely read as "proven").
    SmtSolverOptions options;
    options.solver_path = "/nonexistent/path/to/z3";
    auto r = run_smt_solver("(check-sat)\n", options);
    check(r.status == SmtSolverStatus::SolverUnavailable && !r.ran(),
          "unavailable solver never yields a verdict");
}

void test_status_names() {
    check(smt_solver_status_name(SmtSolverStatus::Sat) == "sat", "sat name");
    check(smt_solver_status_name(SmtSolverStatus::SolverUnavailable) == "solver_unavailable",
          "unavailable name");
}

// Real-solver integration, run only when AHFL_Z3_PATH points at a Z3 binary
// (set by the guarded ctest). run_smt_solver reads AHFL_Z3_PATH itself, so no
// explicit path is threaded here.
void test_real_solver_roundtrip() {
    const char *z3 = std::getenv("AHFL_Z3_PATH");
    if (z3 == nullptr || *z3 == '\0') {
        return; // not requested; unit assertions above already ran
    }

    SmtSolverOptions options;
    auto unsat = run_smt_solver(
        "(set-logic QF_LIA)\n(declare-const x Int)\n(assert (> x 0))\n(assert (< x 0))\n(check-sat)\n",
        options);
    check(unsat.status == SmtSolverStatus::Unsat, "real Z3 proves contradictory constraints unsat");

    auto sat = run_smt_solver(
        "(set-logic QF_LIA)\n(declare-const x Int)\n(assert (> x 0))\n(check-sat)\n", options);
    check(sat.status == SmtSolverStatus::Sat, "real Z3 finds a satisfying model");
}

} // namespace

int main() {
    test_parse_sat();
    test_parse_unsat();
    test_parse_unknown();
    test_parse_verdict_after_error_lines();
    test_parse_timeout();
    test_parse_no_verdict_clean_exit();
    test_parse_no_verdict_nonzero_exit();
    test_parse_leading_whitespace();
    test_availability_bad_explicit_path();
    test_run_unavailable_never_passes();
    test_status_names();
    test_real_solver_roundtrip();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
