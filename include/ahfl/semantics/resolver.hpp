#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ahfl/frontend/ast.hpp"
#include "ahfl/support/diagnostics.hpp"
#include "ahfl/support/ownership.hpp"
#include "ahfl/support/source.hpp"

namespace ahfl {

struct SourceGraph;

namespace detail {
class ResolverPass;
}

enum class SymbolNamespace {
    Types,
    Consts,
    Capabilities,
    Predicates,
    Agents,
    Workflows,
};

enum class SymbolKind {
    Struct,
    Enum,
    TypeAlias,
    Const,
    Capability,
    Predicate,
    Agent,
    Workflow,
};

enum class ReferenceKind {
    TypeName,
    ConstValue,
    QualifiedValueOwnerType,
    CallTarget,
    TemporalCapability,
    AgentCapability,
    ContractTarget,
    FlowTarget,
    WorkflowNodeTarget,
};

struct SymbolId {
    std::size_t value{0};

    [[nodiscard]] friend bool operator==(SymbolId lhs, SymbolId rhs) noexcept = default;
};

struct Symbol {
    SymbolId id;
    SymbolNamespace name_space{SymbolNamespace::Types};
    SymbolKind kind{SymbolKind::Struct};
    std::string local_name;
    std::string canonical_name;
    std::string module_name;
    std::optional<SourceId> source_id;
    SourceRange declaration_range;
};

struct ResolvedReference {
    ReferenceKind kind{ReferenceKind::TypeName};
    std::string text;
    std::optional<SourceId> source_id;
    SourceRange range;
    SymbolId target;
};

struct ImportBinding {
    std::string alias;
    std::string target_module;
    std::optional<SourceId> source_id;
    SourceRange declaration_range;
};

class SymbolTable {
  public:
    [[nodiscard]] const std::vector<Symbol> &symbols() const noexcept {
        return symbols_;
    }

    [[nodiscard]] MaybeCRef<Symbol> get(SymbolId id) const;
    [[nodiscard]] MaybeCRef<Symbol> find_local(SymbolNamespace name_space,
                                               std::string_view name,
                                               std::string_view module_name = "") const;
    [[nodiscard]] MaybeCRef<Symbol> find_canonical(SymbolNamespace name_space,
                                                   std::string_view name) const;

  private:
    friend class Resolver;
    friend class detail::ResolverPass;

    struct NamespaceIndex {
        std::unordered_map<std::string, SymbolId> canonical_names;
        std::unordered_map<std::string, std::unordered_map<std::string, SymbolId>>
            module_local_names;
    };

    [[nodiscard]] const NamespaceIndex &index(SymbolNamespace name_space) const;
    [[nodiscard]] NamespaceIndex &index(SymbolNamespace name_space);

    std::vector<Symbol> symbols_;
    NamespaceIndex type_symbols_;
    NamespaceIndex const_symbols_;
    NamespaceIndex capability_symbols_;
    NamespaceIndex predicate_symbols_;
    NamespaceIndex agent_symbols_;
    NamespaceIndex workflow_symbols_;
};

struct ResolveResult {
    SymbolTable symbol_table;
    std::vector<ResolvedReference> references;
    std::vector<ImportBinding> imports;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }

    [[nodiscard]] MaybeCRef<ResolvedReference> find_reference(ReferenceKind kind,
                                                              SourceRange range,
                                                              std::optional<SourceId> source_id =
                                                                  std::nullopt) const;
};

class Resolver {
  public:
    [[nodiscard]] ResolveResult resolve(const ast::Program &program) const;
    [[nodiscard]] ResolveResult resolve(const SourceGraph &graph) const;
};

} // namespace ahfl
