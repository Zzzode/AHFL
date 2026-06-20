#pragma once

#include "verification/formal/model_checker_backend.hpp"

namespace ahfl::formal {

class NuXmvBackend final : public ModelCheckerBackend {
  public:
    [[nodiscard]] ModelCheckerKind kind() const override;
    [[nodiscard]] std::string_view name() const override;
    [[nodiscard]] std::string_view file_extension() const override;
    [[nodiscard]] ModelEmissionResult emit_model(const BmcStateMachine &machine) override;
    [[nodiscard]] VerificationSummary verify(const std::string &model_text) override;
};

} // namespace ahfl::formal
