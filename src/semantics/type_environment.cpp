#include "ahfl/semantics/typecheck.hpp"

#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace ahfl {

namespace {

[[nodiscard]] std::size_t hash_mix(std::size_t seed, std::size_t value) noexcept {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
    return seed;
}

template <typename T>
[[nodiscard]] MaybeCRef<T> get_from_map(const std::unordered_map<std::size_t, T> &map,
                                        SymbolId id) {
    if (const auto iter = map.find(id.value); iter != map.end()) {
        return std::cref(iter->second);
    }

    return std::nullopt;
}

[[nodiscard]] std::string join_names(const std::vector<std::string> &names) {
    std::ostringstream builder;

    for (std::size_t index = 0; index < names.size(); ++index) {
        if (index != 0) {
            builder << ", ";
        }

        builder << names[index];
    }

    return builder.str();
}

[[nodiscard]] std::string join_symbol_names(const SymbolTable &symbols,
                                            const std::vector<SymbolId> &ids) {
    std::vector<std::string> names;
    names.reserve(ids.size());

    for (const auto id : ids) {
        if (const auto symbol = symbols.get(id); symbol.has_value()) {
            names.push_back(symbol->get().canonical_name);
        }
    }

    return join_names(names);
}

} // namespace

MaybeCRef<StructFieldInfo> StructTypeInfo::find_field(std::string_view name) const {
    for (const auto &field : fields) {
        if (field.name == name) {
            return std::cref(field);
        }
    }

    return std::nullopt;
}

bool EnumTypeInfo::has_variant(std::string_view name) const noexcept {
    for (const auto &variant : variants) {
        if (variant.name == name) {
            return true;
        }
    }

    return false;
}

MaybeCRef<Type> TypeEnvironment::get_const_type(SymbolId id) const {
    if (const auto iter = const_types_.find(id.value);
        iter != const_types_.end() && static_cast<bool>(iter->second)) {
        return std::cref(*iter->second);
    }

    return std::nullopt;
}

MaybeCRef<StructTypeInfo> TypeEnvironment::get_struct(SymbolId id) const {
    return get_from_map(structs_, id);
}

MaybeCRef<EnumTypeInfo> TypeEnvironment::get_enum(SymbolId id) const {
    return get_from_map(enums_, id);
}

MaybeCRef<StructTypeInfo> TypeEnvironment::find_struct(std::string_view canonical_name) const {
    for (const auto &[id, info] : structs_) {
        (void)id;
        if (info.canonical_name == canonical_name) {
            return std::cref(info);
        }
    }

    return std::nullopt;
}

MaybeCRef<EnumTypeInfo> TypeEnvironment::find_enum(std::string_view canonical_name) const {
    for (const auto &[id, info] : enums_) {
        (void)id;
        if (info.canonical_name == canonical_name) {
            return std::cref(info);
        }
    }

    return std::nullopt;
}

MaybeCRef<CapabilityTypeInfo> TypeEnvironment::get_capability(SymbolId id) const {
    return get_from_map(capabilities_, id);
}

MaybeCRef<PredicateTypeInfo> TypeEnvironment::get_predicate(SymbolId id) const {
    return get_from_map(predicates_, id);
}

MaybeCRef<AgentTypeInfo> TypeEnvironment::get_agent(SymbolId id) const {
    return get_from_map(agents_, id);
}

MaybeCRef<WorkflowTypeInfo> TypeEnvironment::get_workflow(SymbolId id) const {
    return get_from_map(workflows_, id);
}

MaybeCRef<ExpressionTypeInfo>
TypeCheckResult::find_expression_type(SourceRange range, std::optional<SourceId> source_id) const {
    ensure_expression_type_lookup_cache();

    const auto iter = expression_type_lookup_cache_.find(ExpressionTypeLookupKey{
        .begin_offset = range.begin_offset,
        .end_offset = range.end_offset,
        .source_id = source_id,
    });
    if (iter == expression_type_lookup_cache_.end()) {
        return std::nullopt;
    }

    return std::cref(expression_types[iter->second]);
}

std::size_t TypeCheckResult::ExpressionTypeLookupKeyHash::operator()(
    const TypeCheckResult::ExpressionTypeLookupKey &key) const noexcept {
    auto seed = std::hash<std::size_t>{}(key.begin_offset);
    seed = hash_mix(seed, std::hash<std::size_t>{}(key.end_offset));
    seed = hash_mix(seed, std::hash<bool>{}(key.source_id.has_value()));
    if (key.source_id.has_value()) {
        seed = hash_mix(seed, std::hash<std::size_t>{}(key.source_id->value));
    }
    return seed;
}

