#pragma once

#include <cstddef>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ahfl/base/support/diagnostics.hpp"
#include "ahfl/base/support/ownership.hpp"
#include "ahfl/base/support/source.hpp"
#include "ahfl/compiler/frontend/ast.hpp"
#include "ahfl/compiler/semantics/declaration_info.hpp"
#include "ahfl/compiler/semantics/effects.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/type_relations.hpp"
#include "ahfl/compiler/semantics/typed_hir.hpp"
#include "ahfl/compiler/semantics/types.hpp"

namespace ahfl {

struct SourceGraph;
class TypeCheckPass;
class TypeContext;

// P3c.S4a: lightweight non-owning reference to a single resolved impl block.
// Returned by TypeEnvironment::find_impls so callers (method dispatch, bound
// checking, coherence diagnostics) can iterate candidate impls without deep
// copying ImplTypeInfo. The pointer is valid for the TypeEnvironment's
// lifetime.
struct ImplRef {
    std::size_t index{0};
    const ImplTypeInfo *info{nullptr};

    [[nodiscard]] explicit operator bool() const noexcept {
        return info != nullptr;
    }
    [[nodiscard]] const ImplTypeInfo &operator*() const noexcept {
        return *info;
    }
    [[nodiscard]] const ImplTypeInfo *operator->() const noexcept {
        return info;
    }
    [[nodiscard]] friend bool operator==(const ImplRef &lhs,
                                         const ImplRef &rhs) noexcept {
        return lhs.index == rhs.index && lhs.info == rhs.info;
    }
};

class TypeEnvironment {
  public:
    [[nodiscard]] const std::unordered_map<std::size_t, TypePtr> &const_types() const noexcept {
        return const_types_;
    }

    [[nodiscard]] const std::unordered_map<std::size_t, StructTypeInfo> &structs() const noexcept {
        return structs_;
    }

    [[nodiscard]] const std::unordered_map<std::size_t, EnumTypeInfo> &enums() const noexcept {
        return enums_;
    }

    [[nodiscard]] const std::unordered_map<std::size_t, CapabilityTypeInfo> &
    capabilities() const noexcept {
        return capabilities_;
    }

    [[nodiscard]] const std::unordered_map<std::size_t, PredicateTypeInfo> &
    predicates() const noexcept {
        return predicates_;
    }

    [[nodiscard]] const std::unordered_map<std::size_t, AgentTypeInfo> &agents() const noexcept {
        return agents_;
    }

    [[nodiscard]] const std::unordered_map<std::size_t, WorkflowTypeInfo> &
    workflows() const noexcept {
        return workflows_;
    }

    [[nodiscard]] const std::unordered_map<std::size_t, FlowTypeInfo> &flows() const noexcept {
        return flows_;
    }

    [[nodiscard]] const std::unordered_map<std::size_t, ContractTypeInfo> &
    contracts() const noexcept {
        return contracts_;
    }

    // P2 (RFC §3.2.2): declared top-level functions keyed by their Function
    // symbol id.
    [[nodiscard]] const std::unordered_map<std::size_t, FnTypeInfo> &functions() const noexcept {
        return functions_;
    }

    // P3 (RFC §3.2.2 / type-system §1.3): declared traits keyed by their Trait
    // symbol id. Built by build_trait_types; consumed by the impl signature
    // matcher, the orphan-rule checker, and trait-method resolution.
    [[nodiscard]] const std::unordered_map<std::size_t, TraitTypeInfo> &traits() const noexcept {
        return traits_;
    }

    // P3 (RFC §3.2.2 / type-system §1.4): all impl blocks in source order.
    // Keyed by impl index (impls have no name); includes both inherent impls
    // and trait impls. Consumers iterate the whole list — typical program has
    // a handful of impls.
    [[nodiscard]] const std::unordered_map<std::size_t, ImplTypeInfo> &impls() const noexcept {
        return impls_;
    }

