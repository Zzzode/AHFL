#pragma once

#include "ahfl/compiler/ir/program.hpp"

#include <optional>
#include <string>
#include <vector>

namespace ahfl::ir {

enum class VerificationSeverity {
    Error,
    Warning,
};

struct VerificationDiagnostic {
    VerificationSeverity severity{VerificationSeverity::Error};
    std::string path;
    std::string message;
};

struct VerificationResult {
    std::vector<VerificationDiagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept;
    [[nodiscard]] bool has_warnings() const noexcept;
};

[[nodiscard]] VerificationResult verify_ir_program(const Program &program);

} // namespace ahfl::ir
