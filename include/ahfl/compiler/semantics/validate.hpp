#pragma once

#include <cstddef>

#include "ahfl/base/support/diagnostics.hpp"
#include "ahfl/compiler/frontend/ast.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"

namespace ahfl {

struct SourceGraph;

struct ValidationResult {
    DiagnosticBag diagnostics;

    /// Plumbing counters (P4.S5a): filled by ValidationPass by walking every
    /// contract clause. Intentionally not gated on errors so downstream passes
    /// can inspect them regardless of validation status.
    std::size_t total_decreases_clauses{0};
    std::size_t wildcard_decreases_clauses{0};

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