    [[nodiscard]] MaybeCRef<Type> get_const_type(SymbolId id) const;
    [[nodiscard]] MaybeCRef<StructTypeInfo> get_struct(SymbolId id) const;
    [[nodiscard]] MaybeCRef<StructTypeInfo> get_struct(const Type &type) const;
    [[nodiscard]] MaybeCRef<EnumTypeInfo> get_enum(SymbolId id) const;
    [[nodiscard]] MaybeCRef<EnumTypeInfo> get_enum(const Type &type) const;
    [[nodiscard]] MaybeCRef<StructTypeInfo> find_struct(std::string_view canonical_name) const;
    [[nodiscard]] MaybeCRef<EnumTypeInfo> find_enum(std::string_view canonical_name) const;
    [[nodiscard]] MaybeCRef<CapabilityTypeInfo> get_capability(SymbolId id) const;
    [[nodiscard]] MaybeCRef<PredicateTypeInfo> get_predicate(SymbolId id) const;
    [[nodiscard]] MaybeCRef<AgentTypeInfo> get_agent(SymbolId id) const;
    [[nodiscard]] MaybeCRef<WorkflowTypeInfo> get_workflow(SymbolId id) const;
    [[nodiscard]] MaybeCRef<FlowTypeInfo> get_flow(SymbolId id) const;
    [[nodiscard]] MaybeCRef<ContractTypeInfo> get_contract(SymbolId id) const;
    // P2 (RFC §3.2.2): lookup a fn by its Function symbol id.
    [[nodiscard]] MaybeCRef<FnTypeInfo> get_fn(SymbolId id) const;
    // P3 (RFC §3.2.2 / type-system §1.3): lookup a trait by its Trait symbol id.
    [[nodiscard]] MaybeCRef<TraitTypeInfo> get_trait(SymbolId id) const;
    // P3 (RFC §3.2.2 / type-system §1.3): find a trait by canonical name.
    [[nodiscard]] MaybeCRef<TraitTypeInfo> find_trait(std::string_view canonical_name) const;
    // P3 (RFC §3.2.2 / type-system §2.1): trait-resolution query. Given a
    // resolved trait symbol and the nominal target symbol (struct/enum), return
    // the unique trait impl block in the environment, or nullopt when no impl
    // exists. P3b only resolves exact (trait, target) symbol-keyed matches;
    // generic-argument unification (RFC §2.1 step 2) and ambiguity diagnosis
    // land with the method-call expression typecheck (no call sites today).
    // Surfaced here so a future expr-pass can resolve `Type::method(args)` /
    // `e.method(args)` without re-walking the impl table.
    [[nodiscard]] MaybeCRef<ImplTypeInfo>
    resolve_trait_impl(SymbolId trait_symbol, SymbolId target_symbol) const;

    // P3c.S4a (coherence MVP §1): trait-resolution query. Given a resolved
    // trait symbol and a *concrete* (post-instantiation) type, return all
    // non-inherent impl blocks in the environment that match the trait and
    // whose normalized target type is equivalent to `concrete_type`. The
    // normalized equivalence is the same relation the orphan-rule checker and
    // the strict-coherence duplicate detector use (single-source-of-truth via
    // impls_conflict_for_type), so callers never see a candidate set larger
    // than one unless the coherence detector has already diagnosed a conflict.
    //
    // Returns an empty vector when no impl exists. The trait may be nullopt
    // (useful when enumerating all impls for a concrete type regardless of
    // trait), in which case every non-inherent impl whose normalized target
    // type coincides with `concrete_type` is returned.
    [[nodiscard]] std::vector<ImplRef>
    find_impls(std::optional<SymbolId> trait_symbol, const Type &concrete_type) const;

    // P3c.S4a (coherence MVP §2): single-source-of-truth trait/type
    // equivalence used by (a) find_impls candidate filtering,
    // (b) check_impl_coherence (orphan rule), and (c) the build_impl_types
    // duplicate-impl (COHERENCE_CONFLICT) detector. Two impls conflict when
    // they share the same resolved trait and their target types normalize to
    // the same canonical key. Generic-parameter unification is intentionally
    // out of scope for the MVP; the normalized key currently relies on the
    // hash-consed Type identity produced by TypeContext (so structurally
    // identical types built through the same TypeContext always compare
    // equal).
    [[nodiscard]] static bool impls_conflict_for_type(const ImplTypeInfo &lhs,
                                                      const ImplTypeInfo &rhs);

