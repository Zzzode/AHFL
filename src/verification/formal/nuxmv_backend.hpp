#pragma once

#include "verification/formal/model_checker_backend.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace ahfl::formal {

enum class NuXmvOutputStatus {
    Unknown,
    Passed,
    Failed,
    Error,
    Timeout,
};

struct NuXmvVerificationOutput {
    NuXmvOutputStatus status{NuXmvOutputStatus::Unknown};
    std::size_t properties_checked{0};
    std::size_t properties_passed{0};
    std::string counterexample_trace;
    std::string error;
};

[[nodiscard]] NuXmvVerificationOutput parse_nuxmv_verification_output(std::string_view output,
                                                                      bool timed_out = false);

class NuXmvBackend final : public ModelCheckerBackend {
  public:
    [[nodiscard]] ModelCheckerKind kind() const override;
    [[nodiscard]] std::string_view name() const override;
    [[nodiscard]] std::string_view file_extension() const override;
    [[nodiscard]] ModelCheckerCapabilities capabilities() const override;
    [[nodiscard]] ModelCheckerAvailability availability() const override;
    [[nodiscard]] ModelEmissionResult emit_model(const BmcStateMachine &machine) override;
    [[nodiscard]] ModelEmissionResult
    emit_model(const BmcStateMachine &machine, const BackendVerificationOptions &options);
    [[nodiscard]] VerificationSummary verify(const std::string &model_text,
                                             const BackendVerificationOptions &options) override;
};

} // namespace ahfl::formal
