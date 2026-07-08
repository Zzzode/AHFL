#include "compiler/semantics/match_exhaustiveness.hpp"

#include "ahfl/compiler/semantics/pattern_usefulness.hpp"

#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace ahfl {

namespace {

using VariantOrdinal = std::size_t;

struct MatchMatrixLowering {
    PatternUsefulnessContext context;
    PatternDomainId root_domain;
    std::vector<PatternConstructorId> variant_constructors;
    std::vector<std::optional<VariantOrdinal>> variant_for_constructor;
};

[[nodiscard]] std::string_view last_segment(const ast::QualifiedName &name) noexcept {
    if (name.segments.empty()) {
        return {};
    }
    return name.segments.back();
}

[[nodiscard]] std::optional<VariantOrdinal> variant_ordinal(const EnumTypeInfo &enum_info,
                                                            std::string_view name) noexcept {
    for (VariantOrdinal index = 0; index < enum_info.variants.size(); ++index) {
        if (enum_info.variants[index].name == name) {
            return index;
        }
    }
    return std::nullopt;
}

void remember_constructor_variant(MatchMatrixLowering &lowering,
                                  PatternConstructorId constructor,
                                  VariantOrdinal variant) {
    if (constructor.value >= lowering.variant_for_constructor.size()) {
        lowering.variant_for_constructor.resize(constructor.value + 1);
    }
    lowering.variant_for_constructor[constructor.value] = variant;
}

[[nodiscard]] MatchMatrixLowering make_lowering(const EnumTypeInfo &enum_info) {
    MatchMatrixLowering lowering{
        .root_domain = PatternDomainId{0},
    };
    lowering.root_domain = lowering.context.add_domain();
    lowering.variant_constructors.reserve(enum_info.variants.size());
    for (VariantOrdinal index = 0; index < enum_info.variants.size(); ++index) {
        const auto &variant = enum_info.variants[index];
        const auto constructor =
            lowering.context.add_constructor(lowering.root_domain, variant.name, {});
        lowering.variant_constructors.push_back(constructor);
        remember_constructor_variant(lowering, constructor, index);
    }
    return lowering;
}

[[nodiscard]] PatternId
constructor_pattern(MatchMatrixLowering &lowering, VariantOrdinal variant, SourceRange range) {
    return lowering.context.make_constructor_pattern(
        lowering.variant_constructors[variant], {}, range);
}

[[nodiscard]] PatternId lower_pattern(const ast::PatternSyntax &pattern,
                                      const EnumTypeInfo &enum_info,
                                      MatchMatrixLowering &lowering);

[[nodiscard]] PatternId lower_tuple_pattern(const ast::TuplePattern &tuple,
                                            SourceRange range,
                                            const EnumTypeInfo &enum_info,
                                            MatchMatrixLowering &lowering) {
    if (tuple.elements.empty()) {
        return lowering.context.make_never(range);
    }

    bool all_elements_irrefutable = true;
    std::vector<PatternId> branches;
    branches.reserve(tuple.elements.size());
    for (const auto &element : tuple.elements) {
        if (!element) {
            all_elements_irrefutable = false;
            continue;
        }
        const auto lowered = lower_pattern(*element, enum_info, lowering);
        if (lowering.context.pattern(lowered).kind == PatternNodeKind::Wildcard) {
            continue;
        }
        all_elements_irrefutable = false;
        branches.push_back(lowered);
    }

    if (all_elements_irrefutable) {
        return lowering.context.make_wildcard(range);
    }
    if (branches.empty()) {
        return lowering.context.make_never(range);
    }
    if (branches.size() == 1) {
        return branches.front();
    }
    return lowering.context.make_or_pattern(std::move(branches), range);
}

[[nodiscard]] PatternId lower_pattern(const ast::PatternSyntax &pattern,
                                      const EnumTypeInfo &enum_info,
                                      MatchMatrixLowering &lowering) {
    return std::visit(
        [&](const auto &node) -> PatternId {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, ast::WildcardPattern>) {
                return lowering.context.make_wildcard(pattern.range);
            } else if constexpr (std::is_same_v<T, ast::BindingPattern>) {
                if (!node.nested) {
                    const auto ordinal = variant_ordinal(enum_info, node.name);
                    if (ordinal.has_value()) {
                        return constructor_pattern(lowering, *ordinal, pattern.range);
                    }
                    return lowering.context.make_wildcard(pattern.range);
                }
                return lower_pattern(*node.nested, enum_info, lowering);
            } else if constexpr (std::is_same_v<T, ast::VariantPattern>) {
                if (node.path == nullptr) {
                    return lowering.context.make_never(pattern.range);
                }
                const auto variant_name = last_segment(*node.path);
                const auto ordinal = variant_ordinal(enum_info, variant_name);
                if (variant_name.empty() || !ordinal.has_value()) {
                    return lowering.context.make_never(pattern.range);
                }
                return constructor_pattern(lowering, *ordinal, pattern.range);
            } else if constexpr (std::is_same_v<T, ast::OrPattern>) {
                std::vector<PatternId> branches;
                branches.reserve(node.branches.size());
                for (const auto &branch : node.branches) {
                    if (!branch) {
                        continue;
                    }
                    branches.push_back(lower_pattern(*branch, enum_info, lowering));
                }
                if (branches.empty()) {
                    return lowering.context.make_never(pattern.range);
                }
                if (branches.size() == 1) {
                    return branches.front();
                }
                return lowering.context.make_or_pattern(std::move(branches), pattern.range);
            } else if constexpr (std::is_same_v<T, ast::TuplePattern>) {
                return lower_tuple_pattern(node, pattern.range, enum_info, lowering);
            } else {
                return lowering.context.make_never(pattern.range);
            }
        },
        pattern.node);
}