    // P3c.S4a: compute a stable string key for `type` that encodes its
    // normalized form. Two types are considered coherence-equivalent iff
    // their keys compare equal. For nominal types (struct/enum) the key is
    // `$KIND:$SYMBOL_VALUE:$CANONICAL_NAME:$TYPE_ARGS`, so two instantiations
    // that differ in type arguments are kept distinct. For all other types
    // the key is `describe()` output, which is deterministic for the
    // hash-consed type universe. Exposed publicly so tests can assert the
    // shared relation directly.
    [[nodiscard]] static std::string normalize_type_key(const Type &type);

    // P3c.S5a (declaration-layer registration): impl_index — bidirectional
    // trait↔impl map keyed by (Trait, normalized-TypeKey). Populated for
    // every non-inherent impl in declaration order by build_impl_types so
    // consumers (lookup, coherence, IR lowering) can locate the matching
    // impl set in O(1) average time without rescanning the impl table.
    struct ImplIndexKey {
        std::size_t trait_symbol_value{0}; // SymbolId::value of the trait
        std::string normalized_type_key;   // normalize_type_key(target_type)

        [[nodiscard]] friend bool operator==(const ImplIndexKey &lhs,
                                             const ImplIndexKey &rhs) noexcept = default;
    };
    struct ImplIndexKeyHash {
        [[nodiscard]] std::size_t operator()(const ImplIndexKey &k) const noexcept {
            // FNV-1a combine. trait_symbol_value carries the most entropy;
            // mix with the already-deterministic type key.
            std::size_t h = 0xcbf29ce484222325ULL;
            h ^= k.trait_symbol_value;
            h *= 0x100000001b3ULL;
            const std::string &s = k.normalized_type_key;
            for (unsigned char c : s) {
                h ^= static_cast<std::size_t>(c);
                h *= 0x100000001b3ULL;
            }
            return h;
        }
    };
    using ImplIndex =
        std::unordered_map<ImplIndexKey, std::vector<std::size_t>, ImplIndexKeyHash>;

    // O(1) lookup of impl indices by (trait, target-type). Returns the
    // ordered list of impl indices recorded in the impl_index for the given
    // (Trait, normalized_type_key) pair; an empty vector when no such impl
    // exists. The returned indices index into impls() and are stable for
    // the lifetime of the environment.
    [[nodiscard]] std::vector<std::size_t>
    lookup_impl_index(std::optional<SymbolId> trait_symbol,
                      const Type &concrete_type) const;

    // Same query using an already-computed normalized type key — useful for
    // callers (e.g. coherence diagnostics) that already computed a key.
    [[nodiscard]] std::vector<std::size_t>
    lookup_impl_index_by_key(std::optional<SymbolId> trait_symbol,
                             std::string_view normalized_type_key) const;

    // Read-only access to the full impl_index (used by TypedProgram snapshot
    // and by tests that want to iterate populated buckets).
    [[nodiscard]] const ImplIndex &impl_index() const noexcept {
        return impl_index_;
    }

    // O(1) lookup: returns true iff `id` is the symbol of any agent's context struct.
    [[nodiscard]] bool is_agent_context_struct(SymbolId id) const noexcept;

    // Compute a stable 64-bit fingerprint of a declaration's outward-facing
    // signature: a struct's field types, an enum's variants, a capability's
    // parameter / return types, etc. The fingerprint depends only on
    // hash-consed Type identities and canonical names, so two compilations
    // of an unchanged declaration always produce the same value.
    //
    // Returns std::nullopt when `id` does not correspond to a known
    // declaration in this environment. Intended consumers are incremental
    // rebuild drivers and LSP cache invalidation: when a declaration's
    // fingerprint is unchanged, downstream cached results can be reused.
    [[nodiscard]] std::optional<std::uint64_t> signature_fingerprint(SymbolId id) const;

  private:
    friend class TypeChecker;
    friend class TypeCheckPass;

    // Maintain reverse name index alongside primary maps so all queries are O(1) on average.
    void index_struct(std::size_t id, StructTypeInfo info);
    void index_enum(std::size_t id, EnumTypeInfo info);
    void mark_agent_context_struct(SymbolId id);
    // P3c.S5a: register a non-inherent impl into impl_index. Skips inherent
    // impls (no trait_symbol) and impls with an unset trait_symbol.
    void register_impl_index(std::size_t impl_index, const ImplTypeInfo &info);

