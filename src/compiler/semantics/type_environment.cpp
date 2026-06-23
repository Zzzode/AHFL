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
    // Use the hash index when available; fall back to linear scan for
    // callers that build a StructTypeInfo without calling rebuild_field_index().
    if (!field_index_.empty()) {
        const auto iter = field_index_.find(std::string(name));
        if (iter != field_index_.end() && iter->second < fields.size()) {
            return std::cref(fields[iter->second]);
        }
        return std::nullopt;
    }

    for (const auto &field : fields) {
        if (field.name == name) {
            return std::cref(field);
        }
    }

    return std::nullopt;
}

void StructTypeInfo::rebuild_field_index() {
    field_index_.clear();
    field_index_.reserve(fields.size());
    for (std::size_t i = 0; i < fields.size(); ++i) {
        field_index_.emplace(fields[i].name, i);
    }
}

bool EnumTypeInfo::has_variant(std::string_view name) const noexcept {
    // Use the hash set when available; fall back to linear scan for
    // callers that build an EnumTypeInfo without calling rebuild_variant_index().
    if (!variant_set_.empty()) {
        return variant_set_.count(std::string(name)) > 0;
    }

    for (const auto &variant : variants) {
        if (variant.name == name) {
            return true;
        }
    }

    return false;
}

void EnumTypeInfo::rebuild_variant_index() {
    variant_set_.clear();
    variant_set_.reserve(variants.size());
    for (const auto &v : variants) {
        variant_set_.insert(v.name);
    }
}

MaybeCRef<EnumVariantInfo> EnumTypeInfo::find_variant(std::string_view name) const {
    for (const auto &variant : variants) {
        if (variant.name == name) {
            return std::cref(variant);
        }
    }
    return std::nullopt;
}

// P3 (RFC §3.2.2 / type-system §1.3): trait item lookups. Linear scans — a
// trait rarely has more than a handful of methods/assoc types (mirrors
// EnumTypeInfo::find_variant).
MaybeCRef<TraitMethodInfo> TraitTypeInfo::find_method(std::string_view name) const {
    for (const auto &method : methods) {
        if (method.name == name) {
            return std::cref(method);
        }
    }
    return std::nullopt;
}

