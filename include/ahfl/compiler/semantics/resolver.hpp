#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ahfl/base/support/diagnostics.hpp"
#include "ahfl/base/support/ownership.hpp"
#include "ahfl/base/support/source.hpp"
#include "ahfl/compiler/frontend/ast.hpp"

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
    // P2 (RFC §3.2.2): top-level function declarations live in their own
    // namespace so a fn name `f` is distinct from a capability / predicate /
    // type of the same spelling, mirroring how the existing namespaces keep
    // capabilities and predicates disjoint at call sites.
    Functions,
    // C-2 (Wave-24): trait declarations live in their own namespace so a
    // trait name `Ord` is distinct from a type / type-alias of the same
    // spelling. This prevents accidental shadowing (e.g. a struct named
    // `Ord` colliding with the `Ord` trait) and lets the resolver emit
    // precise "trait not found" vs "type not found" diagnostics.
    Traits,
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
    // P2 (RFC §3.2.2): a declared `fn` registers as a Function symbol.
    Function,
    // C-2 (Wave-24): a declared `trait` registers as a Trait symbol in the
    // Traits namespace, distinct from Types. A trait name is usable at bound
    // positions (`T: Ord`) and impl positions (`impl Ord for T`) via a
    // dedicated TraitBound reference kind.
    Trait,
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
    // P2 (RFC §3.2.2): a fn call `f(args)` resolves its callee to a Function
    // symbol. Kept distinct from `CallTarget` (which the existing
    // capability/predicate resolver writes) so the call-target kind lookup
    // can disambiguate a fn call from a capability/predicate call without
    // re-walking both namespaces.
    FnCallTarget,
    // M3 (RFC §1.5): an enum variant constructor call `EnumName::Variant(args)`
    // resolves its callee owner to the Enum symbol. The variant name (last
    // path segment) and payload arity are validated by the typecheck pass,
    // which has the EnumTypeInfo; the resolver only registers the owner so
    // the call site can be recognised without re-walking the Types namespace.
    EnumVariantConstructor,
    // C-2 (Wave-24): a trait bound `T: Ord` resolves "Ord" to a Trait symbol
    // in the Traits namespace. Kept distinct from TypeName so the typecheck
    // pass can distinguish a trait reference from a regular type reference
    // at where-clause / type-param bound positions.
    TraitBound,
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
    // Returns every symbol in the given namespace whose `local_name` matches
    // `name`, regardless of which module owns it. Used by diagnostics that
    // want to surface "declared in N locations" context when a name is
    // provided by more than one module (e.g. TypeMismatch origin notes).
    [[nodiscard]] std::vector<SymbolId> find_all_local(SymbolNamespace name_space,
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
    NamespaceIndex function_symbols_; // P2 (RFC §3.2.2)
    NamespaceIndex trait_symbols_;    // C-2 (Wave-24)
};

struct ResolveResult {
    ResolveResult() = default;
    ResolveResult(const ResolveResult &other);
    ResolveResult &operator=(const ResolveResult &other);
    ResolveResult(ResolveResult &&other) noexcept;
    ResolveResult &operator=(ResolveResult &&other) noexcept;

    SymbolTable symbol_table;
    DiagnosticBag diagnostics;

    // C-4 (Wave-24): per-lambda explicit capture list. Keyed by AST NodeId so
    // the typecheck + lowering passes can recover the ordered list of outer
    // names the user wrote in `\[a, b] ...` without reaching back into the
    // parser-produced AST. Empty map entry is *not* stored when a lambda uses
    // the implicit-capture default (no `[...]` prefix).
    std::unordered_map<std::uint64_t, std::vector<std::string>> captured_names_by_expr;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }

    [[nodiscard]] const std::vector<ResolvedReference> &references() const noexcept {
        return references_;
    }

    [[nodiscard]] const std::vector<ImportBinding> &imports() const noexcept {
        return imports_;
    }

    void add_reference(ResolvedReference reference);
    void add_import(ImportBinding binding);

    [[nodiscard]] MaybeCRef<ResolvedReference>
    find_reference(ReferenceKind kind,
                   SourceRange range,
                   std::optional<SourceId> source_id = std::nullopt) const;

  private:
    struct ReferenceLookupKey {
        ReferenceKind kind{ReferenceKind::TypeName};
        std::size_t begin_offset{0};
        std::size_t end_offset{0};
        std::optional<SourceId> source_id;

        [[nodiscard]] friend bool operator==(const ReferenceLookupKey &lhs,
                                             const ReferenceLookupKey &rhs) noexcept = default;
    };

    struct ReferenceLookupKeyHash {
        [[nodiscard]] std::size_t operator()(const ReferenceLookupKey &key) const noexcept;
    };

    struct ReferenceLookupNoSourceKey {
        ReferenceKind kind{ReferenceKind::TypeName};
        std::size_t begin_offset{0};
        std::size_t end_offset{0};

        [[nodiscard]] friend bool
        operator==(const ReferenceLookupNoSourceKey &lhs,
                   const ReferenceLookupNoSourceKey &rhs) noexcept = default;
    };

    struct ReferenceLookupNoSourceKeyHash {
        [[nodiscard]] std::size_t operator()(const ReferenceLookupNoSourceKey &key) const noexcept;
    };

    void rebuild_reference_lookup_cache() const;
    void ensure_reference_lookup_cache() const;
    void invalidate_reference_lookup_cache() const noexcept;

    std::vector<ResolvedReference> references_;
    std::vector<ImportBinding> imports_;

    mutable std::unordered_map<ReferenceLookupKey, std::size_t, ReferenceLookupKeyHash>
        reference_lookup_cache_;
    mutable std::
        unordered_map<ReferenceLookupNoSourceKey, std::size_t, ReferenceLookupNoSourceKeyHash>
            reference_lookup_no_source_cache_;
    mutable std::size_t reference_lookup_cache_size_{0};
    mutable const ResolvedReference *reference_lookup_cache_data_{nullptr};
    mutable bool reference_lookup_cache_valid_{false};
};

class Resolver {
  public:
    [[nodiscard]] ResolveResult resolve(const ast::Program &program) const;
    [[nodiscard]] ResolveResult resolve(const SourceGraph &graph) const;
};

} // namespace ahfl
