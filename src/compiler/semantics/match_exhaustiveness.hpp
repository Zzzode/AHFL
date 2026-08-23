#pragma once

#include "ahfl/compiler/frontend/ast.hpp"
#include "ahfl/compiler/semantics/declaration_info.hpp"
#include "ahfl/compiler/semantics/typed_hir.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
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
    std::vector<MatchMissingVariant> variants{};
    std::vector<std::string> witnesses{};
};

struct MatchUnreachableArmDiagnostic {
    std::size_t arm_index{0}; // 1-based source order.
    SourceRange pattern_range;
    std::vector<std::size_t> covering_arm_indices{};
    std::vector<SourceRange> covering_arm_ranges{};
};

struct MatchOverlapDiagnostic {
    std::size_t arm_index{0}; // 1-based source order.
    SourceRange pattern_range;
    std::size_t previous_arm_index{0}; // 1-based source order.
    SourceRange previous_pattern_range;
};

struct MatchRedundantPatternDiagnostic {
    std::size_t arm_index{0};    // 1-based source order.
    std::size_t branch_index{0}; // 1-based source order inside the or-pattern.
    SourceRange branch_range;
    std::vector<std::size_t> covering_arm_indices{};
    std::vector<SourceRange> covering_arm_ranges{};
    std::vector<std::size_t> covering_branch_indices{};
    std::vector<SourceRange> covering_branch_ranges{};
};

struct MatchExhaustivenessDiagnostics {
    std::optional<MatchMissingPatternsDiagnostic> missing_patterns;
    std::vector<MatchUnreachableArmDiagnostic> unreachable_arms;
    std::vector<MatchOverlapDiagnostic> overlaps;
    std::vector<MatchRedundantPatternDiagnostic> redundant_patterns;
};

[[nodiscard]] MatchExhaustivenessDiagnostics
analyze_match_exhaustiveness(const EnumTypeInfo &enum_info,
                             const std::vector<Owned<ast::MatchArmSyntax>> &arms,
                             SourceRange match_range);

using MatchEnumInfoResolver = std::function<std::optional<EnumTypeInfo>(const Type &)>;

[[nodiscard]] MatchExhaustivenessDiagnostics
analyze_match_exhaustiveness(const Type &scrutinee_type,
                             const EnumTypeInfo &enum_info,
                             const std::vector<Owned<ast::MatchArmSyntax>> &arms,
                             SourceRange match_range,
                             const MatchEnumInfoResolver &enum_resolver);

struct MatchTypedPatternRow {
    std::uint32_t pattern_index{UINT32_MAX};
    SourceRange range;
    bool contributes_to_exhaustiveness{true};
};

using MatchTypedPatternResolver = std::function<const TypedPattern *(std::uint32_t)>;

[[nodiscard]] MatchExhaustivenessDiagnostics
analyze_match_exhaustiveness(const Type &scrutinee_type,
                             const EnumTypeInfo &enum_info,
                             const std::vector<MatchTypedPatternRow> &rows,
                             SourceRange match_range,
                             const MatchEnumInfoResolver &enum_resolver,
                             const MatchTypedPatternResolver &pattern_resolver);

} // namespace ahfl