void TypeCheckResult::rebuild_expression_type_lookup_cache() const {
    expression_type_lookup_cache_.clear();
    expression_type_lookup_cache_.reserve(expression_types.size());

    for (std::size_t index = 0; index < expression_types.size(); ++index) {
        const auto &entry = expression_types[index];
        expression_type_lookup_cache_.try_emplace(ExpressionTypeLookupKey{
                                                      .begin_offset = entry.range.begin_offset,
                                                      .end_offset = entry.range.end_offset,
                                                      .source_id = entry.source_id,
                                                  },
                                                  index);
    }

    expression_type_lookup_cache_size_ = expression_types.size();
    expression_type_lookup_cache_data_ = expression_types.data();
}

void TypeCheckResult::ensure_expression_type_lookup_cache() const {
    if (expression_type_lookup_cache_size_ != expression_types.size() ||
        expression_type_lookup_cache_data_ != expression_types.data()) {
        rebuild_expression_type_lookup_cache();
    }
}

void dump_type_environment(const TypeEnvironment &environment,
                           const SymbolTable &symbols,
                           std::ostream &out) {
    for (const auto &symbol : symbols.symbols()) {
        switch (symbol.kind) {
        case SymbolKind::Const: {
            const auto type = environment.get_const_type(symbol.id);
            if (!type.has_value()) {
                break;
            }

            out << "const " << symbol.canonical_name << ": " << type->get().describe() << '\n';
            break;
        }
        case SymbolKind::Struct: {
            const auto info = environment.get_struct(symbol.id);
            if (!info.has_value()) {
                break;
            }

            out << "struct " << info->get().canonical_name << '\n';
            for (const auto &field : info->get().fields) {
                out << "  " << field.name << ": " << (field.type ? field.type->describe() : "Any");
                if (field.has_default) {
                    out << " = <default>";
                }
                out << '\n';
            }
            break;
        }
        case SymbolKind::Enum: {
            const auto info = environment.get_enum(symbol.id);
            if (!info.has_value()) {
                break;
            }

            out << "enum " << info->get().canonical_name << '\n';
            for (const auto &variant : info->get().variants) {
                out << "  " << variant.name << '\n';
            }
            break;
        }
        case SymbolKind::Capability: {
            const auto info = environment.get_capability(symbol.id);
            if (!info.has_value()) {
                break;
            }

            out << "capability " << info->get().canonical_name << '(';
            for (std::size_t index = 0; index < info->get().params.size(); ++index) {
                if (index != 0) {
                    out << ", ";
                }
                const auto &param = info->get().params[index];
                out << param.name << ": " << (param.type ? param.type->describe() : "Any");
            }
            out << ") -> "
                << (info->get().return_type ? info->get().return_type->describe() : "Any") << '\n';
            break;
        }
        case SymbolKind::Predicate: {
            const auto info = environment.get_predicate(symbol.id);
            if (!info.has_value()) {
                break;
            }

            out << "predicate " << info->get().canonical_name << '(';
            for (std::size_t index = 0; index < info->get().params.size(); ++index) {
                if (index != 0) {
                    out << ", ";
                }
                const auto &param = info->get().params[index];
                out << param.name << ": " << (param.type ? param.type->describe() : "Any");
            }
            out << ") -> Bool\n";
            break;
        }
        case SymbolKind::Agent: {
            const auto info = environment.get_agent(symbol.id);
            if (!info.has_value()) {
                break;
            }

            out << "agent " << info->get().canonical_name << '\n';
            out << "  input: "
                << (info->get().input_type ? info->get().input_type->describe() : "Any") << '\n';
            out << "  context: "
                << (info->get().context_type ? info->get().context_type->describe() : "Any")
                << '\n';
            out << "  output: "
                << (info->get().output_type ? info->get().output_type->describe() : "Any") << '\n';
            out << "  capabilities: [" << join_symbol_names(symbols, info->get().capability_symbols)
                << "]\n";
            break;
        }
        case SymbolKind::Workflow: {
            const auto info = environment.get_workflow(symbol.id);
            if (!info.has_value()) {
                break;
            }

            out << "workflow " << info->get().canonical_name << '\n';
            out << "  input: "
                << (info->get().input_type ? info->get().input_type->describe() : "Any") << '\n';
            out << "  output: "
                << (info->get().output_type ? info->get().output_type->describe() : "Any") << '\n';
            break;
        }
        case SymbolKind::TypeAlias:
            break;
        }
    }
}

} // namespace ahfl
