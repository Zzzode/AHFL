#pragma once

#include "ahfl/base/support/source.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ahfl {

struct PatternDomainId {
    std::size_t value{0};

    [[nodiscard]] friend bool operator==(PatternDomainId lhs,
                                         PatternDomainId rhs) noexcept = default;
};

struct PatternConstructorId {
    std::size_t value{0};

    [[nodiscard]] friend bool operator==(PatternConstructorId lhs,
                                         PatternConstructorId rhs) noexcept = default;
};

struct PatternId {
    std::size_t value{0};

    [[nodiscard]] friend bool operator==(PatternId lhs, PatternId rhs) noexcept = default;
};

enum class PatternDomainKind {
    Finite,
    Open,
    BoundedInt,
};

enum class PatternConstructorPayloadKind {
    Unit,
    Tuple,
    Struct,
};

struct PatternConstructorDisplay {
    PatternConstructorPayloadKind payload_kind{PatternConstructorPayloadKind::Unit};
    std::vector<std::string> field_names;
};

struct PatternConstructor {
    PatternDomainId result_domain;
    std::string debug_name;
    std::vector<PatternDomainId> field_domains;
    PatternConstructorDisplay display;
    std::optional<std::int64_t> int_value{};
};

struct PatternIntBounds {
    std::int64_t minimum{0};
    std::int64_t maximum{0};
};

struct PatternDomain {
    PatternDomainKind kind{PatternDomainKind::Finite};
    std::vector<PatternConstructorId> constructors{};
    std::optional<PatternIntBounds> int_bounds{};
};

enum class PatternNodeKind {
    Never,
    Wildcard,
    Constructor,
    IntRange,
    Or,
};

struct PatternNode {
    PatternNodeKind kind{PatternNodeKind::Wildcard};
    SourceRange range;
    std::optional<PatternConstructorId> constructor{};
    std::vector<PatternId> children{};
    std::int64_t int_range_start{0};
    std::int64_t int_range_end{0};
};

struct PatternWitness {
    PatternConstructorId constructor;
    std::vector<PatternWitness> fields{};
    std::optional<std::int64_t> int_value{};
};

class PatternUsefulnessContext {
  public:
    [[nodiscard]] PatternDomainId add_domain(PatternDomainKind kind = PatternDomainKind::Finite);
    [[nodiscard]] PatternDomainId add_bounded_int_domain(std::int64_t minimum,
                                                         std::int64_t maximum);

    [[nodiscard]] PatternConstructorId add_constructor(PatternDomainId result_domain,
                                                       std::string debug_name,
                                                       std::vector<PatternDomainId> field_domains,
                                                       PatternConstructorDisplay display = {});
    [[nodiscard]] PatternConstructorId add_int_constructor(PatternDomainId result_domain,
                                                           std::int64_t value);

    [[nodiscard]] PatternId make_never(SourceRange range = {});
    [[nodiscard]] PatternId make_wildcard(SourceRange range = {});
    [[nodiscard]] PatternId make_constructor_pattern(PatternConstructorId constructor,
                                                     std::vector<PatternId> children,
                                                     SourceRange range = {});
    [[nodiscard]] PatternId
    make_int_range_pattern(std::int64_t start, std::int64_t end, SourceRange range = {});
    [[nodiscard]] PatternId make_or_pattern(std::vector<PatternId> branches,
                                            SourceRange range = {});

    [[nodiscard]] const PatternDomain &domain(PatternDomainId id) const;
    [[nodiscard]] const PatternConstructor &constructor(PatternConstructorId id) const;
    [[nodiscard]] const PatternNode &pattern(PatternId id) const;

  private:
    [[nodiscard]] bool has_domain(PatternDomainId id) const noexcept;
    [[nodiscard]] bool has_constructor(PatternConstructorId id) const noexcept;
    [[nodiscard]] bool has_pattern(PatternId id) const noexcept;

    std::vector<PatternDomain> domains_;
    std::vector<PatternConstructor> constructors_;
    std::vector<PatternNode> patterns_;
};

struct PatternUsefulnessRow {
    PatternId pattern;
    SourceRange range{};
    bool contributes_to_exhaustiveness{true};
};

struct PatternUnreachableRow {
    std::size_t row_index{0};
    SourceRange range;
    std::vector<std::size_t> covering_row_indices{};
    std::vector<SourceRange> covering_row_ranges{};
};

struct PatternOverlapRow {
    std::size_t row_index{0};
    SourceRange range;
    std::size_t previous_row_index{0};
    SourceRange previous_range;
};

struct PatternRedundantOrBranch {
    std::size_t row_index{0};
    PatternId or_pattern;
    std::size_t branch_index{0};
    SourceRange branch_range;
    std::vector<std::size_t> covering_row_indices{};
    std::vector<SourceRange> covering_row_ranges{};
    std::vector<std::size_t> covering_branch_indices{};
    std::vector<SourceRange> covering_branch_ranges{};
};

struct PatternUsefulnessOptions {
    std::size_t max_witnesses{4096};
};

struct PatternUsefulnessAnalysis {
    bool root_domain_is_finite{true};
    bool witness_limit_exceeded{false};
    std::optional<PatternWitness> missing_witness;
    std::vector<PatternWitness> missing_witnesses;
    std::vector<PatternUnreachableRow> unreachable_rows;
    std::vector<PatternOverlapRow> overlaps;
    std::vector<PatternRedundantOrBranch> redundant_or_branches;
};

[[nodiscard]] PatternUsefulnessAnalysis
analyze_pattern_usefulness(const PatternUsefulnessContext &context,
                           PatternDomainId root_domain,
                           const std::vector<PatternUsefulnessRow> &rows,
                           PatternUsefulnessOptions options = {});

[[nodiscard]] std::string render_pattern_witness(const PatternUsefulnessContext &context,
                                                 const PatternWitness &witness);

} // namespace ahfl
