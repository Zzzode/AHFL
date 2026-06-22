#pragma once

// ============================================================================
// P2c (RFC §3.2.2 / §3.5 / §5): monomorphization pass
// ============================================================================
//
// The monomorphization pass runs *after* the typed-tree is built and *before*
// IR lowering. It consumes `TypedProgram::fn_call_sites` — the per-call-site
// (fn symbol, type-args) pairs recorded by the typecheck pass — and builds the
// instantiation set: each distinct (fn declaration, canonical type-args) pair
// is a *typed instance* that downstream IR lowering / SMV encoding consumes.
//
// Design (corelib-type-system.zh.md §5):
//
//   1. Instantiation timing — typed HIR after, IR lowering before. The typed
//      tree already carries the full resolved types; IR sees only concrete
//      instances afterwards.
//   2. Cache key — (decl canonical id, type_args canonical, where-clause
//      substituted). Two call sites share an instance iff the three are equal.
//   3. Budget — user code ≤ 32 instances, stdlib ≤ 256. Exceeding the user
//      budget surfaces `E::monomorphization_budget_exceeded`.
//   4. Side-table — an instance is a *new* record; the original generic
//      declaration is never mutated, so other call sites keep sharing it.
//
// P2c scope: the pass records the instantiation surface (cache keys, deduped
// instances, per-declaration instance counts) and enforces the budget. True
// type-argument substitution into a cloned typed body (deep clone + walk +
// rewrite) lands with the generic-body typecheck work that produces a typed
// block for a generic fn body; today a generic fn body is type-checked
// structurally and is not indexed as a TypedBlock, so the pass records the
// instances and budget state without emitting a rewritten body.

#include "ahfl/base/support/source.hpp"
#include "ahfl/compiler/semantics/typed_hir.hpp"
#include "ahfl/compiler/semantics/types.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ahfl {

class TypeContext;

// Monomorphization budget ceilings (RFC §5.3). The user budget applies to all
// instances triggered by non-`std::` modules; the stdlib budget is the larger
// ceiling reserved for `std::`-originated instantiations.
inline constexpr std::uint32_t kMonomorphizationUserBudget{32};
inline constexpr std::uint32_t kMonomorphizationStdlibBudget{256};

// Result classification for a single instantiation attempt.
enum class MonomorphizationStatus : std::uint8_t {
    // A new typed instance was generated and counted against the budget.
    Created,
    // The (decl, type_args) pair was already present; the existing instance
    // is reused (no budget cost).
    Reused,
    // The user budget would be exceeded by this instantiation.
    BudgetExceeded,
};

// One distinct typed instance produced by the pass.
struct MonomorphizationInstance {
    // Canonical name of the originating fn declaration (e.g.
    // `app::sum_squares`). Used as the leading component of the cache key.
    std::string decl_canonical_name;
    // SymbolId of the originating fn declaration.
    SymbolId fn_symbol{0};
    // Canonical (RFC §5.2) stringification of the explicit type arguments in
    // source order. Empty for a non-generic fn (a single canonical instance
    // covers all call sites) or an inference-driven call.
    std::string type_args_canonical;
    // Stable, human-readable instance id (e.g. `app::sum_squares<Int>`).
    std::string instance_name;
    // Whether this instance was triggered by a `std::` module declaration.
    // Used to attribute it to the stdlib budget instead of the user budget.
    bool from_stdlib{false};
    // P2d: index of the instantiated (cloned + type-substituted) function body
    // block in TypedProgram::blocks. UINT32_MAX when the original fn has no
    // body (e.g. a prototype/builtin) or when instantiation has not been
    // performed (e.g. non-generic fn reused as-is).
    std::uint32_t body_block_index{UINT32_MAX};
};

// Aggregated result of running the pass over a TypedProgram.
struct MonomorphizationResult {
    // Deduplicated instances in first-seen order.
    std::vector<MonomorphizationInstance> instances;
    // Per-declaration instance count (keyed by canonical decl name). Drives
    // the "largest contributors" hint in the budget-exceeded diagnostic.
    std::unordered_map<std::string, std::uint32_t> instances_per_decl;
    // Budget counters actually consumed (Created instances).
    std::uint32_t user_instance_count{0};
    std::uint32_t stdlib_instance_count{0};

    [[nodiscard]] std::uint32_t total_instance_count() const noexcept {
        return user_instance_count + stdlib_instance_count;
    }

    [[nodiscard]] bool budget_exceeded() const noexcept {
        return user_instance_count > kMonomorphizationUserBudget ||
               stdlib_instance_count > kMonomorphizationStdlibBudget;
    }
};

