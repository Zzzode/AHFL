#include "ahfl/compiler/semantics/pattern_usefulness.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <stdexcept>
#include <utility>

namespace ahfl {

namespace {

struct Replacement {
    PatternId or_pattern;
    PatternId branch;
};

struct WitnessEnumeration {
    std::vector<PatternWitness> witnesses;
    bool finite{true};
    bool limit_exceeded{false};
};

constexpr std::uint64_t kMaxMaterializedBoundedIntConstructors = 4096;
constexpr PatternConstructorId kInlineIntWitnessConstructor{
    std::numeric_limits<std::size_t>::max()};

struct IntInterval {
    std::int64_t start{0};
    std::int64_t end{0};
};

struct IntIntervalRow {
    std::size_t row_index{0};
    SourceRange range;
    std::vector<IntInterval> intervals;
    bool contributes_to_exhaustiveness{true};
};

[[nodiscard]] PatternWitness make_inline_int_witness(std::int64_t value) {
    return PatternWitness{
        .constructor = kInlineIntWitnessConstructor,
        .int_value = value,
    };
}

[[nodiscard]] bool is_materializable_int_span(std::int64_t minimum, std::int64_t maximum) noexcept {
    std::uint64_t count = 1;
    for (auto cursor = minimum; cursor != maximum;) {
        if (count >= kMaxMaterializedBoundedIntConstructors ||
            cursor == std::numeric_limits<std::int64_t>::max()) {
            return false;
        }
        ++cursor;
        ++count;
    }
    return true;
}

[[nodiscard]] std::optional<IntInterval> intersect_interval(IntInterval lhs,
                                                            IntInterval rhs) noexcept {
    const IntInterval result{
        .start = std::max(lhs.start, rhs.start),
        .end = std::min(lhs.end, rhs.end),
    };
    if (result.start > result.end) {
        return std::nullopt;
    }
    return result;
}

[[nodiscard]] std::vector<IntInterval> normalize_intervals(std::vector<IntInterval> intervals) {
    if (intervals.empty()) {
        return {};
    }

    std::sort(intervals.begin(), intervals.end(), [](IntInterval lhs, IntInterval rhs) {
        if (lhs.start != rhs.start) {
            return lhs.start < rhs.start;
        }
        return lhs.end < rhs.end;
    });

    std::vector<IntInterval> normalized;
    normalized.reserve(intervals.size());
    for (const auto interval : intervals) {
        if (normalized.empty()) {
            normalized.push_back(interval);
            continue;
        }

        auto &last = normalized.back();
        const bool adjacent =
            last.end != std::numeric_limits<std::int64_t>::max() && interval.start == last.end + 1;
        if (interval.start <= last.end || adjacent) {
            last.end = std::max(last.end, interval.end);
            continue;
        }
        normalized.push_back(interval);
    }
    return normalized;
}

[[nodiscard]] bool intervals_intersect(const std::vector<IntInterval> &lhs,
                                       const std::vector<IntInterval> &rhs) noexcept {
    std::size_t left = 0;
    std::size_t right = 0;
    while (left < lhs.size() && right < rhs.size()) {
        if (lhs[left].end < rhs[right].start) {
            ++left;
            continue;
        }
        if (rhs[right].end < lhs[left].start) {
            ++right;
            continue;
        }
        return true;
    }
    return false;
}

[[nodiscard]] std::vector<IntInterval> subtract_intervals(std::vector<IntInterval> candidate,
                                                          const std::vector<IntInterval> &covered) {
    candidate = normalize_intervals(std::move(candidate));
    std::vector<IntInterval> remaining;
    for (const auto interval : candidate) {
        std::vector<IntInterval> fragments{interval};
        for (const auto cover : covered) {
            std::vector<IntInterval> next_fragments;
            for (const auto fragment : fragments) {
                const auto overlap = intersect_interval(fragment, cover);
                if (!overlap.has_value()) {
                    next_fragments.push_back(fragment);
                    continue;
                }
                if (fragment.start < overlap->start) {
                    next_fragments.push_back(IntInterval{
                        .start = fragment.start,
                        .end = static_cast<std::int64_t>(overlap->start - 1),
                    });
                }
                if (overlap->end < fragment.end) {
                    next_fragments.push_back(IntInterval{
                        .start = static_cast<std::int64_t>(overlap->end + 1),
                        .end = fragment.end,
                    });
                }
            }
            fragments = std::move(next_fragments);
            if (fragments.empty()) {
                break;
            }
        }
        remaining.insert(remaining.end(), fragments.begin(), fragments.end());
    }
    return normalize_intervals(std::move(remaining));
}

[[nodiscard]] bool contains_domain(const std::vector<PatternDomainId> &stack,
                                   PatternDomainId id) noexcept {
    return std::find(stack.begin(), stack.end(), id) != stack.end();
}

[[nodiscard]] bool append_witness(std::vector<PatternWitness> &output,
                                  PatternWitness witness,
                                  std::size_t max_witnesses,
                                  bool &limit_exceeded) {
    if (output.size() >= max_witnesses) {
        limit_exceeded = true;
        return false;
    }
    output.push_back(std::move(witness));
    return true;
}

[[nodiscard]] bool append_product(PatternConstructorId constructor,
                                  const std::vector<std::vector<PatternWitness>> &field_values,
                                  std::size_t field_index,
                                  std::vector<PatternWitness> &current_fields,
                                  std::vector<PatternWitness> &output,
                                  std::size_t max_witnesses,
                                  bool &limit_exceeded) {
    if (field_index == field_values.size()) {
        return append_witness(output,
                              PatternWitness{.constructor = constructor, .fields = current_fields},
                              max_witnesses,
                              limit_exceeded);
    }

    for (const auto &field_witness : field_values[field_index]) {
        current_fields.push_back(field_witness);
        if (!append_product(constructor,
                            field_values,
                            field_index + 1,
                            current_fields,
                            output,
                            max_witnesses,
                            limit_exceeded)) {
            return false;
        }
        current_fields.pop_back();
    }
    return true;
}

[[nodiscard]] std::optional<std::vector<PatternWitness>>
enumerate_domain(const PatternUsefulnessContext &context,
                 PatternDomainId domain_id,
                 PatternUsefulnessOptions options,
                 std::vector<PatternDomainId> &stack,
                 bool &encountered_open_domain,
                 bool &limit_exceeded) {
    const auto &domain = context.domain(domain_id);
    if (contains_domain(stack, domain_id)) {
        return std::nullopt;
    }
    if (domain.kind == PatternDomainKind::Open) {
        encountered_open_domain = true;
    }

    stack.push_back(domain_id);
    std::vector<PatternWitness> result;
    for (const auto constructor_id : domain.constructors) {
        const auto &constructor = context.constructor(constructor_id);
        std::vector<std::vector<PatternWitness>> field_values;
        field_values.reserve(constructor.field_domains.size());
        for (const auto field_domain : constructor.field_domains) {
            auto values = enumerate_domain(
                context, field_domain, options, stack, encountered_open_domain, limit_exceeded);
            if (!values.has_value()) {
                stack.pop_back();
                return std::nullopt;
            }
            field_values.push_back(std::move(*values));
        }

        std::vector<PatternWitness> current_fields;
        current_fields.reserve(field_values.size());
        if (!append_product(constructor_id,
                            field_values,
                            0,
                            current_fields,
                            result,
                            options.max_witnesses,
                            limit_exceeded)) {
            stack.pop_back();
            return std::nullopt;
        }
    }
    stack.pop_back();
    return result;
}

[[nodiscard]] WitnessEnumeration enumerate_root_witnesses(const PatternUsefulnessContext &context,
                                                          PatternDomainId root_domain,
                                                          PatternUsefulnessOptions options) {
    WitnessEnumeration enumeration;
    bool limit_exceeded = false;
    bool encountered_open_domain = false;
    std::vector<PatternDomainId> stack;
    auto witnesses = enumerate_domain(
        context, root_domain, options, stack, encountered_open_domain, limit_exceeded);
    enumeration.limit_exceeded = limit_exceeded;
    if (!witnesses.has_value()) {
        enumeration.finite = false;
        return enumeration;
    }
    enumeration.finite = !encountered_open_domain;
    enumeration.witnesses = std::move(*witnesses);
    return enumeration;
}

[[nodiscard]] bool matches_pattern(const PatternUsefulnessContext &context,
                                   PatternId pattern_id,
                                   const PatternWitness &witness,
                                   const std::optional<Replacement> &replacement);

[[nodiscard]] bool matches_children(const PatternUsefulnessContext &context,
                                    const std::vector<PatternId> &patterns,
                                    const std::vector<PatternWitness> &witnesses,
                                    const std::optional<Replacement> &replacement) {
    if (patterns.size() != witnesses.size()) {
        return false;
    }
    for (std::size_t index = 0; index < patterns.size(); ++index) {
        if (!matches_pattern(context, patterns[index], witnesses[index], replacement)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool matches_pattern(const PatternUsefulnessContext &context,
                                   PatternId pattern_id,
                                   const PatternWitness &witness,
                                   const std::optional<Replacement> &replacement) {
    if (replacement.has_value() && replacement->or_pattern == pattern_id) {
        return matches_pattern(context, replacement->branch, witness, std::nullopt);
    }

    const auto &pattern = context.pattern(pattern_id);
    switch (pattern.kind) {
    case PatternNodeKind::Never:
        return false;
    case PatternNodeKind::Wildcard:
        return true;
    case PatternNodeKind::Constructor:
        if (!pattern.constructor.has_value()) {
            return false;
        }
        if (*pattern.constructor != witness.constructor) {
            const auto &constructor = context.constructor(*pattern.constructor);
            if (!witness.int_value.has_value() || !constructor.int_value.has_value() ||
                witness.fields.size() != pattern.children.size() ||
                *witness.int_value != *constructor.int_value) {
                return false;
            }
            return matches_children(context, pattern.children, witness.fields, replacement);
        }
        return matches_children(context, pattern.children, witness.fields, replacement);
    case PatternNodeKind::IntRange: {
        const auto value = witness.int_value.has_value()
                               ? witness.int_value
                               : context.constructor(witness.constructor).int_value;
        return value.has_value() && pattern.int_range_start <= *value &&
               *value <= pattern.int_range_end;
    }
    case PatternNodeKind::Or:
        return std::any_of(pattern.children.begin(), pattern.children.end(), [&](PatternId branch) {
            return matches_pattern(context, branch, witness, replacement);
        });
    }
    return false;
}

[[nodiscard]] std::vector<IntInterval>
pattern_intervals(const PatternUsefulnessContext &context,
                  PatternId pattern_id,
                  IntInterval domain_bounds,
                  const std::optional<Replacement> &replacement) {
    if (replacement.has_value() && replacement->or_pattern == pattern_id) {
        return pattern_intervals(context, replacement->branch, domain_bounds, std::nullopt);
    }

    const auto &pattern = context.pattern(pattern_id);
    switch (pattern.kind) {
    case PatternNodeKind::Never:
        return {};
    case PatternNodeKind::Wildcard:
        return {domain_bounds};
    case PatternNodeKind::Constructor: {
        if (!pattern.constructor.has_value()) {
            return {};
        }
        const auto value = context.constructor(*pattern.constructor).int_value;
        if (!value.has_value()) {
            return {};
        }
        const auto interval =
            intersect_interval(IntInterval{.start = *value, .end = *value}, domain_bounds);
        return interval.has_value() ? std::vector<IntInterval>{*interval}
                                    : std::vector<IntInterval>{};
    }
    case PatternNodeKind::IntRange: {
        const auto interval = intersect_interval(
            IntInterval{.start = pattern.int_range_start, .end = pattern.int_range_end},
            domain_bounds);
        return interval.has_value() ? std::vector<IntInterval>{*interval}
                                    : std::vector<IntInterval>{};
    }
    case PatternNodeKind::Or: {
        std::vector<IntInterval> intervals;
        for (const auto branch : pattern.children) {
            auto branch_intervals = pattern_intervals(context, branch, domain_bounds, replacement);
            intervals.insert(intervals.end(), branch_intervals.begin(), branch_intervals.end());
        }
        return normalize_intervals(std::move(intervals));
    }
    }
    return {};
}

using WitnessMatcher = std::function<bool(const PatternWitness &)>;

[[nodiscard]] bool any_match(const std::vector<PatternWitness> &witnesses,
                             const WitnessMatcher &candidate) {
    return std::any_of(witnesses.begin(), witnesses.end(), candidate);
}

[[nodiscard]] bool is_useful_against(const std::vector<PatternWitness> &witnesses,
                                     const WitnessMatcher &candidate,
                                     const std::vector<WitnessMatcher> &previous) {
    for (const auto &witness : witnesses) {
        if (!candidate(witness)) {
            continue;
        }
        const bool covered =
            std::any_of(previous.begin(), previous.end(), [&](const auto &previous_matcher) {
                return previous_matcher(witness);
            });
        if (!covered) {
            return true;
        }
    }
    return false;
}

void collect_covering_rows(const std::vector<PatternWitness> &witnesses,
                           const WitnessMatcher &candidate,
                           const std::vector<WitnessMatcher> &previous,
                           const std::vector<std::size_t> &previous_row_indices,
                           const std::vector<SourceRange> &previous_ranges,
                           std::vector<std::size_t> &output_indices,
                           std::vector<SourceRange> &output_ranges) {
    for (std::size_t index = 0; index < previous.size(); ++index) {
        const bool overlaps = std::any_of(witnesses.begin(), witnesses.end(), [&](const auto &w) {
            return candidate(w) && previous[index](w);
        });
        if (overlaps) {
            output_indices.push_back(previous_row_indices[index]);
            output_ranges.push_back(previous_ranges[index]);
        }
    }
}

void collect_or_patterns(const PatternUsefulnessContext &context,
                         PatternId pattern_id,
                         std::vector<PatternId> &output) {
    const auto &pattern = context.pattern(pattern_id);
    if (pattern.kind == PatternNodeKind::Or) {
        output.push_back(pattern_id);
    }
    for (const auto child : pattern.children) {
        collect_or_patterns(context, child, output);
    }
}

[[nodiscard]] SourceRange row_range(const PatternUsefulnessContext &context,
                                    const PatternUsefulnessRow &row) {
    if (!row.range.empty()) {
        return row.range;
    }
    return context.pattern(row.pattern).range;
}

[[nodiscard]] std::vector<IntInterval> merge_interval_sets(std::vector<IntInterval> lhs,
                                                           const std::vector<IntInterval> &rhs) {
    lhs.insert(lhs.end(), rhs.begin(), rhs.end());
    return normalize_intervals(std::move(lhs));
}

[[nodiscard]] std::optional<PatternUsefulnessAnalysis>
analyze_bounded_int_intervals(const PatternUsefulnessContext &context,
                              PatternDomainId root_domain,
                              const std::vector<PatternUsefulnessRow> &rows,
                              PatternUsefulnessOptions options) {
    const auto &domain = context.domain(root_domain);
    if (domain.kind != PatternDomainKind::BoundedInt || !domain.int_bounds.has_value()) {
        return std::nullopt;
    }

    const IntInterval domain_bounds{
        .start = domain.int_bounds->minimum,
        .end = domain.int_bounds->maximum,
    };

    PatternUsefulnessAnalysis analysis;
    analysis.root_domain_is_finite = true;

    std::vector<IntIntervalRow> contributing_rows;
    std::vector<IntIntervalRow> previous_rows;
    std::vector<IntInterval> contributing_intervals;

    for (std::size_t row_index = 0; row_index < rows.size(); ++row_index) {
        const auto &row = rows[row_index];
        const auto current_range = row_range(context, row);
        const auto intervals = pattern_intervals(context, row.pattern, domain_bounds, std::nullopt);

        for (const auto &previous : previous_rows) {
            if (intervals_intersect(intervals, previous.intervals)) {
                analysis.overlaps.push_back(PatternOverlapRow{
                    .row_index = row_index,
                    .range = current_range,
                    .previous_row_index = previous.row_index,
                    .previous_range = previous.range,
                });
            }
        }

        const auto uncovered = subtract_intervals(intervals, contributing_intervals);
        if (uncovered.empty() && !intervals.empty()) {
            PatternUnreachableRow unreachable{
                .row_index = row_index,
                .range = current_range,
            };
            for (const auto &previous : contributing_rows) {
                if (intervals_intersect(intervals, previous.intervals)) {
                    unreachable.covering_row_indices.push_back(previous.row_index);
                    unreachable.covering_row_ranges.push_back(previous.range);
                }
            }
            analysis.unreachable_rows.push_back(std::move(unreachable));
        }

        std::vector<PatternId> or_patterns;
        collect_or_patterns(context, row.pattern, or_patterns);
        for (const auto or_pattern : or_patterns) {
            const auto &or_node = context.pattern(or_pattern);
            std::vector<IntInterval> previous_branch_intervals;
            for (std::size_t branch_index = 0; branch_index < or_node.children.size();
                 ++branch_index) {
                const auto branch = or_node.children[branch_index];
                const auto branch_intervals =
                    pattern_intervals(context,
                                      row.pattern,
                                      domain_bounds,
                                      Replacement{.or_pattern = or_pattern, .branch = branch});
                auto previous_intervals =
                    merge_interval_sets(contributing_intervals, previous_branch_intervals);
                const auto branch_uncovered =
                    subtract_intervals(branch_intervals, previous_intervals);
                if (branch_uncovered.empty() && !branch_intervals.empty()) {
                    analysis.redundant_or_branches.push_back(PatternRedundantOrBranch{
                        .row_index = row_index,
                        .or_pattern = or_pattern,
                        .branch_index = branch_index,
                        .branch_range = context.pattern(branch).range,
                    });
                }
                previous_branch_intervals =
                    merge_interval_sets(std::move(previous_branch_intervals), branch_intervals);
            }
        }

        IntIntervalRow interval_row{
            .row_index = row_index,
            .range = current_range,
            .intervals = intervals,
            .contributes_to_exhaustiveness = row.contributes_to_exhaustiveness,
        };
        if (row.contributes_to_exhaustiveness) {
            contributing_intervals = merge_interval_sets(contributing_intervals, intervals);
            contributing_rows.push_back(interval_row);
        }
        previous_rows.push_back(std::move(interval_row));
    }

    const auto missing_intervals =
        subtract_intervals(std::vector<IntInterval>{domain_bounds}, contributing_intervals);
    for (const auto interval : missing_intervals) {
        if (analysis.missing_witnesses.size() >= options.max_witnesses) {
            analysis.witness_limit_exceeded = true;
            break;
        }
        auto witness = make_inline_int_witness(interval.start);
        if (!analysis.missing_witness.has_value()) {
            analysis.missing_witness = witness;
        }
        analysis.missing_witnesses.push_back(std::move(witness));
    }

    return analysis;
}

} // namespace

PatternDomainId PatternUsefulnessContext::add_domain(PatternDomainKind kind) {
    const PatternDomainId id{domains_.size()};
    domains_.push_back(PatternDomain{.kind = kind});
    return id;
}

PatternDomainId PatternUsefulnessContext::add_bounded_int_domain(std::int64_t minimum,
                                                                 std::int64_t maximum) {
    if (minimum > maximum) {
        throw std::invalid_argument("bounded int domain lower bound exceeds upper bound");
    }

    const PatternDomainId id{domains_.size()};
    domains_.push_back(PatternDomain{
        .kind = PatternDomainKind::BoundedInt,
        .int_bounds = PatternIntBounds{.minimum = minimum, .maximum = maximum},
    });

    if (!is_materializable_int_span(minimum, maximum)) {
        return id;
    }

    for (std::int64_t value = minimum;; ++value) {
        (void)add_int_constructor(id, value);
        if (value == maximum) {
            break;
        }
    }
    return id;
}

PatternConstructorId
PatternUsefulnessContext::add_constructor(PatternDomainId result_domain,
                                          std::string debug_name,
                                          std::vector<PatternDomainId> field_domains,
                                          PatternConstructorDisplay display) {
    if (!has_domain(result_domain)) {
        throw std::out_of_range("pattern constructor result domain id is invalid");
    }
    for (const auto field_domain : field_domains) {
        if (!has_domain(field_domain)) {
            throw std::out_of_range("pattern constructor field domain id is invalid");
        }
    }
    if (display.payload_kind == PatternConstructorPayloadKind::Unit && !field_domains.empty()) {
        display.payload_kind = PatternConstructorPayloadKind::Tuple;
    }
    if (display.payload_kind == PatternConstructorPayloadKind::Struct &&
        display.field_names.size() != field_domains.size()) {
        throw std::invalid_argument("struct constructor display field arity does not match shape");
    }
    if (display.payload_kind != PatternConstructorPayloadKind::Struct &&
        !display.field_names.empty()) {
        throw std::invalid_argument("field names require struct constructor display");
    }

    const PatternConstructorId id{constructors_.size()};
    constructors_.push_back(PatternConstructor{
        .result_domain = result_domain,
        .debug_name = std::move(debug_name),
        .field_domains = std::move(field_domains),
        .display = std::move(display),
    });
    domains_[result_domain.value].constructors.push_back(id);
    return id;
}

PatternConstructorId PatternUsefulnessContext::add_int_constructor(PatternDomainId result_domain,
                                                                   std::int64_t value) {
    const auto constructor = add_constructor(result_domain, std::to_string(value), {});
    constructors_[constructor.value].int_value = value;
    return constructor;
}

PatternId PatternUsefulnessContext::make_never(SourceRange range) {
    const PatternId id{patterns_.size()};
    patterns_.push_back(PatternNode{.kind = PatternNodeKind::Never, .range = range});
    return id;
}

PatternId PatternUsefulnessContext::make_wildcard(SourceRange range) {
    const PatternId id{patterns_.size()};
    patterns_.push_back(PatternNode{.kind = PatternNodeKind::Wildcard, .range = range});
    return id;
}

PatternId PatternUsefulnessContext::make_constructor_pattern(PatternConstructorId constructor,
                                                             std::vector<PatternId> children,
                                                             SourceRange range) {
    if (!has_constructor(constructor)) {
        throw std::out_of_range("pattern constructor id is invalid");
    }
    const auto &ctor = constructors_[constructor.value];
    if (ctor.field_domains.size() != children.size()) {
        throw std::invalid_argument("constructor pattern arity does not match constructor shape");
    }
    for (const auto child : children) {
        if (!has_pattern(child)) {
            throw std::out_of_range("constructor pattern child id is invalid");
        }
    }

    const PatternId id{patterns_.size()};
    patterns_.push_back(PatternNode{
        .kind = PatternNodeKind::Constructor,
        .range = range,
        .constructor = constructor,
        .children = std::move(children),
    });
    return id;
}

PatternId PatternUsefulnessContext::make_int_range_pattern(std::int64_t start,
                                                           std::int64_t end,
                                                           SourceRange range) {
    if (start > end) {
        throw std::invalid_argument("int range pattern lower bound exceeds upper bound");
    }
    const PatternId id{patterns_.size()};
    patterns_.push_back(PatternNode{
        .kind = PatternNodeKind::IntRange,
        .range = range,
        .int_range_start = start,
        .int_range_end = end,
    });
    return id;
}

PatternId PatternUsefulnessContext::make_or_pattern(std::vector<PatternId> branches,
                                                    SourceRange range) {
    if (branches.empty()) {
        throw std::invalid_argument("or-pattern requires at least one branch");
    }
    for (const auto branch : branches) {
        if (!has_pattern(branch)) {
            throw std::out_of_range("or-pattern branch id is invalid");
        }
    }

    const PatternId id{patterns_.size()};
    patterns_.push_back(PatternNode{
        .kind = PatternNodeKind::Or,
        .range = range,
        .children = std::move(branches),
    });
    return id;
}

const PatternDomain &PatternUsefulnessContext::domain(PatternDomainId id) const {
    if (!has_domain(id)) {
        throw std::out_of_range("pattern domain id is invalid");
    }
    return domains_[id.value];
}

const PatternConstructor &PatternUsefulnessContext::constructor(PatternConstructorId id) const {
    if (!has_constructor(id)) {
        throw std::out_of_range("pattern constructor id is invalid");
    }
    return constructors_[id.value];
}

const PatternNode &PatternUsefulnessContext::pattern(PatternId id) const {
    if (!has_pattern(id)) {
        throw std::out_of_range("pattern id is invalid");
    }
    return patterns_[id.value];
}

bool PatternUsefulnessContext::has_domain(PatternDomainId id) const noexcept {
    return id.value < domains_.size();
}

bool PatternUsefulnessContext::has_constructor(PatternConstructorId id) const noexcept {
    return id.value < constructors_.size();
}

bool PatternUsefulnessContext::has_pattern(PatternId id) const noexcept {
    return id.value < patterns_.size();
}

PatternUsefulnessAnalysis analyze_pattern_usefulness(const PatternUsefulnessContext &context,
                                                     PatternDomainId root_domain,
                                                     const std::vector<PatternUsefulnessRow> &rows,
                                                     PatternUsefulnessOptions options) {
    (void)context.domain(root_domain);

    PatternUsefulnessAnalysis analysis;
    const auto enumeration = enumerate_root_witnesses(context, root_domain, options);
    analysis.root_domain_is_finite = enumeration.finite;
    analysis.witness_limit_exceeded = enumeration.limit_exceeded;
    if (enumeration.witnesses.empty()) {
        if (auto interval_analysis =
                analyze_bounded_int_intervals(context, root_domain, rows, options);
            interval_analysis.has_value()) {
            return *interval_analysis;
        }
        return analysis;
    }

    std::vector<WitnessMatcher> contributing_matchers;
    std::vector<std::size_t> contributing_row_indices;
    std::vector<SourceRange> contributing_row_ranges;
    std::vector<WitnessMatcher> previous_matchers;
    std::vector<std::size_t> previous_row_indices;
    std::vector<SourceRange> previous_row_ranges;

    for (std::size_t row_index = 0; row_index < rows.size(); ++row_index) {
        const auto &row = rows[row_index];
        const auto row_pattern = row.pattern;
        (void)context.pattern(row_pattern);
        const auto current_range = row_range(context, row);

        const WitnessMatcher candidate = [&, row_pattern](const PatternWitness &witness) {
            return matches_pattern(context, row_pattern, witness, std::nullopt);
        };

        for (std::size_t previous = 0; previous < previous_matchers.size(); ++previous) {
            const bool overlaps =
                std::any_of(enumeration.witnesses.begin(),
                            enumeration.witnesses.end(),
                            [&](const auto &witness) {
                                return candidate(witness) && previous_matchers[previous](witness);
                            });
            if (overlaps) {
                analysis.overlaps.push_back(PatternOverlapRow{
                    .row_index = row_index,
                    .range = current_range,
                    .previous_row_index = previous_row_indices[previous],
                    .previous_range = previous_row_ranges[previous],
                });
            }
        }

        const bool useful =
            is_useful_against(enumeration.witnesses, candidate, contributing_matchers);
        if (!useful && any_match(enumeration.witnesses, candidate)) {
            PatternUnreachableRow unreachable{
                .row_index = row_index,
                .range = current_range,
            };
            collect_covering_rows(enumeration.witnesses,
                                  candidate,
                                  contributing_matchers,
                                  contributing_row_indices,
                                  contributing_row_ranges,
                                  unreachable.covering_row_indices,
                                  unreachable.covering_row_ranges);
            analysis.unreachable_rows.push_back(std::move(unreachable));
        }

        std::vector<PatternId> or_patterns;
        collect_or_patterns(context, row_pattern, or_patterns);
        for (const auto or_pattern : or_patterns) {
            const auto &or_node = context.pattern(or_pattern);
            std::vector<WitnessMatcher> previous_or_branch_matchers;
            for (std::size_t branch_index = 0; branch_index < or_node.children.size();
                 ++branch_index) {
                const auto branch = or_node.children[branch_index];
                const WitnessMatcher branch_candidate = [&, row_pattern, or_pattern, branch](
                                                            const PatternWitness &witness) {
                    return matches_pattern(context,
                                           row_pattern,
                                           witness,
                                           Replacement{.or_pattern = or_pattern, .branch = branch});
                };

                std::vector<WitnessMatcher> previous = contributing_matchers;
                previous.insert(previous.end(),
                                previous_or_branch_matchers.begin(),
                                previous_or_branch_matchers.end());
                const bool branch_useful =
                    is_useful_against(enumeration.witnesses, branch_candidate, previous);
                if (!branch_useful && any_match(enumeration.witnesses, branch_candidate)) {
                    analysis.redundant_or_branches.push_back(PatternRedundantOrBranch{
                        .row_index = row_index,
                        .or_pattern = or_pattern,
                        .branch_index = branch_index,
                        .branch_range = context.pattern(branch).range,
                    });
                }
                previous_or_branch_matchers.push_back(std::move(branch_candidate));
            }
        }

        if (row.contributes_to_exhaustiveness) {
            contributing_matchers.push_back(candidate);
            contributing_row_indices.push_back(row_index);
            contributing_row_ranges.push_back(current_range);
        }
        previous_matchers.push_back(candidate);
        previous_row_indices.push_back(row_index);
        previous_row_ranges.push_back(current_range);
    }

    for (const auto &witness : enumeration.witnesses) {
        const bool covered = std::any_of(contributing_matchers.begin(),
                                         contributing_matchers.end(),
                                         [&](const auto &matcher) { return matcher(witness); });
        if (!covered) {
            analysis.missing_witnesses.push_back(witness);
            if (!analysis.missing_witness.has_value()) {
                analysis.missing_witness = witness;
            }
        }
    }

    return analysis;
}

std::string render_pattern_witness(const PatternUsefulnessContext &context,
                                   const PatternWitness &witness) {
    if (witness.int_value.has_value()) {
        return std::to_string(*witness.int_value);
    }

    const auto &constructor = context.constructor(witness.constructor);
    std::string rendered = constructor.debug_name.empty()
                               ? "#" + std::to_string(witness.constructor.value)
                               : constructor.debug_name;
    if (witness.fields.empty()) {
        return rendered;
    }

    if (constructor.display.payload_kind == PatternConstructorPayloadKind::Struct) {
        rendered += " { ";
        for (std::size_t index = 0; index < witness.fields.size(); ++index) {
            if (index > 0) {
                rendered += ", ";
            }
            rendered += constructor.display.field_names[index];
            rendered += ": ";
            rendered += render_pattern_witness(context, witness.fields[index]);
        }
        rendered += " }";
        return rendered;
    }

    rendered += "(";
    for (std::size_t index = 0; index < witness.fields.size(); ++index) {
        if (index > 0) {
            rendered += ", ";
        }
        rendered += render_pattern_witness(context, witness.fields[index]);
    }
    rendered += ")";
    return rendered;
}

} // namespace ahfl
