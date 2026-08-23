#pragma once

#include "ahfl/compiler/semantics/monomorphization.hpp" // TypeSubstitutionMap
#include "ahfl/compiler/semantics/types.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ahfl {

// ============================================================================
// RFC 0013 P2-S1: shared generic-call inference module.
//
// Bidirectional-checking style (NOT Hindley-Milner union-find): the call
// site's expected type and the argument types drive a TypeSubstitutionMap
// over the callee's type parameters. The two core helpers are:
//
//   * unify_param_with_arg      — bind callee TypeVars from a concrete
//                                  argument type (first-occurrence-wins;
//                                  mismatch stops binding).
//   * prefill_subst_from_expected — bind callee TypeVars from the
//                                    surrounding expected type (R1
//                                    precedence lattice).
//
// Plus two query helpers:
//
//   * contains_type_var         — structural TypeVar presence test (R5
//                                  lambda flow-back decision).
//   * unbound_param_indices     — R2 concreteness test for the ambiguity
//                                  diagnostic.
//
// All TypePtr values stored in `subst` are already interned by TypeContext
// (they originate from declared or inferred types), so the hash-consing
// invariant is preserved without re-interning.
// ============================================================================

// Unify a declared parameter type (which may contain TypeVars bound to the
// callee's own type parameters) with a concrete argument type, recording
// TypeVar -> Type bindings into `subst` (indexed by TypeVarT::index, matching
// the callee's type_param_names order). The traversal is structural: it
// recurses through nominal type applications (Enum/Struct), the stdlib
// container types (Option/List/Set/Map) and Fn types so TypeVars nested in
// `Option<T>`, `Map<K,V>`, or `Fn(T)->U` are bound from the corresponding
// argument sub-types.
//
// Unification's job is inference, not diagnosis. A structural mismatch
// (different constructor, different nominal symbol, or mismatched arity)
// simply stops binding for that sub-tree — the subsequent check_assignable
// against the instantiated param type reports any real mismatch with a
// proper diagnostic.
//
// R1 precedence (concrete > TypeVar): a concrete arg overwrites a slot that
// holds a TypeVar (e.g. `None` adopted an expected TypeVar, then `Some(2)`
// provides concrete Int). A TypeVar arg only fills an empty slot — it never
// clobbers a concrete one. First occurrence of a concrete type wins.
//
// R7.1 occurs check: when binding a param-side TypeVar to an arg-side type,
// if the arg type structurally contains the same TypeVar (same index AND
// scope_id), do not bind (leave unbound so the ambiguity diagnostic fires).
// Flow-back (R5) puts the callee's own TypeVars on the arg side for the
// first time, so this guard is load-bearing.
//
// Reflexive equality (T := T, same index AND scope_id) is NOT an occurs
// violation — it is the common case for method calls on generic receivers
// where the method's impl-level type params share the impl's scope_id with
// the receiver's type params. The slot is marked occupied so receiver-pinned
// masking and downstream substitution see it; the TypeVar re-resolves to
// itself.
void unify_param_with_arg(const Type &param, const Type &arg, TypeSubstitutionMap &subst);

// R1 precedence lattice for expected-type prefill.
//
// Bind callee TypeVars from the surrounding expected type by walking the
// declared type (e.g. the callee's return type) against the expected type
// structurally. For each TypeVar encountered on the declared side at
// index i:
//
//   1. explicit_mask[i]        — never overwritten (explicit type args win).
//   2. receiver_pinned_mask[i] — never overwritten by prefill (receiver
//      unification pinned this index; B2).
//   3. expected-concrete       — always wins over arg-inferred (R1).
//   4. expected-TypeVar        — NEVER adopted, even for an empty slot.
//      Arg-inference is more informative: it can produce a concrete type
//      where the expected is a free TypeVar from an enclosing scope.
//
// The walk recurses through EnumT/StructT (same symbol + arity), stdlib
// containers (same kind), and FnT (same param arity), stopping on structural
// mismatch. `explicit_mask` and `receiver_pinned_mask` may be empty (treated
// as all-false); out-of-range entries are treated as false.
void prefill_subst_from_expected(const Type &declared,
                                 const Type &expected,
                                 TypeSubstitutionMap &subst,
                                 const std::vector<bool> &explicit_mask,
                                 const std::vector<bool> &receiver_pinned_mask);

// R5: true when the type structurally contains any TypeVarT (of any scope).
// Used by the lambda flow-back decision: a concrete expected return type
// (no TypeVars) is kept as-is; a TypeVar-bearing expected return flows back
// to the inferred body type.
[[nodiscard]] bool contains_type_var(const Type &type) noexcept;

// R2: return the indices of type parameters that are not fully concrete
// after inference: entries that are nullptr OR structurally contain a
// TypeVarT whose scope_id == callee_scope_id (a free variable of the
// callee's own scope). TypeVars of enclosing scopes (scope_id !=
// callee_scope_id) are legitimate deferred propagation (e.g. `inner(x)`
// inside `fn outer<T>`), not ambiguity.
//
// RFC 0013 P2-S1 (R0.1): for impl methods, pass the method's per-method
// scope_id as `method_scope_id` so method-level TypeVars are also detected
// as unbound. kUnknownTypeVarScopeId (default) checks only one scope.
[[nodiscard]] std::vector<std::size_t>
unbound_param_indices(const TypeSubstitutionMap &subst,
                      std::uint32_t callee_scope_id,
                      std::uint32_t method_scope_id = kUnknownTypeVarScopeId);

// RFC 0013 P2-S1 (R0.1): substitute TypeVars in a type using BOTH the
// impl-wide scope_id (for impl-level TypeVars at indices < impl_tparam_count)
// and the per-method scope_id (for method-level TypeVars). This is the
// method-call counterpart to substitute_type: impl methods carry TypeVars
// from two scopes after the R0.1 split.
[[nodiscard]] TypePtr substitute_method_type(TypePtr type,
                                              const TypeSubstitutionMap &subst,
                                              std::uint32_t impl_scope_id,
                                              std::uint32_t method_scope_id,
                                              TypeContext &types);

} // namespace ahfl