// Options steering the pass.
struct MonomorphizationOptions {
    // When true, the first instantiation that would exceed the user (resp.
    // stdlib) budget is dropped and the run continues collecting remaining
    // sites; the result surfaces `budget_exceeded()`. When false, the
    // over-budget site still records a Reused/Created outcome per the cache
    // but the counters are clamped — keeping the default (true) matches the
    // RFC §5.3 diagnostic behaviour.
    bool enforce_budget{true};
};

// Build the canonical type-args string for a call site (RFC §5.2). The
// rendering is structural and recursive so two equivalent type-argument lists
// (e.g. `List<Int>` vs an alias-expanded `List<Int>`) that share the same
// hash-consed `Type` collapse to the same key.
[[nodiscard]] std::string canonical_type_args(const std::vector<TypePtr> &type_args);

// Run the monomorphization pass over a typed program's recorded fn call sites.
// Pure: reads `program.fn_call_sites` and the symbol table, writes only into
// the returned result. A program with no generic call sites yields an empty
// result.
//
// This is the P2c entry point: it records the instantiation surface (cache
// keys, deduped instances, per-declaration instance counts) and enforces the
// budget. It does NOT clone or rewrite the typed body; use the P2d overload
// below when you need instantiated typed bodies.
[[nodiscard]] MonomorphizationResult
run_monomorphization(const TypedProgram &program, MonomorphizationOptions options = {});

// P2d overload: also instantiate each generic fn's typed body.
//
// For every distinct (fn, type_args) pair that passes the budget check, this
// deep-clones the fn's original typed body block and applies the type
// substitution, appending the new nodes to `program`'s flat stores. The
// resulting `MonomorphizationInstance::body_block_index` points to the
// instantiated root block.
//
// Non-generic instances (empty type_args) and fns with no body skip
// instantiation; their `body_block_index` stays UINT32_MAX.
[[nodiscard]] MonomorphizationResult
run_monomorphization(TypedProgram &program, TypeContext &types, MonomorphizationOptions options = {});

// ---------------------------------------------------------------------------
// Type substitution helper (P2d)
// ---------------------------------------------------------------------------

// A substitution map: type-parameter index -> concrete type.
//
// Index is the zero-based position of the type parameter in the enclosing
// generic declaration's parameter list. This is the industry-standard
// approach (Rust Substs, Swift GenericTypeParamKey, Clang TemplateParmIndex):
// O(1) position-based lookup, no string hashing, no name-confusion bugs.
// The index aligns with `TypeVarT::index` and `FnTypeInfo::type_param_names`.
//
// Indexes are dense (0..N-1 for N type parameters). A vector is used
// instead of a sparse map because generic parameter counts are small
// (typically 0-4 in P2 programs) and position-based access is cache-friendly.
using TypeSubstitutionMap = std::vector<TypePtr>;

// Apply a substitution to a type, replacing each TypeVar whose name is in the
// map with the corresponding concrete type. Types not in the map are left as
// TypeVar (the caller is responsible for ensuring all variables are bound).
// Recurses into composite types (Optional, List, Set, Map, Fn).
//
// The result is always an interned type (returned via TypeContext), so pointer
// equality can be used for fast comparison.
[[nodiscard]] TypePtr substitute_type(TypePtr type,
                                      const TypeSubstitutionMap &subst,
                                      TypeContext &types);

// ---------------------------------------------------------------------------
// Typed body instantiation (P2d)
// ---------------------------------------------------------------------------

// Result of instantiating a generic function body.
struct BodyInstantiationResult {
    // Index of the new (cloned + type-substituted) TypedBlock in
    // TypedProgram::blocks.
    std::uint32_t body_block_index{UINT32_MAX};
    // Number of new expressions added to TypedProgram::expressions.
    std::uint32_t expr_count{0};
    // Number of new statements added to TypedProgram::statements.
    std::uint32_t stmt_count{0};
    // Number of new blocks added to TypedProgram::blocks (including the root).
    std::uint32_t block_count{0};
};

// Deep-clone a function body (block + statements + expressions) and apply a
// type substitution to every type in the cloned tree. The cloned nodes are
// appended to the program's flat stores. Returns the index of the new root
// block plus counts of new nodes.
//
// Preconditions:
//   - body_block_index must be valid (< program.blocks.size())
//   - subst must cover all TypeVars in the body (unbound vars are kept as-is)
//
// This is the core of P2d monomorphization: each call site of a generic fn
// gets its own independent typed body with concrete types.
[[nodiscard]] BodyInstantiationResult
instantiate_fn_body(TypedProgram &program,
                    std::uint32_t body_block_index,
                    const TypeSubstitutionMap &subst,
                    TypeContext &types);

} // namespace ahfl
