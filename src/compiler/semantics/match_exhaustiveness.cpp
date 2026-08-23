#include "compiler/semantics/match_exhaustiveness.hpp"

#include "ahfl/compiler/semantics/pattern_usefulness.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
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
    std::vector<PatternConstructorId> variant_constructors{};
    bool is_root{false};
};

struct BoolDomainLowering {
    PatternDomainId domain;
    PatternConstructorId false_constructor;
    PatternConstructorId true_constructor;
};

struct StringSingletonDomainLowering {
    PatternDomainId domain;
    PatternConstructorId empty_constructor;
};

enum class LiteralPatternKind {
    Bool,
    Int,
    Float,
    String,
    None,
    Unknown,
};

enum class OpenLiteralDomainKind {
    Int,
    Float,
    String,
};

struct OpenLiteralDomainLowering {
    TypePtr type{nullptr};
    PatternDomainId domain;
    OpenLiteralDomainKind kind{OpenLiteralDomainKind::Int};
    PatternConstructorId default_constructor;
    std::unordered_map<std::string, PatternConstructorId> literal_constructors{};
};

struct MatchMatrixLowering {
    PatternUsefulnessContext context{};
    PatternDomainId root_domain;
    std::vector<EnumDomainLowering> enum_domains{};
    std::unordered_map<TypePtr, std::size_t> enum_domain_by_type{};
    std::vector<std::optional<std::size_t>> enum_domain_for_pattern_domain{};
    std::vector<OpenLiteralDomainLowering> open_literal_domains{};
    std::unordered_map<TypePtr, std::size_t> open_literal_domain_by_type{};
    std::vector<std::optional<std::size_t>> open_literal_domain_for_pattern_domain{};
    std::unordered_map<TypePtr, PatternDomainId> bounded_int_domain_by_type{};
    std::vector<std::optional<VariantOrdinal>> variant_for_constructor{};
    std::optional<BoolDomainLowering> bool_domain{};
    std::optional<StringSingletonDomainLowering> empty_string_domain{};
    const MatchEnumInfoResolver *enum_resolver{nullptr};
    bool lower_payloads{false};
};

