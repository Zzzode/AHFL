#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ahfl/base/support/diagnostics.hpp"
#include "ahfl/base/support/source.hpp"
#include "ahfl/compiler/semantics/types.hpp"
#include "compiler/package_graph/package_graph.hpp"
#include "compiler/syntax/frontend/project.hpp"
#include "tooling/lsp/protocol_types.hpp"

namespace ahfl {
class Frontend;
} // namespace ahfl

namespace ahfl::lsp {

enum class PrimitiveKind : std::uint8_t {
    Unit,
    Bool,
    Int,
    Float,
    String,
    UUID,
    Timestamp,
    Duration,
    Decimal,
};

enum class FactCompleteness : std::uint8_t {
    Parsed,
    Resolved,
    Typed,
    Invalid,
};

enum class LspNavigationIndexSourceKind : std::uint8_t {
    SemanticEntry,
    PackageExport,
    SysrootExport,
    OpenOverlay,
};

using SourceUnitId = package_graph::SourceUnitId;

struct DefId {
    std::size_t value{0};

    [[nodiscard]] friend bool operator==(DefId lhs, DefId rhs) noexcept = default;
};

struct DefFingerprint {
    std::uint64_t value{0};

    [[nodiscard]] friend bool operator==(DefFingerprint lhs, DefFingerprint rhs) noexcept = default;
};

struct WorkspaceImplId {
    std::size_t value{0};

    [[nodiscard]] friend bool operator==(WorkspaceImplId lhs,
                                         WorkspaceImplId rhs) noexcept = default;
};

struct ReferenceFactId {
    std::size_t value{0};

    [[nodiscard]] friend bool operator==(ReferenceFactId lhs,
                                         ReferenceFactId rhs) noexcept = default;
};

struct IndexDiagnosticFactId {
    std::size_t value{0};

    [[nodiscard]] friend bool operator==(IndexDiagnosticFactId lhs,
                                         IndexDiagnosticFactId rhs) noexcept = default;
};

enum class IndexDiagnosticPhase : std::uint8_t {
    Parse,
    Resolve,
    TypeCheck,
};

struct TypeKey {
    enum class Kind : std::uint8_t {
        Unknown,
        Error,
        Primitive,
        Nominal,
    };

    Kind kind{Kind::Unknown};
    std::optional<PrimitiveKind> primitive{};
    std::int64_t primitive_parameter{0};
    std::optional<DefId> def{};
    std::vector<TypeKey> type_args{};

    [[nodiscard]] friend bool operator==(const TypeKey &lhs, const TypeKey &rhs) noexcept = default;
};

struct TypeKeyHash {
    [[nodiscard]] std::size_t operator()(const TypeKey &key) const noexcept;
};

struct SourceUnitFact {
    SourceUnitId source_unit_id;
    package_graph::PackageId package_id;
    std::filesystem::path path;
    std::string uri;
    std::uint64_t revision{0};
    std::uint64_t content_fingerprint{0};
    std::vector<LspNavigationIndexSourceKind> scope_kinds{};
    FactCompleteness completeness{FactCompleteness::Resolved};
    bool valid{false};
};

struct ImplMethodFact {
    std::string name;
    SourceRange declaration_range;
    bool has_body{false};
    std::optional<std::string> builtin_name;
    std::size_t source_order{0};
};

struct ImplFact {
    WorkspaceImplId impl_id;
    package_graph::PackageId package_id;
    SourceUnitId source_unit_id;
    TypeKey target_type;
    std::optional<DefId> trait_def{};
    std::optional<SourceRange> trait_range{};
    SourceRange declaration_range;
    SourceRange target_range;
    Location location;
    std::optional<Location> trait_location{};
    std::vector<ImplMethodFact> methods{};
    std::size_t source_order{0};
    FactCompleteness completeness{FactCompleteness::Typed};
};

struct SymbolFact {
    DefId def_id;
    DefFingerprint fingerprint{};
    std::optional<AliasDefId> alias_id{};
    std::optional<DefId> alias_target_def{};
    package_graph::PackageId package_id;
    SourceUnitId source_unit_id;
    SymbolKind kind{SymbolKind::Struct};
    SymbolNamespace name_space{SymbolNamespace::Types};
    ast::Visibility visibility{ast::Visibility::PackageInternal};
    bool api_reachable{false};
    bool artifact_reachable{false};
    std::string local_name;
    std::string canonical_name;
    SourceRange declaration_range;
    SourceRange selection_range;
    Location location;
    FactCompleteness completeness{FactCompleteness::Resolved};
};

struct ReferenceFact {
    package_graph::PackageId package_id;
    SourceUnitId source_unit_id;
    std::optional<DefId> target_def;
    ReferenceKind reference_kind{ReferenceKind::TypeName};
    SourceRange range;
    Location location;
    FactCompleteness completeness{FactCompleteness::Resolved};
};

struct IndexDiagnosticFact {
    IndexDiagnosticFactId diagnostic_id;
    package_graph::PackageId package_id;
    SourceUnitId source_unit_id;
    IndexDiagnosticPhase phase{IndexDiagnosticPhase::Parse};
    ahfl::DiagnosticSeverity severity{ahfl::DiagnosticSeverity::Error};
    std::string code;
    std::string message;
    SourceRange range;
    FactCompleteness completeness{FactCompleteness::Invalid};
};

struct NavigationIndexMetadata {
    std::uint64_t revision{0};
    std::string index_schema_version;
    std::string index_identity_schema_version;
};

struct NavigationIndexReuseStats {
    std::size_t reused_source_units{0};
    std::size_t reused_symbol_facts{0};
    std::size_t reused_reference_facts{0};
    std::size_t reused_impl_facts{0};
};

class LspWorkspaceIndex {
  public:
    void set_metadata(NavigationIndexMetadata metadata);
    void set_reuse_stats(NavigationIndexReuseStats stats);