MaybeCRef<TraitAssocTypeInfo> TraitTypeInfo::find_assoc_type(std::string_view name) const {
    for (const auto &assoc : assoc_types) {
        if (assoc.name == name) {
            return std::cref(assoc);
        }
    }
    return std::nullopt;
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

MaybeCRef<FlowTypeInfo> TypeEnvironment::get_flow(SymbolId id) const {
    return get_from_map(flows_, id);
}

MaybeCRef<ContractTypeInfo> TypeEnvironment::get_contract(SymbolId id) const {
    return get_from_map(contracts_, id);
}

MaybeCRef<FnTypeInfo> TypeEnvironment::get_fn(SymbolId id) const {
    return get_from_map(functions_, id);
}

// P3 (RFC §3.2.2 / type-system §1.3): trait lookup mirrors fn lookup.
MaybeCRef<TraitTypeInfo> TypeEnvironment::get_trait(SymbolId id) const {
    return get_from_map(traits_, id);
}

MaybeCRef<TraitTypeInfo> TypeEnvironment::find_trait(std::string_view canonical_name) const {
    // P3: traits are keyed by symbol id; the reverse name index is built lazily
    // on first lookup to avoid re-walking the trait table per query. The
    // typical program has a handful of traits, so the walk is cheap.
    const auto iter = trait_name_index_.find(std::string(canonical_name));
    if (iter != trait_name_index_.end()) {
        const auto trait_iter = traits_.find(iter->second);
        if (trait_iter != traits_.end()) {
            return std::cref(trait_iter->second);
        }
    }
    for (const auto &[id, info] : traits_) {
        if (info.canonical_name == canonical_name) {
            const_cast<TypeEnvironment *>(this)->trait_name_index_.emplace(info.canonical_name, id);
            return std::cref(info);
        }
    }
    return std::nullopt;
}

MaybeCRef<ImplTypeInfo>
TypeEnvironment::resolve_trait_impl(SymbolId trait_symbol, SymbolId target_symbol) const {
    // P3 (RFC §2.1): walk the impl table for a trait impl matching the
    // (trait, target) symbol pair. Coherence guarantees at most one such impl
    // (the duplicate-trait-impl detector rejects the second), so the first
    // match is authoritative. Generic-argument unification is deferred to the
    // method-call expr typecheck (no call sites today).
    for (const auto &[index, impl] : impls_) {
        (void)index;
        if (impl.is_inherent) {
            continue;
        }
        if (impl.trait_symbol.has_value() && *impl.trait_symbol == trait_symbol &&
            impl.target_symbol.has_value() && *impl.target_symbol == target_symbol) {
            return std::cref(impl);
        }
    }
    return std::nullopt;
}

// P3c.S4a: single-source-of-truth normalized type key for coherence and
// orphan-rule comparison. Two types compare equivalent iff this function
// produces identical strings. For nominal types (struct/enum) the key carries
// the nominal symbol id, canonical name, and type-argument sub-keys so two
// instantiations that differ in their generic arguments remain distinct. For
// all other types we fall back to Type::describe(), which is deterministic
// over hash-consed types.
namespace {
[[nodiscard]] std::string normalize_args_key(const std::vector<TypePtr> &args) {
    std::string key;
    key.reserve(args.size() * 16);
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i != 0) {
            key.push_back(',');
        }
        if (args[i] != nullptr) {
            key += TypeEnvironment::normalize_type_key(*args[i]);
        } else {
            key += "<null>";
        }
    }
    return key;
}
} // namespace

std::string TypeEnvironment::normalize_type_key(const Type &type) {
    return type.visit(types::Overloads{
        [](const types::StructT &s) {
            return "struct:" + std::to_string(s.symbol.has_value() ? s.symbol->value : 0) +
                   ":" + s.canonical_name + "<" + normalize_args_key(s.type_args) + ">";
        },
        [](const types::EnumT &e) {
            return "enum:" + std::to_string(e.symbol.has_value() ? e.symbol->value : 0) +
                   ":" + e.canonical_name + "<" + normalize_args_key(e.type_args) + ">";
        },
        [](const types::EnumVariantT &v) {
            return "enum_variant:" + v.canonical_name + "::" + v.variant_name + "<" +
                   normalize_args_key(v.type_args) + ">";
        },
        [](const types::FnT &f) {
            return "fn<" + normalize_args_key(f.params) + "->" +
                   std::string{f.return_type ? normalize_type_key(*f.return_type) : std::string{"void"}} +
                   ">";
        },
        [](const types::TypeVarT &v) { return "type_var:" + v.name; },
        [&type](const auto &) { return type.describe(); },
    });
}

// P3c.S4a: shared conflict predicate. An impl conflicts with another when
// both are non-inherent, both target the same trait symbol, and their target
// types normalize to the same key. The orphan-rule checker and the strict
// coherence duplicate detector (build_impl_types) both route through this
// function so the equivalence relation is defined exactly once.
bool TypeEnvironment::impls_conflict_for_type(const ImplTypeInfo &lhs,
                                              const ImplTypeInfo &rhs) {
    if (lhs.is_inherent || rhs.is_inherent) {
        return false;
    }
    if (!lhs.trait_symbol.has_value() || !rhs.trait_symbol.has_value()) {
        return false;
    }
    if (*lhs.trait_symbol != *rhs.trait_symbol) {
        return false;
    }
    if (lhs.target_type == nullptr || rhs.target_type == nullptr) {
        return false;
    }
    return normalize_type_key(*lhs.target_type) == normalize_type_key(*rhs.target_type);
}

