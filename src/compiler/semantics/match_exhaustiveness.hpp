#pragma once

#include "ahfl/compiler/frontend/ast.hpp"
#include "ahfl/compiler/semantics/declaration_info.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace ahfl {

struct MatchMissingVariant {
    std::string name;
    SourceRange declaration_range;
};

struct MatchMissingPatternsDiagnostic {
    SourceRange match_range;
    SourceRange enum_declaration_range;
    std::vector<MatchMissingVariant> variants;
};

struct MatchUnreachableArmDiagnostic {
    std::size_t arm_index{0}; // 1-based source order.
    SourceRange pattern_range;
    std::vector<std::size_t> covering_arm_indices;
    std::vector<SourceRange> covering_arm_ranges;
};

struct MatchOverlapDiagnostic {
    std::size_t arm_index{0};          // 1-based source order.
    SourceRange pattern_range;
    std::size_t previous_arm_index{0}; // 1-based source order.
    SourceRange previous_pattern_range;
};

struct MatchExhaustivenessDiagnostics {
    std::optional<MatchMissingPatternsDiagnostic> missing_patterns;
    std::vector<MatchUnreachableArmDiagnostic> unreachable_arms;
    std::vector<MatchOverlapDiagnostic> overlaps;
};

[[nodiscard]] MatchExhaustivenessDiagnostics
analyze_match_exhaustiveness(const EnumTypeInfo &enum_info,
                             const std::vector<Owned<ast::MatchArmSyntax>> &arms,
                             SourceRange match_range);

} // namespace ahfl
