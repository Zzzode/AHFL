#pragma once

#include "ahfl/frontend/ast.hpp"
#include "ahfl/semantics/resolver.hpp"
#include "ahfl/semantics/typecheck.hpp"
#include "ahfl/support/diagnostics.hpp"

namespace ahfl {

struct SourceGraph;

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
    [[nodiscard]] ValidationResult validate(const SourceGraph &graph,
                                            const ResolveResult &resolve_result,
                                            const TypeCheckResult &type_check_result) const;
};

} // namespace ahfl