// P3c.S4a: walk the impl table and return all non-inherent impl refs whose
// (trait, normalized target type) key equals the query (trait_symbol,
// concrete_type). `trait_symbol` may be nullopt to enumerate every impl for
// the concrete type; the same normalized-type equivalence as
// impls_conflict_for_type is used so the returned set is consistent with
// both the orphan rule and the duplicate-impl detector.
std::vector<ImplRef>
TypeEnvironment::find_impls(std::optional<SymbolId> trait_symbol,
                            const Type &concrete_type) const {
    std::vector<ImplRef> matches;
    const auto query_key = normalize_type_key(concrete_type);
    for (const auto &[index, impl] : impls_) {
        if (impl.is_inherent) {
            continue;
        }
        if (trait_symbol.has_value()) {
            if (!impl.trait_symbol.has_value() || *impl.trait_symbol != *trait_symbol) {
                continue;
            }
        }
        if (impl.target_type == nullptr) {
            continue;
        }
        if (normalize_type_key(*impl.target_type) == query_key) {
            matches.push_back(ImplRef{index, &impl});
        }
    }
    return matches;
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
        [&](const types::FnT &f) {
            for (const auto &p : f.params) {
                seed = fingerprint_mix(seed, fingerprint_type(p));
            }
            seed = fingerprint_mix(seed, fingerprint_type(f.return_type));
            seed = fingerprint_mix(seed, static_cast<std::uint64_t>(f.effect.kind));
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
    info.rebuild_field_index();
    struct_name_index_.insert_or_assign(info.canonical_name, id);
    structs_.insert_or_assign(id, std::move(info));
}

void TypeEnvironment::index_enum(std::size_t id, EnumTypeInfo info) {
    info.rebuild_variant_index();
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
        case SymbolKind::Function: {
            // P2 (RFC §3.2.2): dump the fn signature so the environment dump
            // covers the new declaration surface.
            const auto info = environment.get_fn(symbol.id);
            if (!info.has_value()) {
                break;
            }

            out << "fn " << info->get().canonical_name;
            if (!info->get().type_param_names.empty()) {
                out << '<';
                for (std::size_t index = 0; index < info->get().type_param_names.size(); ++index) {
                    if (index != 0) {
                        out << ", ";
                    }
                    out << info->get().type_param_names[index];
                }
                out << '>';
            }
            out << '(';
            for (std::size_t index = 0; index < info->get().params.size(); ++index) {
                if (index != 0) {
                    out << ", ";
                }
                const auto &param = info->get().params[index];
                out << param.name << ": " << (param.type ? param.type->describe() : "Any");
            }
            out << ')';
            if (info->get().return_type) {
                out << " -> " << info->get().return_type->describe();
            }
            out << '\n';
            break;
        }
        case SymbolKind::Trait: {
            // P3 (RFC §3.2.2 / type-system §1.3): dump the trait surface so
            // the environment dump covers the new declaration.
            const auto info = environment.get_trait(symbol.id);
            if (!info.has_value()) {
                break;
            }
            out << "trait " << info->get().canonical_name;
            if (!info->get().type_param_names.empty()) {
                out << '<';
                for (std::size_t index = 0; index < info->get().type_param_names.size(); ++index) {
                    if (index != 0) {
                        out << ", ";
                    }
                    out << info->get().type_param_names[index];
                }
                out << '>';
            }
            if (!info->get().super_traits.empty()) {
                out << ": ";
                for (std::size_t index = 0; index < info->get().super_traits.size(); ++index) {
                    if (index != 0) {
                        out << " + ";
                    }
                    const auto super = environment.get_trait(info->get().super_traits[index]);
                    out << (super.has_value() ? super->get().canonical_name : std::string{"<unknown>"});
                }
            }
            out << " { " << info->get().methods.size() << " method(s), "
                << info->get().assoc_types.size() << " assoc type(s) }\n";
            break;
        }
        }
    }
}

} // namespace ahfl
