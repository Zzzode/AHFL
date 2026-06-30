#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "verification/formal/bmc.hpp"

namespace ahfl::formal {

enum class ModelCheckerKind {
    NuSMV,
    NuXmv,
    SPIN,
    TLAPlus,
};

enum class ModelCheckerAvailabilityStatus {
    Available,
    MissingBinary,
    VerificationUnsupported,
};

struct ModelCheckerCapabilities {
    bool emits_model{true};
    bool supports_external_verification{false};
    bool supports_ahfl_smv_semantics{false};
    std::string required_binary;
    std::vector<std::string> property_semantics;
    std::string skip_reason;
};

struct ModelCheckerAvailability {
    ModelCheckerAvailabilityStatus status{ModelCheckerAvailabilityStatus::VerificationUnsupported};
    std::string binary_path;
    std::string reason;

    [[nodiscard]] bool can_verify() const noexcept {
        return status == ModelCheckerAvailabilityStatus::Available;
    }
};

struct ModelEmissionResult {
    bool success{false};
    std::string model_text;
    std::string error_message;
};

struct VerificationSummary {
    bool all_passed{false};
    std::size_t properties_checked{0};
    std::size_t properties_passed{0};
    bool timed_out{false};
    std::string raw_output;
    std::string error_message;
};

/// Runtime options forwarded from the CLI / FormalCheckerOptions to the
/// backend's verify() call.
struct BackendVerificationOptions {
    std::chrono::seconds timeout{60};
    std::size_t bmc_depth{20};
    bool use_bmc_engine{false};
    bool bmc_boundary_invariants{false};
};

class ModelCheckerBackend {
  public:
    virtual ~ModelCheckerBackend() = default;
    [[nodiscard]] virtual ModelCheckerKind kind() const = 0;
    [[nodiscard]] virtual std::string_view name() const = 0;
    [[nodiscard]] virtual std::string_view file_extension() const = 0;
    [[nodiscard]] virtual ModelCheckerCapabilities capabilities() const = 0;
    [[nodiscard]] virtual ModelCheckerAvailability availability() const = 0;
    [[nodiscard]] virtual ModelEmissionResult emit_model(const BmcStateMachine &machine) = 0;
    [[nodiscard]] virtual VerificationSummary
    verify(const std::string &model_text, const BackendVerificationOptions &options) = 0;
};

[[nodiscard]] std::unique_ptr<ModelCheckerBackend> create_backend(ModelCheckerKind kind);

} // namespace ahfl::formal
