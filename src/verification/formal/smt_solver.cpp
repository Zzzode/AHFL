#include "verification/formal/smt_solver.hpp"

#include "verification/formal/process_launcher.hpp"

#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>

namespace ahfl::formal {

namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::string_view solver_binary_name(SmtSolverKind kind) noexcept {
    switch (kind) {
    case SmtSolverKind::Z3:
        return "z3";
    }
    return "z3";
}

[[nodiscard]] std::string_view solver_env_var(SmtSolverKind kind) noexcept {
    switch (kind) {
    case SmtSolverKind::Z3:
        return "AHFL_Z3_PATH";
    }
    return "AHFL_Z3_PATH";
}

// Trim ASCII whitespace from both ends of a token.
[[nodiscard]] std::string_view trim(std::string_view text) noexcept {
    const auto is_space = [](char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && is_space(text[begin])) {
        ++begin;
    }
    while (end > begin && is_space(text[end - 1])) {
        --end;
    }
    return text.substr(begin, end - begin);
}

} // namespace

SmtSolverAvailability resolve_smt_solver(const SmtSolverOptions &options) {
    SmtSolverAvailability availability;
    availability.required_binary = std::string(solver_binary_name(options.kind));

    // 1. Explicit override.
    if (options.solver_path.has_value() && !options.solver_path->empty()) {
        if (fs::exists(*options.solver_path)) {
            availability.available = true;
            availability.binary_path = *options.solver_path;
            return availability;
        }
        availability.reason = "specified solver path does not exist: " + *options.solver_path;
        return availability;
    }

    // 2. Environment variable.
    if (const char *env = std::getenv(std::string(solver_env_var(options.kind)).c_str());
        env != nullptr && *env != '\0' && fs::exists(env)) {
        availability.available = true;
        availability.binary_path = env;
        return availability;
    }

    // 3. PATH lookup.
    if (auto found = find_executable(solver_binary_name(options.kind)); found.has_value()) {
        availability.available = true;
        availability.binary_path = *found;
        return availability;
    }

    availability.reason = availability.required_binary +
                          " not found; pass --smt-solver or set " +
                          std::string(solver_env_var(options.kind));
    return availability;
}

SmtSolverResult parse_solver_output(std::string_view output, int exit_code, bool timed_out) {
    SmtSolverResult result;
    result.raw_output = std::string(output);

    if (timed_out) {
        result.status = SmtSolverStatus::Timeout;
        result.error_message = "SMT solver process timed out";
        return result;
    }

    // Scan line by line for the check-sat verdict. The first recognized token
    // wins; a solver may print (error ...) lines around it.
    std::istringstream stream{std::string(output)};
    std::string line;
    while (std::getline(stream, line)) {
        const auto token = trim(line);
        if (token == "sat") {
            result.status = SmtSolverStatus::Sat;
            return result;
        }
        if (token == "unsat") {
            result.status = SmtSolverStatus::Unsat;
            return result;
        }
        if (token == "unknown") {
            result.status = SmtSolverStatus::Unknown;
            return result;
        }
    }

    // No verdict. A non-zero exit is a solver error; a clean exit with no
    // verdict is still an error (never a silent pass).
    result.status = SmtSolverStatus::SolverError;
    result.error_message = exit_code == 0
                               ? "SMT solver produced no sat/unsat/unknown verdict"
                               : "SMT solver exited with code " + std::to_string(exit_code);
    return result;
}

SmtSolverResult run_smt_solver(const std::string &smtlib_document,
                               const SmtSolverOptions &options) {
    const auto availability = resolve_smt_solver(options);
    if (!availability.available) {
        SmtSolverResult result;
        result.status = SmtSolverStatus::SolverUnavailable;
        result.error_message = availability.reason;
        return result;
    }

    ProcessConfig config;
    config.executable = availability.binary_path;
    // `-in` makes Z3 read an SMT-LIB 2 script from stdin.
    config.arguments = {"-in"};
    config.stdin_input = smtlib_document;
    config.timeout = options.timeout;

    const auto process = launch_process(config);
    return parse_solver_output(process.stdout_output, process.exit_code, process.timed_out);
}

std::string_view smt_solver_status_name(SmtSolverStatus status) noexcept {
    switch (status) {
    case SmtSolverStatus::Sat:
        return "sat";
    case SmtSolverStatus::Unsat:
        return "unsat";
    case SmtSolverStatus::Unknown:
        return "unknown";
    case SmtSolverStatus::SolverUnavailable:
        return "solver_unavailable";
    case SmtSolverStatus::SolverError:
        return "solver_error";
    case SmtSolverStatus::Timeout:
        return "timeout";
    }
    return "solver_error";
}

} // namespace ahfl::formal
