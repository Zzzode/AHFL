#include "runtime/evaluator/pattern_match.hpp"

#include "ahfl/base/support/overloaded.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace ahfl::evaluator {

namespace {

[[nodiscard]] std::string_view last_path_segment(std::string_view path) {
    const auto pos = path.rfind("::");
    if (pos == std::string_view::npos) {
        return path;
    }
    return path.substr(pos + 2);
}

[[nodiscard]] bool
bind_pattern_name(PatternBindings &bindings, std::string_view name, const Value &value) {
    if (name.empty() || name == "_") {
        return true;
    }
    bindings.insert_or_assign(std::string{name}, clone_value(value));
    return true;
}

[[nodiscard]] PatternBindings clone_pattern_bindings(const PatternBindings &bindings) {
    PatternBindings cloned;
    cloned.reserve(bindings.size());
    for (const auto &[name, value] : bindings) {
        cloned.emplace(name, clone_value(value));
    }
    return cloned;
}

[[nodiscard]] bool match_literal_pattern(std::string_view spelling, const Value &value) {
    if (spelling == "none") {
        return is_optional_none(value);
    }
    if (spelling == "true" || spelling == "false") {
        const auto *boolean = std::get_if<BoolValue>(&value.node);
        return boolean != nullptr && boolean->value == (spelling == "true");
    }
    if (spelling.size() >= 2 && spelling.front() == '"' && spelling.back() == '"') {
        const auto *string_value = std::get_if<StringValue>(&value.node);
        return string_value != nullptr &&
               string_value->value == std::string{spelling.substr(1, spelling.size() - 2)};
    }
    try {
        const auto integer = std::stoll(std::string{spelling});
        const auto *int_value = std::get_if<IntValue>(&value.node);
        return int_value != nullptr && int_value->value == integer;
    } catch (...) {
        return false;
    }
}

[[nodiscard]] bool match_variant_pattern(const ir::VariantPattern &pattern,
                                         const Value &value,
                                         PatternBindings &bindings) {
    const auto variant_name = last_path_segment(pattern.path);
    const auto *enum_value = std::get_if<EnumValue>(&value.node);
    if (enum_value == nullptr || enum_value->variant != variant_name) {
        return false;
    }
    if (pattern.kind == ir::VariantPatternKind::Unit) {
        return true;
    }
    if (pattern.kind == ir::VariantPatternKind::Struct) {
        if (enum_value->named_payload.empty()) {
            return false;
        }
        for (const auto &field : pattern.fields) {
            if (field.is_rest) {
                continue;
            }
            const auto iter = enum_value->named_payload.find(field.name);
            if (iter == enum_value->named_payload.end() || !iter->second || !field.pattern) {
                return false;
            }
            if (!match_pattern(*field.pattern, *iter->second, bindings)) {
                return false;
            }
        }
        return true;
    }

    if (enum_value->payload.empty()) {
        return false;
    }
    if (pattern.subpatterns.size() == enum_value->payload.size()) {
        for (std::size_t index = 0; index < pattern.subpatterns.size(); ++index) {
            if (!pattern.subpatterns[index] || !enum_value->payload[index]) {
                return false;
            }
            if (!match_pattern(
                    *pattern.subpatterns[index], *enum_value->payload[index], bindings)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

} // namespace

bool match_pattern(const ir::MatchPattern &pattern, const Value &value, PatternBindings &bindings) {
    return std::visit(
        Overloaded{
            [&](const ir::LiteralPattern &literal) {
                return match_literal_pattern(literal.spelling, value);
            },
            [&](const ir::IntRangePattern &range) {
                const auto *int_value = std::get_if<IntValue>(&value.node);
                return int_value != nullptr && range.start <= int_value->value &&
                       int_value->value <= range.end;
            },
            [&](const ir::VariantPattern &variant) {
                return match_variant_pattern(variant, value, bindings);
            },
            [](const ir::WildcardPattern &) { return true; },
            [&](const ir::BindingPattern &binding) {
                PatternBindings nested_bindings = clone_pattern_bindings(bindings);
                if (binding.nested && !match_pattern(*binding.nested, value, nested_bindings)) {
                    return false;
                }
                (void)bind_pattern_name(nested_bindings, binding.name, value);
                bindings = std::move(nested_bindings);
                return true;
            },
            [](const ir::TuplePattern &) { return false; },
            [&](const ir::OrPattern &pattern_or) {
                for (const auto &branch : pattern_or.branches) {
                    if (!branch) {
                        continue;
                    }
                    PatternBindings branch_bindings = clone_pattern_bindings(bindings);
                    if (match_pattern(*branch, value, branch_bindings)) {
                        bindings = std::move(branch_bindings);
                        return true;
                    }
                }
                return false;
            },
        },
        pattern.node);
}

} // namespace ahfl::evaluator