struct PatternExpected {
    PatternDomainId domain;
    const EnumDomainLowering *enum_domain{nullptr};
    const BoolDomainLowering *bool_domain{nullptr};
    const StringSingletonDomainLowering *empty_string_domain{nullptr};
    std::optional<std::size_t> open_literal_domain_index;
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

void remember_open_literal_domain(MatchMatrixLowering &lowering,
                                  PatternDomainId domain,
                                  std::size_t open_domain_index) {
    if (domain.value >= lowering.open_literal_domain_for_pattern_domain.size()) {
        lowering.open_literal_domain_for_pattern_domain.resize(domain.value + 1);
    }
    lowering.open_literal_domain_for_pattern_domain[domain.value] = open_domain_index;
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

[[nodiscard]] std::optional<std::size_t>
open_literal_domain_index_for(const MatchMatrixLowering &lowering, PatternDomainId domain) {
    if (domain.value >= lowering.open_literal_domain_for_pattern_domain.size()) {
        return std::nullopt;
    }
    const auto index = lowering.open_literal_domain_for_pattern_domain[domain.value];
    if (!index.has_value() || *index >= lowering.open_literal_domains.size()) {
        return std::nullopt;
    }
    return index;
}

[[nodiscard]] PatternExpected expected_for_domain(const MatchMatrixLowering &lowering,
                                                  PatternDomainId domain) {
    const auto *bool_domain =
        lowering.bool_domain.has_value() && lowering.bool_domain->domain == domain
            ? &*lowering.bool_domain
            : nullptr;
    const auto *empty_string_domain =
        lowering.empty_string_domain.has_value() && lowering.empty_string_domain->domain == domain
            ? &*lowering.empty_string_domain
            : nullptr;
    return PatternExpected{
        .domain = domain,
        .enum_domain = enum_domain_for(lowering, domain),
        .bool_domain = bool_domain,
        .empty_string_domain = empty_string_domain,
        .open_literal_domain_index = open_literal_domain_index_for(lowering, domain),
    };
}

[[nodiscard]] PatternDomainId make_opaque_domain(MatchMatrixLowering &lowering) {
    const auto domain = lowering.context.add_domain();
    (void)lowering.context.add_constructor(domain, "_", {});
    return domain;
}

[[nodiscard]] bool is_bounded_int_domain(const MatchMatrixLowering &lowering,
                                         PatternDomainId domain) {
    return lowering.context.domain(domain).kind == PatternDomainKind::BoundedInt;
}

[[nodiscard]] PatternDomainId ensure_bounded_int_domain(MatchMatrixLowering &lowering,
                                                        TypePtr type,
                                                        const types::BoundedIntT &bounds) {
    if (type != nullptr) {
        if (const auto found = lowering.bounded_int_domain_by_type.find(type);
            found != lowering.bounded_int_domain_by_type.end()) {
            return found->second;
        }
    }

    const auto domain = lowering.context.add_bounded_int_domain(bounds.minimum, bounds.maximum);
    if (type != nullptr) {
        lowering.bounded_int_domain_by_type.emplace(type, domain);
    }
    return domain;
}

[[nodiscard]] std::optional<OpenLiteralDomainKind> open_literal_domain_kind_for(TypePtr type) {
    if (type == nullptr) {
        return std::nullopt;
    }
    if (type->get_if<types::IntT>() != nullptr) {
        return OpenLiteralDomainKind::Int;
    }
    if (type->get_if<types::FloatT>() != nullptr) {
        return OpenLiteralDomainKind::Float;
    }
    if (type->get_if<types::StringT>() != nullptr) {
        return OpenLiteralDomainKind::String;
    }
    return std::nullopt;
}

[[nodiscard]] PatternDomainId ensure_open_literal_domain(MatchMatrixLowering &lowering,
                                                         TypePtr type,
                                                         OpenLiteralDomainKind kind) {
    if (type != nullptr) {
        if (const auto found = lowering.open_literal_domain_by_type.find(type);
            found != lowering.open_literal_domain_by_type.end()) {
            return lowering.open_literal_domains[found->second].domain;
        }
    }

    const auto domain = lowering.context.add_domain(PatternDomainKind::Open);
    const auto default_constructor = lowering.context.add_constructor(domain, "_", {});
    const auto domain_index = lowering.open_literal_domains.size();
    lowering.open_literal_domains.push_back(OpenLiteralDomainLowering{
        .type = type,
        .domain = domain,
        .kind = kind,
        .default_constructor = default_constructor,
    });
    remember_open_literal_domain(lowering, domain, domain_index);
    if (type != nullptr) {
        lowering.open_literal_domain_by_type.emplace(type, domain_index);
    }
    return domain;
}

[[nodiscard]] PatternDomainId ensure_bool_domain(MatchMatrixLowering &lowering) {
    if (lowering.bool_domain.has_value()) {
        return lowering.bool_domain->domain;
    }

    const auto domain = lowering.context.add_domain();
    const auto false_constructor = lowering.context.add_constructor(domain, "false", {});
    const auto true_constructor = lowering.context.add_constructor(domain, "true", {});
    lowering.bool_domain = BoolDomainLowering{
        .domain = domain,
        .false_constructor = false_constructor,
        .true_constructor = true_constructor,
    };
    return domain;
}

[[nodiscard]] PatternDomainId ensure_empty_string_domain(MatchMatrixLowering &lowering) {
    if (lowering.empty_string_domain.has_value()) {
        return lowering.empty_string_domain->domain;
    }

    const auto domain = lowering.context.add_domain();
    const auto empty_constructor = lowering.context.add_constructor(domain, "\"\"", {});
    lowering.empty_string_domain = StringSingletonDomainLowering{
        .domain = domain,
        .empty_constructor = empty_constructor,
    };
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
        PatternConstructorDisplay display;
        if (lowering.lower_payloads) {
            if (variant.payload_kind == EnumVariantPayloadKind::Tuple) {
                display.payload_kind = PatternConstructorPayloadKind::Tuple;
                field_domains.reserve(variant.payload.size());
                for (const auto payload_type : variant.payload) {
                    field_domains.push_back(ensure_domain_for_type(lowering, payload_type, stack));
                }
            } else if (variant.payload_kind == EnumVariantPayloadKind::Struct) {
                display.payload_kind = PatternConstructorPayloadKind::Struct;
                field_domains.reserve(variant.fields.size());
                display.field_names.reserve(variant.fields.size());
                for (const auto &field : variant.fields) {
                    field_domains.push_back(ensure_domain_for_type(lowering, field.type, stack));
                    display.field_names.push_back(field.name);
                }
            }
        }

        const auto constructor = lowering.context.add_constructor(
            domain, variant.name, std::move(field_domains), std::move(display));
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
    if (type == nullptr || !lowering.lower_payloads) {
        return make_opaque_domain(lowering);
    }
    if (type->get_if<types::BoolT>() != nullptr) {
        return ensure_bool_domain(lowering);
    }
    if (const auto *bounded_int = type->get_if<types::BoundedIntT>()) {
        return ensure_bounded_int_domain(lowering, type, *bounded_int);
    }
    if (const auto *bounded_string = type->get_if<types::BoundedStringT>()) {
        if (bounded_string->minimum == 0 && bounded_string->maximum == 0) {
            return ensure_empty_string_domain(lowering);
        }
        return ensure_open_literal_domain(lowering, type, OpenLiteralDomainKind::String);
    }
    if (auto open_kind = open_literal_domain_kind_for(type); open_kind.has_value()) {
        return ensure_open_literal_domain(lowering, type, *open_kind);
    }
    if (lowering.enum_resolver == nullptr || contains_type(stack, type) ||
        type->get_if<types::EnumT>() == nullptr) {
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

[[nodiscard]] LiteralPatternKind literal_pattern_kind(std::string_view spelling) {
    if (spelling == "true" || spelling == "false") {
        return LiteralPatternKind::Bool;
    }
    if (spelling == "none") {
        return LiteralPatternKind::None;
    }
    if (spelling.starts_with('"')) {
        return LiteralPatternKind::String;
    }
    if (spelling.find('.') != std::string_view::npos) {
        return LiteralPatternKind::Float;
    }
    if (!spelling.empty() && std::all_of(spelling.begin(), spelling.end(), [](unsigned char ch) {
            return std::isdigit(ch) != 0;
        })) {
        return LiteralPatternKind::Int;
    }
    return LiteralPatternKind::Unknown;
}

[[nodiscard]] bool literal_matches_open_domain(LiteralPatternKind literal,
                                               OpenLiteralDomainKind domain) noexcept {
    switch (domain) {
    case OpenLiteralDomainKind::Int:
        return literal == LiteralPatternKind::Int;
    case OpenLiteralDomainKind::Float:
        return literal == LiteralPatternKind::Float;
    case OpenLiteralDomainKind::String:
        return literal == LiteralPatternKind::String;
    }
    return false;
}

[[nodiscard]] std::optional<std::int64_t> parse_int_literal_spelling(std::string_view spelling) {
    if (spelling.empty()) {
        return std::nullopt;
    }
    std::int64_t value = 0;
    const auto *begin = spelling.data();
    const auto *end = spelling.data() + spelling.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::optional<std::string> decode_string_literal_spelling(std::string_view spelling) {
    if (spelling.size() < 2 || spelling.front() != '"' || spelling.back() != '"') {
        return std::nullopt;
    }

    std::string decoded;
    decoded.reserve(spelling.size() - 2);
    for (std::size_t index = 1; index + 1 < spelling.size(); ++index) {
        char value = spelling[index];
        if (value == '\\') {
            ++index;
            if (index + 1 >= spelling.size()) {
                return std::nullopt;
            }
            switch (spelling[index]) {
            case '"':
                value = '"';
                break;
            case '\\':
                value = '\\';
                break;
            case 'n':
                value = '\n';
                break;
            case 'r':
                value = '\r';
                break;
            case 't':
                value = '\t';
                break;
            default:
                return std::nullopt;
            }
        }
        decoded.push_back(value);
    }
    return decoded;
}

[[nodiscard]] std::optional<std::string> canonical_float_literal_key(std::string_view spelling) {
    try {
        std::size_t parsed_size = 0;
        const double value = std::stod(std::string{spelling}, &parsed_size);
        if (parsed_size != spelling.size()) {
            return std::nullopt;
        }
        return std::string{"f64:"} + std::to_string(std::bit_cast<std::uint64_t>(value));
    } catch (const std::invalid_argument &) {
        return std::nullopt;
    } catch (const std::out_of_range &) {
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<std::string> canonical_open_literal_key(OpenLiteralDomainKind kind,
                                                                    std::string_view spelling) {
    if (kind == OpenLiteralDomainKind::String) {
        return decode_string_literal_spelling(spelling);
    }
    if (kind == OpenLiteralDomainKind::Float) {
        return canonical_float_literal_key(spelling);
    }
    return std::string{spelling};
}

[[nodiscard]] PatternConstructorId
ensure_open_literal_constructor(MatchMatrixLowering &lowering,
                                std::size_t domain_index,
                                std::string key,
                                std::string_view display_spelling) {
    auto &domain = lowering.open_literal_domains[domain_index];
    if (const auto found = domain.literal_constructors.find(key);
        found != domain.literal_constructors.end()) {
        return found->second;
    }
    const auto parsed_int = domain.kind == OpenLiteralDomainKind::Int
                                ? parse_int_literal_spelling(display_spelling)
                                : std::nullopt;
    const auto constructor =
        parsed_int.has_value()
            ? lowering.context.add_int_constructor(domain.domain, *parsed_int)
            : lowering.context.add_constructor(domain.domain, std::string{display_spelling}, {});
    domain.literal_constructors.emplace(key, constructor);
    return constructor;
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

[[nodiscard]] PatternId lower_literal_spelling(std::string_view spelling,
                                               SourceRange range,
                                               PatternExpected expected,
                                               MatchMatrixLowering &lowering);

[[nodiscard]] PatternId lower_literal_pattern(const ast::LiteralPattern &literal,
                                              SourceRange range,
                                              PatternExpected expected,
                                              MatchMatrixLowering &lowering) {
    return lower_literal_spelling(literal.spelling, range, expected, lowering);
}

[[nodiscard]] PatternId lower_int_range_pattern(const ast::IntRangePattern &range_pattern,
                                                SourceRange range,
                                                PatternExpected expected,
                                                MatchMatrixLowering &lowering) {
    if (range_pattern.start > range_pattern.end) {
        return lowering.context.make_never(range);
    }
    if (is_bounded_int_domain(lowering, expected.domain)) {
        return lowering.context.make_int_range_pattern(
            range_pattern.start, range_pattern.end, range);
    }
    if (!expected.open_literal_domain_index.has_value()) {
        return lowering.context.make_never(range);
    }
    const auto &domain = lowering.open_literal_domains[*expected.open_literal_domain_index];
    if (domain.kind != OpenLiteralDomainKind::Int) {
        return lowering.context.make_never(range);
    }
    return lowering.context.make_int_range_pattern(range_pattern.start, range_pattern.end, range);
}

[[nodiscard]] PatternId lower_int_range_pattern(const TypedPattern &pattern,
                                                PatternExpected expected,
                                                MatchMatrixLowering &lowering) {
    if (pattern.int_range_start > pattern.int_range_end) {
        return lowering.context.make_never(pattern.range);
    }
    if (is_bounded_int_domain(lowering, expected.domain)) {
        return lowering.context.make_int_range_pattern(
            pattern.int_range_start, pattern.int_range_end, pattern.range);
    }
    if (!expected.open_literal_domain_index.has_value()) {
        return lowering.context.make_never(pattern.range);
    }
    const auto &domain = lowering.open_literal_domains[*expected.open_literal_domain_index];
    if (domain.kind != OpenLiteralDomainKind::Int) {
        return lowering.context.make_never(pattern.range);
    }
    return lowering.context.make_int_range_pattern(
        pattern.int_range_start, pattern.int_range_end, pattern.range);
}

[[nodiscard]] PatternId lower_literal_spelling(std::string_view spelling,
                                               SourceRange range,
                                               PatternExpected expected,
                                               MatchMatrixLowering &lowering) {
    if (expected.bool_domain != nullptr) {
        if (spelling == "false") {
            return lowering.context.make_constructor_pattern(
                expected.bool_domain->false_constructor, {}, range);
        }
        if (spelling == "true") {
            return lowering.context.make_constructor_pattern(
                expected.bool_domain->true_constructor, {}, range);
        }
    }

    if (expected.enum_domain != nullptr && spelling == "none") {
        const auto ordinal = variant_ordinal(expected.enum_domain->enum_info, "None");
        if (ordinal.has_value()) {
            return constructor_pattern(lowering, *expected.enum_domain, *ordinal, range);
        }
    }

    if (expected.empty_string_domain != nullptr) {
        const auto decoded = decode_string_literal_spelling(spelling);
        if (decoded.has_value() && decoded->empty()) {
            return lowering.context.make_constructor_pattern(
                expected.empty_string_domain->empty_constructor, {}, range);
        }
        return lowering.context.make_never(range);
    }

    if (expected.open_literal_domain_index.has_value()) {
        const auto &domain = lowering.open_literal_domains[*expected.open_literal_domain_index];
        const auto literal_kind = literal_pattern_kind(spelling);
        if (literal_matches_open_domain(literal_kind, domain.kind)) {
            auto key = canonical_open_literal_key(domain.kind, spelling);
            if (!key.has_value()) {
                return lowering.context.make_never(range);
            }
            return lowering.context.make_constructor_pattern(
                ensure_open_literal_constructor(
                    lowering, *expected.open_literal_domain_index, std::move(*key), spelling),
                {},
                range);
        }
    }

    if (is_bounded_int_domain(lowering, expected.domain)) {
        if (const auto value = parse_int_literal_spelling(spelling); value.has_value()) {
            return lowering.context.make_int_range_pattern(*value, *value, range);
        }
    }

    return lowering.context.make_never(range);
}

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
            } else if constexpr (std::is_same_v<T, ast::LiteralPattern>) {
                return lower_literal_pattern(node, pattern.range, expected, lowering);
            } else if constexpr (std::is_same_v<T, ast::IntRangePattern>) {
                return lower_int_range_pattern(node, pattern.range, expected, lowering);
            } else {
                return lowering.context.make_never(pattern.range);
            }
        },
        pattern.node);
}

[[nodiscard]] std::optional<std::size_t> parse_child_ordinal(std::string_view text) noexcept {
    if (text.empty()) {
        return std::nullopt;
    }
    std::size_t value = 0;
    const auto *begin = text.data();
    const auto *end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] const TypedPattern *resolve_typed_pattern(const MatchTypedPatternResolver &resolver,
                                                        std::uint32_t index) {
    if (index == UINT32_MAX) {
        return nullptr;
    }
    return resolver(index);
}

[[nodiscard]] PatternId lower_typed_pattern(const TypedPattern &pattern,
                                            PatternExpected expected,
                                            MatchMatrixLowering &lowering,
                                            const MatchTypedPatternResolver &resolver);

[[nodiscard]] PatternId lower_typed_child(const TypedPatternChild &child,
                                          PatternExpected expected,
                                          MatchMatrixLowering &lowering,
                                          const MatchTypedPatternResolver &resolver) {
    const auto *typed_child = resolve_typed_pattern(resolver, child.pattern_index);
    if (typed_child == nullptr) {
        return lowering.context.make_wildcard();
    }
    return lower_typed_pattern(*typed_child, expected, lowering, resolver);
}

[[nodiscard]] PatternId lower_typed_tuple_pattern(const TypedPattern &pattern,
                                                  PatternExpected expected,
                                                  MatchMatrixLowering &lowering,
                                                  const MatchTypedPatternResolver &resolver) {
    if (pattern.children.empty()) {
        return lowering.context.make_never(pattern.range);
    }

    bool all_elements_irrefutable = true;
    std::vector<PatternId> branches;
    branches.reserve(pattern.children.size());
    for (const auto &child : pattern.children) {
        const auto lowered = lower_typed_child(child, expected, lowering, resolver);
        if (lowering.context.pattern(lowered).kind == PatternNodeKind::Wildcard) {
            continue;
        }
        all_elements_irrefutable = false;
        branches.push_back(lowered);
    }

    if (all_elements_irrefutable) {
        return lowering.context.make_wildcard(pattern.range);
    }
    if (branches.empty()) {
        return lowering.context.make_never(pattern.range);
    }
    if (branches.size() == 1) {
        return branches.front();
    }
    return lowering.context.make_or_pattern(std::move(branches), pattern.range);
}

[[nodiscard]] PatternId lower_typed_variant_pattern(const TypedPattern &pattern,
                                                    PatternExpected expected,
                                                    MatchMatrixLowering &lowering,
                                                    const MatchTypedPatternResolver &resolver) {
    if (expected.enum_domain == nullptr || pattern.variant_name.empty()) {
        return lowering.context.make_never(pattern.range);
    }

    const auto ordinal = variant_ordinal(expected.enum_domain->enum_info, pattern.variant_name);
    if (!ordinal.has_value()) {
        return lowering.context.make_never(pattern.range);
    }

    const auto constructor = expected.enum_domain->variant_constructors[*ordinal];
    auto children = wildcard_children_for_constructor(lowering, constructor);
    const auto &variant_info = expected.enum_domain->enum_info.variants[*ordinal];
    const auto &shape = lowering.context.constructor(constructor);

    if (variant_info.payload_kind == EnumVariantPayloadKind::Tuple) {
        for (const auto &child : pattern.children) {
            const auto child_index = parse_child_ordinal(child.name);
            if (!child_index.has_value() || *child_index >= children.size() ||
                *child_index >= shape.field_domains.size()) {
                continue;
            }
            children[*child_index] =
                lower_typed_child(child,
                                  expected_for_domain(lowering, shape.field_domains[*child_index]),
                                  lowering,
                                  resolver);
        }
    } else if (variant_info.payload_kind == EnumVariantPayloadKind::Struct) {
        for (const auto &child : pattern.children) {
            for (std::size_t field_index = 0; field_index < variant_info.fields.size();
                 ++field_index) {
                if (variant_info.fields[field_index].name != child.name ||
                    field_index >= children.size() || field_index >= shape.field_domains.size()) {
                    continue;
                }
                children[field_index] = lower_typed_child(
                    child,
                    expected_for_domain(lowering, shape.field_domains[field_index]),
                    lowering,
                    resolver);
                break;
            }
        }
    }

    return lowering.context.make_constructor_pattern(
        constructor, std::move(children), pattern.range);
}

[[nodiscard]] PatternId lower_typed_or_pattern(const TypedPattern &pattern,
                                               PatternExpected expected,
                                               MatchMatrixLowering &lowering,
                                               const MatchTypedPatternResolver &resolver) {
    std::vector<PatternId> branches;
    branches.reserve(pattern.children.size());
    for (const auto &child : pattern.children) {
        const auto *typed_child = resolve_typed_pattern(resolver, child.pattern_index);
        if (typed_child == nullptr) {
            continue;
        }
        branches.push_back(lower_typed_pattern(*typed_child, expected, lowering, resolver));
    }
    if (branches.empty()) {
        return lowering.context.make_never(pattern.range);
    }
    if (branches.size() == 1) {
        return branches.front();
    }
    return lowering.context.make_or_pattern(std::move(branches), pattern.range);
}

[[nodiscard]] PatternId lower_typed_binding_pattern(const TypedPattern &pattern,
                                                    PatternExpected expected,
                                                    MatchMatrixLowering &lowering,
                                                    const MatchTypedPatternResolver &resolver) {
    if (pattern.children.empty()) {
        return lowering.context.make_wildcard(pattern.range);
    }
    const auto nested =
        std::find_if(pattern.children.begin(),
                     pattern.children.end(),
                     [](const TypedPatternChild &child) { return child.name == "nested"; });
    if (nested != pattern.children.end()) {
        return lower_typed_child(*nested, expected, lowering, resolver);
    }
    return lower_typed_child(pattern.children.front(), expected, lowering, resolver);
}

[[nodiscard]] PatternId lower_typed_pattern(const TypedPattern &pattern,
                                            PatternExpected expected,
                                            MatchMatrixLowering &lowering,
                                            const MatchTypedPatternResolver &resolver) {
    switch (pattern.kind) {
    case TypedPatternKind::Literal:
        return lower_literal_spelling(pattern.literal_spelling, pattern.range, expected, lowering);
    case TypedPatternKind::Variant:
        return lower_typed_variant_pattern(pattern, expected, lowering, resolver);
    case TypedPatternKind::Wildcard:
        return lowering.context.make_wildcard(pattern.range);
    case TypedPatternKind::Binding:
        return lower_typed_binding_pattern(pattern, expected, lowering, resolver);
    case TypedPatternKind::Tuple:
        return lower_typed_tuple_pattern(pattern, expected, lowering, resolver);
    case TypedPatternKind::Or:
        return lower_typed_or_pattern(pattern, expected, lowering, resolver);
    case TypedPatternKind::IntRange:
        return lower_int_range_pattern(pattern, expected, lowering);
    }
    return lowering.context.make_never(pattern.range);
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

[[nodiscard]] MatchRedundantPatternDiagnostic
make_redundant_pattern(const PatternRedundantOrBranch &branch,
                       const std::vector<std::size_t> &arm_indices) {
    MatchRedundantPatternDiagnostic diagnostic{
        .arm_index = arm_indices[branch.row_index],
        .branch_index = branch.branch_index + 1,
        .branch_range = branch.branch_range,
    };
    diagnostic.covering_arm_indices.reserve(branch.covering_row_indices.size());
    for (const auto row_index : branch.covering_row_indices) {
        diagnostic.covering_arm_indices.push_back(arm_indices[row_index]);
    }
    diagnostic.covering_arm_ranges = branch.covering_row_ranges;
    diagnostic.covering_branch_indices.reserve(branch.covering_branch_indices.size());
    for (const auto branch_index : branch.covering_branch_indices) {
        diagnostic.covering_branch_indices.push_back(branch_index + 1);
    }
    diagnostic.covering_branch_ranges = branch.covering_branch_ranges;
    return diagnostic;
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

    diagnostics.redundant_patterns.reserve(analysis.redundant_or_branches.size());
    for (const auto &branch : analysis.redundant_or_branches) {
        diagnostics.redundant_patterns.push_back(make_redundant_pattern(branch, arm_indices));
    }

    return diagnostics;
}

MatchExhaustivenessDiagnostics
analyze_with_typed_lowering(MatchMatrixLowering lowering,
                            const EnumTypeInfo &enum_info,
                            const std::vector<MatchTypedPatternRow> &typed_rows,
                            SourceRange match_range,
                            const MatchTypedPatternResolver &pattern_resolver) {
    MatchExhaustivenessDiagnostics diagnostics;
    const auto root_expected = expected_for_domain(lowering, lowering.root_domain);

    std::vector<PatternUsefulnessRow> rows;
    std::vector<std::size_t> arm_indices;
    rows.reserve(typed_rows.size());
    arm_indices.reserve(typed_rows.size());
    for (std::size_t index = 0; index < typed_rows.size(); ++index) {
        const auto &row = typed_rows[index];
        const auto *typed_pattern = resolve_typed_pattern(pattern_resolver, row.pattern_index);
        if (typed_pattern == nullptr) {
            continue;
        }
        const auto pattern_id =
            lower_typed_pattern(*typed_pattern, root_expected, lowering, pattern_resolver);
        rows.push_back(PatternUsefulnessRow{
            .pattern = pattern_id,
            .range = row.range,
            .contributes_to_exhaustiveness = row.contributes_to_exhaustiveness,
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

    diagnostics.redundant_patterns.reserve(analysis.redundant_or_branches.size());
    for (const auto &branch : analysis.redundant_or_branches) {
        diagnostics.redundant_patterns.push_back(make_redundant_pattern(branch, arm_indices));
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

MatchExhaustivenessDiagnostics
analyze_match_exhaustiveness(const Type &scrutinee_type,
                             const EnumTypeInfo &enum_info,
                             const std::vector<MatchTypedPatternRow> &rows,
                             SourceRange match_range,
                             const MatchEnumInfoResolver &enum_resolver,
                             const MatchTypedPatternResolver &pattern_resolver) {
    return analyze_with_typed_lowering(make_lowering(scrutinee_type, enum_info, enum_resolver),
                                       enum_info,
                                       rows,
                                       match_range,
                                       pattern_resolver);
}

} // namespace ahfl
