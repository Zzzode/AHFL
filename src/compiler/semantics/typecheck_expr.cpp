#include "compiler/semantics/typecheck_internal.hpp"

#include "ahfl/compiler/semantics/monomorphization.hpp"
#include "ahfl/compiler/semantics/name_suggestions.hpp"
#include "compiler/semantics/std_container_types.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace ahfl {

using internal::BindingMap;
using internal::CallContext;
using internal::is_bool_type;
using internal::is_error_type;
using internal::is_numeric_type;
using internal::TypedValue;
using internal::ValueContext;

namespace {

template <typename... Ts> struct overloaded : Ts... {
    using Ts::operator()...;
};

template <typename... Ts> overloaded(Ts...) -> overloaded<Ts...>;

[[nodiscard]] std::int64_t parse_decimal_scale(std::string_view text) {
    const auto dot_index = text.find('.');
    if (dot_index == std::string_view::npos) {
        return 0;
    }

    std::int64_t scale = 0;
    for (std::size_t index = dot_index + 1; index < text.size(); ++index) {
        if (!std::isdigit(static_cast<unsigned char>(text[index]))) {
            break;
        }

        ++scale;
    }

    return scale;
}

[[nodiscard]] ExprEffect expr_effect_from_judgement(const EffectJudgement &judgement) noexcept {
    switch (judgement.kind) {
    case EffectJudgement::Kind::Pure:
    case EffectJudgement::Kind::Bottom:
        return ExprEffect::Pure;
    case EffectJudgement::Kind::Nondet:
        return ExprEffect::Nondet;
    case EffectJudgement::Kind::CapabilitySet:
        return ExprEffect::ExternalEffect;
    }
    return ExprEffect::Pure;
}

struct MethodCandidate {
    const ImplTypeInfo *impl{nullptr};
    const ImplMethodInfo *method{nullptr};
};

[[nodiscard]] const ImplMethodInfo *find_method_ptr(const ImplTypeInfo &impl,
                                                    std::string_view name) noexcept {
    for (const auto &method : impl.methods) {
        if (method.name == name) {
            return &method;
        }
    }
    return nullptr;
}

[[nodiscard]] std::optional<SymbolId> nominal_symbol_of_type(const Type &type) noexcept {
    if (const auto *structure = type.get_if<types::StructT>(); structure != nullptr) {
        return structure->symbol;
    }
    if (const auto *enumeration = type.get_if<types::EnumT>(); enumeration != nullptr) {
        return enumeration->symbol;
    }
    return std::nullopt;
}

[[nodiscard]] std::string method_call_name(const Type &receiver_type, std::string_view method) {
    std::string name = receiver_type.describe();
    name.push_back('.');
    name.append(method);
    return name;
}

// Strip the module-qualification prefix from a symbol spelling so the
// per-argument diagnostic refers to the short callable name (e.g. "handle"
// instead of "lib::api::handle"). Keeps a class-method style dot separator so
// receiver-type qualified method names render as "Receiver.method".
[[nodiscard]] std::string callable_basename(std::string_view qualified) noexcept {
    const auto sep = qualified.rfind("::");
    if (sep == std::string_view::npos) {
        return std::string{qualified};
    }
    return std::string{qualified.substr(sep + 2)};
}

// P2d (RFC §3.5): unification for generic fn call-site type-argument inference.
//
// Walks a declared parameter type (which may contain TypeVars bound to the
// callee's own type parameters) alongside the concrete argument type, recording
// TypeVar -> Type bindings into `subst` (indexed by TypeVarT::index, matching
// FnTypeInfo::type_param_names order). The traversal is structural: it recurses
// through nominal type applications (Enum/Struct) and the container/Fn type
// constructors so TypeVars nested in `Option<T>`, `Map<K,V>`, or `Fn(T)->U`
// are bound from the corresponding argument sub-types.
//
// Unification's job is inference, not diagnosis. A structural mismatch
// (different constructor, different nominal symbol, or mismatched arity) simply
// stops binding for that sub-tree — the subsequent check_assignable against the
// instantiated param type reports any real mismatch with a proper diagnostic.
// First occurrence of a TypeVar wins; a later inconsistent use is likewise left
// to check_assignable.
void unify_param_with_arg(const Type &param, const Type &arg, TypeSubstitutionMap &subst) {
    if (param.holds<types::ErrorT>() || arg.holds<types::ErrorT>()) {
        return;
    }
    // TypeVar on the param side: bind it.
    if (const auto *tv = param.get_if<types::TypeVarT>(); tv != nullptr) {
        if (tv->index < subst.size() && subst[tv->index] == nullptr) {
            subst[tv->index] = &arg;
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
            unify_param_with_arg(*param_container->first, *arg_container->first, subst);
            return;
        case stdlib_bridge::StdContainerKind::Map:
            unify_param_with_arg(*param_container->first, *arg_container->first, subst);
            unify_param_with_arg(*param_container->second, *arg_container->second, subst);
            return;
        }
    }
    // EnumT: same symbol, recurse type_args.
    if (const auto *pe = param.get_if<types::EnumT>(); pe != nullptr) {
        const auto *ae = arg.get_if<types::EnumT>();
        if (ae == nullptr || pe->type_args.size() != ae->type_args.size()) {
            return;
        }
        const bool same = (pe->symbol.has_value() && ae->symbol.has_value())
                              ? (*pe->symbol == *ae->symbol)
                              : (pe->canonical_name == ae->canonical_name);
        if (!same) {
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

} // namespace

class ExpressionCheckerServices final {
  public:
    ExpressionCheckerServices(const ResolveResult &resolve_result,
                              std::optional<SourceId> current_source_id,
                              const TypeEnvironment &environment,
                              TypeContext &types,
                              TypeRelationContext &relations,
                              ExpressionSemaDelegate &delegate,
                              bool enable_trace_dispatch)
        : trace_dispatch(enable_trace_dispatch), resolve_result_(resolve_result),
          current_source_id_(current_source_id), environment_(environment), types_(types),
          relations_(relations), delegate_(&delegate), values_(types_) {}

    [[nodiscard]] const ExpressionValueFactory &values() const noexcept {
        return values_;
    }

    void typecheck_error_here(ErrorCode<DiagnosticCategory::TypeCheck> code,
                              std::string message,
                              SourceRange range) const {
        delegate_->typecheck_error(code, std::move(message), range);
    }

    void typecheck_error_here(ErrorCode<DiagnosticCategory::TypeCheck> code,
                              std::string message,
                              SourceRange range,
                              std::vector<Diagnostic::Related> notes) const {
        delegate_->typecheck_error(code, std::move(message), range, std::move(notes));
    }

    [[nodiscard]] TypedValue check_expr(const ast::ExprSyntax &expr,
                                        const ValueContext &context,
                                        MaybeCRef<Type> expected_type) const {
        return delegate_->check_nested(expr, context, expected_type);
    }

    [[nodiscard]] TypedValue check_expr(const ast::ExprSyntax &expr,
                                        const ValueContext &context,
                                        const TypeExpectation &expectation) const {
        return delegate_->check_nested(expr, context, expectation);
    }

    [[nodiscard]] bool check_assignable(const Type &source,
                                        const Type &target,
                                        SourceRange range,
                                        std::string_view context_label) const {
        if (is_error_type(source) || is_error_type(target)) {
            return true;
        }

        if (ahfl::is_assignable_to(source, target, relations_)) {
            return true;
        }

        std::vector<Diagnostic::Related> notes;
        append_multi_declaration_notes(
            notes, collect_nominal_declarations(target, resolve_result_.symbol_table),
            target.describe(), "expected type");
        append_multi_declaration_notes(
            notes, collect_nominal_declarations(source, resolve_result_.symbol_table),
            source.describe(), "actual type");
        append_nominal_declared_here_note(
            notes, collect_nominal_declarations(target, resolve_result_.symbol_table),
            target.describe(), "expected type");
        append_nominal_declared_here_note(
            notes, collect_nominal_declarations(source, resolve_result_.symbol_table),
            source.describe(), "actual type");
        notes.push_back(Diagnostic::Related{
            .message = actual_type_note(source),
            .range = range,
        });
        typecheck_error_here(error_codes::typecheck::TypeMismatch,
                             messages::typecheck::TypeMismatch.format_with(
                                 context_label, target.describe(), source.describe()),
                             range,
                             std::move(notes));
        return false;
    }

    [[nodiscard]] bool check_assignable(const Type &source,
                                        const Type &target,
                                        SourceRange range,
                                        std::string_view context_label,
                                        const TypeExpectation &expectation) const {
        if (is_error_type(source) || is_error_type(target)) {
            return true;
        }

        if (ahfl::is_assignable_to(source, target, relations_)) {
            return true;
        }

        std::vector<Diagnostic::Related> notes;
        append_multi_declaration_notes(
            notes, collect_nominal_declarations(target, resolve_result_.symbol_table),
            target.describe(), "expected type");
        append_multi_declaration_notes(
            notes, collect_nominal_declarations(source, resolve_result_.symbol_table),
            source.describe(), "actual type");
        append_nominal_declared_here_note(
            notes, collect_nominal_declarations(target, resolve_result_.symbol_table),
            target.describe(), "expected type");
        append_nominal_declared_here_note(
            notes, collect_nominal_declarations(source, resolve_result_.symbol_table),
            source.describe(), "actual type");
        notes.push_back(Diagnostic::Related{
            .message = expected_type_note(target, expectation),
            .range = expectation.origin_range,
        });
        notes.push_back(Diagnostic::Related{
            .message = actual_type_note(source),
            .range = range,
        });
        typecheck_error_here(error_codes::typecheck::TypeMismatch,
                             messages::typecheck::TypeMismatch.format_with(
                                 context_label, target.describe(), source.describe()),
                             range,
                             std::move(notes));
        return false;
    }

    [[nodiscard]] MaybeCRef<ResolvedReference> find_reference(ReferenceKind kind,
                                                              SourceRange range) const {
        return resolve_result_.find_reference(kind, range, current_source_id_);
    }

    [[nodiscard]] MaybeCRef<Symbol> symbol_of(SymbolId id) const {
        return resolve_result_.symbol_table.get(id);
    }

    [[nodiscard]] MaybeCRef<Type> get_const_type(SymbolId id) const {
        return environment_.get_const_type(id);
    }

    [[nodiscard]] TypePtr resolve_type_symbol(SymbolId id, SourceRange use_range) const {
        return delegate_->resolve_type_symbol(id, use_range);
    }

    // P2 (RFC §6): resolve a closure parameter's type annotation.
    [[nodiscard]] TypePtr resolve_type_syntax(const ast::TypeSyntax &type) const {
        return delegate_->resolve_type_syntax(type);
    }

    // P2c (RFC §3.5): record a resolved fn call site for monomorphization.
    void record_fn_call_site(SymbolId fn_symbol,
                             SourceRange call_range,
                             std::vector<TypePtr> type_args) const {
        delegate_->record_fn_call_site(fn_symbol, call_range, std::move(type_args));
    }

    // P2d.S2 (RFC §3.5 / §2): check that a type satisfies the named trait
    // bound; emits a TRAIT_BOUND_NOT_SATISFIED diagnostic at the given range
    // on failure and returns false. Super-trait chains are followed so a
    // sub-trait impl also satisfies its ancestor bounds.
    [[nodiscard]] bool check_bound(const Type &subject_type,
                                   std::string_view trait_name,
                                   SourceRange range) const {
        return delegate_->check_bound(subject_type, trait_name, range);
    }

    // P3c.S5b: emit a non-error informational note at `range`. Used by the
    // three-stage method-dispatch path to leave an auditable trace so
    // consumers (tests, developer logs) can see which resolution stage
    // produced the final candidate.
    void note_here(std::string message, SourceRange range) const {
        delegate_->note(std::move(message), range);
    }

    // P2d.S2 helper: resolve a nominal type by its canonical (short) name.
    // Looks up the name in the Struct and Enum symbol tables via the
    // ResolveResult and returns a TypePtr with the nominal symbol set, or
    // nullptr when no nominal type with that name is declared.
    [[nodiscard]] TypePtr resolve_nominal_by_name(std::string_view name) const {
        const auto struct_symbol =
            resolve_result_.symbol_table.find_canonical(SymbolNamespace::Types, name);
        if (struct_symbol.has_value()) {
            const auto &sym = struct_symbol->get();
            if (sym.kind == SymbolKind::Struct) {
                return types_.struct_type(sym.canonical_name, sym.id);
            }
            if (sym.kind == SymbolKind::Enum) {
                return types_.enum_type(sym.canonical_name, sym.id);
            }
        }
        return nullptr;
    }

    [[nodiscard]] MaybeCRef<EnumTypeInfo> get_enum(const Type &type) const {
        return environment_.get_enum(type);
    }

    [[nodiscard]] MaybeCRef<StructTypeInfo> get_struct(const Type &type) const {
        return environment_.get_struct(type);
    }

    [[nodiscard]] MaybeCRef<CapabilityTypeInfo> get_capability(SymbolId id) const {
        return environment_.get_capability(id);
    }

    [[nodiscard]] MaybeCRef<AgentTypeInfo> get_agent(SymbolId id) const {
        return environment_.get_agent(id);
    }

    [[nodiscard]] MaybeCRef<PredicateTypeInfo> get_predicate(SymbolId id) const {
        return environment_.get_predicate(id);
    }

    // P2 (RFC §3.2.2): fn signature lookup for fn-call typecheck.
    [[nodiscard]] MaybeCRef<FnTypeInfo> get_fn(SymbolId id) const {
        return environment_.get_fn(id);
    }

    // P3c.S5b: opt-in flag for dispatch audit-trace notes. Mirrors
    // TypeCheckOptions::trace_dispatch. Read-only after construction.
    bool trace_dispatch{false};

    // P3c: collect impl methods that match a receiver nominal type and method
    // name. Impl selection uses normalize_type_key equality (the same
    // equivalence relation the coherence/orphan-rule and impl_index use) so
    // primitive targets like `impl Eq for Int` and compound nominal targets
    // like `impl Show for Widget` both resolve uniformly.
    //
    // P3c.S5b: in addition to concrete impl-block methods, we also collect
    // trait-declared method signatures as *synthetic* candidates. This lets
    // a method call on a type `T` resolve to the unique trait method shape
    // (stage2) even when no concrete `impl Trait for T` exists yet — the
    // stage3 bound gate then emits TRAIT_BOUND_NOT_SATISFIED because no
    // concrete impl was found, giving the user a precise "missing impl"
    // diagnostic instead of the generic UNKNOWN_CALLABLE fallback.
    //
    // Synthetic entries live in `trait_method_candidate_storage_` (mutable
    // because method_candidates is const-queried) and pointers remain valid
    // for the duration of the surrounding check_method_call.
    [[nodiscard]] std::vector<MethodCandidate>
    method_candidates(const Type &receiver_type, std::string_view method_name) const {
        std::vector<MethodCandidate> candidates;
        const std::string receiver_key = TypeEnvironment::normalize_type_key(receiver_type);
        if (receiver_key.empty()) {
            return candidates;
        }
        // The receiver's nominal symbol (if any) is used to match generic impl
        // targets like `impl<T> Option<T> { ... }` against a concrete receiver
        // like `Option<Int>`. An exact normalize_type_key equality would never
        // match in that case because the impl target carries a type-variable
        // sub-key while the receiver has a primitive/structural sub-key. The
        // nominal-head match is safe because the coherence checker already
        // guarantees at most one inherent impl per nominal-head per module,
        // and the dispatch stage 1/2 gates on candidate uniqueness anyway.
        const auto receiver_nominal = nominal_symbol_of_type(receiver_type);

        // (1) Concrete impls registered in the environment (P3 base surface).
        for (const auto &[impl_index, impl] : environment_.impls()) {
            (void)impl_index;
            if (impl.target_type == nullptr) {
                continue;
            }
            bool key_match =
                TypeEnvironment::normalize_type_key(*impl.target_type) == receiver_key;
            bool nominal_match = false;
            if (!key_match && receiver_nominal.has_value() && impl.target_symbol.has_value() &&
                *receiver_nominal == *impl.target_symbol) {
                // Generic impl (e.g. `impl<T> Option<T>`) vs concrete receiver
                // (e.g. `Option<Int>`). Treat as equivalent for candidate
                // collection; per-method unification in check_impl_method_call
                // then specialises the generic signature to the concrete type
                // arguments of the receiver.
                nominal_match = true;
            }
            if (!key_match && !nominal_match) {
                continue;
            }
            if (const auto *method = find_method_ptr(impl, method_name); method != nullptr) {
                candidates.push_back(MethodCandidate{.impl = &impl, .method = method});
            }
        }

        // (2) Trait-declared methods (P3c.S5b stage2 + stage3 gate).
        //     Contribute a synthetic candidate ONLY when no concrete impl in
        //     the environment already matches (trait, receiver_type). This
        //     prevents a duplicate candidate for programs that have both a
        //     trait declaration *and* a matching `impl Trait for Type` (the
        //     far more common case). The bound gate still fires on the
        //     concrete impl, so we lose no coverage there.
        //
        //     Synthetic entries live in `trait_method_candidate_storage_`
        //     (mutable because method_candidates is const-queried) and
        //     pointers remain valid for the duration of the surrounding
        //     check_method_call.
        trait_method_candidate_storage_.impls.clear();
        trait_method_candidate_storage_.methods.clear();
        for (const auto &[trait_id, trait] : environment_.traits()) {
            (void)trait_id;
            const auto maybe_method = trait.find_method(method_name);
            if (!maybe_method.has_value()) {
                continue;
            }
            const auto &trait_method = maybe_method->get();
            if (trait_method.params.empty()) {
                continue; // no self param → never a method call target
            }
            const auto &self_param = trait_method.params.front();
            if (self_param.type == nullptr) {
                continue;
            }
            if (TypeEnvironment::normalize_type_key(*self_param.type) != receiver_key) {
                continue;
            }
            // Skip: a concrete impl of this trait for the receiver type
            // already contributed a candidate via pass (1).
            bool concrete_impl_exists = false;
            for (const auto &[impl_index, impl] : environment_.impls()) {
                (void)impl_index;
                if (impl.is_inherent) {
                    continue;
                }
                if (!impl.trait_symbol.has_value() || *impl.trait_symbol != trait.symbol) {
                    continue;
                }
                if (impl.target_type == nullptr) {
                    continue;
                }
                if (TypeEnvironment::normalize_type_key(*impl.target_type) == receiver_key) {
                    concrete_impl_exists = true;
                    break;
                }
            }
            if (concrete_impl_exists) {
                continue;
            }
            // Build an ImplMethodInfo mirror of the trait-declared signature
            // so check_impl_method_call can use the existing arity/type/
            // effect/return-type flow verbatim. Only the fields that
            // check_impl_method_call actually reads are populated; fields
            // like `body_block_index` retain their no-body default.
            ImplMethodInfo method_info{
                .name = trait_method.name,
                .symbol = SymbolId{0},
                .params = trait_method.params,
                .return_type = trait_method.return_type,
                .return_type_range = trait_method.return_type_range,
                .type_param_names = trait_method.type_param_names,
                .effect = trait_method.effect,
                .has_body = false,
                .declaration_range = trait_method.declaration_range,
                .body_block_index = UINT32_MAX,
            };
            ImplTypeInfo impl_info{
                .index = 0,
                .is_inherent = false,
                .trait_symbol = trait.symbol,
                .trait_name = trait.canonical_name,
                .target_type = self_param.type,
                .target_symbol = nominal_symbol_of_type(*self_param.type),
                .type_param_names = trait.type_param_names,
                .methods = {},
                .assoc_items = {},
                .declaration_range = trait.declaration_range,
                .trait_ref_range = trait.declaration_range,
                .target_type_range = self_param.declaration_range,
                .source_id = std::nullopt,
                .module_name = {},
            };
            trait_method_candidate_storage_.methods.push_back(std::move(method_info));
            impl_info.methods.push_back(trait_method_candidate_storage_.methods.back());
            trait_method_candidate_storage_.impls.push_back(std::move(impl_info));

            const auto &stored_impl = trait_method_candidate_storage_.impls.back();
            const auto *stored_method = find_method_ptr(stored_impl, method_name);
            if (stored_method != nullptr) {
                candidates.push_back(MethodCandidate{.impl = &stored_impl,
                                                     .method = stored_method});
            }
        }
        return candidates;
    }

    // P2d (RFC §3.5): TypeContext access for generic fn call-site type-argument
    // inference — unify param types against argument types, then substitute the
    // inferred TypeSubstitutionMap into the declared return type so the call
    // yields a concrete (instantiated) type and the monomorphization pass
    // receives the type_args it needs to instantiate the body.
    [[nodiscard]] TypeContext &types() const noexcept {
        return types_;
    }

    [[nodiscard]] TypePtr std_container_type(std::string_view canonical_name,
                                             const std::vector<TypePtr> &arguments) const {
        const auto symbol =
            resolve_result_.symbol_table.find_canonical(SymbolNamespace::Types, canonical_name);
        if (!symbol.has_value()) {
            return nullptr;
        }

        switch (symbol->get().kind) {
        case SymbolKind::Struct:
            if (const auto info = environment_.get_struct(symbol->get().id);
                info.has_value() && info->get().type_param_names.size() != arguments.size()) {
                return nullptr;
            }
            return types_.struct_type(
                symbol->get().canonical_name, symbol->get().id, std::vector<TypePtr>(arguments));
        case SymbolKind::Enum:
            if (const auto info = environment_.get_enum(symbol->get().id);
                info.has_value() && info->get().type_param_names.size() != arguments.size()) {
                return nullptr;
            }
            return types_.enum_type(
                symbol->get().canonical_name, symbol->get().id, std::vector<TypePtr>(arguments));
        case SymbolKind::TypeAlias:
        case SymbolKind::Const:
        case SymbolKind::Capability:
        case SymbolKind::Predicate:
        case SymbolKind::Agent:
        case SymbolKind::Workflow:
        case SymbolKind::Function:
        case SymbolKind::Trait:
            return nullptr;
        }

        return nullptr;
    }

    [[nodiscard]] TypePtr
    field_access(const Type &base_type, std::string_view field_name, SourceRange range) const {
        return resolve_expression_field_access(
            base_type, field_name, range, environment_, types_, *delegate_);
    }

    [[nodiscard]] TypedValue resolve_path(const ast::PathSyntax &path,
                                          const ValueContext &context) const {
        return resolve_expression_path(path, context, environment_, types_, *delegate_);
    }

    [[nodiscard]] TypePtr
    apply_flow_narrowing(TypePtr type, const Place &place, const FlowFacts &facts) const {
        return apply_expression_flow_narrowing(std::move(type), place, facts, environment_, types_);
    }

  private:
    const ResolveResult &resolve_result_;
    std::optional<SourceId> current_source_id_;
    const TypeEnvironment &environment_;
    TypeContext &types_;
    TypeRelationContext &relations_;
    ExpressionSemaDelegate *delegate_{nullptr};
    ExpressionValueFactory values_;

    // P3c.S5b: persistent backing store for trait-declared method candidates
    // synthesized by method_candidates(). Mutable because method_candidates()
    // is logically const (it never mutates the environment / symbol table)
    // even though it allocates temporary ImplTypeInfo / ImplMethodInfo
    // copies. Callers consume the returned pointers before the next
    // method_candidates() call; the storage is cleared at the top of each
    // invocation to prevent stale-pointer reuse.
    struct TraitCandidateStorage {
        std::vector<ImplTypeInfo> impls;
        std::vector<ImplMethodInfo> methods;
    };
    mutable TraitCandidateStorage trait_method_candidate_storage_{};
};

class ExpressionChecker final {
  public:
    ExpressionChecker(ExpressionCheckerServices &services,
                      const ValueContext &context,
                      MaybeCRef<Type> expected_type,
                      const TypeExpectation *expectation)
        : services_(services), context_(context), expected_type_(expected_type),
          expectation_(expectation), values_(services.values()) {}

    [[nodiscard]] TypedValue check(const ast::ExprSyntax &expr) const {
        return std::visit(
            overloaded{
                [&](const ast::BoolLiteralExpr &) { return visit_bool_literal(expr); },
                [&](const ast::IntegerLiteralExpr &) { return visit_integer_literal(expr); },
                [&](const ast::FloatLiteralExpr &) { return visit_float_literal(expr); },
                [&](const ast::DecimalLiteralExpr &) { return visit_decimal_literal(expr); },
                [&](const ast::StringLiteralExpr &) { return visit_string_literal(expr); },
                [&](const ast::DurationLiteralExpr &) { return visit_duration_literal(expr); },
                [&](const ast::PathExpr &) { return visit_path(expr); },
                [&](const ast::QualifiedValueExpr &) { return visit_qualified_value(expr); },
                [&](const ast::CallExpr &) { return visit_call(expr); },
                [&](const ast::MethodCallExpr &) { return visit_method_call(expr); },
                [&](const ast::StructLiteralExpr &) { return visit_struct_literal(expr); },
                [&](const ast::UnaryExpr &) { return visit_unary(expr); },
                [&](const ast::BinaryExpr &) { return visit_binary(expr); },
                [&](const ast::MemberAccessExpr &) { return visit_member_access(expr); },
                [&](const ast::IndexAccessExpr &) { return visit_index_access(expr); },
                [&](const ast::GroupExpr &) { return visit_group(expr); },
                [&](const ast::MatchExpr &) { return visit_match(expr); },
                [&](const ast::LambdaExpr &) { return visit_lambda(expr); },
                // P4-02: unwrap(e) as right-hand-side expression.
                [&](const ast::UnwrapExprSyntax &) { return visit_unwrap(expr); },
            },
            expr.node);
    }

    [[nodiscard]] TypedValue visit_bool_literal(const ast::ExprSyntax &) const {
        return values_.typed(values_.make_type(TypeKind::Bool));
    }

    [[nodiscard]] TypedValue visit_integer_literal(const ast::ExprSyntax &) const {
        return values_.typed(values_.make_type(TypeKind::Int));
    }

    [[nodiscard]] TypedValue visit_float_literal(const ast::ExprSyntax &) const {
        return values_.typed(values_.make_type(TypeKind::Float));
    }

    [[nodiscard]] TypedValue visit_decimal_literal(const ast::ExprSyntax &expr) const {
        return values_.typed(
            values_.decimal_type(parse_decimal_scale(expr.as<ast::DecimalLiteralExpr>().spelling)));
    }

    [[nodiscard]] TypedValue visit_string_literal(const ast::ExprSyntax &) const {
        return values_.typed(values_.string_type());
    }

    [[nodiscard]] TypedValue visit_duration_literal(const ast::ExprSyntax &) const {
        return values_.typed(values_.make_type(TypeKind::Duration));
    }

    [[nodiscard]] TypedValue visit_path(const ast::ExprSyntax &expr) const {
        return check_path(*expr.as<ast::PathExpr>().path);
    }

    [[nodiscard]] TypedValue visit_qualified_value(const ast::ExprSyntax &expr) const {
        return check_qualified_value(expr);
    }

    [[nodiscard]] TypedValue visit_call(const ast::ExprSyntax &expr) const {
        return check_call(expr);
    }

    [[nodiscard]] TypedValue visit_method_call(const ast::ExprSyntax &expr) const {
        return check_method_call(expr);
    }

    [[nodiscard]] TypedValue visit_struct_literal(const ast::ExprSyntax &expr) const {
        return check_struct_literal(expr);
    }

    [[nodiscard]] TypedValue visit_unary(const ast::ExprSyntax &expr) const {
        return check_unary_expr(expr);
    }

    [[nodiscard]] TypedValue visit_binary(const ast::ExprSyntax &expr) const {
        return check_binary_expr(expr);
    }

    [[nodiscard]] TypedValue visit_member_access(const ast::ExprSyntax &expr) const {
        return check_member_access(expr);
    }

    [[nodiscard]] TypedValue visit_index_access(const ast::ExprSyntax &expr) const {
        return check_index_access(expr);
    }

    [[nodiscard]] TypedValue visit_group(const ast::ExprSyntax &expr) const {
        const auto &group = expr.as<ast::GroupExpr>();
        if (expectation_ != nullptr) {
            return services_.check_expr(*group.inner, context_, *expectation_);
        }
        return services_.check_expr(*group.inner, context_, expected_type_);
    }

    // P2 (RFC §6 / §2.6.3): lambda (closure) typecheck. The current P2/P4
    // closure model supports Pure closures only, represented structurally as
    // `Fn(params) -> return`. When an expected Fn type is available, it drives
    // parameter and return checking bidirectionally; otherwise annotated
    // parameter types and the body type form the structural Fn type.
    [[nodiscard]] TypedValue visit_lambda(const ast::ExprSyntax &expr) const {
        const auto &lambda = expr.as<ast::LambdaExpr>();
        const auto *expected_fn =
            expected_type_.has_value() ? expected_type_->get().get_if<types::FnT>() : nullptr;

        if (expected_fn != nullptr && expected_fn->params.size() != lambda.params.size()) {
            services_.typecheck_error_here(error_codes::typecheck::WrongArity,
                                           messages::typecheck::WrongArity.format_with(
                                               "lambda",
                                               "<closure>",
                                               std::to_string(expected_fn->params.size()),
                                               std::to_string(lambda.params.size())),
                                           expr.range);
        }

        // Bind the closure's parameters into a child value context so the
        // body can reference them by name.
        ValueContext body_context{
            .bindings = context_.bindings,
            .flow_facts = {},
            .call_context = CallContext::PureOnly,
            .current_agent = context_.current_agent,
        };
        std::vector<TypePtr> param_types;
        param_types.reserve(lambda.params.size());
        for (std::size_t index = 0; index < lambda.params.size(); ++index) {
            const auto &param = lambda.params[index];
            TypePtr param_type = nullptr;
            if (param->type) {
                param_type = services_.resolve_type_syntax(*param->type);
            } else if (expected_fn != nullptr && index < expected_fn->params.size() &&
                       expected_fn->params[index] != nullptr) {
                param_type = expected_fn->params[index]->clone();
            } else {
                param_type = values_.make_error_type();
            }
            param_types.push_back(param_type);
            body_context.bindings.emplace(param->name, param_type);
        }

        if (lambda.body) {
            TypedValue body_result;
            if (expected_fn != nullptr && expected_fn->return_type != nullptr) {
                const auto return_expectation = TypeExpectation{
                    .expected = expected_fn->return_type,
                    .origin_kind = TypeExpectationOriginKind::ReturnType,
                    .origin_range = expr.range,
                    .description = "lambda return",
                };
                body_result = services_.check_expr(*lambda.body, body_context, return_expectation);
            } else {
                body_result = services_.check_expr(*lambda.body, body_context, std::nullopt);
            }
            const auto return_type =
                (expected_fn != nullptr && expected_fn->return_type != nullptr)
                    ? expected_fn->return_type->clone()
                    : (body_result.type != nullptr ? body_result.type->clone()
                                                   : values_.make_error_type());
            auto fn_type = services_.types().fn(
                std::move(param_types), return_type, EffectJudgement::make_pure());
            return values_.typed_effect(fn_type, body_result.effect);
        }

        return values_.error_typed();
    }

    // P1b (ADT, RFC §1.6): full match typecheck.
    //
    //   1. The scrutinee must type-check to an enum type (the source of
    //      variants we narrow against). Otherwise report
    //      MATCH_SCRUTINEE_REQUIRES_ENUM.
    //   2. For each arm, walk the pattern, registering any `@`-free / explicit
    //      binding name against the corresponding payload slot type. Sub-patterns
    //      (tuple/or/literal) are checked structurally; payload arity mismatches
    //      surface MATCH_VARIANT_PAYLOAD_ARITY.
    //   3. Each arm body is checked in a context carrying the pattern bindings;
    //      the first arm's body type is the expected type for every subsequent
    //      arm body (MATCH_ARM_TYPE_MISMATCH on divergence).
    //   4. Exhaustiveness: the match is exhaustive when an arm's pattern can
    //      match every value of the scrutinee type. The first version accepts
    //      either a wildcard arm (`_`) or a binding-only arm (`x`) at any
    //      position, or full coverage of all declared variants. Anything else
    //      reports MATCH_NOT_EXHAUSTIVE with the missing variant names.
    [[nodiscard]] TypedValue visit_match(const ast::ExprSyntax &expr) const {
        const auto &match = expr.as<ast::MatchExpr>();

        const auto scrutinee = services_.check_expr(*match.scrutinee, context_, std::nullopt);
        ExprEffect joined = scrutinee.effect;

        const auto *enum_payload = scrutinee.type->get_if<types::EnumT>();
        if (enum_payload == nullptr && !is_error_type(*scrutinee.type)) {
            services_.typecheck_error_here(
                error_codes::typecheck::MatchScrutineeRequiresEnum,
                messages::typecheck::MatchScrutineeRequiresEnum.format_with(
                    scrutinee.type->describe()),
                match.scrutinee->range);
            return values_.error_typed_effect(joined);
        }

        // If the scrutinee failed to resolve to an enum, still walk the arms so
        // cascaded diagnostics stay localized; the match result is error.
        auto enum_info_owned =
            enum_payload != nullptr ? services_.get_enum(*scrutinee.type) : std::nullopt;
        // For a generic enum (Option<T>, List<T>, ...), the EnumTypeInfo in the
        // environment stores variant payload types with TypeVarT placeholders
        // keyed by the declaration's type_param_names order. When the scrutinee
        // is an *instantiated* enum (e.g. Option<List<Int>>) we must substitute
        // each TypeVarT with the actual type_args from the scrutinee, otherwise
        // pattern bindings inside variant arms leak unsubstituted TypeVarT
        // (e.g. `xs : T` instead of `xs : List<Int>`).
        //
        // Build a locally-substituted copy so the global EnumTypeInfo (shared
        // across all instantiations) is never mutated.
        std::optional<EnumTypeInfo> substituted_enum_info_storage;
        std::optional<std::reference_wrapper<const EnumTypeInfo>> enum_info = std::nullopt;
        if (enum_info_owned.has_value() && enum_payload != nullptr &&
            !enum_info_owned->get().type_param_names.empty() &&
            enum_payload->type_args.size() == enum_info_owned->get().type_param_names.size()) {
            TypeSubstitutionMap subst;
            subst.reserve(enum_payload->type_args.size());
            for (const auto *type_arg : enum_payload->type_args) {
                subst.push_back(type_arg);
            }
            EnumTypeInfo substituted = enum_info_owned->get();
            for (auto &variant : substituted.variants) {
                for (auto &slot : variant.payload) {
                    if (slot != nullptr) {
                        slot = substitute_type(slot, subst, services_.types());
                    }
                }
            }
            substituted_enum_info_storage.emplace(std::move(substituted));
            enum_info = std::cref(*substituted_enum_info_storage);
        } else {
            enum_info = enum_info_owned;
        }

        bool has_catch_all = false;
        std::unordered_set<std::string> covered_variants;
        std::optional<TypePtr> unified_body_type;
        bool unified_is_error = false;
        if (expected_type_.has_value()) {
            unified_body_type = expected_type_->get().clone();
            unified_is_error = is_error_type(**unified_body_type);
        }

        for (const auto &arm : match.arms) {
            // Build a per-arm value context that layers the pattern's bindings
            // on top of the surrounding bindings. Bindings introduced by the
            // pattern shadow any same-named outer binding (match-arm scope).
            ValueContext arm_context = context_;
            const bool arm_catches_all = lower_pattern(*arm->pattern,
                                                       scrutinee.type,
                                                       enum_info,
                                                       arm_context.bindings,
                                                       covered_variants,
                                                       arm->pattern->range);

            if (arm_catches_all) {
                has_catch_all = true;
            }

            // Optional guard: must be a pure Bool expression evaluated in the
            // arm-local binding scope. A guard with side effects degrades the
            // match's effect but does not invalidate exhaustiveness accounting.
            if (arm->guard) {
                const auto guard_value =
                    services_.check_expr(*arm->guard, arm_context, std::nullopt);
                joined = join_effects(joined, guard_value.effect);
            }

            MaybeCRef<Type> arm_expected = std::nullopt;
            if (unified_body_type.has_value()) {
                arm_expected = std::cref(**unified_body_type);
            }
            const auto body = services_.check_expr(*arm->body, arm_context, arm_expected);
            joined = join_effects(joined, body.effect);

            if (!unified_body_type.has_value()) {
                if (body.type != nullptr) {
                    unified_body_type = body.type;
                    unified_is_error = is_error_type(*body.type);
                } else {
                    unified_is_error = true;
                }
            } else if (!unified_is_error && body.type != nullptr && !is_error_type(*body.type)) {
                (void)services_.check_assignable(
                    *body.type, **unified_body_type, arm->body->range, "match arm body");
            }
        }

        // Exhaustiveness: catch-all arm short-circuits; otherwise every variant
        // of the scrutinee enum must appear in some arm's pattern.
        if (!has_catch_all) {
            if (enum_info.has_value()) {
                for (const auto &variant : enum_info->get().variants) {
                    if (covered_variants.count(variant.name) == 0) {
                        std::vector<std::string> missing;
                        missing.reserve(enum_info->get().variants.size());
                        for (const auto &v : enum_info->get().variants) {
                            if (covered_variants.count(v.name) == 0) {
                                missing.push_back(v.name);
                            }
                        }
                        std::string listing;
                        for (std::size_t i = 0; i < missing.size(); ++i) {
                            listing += (i == 0 ? "" : ", ") + missing[i];
                        }
                        services_.typecheck_error_here(
                            error_codes::typecheck::MatchNotExhaustive,
                            messages::typecheck::MatchNotExhaustive.format_with(listing),
                            expr.range);
                        break;
                    }
                }
            }
            // When the scrutinee is not an enum (error path) we skip the
            // exhaustiveness check — the MATCH_SCRUTINEE_REQUIRES_ENUM
            // diagnostic above already flags the real problem.
        }

        TypePtr result_type = unified_body_type.has_value() && !unified_is_error
                                  ? *unified_body_type
                                  : values_.make_error_type();
        return values_.typed_effect(std::move(result_type), joined);
    }

    // P4-02: `unwrap(e)` — operand must be an Option<T>; result type is T.
    // Mirrors the statement-level unwrap (P4-01) in the TypeCheckPass but
    // produces a T-valued TypedValue so the expression can appear as a
    // right-hand side, call argument, etc.
    [[nodiscard]] TypedValue visit_unwrap(const ast::ExprSyntax &expr) const {
        const auto &unwrap = expr.as<ast::UnwrapExprSyntax>();
        if (unwrap.operand == nullptr) {
            return values_.error_typed();
        }

        // Same pure gate as statement-level unwrap: the operand cannot call
        // side-effecting capabilities — option-extraction should be
        // observable only through its optional value, not side effects.
        ValueContext pure_ctx{
            .bindings = context_.bindings,
            .flow_facts = context_.flow_facts,
            .call_context = CallContext::PureOnly,
            .current_agent = context_.current_agent,
        };
        const auto operand = services_.check_expr(*unwrap.operand, pure_ctx, std::nullopt);

        TypePtr result_type = nullptr;
        const bool is_optional = [&] {
            if (operand.type == nullptr) return false;
            if (is_error_type(*operand.type)) return true;
            const auto view = stdlib_bridge::std_container_type_view(*operand.type);
            if (!view.has_value() || view->kind != stdlib_bridge::StdContainerKind::Option) {
                return false;
            }
            // Extract T from Option<T>'s instantiated template args.  The
            // nominal std::option::Option enum has exactly one type parameter
            // — that is the unwrap result type.
            if (const auto *enm = operand.type->get_if<types::EnumT>();
                enm != nullptr && !enm->type_args.empty()) {
                result_type = enm->type_args.front() != nullptr
                                  ? enm->type_args.front()->clone()
                                  : values_.make_error_type();
            } else if (const auto *var = operand.type->get_if<types::EnumVariantT>();
                       var != nullptr && !var->type_args.empty()) {
                result_type = var->type_args.front() != nullptr
                                  ? var->type_args.front()->clone()
                                  : values_.make_error_type();
            }
            return true;
        }();

        if (!is_optional) {
            std::vector<Diagnostic::Related> notes;
            if (operand.type != nullptr) {
                notes.push_back(Diagnostic::Related{
                    .message = std::string("actual type: ") + operand.type->describe(),
                    .range = unwrap.operand->range,
                });
            }
            services_.typecheck_error_here(
                error_codes::typecheck::TypeMismatch,
                std::string("unwrap operand must be of type Option<T>"),
                unwrap.operand->range,
                std::move(notes));
            return values_.error_typed_effect(operand.effect);
        }

        if (result_type == nullptr) {
            // Type extraction failed (e.g. Option was referenced but never
            // actually instantiated with T).  Tolerate the missing type so
            // diagnostics never cascade.
            result_type = values_.make_error_type();
        }

        if (!operand.is_pure) {
            // Mirror the statement-level unwrap's non-pure error.  Emit via
            // the shared typecheck-error sink so diagnostics are uniform.
            services_.typecheck_error_here(
                error_codes::typecheck::NonPureExpression,
                std::string("unwrap operand must be pure; expression effect: ") +
                    std::string(to_string(operand.effect)),
                unwrap.operand->range);
        }

        return values_.typed_effect(std::move(result_type), operand.effect);
    }

  private:
    [[nodiscard]] TypedValue check_path(const ast::PathSyntax &path) const {
        return services_.resolve_path(path, context_);
    }

    // ---------------------------------------------------------------------------
    // P1b ADT: pattern lowering for `match` arms.
    //
    // `lower_pattern` walks a PatternSyntax, recording:
    //   * payload-binding names into `bindings` (mapping name -> slot type)
    //   * matched variant names into `covered_variants` (for exhaustiveness)
    //
    // Returns true when the pattern is irrefutable at the scrutinee type — i.e.
    // a wildcard `_`, a bare binding `x` (no `@`), or a tuple pattern composed
    // entirely of irrefutable sub-patterns. Such patterns make the enclosing
    // arm a catch-all for exhaustiveness purposes.
    //
    // `scrutinee_type` is the narrowed type for this sub-pattern (the enum type
    // at the top level, the slot type inside a variant payload). `enum_info`
    // is the looked-up EnumTypeInfo for the top-level scrutinee; sub-pattern
    // recursion passes std::nullopt since nested scrutinees are not enums.
    [[nodiscard]] bool
    lower_pattern(const ast::PatternSyntax &pattern,
                  TypePtr scrutinee_type,
                  std::optional<std::reference_wrapper<const EnumTypeInfo>> enum_info,
                  BindingMap &bindings,
                  std::unordered_set<std::string> &covered_variants,
                  SourceRange range) const {
        return std::visit(
            overloaded{
                [&](const ast::LiteralPattern &) {
                    // Literal patterns only narrow (no binding); they are not
                    // catch-alls. Exhaustiveness against literals is not modeled
                    // in the first version — only variant coverage counts.
                    (void)scrutinee_type;
                    (void)enum_info;
                    (void)bindings;
                    (void)covered_variants;
                    (void)range;
                    return false;
                },
                [&](const ast::WildcardPattern &) {
                    (void)scrutinee_type;
                    (void)enum_info;
                    (void)bindings;
                    (void)covered_variants;
                    (void)range;
                    return true;
                },
                [&](const ast::BindingPattern &binding) {
                    // P1 disambiguation (Rust-style): in an enum scrutinee
                    // context, a bare identifier that names a variant of that
                    // enum is a payload-less variant pattern (covers the variant,
                    // is NOT a catch-all). A non-variant identifier is an
                    // irrefutable binding (catch-all).
                    if (enum_info.has_value() && !binding.nested &&
                        enum_info->get().has_variant(binding.name)) {
                        covered_variants.insert(std::string(binding.name));
                        return false;
                    }
                    // A bare binding matches everything and is irrefutable.
                    // Bind the name to the (narrowed) scrutinee type. When an
                    // `@`-bound nested pattern is present, recurse into it for
                    // its own bindings; the outer name still binds the whole.
                    if (!binding.name.empty()) {
                        const auto [iter, inserted] = bindings.emplace(
                            binding.name,
                            scrutinee_type != nullptr ? scrutinee_type : values_.make_error_type());
                        if (!inserted && (!iter->second || !is_error_type(*iter->second))) {
                            services_.typecheck_error_here(
                                error_codes::typecheck::MatchDuplicateBinding,
                                messages::typecheck::MatchDuplicateBinding.format_with(
                                    binding.name),
                                range);
                        }
                    }
                    if (binding.nested) {
                        (void)lower_pattern(*binding.nested,
                                            scrutinee_type,
                                            enum_info,
                                            bindings,
                                            covered_variants,
                                            binding.nested->range);
                    }
                    // A binding is a catch-all unless it is constrained by an
                    // `@`-bound variant sub-pattern.
                    if (binding.nested && binding.nested->is<ast::VariantPattern>()) {
                        return false;
                    }
                    return true;
                },
                [&](const ast::TuplePattern &tuple) {
                    // Tuple patterns are irrefutable iff every element is. The
                    // first-version match grammar uses tuple patterns only for
                    // payload positional structure; the scrutinee slot types
                    // are not tuples at this stage, so we conservatively treat
                    // each element against the same scrutinee type.
                    bool all_irrefutable = true;
                    for (const auto &element : tuple.elements) {
                        const auto sub_irrefutable = lower_pattern(*element,
                                                                   scrutinee_type,
                                                                   enum_info,
                                                                   bindings,
                                                                   covered_variants,
                                                                   element->range);
                        all_irrefutable = all_irrefutable && sub_irrefutable;
                    }
                    return all_irrefutable && !tuple.elements.empty();
                },
                [&](const ast::VariantPattern &variant) {
                    // Variant patterns only narrow; they are never catch-alls.
                    if (!enum_info.has_value()) {
                        // No enum context to validate against (scrutinee failed
                        // to resolve). Skip variant coverage bookkeeping.
                        return false;
                    }
                    const auto &segments = variant.path->segments;
                    if (segments.empty()) {
                        return false;
                    }
                    const auto &variant_name = segments.back();
                    const auto variant_info = enum_info->get().find_variant(variant_name);
                    if (!variant_info.has_value()) {
                        services_.typecheck_error_here(
                            error_codes::typecheck::MatchUnknownVariant,
                            messages::typecheck::MatchUnknownVariant.format_with(
                                variant.path->spelling(), enum_info->get().canonical_name),
                            range);
                        return false;
                    }

                    covered_variants.insert(std::string(variant_name));

                    const auto &payload = variant_info->get().payload;
                    if (variant.subpatterns.size() != payload.size()) {
                        services_.typecheck_error_here(
                            error_codes::typecheck::MatchVariantPayloadArity,
                            messages::typecheck::MatchVariantPayloadArity.format_with(
                                std::string(variant_name),
                                enum_info->get().canonical_name,
                                std::to_string(payload.size()),
                                std::to_string(variant.subpatterns.size())),
                            range);
                    }

                    const std::size_t limit = std::min(variant.subpatterns.size(), payload.size());
                    for (std::size_t index = 0; index < limit; ++index) {
                        (void)lower_pattern(*variant.subpatterns[index],
                                            payload[index],
                                            std::nullopt,
                                            bindings,
                                            covered_variants,
                                            variant.subpatterns[index]->range);
                    }
                    return false;
                },
                [&](const ast::OrPattern &or_pattern) {
                    // An or-pattern is catch-all only if every branch is. We
                    // lower every branch so each branch's variant coverage and
                    // bindings are recorded (the first-version requirement is
                    // that all branches bind equivalent names; we do not yet
                    // enforce that here — coverage is what matters for
                    // exhaustiveness).
                    bool all_irrefutable = true;
                    for (const auto &branch : or_pattern.branches) {
                        const auto sub_irrefutable = lower_pattern(*branch,
                                                                   scrutinee_type,
                                                                   enum_info,
                                                                   bindings,
                                                                   covered_variants,
                                                                   branch->range);
                        all_irrefutable = all_irrefutable && sub_irrefutable;
                    }
                    return all_irrefutable && !or_pattern.branches.empty();
                },
            },
            pattern.node);
    }

    [[nodiscard]] TypedValue check_qualified_value(const ast::ExprSyntax &expr) const {
        const auto &qualified = expr.as<ast::QualifiedValueExpr>();
        if (const auto const_reference =
                services_.find_reference(ReferenceKind::ConstValue, qualified.name->range);
            const_reference.has_value()) {
            const auto const_type = services_.get_const_type(const_reference->get().target);
            if (!const_type.has_value()) {
                services_.typecheck_error_here(
                    error_codes::typecheck::MissingConstMetadata,
                    messages::typecheck::ConstTypeInfoMissing.format_with(
                        qualified.name->spelling()),
                    expr.range);
                return values_.error_typed();
            }

            return values_.typed_effect(const_type->get().clone(), ExprEffect::ConstOnly);
        }

        const auto owner_reference =
            services_.find_reference(ReferenceKind::QualifiedValueOwnerType, qualified.name->range);
        if (!owner_reference.has_value()) {
            services_.typecheck_error_here(
                error_codes::typecheck::UnknownQualifiedValue,
                messages::typecheck::UnknownQualifiedValue.format_with(qualified.name->spelling()),
                expr.range);
            return values_.error_typed();
        }

        auto owner_type = services_.resolve_type_symbol(owner_reference->get().target, expr.range);
        if (!owner_type || !owner_type->holds<types::EnumT>()) {
            services_.typecheck_error_here(
                error_codes::typecheck::InvalidQualifiedValue,
                messages::typecheck::QualifiedValueRequiresConstOrEnumVariant.format_with(
                    qualified.name->spelling()),
                expr.range);
            return values_.error_typed();
        }

        const auto enum_info = services_.get_enum(*owner_type);
        if (!enum_info.has_value()) {
            services_.typecheck_error_here(
                error_codes::typecheck::MissingTypeMetadata,
                messages::typecheck::EnumTypeInfoMissing.format_with(owner_type->describe()),
                expr.range);
            return values_.error_typed();
        }

        const auto &segments = qualified.name->segments;
        if (segments.empty() || !enum_info->get().has_variant(segments.back())) {
            std::string message =
                messages::typecheck::UnknownEnumVariant.format_with(qualified.name->spelling());
            if (!segments.empty()) {
                std::vector<std::string> candidates;
                candidates.reserve(enum_info->get().variants.size());
                for (const auto &variant : enum_info->get().variants) {
                    candidates.push_back(variant.name);
                }
                if (const auto suggestion = suggest_name(segments.back(), candidates);
                    suggestion.has_value()) {
                    message += "; did you mean '" + *suggestion + "'?";
                }
            }
            services_.typecheck_error_here(
                error_codes::typecheck::UnknownEnumVariant, std::move(message), expr.range);
            return values_.error_typed();
        }

        // Bidirectional inference for generic enum unit variants. A bare
        // `Option::None` (or any payload-less variant of a generic enum)
        // carries no type arguments, so its type is ambiguous on its own.
        // When the surrounding context expects a particular instantiation —
        // e.g. `Option<T>` from a generic fn's return type, or `Option<Int>`
        // from a let annotation, or the arm-expected type of a match — adopt
        // that instantiation. Without this, every `Option::None` in a generic
        // context would need explicit type arguments the grammar does not
        // surface. Monomorphic enums have empty type_args and are unaffected
        // (the expected instantiation check below requires non-empty args).
        if (expected_type_.has_value()) {
            const auto *expected_enum = expected_type_->get().get_if<types::EnumT>();
            const auto *owner_enum = owner_type->get_if<types::EnumT>();
            if (expected_enum != nullptr && owner_enum != nullptr &&
                !expected_enum->type_args.empty() && expected_enum->symbol.has_value() &&
                owner_enum->symbol.has_value() && *expected_enum->symbol == *owner_enum->symbol) {
                return values_.typed_effect(expected_type_->get().clone(), ExprEffect::ConstOnly);
            }
        }

        return values_.typed_effect(std::move(owner_type), ExprEffect::ConstOnly);
    }

    [[nodiscard]] TypedValue check_struct_literal(const ast::ExprSyntax &expr) const {
        const auto &struct_lit = expr.as<ast::StructLiteralExpr>();
        const auto reference =
            services_.find_reference(ReferenceKind::TypeName, struct_lit.type_name->range);
        if (!reference.has_value()) {
            services_.typecheck_error_here(
                error_codes::typecheck::UnknownType,
                messages::typecheck::UnknownType.format_with(struct_lit.type_name->spelling()),
                expr.range);
            return values_.error_typed();
        }

        auto struct_type = services_.resolve_type_symbol(reference->get().target, expr.range);
        if (!struct_type || !struct_type->holds<types::StructT>()) {
            services_.typecheck_error_here(
                error_codes::typecheck::InvalidStructLiteralTarget,
                messages::typecheck::StructLiteralTargetRequiresStruct.format_with(
                    struct_lit.type_name->spelling()),
                expr.range);
            return values_.error_typed();
        }

        const auto struct_info = services_.get_struct(*struct_type);
        if (!struct_info.has_value()) {
            services_.typecheck_error_here(
                error_codes::typecheck::MissingTypeMetadata,
                messages::typecheck::StructTypeInfoMissing.format_with(struct_type->describe()),
                expr.range);
            return values_.error_typed();
        }

        std::unordered_set<std::string> seen_fields;
        ExprEffect effect = ExprEffect::Pure;

        for (const auto &field_init : struct_lit.fields) {
            if (!seen_fields.insert(field_init->field_name).second) {
                services_.typecheck_error_here(
                    error_codes::typecheck::DuplicateField,
                    messages::typecheck::DuplicateField.format_with(field_init->field_name),
                    field_init->range);
                continue;
            }

            const auto field = struct_info->get().find_field(field_init->field_name);
            if (!field.has_value()) {
                std::vector<std::string> candidates;
                candidates.reserve(struct_info->get().fields.size());
                for (const auto &candidate : struct_info->get().fields) {
                    candidates.push_back(candidate.name);
                }
                auto message = messages::typecheck::UnknownField.format_with(
                    field_init->field_name, struct_type->describe());
                if (const auto suggestion = suggest_name(field_init->field_name, candidates);
                    suggestion.has_value()) {
                    message += "; did you mean '" + *suggestion + "'?";
                }
                services_.typecheck_error_here(
                    error_codes::typecheck::UnknownField, std::move(message), field_init->range);
                continue;
            }

            const auto expectation = TypeExpectation{
                .expected = field->get().type,
                .origin_kind = TypeExpectationOriginKind::StructField,
                .origin_range = field->get().declaration_range,
                .description = "struct field '" + field->get().name + "'",
            };
            const auto value = services_.check_expr(*field_init->value, context_, expectation);
            effect = join_effects(effect, value.effect);
            (void)services_.check_assignable(*value.type,
                                             *field->get().type,
                                             field_init->value->range,
                                             "struct field",
                                             expectation);
        }

        for (const auto &field : struct_info->get().fields) {
            if (!seen_fields.contains(field.name)) {
                services_.typecheck_error_here(
                    error_codes::typecheck::MissingField,
                    messages::typecheck::MissingField.format_with(field.name),
                    expr.range);
            }
        }

        return values_.typed_effect(std::move(struct_type), effect);
    }


    [[nodiscard]] TypedValue check_call(const ast::ExprSyntax &expr) const {
        const auto &call = expr.as<ast::CallExpr>();

        // P2 (RFC §3.2.2): a fn call resolves to a FnCallTarget reference.
        // Check it first so a fn name takes precedence over the legacy
        // capability/predicate CallTarget reference (the resolver only writes
        // one reference kind per callee range, so the two never collide).
        if (const auto fn_reference =
                services_.find_reference(ReferenceKind::FnCallTarget, call.callee->range);
            fn_reference.has_value()) {
            return check_fn_call(expr, fn_reference->get().target);
        }

        // M3 (RFC §1.5): an enum variant constructor `EnumName::Variant(args)`.
        // The resolver registered an EnumVariantConstructor reference pointing
        // at the enum symbol; validate the variant name and payload arity here
        // and build the variant value type (inferring type_args for generic
        // enums from the argument types, mirroring generic fn call inference).
        if (const auto variant_reference =
                services_.find_reference(ReferenceKind::EnumVariantConstructor, call.callee->range);
            variant_reference.has_value()) {
            return check_enum_variant_constructor(expr, variant_reference->get().target);
        }

        const auto reference =
            services_.find_reference(ReferenceKind::CallTarget, call.callee->range);
        if (!reference.has_value()) {
            if (const auto value_call = check_fn_value_call(expr); value_call.has_value()) {
                return *value_call;
            }
            services_.typecheck_error_here(
                error_codes::typecheck::UnknownCallable,
                messages::typecheck::UnknownCallable.format_with(call.callee->spelling()),
                expr.range);
            return values_.error_typed(false);
        }

        const auto symbol = services_.symbol_of(reference->get().target);
        if (!symbol.has_value()) {
            services_.typecheck_error_here(
                error_codes::typecheck::InvalidCallableReference,
                messages::typecheck::CallTargetSymbolMissing.format_with(),
                expr.range);
            return values_.error_typed(false);
        }

        if (symbol->get().kind == SymbolKind::Capability) {
            return check_capability_call(expr, reference->get().target);
        }

        if (symbol->get().kind != SymbolKind::Predicate) {
            services_.typecheck_error_here(
                error_codes::typecheck::InvalidCallableReference,
                messages::typecheck::SymbolDoesNotNameCallable.format_with(
                    symbol->get().canonical_name),
                expr.range);
            return values_.error_typed(false);
        }

        return check_predicate_call(expr, reference->get().target);
    }

    [[nodiscard]] TypedValue check_method_call(const ast::ExprSyntax &expr) const {
        const auto &call = expr.as<ast::MethodCallExpr>();
        if (call.receiver == nullptr) {
            return values_.error_typed();
        }

        const auto receiver = services_.check_expr(*call.receiver, context_, std::nullopt);
        if (receiver.type == nullptr || is_error_type(*receiver.type)) {
            return values_.error_typed_effect(receiver.effect);
        }

        // P3c.S5b: three-stage method dispatch with explicit audit-trace notes
        // emitted at every decision point. Ordering matches the RFC §3.2.2
        // resolution priority: inherent impls shadow trait impls, a unique
        // trait candidate resolves only when no inherent impl matches, and
        // every final trait selection must also pass the bound-verification
        // gate (so callers that explicitly name `T : Trait` through a where
        // clause additionally prove the (receiver, trait) pair at the use
        // site rather than trusting candidate-count alone).
        //
        //   Stage 1 — inherent unique: 1 candidate → select; >1 → ambiguous.
        //   Stage 2 — trait unique:     1 candidate → proceed to bound check;
        //                               >1 → ambiguous; 0 → unknown callable.
        //   Stage 3 — bound check:      for the trait-selected candidate,
        //                               verify `receiver.type : candidate.trait`.
        const auto candidates = services_.method_candidates(*receiver.type, call.method);
        std::vector<MethodCandidate> inherent_candidates;
        std::vector<MethodCandidate> trait_candidates;
        for (const auto &candidate : candidates) {
            if (candidate.impl == nullptr || candidate.method == nullptr) {
                continue;
            }
            if (candidate.impl->is_inherent) {
                inherent_candidates.push_back(candidate);
            } else {
                trait_candidates.push_back(candidate);
            }
        }

        const MethodCandidate *selected = nullptr;
        bool selected_from_trait = false;

        // Tiny helper so the conditional-gating noise doesn't drown out the
        // actual dispatch logic. Notes are opt-in (TypeCheckOptions::
        // trace_dispatch) to keep success-path diagnostics empty for the
        // narrowing / effect-purity tests.
        const auto dispatch_note = [&](std::string message) {
            if (services_.trace_dispatch) {
                services_.note_here(std::move(message), expr.range);
            }
        };

        // ---- Stage 1: inherent unique ----------------------------------------
        dispatch_note("[dispatch.stage1.inherent] " +
                      method_call_name(*receiver.type, call.method) +
                      ": inherent candidates=" +
                      std::to_string(inherent_candidates.size()));
        if (inherent_candidates.size() == 1) {
            selected = &inherent_candidates.front();
            selected_from_trait = false;
            dispatch_note("[dispatch.stage1.inherent] selected unique inherent method '" +
                          inherent_candidates.front().method->name + "'");
        } else if (inherent_candidates.size() > 1) {
            dispatch_note("[dispatch.stage1.inherent] multiple inherent candidates → "
                          "AMBIGUOUS_TRAIT_IMPL");
            services_.typecheck_error_here(error_codes::typecheck::AmbiguousTraitImpl,
                                           messages::typecheck::AmbiguousTraitImpl.format_with(
                                               call.method, receiver.type->describe()),
                                           expr.range);
            return values_.error_typed_effect(receiver.effect);
        } else {
            dispatch_note("[dispatch.stage1.inherent] no inherent candidates → fall "
                          "through to trait lookup");
        }

        // ---- Stage 2: trait unique (only if stage 1 produced no match) -------
        if (selected == nullptr) {
            dispatch_note("[dispatch.stage2.trait] " +
                          method_call_name(*receiver.type, call.method) +
                          ": trait candidates=" +
                          std::to_string(trait_candidates.size()));
            if (trait_candidates.size() == 1) {
                selected = &trait_candidates.front();
                selected_from_trait = true;
                dispatch_note("[dispatch.stage2.trait] selected unique trait method '" +
                                  trait_candidates.front().method->name + "' from trait '" +
                                  trait_candidates.front().impl->trait_name + "'");
            } else if (trait_candidates.size() > 1) {
                dispatch_note("[dispatch.stage2.trait] multiple trait candidates → "
                              "AMBIGUOUS_TRAIT_IMPL");
                services_.typecheck_error_here(error_codes::typecheck::AmbiguousTraitImpl,
                                               messages::typecheck::AmbiguousTraitImpl.format_with(
                                                   call.method, receiver.type->describe()),
                                               expr.range);
                return values_.error_typed_effect(receiver.effect);
            } else {
                dispatch_note("[dispatch.stage2.trait] no trait candidates → dispatch "
                              "fails with UNKNOWN_CALLABLE");
            }
        }

        if (selected == nullptr || selected->method == nullptr) {
            services_.typecheck_error_here(error_codes::typecheck::UnknownCallable,
                                           messages::typecheck::UnknownCallable.format_with(
                                               method_call_name(*receiver.type, call.method)),
                                           expr.range);
            return values_.error_typed_effect(receiver.effect);
        }

        // ---- Stage 3: bound check (trait selections only) --------------------
        if (selected_from_trait && selected->impl != nullptr) {
            const std::string_view trait_name = selected->impl->trait_name;
            dispatch_note("[dispatch.stage3.bound] verifying bound '" +
                          receiver.type->describe() + " : " + std::string(trait_name) +
                          "' for trait dispatch target");
            const bool bound_ok = services_.check_bound(*receiver.type, trait_name, expr.range);
            if (bound_ok) {
                dispatch_note("[dispatch.stage3.bound] bound satisfied for trait '" +
                              std::string(trait_name) + "'");
            } else {
                dispatch_note("[dispatch.stage3.bound] bound NOT satisfied → "
                              "TRAIT_BOUND_NOT_SATISFIED already emitted");
                // check_bound() already emitted the primary diagnostic with
                // the exact (type, trait) pair. Return an error-typed value so
                // downstream passes don't treat the call as well-typed even if
                // error-recovery fills in a method body elsewhere.
                return values_.error_typed_effect(receiver.effect);
            }
        } else {
            dispatch_note("[dispatch.stage3.bound] inherent dispatch skips bound check");
        }

        return check_impl_method_call(expr, receiver, *selected);
    }

    [[nodiscard]] TypedValue check_impl_method_call(const ast::ExprSyntax &expr,
                                                    const TypedValue &receiver,
                                                    const MethodCandidate &candidate) const {
        const auto &call = expr.as<ast::MethodCallExpr>();
        const auto &method = *candidate.method;
        const auto method_name =
            receiver.type != nullptr ? method_call_name(*receiver.type, call.method) : call.method;

        const auto &judgement = method.effect.judgement;
        if (context_.verification_context != VerificationContext::None && !judgement.is_pure()) {
            services_.typecheck_error_here(
                error_codes::typecheck::EffectNotPure,
                messages::typecheck::EffectNotPure.format_with(method_name, to_string(judgement)),
                expr.range);
            services_.typecheck_error_here(error_codes::typecheck::NotInVerifiedSubset,
                                           messages::typecheck::NotInVerifiedSubset.format_with(
                                               method_name, "declared effect is not Pure"),
                                           expr.range);

            if (context_.verification_context == VerificationContext::InvariantSafetyLiveness &&
                judgement.kind == EffectJudgement::Kind::Nondet) {
                services_.typecheck_error_here(
                    error_codes::typecheck::NondetInInvariant,
                    messages::typecheck::NondetInInvariant.format_with(method_name),
                    expr.range);
            }
        }

        const std::size_t receiver_param_count = method.params.empty() ? 0U : 1U;
        const std::size_t expected_arg_count = method.params.size() >= receiver_param_count
                                                   ? method.params.size() - receiver_param_count
                                                   : 0U;
        if (call.arguments.size() != expected_arg_count) {
            services_.typecheck_error_here(
                error_codes::typecheck::WrongArity,
                messages::typecheck::WrongArity.format_with("method",
                                                            method_name,
                                                            std::to_string(expected_arg_count),
                                                            std::to_string(call.arguments.size())),
                expr.range);
        }

        ExprEffect effect = join_effects(expr_effect_from_judgement(judgement), receiver.effect);
        const bool is_generic = !method.type_param_names.empty();

        // P3 surface semantics: explicit call-site type arguments <A, B, ...>
        // bind ONLY to METHOD-level type parameters, never to impl-level ones.
        // Users cannot spell impl-level type params at a method call site; they
        // are always inferred from the receiver type. The candidate's
        // method.type_param_names contains IMPL-LEVEL + METHOD-LEVEL names in
        // order (needed because TypeVarT indices embedded in param/return types
        // are built against the full scope), so we offset by impl_count when
        // routing explicit args. Without this offset, `self.map<U>(f)` inside
        // impl<T> List<T> { fn map<U>(...) } would assign <U> to the FIRST
        // position in the scope (impl-level 'T') instead of method-level 'U',
        // giving "expected List<U>, got List<T>" receiver mismatches.
        const std::size_t impl_tparam_count =
            candidate.impl != nullptr ? candidate.impl->type_param_names.size() : 0u;
        const std::size_t method_only_tparam_count =
            method.type_param_names.size() >= impl_tparam_count
                ? method.type_param_names.size() - impl_tparam_count
                : 0u;
        // Sanity guard: candidate.impl->type_param_names should always be a
        // prefix of method.type_param_names (guaranteed by build_impl_types).
        // If the invariant breaks, fall back to zero offset (conservative:
        // explicit args bind to the start of the combined scope).
        const bool prefix_ok = [&] {
            if (method.type_param_names.size() < impl_tparam_count) return false;
            for (std::size_t i = 0; i < impl_tparam_count; ++i) {
                if (method.type_param_names[i] != candidate.impl->type_param_names[i]) {
                    return false;
                }
            }
            return true;
        }();
        const std::size_t explicit_tparam_offset =
            (candidate.impl != nullptr && prefix_ok) ? impl_tparam_count : 0u;

        TypeSubstitutionMap subst;
        if (is_generic) {
            subst.assign(method.type_param_names.size(), nullptr);
            if (call.type_args.size() > method_only_tparam_count) {
                services_.typecheck_error_here(error_codes::typecheck::WrongArity,
                                               messages::typecheck::WrongArity.format_with(
                                                   "method type arguments",
                                                   method_name,
                                                   std::to_string(method_only_tparam_count),
                                                   std::to_string(call.type_args.size())),
                                               expr.range);
            }
            const auto explicit_limit =
                std::min(call.type_args.size(), method_only_tparam_count);
            for (std::size_t index = 0; index < explicit_limit; ++index) {
                subst[explicit_tparam_offset + index] =
                    services_.resolve_type_syntax(*call.type_args[index]);
            }
        } else if (!call.type_args.empty()) {
            services_.typecheck_error_here(
                error_codes::typecheck::WrongArity,
                messages::typecheck::WrongArity.format_with("method type arguments",
                                                            method_name,
                                                            "0",
                                                            std::to_string(call.type_args.size())),
                expr.range);
        }

        if (!method.params.empty() && receiver.type != nullptr) {
            const auto &self_param = method.params.front();
            if (is_generic && self_param.type != nullptr) {
                unify_param_with_arg(*self_param.type, *receiver.type, subst);
            }
            const auto expected_self =
                is_generic ? substitute_type(self_param.type, subst, services_.types())
                           : self_param.type;
            if (expected_self != nullptr) {
                const auto expectation = TypeExpectation{
                    .expected = expected_self,
                    .origin_kind = TypeExpectationOriginKind::FunctionParameter,
                    .origin_range = self_param.declaration_range,
                    .description = "method receiver",
                };
                (void)services_.check_assignable(*receiver.type,
                                                 *expected_self,
                                                 call.receiver->range,
                                                 "method receiver",
                                                 expectation);
            }
        }

        const auto limit = std::min(call.arguments.size(), expected_arg_count);
        std::vector<TypePtr> arg_types(limit, nullptr);
        for (std::size_t index = 0; index < limit; ++index) {
            const auto &param = method.params[index + receiver_param_count];
            const auto expected_param =
                is_generic ? substitute_type(param.type, subst, services_.types()) : param.type;
            const auto expectation = TypeExpectation{
                .expected = expected_param,
                .origin_kind = TypeExpectationOriginKind::FunctionParameter,
                .origin_range = param.declaration_range,
                .description = "parameter '" + param.name + "'",
                .callable_name = callable_basename(method.name),
                .argument_index = index + 1,
            };
            const auto argument =
                services_.check_expr(*call.arguments[index], context_, expectation);
            effect = join_effects(effect, argument.effect);
            arg_types[index] = argument.type;
        }

        if (is_generic) {
            for (std::size_t index = 0; index < limit; ++index) {
                const auto &param = method.params[index + receiver_param_count];
                if (param.type != nullptr && arg_types[index] != nullptr) {
                    unify_param_with_arg(*param.type, *arg_types[index], subst);
                }
            }
        }

        for (std::size_t index = 0; index < limit; ++index) {
            const auto &param = method.params[index + receiver_param_count];
            if (param.type == nullptr || arg_types[index] == nullptr) {
                continue;
            }
            // g-1 Phase 2: per-argument diff note with method name.
            const auto expectation = TypeExpectation{
                .expected = param.type,
                .origin_kind = TypeExpectationOriginKind::FunctionParameter,
                .origin_range = param.declaration_range,
                .description = "parameter '" + param.name + "'",
                .callable_name = callable_basename(method.name),
                .argument_index = index + 1,
            };
            if (is_generic) {
                const auto instantiated_param =
                    substitute_type(param.type, subst, services_.types());
                (void)services_.check_assignable(*arg_types[index],
                                                 *instantiated_param,
                                                 call.arguments[index]->range,
                                                 "method argument",
                                                 expectation);
            } else {
                (void)services_.check_assignable(*arg_types[index],
                                                 *param.type,
                                                 call.arguments[index]->range,
                                                 "method argument",
                                                 expectation);
            }
        }

        TypePtr result_type = nullptr;
        if (method.return_type != nullptr) {
            result_type = is_generic ? substitute_type(method.return_type, subst, services_.types())
                                     : method.return_type;
        }
        return values_.typed_effect(
            result_type != nullptr ? result_type->clone() : values_.make_error_type(), effect);
    }

    [[nodiscard]] std::optional<TypedValue> check_fn_value_call(const ast::ExprSyntax &expr) const {
        const auto &call = expr.as<ast::CallExpr>();
        if (call.callee == nullptr || call.callee->segments.size() != 1) {
            return std::nullopt;
        }

        const auto iter = context_.bindings.find(call.callee->segments.front());
        if (iter == context_.bindings.end() || iter->second == nullptr) {
            return std::nullopt;
        }

        const auto *fn_type = iter->second->get_if<types::FnT>();
        if (fn_type == nullptr) {
            return std::nullopt;
        }

        if (!call.type_args.empty()) {
            services_.typecheck_error_here(
                error_codes::typecheck::WrongArity,
                messages::typecheck::WrongArity.format_with("function value",
                                                            call.callee->spelling(),
                                                            "0",
                                                            std::to_string(call.type_args.size())),
                expr.range);
        }

        if (call.arguments.size() != fn_type->params.size()) {
            services_.typecheck_error_here(
                error_codes::typecheck::WrongArity,
                messages::typecheck::WrongArity.format_with("function value",
                                                            call.callee->spelling(),
                                                            std::to_string(fn_type->params.size()),
                                                            std::to_string(call.arguments.size())),
                expr.range);
        }

        ExprEffect effect = expr_effect_from_judgement(fn_type->effect);
        const auto limit = std::min(call.arguments.size(), fn_type->params.size());
        for (std::size_t index = 0; index < limit; ++index) {
            const auto param_type = fn_type->params[index] != nullptr ? fn_type->params[index]
                                                                      : values_.make_error_type();
            // g-1 Phase 2: per-argument diff note for function-value calls.
            // The callable spelling is kept for correlation; FnT does not
            // carry a declared param source_range so we fall back to the
            // call expression range.
            const auto expectation = TypeExpectation{
                .expected = param_type,
                .origin_kind = TypeExpectationOriginKind::FunctionParameter,
                .origin_range = expr.range,
                .description = "function value parameter",
                .callable_name = callable_basename(call.callee->spelling()),
                .argument_index = index + 1,
            };
            const auto argument =
                services_.check_expr(*call.arguments[index], context_, expectation);
            effect = join_effects(effect, argument.effect);
            if (argument.type == nullptr) {
                continue;
            }
            (void)services_.check_assignable(*argument.type,
                                             *param_type,
                                             call.arguments[index]->range,
                                             "function value argument",
                                             expectation);
        }

        return values_.typed_effect(fn_type->return_type != nullptr ? fn_type->return_type->clone()
                                                                    : values_.make_error_type(),
                                    effect);
    }

    // P2 (RFC §3.2.2 / §2.6.1): resolve a fn call against its FnTypeInfo
    // signature. Matches positional arguments to declared params, applies the
    // declared effect clause to derive the call-site ExprEffect, substitutes
    // explicit and inferred type arguments for generic fns, and records the
    // concrete instantiation for monomorphization.
    [[nodiscard]] TypedValue check_fn_call(const ast::ExprSyntax &expr, SymbolId target) const {
        const auto &call = expr.as<ast::CallExpr>();
        const auto fn = services_.get_fn(target);
        if (!fn.has_value()) {
            services_.typecheck_error_here(
                error_codes::typecheck::MissingCallableMetadata,
                messages::typecheck::CallTargetSymbolMissing.format_with(),
                expr.range);
            return values_.error_typed();
        }

        // P4a (RFC corelib-effect-system.zh.md §4.5): verified-subset effect
        // check. When the expression context is a contract clause, an
        // invariant, or a safety/liveness formula, function calls must be
        // effect Pure. If the declared effect is not Pure, emit the specific
        // EffectNotPure diagnostic plus the umbrella NotInVerifiedSubset
        // code. For invariant/safety/liveness contexts, Nondet functions
        // additionally get a NondetInInvariant diagnostic (§4.2).
        if (context_.verification_context != VerificationContext::None) {
            const auto &judgement = fn->get().effect.judgement;
            if (!judgement.is_pure()) {
                const auto callee_name = call.callee->spelling();
                services_.typecheck_error_here(error_codes::typecheck::EffectNotPure,
                                               messages::typecheck::EffectNotPure.format_with(
                                                   callee_name, to_string(judgement)),
                                               expr.range);
                services_.typecheck_error_here(error_codes::typecheck::NotInVerifiedSubset,
                                               messages::typecheck::NotInVerifiedSubset.format_with(
                                                   callee_name, "declared effect is not Pure"),
                                               expr.range);

                if (context_.verification_context == VerificationContext::InvariantSafetyLiveness &&
                    judgement.kind == EffectJudgement::Kind::Nondet) {
                    services_.typecheck_error_here(
                        error_codes::typecheck::NondetInInvariant,
                        messages::typecheck::NondetInInvariant.format_with(callee_name),
                        expr.range);
                }
            }
        }

        // P5.6a: @builtin("list_from_array") / @builtin("set_from_array") /
        // @builtin("map_from_entries") are variadic — the runtime evaluator
        // accepts any arity (they are the compiler lowering targets for
        // sugar-style collection literals). Skip arity checking for these.
        const bool is_variadic_collection_builtin =
            fn->get().builtin_name.has_value() &&
            (*fn->get().builtin_name == "list_from_array" ||
             *fn->get().builtin_name == "set_from_array" ||
             *fn->get().builtin_name == "map_from_entries");
        if (!is_variadic_collection_builtin &&
            call.arguments.size() != fn->get().params.size()) {
            services_.typecheck_error_here(
                error_codes::typecheck::WrongArity,
                messages::typecheck::WrongArity.format_with("function",
                                                            call.callee->spelling(),
                                                            std::to_string(fn->get().params.size()),
                                                            std::to_string(call.arguments.size())),
                expr.range);
        }

        // RFC §2.6.1: the call-site effect is the join of the declared fn
        // effect and each argument's effect. P4a derives the declared
        // contribution from the signature-level EffectJudgement (RFC §2.2 /
        // §6.1): Pure projects to ExprEffect::Pure, Nondet remains distinct
        // for body-effect recovery, and CapabilitySet widens to
        // ExternalEffect. Bottom (recovery) is treated as Pure so a
        // previously-recovered fn does not spuriously taint callers.
        ExprEffect declared_effect = expr_effect_from_judgement(fn->get().effect.judgement);

        ExprEffect effect = declared_effect;
        const auto limit = std::min(call.arguments.size(), fn->get().params.size());
        const bool is_generic = !fn->get().type_param_names.empty();
        TypeSubstitutionMap subst;
        if (is_generic) {
            subst.assign(fn->get().type_param_names.size(), nullptr);
            if (call.type_args.size() > fn->get().type_param_names.size()) {
                services_.typecheck_error_here(
                    error_codes::typecheck::WrongArity,
                    messages::typecheck::WrongArity.format_with(
                        "function type arguments",
                        call.callee->spelling(),
                        std::to_string(fn->get().type_param_names.size()),
                        std::to_string(call.type_args.size())),
                    expr.range);
            }
            const auto explicit_limit =
                std::min(call.type_args.size(), fn->get().type_param_names.size());
            for (std::size_t index = 0; index < explicit_limit; ++index) {
                subst[index] = services_.resolve_type_syntax(*call.type_args[index]);
            }
        } else if (!call.type_args.empty()) {
            services_.typecheck_error_here(
                error_codes::typecheck::WrongArity,
                messages::typecheck::WrongArity.format_with("function type arguments",
                                                            call.callee->spelling(),
                                                            "0",
                                                            std::to_string(call.type_args.size())),
                expr.range);
        }

        // Type-check every argument carrying the declared param as the expected
        // type so bidirectional inference still applies (e.g. an `Option::None`
        // argument adopts the param's `Option<T>`). Argument types are
        // collected first; assignability is checked after generic type-argument
        // inference so the check runs against the instantiated param type
        // (`Option<Int>`) rather than the raw `Option<T>` signature.
        std::vector<TypePtr> arg_types(limit, nullptr);
        for (std::size_t index = 0; index < limit; ++index) {
            const auto &param = fn->get().params[index];
            const auto expected_param =
                is_generic ? substitute_type(param.type, subst, services_.types()) : param.type;
            const auto expectation = TypeExpectation{
                .expected = expected_param,
                .origin_kind = TypeExpectationOriginKind::FunctionParameter,
                .origin_range = param.declaration_range,
                .description = "parameter '" + param.name + "'",
                .callable_name = callable_basename(call.callee->spelling()),
                .argument_index = index + 1,
            };
            const auto argument =
                services_.check_expr(*call.arguments[index], context_, expectation);
            effect = join_effects(effect, argument.effect);
            arg_types[index] = argument.type;
        }

        // P2d (RFC §3.5): for a generic fn, infer the type arguments by
        // unifying each declared param type with the corresponding argument
        // type, then substitute them into the param types (for the
        // assignability check) and the return type (for the call's result).
        if (is_generic) {
            for (std::size_t index = 0; index < limit; ++index) {
                const auto &param = fn->get().params[index];
                if (param.type != nullptr && arg_types[index] != nullptr) {
                    unify_param_with_arg(*param.type, *arg_types[index], subst);
                }
            }
        }

        // Record the call site exactly once with concrete type_args from
        // explicit syntax plus inference (empty for non-generic fns) so
        // monomorphization can instantiate the body.
        services_.record_fn_call_site(target, expr.range, subst);

        // P2d.S2 (RFC §3.5 / §2): where-bound checking at the call site. For
        // every `where Subject: Trait1 + Trait2 + ...` entry in the callee's
        // signature, resolve the subject type parameter to the concrete type
        // supplied at this call site and verify the environment contains a
        // trait impl (or super-trait impl) for the (type, trait) pair. Each
        // unsatisfied bound emits a TRAIT_BOUND_NOT_SATISFIED diagnostic at
        // the call range plus a note at the bound's declaration range.
        //
        // Bounds whose subject is not a declared type parameter (e.g. a
        // forward-reference to a non-generic type) are skipped conservatively:
        // the signature-resolution pass will have already rejected them as
        // malformed if they referenced unknown types.
        if (!fn->get().where_clause.bounds.empty()) {
            const auto &param_names = fn->get().type_param_names;
            for (const auto &bound : fn->get().where_clause.bounds) {
                TypePtr subject_type = nullptr;
                // Resolve the bound subject: match the subject name against
                // the callee's type parameter list using the (already
                // substituted) generic argument vector. Non-generic callees
                // still carry type_param_names (empty) and fall through to
                // the nominal lookup branch.
                if (!param_names.empty() && !subst.empty()) {
                    auto it = std::find(param_names.begin(), param_names.end(), bound.subject_name);
                    if (it != param_names.end()) {
                        const std::size_t index = static_cast<std::size_t>(it - param_names.begin());
                        if (index < subst.size()) {
                            subject_type = subst[index];
                        }
                    }
                }
                if (subject_type == nullptr) {
                    // Subject did not resolve via the generic parameter map:
                    // fall back to a canonical-name lookup using the subject
                    // name as a nominal type name (covers the rare case of a
                    // concrete-type where constraint on a non-generic fn).
                    subject_type = services_.resolve_nominal_by_name(bound.subject_name);
                }
                if (subject_type == nullptr) {
                    continue;
                }
                for (const auto &trait_name : bound.trait_names) {
                    // Primary diagnostic is emitted by check_bound at the
                    // call expression range; the diagnostic message embeds
                    // the bound subject name, trait name and actual
                    // resolved type so users can correlate the failure
                    // with the fn's where clause.
                    (void)services_.check_bound(*subject_type, trait_name, expr.range);
                }
            }
        }

        // Assignability check against the (instantiated, for generic fns) param.
        for (std::size_t index = 0; index < limit; ++index) {
            const auto &param = fn->get().params[index];
            if (param.type == nullptr || arg_types[index] == nullptr) {
                continue;
            }
            // g-1 Phase 2: surface the 1-based argument index and callable
            // name in the TypeMismatch note so users can navigate from the
            // call-site directly to the parameter declaration.
            const auto expectation = TypeExpectation{
                .expected = param.type,
                .origin_kind = TypeExpectationOriginKind::FunctionParameter,
                .origin_range = param.declaration_range,
                .description = "parameter '" + param.name + "'",
                .callable_name = callable_basename(call.callee->spelling()),
                .argument_index = index + 1,
            };
            if (is_generic) {
                const auto instantiated_param =
                    substitute_type(param.type, subst, services_.types());
                (void)services_.check_assignable(*arg_types[index],
                                                 *instantiated_param,
                                                 call.arguments[index]->range,
                                                 "function argument",
                                                 expectation);
            } else {
                (void)services_.check_assignable(*arg_types[index],
                                                 *param.type,
                                                 call.arguments[index]->range,
                                                 "function argument",
                                                 expectation);
            }
        }

        // Result type: instantiate the return type for generic fns.
        TypePtr result_type = nullptr;
        if (fn->get().return_type != nullptr) {
            result_type = is_generic
                              ? substitute_type(fn->get().return_type, subst, services_.types())
                              : fn->get().return_type;
        }
        return values_.typed_effect(
            result_type != nullptr ? result_type->clone() : values_.make_error_type(), effect);
    }

    // M3 (RFC §1.5): typecheck an enum variant constructor call
    // `EnumName::Variant(arg0, arg1, ...)`. The resolver already confirmed the
    // callee's owner is an enum and registered an EnumVariantConstructor
    // reference pointing at the enum symbol; here we validate the variant
    // name, check the payload arity and argument types, infer type_args for a
    // generic enum (mirroring generic fn call inference), and build the
    // variant value type (EnumVariantT), which is a subtype of the enum
    // instantiation (EnumT) so it flows into let bindings and match scrutinees.
    [[nodiscard]] TypedValue check_enum_variant_constructor(const ast::ExprSyntax &expr,
                                                            SymbolId enum_symbol) const {
        const auto &call = expr.as<ast::CallExpr>();
        auto owner_type = services_.resolve_type_symbol(enum_symbol, expr.range);
        if (!owner_type || !owner_type->holds<types::EnumT>()) {
            services_.typecheck_error_here(
                error_codes::typecheck::InvalidCallableReference,
                messages::typecheck::CallTargetSymbolMissing.format_with(),
                expr.range);
            return values_.error_typed(false);
        }
        const auto enum_info = services_.get_enum(*owner_type);
        if (!enum_info.has_value()) {
            services_.typecheck_error_here(
                error_codes::typecheck::MissingTypeMetadata,
                messages::typecheck::EnumTypeInfoMissing.format_with(owner_type->describe()),
                expr.range);
            return values_.error_typed(false);
        }

        const auto &segments = call.callee->segments;
        const auto variant = enum_info->get().find_variant(segments.back());
        if (!variant.has_value()) {
            services_.typecheck_error_here(
                error_codes::typecheck::UnknownEnumVariant,
                messages::typecheck::UnknownEnumVariant.format_with(call.callee->spelling()),
                expr.range);
            return values_.error_typed(false);
        }

        if (call.arguments.size() != variant->get().payload.size()) {
            services_.typecheck_error_here(error_codes::typecheck::WrongArity,
                                           messages::typecheck::WrongArity.format_with(
                                               "enum variant",
                                               call.callee->spelling(),
                                               std::to_string(variant->get().payload.size()),
                                               std::to_string(call.arguments.size())),
                                           expr.range);
        }

        const bool is_generic = !enum_info->get().type_param_names.empty();
        const auto limit = std::min(call.arguments.size(), variant->get().payload.size());

        // Type-check arguments against the declared payload types (carrying
        // each payload type as the expected type so bidirectional inference
        // applies to payload arguments too).
        ExprEffect effect = ExprEffect::Pure;
        std::vector<TypePtr> arg_types(limit, nullptr);
        for (std::size_t index = 0; index < limit; ++index) {
            const auto &payload_type = variant->get().payload[index];
            const auto expectation = TypeExpectation{
                .expected = payload_type,
                .origin_kind = TypeExpectationOriginKind::FunctionParameter,
                .origin_range = variant->get().declaration_range,
                .description = "enum variant payload",
            };
            const auto argument =
                services_.check_expr(*call.arguments[index], context_, expectation);
            effect = join_effects(effect, argument.effect);
            arg_types[index] = argument.type;
        }

        // For a generic enum, infer type_args by unifying payload types with
        // argument types, then check assignability against the instantiated
        // payload types (same pattern as generic fn call inference).
        TypeSubstitutionMap subst;
        if (is_generic) {
            subst.assign(enum_info->get().type_param_names.size(), nullptr);
            for (std::size_t index = 0; index < limit; ++index) {
                const auto &payload_type = variant->get().payload[index];
                if (payload_type != nullptr && arg_types[index] != nullptr) {
                    unify_param_with_arg(*payload_type, *arg_types[index], subst);
                }
            }
            if (expected_type_.has_value()) {
                const auto *expected_enum = expected_type_->get().get_if<types::EnumT>();
                const auto *owner_enum = owner_type->get_if<types::EnumT>();
                if (expected_enum != nullptr && owner_enum != nullptr &&
                    expected_enum->symbol.has_value() && owner_enum->symbol.has_value() &&
                    *expected_enum->symbol == *owner_enum->symbol &&
                    expected_enum->type_args.size() == subst.size()) {
                    // Drive generic enum variant instantiation from the
                    // surrounding expected type when it carries concrete type
                    // arguments. Two cases:
                    //   (a) subst[i] is still nullptr  → argument inference
                    //       never pinned this parameter, adopt from context
                    //       (original behaviour).
                    //   (b) expected_type_args[i] is concrete (not a TypeVar)
                    //       → even if arguments inferred a different concrete
                    //       type (e.g. `let v: Option<String> = Some(1)` where
                    //       the payload pins T := Int), the declared target
                    //       type wins so assignability diagnostics surface.
                    // When the expected type's argument is itself a TypeVar
                    // (generic enclosing scope) we never clobber a pre-pinned
                    // inference: that would produce spurious "T vs T" errors
                    // for unrelated generic variants used during stdlib
                    // expansion.
                    for (std::size_t index = 0; index < subst.size(); ++index) {
                        const auto *expected_arg = expected_enum->type_args[index];
                        if (expected_arg == nullptr) {
                            continue;
                        }
                        if (subst[index] == nullptr ||
                            !expected_arg->holds<types::TypeVarT>()) {
                            subst[index] = expected_arg;
                        }
                    }
                }
            }
        }
        for (std::size_t index = 0; index < limit; ++index) {
            const auto &payload_type = variant->get().payload[index];
            if (payload_type == nullptr || arg_types[index] == nullptr) {
                continue;
            }
            // g-1 Phase 2: per-argument diff note for enum variant constructors.
            const auto expectation = TypeExpectation{
                .expected = payload_type,
                .origin_kind = TypeExpectationOriginKind::FunctionParameter,
                .origin_range = variant->get().declaration_range,
                .description = "enum variant payload",
                .callable_name = std::string(segments.back()),
                .argument_index = index + 1,
            };
            const auto effective_payload =
                is_generic ? substitute_type(payload_type, subst, services_.types()) : payload_type;
            (void)services_.check_assignable(*arg_types[index],
                                             *effective_payload,
                                             call.arguments[index]->range,
                                             "enum variant payload",
                                             expectation);
        }

        const auto *owner_enum = owner_type->get_if<types::EnumT>();
        const auto canonical_name =
            owner_enum != nullptr ? owner_enum->canonical_name : std::string{};
        const auto result_type = services_.types().enum_variant_type(
            canonical_name, std::string(segments.back()), enum_symbol, subst);
        if (expected_type_.has_value()) {
            const auto *expected_enum = expected_type_->get().get_if<types::EnumT>();
            if (expected_enum != nullptr && owner_enum != nullptr &&
                expected_enum->symbol.has_value() && owner_enum->symbol.has_value() &&
                *expected_enum->symbol == *owner_enum->symbol) {
                return values_.typed_effect(expected_type_->get().clone(), effect);
            }
        }
        return values_.typed_effect(result_type->clone(), effect);
    }

    [[nodiscard]] TypedValue check_capability_call(const ast::ExprSyntax &expr,
                                                   SymbolId target) const {
        const auto &call = expr.as<ast::CallExpr>();
        const auto capability = services_.get_capability(target);
        if (!capability.has_value()) {
            services_.typecheck_error_here(
                error_codes::typecheck::MissingCallableMetadata,
                messages::typecheck::CapabilityTypeInfoMissing.format_with(call.callee->spelling()),
                expr.range);
            return values_.error_typed(false);
        }

        if (context_.call_context != CallContext::Flow) {
            services_.typecheck_error_here(
                error_codes::typecheck::CapabilityNotAllowed,
                messages::typecheck::CapabilityNotAllowed.format_with(call.callee->spelling()),
                expr.range);
        }

        if (context_.current_agent.has_value()) {
            const auto agent_info = services_.get_agent(*context_.current_agent);
            if (agent_info.has_value()) {
                const auto allowed = std::find(agent_info->get().capability_symbols.begin(),
                                               agent_info->get().capability_symbols.end(),
                                               target);
                if (allowed == agent_info->get().capability_symbols.end()) {
                    services_.typecheck_error_here(
                        error_codes::typecheck::CapabilityNotAllowed,
                        messages::typecheck::CapabilityNotDeclared.format_with(
                            call.callee->spelling()),
                        expr.range);
                }
            }
        }

        if (call.arguments.size() != capability->get().params.size()) {
            services_.typecheck_error_here(error_codes::typecheck::WrongArity,
                                           messages::typecheck::WrongArity.format_with(
                                               "capability",
                                               call.callee->spelling(),
                                               std::to_string(capability->get().params.size()),
                                               std::to_string(call.arguments.size())),
                                           expr.range);
        }

        const auto limit = std::min(call.arguments.size(), capability->get().params.size());
        for (std::size_t index = 0; index < limit; ++index) {
            const auto &param = capability->get().params[index];
            // g-1 Phase 2: per-argument diff note with capability name.
            const auto expectation = TypeExpectation{
                .expected = param.type,
                .origin_kind = TypeExpectationOriginKind::FunctionParameter,
                .origin_range = param.declaration_range,
                .description = "parameter '" + param.name + "'",
                .callable_name = callable_basename(call.callee->spelling()),
                .argument_index = index + 1,
            };
            const auto argument =
                services_.check_expr(*call.arguments[index], context_, expectation);
            (void)services_.check_assignable(*argument.type,
                                             *param.type,
                                             call.arguments[index]->range,
                                             "capability argument",
                                             expectation);
        }

        return values_.typed_effect(capability->get().return_type
                                        ? capability->get().return_type->clone()
                                        : values_.make_error_type(),
                                    ExprEffect::CapabilityCall);
    }

    [[nodiscard]] TypedValue check_predicate_call(const ast::ExprSyntax &expr,
                                                  SymbolId target) const {
        const auto &call = expr.as<ast::CallExpr>();
        const auto predicate = services_.get_predicate(target);
        if (!predicate.has_value()) {
            services_.typecheck_error_here(
                error_codes::typecheck::MissingCallableMetadata,
                messages::typecheck::PredicateTypeInfoMissing.format_with(call.callee->spelling()),
                expr.range);
            return values_.error_typed();
        }

        if (call.arguments.size() != predicate->get().params.size()) {
            services_.typecheck_error_here(error_codes::typecheck::WrongArity,
                                           messages::typecheck::WrongArity.format_with(
                                               "predicate",
                                               call.callee->spelling(),
                                               std::to_string(predicate->get().params.size()),
                                               std::to_string(call.arguments.size())),
                                           expr.range);
        }

        ExprEffect effect = ExprEffect::PredicateCall;
        const auto limit = std::min(call.arguments.size(), predicate->get().params.size());
        for (std::size_t index = 0; index < limit; ++index) {
            const auto &param = predicate->get().params[index];
            // g-1 Phase 2: per-argument diff note with predicate name.
            const auto expectation = TypeExpectation{
                .expected = param.type,
                .origin_kind = TypeExpectationOriginKind::FunctionParameter,
                .origin_range = param.declaration_range,
                .description = "parameter '" + param.name + "'",
                .callable_name = callable_basename(call.callee->spelling()),
                .argument_index = index + 1,
            };
            const auto argument =
                services_.check_expr(*call.arguments[index], context_, expectation);
            effect = join_effects(effect, argument.effect);
            if (!argument.is_pure) {
                services_.typecheck_error_here(
                    error_codes::typecheck::NonPureExpression,
                    messages::typecheck::PredicateArgsNotPure.format_with() +
                        "; expression effect: " + std::string(to_string(argument.effect)),
                    call.arguments[index]->range);
            }

            (void)services_.check_assignable(*argument.type,
                                             *param.type,
                                             call.arguments[index]->range,
                                             "predicate argument",
                                             expectation);
        }

        return values_.typed_effect(values_.make_type(TypeKind::Bool), effect);
    }

    [[nodiscard]] TypedValue check_unary_expr(const ast::ExprSyntax &expr) const {
        const auto &unary = expr.as<ast::UnaryExpr>();
        const auto operand = services_.check_expr(*unary.operand, context_, std::nullopt);
        switch (unary.op) {
        case ast::ExprUnaryOp::Not:
            if (!is_bool_type(*operand.type) && !is_error_type(*operand.type)) {
                services_.typecheck_error_here(
                    error_codes::typecheck::InvalidOperation,
                    messages::typecheck::LogicalNotRequiresBool.format_with(
                        operand.type->describe()),
                    expr.range);
            }
            return values_.typed_effect(values_.make_type(TypeKind::Bool), operand.effect);
        case ast::ExprUnaryOp::Negate:
        case ast::ExprUnaryOp::Positive:
            if (!is_numeric_type(*operand.type) && !is_error_type(*operand.type)) {
                services_.typecheck_error_here(
                    error_codes::typecheck::InvalidOperation,
                    messages::typecheck::NumericUnaryRequiresNumeric.format_with(
                        operand.type->describe()),
                    expr.range);
            }
            return values_.typed_effect(
                operand.type ? operand.type->clone() : values_.make_error_type(), operand.effect);
        }

        return values_.error_typed_effect(operand.effect);
    }

    [[nodiscard]] TypedValue check_binary_expr(const ast::ExprSyntax &expr) const {
        const auto &binary = expr.as<ast::BinaryExpr>();
        const auto is_option_none_value = [&](const ast::ExprSyntax &candidate) {
            if (!candidate.is<ast::QualifiedValueExpr>()) {
                return false;
            }
            const auto &qualified = candidate.as<ast::QualifiedValueExpr>();
            if (qualified.name == nullptr || qualified.name->segments.empty() ||
                qualified.name->segments.back() != "None") {
                return false;
            }
            const auto owner_reference = services_.find_reference(
                ReferenceKind::QualifiedValueOwnerType, qualified.name->range);
            if (!owner_reference.has_value()) {
                return false;
            }
            const auto owner_type =
                services_.resolve_type_symbol(owner_reference->get().target, candidate.range);
            if (owner_type == nullptr) {
                return false;
            }
            const auto view = stdlib_bridge::std_container_type_view(*owner_type);
            return view.has_value() &&
                   view->kind == stdlib_bridge::StdContainerKind::Option && view->nominal;
        };
        if ((binary.op == ast::ExprBinaryOp::Equal || binary.op == ast::ExprBinaryOp::NotEqual) &&
            binary.lhs && binary.rhs &&
            (is_option_none_value(*binary.lhs) || is_option_none_value(*binary.rhs))) {
            const auto &none_operand =
                is_option_none_value(*binary.lhs) ? *binary.lhs : *binary.rhs;
            const auto &value_operand =
                is_option_none_value(*binary.lhs) ? *binary.rhs : *binary.lhs;
            const auto value = services_.check_expr(value_operand, context_, std::nullopt);
            const auto none = services_.check_expr(none_operand, context_, std::cref(*value.type));
            const auto effect = join_effects(value.effect, none.effect);
            const auto optional = stdlib_bridge::std_container_type_view(*value.type);
            if (!is_error_type(*value.type) &&
                (!optional.has_value() ||
                 optional->kind != stdlib_bridge::StdContainerKind::Option)) {
                services_.typecheck_error_here(
                    error_codes::typecheck::InvalidOperation,
                    messages::typecheck::NoneComparisonRequiresOptional.format_with(
                        value.type->describe()),
                    expr.range);
            }
            return values_.typed_effect(values_.make_type(TypeKind::Bool), effect);
        }


        const auto lhs = services_.check_expr(*binary.lhs, context_, std::nullopt);
        const auto rhs = services_.check_expr(*binary.rhs, context_, std::nullopt);
        const auto effect = join_effects(lhs.effect, rhs.effect);

        const auto comparable = [&]() {
            return is_error_type(*lhs.type) || is_error_type(*rhs.type) ||
                   is_subtype_of(*lhs.type, *rhs.type) || is_subtype_of(*rhs.type, *lhs.type);
        };

        const auto strict_arithmetic = [&](ast::ExprBinaryOp op) -> TypePtr {
            if (is_error_type(*lhs.type) || is_error_type(*rhs.type)) {
                return values_.make_error_type();
            }

            const bool lhs_int = lhs.type->holds<types::IntT>();
            const bool rhs_int = rhs.type->holds<types::IntT>();
            const bool lhs_float = lhs.type->holds<types::FloatT>();
            const bool rhs_float = rhs.type->holds<types::FloatT>();
            const auto *lhs_dec = lhs.type->get_if<types::DecimalT>();
            const auto *rhs_dec = rhs.type->get_if<types::DecimalT>();

            if (lhs_int && rhs_int) {
                return values_.make_type(TypeKind::Int);
            }
            if (lhs_float && rhs_float) {
                return values_.make_type(TypeKind::Float);
            }
            if ((op == ast::ExprBinaryOp::Add || op == ast::ExprBinaryOp::Subtract) &&
                lhs_dec != nullptr && rhs_dec != nullptr && lhs_dec->scale == rhs_dec->scale) {
                return lhs.type->clone();
            }
            return nullptr;
        };

        switch (binary.op) {
        case ast::ExprBinaryOp::Implies:
        case ast::ExprBinaryOp::Or:
        case ast::ExprBinaryOp::And:
            if ((!is_bool_type(*lhs.type) || !is_bool_type(*rhs.type)) &&
                !is_error_type(*lhs.type) && !is_error_type(*rhs.type)) {
                services_.typecheck_error_here(
                    error_codes::typecheck::InvalidOperation,
                    messages::typecheck::LogicalOperatorRequiresBool.format_with(),
                    expr.range);
            }
            return values_.typed_effect(values_.make_type(TypeKind::Bool), effect);
        case ast::ExprBinaryOp::Equal:
        case ast::ExprBinaryOp::NotEqual:
        case ast::ExprBinaryOp::Less:
        case ast::ExprBinaryOp::LessEqual:
        case ast::ExprBinaryOp::Greater:
        case ast::ExprBinaryOp::GreaterEqual:
            if (!comparable()) {
                services_.typecheck_error_here(
                    error_codes::typecheck::InvalidOperation,
                    messages::typecheck::ComparisonOperandsIncompatible.format_with(
                        lhs.type->describe(), rhs.type->describe()),
                    expr.range);
            }
            return values_.typed_effect(values_.make_type(TypeKind::Bool), effect);
        case ast::ExprBinaryOp::Add:
            if (lhs.type->holds<types::StringT>() && rhs.type->holds<types::StringT>()) {
                return values_.typed_effect(values_.string_type(), effect);
            }

            if (const auto result = strict_arithmetic(binary.op); result != nullptr) {
                return values_.typed_effect(result, effect);
            }

            if (!is_error_type(*lhs.type) && !is_error_type(*rhs.type)) {
                services_.typecheck_error_here(error_codes::typecheck::InvalidOperation,
                                               messages::typecheck::InvalidOperator.format_with(
                                                   "+", lhs.type->describe(), rhs.type->describe()),
                                               expr.range);
            }
            return values_.error_typed_effect(effect);
        case ast::ExprBinaryOp::Subtract:
            if (const auto result = strict_arithmetic(binary.op); result != nullptr) {
                return values_.typed_effect(result, effect);
            }

            if (!is_error_type(*lhs.type) && !is_error_type(*rhs.type)) {
                services_.typecheck_error_here(error_codes::typecheck::InvalidOperation,
                                               messages::typecheck::InvalidOperator.format_with(
                                                   "-", lhs.type->describe(), rhs.type->describe()),
                                               expr.range);
            }
            return values_.error_typed_effect(effect);
        case ast::ExprBinaryOp::Multiply:
        case ast::ExprBinaryOp::Divide:
            if (const auto result = strict_arithmetic(binary.op); result != nullptr) {
                return values_.typed_effect(result, effect);
            }

            if (!is_error_type(*lhs.type) && !is_error_type(*rhs.type)) {
                services_.typecheck_error_here(
                    error_codes::typecheck::InvalidOperation,
                    messages::typecheck::ArithmeticOperatorInvalid.format_with(
                        lhs.type->describe(), rhs.type->describe()),
                    expr.range);
            }
            return values_.error_typed_effect(effect);
        case ast::ExprBinaryOp::Modulo:
            if (lhs.type->holds<types::IntT>() && rhs.type->holds<types::IntT>()) {
                return values_.typed_effect(values_.make_type(TypeKind::Int), effect);
            }

            if (!is_error_type(*lhs.type) && !is_error_type(*rhs.type)) {
                services_.typecheck_error_here(error_codes::typecheck::InvalidOperation,
                                               messages::typecheck::ModuloRequiresInt.format_with(),
                                               expr.range);
            }
            return values_.error_typed_effect(effect);
        }

        return values_.error_typed_effect(effect);
    }

    [[nodiscard]] TypedValue check_member_access(const ast::ExprSyntax &expr) const {
        const auto &member_access = expr.as<ast::MemberAccessExpr>();
        const auto base = services_.check_expr(*member_access.base, context_, std::nullopt);
        auto member_type = services_.field_access(*base.type, member_access.member, expr.range);
        if (const auto place = place_of_expression(expr); place.has_value()) {
            member_type = services_.apply_flow_narrowing(member_type, *place, context_.flow_facts);
        }
        return values_.typed_effect(member_type, base.effect);
    }

    [[nodiscard]] TypedValue check_index_access(const ast::ExprSyntax &expr) const {
        const auto &index_access = expr.as<ast::IndexAccessExpr>();
        const auto collection = services_.check_expr(*index_access.base, context_, std::nullopt);
        const auto index = services_.check_expr(*index_access.index, context_, std::nullopt);
        const auto effect = join_effects(collection.effect, index.effect);

        const auto collection_view = stdlib_bridge::std_container_type_view(*collection.type);
        if (collection_view.has_value() &&
            collection_view->kind == stdlib_bridge::StdContainerKind::List) {
            if (!index.type->holds<types::IntT>() && !is_error_type(*index.type)) {
                services_.typecheck_error_here(
                    error_codes::typecheck::InvalidIndexAccess,
                    messages::typecheck::ListIndexRequiresInt.format_with(),
                    index_access.index->range);
            }

            if (collection_view->first != nullptr) {
                return values_.typed_effect(collection_view->first->clone(), effect);
            }

            return values_.error_typed_effect(effect);
        }

        if (collection_view.has_value() &&
            collection_view->kind == stdlib_bridge::StdContainerKind::Map) {
            if (collection_view->first != nullptr) {
                (void)services_.check_assignable(
                    *index.type, *collection_view->first, index_access.index->range, "map index");
            }

            if (collection_view->second != nullptr) {
                return values_.typed_effect(collection_view->second->clone(), effect);
            }

            return values_.error_typed_effect(effect);
        }

        if (!is_error_type(*collection.type)) {
            services_.typecheck_error_here(
                error_codes::typecheck::InvalidIndexAccess,
                messages::typecheck::IndexTargetRequiresCollection.format_with(
                    collection.type->describe()),
                expr.range);
        }

        return values_.error_typed_effect(effect);
    }

    ExpressionCheckerServices &services_;
    const ValueContext &context_;
    MaybeCRef<Type> expected_type_;
    const TypeExpectation *expectation_{nullptr};
    const ExpressionValueFactory &values_;
};

ExpressionSema::ExpressionSema(ExpressionSemaServices services) : services_(std::move(services)) {}

ExpressionValue ExpressionSema::check(const ast::ExprSyntax &expr,
                                      const ExpressionContext &context,
                                      MaybeCRef<Type> expected_type) const {
    ExpressionCheckerServices services{
        *services_.resolve_result,
        services_.current_source_id,
        *services_.environment,
        *services_.types,
        *services_.relations,
        *services_.delegate,
        services_.trace_dispatch,
    };
    return ExpressionChecker{services, context, expected_type, nullptr}.check(expr);
}

ExpressionValue ExpressionSema::check(const ast::ExprSyntax &expr,
                                      const ExpressionContext &context,
                                      const TypeExpectation &expectation) const {
    MaybeCRef<Type> expected_type = std::nullopt;
    if (expectation.expected != nullptr) {
        expected_type = std::cref(*expectation.expected);
    }
    ExpressionCheckerServices services{
        *services_.resolve_result,
        services_.current_source_id,
        *services_.environment,
        *services_.types,
        *services_.relations,
        *services_.delegate,
        services_.trace_dispatch,
    };
    return ExpressionChecker{services, context, expected_type, &expectation}.check(expr);
}

TypedValue TypeCheckPass::check_expr(const ast::ExprSyntax &expr,
                                     const ValueContext &context,
                                     MaybeCRef<Type> expected_type) {
    auto value = check_expr_impl(expr, context, expected_type);
    remember_expression_type(expr, value);
    return value;
}

TypedValue TypeCheckPass::check_expr(const ast::ExprSyntax &expr,
                                     const ValueContext &context,
                                     const TypeExpectation &expectation) {
    MaybeCRef<Type> expected_type = std::nullopt;
    if (expectation.expected != nullptr) {
        expected_type = std::cref(*expectation.expected);
    }
    auto value = check_expr_impl(expr, context, expected_type, &expectation);
    remember_expression_type(expr, value);
    return value;
}

TypedValue TypeCheckPass::check_expr_impl(const ast::ExprSyntax &expr,
                                          const ValueContext &context,
                                          MaybeCRef<Type> expected_type,
                                          const TypeExpectation *expectation) {
    auto type_resolver = make_type_resolver();

    class PassExpressionSemaDelegate final : public ExpressionSemaDelegate {
      public:
        PassExpressionSemaDelegate(TypeCheckPass &pass, TypeResolver &type_resolver)
            : pass_(&pass), type_resolver_(&type_resolver) {}

        void typecheck_error(ErrorCode<DiagnosticCategory::TypeCheck> code,
                             std::string message,
                             SourceRange range) override {
            pass_->typecheck_error_here(code, std::move(message), range);
        }

        void typecheck_error(ErrorCode<DiagnosticCategory::TypeCheck> code,
                             std::string message,
                             SourceRange range,
                             std::vector<Diagnostic::Related> notes) override {
            pass_->typecheck_error_here(code, std::move(message), range, std::move(notes));
        }

        ExpressionValue check_nested(const ast::ExprSyntax &nested_expr,
                                     const ExpressionContext &nested_context,
                                     MaybeCRef<Type> nested_expected) override {
            return pass_->check_expr(nested_expr, nested_context, nested_expected);
        }

        ExpressionValue check_nested(const ast::ExprSyntax &nested_expr,
                                     const ExpressionContext &nested_context,
                                     const TypeExpectation &nested_expectation) override {
            return pass_->check_expr(nested_expr, nested_context, nested_expectation);
        }

        TypePtr resolve_type_symbol(SymbolId id, SourceRange range) override {
            return type_resolver_->resolve_type_symbol(id, range);
        }

        // P2 (RFC §6): resolve a closure parameter's type annotation through
        // the shared TypeCheckPass resolver so the current symbol / module
        // context is honoured.
        TypePtr resolve_type_syntax(const ast::TypeSyntax &type) override {
            return pass_->resolve_type_syntax(type);
        }

        // P2c (RFC §3.5): forward the recorded fn call site to the
        // TypeCheckPass so it lands in the typed program's fn_call_sites.
        void record_fn_call_site(SymbolId fn_symbol,
                                 SourceRange call_range,
                                 std::vector<TypePtr> type_args) override {
            pass_->record_fn_call_site(fn_symbol, call_range, std::move(type_args));
        }

        // P2d.S2 (RFC §3.5 / §2): forward the bound check to the TypeCheckPass
        // implementation (which also emits the diagnostic on failure).
        bool check_bound(const Type &subject_type,
                         std::string_view trait_name,
                         SourceRange range) override {
            return pass_->check_bound(subject_type, trait_name, range);
        }

        // P3c.S5b: forward informational notes to the TypeCheckPass reporter
        // so three-stage method-dispatch audit lines appear in the diagnostic
        // bag alongside real errors.
        void note(std::string message, SourceRange range) override {
            pass_->note_here(std::move(message), range);
        }

      private:
        TypeCheckPass *pass_{nullptr};
        TypeResolver *type_resolver_{nullptr};
    };

    PassExpressionSemaDelegate delegate{*this, type_resolver};
    ExpressionSema sema{ExpressionSemaServices{
        .resolve_result = &resolve_result_,
        .current_source_id = current_source_id_,
        .environment = &result_.environment,
        .types = types_,
        .relations = &relations_,
        .delegate = &delegate,
        .trace_dispatch = options_.trace_dispatch,
    }};
    if (expectation != nullptr) {
        return sema.check(expr, context, *expectation);
    }
    return sema.check(expr, context, expected_type);
}

TypedValue TypeCheckPass::check_path(const ast::PathSyntax &path, const ValueContext &context) {
    class PathDelegate final : public ExpressionSemaDelegate {
      public:
        explicit PathDelegate(TypeCheckPass &pass) : pass_(&pass) {}

        void typecheck_error(ErrorCode<DiagnosticCategory::TypeCheck> code,
                             std::string message,
                             SourceRange range) override {
            pass_->typecheck_error_here(code, std::move(message), range);
        }

        void typecheck_error(ErrorCode<DiagnosticCategory::TypeCheck> code,
                             std::string message,
                             SourceRange range,
                             std::vector<Diagnostic::Related> notes) override {
            pass_->typecheck_error_here(code, std::move(message), range, std::move(notes));
        }

        ExpressionValue
        check_nested(const ast::ExprSyntax &, const ExpressionContext &, MaybeCRef<Type>) override {
            return ExpressionValue{
                .type = pass_->make_error_type(),
                .effect = ExprEffect::Pure,
                .is_pure = true,
                .path_root_kind = std::nullopt,
            };
        }

        ExpressionValue check_nested(const ast::ExprSyntax &,
                                     const ExpressionContext &,
                                     const TypeExpectation &) override {
            return ExpressionValue{
                .type = pass_->make_error_type(),
                .effect = ExprEffect::Pure,
                .is_pure = true,
                .path_root_kind = std::nullopt,
            };
        }

        TypePtr resolve_type_symbol(SymbolId id, SourceRange range) override {
            return pass_->resolve_type_symbol(id, range);
        }

        // P2 (RFC §6): path resolution does not enter a closure body, so a
        // type annotation resolution request is rare here; forward it to the
        // TypeCheckPass resolver for correctness when it does occur.
        TypePtr resolve_type_syntax(const ast::TypeSyntax &type) override {
            return pass_->resolve_type_syntax(type);
        }

        // P2c (RFC §3.5): path resolution never resolves a fn call site, so
        // the recording hook is a no-op here. (The fn-call typecheck path
        // runs through the PassExpressionSemaDelegate, which does forward.)
        void record_fn_call_site(SymbolId /*fn_symbol*/,
                                 SourceRange /*call_range*/,
                                 std::vector<TypePtr> /*type_args*/) override {}

        // P2d.S2 (RFC §3.5 / §2): path resolution never performs a where-bound
        // check; the no-op returns false without emitting a diagnostic.
        bool check_bound(const Type & /*subject_type*/,
                         std::string_view /*trait_name*/,
                         SourceRange /*range*/) override {
            return false;
        }

        // P3c.S5b: path resolution never selects a method call, so the note
        // sink is a no-op.
        void note(std::string /*message*/, SourceRange /*range*/) override {}

      private:
        TypeCheckPass *pass_{nullptr};
    };

    PathDelegate delegate{*this};
    return resolve_expression_path(path, context, result_.environment, *types_, delegate);
}

} // namespace ahfl
