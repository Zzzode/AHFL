#include "ahfl/compiler/semantics/typecheck.hpp"

#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace ahfl {

namespace {

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

MaybeCRef<StructTypeInfo> TypeEnvironment::get_struct(const Type &type) const {
    const auto *struct_type = type.get_if<types::StructT>();
    if (struct_type == nullptr) {
        return std::nullopt;
    }

    if (struct_type->symbol.has_value()) {
        if (auto by_symbol = get_struct(*struct_type->symbol); by_symbol.has_value()) {
            return by_symbol;
        }
    }

    return find_struct(struct_type->canonical_name);
}

MaybeCRef<EnumTypeInfo> TypeEnvironment::get_enum(SymbolId id) const {
    return get_from_map(enums_, id);
}

MaybeCRef<EnumTypeInfo> TypeEnvironment::get_enum(const Type &type) const {
    const auto *enum_type = type.get_if<types::EnumT>();
    if (enum_type == nullptr) {
        return std::nullopt;
    }

    if (enum_type->symbol.has_value()) {
        if (auto by_symbol = get_enum(*enum_type->symbol); by_symbol.has_value()) {
            return by_symbol;
        }
    }

    return find_enum(enum_type->canonical_name);
}

MaybeCRef<StructTypeInfo> TypeEnvironment::find_struct(std::string_view canonical_name) const {
    const auto iter = struct_name_index_.find(std::string(canonical_name));
    if (iter == struct_name_index_.end()) {
        return std::nullopt;
    }

    const auto info_iter = structs_.find(iter->second);
    if (info_iter == structs_.end()) {
        return std::nullopt;
    }

    return std::cref(info_iter->second);
}

MaybeCRef<EnumTypeInfo> TypeEnvironment::find_enum(std::string_view canonical_name) const {
    const auto iter = enum_name_index_.find(std::string(canonical_name));
    if (iter == enum_name_index_.end()) {
        return std::nullopt;
    }

    const auto info_iter = enums_.find(iter->second);
    if (info_iter == enums_.end()) {
        return std::nullopt;
    }

    return std::cref(info_iter->second);
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

bool TypeEnvironment::is_agent_context_struct(SymbolId id) const noexcept {
    return agent_context_struct_ids_.contains(id.value);
}

namespace {

// 64-bit FNV-1a-style mix used for fingerprint composition. We do not use
// std::hash<size_t> directly because we want byte-stable hashes that survive
// rebuilds; FNV-1a meets that requirement and is cheap to compute.
[[nodiscard]] std::uint64_t fingerprint_mix(std::uint64_t seed, std::uint64_t value) noexcept {
    constexpr std::uint64_t kPrime = 0x100000001b3ULL;
    seed ^= value;
    seed *= kPrime;
    return seed;
}

[[nodiscard]] std::uint64_t fingerprint_string(std::string_view text) noexcept {
    std::uint64_t seed = 0xcbf29ce484222325ULL; // FNV offset basis
    for (const auto byte : text) {
        seed = fingerprint_mix(seed, static_cast<std::uint64_t>(static_cast<unsigned char>(byte)));
    }
    return seed;
}

[[nodiscard]] std::uint64_t fingerprint_optional_symbol(std::optional<SymbolId> id) noexcept {
    if (!id.has_value()) {
        return 0xfeedfacefeedfaceULL;
    }
    return fingerprint_mix(0xc0ffeec0ffeec0ffULL, static_cast<std::uint64_t>(id->value));
}

// Hash-consed Type identity is sufficient at the structural level because
// every distinct type lives at a single canonical address. We additionally
// fold in payload fields so the fingerprint is stable across runs (where the
// canonical address itself is not).
[[nodiscard]] std::uint64_t fingerprint_type(const Type *type) noexcept {
    if (type == nullptr) {
        return 0xdeaddeaddeaddeadULL;
    }

    std::uint64_t seed = 0x9e3779b97f4a7c15ULL;
    seed = fingerprint_mix(seed, static_cast<std::uint64_t>(type->payload.index()));

    type->visit(types::Overloads{
        [&](const types::BoundedStringT &b) {
            seed = fingerprint_mix(seed, static_cast<std::uint64_t>(b.minimum));
            seed = fingerprint_mix(seed, static_cast<std::uint64_t>(b.maximum));
        },
        [&](const types::DecimalT &d) {
            seed = fingerprint_mix(seed, static_cast<std::uint64_t>(d.scale));
        },
        [&](const types::StructT &s) {
            seed = fingerprint_mix(seed, fingerprint_string(s.canonical_name));
            seed = fingerprint_mix(seed, fingerprint_optional_symbol(s.symbol));
        },
        [&](const types::EnumT &e) {
            seed = fingerprint_mix(seed, fingerprint_string(e.canonical_name));
            seed = fingerprint_mix(seed, fingerprint_optional_symbol(e.symbol));
        },
        [&](const types::OptionalT &o) {
            seed = fingerprint_mix(seed, fingerprint_type(o.inner));
        },
        [&](const types::ListT &l) {
            seed = fingerprint_mix(seed, fingerprint_type(l.element));
        },
        [&](const types::SetT &s) { seed = fingerprint_mix(seed, fingerprint_type(s.element)); },
        [&](const types::MapT &m) {
            seed = fingerprint_mix(seed, fingerprint_type(m.key));
            seed = fingerprint_mix(seed, fingerprint_type(m.value));
        },
        [](const auto &) {},
    });

    return seed;
}

} // namespace

std::optional<std::uint64_t> TypeEnvironment::signature_fingerprint(SymbolId id) const {
    if (const auto info = get_struct(id); info.has_value()) {
        std::uint64_t seed = fingerprint_string("struct:");
        seed = fingerprint_mix(seed, fingerprint_string(info->get().canonical_name));
        for (const auto &field : info->get().fields) {
            seed = fingerprint_mix(seed, fingerprint_string(field.name));
            seed = fingerprint_mix(seed, fingerprint_type(field.type));
            seed = fingerprint_mix(seed, static_cast<std::uint64_t>(field.has_default ? 1U : 0U));
        }
        return seed;
    }

    if (const auto info = get_enum(id); info.has_value()) {
        std::uint64_t seed = fingerprint_string("enum:");
        seed = fingerprint_mix(seed, fingerprint_string(info->get().canonical_name));
        for (const auto &variant : info->get().variants) {
            seed = fingerprint_mix(seed, fingerprint_string(variant.name));
        }
        return seed;
    }

    if (const auto info = get_capability(id); info.has_value()) {
        std::uint64_t seed = fingerprint_string("capability:");
        seed = fingerprint_mix(seed, fingerprint_string(info->get().canonical_name));
        for (const auto &param : info->get().params) {
            seed = fingerprint_mix(seed, fingerprint_string(param.name));
            seed = fingerprint_mix(seed, fingerprint_type(param.type));
        }
        seed = fingerprint_mix(seed, fingerprint_type(info->get().return_type));
        return seed;
    }

    if (const auto info = get_predicate(id); info.has_value()) {
        std::uint64_t seed = fingerprint_string("predicate:");
        seed = fingerprint_mix(seed, fingerprint_string(info->get().canonical_name));
        for (const auto &param : info->get().params) {
            seed = fingerprint_mix(seed, fingerprint_string(param.name));
            seed = fingerprint_mix(seed, fingerprint_type(param.type));
        }
        return seed;
    }

    if (const auto info = get_agent(id); info.has_value()) {
        std::uint64_t seed = fingerprint_string("agent:");
        seed = fingerprint_mix(seed, fingerprint_string(info->get().canonical_name));
        seed = fingerprint_mix(seed, fingerprint_type(info->get().input_type));
        seed = fingerprint_mix(seed, fingerprint_type(info->get().context_type));
        seed = fingerprint_mix(seed, fingerprint_type(info->get().output_type));
        for (const auto cap_id : info->get().capability_symbols) {
            seed = fingerprint_mix(seed, static_cast<std::uint64_t>(cap_id.value));
        }
        return seed;
    }

    if (const auto info = get_workflow(id); info.has_value()) {
        std::uint64_t seed = fingerprint_string("workflow:");
        seed = fingerprint_mix(seed, fingerprint_string(info->get().canonical_name));
        seed = fingerprint_mix(seed, fingerprint_type(info->get().input_type));
        seed = fingerprint_mix(seed, fingerprint_type(info->get().output_type));
        return seed;
    }

    if (const auto type = get_const_type(id); type.has_value()) {
        std::uint64_t seed = fingerprint_string("const:");
        seed = fingerprint_mix(seed, fingerprint_type(&type->get()));
        return seed;
    }

    return std::nullopt;
}

void TypeEnvironment::index_struct(std::size_t id, StructTypeInfo info) {
    struct_name_index_.insert_or_assign(info.canonical_name, id);
    structs_.insert_or_assign(id, std::move(info));
}

void TypeEnvironment::index_enum(std::size_t id, EnumTypeInfo info) {
    enum_name_index_.insert_or_assign(info.canonical_name, id);
    enums_.insert_or_assign(id, std::move(info));
}

void TypeEnvironment::mark_agent_context_struct(SymbolId id) {
    agent_context_struct_ids_.insert(id.value);
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
