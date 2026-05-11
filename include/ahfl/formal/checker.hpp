#pragma once

#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "ahfl/ir/ir.hpp"

namespace ahfl::formal {

enum class FormalVerificationStatus {
    Passed,
    Failed,
    CheckerError,
};

struct FormalCheckerOptions {
    std::optional<std::string> checker_path;
    std::optional<std::filesystem::path> model_output_path;
};

struct FormalVerificationResult {
    FormalVerificationStatus status{FormalVerificationStatus::CheckerError};
    std::string checker_path;
    std::filesystem::path model_path;
    bool model_path_retained{false};
    int exit_code{-1};
    std::size_t expected_specification_count{0};
    std::vector<std::string> proven_specifications;
    std::vector<std::string> failing_specifications;
    std::vector<std::string> counterexample_excerpt;
    std::vector<std::string> counterexample_mappings;
    std::string output;
    std::string error_message;
};

[[nodiscard]] bool is_formal_verification_success(const FormalVerificationResult &result);

[[nodiscard]] FormalVerificationResult
verify_program_with_smv_checker(const ir::Program &program, const FormalCheckerOptions &options);

void print_formal_verification_report(const FormalVerificationResult &result, std::ostream &out);

} // namespace ahfl::formal
