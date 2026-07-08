#include "compiler/semantics/match_exhaustiveness.hpp"

#include "ahfl/compiler/semantics/pattern_usefulness.hpp"

#include <algorithm>
#include <optional>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ahfl {

namespace {

using VariantOrdinal = std::size_t;

struct EnumDomainLowering {
    TypePtr type{nullptr};
    EnumTypeInfo enum_info;
    PatternDomainId domain;
    std::vector<PatternConstructorId> variant_constructors;
    bool is_root{false};
};

struct MatchMatrixLowering {
    PatternUsefulnessContext context;
    PatternDomainId root_domain;
    std::vector<EnumDomainLowering> enum_domains;
    std::unordered_map<TypePtr, std::size_t> enum_domain_by_type;
    std::vector<std::optional<std::size_t>> enum_domain_for_pattern_domain;
    std::vector<std::optional<VariantOrdinal>> variant_for_constructor;
    const MatchEnumInfoResolver *enum_resolver{nullptr};
    bool lower_payloads{false};
};

struct PatternExpected {
    PatternDomainId domain;
    const EnumDomainLowering *enum_domain{nullptr};
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

[[nodiscard]] bool contains_type(const std::vector<TypePtr> &stack, TypePtr type) noexcept {
    return std::find(stack.begin(), stack.end(), type) != stack.end();
}

void remember_constructor_variant(MatchMatrixLowering &lowering,
                                  PatternConstructorId constructor,
                                  VariantOrdinal variant) {
    if (constructor.value >= lowering.variant_for_constructor.size()) {
        lowering.variant_for_constructor.resize(constructor.value + 1);
    }
    lowering.variant_for_constructor[constructor.value] = variant;
}

void remember_enum_domain(MatchMatrixLowering &lowering,
                          PatternDomainId domain,
                          std::size_t enum_domain_index) {
    if (domain.value >= lowering.enum_domain_for_pattern_domain.size()) {
        lowering.enum_domain_for_pattern_domain.resize(domain.value + 1);
    }
    lowering.enum_domain_for_pattern_domain[domain.value] = enum_domain_index;
}

[[nodiscard]] const EnumDomainLowering *enum_domain_for(const MatchMatrixLowering &lowering,
                                                        PatternDomainId domain) {
    if (domain.value >= lowering.enum_domain_for_pattern_domain.size()) {
        return nullptr;
    }
    const auto index = lowering.enum_domain_for_pattern_domain[domain.value];
    if (!index.has_value() || *index >= lowering.enum_domains.size()) {
        return nullptr;
    }
    return &lowering.enum_domains[*index];
}

[[nodiscard]] PatternExpected expected_for_domain(const MatchMatrixLowering &lowering,
                                                  PatternDomainId domain) {
    return PatternExpected{
        .domain = domain,
        .enum_domain = enum_domain_for(lowering, domain),
    };
}

[[nodiscard]] PatternDomainId make_opaque_domain(MatchMatrixLowering &lowering) {
    const auto domain = lowering.context.add_domain();
    (void)lowering.context.add_constructor(domain, "_", {});
    return domain;
}

[[nodiscard]] PatternDomainId
ensure_domain_for_type(MatchMatrixLowering &lowering, TypePtr type, std::vector<TypePtr> &stack);

[[nodiscard]] std::size_t ensure_enum_domain(MatchMatrixLowering &lowering,
                                             TypePtr type,
                                             EnumTypeInfo enum_info,
                                             bool is_root,
                                             std::vector<TypePtr> &stack) {
    if (type != nullptr) {
        if (const auto found = lowering.enum_domain_by_type.find(type);
            found != lowering.enum_domain_by_type.end()) {
            return found->second;
        }
    }

    const auto domain = lowering.context.add_domain();
    const auto domain_index = lowering.enum_domains.size();
    lowering.enum_domains.push_back(EnumDomainLowering{
        .type = type,
        .enum_info = std::move(enum_info),
        .domain = domain,
        .is_root = is_root,
    });
    remember_enum_domain(lowering, domain, domain_index);
    if (type != nullptr) {
        lowering.enum_domain_by_type.emplace(type, domain_index);
        stack.push_back(type);
    }

    const auto variant_count = lowering.enum_domains[domain_index].enum_info.variants.size();
    lowering.enum_domains[domain_index].variant_constructors.reserve(variant_count);
    for (VariantOrdinal index = 0; index < variant_count; ++index) {
        const auto variant = lowering.enum_domains[domain_index].enum_info.variants[index];
        std::vector<PatternDomainId> field_domains;
        if (lowering.lower_payloads) {
            if (variant.payload_kind == EnumVariantPayloadKind::Tuple) {
                field_domains.reserve(variant.payload.size());
                for (const auto payload_type : variant.payload) {
                    field_domains.push_back(ensure_domain_for_type(lowering, payload_type, stack));
                }
            } else if (variant.payload_kind == EnumVariantPayloadKind::Struct) {
                field_domains.reserve(variant.fields.size());
                for (const auto &field : variant.fields) {
                    field_domains.push_back(ensure_domain_for_type(lowering, field.type, stack));
                }
            }
        }

        const auto constructor =
            lowering.context.add_constructor(domain, variant.name, std::move(field_domains));
        lowering.enum_domains[domain_index].variant_constructors.push_back(constructor);
        if (is_root) {
            remember_constructor_variant(lowering, constructor, index);
        }
    }

    if (type != nullptr) {
        stack.pop_back();
    }
    return domain_index;
}

[[nodiscard]] PatternDomainId
ensure_domain_for_type(MatchMatrixLowering &lowering, TypePtr type, std::vector<TypePtr> &stack) {
    if (type == nullptr || !lowering.lower_payloads || lowering.enum_resolver == nullptr ||
        contains_type(stack, type) || type->get_if<types::EnumT>() == nullptr) {
        return make_opaque_domain(lowering);
    }

    auto enum_info = (*lowering.enum_resolver)(*type);
    if (!enum_info.has_value()) {
        return make_opaque_domain(lowering);
    }

    const auto enum_domain_index =
        ensure_enum_domain(lowering, type, std::move(*enum_info), false, stack);
    return lowering.enum_domains[enum_domain_index].domain;
}

[[nodiscard]] MatchMatrixLowering make_lowering(const EnumTypeInfo &enum_info) {
    MatchMatrixLowering lowering{.root_domain = PatternDomainId{0}};
    std::vector<TypePtr> stack;
    const auto root_index = ensure_enum_domain(lowering, nullptr, enum_info, true, stack);
    lowering.root_domain = lowering.enum_domains[root_index].domain;
    return lowering;
}

[[nodiscard]] MatchMatrixLowering make_lowering(const Type &scrutinee_type,
                                                const EnumTypeInfo &enum_info,
                                                const MatchEnumInfoResolver &enum_resolver) {
    MatchMatrixLowering lowering{
        .root_domain = PatternDomainId{0},
        .enum_resolver = &enum_resolver,
        .lower_payloads = true,
    };
    std::vector<TypePtr> stack;
    const auto root_index = ensure_enum_domain(lowering, &scrutinee_type, enum_info, true, stack);
    lowering.root_domain = lowering.enum_domains[root_index].domain;
    return lowering;
}

[[nodiscard]] PatternId constructor_pattern(MatchMatrixLowering &lowering,
                                            PatternConstructorId constructor,
                                            SourceRange range) {
    const auto &shape = lowering.context.constructor(constructor);
    std::vector<PatternId> children;
    children.reserve(shape.field_domains.size());
    for (const auto field_domain : shape.field_domains) {
        children.push_back(lowering.context.make_wildcard());
        (void)field_domain;
    }
    return lowering.context.make_constructor_pattern(constructor, std::move(children), range);
}

[[nodiscard]] PatternId constructor_pattern(MatchMatrixLowering &lowering,
                                            const EnumDomainLowering &enum_domain,
                                            VariantOrdinal variant,
                                            SourceRange range) {
    return constructor_pattern(lowering, enum_domain.variant_constructors[variant], range);
}

[[nodiscard]] PatternId lower_pattern(const ast::PatternSyntax &pattern,
                                      PatternExpected expected,
                                      MatchMatrixLowering &lowering);

[[nodiscard]] PatternId lower_tuple_pattern(const ast::TuplePattern &tuple,
                                            SourceRange range,
                                            PatternExpected expected,
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
        const auto lowered = lower_pattern(*element, expected, lowering);
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

[[nodiscard]] std::vector<PatternId>
wildcard_children_for_constructor(MatchMatrixLowering &lowering, PatternConstructorId constructor) {
    const auto &shape = lowering.context.constructor(constructor);
    std::vector<PatternId> children;
    children.reserve(shape.field_domains.size());
    for (const auto field_domain : shape.field_domains) {
        children.push_back(lowering.context.make_wildcard());
        (void)field_domain;
    }
    return children;
}

[[nodiscard]] PatternId lower_variant_pattern(const ast::VariantPattern &variant,
                                              SourceRange range,
                                              PatternExpected expected,
                                              MatchMatrixLowering &lowering) {
    if (variant.path == nullptr || expected.enum_domain == nullptr) {
        return lowering.context.make_never(range);
    }

    const auto variant_name = last_segment(*variant.path);
    const auto ordinal = variant_ordinal(expected.enum_domain->enum_info, variant_name);
    if (variant_name.empty() || !ordinal.has_value()) {
        return lowering.context.make_never(range);
    }

    const auto constructor = expected.enum_domain->variant_constructors[*ordinal];
    auto children = wildcard_children_for_constructor(lowering, constructor);
    const auto &variant_info = expected.enum_domain->enum_info.variants[*ordinal];

    if (variant_info.payload_kind == EnumVariantPayloadKind::Tuple) {
        const auto limit = std::min(variant.subpatterns.size(), children.size());
        const auto &shape = lowering.context.constructor(constructor);
        for (std::size_t index = 0; index < limit; ++index) {
            if (!variant.subpatterns[index]) {
                continue;
            }
            children[index] =
                lower_pattern(*variant.subpatterns[index],
                              expected_for_domain(lowering, shape.field_domains[index]),
                              lowering);
        }
    } else if (variant_info.payload_kind == EnumVariantPayloadKind::Struct) {
        const auto &shape = lowering.context.constructor(constructor);
        for (const auto &field_pattern : variant.fields) {
            if (!field_pattern || field_pattern->is_rest) {
                continue;
            }
            for (std::size_t field_index = 0; field_index < variant_info.fields.size();
                 ++field_index) {
                if (variant_info.fields[field_index].name != field_pattern->name ||
                    field_pattern->pattern == nullptr ||
                    field_index >= shape.field_domains.size()) {
                    continue;
                }
                children[field_index] =
                    lower_pattern(*field_pattern->pattern,
                                  expected_for_domain(lowering, shape.field_domains[field_index]),
                                  lowering);
                break;
            }
        }
    }

    return lowering.context.make_constructor_pattern(constructor, std::move(children), range);
}

[[nodiscard]] PatternId lower_pattern(const ast::PatternSyntax &pattern,
                                      PatternExpected expected,
                                      MatchMatrixLowering &lowering) {
    return std::visit(
        [&](const auto &node) -> PatternId {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, ast::WildcardPattern>) {
                return lowering.context.make_wildcard(pattern.range);
            } else if constexpr (std::is_same_v<T, ast::BindingPattern>) {
                if (!node.nested) {
                    if (expected.enum_domain != nullptr) {
                        const auto ordinal =
                            variant_ordinal(expected.enum_domain->enum_info, node.name);
                        if (ordinal.has_value()) {
                            return constructor_pattern(
                                lowering, *expected.enum_domain, *ordinal, pattern.range);
                        }
                    }
                    return lowering.context.make_wildcard(pattern.range);
                }
                return lower_pattern(*node.nested, expected, lowering);
            } else if constexpr (std::is_same_v<T, ast::VariantPattern>) {
                return lower_variant_pattern(node, pattern.range, expected, lowering);
            } else if constexpr (std::is_same_v<T, ast::OrPattern>) {
                std::vector<PatternId> branches;
                branches.reserve(node.branches.size());
                for (const auto &branch : node.branches) {
                    if (!branch) {
                        continue;
                    }
                    branches.push_back(lower_pattern(*branch, expected, lowering));
                }
                if (branches.empty()) {
                    return lowering.context.make_never(pattern.range);
                }
                if (branches.size() == 1) {
                    return branches.front();
                }
                return lowering.context.make_or_pattern(std::move(branches), pattern.range);
            } else if constexpr (std::is_same_v<T, ast::TuplePattern>) {
                return lower_tuple_pattern(node, pattern.range, expected, lowering);
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
        missing.witnesses.push_back(render_pattern_witness(lowering.context, witness));
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

    if (!missing.variants.empty() || !missing.witnesses.empty()) {
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

MatchExhaustivenessDiagnostics
analyze_with_lowering(MatchMatrixLowering lowering,
                      const EnumTypeInfo &enum_info,
                      const std::vector<Owned<ast::MatchArmSyntax>> &arms,
                      SourceRange match_range) {
    MatchExhaustivenessDiagnostics diagnostics;
    const auto root_expected = expected_for_domain(lowering, lowering.root_domain);

    std::vector<PatternUsefulnessRow> rows;
    std::vector<std::size_t> arm_indices;
    rows.reserve(arms.size());
    arm_indices.reserve(arms.size());
    for (std::size_t index = 0; index < arms.size(); ++index) {
        const auto &arm = arms[index];
        if (!arm || !arm->pattern) {
            continue;
        }
        const auto pattern_id = lower_pattern(*arm->pattern, root_expected, lowering);
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

} // namespace

MatchExhaustivenessDiagnostics
analyze_match_exhaustiveness(const EnumTypeInfo &enum_info,
                             const std::vector<Owned<ast::MatchArmSyntax>> &arms,
                             SourceRange match_range) {
    return analyze_with_lowering(make_lowering(enum_info), enum_info, arms, match_range);
}

MatchExhaustivenessDiagnostics
analyze_match_exhaustiveness(const Type &scrutinee_type,
                             const EnumTypeInfo &enum_info,
                             const std::vector<Owned<ast::MatchArmSyntax>> &arms,
                             SourceRange match_range,
                             const MatchEnumInfoResolver &enum_resolver) {
    return analyze_with_lowering(
        make_lowering(scrutinee_type, enum_info, enum_resolver), enum_info, arms, match_range);
}

} // namespace ahfl
