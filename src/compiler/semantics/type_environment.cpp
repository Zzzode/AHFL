#include "ahfl/compiler/semantics/typecheck.hpp"

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

MaybeCRef<StructTypeInfo> TypeEnvironment::get_struct(const Type &type) const {
    if (type.nominal_symbol.has_value()) {
        if (auto by_symbol = get_struct(*type.nominal_symbol); by_symbol.has_value()) {
            return by_symbol;
        }
    }

    return find_struct(type.name);
}

MaybeCRef<EnumTypeInfo> TypeEnvironment::get_enum(SymbolId id) const {
    return get_from_map(enums_, id);
}

MaybeCRef<EnumTypeInfo> TypeEnvironment::get_enum(const Type &type) const {
    if (type.nominal_symbol.has_value()) {
        if (auto by_symbol = get_enum(*type.nominal_symbol); by_symbol.has_value()) {
            return by_symbol;
        }
    }

    return find_enum(type.name);
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
    seed = fingerprint_mix(seed, static_cast<std::uint64_t>(type->kind));
    seed = fingerprint_mix(seed, fingerprint_string(type->name));
    if (type->string_bounds.has_value()) {
        seed = fingerprint_mix(seed, static_cast<std::uint64_t>(type->string_bounds->first));
        seed = fingerprint_mix(seed, static_cast<std::uint64_t>(type->string_bounds->second));
    }
    if (type->decimal_scale.has_value()) {
        seed = fingerprint_mix(seed, static_cast<std::uint64_t>(*type->decimal_scale));
    }
    seed = fingerprint_mix(seed, fingerprint_optional_symbol(type->nominal_symbol));
    seed = fingerprint_mix(seed, fingerprint_type(type->first));
    seed = fingerprint_mix(seed, fingerprint_type(type->second));
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

    return std::cref(expression_types_[iter->second]);
}

MaybeCRef<ExpressionTypeInfo>
TypeCheckResult::find_expression_type_by_node(std::uint64_t node_id,
                                              std::optional<SourceId> source_id) const {
    if (node_id == 0) {
        return std::nullopt;
    }
    ensure_expression_type_lookup_cache();

    const auto iter = expression_type_node_cache_.find(ExpressionTypeNodeKey{
        .node_id = node_id,
        .source_id = source_id,
    });
    if (iter == expression_type_node_cache_.end()) {
        return std::nullopt;
    }

    return std::cref(expression_types_[iter->second]);
}

std::vector<ExpressionTypeInfo> &TypeCheckResult::mutable_expression_types() noexcept {
    invalidate_expression_type_lookup_cache();
    return expression_types_;
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

std::size_t TypeCheckResult::ExpressionTypeNodeKeyHash::operator()(
    const TypeCheckResult::ExpressionTypeNodeKey &key) const noexcept {
    auto seed = std::hash<std::uint64_t>{}(key.node_id);
    seed = hash_mix(seed, std::hash<bool>{}(key.source_id.has_value()));
    if (key.source_id.has_value()) {
        seed = hash_mix(seed, std::hash<std::size_t>{}(key.source_id->value));
    }
    return seed;
}

void TypeCheckResult::rebuild_expression_type_lookup_cache() const {
    expression_type_lookup_cache_.clear();
    expression_type_lookup_cache_.reserve(expression_types_.size());
    expression_type_node_cache_.clear();
    expression_type_node_cache_.reserve(expression_types_.size());

    for (std::size_t index = 0; index < expression_types_.size(); ++index) {
        const auto &entry = expression_types_[index];
        expression_type_lookup_cache_.try_emplace(
            ExpressionTypeLookupKey{
                .begin_offset = entry.range.begin_offset,
                .end_offset = entry.range.end_offset,
                .source_id = entry.source_id,
            },
            index);
        if (entry.node_id != 0) {
            // Last writer wins on collision; remember_expression_type updates
            // existing entries in place so the cache stays consistent.
            expression_type_node_cache_.insert_or_assign(
                ExpressionTypeNodeKey{
                    .node_id = entry.node_id,
                    .source_id = entry.source_id,
                },
                index);
        }
    }

    expression_type_lookup_cache_size_ = expression_types_.size();
    expression_type_lookup_cache_data_ = expression_types_.data();
    expression_type_lookup_cache_valid_ = true;
}

void TypeCheckResult::ensure_expression_type_lookup_cache() const {
    if (!expression_type_lookup_cache_valid_ ||
        expression_type_lookup_cache_size_ != expression_types_.size() ||
        expression_type_lookup_cache_data_ != expression_types_.data()) {
        rebuild_expression_type_lookup_cache();
    }
}

void TypeCheckResult::invalidate_expression_type_lookup_cache() const noexcept {
    expression_type_lookup_cache_valid_ = false;
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
