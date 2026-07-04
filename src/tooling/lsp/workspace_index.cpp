#include "tooling/lsp/workspace_index.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <limits>
#include <unordered_map>
#include <utility>

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "compiler/syntax/frontend/project.hpp"

namespace ahfl::lsp {

namespace {

[[nodiscard]] Position to_lsp_position(const SourceFile &source, std::size_t offset) {
    const auto pos = source.locate(offset);
    return Position{
        .line = static_cast<std::uint32_t>(pos.line > 0 ? pos.line - 1 : 0),
        .character = static_cast<std::uint32_t>(pos.column > 0 ? pos.column - 1 : 0),
    };
}

[[nodiscard]] Range to_lsp_range(const SourceFile &source, SourceRange range) {
    const auto bounded_begin = std::min(range.begin_offset, source.content.size());
    const auto bounded_end =
        std::max(bounded_begin, std::min(range.end_offset, source.content.size()));
    return Range{
        .start = to_lsp_position(source, bounded_begin),
        .end = to_lsp_position(source, bounded_end),
    };
}

[[nodiscard]] bool is_unreserved_uri_char(unsigned char ch) noexcept {
    return std::isalnum(ch) != 0 || ch == '-' || ch == '_' || ch == '.' || ch == '~' || ch == '/';
}

[[nodiscard]] std::string percent_encode_path(std::string_view path) {
    constexpr char kHex[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(path.size());
    for (const unsigned char ch : path) {
        if (is_unreserved_uri_char(ch)) {
            encoded.push_back(static_cast<char>(ch));
            continue;
        }
        encoded.push_back('%');
        encoded.push_back(kHex[(ch >> 4) & 0xF]);
        encoded.push_back(kHex[ch & 0xF]);
    }
    return encoded;
}

[[nodiscard]] std::string uri_from_path(const std::filesystem::path &path) {
    return "file://" + percent_encode_path(path.generic_string());
}

[[nodiscard]] std::filesystem::path normalize_path(const std::filesystem::path &path) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    auto candidate = error ? path.lexically_normal() : absolute.lexically_normal();
    if (std::filesystem::exists(candidate, error)) {
        const auto canonical = std::filesystem::weakly_canonical(candidate, error);
        if (!error) {
            candidate = canonical.lexically_normal();
        }
    }
    return candidate;
}

[[nodiscard]] bool path_is_equal_or_descendant(const std::filesystem::path &path,
                                               const std::filesystem::path &ancestor) {
    auto path_part = path.begin();
    for (auto ancestor_part = ancestor.begin(); ancestor_part != ancestor.end(); ++ancestor_part) {
        if (path_part == path.end() || *path_part != *ancestor_part) {
            return false;
        }
        ++path_part;
    }
    return true;
}

[[nodiscard]] package_graph::PackageId
package_id_for_path(const std::vector<LspIndexPackageRoot> &package_roots,
                    const std::filesystem::path &raw_path) {
    const auto path = normalize_path(raw_path);
    const LspIndexPackageRoot *best = nullptr;
    for (const auto &root : package_roots) {
        const auto module_root = normalize_path(root.module_root);
        if (!path_is_equal_or_descendant(path, module_root)) {
            continue;
        }
        if (best == nullptr || module_root.generic_string().size() >
                                   normalize_path(best->module_root).generic_string().size()) {
            best = &root;
        }
    }
    if (best != nullptr) {
        return best->package_id;
    }
    return package_graph::PackageId{std::numeric_limits<std::size_t>::max()};
}

[[nodiscard]] bool has_extent(SourceRange range) noexcept {
    return range.end_offset > range.begin_offset;
}

[[nodiscard]] bool is_identifier_char(char ch) noexcept {
    const auto value = static_cast<unsigned char>(ch);
    return std::isalnum(value) != 0 || ch == '_';
}

[[nodiscard]] SourceRange impl_location_range(const ImplTypeInfo &impl) {
    if (has_extent(impl.target_type_range)) {
        return impl.target_type_range;
    }
    return impl.declaration_range;
}

[[nodiscard]] const SourceUnit *source_unit_for_id(const SourceGraph &graph, SourceId id) {
    for (const auto &source : graph.sources) {
        if (source.id == id) {
            return &source;
        }
    }
    return nullptr;
}

[[nodiscard]] SourceRange symbol_navigation_range(const SourceFile &source, const Symbol &symbol) {
    if (symbol.local_name.empty()) {
        return symbol.declaration_range;
    }

    const auto begin = std::min(symbol.declaration_range.begin_offset, source.content.size());
    const auto end =
        std::max(begin, std::min(symbol.declaration_range.end_offset, source.content.size()));
    std::size_t cursor = begin;
    while (cursor < end) {
        const auto found = source.content.find(symbol.local_name, cursor);
        if (found == std::string::npos || found + symbol.local_name.size() > end) {
            break;
        }

        const auto before_ok = found == 0 || !is_identifier_char(source.content[found - 1]);
        const auto after = found + symbol.local_name.size();
        const auto after_ok =
            after >= source.content.size() || !is_identifier_char(source.content[after]);
        if (before_ok && after_ok) {
            return SourceRange{
                .begin_offset = found,
                .end_offset = after,
            };
        }

        cursor = found + 1;
    }

    return symbol.declaration_range;
}

[[nodiscard]] std::optional<Location> impl_location(const SourceGraph &graph,
                                                    const ImplTypeInfo &impl) {
    if (!impl.source_id.has_value()) {
        return std::nullopt;
    }

    const auto *source = source_unit_for_id(graph, *impl.source_id);
    if (source == nullptr) {
        return std::nullopt;
    }

    const auto range = impl_location_range(impl);
    if (!has_extent(range)) {
        return std::nullopt;
    }

    return Location{
        .uri = uri_from_path(source->path),
        .range = to_lsp_range(source->source, range),
    };
}

[[nodiscard]] std::optional<Location> symbol_location(const SourceGraph &graph,
                                                      const Symbol &symbol) {
    if (!symbol.source_id.has_value() || !has_extent(symbol.declaration_range)) {
        return std::nullopt;
    }

    const auto *source = source_unit_for_id(graph, *symbol.source_id);
    if (source == nullptr) {
        return std::nullopt;
    }

    return Location{
        .uri = uri_from_path(source->path),
        .range = to_lsp_range(source->source, symbol_navigation_range(source->source, symbol)),
    };
}

[[nodiscard]] std::optional<Location> reference_location(const SourceGraph &graph,
                                                         const ResolvedReference &reference) {
    if (!reference.source_id.has_value() || !has_extent(reference.range)) {
        return std::nullopt;
    }

    const auto *source = source_unit_for_id(graph, *reference.source_id);
    if (source == nullptr) {
        return std::nullopt;
    }

    return Location{
        .uri = uri_from_path(source->path),
        .range = to_lsp_range(source->source, reference.range),
    };
}

using DefBySymbolMap = std::unordered_map<std::size_t, DefId>;
using SourceUnitBySourceIdMap = std::unordered_map<std::size_t, SourceUnitId>;
using SourceUnitByPathMap = std::unordered_map<std::string, SourceUnitId>;

[[nodiscard]] TypeKey type_key_for_type_with_defs(const Type &type,
                                                  const DefBySymbolMap *def_by_symbol);

[[nodiscard]] std::vector<TypeKey> type_keys_for_args(const std::vector<TypePtr> &args,
                                                      const DefBySymbolMap *def_by_symbol) {
    std::vector<TypeKey> keys;
    keys.reserve(args.size());
    for (const auto *arg : args) {
        keys.push_back(arg == nullptr ? TypeKey{.kind = TypeKey::Kind::Unknown}
                                      : type_key_for_type_with_defs(*arg, def_by_symbol));
    }
    return keys;
}

[[nodiscard]] std::optional<DefId> def_for_symbol(const DefBySymbolMap &def_by_symbol,
                                                  SymbolId symbol) {
    const auto found = def_by_symbol.find(symbol.value);
    if (found == def_by_symbol.end()) {
        return std::nullopt;
    }
    return found->second;
}

[[nodiscard]] const SourceUnitFact *source_unit_fact(const LspWorkspaceIndex &index,
                                                     SourceUnitId id) {
    const auto &source_units = index.source_units();
    if (id.value >= source_units.size()) {
        return nullptr;
    }
    return &source_units[id.value];
}

[[nodiscard]] SourceUnitId register_source_unit(LspWorkspaceIndex &index,
                                                SourceUnitByPathMap &source_units_by_path,
                                                const LspWorkspaceIndexInput &input,
                                                const std::filesystem::path &raw_path,
                                                FactCompleteness completeness) {
    const auto path = normalize_path(raw_path);
    const auto path_key = path.generic_string();
    if (const auto existing = source_units_by_path.find(path_key);
        existing != source_units_by_path.end()) {
        return existing->second;
    }

    const auto source_unit = SourceUnitId{index.source_units().size()};
    const auto package_id = package_id_for_path(input.package_roots, path);
    index.add_source_unit(SourceUnitFact{
        .source_unit_id = source_unit,
        .package_id = package_id,
        .path = path,
        .uri = uri_from_path(path),
        .revision = input.revision,
        .completeness = completeness,
    });
    source_units_by_path.emplace(path_key, source_unit);
    return source_unit;
}

[[nodiscard]] std::string module_name_for_program(const ast::Program &program) {
    for (const auto &declaration : program.declarations) {
        if (declaration == nullptr || declaration->kind != ast::NodeKind::ModuleDecl) {
            continue;
        }
        const auto &module = static_cast<const ast::ModuleDecl &>(*declaration);
        return module.name == nullptr ? std::string{} : module.name->spelling();
    }
    return {};
}

[[nodiscard]] std::optional<std::pair<SymbolKind, std::string>>
skeleton_symbol_for_declaration(const ast::Decl &declaration) {
    switch (declaration.kind) {
    case ast::NodeKind::StructDecl: {
        const auto &typed = static_cast<const ast::StructDecl &>(declaration);
        return std::pair{SymbolKind::Struct, typed.name};
    }
    case ast::NodeKind::EnumDecl: {
        const auto &typed = static_cast<const ast::EnumDecl &>(declaration);
        return std::pair{SymbolKind::Enum, typed.name};
    }
    case ast::NodeKind::TypeAliasDecl: {
        const auto &typed = static_cast<const ast::TypeAliasDecl &>(declaration);
        return std::pair{SymbolKind::TypeAlias, typed.name};
    }
    case ast::NodeKind::ConstDecl: {
        const auto &typed = static_cast<const ast::ConstDecl &>(declaration);
        return std::pair{SymbolKind::Const, typed.name};
    }
    case ast::NodeKind::CapabilityDecl: {
        const auto &typed = static_cast<const ast::CapabilityDecl &>(declaration);
        return std::pair{SymbolKind::Capability, typed.name};
    }
    case ast::NodeKind::PredicateDecl: {
        const auto &typed = static_cast<const ast::PredicateDecl &>(declaration);
        return std::pair{SymbolKind::Predicate, typed.name};
    }
    case ast::NodeKind::AgentDecl: {
        const auto &typed = static_cast<const ast::AgentDecl &>(declaration);
        return std::pair{SymbolKind::Agent, typed.name};
    }
    case ast::NodeKind::WorkflowDecl: {
        const auto &typed = static_cast<const ast::WorkflowDecl &>(declaration);
        return std::pair{SymbolKind::Workflow, typed.name};
    }
    case ast::NodeKind::FnDecl: {
        const auto &typed = static_cast<const ast::FnDecl &>(declaration);
        return std::pair{SymbolKind::Function, typed.name};
    }
    case ast::NodeKind::TraitDecl: {
        const auto &typed = static_cast<const ast::TraitDecl &>(declaration);
        return std::pair{SymbolKind::Trait, typed.name};
    }
    default:
        return std::nullopt;
    }
}

void append_skeleton_symbol_facts(LspWorkspaceIndex &index,
                                  SourceUnitId source_unit,
                                  package_graph::PackageId package_id,
                                  std::string_view module_name,
                                  std::string_view uri,
                                  const SourceFile &source,
                                  const ast::Program &program) {
    for (const auto &declaration : program.declarations) {
        if (declaration == nullptr) {
            continue;
        }
        const auto skeleton = skeleton_symbol_for_declaration(*declaration);
        if (!skeleton.has_value() || skeleton->second.empty()) {
            continue;
        }

        const auto canonical_name = module_name.empty()
                                        ? skeleton->second
                                        : std::string(module_name) + "::" + skeleton->second;
        const auto selection_range =
            symbol_navigation_range(source,
                                    Symbol{
                                        .local_name = skeleton->second,
                                        .declaration_range = declaration->range,
                                    });
        index.add_symbol(SymbolFact{
            .def_id = DefId{index.symbols().size()},
            .package_id = package_id,
            .source_unit_id = source_unit,
            .kind = skeleton->first,
            .local_name = skeleton->second,
            .canonical_name = canonical_name,
            .declaration_range = declaration->range,
            .selection_range = selection_range,
            .location =
                Location{
                    .uri = std::string(uri),
                    .range = to_lsp_range(source, selection_range),
                },
            .completeness = FactCompleteness::Parsed,
        });
    }
}

void append_parse_skeleton_facts(LspWorkspaceIndex &index,
                                 const Frontend &frontend,
                                 const LspWorkspaceIndexInput &input) {
    SourceUnitByPathMap source_units_by_path;
    for (const auto &entry_file : input.project.entry_files) {
        const auto path = normalize_path(entry_file);
        const auto path_key = path.generic_string();
        auto parse_result = [&]() {
            if (const auto overlay = input.project.source_overlays.find(path_key);
                overlay != input.project.source_overlays.end()) {
                return frontend.parse_text(path_key, overlay->second);
            }
            return frontend.parse_file(path);
        }();
        const auto source_unit = register_source_unit(
            index,
            source_units_by_path,
            input,
            path,
            parse_result.program ? FactCompleteness::Parsed : FactCompleteness::Invalid);
        if (!parse_result.program) {
            continue;
        }

        const auto module_name = module_name_for_program(*parse_result.program);
        const auto *source_fact = source_unit_fact(index, source_unit);
        const auto package_id = source_fact == nullptr
                                    ? package_id_for_path(input.package_roots, path)
                                    : source_fact->package_id;
        append_skeleton_symbol_facts(index,
                                     source_unit,
                                     package_id,
                                     module_name,
                                     uri_from_path(path),
                                     parse_result.source,
                                     *parse_result.program);
    }
}

} // namespace

void LspWorkspaceIndex::add_source_unit(SourceUnitFact fact) {
    source_units_.push_back(std::move(fact));
}

void LspWorkspaceIndex::add_symbol(SymbolFact fact) {
    symbols_.push_back(std::move(fact));
}

void LspWorkspaceIndex::add_reference(ReferenceFact fact) {
    references_.push_back(std::move(fact));
}

void LspWorkspaceIndex::add_impl(ImplFact fact) {
    impls_.push_back(std::move(fact));
}

std::optional<DefId> LspWorkspaceIndex::find_def(SymbolKind kind,
                                                 std::string_view canonical_name) const {
    for (const auto &symbol : symbols_) {
        if (symbol.kind == kind && symbol.canonical_name == canonical_name) {
            return symbol.def_id;
        }
    }
    return std::nullopt;
}

std::vector<const SymbolFact *> LspWorkspaceIndex::workspace_symbols(std::string_view query) const {
    std::vector<const SymbolFact *> matched;
    for (const auto &symbol : symbols_) {
        if (!query.empty() && symbol.local_name.find(query) == std::string::npos &&
            symbol.canonical_name.find(query) == std::string::npos) {
            continue;
        }
        matched.push_back(&symbol);
    }
    std::sort(matched.begin(), matched.end(), [](const SymbolFact *lhs, const SymbolFact *rhs) {
        if (lhs->source_unit_id.value != rhs->source_unit_id.value) {
            return lhs->source_unit_id.value < rhs->source_unit_id.value;
        }
        if (lhs->location.range.start.line != rhs->location.range.start.line) {
            return lhs->location.range.start.line < rhs->location.range.start.line;
        }
        return lhs->location.range.start.character < rhs->location.range.start.character;
    });
    return matched;
}

std::vector<Location> LspWorkspaceIndex::reference_locations_for_def(DefId def) const {
    std::vector<const ReferenceFact *> matched;
    for (const auto &reference : references_) {
        if (reference.completeness == FactCompleteness::Resolved && reference.target_def == def) {
            matched.push_back(&reference);
        }
    }

    std::sort(
        matched.begin(), matched.end(), [](const ReferenceFact *lhs, const ReferenceFact *rhs) {
            if (lhs->source_unit_id.value != rhs->source_unit_id.value) {
                return lhs->source_unit_id.value < rhs->source_unit_id.value;
            }
            if (lhs->location.range.start.line != rhs->location.range.start.line) {
                return lhs->location.range.start.line < rhs->location.range.start.line;
            }
            return lhs->location.range.start.character < rhs->location.range.start.character;
        });

    std::vector<Location> locations;
    locations.reserve(matched.size());
    for (const auto *reference : matched) {
        locations.push_back(reference->location);
    }
    return locations;
}

std::vector<Location>
LspWorkspaceIndex::implementation_locations_for_type(const TypeKey &type) const {
    std::vector<const ImplFact *> matched;
    for (const auto &impl : impls_) {
        if (impl.completeness != FactCompleteness::Typed || impl.target_type != type) {
            continue;
        }
        matched.push_back(&impl);
    }

    std::sort(matched.begin(), matched.end(), [](const ImplFact *lhs, const ImplFact *rhs) {
        if (lhs->source_unit_id.value != rhs->source_unit_id.value) {
            return lhs->source_unit_id.value < rhs->source_unit_id.value;
        }
        return lhs->source_order < rhs->source_order;
    });

    std::vector<Location> locations;
    locations.reserve(matched.size());
    for (const auto *impl : matched) {
        const auto duplicate =
            std::find_if(locations.begin(), locations.end(), [&](const Location &location) {
                return location.uri == impl->location.uri &&
                       location.range.start.line == impl->location.range.start.line &&
                       location.range.start.character == impl->location.range.start.character &&
                       location.range.end.line == impl->location.range.end.line &&
                       location.range.end.character == impl->location.range.end.character;
            });
        if (duplicate == locations.end()) {
            locations.push_back(impl->location);
        }
    }
    return locations;
}

std::vector<Location>
LspWorkspaceIndex::implementation_locations_for_primitive(PrimitiveKind kind) const {
    return implementation_locations_for_type(
        TypeKey{.kind = TypeKey::Kind::Primitive, .primitive = kind});
}

std::optional<PrimitiveKind> primitive_kind_from_spelling(std::string_view name) {
    if (name == "Unit") {
        return PrimitiveKind::Unit;
    }
    if (name == "Bool") {
        return PrimitiveKind::Bool;
    }
    if (name == "Int") {
        return PrimitiveKind::Int;
    }
    if (name == "Float") {
        return PrimitiveKind::Float;
    }
    if (name == "String") {
        return PrimitiveKind::String;
    }
    if (name == "UUID") {
        return PrimitiveKind::UUID;
    }
    if (name == "Timestamp") {
        return PrimitiveKind::Timestamp;
    }
    if (name == "Duration") {
        return PrimitiveKind::Duration;
    }
    if (name == "Decimal") {
        return PrimitiveKind::Decimal;
    }
    return std::nullopt;
}

std::optional<PrimitiveKind> primitive_kind_for_type(const Type &type) {
    if (type.holds<types::UnitT>()) {
        return PrimitiveKind::Unit;
    }
    if (type.holds<types::BoolT>()) {
        return PrimitiveKind::Bool;
    }
    if (type.holds<types::IntT>()) {
        return PrimitiveKind::Int;
    }
    if (type.holds<types::FloatT>()) {
        return PrimitiveKind::Float;
    }
    if (type.holds<types::StringT>()) {
        return PrimitiveKind::String;
    }
    if (type.holds<types::UUIDT>()) {
        return PrimitiveKind::UUID;
    }
    if (type.holds<types::TimestampT>()) {
        return PrimitiveKind::Timestamp;
    }
    if (type.holds<types::DurationT>()) {
        return PrimitiveKind::Duration;
    }
    if (type.holds<types::DecimalT>()) {
        return PrimitiveKind::Decimal;
    }
    return std::nullopt;
}

namespace {

TypeKey type_key_for_type_with_defs(const Type &type, const DefBySymbolMap *def_by_symbol) {
    if (type.holds<types::ErrorT>()) {
        return TypeKey{.kind = TypeKey::Kind::Error};
    }
    if (const auto *decimal = type.get_if<types::DecimalT>(); decimal != nullptr) {
        return TypeKey{
            .kind = TypeKey::Kind::Primitive,
            .primitive = PrimitiveKind::Decimal,
            .primitive_parameter = decimal->scale,
        };
    }
    if (const auto primitive = primitive_kind_for_type(type); primitive.has_value()) {
        return TypeKey{.kind = TypeKey::Kind::Primitive, .primitive = *primitive};
    }
    if (const auto *structure = type.get_if<types::StructT>();
        structure != nullptr && structure->symbol.has_value()) {
        if (def_by_symbol != nullptr) {
            if (const auto def = def_for_symbol(*def_by_symbol, *structure->symbol);
                def.has_value()) {
                return TypeKey{
                    .kind = TypeKey::Kind::Nominal,
                    .def = *def,
                    .type_args = type_keys_for_args(structure->type_args, def_by_symbol),
                };
            }
        }
        return TypeKey{.kind = TypeKey::Kind::Unknown};
    }
    if (const auto *enumeration = type.get_if<types::EnumT>();
        enumeration != nullptr && enumeration->symbol.has_value()) {
        if (def_by_symbol != nullptr) {
            if (const auto def = def_for_symbol(*def_by_symbol, *enumeration->symbol);
                def.has_value()) {
                return TypeKey{
                    .kind = TypeKey::Kind::Nominal,
                    .def = *def,
                    .type_args = type_keys_for_args(enumeration->type_args, def_by_symbol),
                };
            }
        }
        return TypeKey{.kind = TypeKey::Kind::Unknown};
    }
    return TypeKey{.kind = TypeKey::Kind::Unknown};
}

} // namespace

TypeKey type_key_for_type(const Type &type) {
    return type_key_for_type_with_defs(type, nullptr);
}

LspWorkspaceIndex build_lsp_workspace_index(const Frontend &frontend,
                                            LspWorkspaceIndexInput input) {
    LspWorkspaceIndex index;

    auto project = parse_project(frontend, input.project);
    if (project.has_errors()) {
        append_parse_skeleton_facts(index, frontend, input);
        return index;
    }

    SourceUnitByPathMap source_units_by_path;
    SourceUnitBySourceIdMap source_units_by_source_id;
    for (const auto &source : project.graph.sources) {
        const auto source_unit = register_source_unit(
            index, source_units_by_path, input, source.path, FactCompleteness::Resolved);
        source_units_by_source_id.emplace(source.id.value, source_unit);
    }

    Resolver resolver;
    auto resolved = resolver.resolve(project.graph);
    if (resolved.has_errors()) {
        for (const auto &source : project.graph.sources) {
            const auto source_unit = source_units_by_source_id.find(source.id.value);
            if (source_unit == source_units_by_source_id.end() || source.program == nullptr) {
                continue;
            }
            const auto *source_fact = source_unit_fact(index, source_unit->second);
            const auto package_id = source_fact == nullptr
                                        ? package_id_for_path(input.package_roots, source.path)
                                        : source_fact->package_id;
            append_skeleton_symbol_facts(index,
                                         source_unit->second,
                                         package_id,
                                         source.module_name,
                                         uri_from_path(source.path),
                                         source.source,
                                         *source.program);
        }
        return index;
    }

    DefBySymbolMap def_by_symbol;
    for (const auto &symbol : resolved.symbol_table.symbols()) {
        const auto location = symbol_location(project.graph, symbol);
        if (!location.has_value() || !symbol.source_id.has_value()) {
            continue;
        }
        const auto *source_unit = source_unit_for_id(project.graph, *symbol.source_id);
        if (source_unit == nullptr) {
            continue;
        }
        const auto source_unit_id = source_units_by_source_id.find(symbol.source_id->value);
        if (source_unit_id == source_units_by_source_id.end()) {
            continue;
        }
        const auto def_id = DefId{index.symbols().size()};
        def_by_symbol.emplace(symbol.id.value, def_id);
        const auto *source_fact = source_unit_fact(index, source_unit_id->second);
        const auto package_id = source_fact == nullptr
                                    ? package_id_for_path(input.package_roots, source_unit->path)
                                    : source_fact->package_id;
        const auto selection_range = symbol_navigation_range(source_unit->source, symbol);
        index.add_symbol(SymbolFact{
            .def_id = def_id,
            .package_id = package_id,
            .source_unit_id = source_unit_id->second,
            .kind = symbol.kind,
            .local_name = symbol.local_name,
            .canonical_name = symbol.canonical_name,
            .declaration_range = symbol.declaration_range,
            .selection_range = selection_range,
            .location = *location,
            .completeness = FactCompleteness::Resolved,
        });
    }

    for (const auto &reference : resolved.references()) {
        const auto location = reference_location(project.graph, reference);
        if (!location.has_value() || !reference.source_id.has_value()) {
            continue;
        }
        const auto *source_unit = source_unit_for_id(project.graph, *reference.source_id);
        if (source_unit == nullptr) {
            continue;
        }
        const auto source_unit_id = source_units_by_source_id.find(reference.source_id->value);
        if (source_unit_id == source_units_by_source_id.end()) {
            continue;
        }
        const auto *source_fact = source_unit_fact(index, source_unit_id->second);
        const auto target = def_by_symbol.find(reference.target.value);
        index.add_reference(ReferenceFact{
            .package_id = source_fact == nullptr
                              ? package_id_for_path(input.package_roots, source_unit->path)
                              : source_fact->package_id,
            .source_unit_id = source_unit_id->second,
            .target_def =
                target == def_by_symbol.end() ? std::nullopt : std::optional<DefId>{target->second},
            .reference_kind = reference.kind,
            .range = reference.range,
            .location = *location,
            .completeness = target == def_by_symbol.end() ? FactCompleteness::Parsed
                                                          : FactCompleteness::Resolved,
        });
    }

    TypeChecker type_checker;
    auto typed = type_checker.check(project.graph, resolved);
    if (typed.has_errors()) {
        return index;
    }

    for (const auto &[raw_index, impl] : typed.environment.impls()) {
        (void)raw_index;
        if (impl.target_type == nullptr || !impl.source_id.has_value()) {
            continue;
        }
        const auto location = impl_location(project.graph, impl);
        if (!location.has_value()) {
            continue;
        }
        const auto *source_unit = source_unit_for_id(project.graph, *impl.source_id);
        if (source_unit == nullptr) {
            continue;
        }
        const auto source_unit_id = source_units_by_source_id.find(impl.source_id->value);
        if (source_unit_id == source_units_by_source_id.end()) {
            continue;
        }
        const auto *source_fact = source_unit_fact(index, source_unit_id->second);
        index.add_impl(ImplFact{
            .impl_id = WorkspaceImplId{index.impls().size()},
            .package_id = source_fact == nullptr
                              ? package_id_for_path(input.package_roots, source_unit->path)
                              : source_fact->package_id,
            .source_unit_id = source_unit_id->second,
            .target_type = type_key_for_type_with_defs(*impl.target_type, &def_by_symbol),
            .trait_def = std::nullopt,
            .declaration_range = impl.declaration_range,
            .target_range = impl_location_range(impl),
            .location = *location,
            .source_order = impl.index,
            .completeness = FactCompleteness::Typed,
        });
    }

    return index;
}

} // namespace ahfl::lsp
