#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

struct SourceUnitId {
    std::size_t value{0};

    [[nodiscard]] friend bool operator==(SourceUnitId lhs, SourceUnitId rhs) noexcept = default;
};

struct DefId {
    std::size_t value{0};

    [[nodiscard]] friend bool operator==(DefId lhs, DefId rhs) noexcept = default;
};

struct WorkspaceImplId {
    std::size_t value{0};

    [[nodiscard]] friend bool operator==(WorkspaceImplId lhs,
                                         WorkspaceImplId rhs) noexcept = default;
};

struct TypeKey {
    enum class Kind : std::uint8_t {
        Unknown,
        Error,
        Primitive,
        Nominal,
    };

    Kind kind{Kind::Unknown};
    std::optional<PrimitiveKind> primitive;
    std::int64_t primitive_parameter{0};
    std::optional<DefId> def;
    std::vector<TypeKey> type_args;

    [[nodiscard]] friend bool operator==(const TypeKey &lhs, const TypeKey &rhs) noexcept = default;
};

struct ImplFact {
    WorkspaceImplId impl_id;
    package_graph::PackageId package_id;
    SourceUnitId source_unit_id;
    TypeKey target_type;
    std::optional<DefId> trait_def;
    Location location;
    std::size_t source_order{0};
    FactCompleteness completeness{FactCompleteness::Typed};
};

struct SymbolFact {
    DefId def_id;
    package_graph::PackageId package_id;
    SourceUnitId source_unit_id;
    SymbolKind kind{SymbolKind::Struct};
    std::string local_name;
    std::string canonical_name;
    Location location;
    FactCompleteness completeness{FactCompleteness::Resolved};
};

struct ReferenceFact {
    package_graph::PackageId package_id;
    SourceUnitId source_unit_id;
    std::optional<DefId> target_def;
    ReferenceKind reference_kind{ReferenceKind::TypeName};
    Location location;
    FactCompleteness completeness{FactCompleteness::Resolved};
};

class LspWorkspaceIndex {
  public:
    void add_symbol(SymbolFact fact);
    void add_reference(ReferenceFact fact);
    void add_impl(ImplFact fact);

    [[nodiscard]] const std::vector<SymbolFact> &symbols() const noexcept {
        return symbols_;
    }

    [[nodiscard]] const std::vector<ImplFact> &impls() const noexcept {
        return impls_;
    }

    [[nodiscard]] std::optional<DefId> find_def(SymbolKind kind,
                                                std::string_view canonical_name) const;
    [[nodiscard]] std::vector<const SymbolFact *> workspace_symbols(std::string_view query) const;
    [[nodiscard]] std::vector<Location> reference_locations_for_def(DefId def) const;
    [[nodiscard]] std::vector<Location>
    implementation_locations_for_type(const TypeKey &type) const;
    [[nodiscard]] std::vector<Location>
    implementation_locations_for_primitive(PrimitiveKind kind) const;

  private:
    std::vector<SymbolFact> symbols_;
    std::vector<ReferenceFact> references_;
    std::vector<ImplFact> impls_;
};

struct LspIndexPackageRoot {
    package_graph::PackageId package_id;
    std::filesystem::path module_root;
};

struct LspWorkspaceIndexInput {
    ProjectInput project;
    std::vector<LspIndexPackageRoot> package_roots;
};

[[nodiscard]] std::optional<PrimitiveKind> primitive_kind_from_spelling(std::string_view name);
[[nodiscard]] std::optional<PrimitiveKind> primitive_kind_for_type(const Type &type);
[[nodiscard]] TypeKey type_key_for_type(const Type &type);
[[nodiscard]] LspWorkspaceIndex build_lsp_workspace_index(const Frontend &frontend,
                                                          LspWorkspaceIndexInput input);

} // namespace ahfl::lsp
