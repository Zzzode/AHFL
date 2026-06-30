#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "ahfl/compiler/ir/ir.hpp"
#include "verification/formal/model_checker_backend.hpp"
#include "verification/formal/state_space_estimator.hpp"

namespace ahfl::formal {

enum class FormalVerificationStatus {
    Passed,
    Failed,
    CheckerUnavailable,
    VerificationUnsupported,
    CheckerError,
};

struct FormalCheckerOptions {
    std::optional<std::string> checker_path;
    std::optional<std::filesystem::path> model_output_path;
    ModelCheckerKind backend_kind{ModelCheckerKind::NuXmv};
    std::chrono::seconds checker_timeout{60};
    bool explain = false;
    std::size_t bmc_depth{20};
    bool bmc_use_bmc_engine{false};
    bool bmc_boundary_invariants{false};
};

struct FormalVerificationResult {
    FormalVerificationStatus status{FormalVerificationStatus::CheckerError};
    ModelCheckerKind backend_kind{ModelCheckerKind::NuXmv};
    ModelCheckerCapabilities capabilities;
    ModelCheckerAvailabilityStatus availability_status{
        ModelCheckerAvailabilityStatus::VerificationUnsupported};
    std::string checker_path;
    std::chrono::seconds checker_timeout{60};
    bool checker_timed_out{false};
    std::filesystem::path model_path;
    bool model_path_retained{false};
    int exit_code{-1};
    std::size_t expected_specification_count{0};
    std::vector<std::string> proven_specifications;
    std::vector<std::string> failing_specifications;
    std::vector<std::string> counterexample_excerpt;
    std::vector<std::string> counterexample_mappings;
    StateSpaceEstimate state_space_estimate;
    std::string model_content;
    std::string output;
    std::string error_message;
    std::optional<std::string> structured_explanation_json;
};

[[nodiscard]] std::optional<ModelCheckerKind> parse_model_checker_kind(std::string_view value);
[[nodiscard]] std::string_view model_checker_kind_name(ModelCheckerKind kind);

[[nodiscard]] bool is_formal_verification_success(const FormalVerificationResult &result);

[[nodiscard]] FormalVerificationResult
verify_program_with_smv_checker(const ir::Program &program, const FormalCheckerOptions &options);

void print_formal_verification_report(const FormalVerificationResult &result, std::ostream &out);

} // namespace ahfl::formal
