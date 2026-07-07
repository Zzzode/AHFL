#include "tooling/lsp/workspace_index.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "compiler/syntax/frontend/project.hpp"

namespace ahfl::lsp {

namespace {

void hash_combine(std::size_t &seed, std::size_t value) noexcept {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
}

void fingerprint_combine(std::uint64_t &seed, std::uint64_t value) noexcept {
    constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        seed ^= (value >> (index * 8U)) & 0xFFU;
        seed *= kFnvPrime;
    }
}

[[nodiscard]] DefFingerprint symbol_fingerprint(package_graph::PackageId package_id,
                                                SourceUnitId source_unit_id,
                                                SymbolNamespace name_space,
                                                SymbolKind kind,
                                                SourceRange declaration_range,
                                                SourceRange selection_range) noexcept {
    std::uint64_t seed = 14695981039346656037ULL;
    fingerprint_combine(seed, static_cast<std::uint64_t>(package_id.value));
    fingerprint_combine(seed, static_cast<std::uint64_t>(source_unit_id.value));
    fingerprint_combine(seed, static_cast<std::uint8_t>(name_space));
    fingerprint_combine(seed, static_cast<std::uint8_t>(kind));
    fingerprint_combine(seed, static_cast<std::uint64_t>(declaration_range.begin_offset));
    fingerprint_combine(seed, static_cast<std::uint64_t>(declaration_range.end_offset));
    fingerprint_combine(seed, static_cast<std::uint64_t>(selection_range.begin_offset));
    fingerprint_combine(seed, static_cast<std::uint64_t>(selection_range.end_offset));
    return DefFingerprint{.value = seed};
}

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
    const auto canonical = std::filesystem::weakly_canonical(candidate, error);
    if (!error) {
        candidate = canonical.lexically_normal();
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

struct SourceUnitOrderKey {
    std::optional<SourceUnitId> seeded_id;
    package_graph::PackageId package_id;
    std::string path_key;
};

[[nodiscard]] const LspIndexSourceUnitSeed *
source_unit_seed_for_path(const NavigationIndexScope &scope,
                          const std::filesystem::path &raw_path) {
    const auto path_key = normalize_path(raw_path).generic_string();
    for (const auto &seed : scope.source_units) {
        if (normalize_path(seed.path).generic_string() == path_key) {
            return &seed;
        }
    }
    return nullptr;
}

[[nodiscard]] SourceUnitOrderKey source_unit_order_key(const LspWorkspaceIndexInput &input,
                                                       const std::filesystem::path &raw_path) {
    const auto path = normalize_path(raw_path);
    const auto *seed = source_unit_seed_for_path(input.scope, path);
    return SourceUnitOrderKey{
        .seeded_id =
            seed == nullptr ? std::nullopt : std::optional<SourceUnitId>{seed->source_unit_id},
        .package_id = seed == nullptr ? package_id_for_path(input.scope.package_roots, path)
                                      : seed->package_id,
        .path_key = path.generic_string(),
    };
}

[[nodiscard]] bool source_unit_order_less(const SourceUnitOrderKey &lhs,
                                          const SourceUnitOrderKey &rhs) {
    if (lhs.seeded_id.has_value() != rhs.seeded_id.has_value()) {
        return lhs.seeded_id.has_value();
    }
    if (lhs.seeded_id.has_value() && lhs.seeded_id->value != rhs.seeded_id->value) {
        return lhs.seeded_id->value < rhs.seeded_id->value;
    }
    if (lhs.package_id.value != rhs.package_id.value) {
        return lhs.package_id.value < rhs.package_id.value;
    }
    return lhs.path_key < rhs.path_key;
}

[[nodiscard]] std::vector<LspNavigationIndexSourceKind>
canonical_scope_kinds(std::vector<LspNavigationIndexSourceKind> kinds) {
    std::sort(kinds.begin(), kinds.end(), [](auto lhs, auto rhs) {
        return static_cast<std::uint8_t>(lhs) < static_cast<std::uint8_t>(rhs);
    });
    kinds.erase(std::unique(kinds.begin(), kinds.end()), kinds.end());
    return kinds;
}

[[nodiscard]] std::vector<std::filesystem::path>
ordered_entry_files_for_index(const LspWorkspaceIndexInput &input) {
    std::vector<std::filesystem::path> paths;
    paths.reserve(input.scope.source_units.size() + input.project.entry_files.size());
    for (const auto &seed : input.scope.source_units) {
        paths.push_back(normalize_path(seed.path));
    }
    for (const auto &entry_file : input.project.entry_files) {
        paths.push_back(normalize_path(entry_file));
    }
    std::sort(paths.begin(), paths.end(), [&](const auto &lhs, const auto &rhs) {
        return source_unit_order_less(source_unit_order_key(input, lhs),
                                      source_unit_order_key(input, rhs));
    });
    paths.erase(std::unique(paths.begin(),
                            paths.end(),
                            [](const auto &lhs, const auto &rhs) {
                                return normalize_path(lhs).generic_string() ==
                                       normalize_path(rhs).generic_string();
                            }),
                paths.end());
    return paths;
}

[[nodiscard]] std::vector<const SourceUnit *>
ordered_source_units_for_index(const LspWorkspaceIndexInput &input, const SourceGraph &graph) {
    std::vector<const SourceUnit *> sources;
    sources.reserve(graph.sources.size());
    for (const auto &source : graph.sources) {
        sources.push_back(&source);
    }
    std::sort(sources.begin(), sources.end(), [&](const SourceUnit *lhs, const SourceUnit *rhs) {
        return source_unit_order_less(source_unit_order_key(input, lhs->path),
                                      source_unit_order_key(input, rhs->path));
    });
    return sources;
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

[[nodiscard]] std::vector<ImplMethodFact> method_facts_for_impl(const ImplTypeInfo &impl) {
    std::vector<ImplMethodFact> methods;
    methods.reserve(impl.methods.size());
    for (std::size_t index = 0; index < impl.methods.size(); ++index) {
        const auto &method = impl.methods[index];
        methods.push_back(ImplMethodFact{
            .name = method.name,
            .declaration_range = method.declaration_range,
            .has_body = method.has_body,
            .builtin_name = method.builtin_name,
            .source_order = index,
        });
    }
    return methods;
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

[[nodiscard]] SourceRange alias_navigation_range(const SourceFile &source,
                                                 const PublicAlias &alias) {
    if (alias.local_name.empty()) {
        return alias.declaration_range;
    }

    const auto begin = std::min(alias.declaration_range.begin_offset, source.content.size());
    const auto end =
        std::max(begin, std::min(alias.declaration_range.end_offset, source.content.size()));
    std::size_t cursor = begin;
    while (cursor < end) {
        const auto found = source.content.find(alias.local_name, cursor);
        if (found == std::string::npos || found + alias.local_name.size() > end) {
            break;
        }

        const auto before_ok = found == 0 || !is_identifier_char(source.content[found - 1]);
        const auto after = found + alias.local_name.size();
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

    return alias.declaration_range;
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

[[nodiscard]] std::optional<Location> impl_trait_location(const SourceGraph &graph,
                                                          const ImplTypeInfo &impl) {
    if (!impl.source_id.has_value() || !has_extent(impl.trait_ref_range)) {
        return std::nullopt;
    }

    const auto *source = source_unit_for_id(graph, *impl.source_id);
    if (source == nullptr) {
        return std::nullopt;
    }

    return Location{
        .uri = uri_from_path(source->path),
        .range = to_lsp_range(source->source, impl.trait_ref_range),
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
using SourceUnitByDiagnosticNameMap = std::unordered_map<std::string, SourceUnitId>;
using SourceUnitBySourceIdMap = std::unordered_map<std::size_t, SourceUnitId>;
using SourceUnitByPathMap = std::unordered_map<std::string, SourceUnitId>;
using SourceUnitPathSet = std::unordered_set<std::string>;

[[nodiscard]] TypeKey type_key_for_type_with_defs(const Type &type,
                                                  const DefBySymbolMap *def_by_symbol);

struct SymbolFactCandidate {
    const Symbol *symbol{nullptr};
    SourceUnitId source_unit_id;
    package_graph::PackageId package_id;
    Location location;
    SourceRange selection_range;
};

[[nodiscard]] bool symbol_fact_candidate_less(const SymbolFactCandidate &lhs,
                                              const SymbolFactCandidate &rhs) {
    if (lhs.package_id.value != rhs.package_id.value) {
        return lhs.package_id.value < rhs.package_id.value;
    }
    if (lhs.source_unit_id.value != rhs.source_unit_id.value) {
        return lhs.source_unit_id.value < rhs.source_unit_id.value;
    }
    const auto lhs_namespace = static_cast<std::uint8_t>(lhs.symbol->name_space);
    const auto rhs_namespace = static_cast<std::uint8_t>(rhs.symbol->name_space);
    if (lhs_namespace != rhs_namespace) {
        return lhs_namespace < rhs_namespace;
    }
    if (lhs.symbol->declaration_range.begin_offset != rhs.symbol->declaration_range.begin_offset) {
        return lhs.symbol->declaration_range.begin_offset <
               rhs.symbol->declaration_range.begin_offset;
    }
    if (lhs.symbol->declaration_range.end_offset != rhs.symbol->declaration_range.end_offset) {
        return lhs.symbol->declaration_range.end_offset < rhs.symbol->declaration_range.end_offset;
    }
    const auto lhs_kind = static_cast<std::uint8_t>(lhs.symbol->kind);
    const auto rhs_kind = static_cast<std::uint8_t>(rhs.symbol->kind);
    if (lhs_kind != rhs_kind) {
        return lhs_kind < rhs_kind;
    }
    return lhs.symbol->canonical_name < rhs.symbol->canonical_name;
}

struct ImplFactCandidate {
    package_graph::PackageId package_id;
    SourceUnitId source_unit_id;
    TypeKey target_type;
    std::optional<DefId> trait_def;
    std::optional<SourceRange> trait_range;
    SourceRange declaration_range;
    SourceRange target_range;
    Location location;
    std::optional<Location> trait_location;
    std::vector<ImplMethodFact> methods;
    std::size_t source_order{0};
};

[[nodiscard]] bool impl_fact_candidate_less(const ImplFactCandidate &lhs,
                                            const ImplFactCandidate &rhs) {
    if (lhs.package_id.value != rhs.package_id.value) {
        return lhs.package_id.value < rhs.package_id.value;
    }
    if (lhs.source_unit_id.value != rhs.source_unit_id.value) {
        return lhs.source_unit_id.value < rhs.source_unit_id.value;
    }
    if (lhs.source_order != rhs.source_order) {
        return lhs.source_order < rhs.source_order;
    }
    if (lhs.declaration_range.begin_offset != rhs.declaration_range.begin_offset) {
        return lhs.declaration_range.begin_offset < rhs.declaration_range.begin_offset;
    }
    if (lhs.declaration_range.end_offset != rhs.declaration_range.end_offset) {
        return lhs.declaration_range.end_offset < rhs.declaration_range.end_offset;
    }
    return lhs.location.uri < rhs.location.uri;
}

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
    return index.source_unit_for_id(id);
}

[[nodiscard]] package_graph::PackageId invalid_package_id() {
    return package_graph::PackageId{std::numeric_limits<std::size_t>::max()};
}

void add_diagnostic_source_alias(SourceUnitByDiagnosticNameMap &source_units_by_name,
                                 std::string_view name,
                                 SourceUnitId source_unit) {
    if (!name.empty()) {
        source_units_by_name.try_emplace(std::string(name), source_unit);
    }
}

void add_diagnostic_source_aliases(SourceUnitByDiagnosticNameMap &source_units_by_name,
                                   const SourceUnitFact &fact,
                                   const SourceFile *source = nullptr) {
    add_diagnostic_source_alias(source_units_by_name, fact.uri, fact.source_unit_id);
    add_diagnostic_source_alias(
        source_units_by_name, fact.path.generic_string(), fact.source_unit_id);
    add_diagnostic_source_alias(source_units_by_name, fact.path.string(), fact.source_unit_id);
    add_diagnostic_source_alias(source_units_by_name, display_path(fact.path), fact.source_unit_id);
    if (source != nullptr) {
        add_diagnostic_source_alias(
            source_units_by_name, source->display_name, fact.source_unit_id);
    }
}

[[nodiscard]] std::optional<SourceUnitId>
source_unit_for_diagnostic(const SourceUnitByDiagnosticNameMap &source_units_by_name,
                           const Diagnostic &diagnostic) {
    if (!diagnostic.source_name.has_value()) {
        return std::nullopt;
    }
    const auto found = source_units_by_name.find(*diagnostic.source_name);
    if (found == source_units_by_name.end()) {
        return std::nullopt;
    }
    return found->second;
}

[[nodiscard]] std::string fallback_code_for_phase(IndexDiagnosticPhase phase) {
    switch (phase) {
    case IndexDiagnosticPhase::Parse:
        return "parse.diagnostic";
    case IndexDiagnosticPhase::Resolve:
        return "resolve.diagnostic";
    case IndexDiagnosticPhase::TypeCheck:
        return "typecheck.diagnostic";
    }
    return "index.diagnostic";
}

[[nodiscard]] FactCompleteness diagnostic_completeness(const LspWorkspaceIndex &index,
                                                       SourceUnitId source_unit,
                                                       std::optional<FactCompleteness> fallback) {
    if (fallback.has_value()) {
        return *fallback;
    }
    const auto *fact = source_unit_fact(index, source_unit);
    return fact == nullptr ? FactCompleteness::Invalid : fact->completeness;
}

void append_index_diagnostic(LspWorkspaceIndex &index,
                             SourceUnitId source_unit,
                             const Diagnostic &diagnostic,
                             IndexDiagnosticPhase phase,
                             std::optional<FactCompleteness> completeness) {
    const auto *fact = source_unit_fact(index, source_unit);
    index.add_diagnostic(IndexDiagnosticFact{
        .diagnostic_id = IndexDiagnosticFactId{index.diagnostics().size()},
        .package_id = fact == nullptr ? invalid_package_id() : fact->package_id,
        .source_unit_id = source_unit,
        .phase = phase,
        .severity = diagnostic.severity,
        .code = diagnostic.code.value_or(fallback_code_for_phase(phase)),
        .message = diagnostic.message,
        .range = diagnostic.range.value_or(SourceRange{}),
        .completeness = diagnostic_completeness(index, source_unit, completeness),
    });
}

void append_index_diagnostics_by_source_name(
    LspWorkspaceIndex &index,
    const SourceUnitByDiagnosticNameMap &source_units_by_name,
    const DiagnosticBag &diagnostics,
    IndexDiagnosticPhase phase,
    std::optional<FactCompleteness> completeness = std::nullopt) {
    for (const auto &diagnostic : diagnostics.entries()) {
        const auto source_unit = source_unit_for_diagnostic(source_units_by_name, diagnostic);
        if (!source_unit.has_value()) {
            continue;
        }
        append_index_diagnostic(index, *source_unit, diagnostic, phase, completeness);
    }
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

    const auto *seed = source_unit_seed_for_path(input.scope, path);
    const auto source_unit =
        seed == nullptr ? SourceUnitId{index.source_units().size()} : seed->source_unit_id;
    const auto package_id =
        seed == nullptr ? package_id_for_path(input.scope.package_roots, path) : seed->package_id;
    const auto scope_kinds = seed == nullptr ? std::vector<LspNavigationIndexSourceKind>{}
                                             : canonical_scope_kinds(seed->scope_kinds);
    index.add_source_unit(SourceUnitFact{
        .source_unit_id = source_unit,
        .package_id = package_id,
        .path = path,
        .uri = uri_from_path(path),
        .revision = input.metadata.revision,
        .scope_kinds = scope_kinds,
        .completeness = completeness,
        .valid = true,
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

[[nodiscard]] SymbolNamespace symbol_namespace_for_kind(SymbolKind kind) noexcept {
    switch (kind) {
    case SymbolKind::Struct:
    case SymbolKind::Enum:
    case SymbolKind::TypeAlias:
        return SymbolNamespace::Types;
    case SymbolKind::Const:
        return SymbolNamespace::Consts;
    case SymbolKind::Capability:
        return SymbolNamespace::Capabilities;
    case SymbolKind::Predicate:
        return SymbolNamespace::Predicates;
    case SymbolKind::Agent:
        return SymbolNamespace::Agents;
    case SymbolKind::Workflow:
        return SymbolNamespace::Workflows;
    case SymbolKind::Function:
        return SymbolNamespace::Functions;
    case SymbolKind::Trait:
        return SymbolNamespace::Traits;
    }
    return SymbolNamespace::Types;
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
            .fingerprint = symbol_fingerprint(package_id,
                                              source_unit,
                                              symbol_namespace_for_kind(skeleton->first),
                                              skeleton->first,
                                              declaration->range,
                                              selection_range),
            .package_id = package_id,
            .source_unit_id = source_unit,
            .kind = skeleton->first,
            .name_space = symbol_namespace_for_kind(skeleton->first),
            .visibility = declaration->visibility,
            .api_reachable = false,
            .artifact_reachable = false,
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

[[nodiscard]] SourceRange skeleton_impl_target_range(const ast::ImplDecl &impl) {
    if (impl.target_type != nullptr && has_extent(impl.target_type->range)) {
        return impl.target_type->range;
    }
    return impl.range;
}

[[nodiscard]] TypeKey primitive_type_key_for_syntax(const ast::TypeSyntax *type) {
    if (type == nullptr) {
        return TypeKey{.kind = TypeKey::Kind::Unknown};
    }
    return std::visit(
        [](const auto &node) -> TypeKey {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, ast::UnitType>) {
                return TypeKey{.kind = TypeKey::Kind::Primitive, .primitive = PrimitiveKind::Unit};
            } else if constexpr (std::is_same_v<Node, ast::BoolType>) {
                return TypeKey{.kind = TypeKey::Kind::Primitive, .primitive = PrimitiveKind::Bool};
            } else if constexpr (std::is_same_v<Node, ast::IntType>) {
                return TypeKey{.kind = TypeKey::Kind::Primitive, .primitive = PrimitiveKind::Int};
            } else if constexpr (std::is_same_v<Node, ast::FloatType>) {
                return TypeKey{.kind = TypeKey::Kind::Primitive, .primitive = PrimitiveKind::Float};
            } else if constexpr (std::is_same_v<Node, ast::StringType> ||
                                 std::is_same_v<Node, ast::BoundedStringType>) {
                return TypeKey{.kind = TypeKey::Kind::Primitive,
                               .primitive = PrimitiveKind::String};
            } else if constexpr (std::is_same_v<Node, ast::UuidType>) {
                return TypeKey{.kind = TypeKey::Kind::Primitive, .primitive = PrimitiveKind::UUID};
            } else if constexpr (std::is_same_v<Node, ast::TimestampType>) {
                return TypeKey{.kind = TypeKey::Kind::Primitive,
                               .primitive = PrimitiveKind::Timestamp};
            } else if constexpr (std::is_same_v<Node, ast::DurationType>) {
                return TypeKey{.kind = TypeKey::Kind::Primitive,
                               .primitive = PrimitiveKind::Duration};
            } else if constexpr (std::is_same_v<Node, ast::DecimalType>) {
                return TypeKey{.kind = TypeKey::Kind::Primitive,
                               .primitive = PrimitiveKind::Decimal,
                               .primitive_parameter = node.scale};
            } else {
                return TypeKey{.kind = TypeKey::Kind::Unknown};
            }
        },
        type->node);
}

[[nodiscard]] std::vector<ImplMethodFact>
skeleton_method_facts_for_impl(const ast::ImplDecl &impl) {
    std::vector<ImplMethodFact> methods;
    methods.reserve(impl.methods.size());
    for (const auto &method : impl.methods) {
        if (method == nullptr) {
            continue;
        }
        methods.push_back(ImplMethodFact{
            .name = method->name,
            .declaration_range = method->range,
            .has_body = method->body != nullptr,
            .builtin_name = method->builtin_name,
            .source_order = methods.size(),
        });
    }
    return methods;
}

void append_skeleton_impl_facts(LspWorkspaceIndex &index,
                                SourceUnitId source_unit,
                                package_graph::PackageId package_id,
                                std::string_view uri,
                                const SourceFile &source,
                                const ast::Program &program,
                                FactCompleteness completeness = FactCompleteness::Parsed) {
    std::size_t source_order = 0;
    for (const auto &declaration : program.declarations) {
        if (declaration == nullptr || declaration->kind != ast::NodeKind::ImplDecl) {
            continue;
        }
        const auto &impl = static_cast<const ast::ImplDecl &>(*declaration);
        const auto target_range = skeleton_impl_target_range(impl);
        const auto trait_range = impl.trait_ref != nullptr && has_extent(impl.trait_ref->range)
                                     ? std::optional<SourceRange>{impl.trait_ref->range}
                                     : std::nullopt;
        index.add_impl(ImplFact{
            .impl_id = WorkspaceImplId{index.impls().size()},
            .package_id = package_id,
            .source_unit_id = source_unit,
            .target_type = primitive_type_key_for_syntax(impl.target_type.get()),
            .trait_def = std::nullopt,
            .trait_range = trait_range,
            .declaration_range = impl.range,
            .target_range = target_range,
            .location =
                Location{
                    .uri = std::string(uri),
                    .range = to_lsp_range(source, target_range),
                },
            .trait_location = trait_range.has_value()
                                  ? std::optional<Location>{Location{
                                        .uri = std::string(uri),
                                        .range = to_lsp_range(source, *trait_range),
                                    }}
                                  : std::nullopt,
            .methods = skeleton_method_facts_for_impl(impl),
            .source_order = source_order,
            .completeness = completeness,
        });
        ++source_order;
    }
}

void append_parse_skeleton_facts_into(LspWorkspaceIndex &index,
                                      const Frontend &frontend,
                                      const LspWorkspaceIndexInput &input,
                                      SourceUnitByPathMap &source_units_by_path,
                                      SourceUnitByDiagnosticNameMap &source_units_by_name,
                                      const DiagnosticBag *project_diagnostics,
                                      const SourceUnitPathSet *skip_paths) {
    for (const auto &entry_file : ordered_entry_files_for_index(input)) {
        const auto path = normalize_path(entry_file);
        const auto path_key = path.generic_string();
        if (skip_paths != nullptr && skip_paths->contains(path_key)) {
            continue;
        }
        auto parse_result = [&]() {
            if (const auto overlay = input.project.source_overlays.find(path_key);
                overlay != input.project.source_overlays.end()) {
                return frontend.parse_text(path_key, overlay->second);
            }
            if (const auto cached = input.project.source_cache.find(path_key);
                cached != input.project.source_cache.end()) {
                return frontend.parse_text(path_key, cached->second);
            }
            return frontend.parse_file(path);
        }();
        const auto source_unit = register_source_unit(
            index,
            source_units_by_path,
            input,
            path,
            parse_result.program ? FactCompleteness::Parsed : FactCompleteness::Invalid);
        if (const auto *source_fact = source_unit_fact(index, source_unit);
            source_fact != nullptr) {
            add_diagnostic_source_aliases(source_units_by_name, *source_fact, &parse_result.source);
        }
        if (!parse_result.program) {
            continue;
        }

        const auto module_name = module_name_for_program(*parse_result.program);
        const auto *source_fact = source_unit_fact(index, source_unit);
        const auto package_id = source_fact == nullptr
                                    ? package_id_for_path(input.scope.package_roots, path)
                                    : source_fact->package_id;
        append_skeleton_symbol_facts(index,
                                     source_unit,
                                     package_id,
                                     module_name,
                                     uri_from_path(path),
                                     parse_result.source,
                                     *parse_result.program);
        append_skeleton_impl_facts(index,
                                   source_unit,
                                   package_id,
                                   uri_from_path(path),
                                   parse_result.source,
                                   *parse_result.program);
    }
    if (project_diagnostics != nullptr) {
        append_index_diagnostics_by_source_name(
            index, source_units_by_name, *project_diagnostics, IndexDiagnosticPhase::Parse);
    }
}

void append_parse_skeleton_facts(LspWorkspaceIndex &index,
                                 const Frontend &frontend,
                                 const LspWorkspaceIndexInput &input,
                                 const DiagnosticBag *project_diagnostics = nullptr) {
    SourceUnitByPathMap source_units_by_path;
    SourceUnitByDiagnosticNameMap source_units_by_name;
    append_parse_skeleton_facts_into(index,
                                     frontend,
                                     input,
                                     source_units_by_path,
                                     source_units_by_name,
                                     project_diagnostics,
                                     nullptr);
}

} // namespace

std::size_t TypeKeyHash::operator()(const TypeKey &key) const noexcept {
    std::size_t seed = std::hash<std::uint8_t>{}(static_cast<std::uint8_t>(key.kind));
    if (key.primitive.has_value()) {
        hash_combine(seed, std::hash<std::uint8_t>{}(static_cast<std::uint8_t>(*key.primitive)));
    }
    hash_combine(seed, std::hash<std::int64_t>{}(key.primitive_parameter));
    if (key.def.has_value()) {
        hash_combine(seed, std::hash<std::size_t>{}(key.def->value));
    }
    for (const auto &arg : key.type_args) {
        hash_combine(seed, TypeKeyHash{}(arg));
    }
    return seed;
}

void LspWorkspaceIndex::set_metadata(NavigationIndexMetadata metadata) {
    metadata_ = std::move(metadata);
}

void LspWorkspaceIndex::add_source_unit(SourceUnitFact fact) {
    fact.valid = true;
    const auto id = fact.source_unit_id;
    if (fact.package_id.value != std::numeric_limits<std::size_t>::max()) {
        if (fact.package_id.value >= source_units_by_package_.size()) {
            source_units_by_package_.resize(fact.package_id.value + 1);
        }
        auto &package_sources = source_units_by_package_[fact.package_id.value];
        if (std::find(package_sources.begin(), package_sources.end(), fact.source_unit_id) ==
            package_sources.end()) {
            package_sources.push_back(fact.source_unit_id);
            std::sort(package_sources.begin(),
                      package_sources.end(),
                      [](SourceUnitId lhs, SourceUnitId rhs) { return lhs.value < rhs.value; });
        }
    }
    if (const auto existing = source_unit_index_by_id_.find(id.value);
        existing != source_unit_index_by_id_.end()) {
        source_units_[existing->second] = std::move(fact);
        return;
    }
    source_unit_index_by_id_.emplace(id.value, source_units_.size());
    source_units_.push_back(std::move(fact));
}

void LspWorkspaceIndex::set_source_unit_completeness(SourceUnitId source_unit,
                                                     FactCompleteness completeness) {
    const auto existing = source_unit_index_by_id_.find(source_unit.value);
    if (existing == source_unit_index_by_id_.end()) {
        return;
    }
    auto &fact = source_units_[existing->second];
    if (!fact.valid) {
        return;
    }
    fact.completeness = completeness;
}

void LspWorkspaceIndex::add_symbol(SymbolFact fact) {
    fact.def_id = DefId{symbols_.size()};
    symbols_.push_back(std::move(fact));
}

void LspWorkspaceIndex::add_reference(ReferenceFact fact) {
    const auto id = ReferenceFactId{references_.size()};
    if (fact.target_def.has_value()) {
        if (fact.target_def->value >= references_by_def_.size()) {
            references_by_def_.resize(fact.target_def->value + 1);
        }
        references_by_def_[fact.target_def->value].push_back(id);
    }
    references_.push_back(std::move(fact));
}

void LspWorkspaceIndex::add_impl(ImplFact fact) {
    const auto id = WorkspaceImplId{impls_.size()};
    fact.impl_id = id;
    if (fact.completeness == FactCompleteness::Typed) {
        impls_by_type_[fact.target_type].push_back(id);
        if (fact.target_type.kind == TypeKey::Kind::Nominal && fact.target_type.def.has_value()) {
            if (fact.target_type.def->value >= impls_by_nominal_def_.size()) {
                impls_by_nominal_def_.resize(fact.target_type.def->value + 1);
            }
            impls_by_nominal_def_[fact.target_type.def->value].push_back(id);
        }
        if (fact.trait_def.has_value()) {
            if (fact.trait_def->value >= impls_by_trait_.size()) {
                impls_by_trait_.resize(fact.trait_def->value + 1);
            }
            impls_by_trait_[fact.trait_def->value].push_back(id);
        }
    }
    impls_.push_back(std::move(fact));
}

void LspWorkspaceIndex::add_diagnostic(IndexDiagnosticFact fact) {
    const auto id = IndexDiagnosticFactId{diagnostics_.size()};
    fact.diagnostic_id = id;
    diagnostics_by_source_[fact.source_unit_id.value].push_back(id);
    diagnostics_.push_back(std::move(fact));
}

const SourceUnitFact *LspWorkspaceIndex::source_unit_for_id(SourceUnitId source_unit) const {
    const auto existing = source_unit_index_by_id_.find(source_unit.value);
    if (existing == source_unit_index_by_id_.end()) {
        return nullptr;
    }
    const auto &fact = source_units_[existing->second];
    return fact.valid ? &fact : nullptr;
}

std::vector<SourceUnitId>
LspWorkspaceIndex::source_units_for_package(package_graph::PackageId package_id) const {
    if (package_id.value >= source_units_by_package_.size()) {
        return {};
    }
    return source_units_by_package_[package_id.value];
}

std::vector<const IndexDiagnosticFact *>
LspWorkspaceIndex::diagnostics_for_source(SourceUnitId source_unit) const {
    const auto source_diagnostics = diagnostics_by_source_.find(source_unit.value);
    if (source_diagnostics == diagnostics_by_source_.end()) {
        return {};
    }

    std::vector<const IndexDiagnosticFact *> matched;
    matched.reserve(source_diagnostics->second.size());
    for (const auto id : source_diagnostics->second) {
        if (id.value < diagnostics_.size()) {
            matched.push_back(&diagnostics_[id.value]);
        }
    }
    return matched;
}

const SymbolFact *LspWorkspaceIndex::symbol_for_def(DefId def) const {
    if (def.value >= symbols_.size()) {
        return nullptr;
    }
    const auto &symbol = symbols_[def.value];
    return symbol.def_id == def ? &symbol : nullptr;
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
        if (lhs->package_id.value != rhs->package_id.value) {
            return lhs->package_id.value < rhs->package_id.value;
        }
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
    if (def.value < references_by_def_.size()) {
        for (const auto id : references_by_def_[def.value]) {
            if (id.value >= references_.size()) {
                continue;
            }
            const auto &reference = references_[id.value];
            if (reference.completeness == FactCompleteness::Resolved) {
                matched.push_back(&reference);
            }
        }
    }

    std::sort(
        matched.begin(), matched.end(), [](const ReferenceFact *lhs, const ReferenceFact *rhs) {
            if (lhs->package_id.value != rhs->package_id.value) {
                return lhs->package_id.value < rhs->package_id.value;
            }
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

[[nodiscard]] std::vector<Location>
sorted_unique_impl_locations(std::vector<const ImplFact *> matched) {
    std::sort(matched.begin(), matched.end(), [](const ImplFact *lhs, const ImplFact *rhs) {
        if (lhs->package_id.value != rhs->package_id.value) {
            return lhs->package_id.value < rhs->package_id.value;
        }
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
LspWorkspaceIndex::implementation_locations_for_type(const TypeKey &type) const {
    std::vector<const ImplFact *> matched;
    if (const auto found = impls_by_type_.find(type); found != impls_by_type_.end()) {
        for (const auto id : found->second) {
            if (id.value >= impls_.size()) {
                continue;
            }
            const auto &impl = impls_[id.value];
            if (impl.completeness == FactCompleteness::Typed) {
                matched.push_back(&impl);
            }
        }
    }
    if (type.kind == TypeKey::Kind::Nominal && type.def.has_value() &&
        type.def->value < impls_by_nominal_def_.size()) {
        for (const auto id : impls_by_nominal_def_[type.def->value]) {
            if (id.value >= impls_.size()) {
                continue;
            }
            const auto &impl = impls_[id.value];
            if (impl.completeness == FactCompleteness::Typed) {
                matched.push_back(&impl);
            }
        }
    }

    return sorted_unique_impl_locations(std::move(matched));
}

std::vector<Location> LspWorkspaceIndex::implementation_locations_for_nominal_def(DefId def) const {
    return implementation_locations_for_type(TypeKey{
        .kind = TypeKey::Kind::Nominal,
        .def = def,
    });
}

std::vector<Location> LspWorkspaceIndex::implementation_locations_for_trait(DefId trait) const {
    std::vector<const ImplFact *> matched;
    if (trait.value < impls_by_trait_.size()) {
        for (const auto id : impls_by_trait_[trait.value]) {
            if (id.value >= impls_.size()) {
                continue;
            }
            const auto &impl = impls_[id.value];
            if (impl.completeness == FactCompleteness::Typed) {
                matched.push_back(&impl);
            }
        }
    }

    std::sort(matched.begin(), matched.end(), [](const ImplFact *lhs, const ImplFact *rhs) {
        if (lhs->package_id.value != rhs->package_id.value) {
            return lhs->package_id.value < rhs->package_id.value;
        }
        if (lhs->source_unit_id.value != rhs->source_unit_id.value) {
            return lhs->source_unit_id.value < rhs->source_unit_id.value;
        }
        return lhs->source_order < rhs->source_order;
    });

    std::vector<Location> locations;
    locations.reserve(matched.size());
    for (const auto *impl : matched) {
        const auto &candidate =
            impl->trait_location.has_value() ? *impl->trait_location : impl->location;
        const auto duplicate =
            std::find_if(locations.begin(), locations.end(), [&](const Location &location) {
                return location.uri == candidate.uri &&
                       location.range.start.line == candidate.range.start.line &&
                       location.range.start.character == candidate.range.start.character &&
                       location.range.end.line == candidate.range.end.line &&
                       location.range.end.character == candidate.range.end.character;
            });
        if (duplicate == locations.end()) {
            locations.push_back(candidate);
        }
    }
    return locations;
}

std::vector<Location>
LspWorkspaceIndex::implementation_locations_for_primitive(PrimitiveKind kind) const {
    return implementation_locations_for_type(
        TypeKey{.kind = TypeKey::Kind::Primitive, .primitive = kind});
}

std::optional<Location>
LspWorkspaceIndex::primitive_home_location_for_type(const TypeKey &type) const {
    if (type.kind != TypeKey::Kind::Primitive) {
        return std::nullopt;
    }

    std::vector<const ImplFact *> matched;
    for (const auto &impl : impls_) {
        if (impl.target_type != type || impl.completeness == FactCompleteness::Invalid) {
            continue;
        }
        matched.push_back(&impl);
    }

    if (matched.empty()) {
        return std::nullopt;
    }

    const auto locations = sorted_unique_impl_locations(std::move(matched));
    return locations.empty() ? std::nullopt : std::optional<Location>{locations.front()};
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

class IndexAnalysisPipeline {
  public:
    IndexAnalysisPipeline(const Frontend &frontend, LspWorkspaceIndexInput input)
        : frontend_(frontend), input_(std::move(input)) {}

    [[nodiscard]] LspWorkspaceIndex run() {
        index_.set_metadata(input_.metadata);
        input_.project.entry_files = ordered_entry_files_for_index(input_);

        auto project = parse_project(frontend_, input_.project);
        if (project.has_errors() && project.graph.sources.empty()) {
            append_parse_skeleton_facts(index_, frontend_, input_, &project.diagnostics);
            return std::move(index_);
        }

        register_parsed_sources(project.graph);
        if (project.has_errors()) {
            const auto parsed_paths = parsed_source_paths(project.graph);
            append_parse_skeleton_facts_into(index_,
                                             frontend_,
                                             input_,
                                             source_units_by_path_,
                                             source_units_by_name_,
                                             nullptr,
                                             &parsed_paths);
        }
        append_index_diagnostics_by_source_name(
            index_, source_units_by_name_, project.diagnostics, IndexDiagnosticPhase::Parse);

        Resolver resolver;
        auto resolved = resolver.resolve(project.graph);
        append_index_diagnostics_by_source_name(index_,
                                                source_units_by_name_,
                                                resolved.diagnostics,
                                                IndexDiagnosticPhase::Resolve,
                                                resolved.has_errors() ? FactCompleteness::Parsed
                                                                      : FactCompleteness::Resolved);
        if (resolved.has_errors()) {
            append_resolved_skeleton_facts(project.graph, FactCompleteness::Parsed);
            return std::move(index_);
        }

        update_source_completeness(project.graph, FactCompleteness::Resolved);
        emit_resolved_symbol_facts(project.graph, resolved);
        emit_resolved_alias_facts(project.graph, resolved);
        emit_resolved_reference_facts(project.graph, resolved);

        TypeChecker type_checker;
        auto typed = type_checker.check(project.graph, resolved);
        append_index_diagnostics_by_source_name(index_,
                                                source_units_by_name_,
                                                typed.diagnostics,
                                                IndexDiagnosticPhase::TypeCheck,
                                                typed.has_errors() ? FactCompleteness::Resolved
                                                                   : FactCompleteness::Typed);
        if (typed.has_errors()) {
            emit_typed_impl_facts(project.graph, typed);
            append_impl_skeleton_facts(project.graph, FactCompleteness::Resolved);
            return std::move(index_);
        }

        update_source_completeness(project.graph, FactCompleteness::Typed);
        emit_typed_impl_facts(project.graph, typed);
        return std::move(index_);
    }

  private:
    [[nodiscard]] SourceUnitPathSet parsed_source_paths(const SourceGraph &graph) const {
        SourceUnitPathSet paths;
        paths.reserve(graph.sources.size());
        for (const auto &source : graph.sources) {
            paths.insert(normalize_path(source.path).generic_string());
        }
        return paths;
    }

    void register_parsed_sources(const SourceGraph &graph) {
        for (const auto *source : ordered_source_units_for_index(input_, graph)) {
            const auto source_unit = register_source_unit(
                index_, source_units_by_path_, input_, source->path, FactCompleteness::Parsed);
            source_units_by_source_id_.emplace(source->id.value, source_unit);
            if (const auto *source_fact = source_unit_fact(index_, source_unit);
                source_fact != nullptr) {
                add_diagnostic_source_aliases(source_units_by_name_, *source_fact, &source->source);
            }
        }
    }

    void update_source_completeness(const SourceGraph &graph, FactCompleteness completeness) {
        for (const auto &source : graph.sources) {
            const auto source_unit = index_source_unit_for_source(source);
            if (source_unit.has_value()) {
                index_.set_source_unit_completeness(*source_unit, completeness);
            }
        }
    }

    [[nodiscard]] std::optional<SourceUnitId>
    index_source_unit_for_source(const SourceUnit &source) const {
        const auto found = source_units_by_source_id_.find(source.id.value);
        if (found == source_units_by_source_id_.end()) {
            return std::nullopt;
        }
        return found->second;
    }

    [[nodiscard]] package_graph::PackageId
    package_id_for_index_source(const SourceUnit &source, SourceUnitId source_unit) const {
        const auto *source_fact = source_unit_fact(index_, source_unit);
        return source_fact == nullptr ? package_id_for_path(input_.scope.package_roots, source.path)
                                      : source_fact->package_id;
    }

    void append_resolved_skeleton_facts(const SourceGraph &graph,
                                        FactCompleteness impl_completeness) {
        for (const auto &source : graph.sources) {
            if (source.program == nullptr) {
                continue;
            }
            const auto source_unit = index_source_unit_for_source(source);
            if (!source_unit.has_value()) {
                continue;
            }
            const auto package_id = package_id_for_index_source(source, *source_unit);
            append_skeleton_symbol_facts(index_,
                                         *source_unit,
                                         package_id,
                                         source.module_name,
                                         uri_from_path(source.path),
                                         source.source,
                                         *source.program);
            append_skeleton_impl_facts(index_,
                                       *source_unit,
                                       package_id,
                                       uri_from_path(source.path),
                                       source.source,
                                       *source.program,
                                       impl_completeness);
        }
    }

    void append_impl_skeleton_facts(const SourceGraph &graph, FactCompleteness completeness) {
        for (const auto &source : graph.sources) {
            if (source.program == nullptr) {
                continue;
            }
            const auto source_unit = index_source_unit_for_source(source);
            if (!source_unit.has_value()) {
                continue;
            }
            append_skeleton_impl_facts(index_,
                                       *source_unit,
                                       package_id_for_index_source(source, *source_unit),
                                       uri_from_path(source.path),
                                       source.source,
                                       *source.program,
                                       completeness);
        }
    }

    void emit_resolved_symbol_facts(const SourceGraph &graph, const ResolveResult &resolved) {
        std::vector<SymbolFactCandidate> symbol_candidates;
        for (const auto &symbol : resolved.symbol_table.symbols()) {
            const auto location = symbol_location(graph, symbol);
            if (!location.has_value() || !symbol.source_id.has_value()) {
                continue;
            }
            const auto *source_unit = source_unit_for_id(graph, *symbol.source_id);
            if (source_unit == nullptr) {
                continue;
            }
            const auto source_unit_id = index_source_unit_for_source(*source_unit);
            if (!source_unit_id.has_value()) {
                continue;
            }
            const auto selection_range = symbol_navigation_range(source_unit->source, symbol);
            symbol_candidates.push_back(SymbolFactCandidate{
                .symbol = &symbol,
                .source_unit_id = *source_unit_id,
                .package_id = package_id_for_index_source(*source_unit, *source_unit_id),
                .location = *location,
                .selection_range = selection_range,
            });
        }

        std::sort(symbol_candidates.begin(), symbol_candidates.end(), symbol_fact_candidate_less);

        for (const auto &candidate : symbol_candidates) {
            const auto def_id = DefId{index_.symbols().size()};
            def_by_symbol_.emplace(candidate.symbol->id.value, def_id);
            index_.add_symbol(SymbolFact{
                .def_id = def_id,
                .fingerprint = symbol_fingerprint(candidate.package_id,
                                                  candidate.source_unit_id,
                                                  candidate.symbol->name_space,
                                                  candidate.symbol->kind,
                                                  candidate.symbol->declaration_range,
                                                  candidate.selection_range),
                .package_id = candidate.package_id,
                .source_unit_id = candidate.source_unit_id,
                .kind = candidate.symbol->kind,
                .name_space = candidate.symbol->name_space,
                .visibility = candidate.symbol->visibility,
                .api_reachable = resolved.is_api_reachable(candidate.symbol->id),
                .artifact_reachable = resolved.is_artifact_reachable(candidate.symbol->id),
                .local_name = candidate.symbol->local_name,
                .canonical_name = candidate.symbol->canonical_name,
                .declaration_range = candidate.symbol->declaration_range,
                .selection_range = candidate.selection_range,
                .location = candidate.location,
                .completeness = FactCompleteness::Resolved,
            });
        }
    }

    void emit_resolved_alias_facts(const SourceGraph &graph, const ResolveResult &resolved) {
        for (const auto &alias : resolved.public_aliases()) {
            const auto target = resolved.symbol_table.get(alias.target);
            if (!target.has_value() || !alias.source_id.has_value()) {
                continue;
            }
            const auto *source_unit = source_unit_for_id(graph, *alias.source_id);
            if (source_unit == nullptr) {
                continue;
            }
            const auto source_unit_id = index_source_unit_for_source(*source_unit);
            if (!source_unit_id.has_value()) {
                continue;
            }
            const auto selection_range = alias_navigation_range(source_unit->source, alias);
            const auto target_def = def_for_symbol(def_by_symbol_, alias.target);
            index_.add_symbol(SymbolFact{
                .def_id = DefId{index_.symbols().size()},
                .fingerprint =
                    symbol_fingerprint(package_id_for_index_source(*source_unit, *source_unit_id),
                                       *source_unit_id,
                                       alias.name_space,
                                       target->get().kind,
                                       alias.declaration_range,
                                       selection_range),
                .alias_id = alias.id,
                .alias_target_def = target_def,
                .package_id = package_id_for_index_source(*source_unit, *source_unit_id),
                .source_unit_id = *source_unit_id,
                .kind = target->get().kind,
                .name_space = alias.name_space,
                .visibility = alias.visibility,
                .api_reachable = resolved.is_api_reachable(alias.id),
                .artifact_reachable = resolved.is_artifact_reachable(alias.id),
                .local_name = alias.local_name,
                .canonical_name = alias.canonical_name,
                .declaration_range = alias.declaration_range,
                .selection_range = selection_range,
                .location =
                    Location{
                        .uri = uri_from_path(source_unit->path),
                        .range = to_lsp_range(source_unit->source, selection_range),
                    },
                .completeness = FactCompleteness::Resolved,
            });
        }
    }

    void emit_resolved_reference_facts(const SourceGraph &graph, const ResolveResult &resolved) {
        for (const auto &reference : resolved.references()) {
            const auto location = reference_location(graph, reference);
            if (!location.has_value() || !reference.source_id.has_value()) {
                continue;
            }
            const auto *source_unit = source_unit_for_id(graph, *reference.source_id);
            if (source_unit == nullptr) {
                continue;
            }
            const auto source_unit_id = index_source_unit_for_source(*source_unit);
            if (!source_unit_id.has_value()) {
                continue;
            }
            const auto target = def_by_symbol_.find(reference.target.value);
            index_.add_reference(ReferenceFact{
                .package_id = package_id_for_index_source(*source_unit, *source_unit_id),
                .source_unit_id = *source_unit_id,
                .target_def = target == def_by_symbol_.end() ? std::nullopt
                                                             : std::optional<DefId>{target->second},
                .reference_kind = reference.kind,
                .range = reference.range,
                .location = *location,
                .completeness = target == def_by_symbol_.end() ? FactCompleteness::Parsed
                                                               : FactCompleteness::Resolved,
            });
        }
    }

    void emit_typed_impl_facts(const SourceGraph &graph, const TypeCheckResult &typed) {
        std::vector<ImplFactCandidate> impl_candidates;
        for (const auto &[raw_index, impl] : typed.environment.impls()) {
            (void)raw_index;
            if (impl.target_type == nullptr || !impl.source_id.has_value()) {
                continue;
            }
            const auto location = impl_location(graph, impl);
            if (!location.has_value()) {
                continue;
            }
            const auto *source_unit = source_unit_for_id(graph, *impl.source_id);
            if (source_unit == nullptr) {
                continue;
            }
            const auto source_unit_id = index_source_unit_for_source(*source_unit);
            if (!source_unit_id.has_value()) {
                continue;
            }
            impl_candidates.push_back(ImplFactCandidate{
                .package_id = package_id_for_index_source(*source_unit, *source_unit_id),
                .source_unit_id = *source_unit_id,
                .target_type = type_key_for_type_with_defs(*impl.target_type, &def_by_symbol_),
                .trait_def = impl.trait_symbol.has_value()
                                 ? def_for_symbol(def_by_symbol_, *impl.trait_symbol)
                                 : std::nullopt,
                .trait_range = has_extent(impl.trait_ref_range)
                                   ? std::optional<SourceRange>{impl.trait_ref_range}
                                   : std::nullopt,
                .declaration_range = impl.declaration_range,
                .target_range = impl_location_range(impl),
                .location = *location,
                .trait_location = impl_trait_location(graph, impl),
                .methods = method_facts_for_impl(impl),
                .source_order = impl.index,
            });
        }

        std::sort(impl_candidates.begin(), impl_candidates.end(), impl_fact_candidate_less);

        for (auto &candidate : impl_candidates) {
            index_.add_impl(ImplFact{
                .impl_id = WorkspaceImplId{index_.impls().size()},
                .package_id = candidate.package_id,
                .source_unit_id = candidate.source_unit_id,
                .target_type = std::move(candidate.target_type),
                .trait_def = candidate.trait_def,
                .trait_range = candidate.trait_range,
                .declaration_range = candidate.declaration_range,
                .target_range = candidate.target_range,
                .location = std::move(candidate.location),
                .trait_location = std::move(candidate.trait_location),
                .methods = std::move(candidate.methods),
                .source_order = candidate.source_order,
                .completeness = FactCompleteness::Typed,
            });
        }
    }

    const Frontend &frontend_;
    LspWorkspaceIndexInput input_;
    LspWorkspaceIndex index_;
    SourceUnitByPathMap source_units_by_path_;
    SourceUnitByDiagnosticNameMap source_units_by_name_;
    SourceUnitBySourceIdMap source_units_by_source_id_;
    DefBySymbolMap def_by_symbol_;
};

} // namespace

TypeKey type_key_for_type(const Type &type) {
    return type_key_for_type_with_defs(type, nullptr);
}

LspWorkspaceIndex build_lsp_workspace_index(const Frontend &frontend,
                                            LspWorkspaceIndexInput input) {
    return IndexAnalysisPipeline(frontend, std::move(input)).run();
}

} // namespace ahfl::lsp
