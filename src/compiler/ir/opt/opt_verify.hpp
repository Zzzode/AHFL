#pragma once

#include "ahfl/compiler/ir/opt/opt_ir.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ahfl::ir::opt {

enum class VerificationSeverity {
    Error,
    Warning,
};

struct VerificationDiagnostic {
    VerificationSeverity severity{VerificationSeverity::Error};
    std::string function_name;
    std::optional<std::uint32_t> block_id;
    std::string message;
};

struct VerificationResult {
    std::vector<VerificationDiagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept;
    [[nodiscard]] bool has_warnings() const noexcept;
};

[[nodiscard]] VerificationResult verify_opt_function(const OptFunction &function);
[[nodiscard]] VerificationResult verify_opt_program(const OptProgram &program);

} // namespace ahfl::ir::opt
