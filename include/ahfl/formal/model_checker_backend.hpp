#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "ahfl/formal/bmc.hpp"

namespace ahfl::formal {

enum class ModelCheckerKind {
    NuSMV,
    NuXmv,
    SPIN,
    TLAPlus,
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
    std::string raw_output;
    std::string error_message;
};

class ModelCheckerBackend {
public:
    virtual ~ModelCheckerBackend() = default;
    [[nodiscard]] virtual ModelCheckerKind kind() const = 0;
    [[nodiscard]] virtual std::string_view name() const = 0;
    [[nodiscard]] virtual std::string_view file_extension() const = 0;
    [[nodiscard]] virtual ModelEmissionResult
    emit_model(const BmcStateMachine &machine) = 0;
    [[nodiscard]] virtual VerificationSummary
    verify(const std::string &model_text) = 0;
};

[[nodiscard]] std::unique_ptr<ModelCheckerBackend>
create_backend(ModelCheckerKind kind);

} // namespace ahfl::formal