    void add_source_unit(SourceUnitFact fact);
    void set_source_unit_completeness(SourceUnitId source_unit, FactCompleteness completeness);
    void set_source_unit_content_fingerprint(SourceUnitId source_unit,
                                             std::uint64_t content_fingerprint);
    void add_symbol(SymbolFact fact);
    void add_reference(ReferenceFact fact);
    void add_impl(ImplFact fact);
    void add_diagnostic(IndexDiagnosticFact fact);

    [[nodiscard]] const std::vector<SourceUnitFact> &source_units() const noexcept {
        return source_units_;
    }

    [[nodiscard]] const std::vector<SymbolFact> &symbols() const noexcept {
        return symbols_;
    }

    [[nodiscard]] const std::vector<ImplFact> &impls() const noexcept {
        return impls_;
    }

    [[nodiscard]] const std::vector<ReferenceFact> &references() const noexcept {
        return references_;
    }

    [[nodiscard]] const std::vector<IndexDiagnosticFact> &diagnostics() const noexcept {
        return diagnostics_;
    }

    [[nodiscard]] const NavigationIndexMetadata &metadata() const noexcept {
        return metadata_;
    }

    [[nodiscard]] const NavigationIndexReuseStats &reuse_stats() const noexcept {
        return reuse_stats_;
    }

    [[nodiscard]] const SourceUnitFact *source_unit_for_id(SourceUnitId source_unit) const;
    [[nodiscard]] std::vector<SourceUnitId>
    source_units_for_package(package_graph::PackageId package_id) const;
    [[nodiscard]] std::vector<const IndexDiagnosticFact *>
    diagnostics_for_source(SourceUnitId source_unit) const;
    [[nodiscard]] const SymbolFact *symbol_for_def(DefId def) const;
    [[nodiscard]] std::vector<const SymbolFact *> workspace_symbols(std::string_view query) const;
    [[nodiscard]] std::vector<Location> reference_locations_for_def(DefId def) const;
    [[nodiscard]] std::vector<Location>
    implementation_locations_for_type(const TypeKey &type) const;
    [[nodiscard]] std::vector<Location> implementation_locations_for_nominal_def(DefId def) const;
    [[nodiscard]] std::vector<Location> implementation_locations_for_trait(DefId trait) const;
    [[nodiscard]] std::vector<Location>
    implementation_locations_for_primitive(PrimitiveKind kind) const;
    [[nodiscard]] std::optional<Location>
    primitive_home_location_for_type(const TypeKey &type) const;

  private:
    NavigationIndexMetadata metadata_;
    NavigationIndexReuseStats reuse_stats_;
    std::vector<SourceUnitFact> source_units_;
    std::vector<SymbolFact> symbols_;
    std::vector<ReferenceFact> references_;
    std::vector<ImplFact> impls_;
    std::vector<IndexDiagnosticFact> diagnostics_;
    std::unordered_map<std::size_t, std::size_t> source_unit_index_by_id_;
    std::vector<std::vector<SourceUnitId>> source_units_by_package_;
    std::vector<std::vector<ReferenceFactId>> references_by_def_;
    std::unordered_map<std::size_t, std::vector<IndexDiagnosticFactId>> diagnostics_by_source_;
    std::vector<std::vector<WorkspaceImplId>> impls_by_trait_;
    std::vector<std::vector<WorkspaceImplId>> impls_by_nominal_def_;
    std::unordered_map<TypeKey, std::vector<WorkspaceImplId>, TypeKeyHash> impls_by_type_;
};

struct LspIndexPackageRoot {
    package_graph::PackageId package_id;
    std::filesystem::path module_root;
};

struct LspIndexSourceUnitSeed {
    SourceUnitId source_unit_id;
    package_graph::PackageId package_id;
    std::filesystem::path path;
    std::vector<LspNavigationIndexSourceKind> scope_kinds;
};

struct NavigationIndexScope {
    std::vector<LspIndexPackageRoot> package_roots;
    std::vector<LspIndexSourceUnitSeed> source_units;
};

struct LspWorkspaceIndexInput {
    ProjectInput project;
    NavigationIndexScope scope;
    NavigationIndexMetadata metadata;
    const LspWorkspaceIndex *previous_index{nullptr};
};

[[nodiscard]] std::optional<PrimitiveKind> primitive_kind_from_spelling(std::string_view name);
[[nodiscard]] std::optional<PrimitiveKind> primitive_kind_for_type(const Type &type);
[[nodiscard]] TypeKey type_key_for_type(const Type &type);
[[nodiscard]] LspWorkspaceIndex build_lsp_workspace_index(const Frontend &frontend,
                                                          LspWorkspaceIndexInput input);

} // namespace ahfl::lsp
