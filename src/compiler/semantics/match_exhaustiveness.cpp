#include "compiler/semantics/match_exhaustiveness.hpp"

#include <algorithm>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace ahfl {

namespace {

using VariantOrdinal = std::size_t;

struct AtomicCoverage {
    bool all{false};
    std::unordered_set<VariantOrdinal> variants;
};

[[nodiscard]] std::string_view last_segment(const ast::QualifiedName &name) noexcept {
    if (name.segments.empty()) {
        return {};
    }
    return name.segments.back();
}

[[nodiscard]] std::optional<VariantOrdinal>
variant_ordinal(const EnumTypeInfo &enum_info, std::string_view name) noexcept {
    for (VariantOrdinal index = 0; index < enum_info.variants.size(); ++index) {
        if (enum_info.variants[index].name == name) {
            return index;
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool is_empty(const AtomicCoverage &coverage) noexcept {
    return !coverage.all && coverage.variants.empty();
}

[[nodiscard]] AtomicCoverage coverage_for_pattern(const ast::PatternSyntax &pattern,
                                                  const EnumTypeInfo &enum_info) {
    return std::visit(
        [&](const auto &node) -> AtomicCoverage {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, ast::WildcardPattern>) {
                return AtomicCoverage{.all = true};
            } else if constexpr (std::is_same_v<T, ast::BindingPattern>) {
                if (!node.nested) {
                    const auto ordinal = variant_ordinal(enum_info, node.name);
                    if (ordinal.has_value()) {
                        return AtomicCoverage{.all = false, .variants = {*ordinal}};
                    }
                }
                if (node.nested) {
                    return coverage_for_pattern(*node.nested, enum_info);
                }
                return AtomicCoverage{.all = true};
            } else if constexpr (std::is_same_v<T, ast::VariantPattern>) {
                if (node.path == nullptr) {
                    return {};
                }
                const auto variant_name = last_segment(*node.path);
                const auto ordinal = variant_ordinal(enum_info, variant_name);
                if (variant_name.empty() || !ordinal.has_value()) {
                    return {};
                }
                return AtomicCoverage{.all = false, .variants = {*ordinal}};
            } else if constexpr (std::is_same_v<T, ast::OrPattern>) {
                AtomicCoverage result;
                for (const auto &branch : node.branches) {
                    if (!branch) {
                        continue;
                    }
                    auto branch_coverage = coverage_for_pattern(*branch, enum_info);
                    if (branch_coverage.all) {
                        result.all = true;
                        result.variants.clear();
                        return result;
                    }
                    result.variants.insert(branch_coverage.variants.begin(),
                                           branch_coverage.variants.end());
                }
                return result;
            } else if constexpr (std::is_same_v<T, ast::TuplePattern>) {
                if (node.elements.empty()) {
                    return {};
                }
                bool all_elements_irrefutable = true;
                AtomicCoverage result;
                for (const auto &element : node.elements) {
                    if (!element) {
                        all_elements_irrefutable = false;
                        continue;
                    }
                    auto element_coverage = coverage_for_pattern(*element, enum_info);
                    all_elements_irrefutable = all_elements_irrefutable && element_coverage.all;
                    if (element_coverage.all) {
                        continue;
                    }
                    result.variants.insert(element_coverage.variants.begin(),
                                           element_coverage.variants.end());
                }
                if (all_elements_irrefutable) {
                    return AtomicCoverage{.all = true};
                }
                return result;
            } else {
                return {};
            }
        },
        pattern.node);
}

[[nodiscard]] bool intersects(const AtomicCoverage &lhs, const AtomicCoverage &rhs) {
    if (is_empty(lhs) || is_empty(rhs)) {
        return false;
    }
    if (lhs.all || rhs.all) {
        return true;
    }
    for (const auto &variant : lhs.variants) {
        if (rhs.variants.contains(variant)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool all_variants_covered(const EnumTypeInfo &enum_info,
                                        const std::unordered_set<VariantOrdinal> &covered) {
    for (VariantOrdinal index = 0; index < enum_info.variants.size(); ++index) {
        if (!covered.contains(index)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool is_unreachable(const AtomicCoverage &coverage,
                                  const EnumTypeInfo &enum_info,
                                  bool covered_all,
                                  const std::unordered_set<VariantOrdinal> &covered_variants) {
    if (is_empty(coverage)) {
        return false;
    }
    if (covered_all) {
        return true;
    }
    if (coverage.all) {
        return all_variants_covered(enum_info, covered_variants);
    }
    if (coverage.variants.empty()) {
        return false;
    }
    return std::all_of(coverage.variants.begin(), coverage.variants.end(), [&](const auto &v) {
        return covered_variants.contains(v);
    });
}

void append_unique_arm(std::vector<std::size_t> &indices,
                       std::vector<SourceRange> &ranges,
                       std::size_t index,
                       SourceRange range) {
    if (std::find(indices.begin(), indices.end(), index) != indices.end()) {
        return;
    }
    indices.push_back(index);
    ranges.push_back(range);
}

[[nodiscard]] MatchUnreachableArmDiagnostic
make_unreachable(std::size_t arm_index,
                 SourceRange pattern_range,
                 const AtomicCoverage &coverage,
                 const EnumTypeInfo &enum_info,
                 std::optional<std::size_t> all_covering_arm,
                 std::optional<SourceRange> all_covering_range,
                 const std::unordered_map<VariantOrdinal, std::size_t> &variant_covering_arm,
                 const std::unordered_map<VariantOrdinal, SourceRange> &variant_covering_range) {
    MatchUnreachableArmDiagnostic diagnostic{
        .arm_index = arm_index,
        .pattern_range = pattern_range,
    };
    if (all_covering_arm.has_value() && all_covering_range.has_value()) {
        append_unique_arm(diagnostic.covering_arm_indices,
                          diagnostic.covering_arm_ranges,
                          *all_covering_arm,
                          *all_covering_range);
        return diagnostic;
    }

    const auto add_variant_cover = [&](VariantOrdinal variant) {
        const auto arm_it = variant_covering_arm.find(variant);
        const auto range_it = variant_covering_range.find(variant);
        if (arm_it == variant_covering_arm.end() || range_it == variant_covering_range.end()) {
            return;
        }
        append_unique_arm(diagnostic.covering_arm_indices,
                          diagnostic.covering_arm_ranges,
                          arm_it->second,
                          range_it->second);
    };

    if (coverage.all) {
        for (VariantOrdinal index = 0; index < enum_info.variants.size(); ++index) {
            add_variant_cover(index);
        }
    } else {
        std::vector<VariantOrdinal> variants{coverage.variants.begin(), coverage.variants.end()};
        std::sort(variants.begin(), variants.end());
        for (const auto &variant : variants) {
            add_variant_cover(variant);
        }
    }
    return diagnostic;
}

} // namespace

MatchExhaustivenessDiagnostics
analyze_match_exhaustiveness(const EnumTypeInfo &enum_info,
                             const std::vector<Owned<ast::MatchArmSyntax>> &arms,
                             SourceRange match_range) {
    MatchExhaustivenessDiagnostics diagnostics;
    std::vector<AtomicCoverage> previous_coverages;
    previous_coverages.reserve(arms.size());
    std::vector<SourceRange> previous_ranges;
    previous_ranges.reserve(arms.size());

    bool covered_all = false;
    std::optional<std::size_t> all_covering_arm;
    std::optional<SourceRange> all_covering_range;
    std::unordered_set<VariantOrdinal> covered_variants;
    std::unordered_map<VariantOrdinal, std::size_t> variant_covering_arm;
    std::unordered_map<VariantOrdinal, SourceRange> variant_covering_range;

    for (std::size_t index = 0; index < arms.size(); ++index) {
        const auto &arm = arms[index];
        if (!arm || !arm->pattern) {
            continue;
        }
        const std::size_t arm_index = index + 1;
        const auto coverage = coverage_for_pattern(*arm->pattern, enum_info);
        const auto pattern_range = arm->pattern->range;
        const bool has_guard = arm->guard != nullptr;

        for (std::size_t previous = 0; previous < previous_coverages.size(); ++previous) {
            if (!intersects(coverage, previous_coverages[previous])) {
                continue;
            }
            diagnostics.overlaps.push_back(MatchOverlapDiagnostic{
                .arm_index = arm_index,
                .pattern_range = pattern_range,
                .previous_arm_index = previous + 1,
                .previous_pattern_range = previous_ranges[previous],
            });
        }

        if (is_unreachable(coverage, enum_info, covered_all, covered_variants)) {
            diagnostics.unreachable_arms.push_back(make_unreachable(arm_index,
                                                                    pattern_range,
                                                                    coverage,
                                                                    enum_info,
                                                                    all_covering_arm,
                                                                    all_covering_range,
                                                                    variant_covering_arm,
                                                                    variant_covering_range));
        }

        previous_coverages.push_back(coverage);
        previous_ranges.push_back(pattern_range);

        if (has_guard) {
            continue;
        }

        if (coverage.all) {
            if (!covered_all) {
                covered_all = true;
                all_covering_arm = arm_index;
                all_covering_range = pattern_range;
            }
            continue;
        }
        for (const auto &variant : coverage.variants) {
            covered_variants.insert(variant);
            variant_covering_arm.try_emplace(variant, arm_index);
            variant_covering_range.try_emplace(variant, pattern_range);
        }
    }

    if (!covered_all && !all_variants_covered(enum_info, covered_variants)) {
        MatchMissingPatternsDiagnostic missing{
            .match_range = match_range,
            .enum_declaration_range = enum_info.declaration_range,
        };
        for (VariantOrdinal index = 0; index < enum_info.variants.size(); ++index) {
            const auto &variant = enum_info.variants[index];
            if (covered_variants.contains(index)) {
                continue;
            }
            missing.variants.push_back(MatchMissingVariant{
                .name = variant.name,
                .declaration_range = variant.declaration_range,
            });
        }
        diagnostics.missing_patterns = std::move(missing);
    }

    return diagnostics;
}

} // namespace ahfl
