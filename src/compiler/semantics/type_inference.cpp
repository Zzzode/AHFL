#include "ahfl/compiler/semantics/type_inference.hpp"

#include "compiler/semantics/std_container_types.hpp"

namespace ahfl {

namespace {

// Uniform view over EnumT and EnumVariantT for structural traversal. Both
// carry the parent enum's symbol and type_args; EnumVariantT additionally
// carries the variant name (irrelevant for inference). Without this, a
// variant constructor result (EnumVariantT, e.g. `Option<Int>::Some`) cannot
// drive inference against a declared param (EnumT, e.g. `Option<T>`).
struct EnumLikeView {
    const std::optional<SymbolId> &symbol;
    const std::string &canonical_name;
    const std::vector<TypePtr> &type_args;
};

[[nodiscard]] std::optional<EnumLikeView> enum_like_view(const Type &type) noexcept {
    if (const auto *e = type.get_if<types::EnumT>(); e != nullptr) {
        return EnumLikeView{e->symbol, e->canonical_name, e->type_args};
    }
    if (const auto *v = type.get_if<types::EnumVariantT>(); v != nullptr) {
        return EnumLikeView{v->symbol, v->canonical_name, v->type_args};
    }
    return std::nullopt;
}

// True when two enum-like views refer to the same enum (by symbol when both
// have one, else by canonical name).
[[nodiscard]] bool same_enum(const EnumLikeView &a, const EnumLikeView &b) noexcept {
    if (a.symbol.has_value() && b.symbol.has_value()) {
        return *a.symbol == *b.symbol;
    }
    return a.canonical_name == b.canonical_name;
}

// R7.1: structural occurs check — true when `type` contains a TypeVarT with
// the given (index, scope_id) pair. Used by unify_param_with_arg to reject
// bindings that would create an infinite type (e.g. T := Option<T>).
[[nodiscard]] bool
occurs_in(const Type &type, std::uint32_t index, std::uint32_t scope_id) noexcept {
    if (const auto *tv = type.get_if<types::TypeVarT>(); tv != nullptr) {
        return tv->index == index && tv->scope_id == scope_id;
    }
    if (const auto container = stdlib_bridge::std_container_type_view(type);
        container.has_value()) {
        if (container->first != nullptr &&
            occurs_in(*container->first, index, scope_id)) {
            return true;
        }
        if (container->second != nullptr &&
            occurs_in(*container->second, index, scope_id)) {
            return true;
        }
        return false;
    }
    if (const auto e = enum_like_view(type); e.has_value()) {
        for (const auto *arg : e->type_args) {
            if (arg != nullptr && occurs_in(*arg, index, scope_id)) {
                return true;
            }
        }
        return false;
    }
    if (const auto *s = type.get_if<types::StructT>(); s != nullptr) {
        for (const auto *arg : s->type_args) {
            if (arg != nullptr && occurs_in(*arg, index, scope_id)) {
                return true;
            }
        }
        return false;
    }
    if (const auto *f = type.get_if<types::FnT>(); f != nullptr) {
        for (const auto *p : f->params) {
            if (p != nullptr && occurs_in(*p, index, scope_id)) {
                return true;
            }
        }
        if (f->return_type != nullptr &&
            occurs_in(*f->return_type, index, scope_id)) {
            return true;
        }
    }
    return false;
}

// Structural TypeVar presence test (any scope). Shared by contains_type_var
// (R5) and the R2 concreteness walk.
[[nodiscard]] bool contains_any_type_var(const Type &type) noexcept {
    if (type.holds<types::TypeVarT>()) {
        return true;
    }
    if (const auto container = stdlib_bridge::std_container_type_view(type);
        container.has_value()) {
        if (container->first != nullptr && contains_any_type_var(*container->first)) {
            return true;
        }
        if (container->second != nullptr && contains_any_type_var(*container->second)) {
            return true;
        }
        return false;
    }
    if (const auto e = enum_like_view(type); e.has_value()) {
        for (const auto *arg : e->type_args) {
            if (arg != nullptr && contains_any_type_var(*arg)) {
                return true;
            }
        }
        return false;
    }
    if (const auto *s = type.get_if<types::StructT>(); s != nullptr) {
        for (const auto *arg : s->type_args) {
            if (arg != nullptr && contains_any_type_var(*arg)) {
                return true;
            }
        }
        return false;
    }
    if (const auto *f = type.get_if<types::FnT>(); f != nullptr) {
        for (const auto *p : f->params) {
            if (p != nullptr && contains_any_type_var(*p)) {
                return true;
            }
        }
        if (f->return_type != nullptr && contains_any_type_var(*f->return_type)) {
            return true;
        }
    }
    return false;
}

// R2: structural test for a free TypeVar of the callee's own scope.
// TypeVars of enclosing scopes (scope_id != callee_scope_id) are legitimate
// deferred propagation, not ambiguity.
[[nodiscard]] bool
contains_callee_scope_type_var(const Type &type, std::uint32_t callee_scope_id) noexcept {
    if (const auto *tv = type.get_if<types::TypeVarT>(); tv != nullptr) {
        return tv->scope_id == callee_scope_id;
    }
    if (const auto container = stdlib_bridge::std_container_type_view(type);
        container.has_value()) {
        if (container->first != nullptr &&
            contains_callee_scope_type_var(*container->first, callee_scope_id)) {
            return true;
        }
        if (container->second != nullptr &&
            contains_callee_scope_type_var(*container->second, callee_scope_id)) {
            return true;
        }
        return false;
    }
    if (const auto e = enum_like_view(type); e.has_value()) {
        for (const auto *arg : e->type_args) {
            if (arg != nullptr &&
                contains_callee_scope_type_var(*arg, callee_scope_id)) {
                return true;
            }
        }
        return false;
    }
    if (const auto *s = type.get_if<types::StructT>(); s != nullptr) {
        for (const auto *arg : s->type_args) {
            if (arg != nullptr &&
                contains_callee_scope_type_var(*arg, callee_scope_id)) {
                return true;
            }
        }
        return false;
    }
    if (const auto *f = type.get_if<types::FnT>(); f != nullptr) {
        for (const auto *p : f->params) {
            if (p != nullptr &&
                contains_callee_scope_type_var(*p, callee_scope_id)) {
                return true;
            }
        }
        if (f->return_type != nullptr &&
            contains_callee_scope_type_var(*f->return_type, callee_scope_id)) {
            return true;
        }
    }
    return false;
}

// R1: true when the index is protected from prefill by an explicit type arg
// or a receiver-pinned binding.
[[nodiscard]] bool masked(const std::vector<bool> &mask, std::size_t index) noexcept {
    return index < mask.size() && mask[index];
}

} // namespace

void unify_param_with_arg(const Type &param, const Type &arg, TypeSubstitutionMap &subst) {
    if (param.holds<types::ErrorT>() || arg.holds<types::ErrorT>()) {
        return;
    }
    // TypeVar on the param side: bind it.
    if (const auto *tv = param.get_if<types::TypeVarT>(); tv != nullptr) {
        if (tv->index < subst.size()) {
            // Reflexive equality: T := T is not an infinite type. This is
            // the common case for method calls on generic receivers where
            // the method's impl-level type params share the impl's scope_id
            // with the receiver's type params. Bind it so the slot is
            // marked occupied (receiver-pinned masking and downstream
            // substitution see it); the TypeVar re-resolves to itself.
            if (const auto *arg_tv = arg.get_if<types::TypeVarT>();
                arg_tv != nullptr && arg_tv->index == tv->index &&
                arg_tv->scope_id == tv->scope_id) {
                if (subst[tv->index] == nullptr) {
                    subst[tv->index] = &arg;
                }
                return;
            }
            // R7.1 occurs check: don't bind a TypeVar to a type that
            // structurally contains the same TypeVar (same index AND
            // scope_id). This would create an infinite type. Leave it
            // unbound so the ambiguity diagnostic fires.
            if (occurs_in(arg, tv->index, tv->scope_id)) {
                return;
            }
            // R1 precedence: concrete > TypeVar. A concrete arg wins over a
            // slot that holds a TypeVar (e.g. `None` adopted an expected
            // TypeVar, then `Some(2)` provides concrete Int). A TypeVar arg
            // only fills an empty slot — it never clobbers a concrete one.
            const bool arg_is_typevar = arg.holds<types::TypeVarT>();
            const bool slot_is_empty = subst[tv->index] == nullptr;
            const bool slot_has_typevar =
                !slot_is_empty && subst[tv->index]->holds<types::TypeVarT>();
            if (slot_is_empty || (!arg_is_typevar && slot_has_typevar)) {
                subst[tv->index] = &arg;
            }
        }
        return;
    }
    const auto param_container = stdlib_bridge::std_container_type_view(param);
    const auto arg_container = stdlib_bridge::std_container_type_view(arg);
    if (param_container.has_value() && arg_container.has_value() &&
        param_container->kind == arg_container->kind) {
        switch (param_container->kind) {
        case stdlib_bridge::StdContainerKind::Option:
        case stdlib_bridge::StdContainerKind::List:
        case stdlib_bridge::StdContainerKind::Set:
            if (param_container->first != nullptr && arg_container->first != nullptr) {
                unify_param_with_arg(*param_container->first, *arg_container->first, subst);
            }
            return;
        case stdlib_bridge::StdContainerKind::Map:
            if (param_container->first != nullptr && arg_container->first != nullptr) {
                unify_param_with_arg(*param_container->first, *arg_container->first, subst);
            }
            if (param_container->second != nullptr && arg_container->second != nullptr) {
                unify_param_with_arg(*param_container->second, *arg_container->second, subst);
            }
            return;
        }
    }
    // Enum-like (EnumT or EnumVariantT): same enum, recurse type_args.
    if (const auto pe = enum_like_view(param); pe.has_value()) {
        const auto ae = enum_like_view(arg);
        if (!ae.has_value() || pe->type_args.size() != ae->type_args.size()) {
            return;
        }
        if (!same_enum(*pe, *ae)) {
            return;
        }
        for (std::size_t i = 0; i < pe->type_args.size(); ++i) {
            if (pe->type_args[i] != nullptr && ae->type_args[i] != nullptr) {
                unify_param_with_arg(*pe->type_args[i], *ae->type_args[i], subst);
            }
        }
        return;
    }
    // StructT: same symbol, recurse type_args.
    if (const auto *ps = param.get_if<types::StructT>(); ps != nullptr) {
        const auto *as = arg.get_if<types::StructT>();
        if (as == nullptr || ps->type_args.size() != as->type_args.size()) {
            return;
        }
        const bool same = (ps->symbol.has_value() && as->symbol.has_value())
                              ? (*ps->symbol == *as->symbol)
                              : (ps->canonical_name == as->canonical_name);
        if (!same) {
            return;
        }
        for (std::size_t i = 0; i < ps->type_args.size(); ++i) {
            if (ps->type_args[i] != nullptr && as->type_args[i] != nullptr) {
                unify_param_with_arg(*ps->type_args[i], *as->type_args[i], subst);
            }
        }
        return;
    }
    if (const auto *pf = param.get_if<types::FnT>(); pf != nullptr) {
        const auto *af = arg.get_if<types::FnT>();
        if (af != nullptr && pf->params.size() == af->params.size()) {
            for (std::size_t i = 0; i < pf->params.size(); ++i) {
                if (pf->params[i] != nullptr && af->params[i] != nullptr) {
                    unify_param_with_arg(*pf->params[i], *af->params[i], subst);
                }
            }
            if (pf->return_type != nullptr && af->return_type != nullptr) {
                unify_param_with_arg(*pf->return_type, *af->return_type, subst);
            }
        }
        return;
    }
    // Leaf types (Bool/Int/String/...): no TypeVars to bind.
}

void prefill_subst_from_expected(const Type &declared,
                                 const Type &expected,
                                 TypeSubstitutionMap &subst,
                                 const std::vector<bool> &explicit_mask,
                                 const std::vector<bool> &receiver_pinned_mask) {
    // TypeVar on the declared side: bind from expected with the R1
    // precedence lattice.
    if (const auto *tv = declared.get_if<types::TypeVarT>(); tv != nullptr) {
        if (tv->index >= subst.size()) {
            return;
        }
        // 1. explicit: never overwritten.
        // 2. receiver-pinned: never overwritten by prefill (B2).
        if (masked(explicit_mask, tv->index) || masked(receiver_pinned_mask, tv->index)) {
            return;
        }
        // 3. expected-concrete: always wins over arg-inferred (R1). An
        //    expected TypeVar is NOT concrete — never adopt it, even for an
        //    empty slot: arg-inference is more informative (it can produce
        //    a concrete type where the expected is a free TypeVar from an
        //    enclosing scope).
        if (!expected.holds<types::TypeVarT>()) {
            subst[tv->index] = &expected;
        }
        return;
    }
    // stdlib containers: same kind, recurse element types.
    const auto declared_container = stdlib_bridge::std_container_type_view(declared);
    const auto expected_container = stdlib_bridge::std_container_type_view(expected);
    if (declared_container.has_value() && expected_container.has_value() &&
        declared_container->kind == expected_container->kind) {
        if (declared_container->first != nullptr && expected_container->first != nullptr) {
            prefill_subst_from_expected(*declared_container->first,
                                        *expected_container->first,
                                        subst,
                                        explicit_mask,
                                        receiver_pinned_mask);
        }
        if (declared_container->second != nullptr && expected_container->second != nullptr) {
            prefill_subst_from_expected(*declared_container->second,
                                        *expected_container->second,
                                        subst,
                                        explicit_mask,
                                        receiver_pinned_mask);
        }
        return;
    }
    // Enum-like (EnumT or EnumVariantT): same enum, recurse type_args.
    if (const auto de = enum_like_view(declared); de.has_value()) {
        const auto ee = enum_like_view(expected);
        if (!ee.has_value() || de->type_args.size() != ee->type_args.size()) {
            return;
        }
        if (!same_enum(*de, *ee)) {
            return;
        }
        for (std::size_t i = 0; i < de->type_args.size(); ++i) {
            if (de->type_args[i] != nullptr && ee->type_args[i] != nullptr) {
                prefill_subst_from_expected(*de->type_args[i],
                                            *ee->type_args[i],
                                            subst,
                                            explicit_mask,
                                            receiver_pinned_mask);
            }
        }
        return;
    }
    // StructT: same symbol, recurse type_args.
    if (const auto *ds = declared.get_if<types::StructT>(); ds != nullptr) {
        const auto *es = expected.get_if<types::StructT>();
        if (es == nullptr || ds->type_args.size() != es->type_args.size()) {
            return;
        }
        const bool same = (ds->symbol.has_value() && es->symbol.has_value())
                              ? (*ds->symbol == *es->symbol)
                              : (ds->canonical_name == es->canonical_name);
        if (!same) {
            return;
        }
        for (std::size_t i = 0; i < ds->type_args.size(); ++i) {
            if (ds->type_args[i] != nullptr && es->type_args[i] != nullptr) {
                prefill_subst_from_expected(*ds->type_args[i],
                                            *es->type_args[i],
                                            subst,
                                            explicit_mask,
                                            receiver_pinned_mask);
            }
        }
        return;
    }
    // FnT: same param arity, recurse params + return.
    if (const auto *df = declared.get_if<types::FnT>(); df != nullptr) {
        const auto *ef = expected.get_if<types::FnT>();
        if (ef != nullptr && df->params.size() == ef->params.size()) {
            for (std::size_t i = 0; i < df->params.size(); ++i) {
                if (df->params[i] != nullptr && ef->params[i] != nullptr) {
                    prefill_subst_from_expected(*df->params[i],
                                                *ef->params[i],
                                                subst,
                                                explicit_mask,
                                                receiver_pinned_mask);
                }
            }
            if (df->return_type != nullptr && ef->return_type != nullptr) {
                prefill_subst_from_expected(*df->return_type,
                                            *ef->return_type,
                                            subst,
                                            explicit_mask,
                                            receiver_pinned_mask);
            }
        }
        return;
    }
    // Leaf types (Bool/Int/String/...): no TypeVars to bind.
}

[[nodiscard]] bool contains_type_var(const Type &type) noexcept {
    return contains_any_type_var(type);
}

[[nodiscard]] std::vector<std::size_t>
unbound_param_indices(const TypeSubstitutionMap &subst,
                      std::uint32_t callee_scope_id,
                      std::uint32_t method_scope_id) {
    std::vector<std::size_t> result;
    for (std::size_t i = 0; i < subst.size(); ++i) {
        if (subst[i] == nullptr) {
            result.push_back(i);
        } else if (contains_callee_scope_type_var(*subst[i], callee_scope_id) ||
                   (method_scope_id != kUnknownTypeVarScopeId &&
                    contains_callee_scope_type_var(*subst[i], method_scope_id))) {
            result.push_back(i);
        }
    }
    return result;
}

[[nodiscard]] TypePtr substitute_method_type(TypePtr type,
                                              const TypeSubstitutionMap &subst,
                                              std::uint32_t impl_scope_id,
                                              std::uint32_t method_scope_id,
                                              TypeContext &types) {
    // First substitute impl-level TypeVars (indices < impl_tparam_count,
    // scope_id == impl_scope_id), then method-level TypeVars (scope_id ==
    // method_scope_id). The two scopes are disjoint after the R0.1 split,
    // so the order doesn't matter for correctness — but substituting the
    // impl scope first ensures that any method-level TypeVars introduced by
    // impl-level subst entries are also resolved.
    auto result = substitute_type(type, subst, impl_scope_id, types);
    if (method_scope_id != kUnknownTypeVarScopeId) {
        result = substitute_type(result, subst, method_scope_id, types);
    }
    return result;
}

} // namespace ahfl