[[nodiscard]] std::optional<VariantOrdinal> variant_for_witness(const MatchMatrixLowering &lowering,
                                                                const PatternWitness &witness) {
    if (witness.constructor.value >= lowering.variant_for_constructor.size()) {
        return std::nullopt;
    }
    return lowering.variant_for_constructor[witness.constructor.value];
}

void add_missing_patterns(MatchExhaustivenessDiagnostics &diagnostics,
                          const PatternUsefulnessAnalysis &analysis,
                          const MatchMatrixLowering &lowering,
                          const EnumTypeInfo &enum_info,
                          SourceRange match_range) {
    if (analysis.missing_witnesses.empty()) {
        return;
    }

    MatchMissingPatternsDiagnostic missing{
        .match_range = match_range,
        .enum_declaration_range = enum_info.declaration_range,
    };
    std::vector<bool> added(enum_info.variants.size(), false);
    for (const auto &witness : analysis.missing_witnesses) {
        const auto variant_index = variant_for_witness(lowering, witness);
        if (!variant_index.has_value() || *variant_index >= enum_info.variants.size() ||
            added[*variant_index]) {
            continue;
        }
        const auto &variant = enum_info.variants[*variant_index];
        missing.variants.push_back(MatchMissingVariant{
            .name = variant.name,
            .declaration_range = variant.declaration_range,
        });
        added[*variant_index] = true;
    }

    if (!missing.variants.empty()) {
        diagnostics.missing_patterns = std::move(missing);
    }
}

[[nodiscard]] MatchUnreachableArmDiagnostic
make_unreachable(const PatternUnreachableRow &row, const std::vector<std::size_t> &arm_indices) {
    MatchUnreachableArmDiagnostic diagnostic{
        .arm_index = arm_indices[row.row_index],
        .pattern_range = row.range,
        .covering_arm_ranges = row.covering_row_ranges,
    };
    diagnostic.covering_arm_indices.reserve(row.covering_row_indices.size());
    for (const auto covering_row : row.covering_row_indices) {
        diagnostic.covering_arm_indices.push_back(arm_indices[covering_row]);
    }
    return diagnostic;
}

[[nodiscard]] MatchOverlapDiagnostic make_overlap(const PatternOverlapRow &row,
                                                  const std::vector<std::size_t> &arm_indices) {
    return MatchOverlapDiagnostic{
        .arm_index = arm_indices[row.row_index],
        .pattern_range = row.range,
        .previous_arm_index = arm_indices[row.previous_row_index],
        .previous_pattern_range = row.previous_range,
    };
}

} // namespace

MatchExhaustivenessDiagnostics
analyze_match_exhaustiveness(const EnumTypeInfo &enum_info,
                             const std::vector<Owned<ast::MatchArmSyntax>> &arms,
                             SourceRange match_range) {
    MatchExhaustivenessDiagnostics diagnostics;
    auto lowering = make_lowering(enum_info);

    std::vector<PatternUsefulnessRow> rows;
    std::vector<std::size_t> arm_indices;
    rows.reserve(arms.size());
    arm_indices.reserve(arms.size());
    for (std::size_t index = 0; index < arms.size(); ++index) {
        const auto &arm = arms[index];
        if (!arm || !arm->pattern) {
            continue;
        }
        const auto pattern_id = lower_pattern(*arm->pattern, enum_info, lowering);
        rows.push_back(PatternUsefulnessRow{
            .pattern = pattern_id,
            .range = arm->pattern->range,
            .contributes_to_exhaustiveness = arm->guard == nullptr,
        });
        arm_indices.push_back(index + 1);
    }

    const auto analysis = analyze_pattern_usefulness(lowering.context, lowering.root_domain, rows);
    add_missing_patterns(diagnostics, analysis, lowering, enum_info, match_range);

    diagnostics.unreachable_arms.reserve(analysis.unreachable_rows.size());
    for (const auto &row : analysis.unreachable_rows) {
        diagnostics.unreachable_arms.push_back(make_unreachable(row, arm_indices));
    }

    diagnostics.overlaps.reserve(analysis.overlaps.size());
    for (const auto &row : analysis.overlaps) {
        diagnostics.overlaps.push_back(make_overlap(row, arm_indices));
    }

    return diagnostics;
}

} // namespace ahfl
