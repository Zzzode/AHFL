#pragma once

// RFC 0017 (BMC Contract Semantics) slice 4: SMT solver seam.
//
// Runs an external SMT-LIB 2 document (as produced by smt_emit / the SMT-BMC
// engine) through a solver process and reports sat / unsat / unknown. Z3 is
// the first supported solver (cvc5 is a follow-up); both speak SMT-LIB 2 on
// stdin, so the seam is solver-agnostic behind a small capability struct.
//
// The output parser (parse_solver_output) is a pure function split out from
// the process launch so it is unit-testable without a solver binary present —
// mirroring parse_nuxmv_verification_output on the model-checker path.

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ahfl::formal {

// The three-valued SMT result plus the not-run states. `Sat` means the
// assertion set is satisfiable (for a proof-by-refutation query, a
// counterexample exists); `Unsat` means unsatisfiable (the property holds on
// this query); `Unknown` means the solver gave up (incomplete).
enum class SmtSolverStatus {
    Sat,
    Unsat,
    Unknown,
    SolverUnavailable, // no solver binary found
    SolverError,       // solver ran but failed (non-zero exit / unparsable)
    Timeout,           // solver exceeded the timeout
};

struct SmtSolverResult {
    SmtSolverStatus status{SmtSolverStatus::SolverError};
    std::string raw_output;    // solver stdout, for diagnostics
    std::string error_message; // populated for the not-run / error states
    // Populated only when the query requested a model (options.request_model)
    // and the verdict was Sat: the parsed (name, value) assignments from the
    // solver's (get-model) block. Values are normalized SMT-LIB literals
    // (e.g. "0", "-1", "true").
    std::vector<std::pair<std::string, std::string>> model;

    [[nodiscard]] bool ran() const noexcept {
        return status == SmtSolverStatus::Sat || status == SmtSolverStatus::Unsat ||
               status == SmtSolverStatus::Unknown;
    }
};

// Which solver to invoke. Kind is kept explicit (not a string) so the seam and
// its capability matrix stay index-based.
enum class SmtSolverKind {
    Z3,
};

struct SmtSolverAvailability {
    bool available{false};
    std::string binary_path;   // resolved absolute path when available
    std::string required_binary; // human-readable name, e.g. "z3"
    std::string reason;        // populated when unavailable
};

struct SmtSolverOptions {
    SmtSolverKind kind{SmtSolverKind::Z3};
    std::optional<std::string> solver_path; // explicit override; else env/PATH
    std::chrono::seconds timeout{60};
    // When true, append `(get-model)` after `(check-sat)` and parse the model
    // into SmtSolverResult::model on a Sat verdict. Off by default: a plain
    // sat/unsat query needs no model.
    bool request_model{false};
};

// Resolve the solver binary: explicit path → AHFL_Z3_PATH → PATH lookup.
[[nodiscard]] SmtSolverAvailability resolve_smt_solver(const SmtSolverOptions &options);

// Parse a solver's stdout into a status. Pure: the first non-empty
// `sat`/`unsat`/`unknown` token wins; `timed_out` and a non-zero `exit_code`
// map to Timeout / SolverError. Exposed for unit tests.
[[nodiscard]] SmtSolverResult
parse_solver_output(std::string_view output, int exit_code, bool timed_out);

// Parse the `(get-model)` block of a solver's stdout into (name, value)
// assignments. Handles Z3's `(define-fun NAME () SORT VALUE)` form, including
// negative values written `(- N)`. Pure; exposed for unit tests.
[[nodiscard]] std::vector<std::pair<std::string, std::string>>
parse_solver_model(std::string_view output);

// Run `smtlib_document` through the configured solver and return the result.
// Never returns Sat/Unsat unless the solver actually ran and agreed — a
// missing binary is SolverUnavailable, never a silent pass.
[[nodiscard]] SmtSolverResult run_smt_solver(const std::string &smtlib_document,
                                             const SmtSolverOptions &options);

[[nodiscard]] std::string_view smt_solver_status_name(SmtSolverStatus status) noexcept;

} // namespace ahfl::formal
