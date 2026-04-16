#pragma once

#include "ahfl/ast.hpp"
#include "ahfl/diagnostics.hpp"
#include "ahfl/resolver.hpp"
#include "ahfl/typecheck.hpp"

namespace ahfl {

struct ValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

class Validator {
  public:
    [[nodiscard]] ValidationResult validate(const ast::Program &program,
                                            const ResolveResult &resolve_result,
                                            const TypeCheckResult &type_check_result) const;
};

} // namespace ahfl