    std::unordered_map<std::size_t, TypePtr> const_types_;
    std::unordered_map<std::size_t, StructTypeInfo> structs_;
    std::unordered_map<std::size_t, EnumTypeInfo> enums_;
    std::unordered_map<std::size_t, CapabilityTypeInfo> capabilities_;
    std::unordered_map<std::size_t, PredicateTypeInfo> predicates_;
    std::unordered_map<std::size_t, AgentTypeInfo> agents_;
    std::unordered_map<std::size_t, WorkflowTypeInfo> workflows_;
    std::unordered_map<std::size_t, FlowTypeInfo> flows_;
    std::unordered_map<std::size_t, ContractTypeInfo> contracts_;
    std::unordered_map<std::size_t, FnTypeInfo> functions_; // P2 (RFC §3.2.2)
    std::unordered_map<std::size_t, TraitTypeInfo> traits_; // P3 (RFC §3.2.2 / type-system §1.3)
    std::unordered_map<std::size_t, ImplTypeInfo> impls_;   // P3 (RFC §3.2.2 / type-system §1.4)
    // P3c.S5a: bidirectional trait↔impl index, keyed by (trait_symbol,
    // normalized_type_key). Values are impl indices stored in declaration
    // order (push_back). Built alongside impls_ during build_impl_types.
    ImplIndex impl_index_;

    // Reverse indices: canonical_name -> SymbolId.value, for O(1) name lookups.
    std::unordered_map<std::string, std::size_t> struct_name_index_;
    std::unordered_map<std::string, std::size_t> enum_name_index_;
    std::unordered_map<std::string, std::size_t> trait_name_index_; // P3

    // SymbolIds of structs used as any agent's context type.
    std::unordered_set<std::size_t> agent_context_struct_ids_;
};

struct TypeCheckOptions {
    // Emit note-level diagnostics explaining why Optional<T> narrowing facts
    // were or were not produced from if conditions. Disabled by default so
    // normal user-facing diagnostics stay quiet.
    bool explain_narrowing{false};
    // Record relation checks performed by the type checker through
    // TypeRelationContext. Disabled by default to avoid retaining trace data in
    // normal compilation.
    bool trace_type_relations{false};
    // Emit three-stage method-dispatch audit-trace notes (stage1 inherent,
    // stage2 trait, stage3 bound verification) into the diagnostic bag.
    // Disabled by default: dispatch notes are informational-only and must not
    // leak into user-facing diagnostics (effects / narrowing tests assert the
    // diagnostic bag is clean on success). Enable only for the trait-impl
    // dispatch-golden test suite.
    bool trace_dispatch{false};
};

struct TypeCheckResult {
    TypeEnvironment environment;
    DiagnosticBag diagnostics;
    TypedProgram typed_program;
    RelationTrace relation_trace;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

class TypeChecker {
  public:
    [[nodiscard]] TypeCheckResult check(const ast::Program &program,
                                        const ResolveResult &resolve_result) const;
    [[nodiscard]] TypeCheckResult check(const ast::Program &program,
                                        const ResolveResult &resolve_result,
                                        TypeCheckOptions options) const;
    [[nodiscard]] TypeCheckResult check(const ast::Program &program,
                                        const ResolveResult &resolve_result,
                                        TypeContext &types) const;
    [[nodiscard]] TypeCheckResult check(const ast::Program &program,
                                        const ResolveResult &resolve_result,
                                        TypeContext &types,
                                        TypeCheckOptions options) const;
    [[nodiscard]] TypeCheckResult check(const SourceGraph &graph,
                                        const ResolveResult &resolve_result) const;
    [[nodiscard]] TypeCheckResult check(const SourceGraph &graph,
                                        const ResolveResult &resolve_result,
                                        TypeCheckOptions options) const;
    [[nodiscard]] TypeCheckResult
    check(const SourceGraph &graph, const ResolveResult &resolve_result, TypeContext &types) const;
    [[nodiscard]] TypeCheckResult check(const SourceGraph &graph,
                                        const ResolveResult &resolve_result,
                                        TypeContext &types,
                                        TypeCheckOptions options) const;
};

void dump_type_environment(const TypeEnvironment &environment,
                           const SymbolTable &symbols,
                           std::ostream &out);

} // namespace ahfl
