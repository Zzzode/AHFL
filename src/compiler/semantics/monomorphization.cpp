// P2c (RFC §3.2.2 / §3.5 / §5): monomorphization pass implementation.
//
// See the header (monomorphization.hpp) for the design. This file implements
// the cache-key construction, dedup, and budget enforcement described in
// corelib-type-system.zh.md §5, plus the P2d type substitution helper
// (substitute_type) used for instantiating generic fn bodies.

#include "ahfl/compiler/semantics/monomorphization.hpp"

#include "ahfl/compiler/semantics/type_context.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace ahfl {

namespace {

// Look up the FnTypeInfo for a given symbol id in the typed program's
// declarations list. Returns nullopt when the symbol is not a fn declaration
// or is not found.
[[nodiscard]] std::optional<std::reference_wrapper<const FnTypeInfo>>
find_fn_type_info(const TypedProgram &program, SymbolId fn_symbol) {
    for (const auto &decl : program.declarations) {
        if (decl.symbol == fn_symbol &&
            std::holds_alternative<FnTypeInfo>(decl.payload)) {
            return std::cref(std::get<FnTypeInfo>(decl.payload));
        }
    }
    return std::nullopt;
}

// RFC §5.2: a stdlib-originated declaration is one whose canonical name is
// rooted at the `std::` module. Used to attribute its instances to the larger
// stdlib budget (§5.3) instead of the user budget.
[[nodiscard]] bool is_stdlib_decl(std::string_view canonical_name) noexcept {
    return canonical_name.size() >= 5 && canonical_name.substr(0, 5) == "std::";
}

// Render a single instance name from a decl name and its canonical type-args.
// `app::sum_squares` + `Int` -> `app::sum_squares<Int>`. A non-generic
// instance (empty type args) keeps the bare decl name, matching the RFC's
// "one canonical instance for a non-generic fn" rule.
[[nodiscard]] std::string render_instance_name(std::string_view decl_name,
                                               std::string_view type_args) {
    if (type_args.empty()) {
        return std::string{decl_name};
    }
    std::string name;
    name.reserve(decl_name.size() + 1 + type_args.size() + 1);
    name.append(decl_name);
    name.push_back('<');
    name.append(type_args);
    name.push_back('>');
    return name;
}

} // namespace

[[nodiscard]] std::string canonical_type_args(const std::vector<TypePtr> &type_args) {
    if (type_args.empty()) {
        return {};
    }
    std::string rendered;
    bool first = true;
    for (const auto type_arg : type_args) {
        if (!first) {
            rendered += ", ";
        }
        first = false;
        rendered += (type_arg != nullptr) ? type_arg->describe() : std::string{"?"};
    }
    return rendered;
}

[[nodiscard]] MonomorphizationResult
run_monomorphization(const TypedProgram &program, MonomorphizationOptions options) {
    MonomorphizationResult result;

    // RFC §5.2 cache key: (decl canonical id, type_args canonical). The
    // where-clause substituted component collapses to True in P2c because
    // where-clause evaluation is part of the generic-body typecheck work
    // (there are no conditional impls yet), so the (decl, type_args) pair is
    // a complete key today.
    struct InstanceKey {
        std::string decl_canonical_name;
        std::string type_args_canonical;

        [[nodiscard]] bool operator==(const InstanceKey &) const noexcept = default;
    };
    struct InstanceKeyHash {
        [[nodiscard]] std::size_t operator()(const InstanceKey &key) const noexcept {
            std::size_t h = std::hash<std::string>{}(key.decl_canonical_name);
            h ^= std::hash<std::string>{}(key.type_args_canonical) + 0x9e3779b9 +
                 (h << 6) + (h >> 2);
            return h;
        }
    };

    std::unordered_map<InstanceKey, std::uint32_t, InstanceKeyHash> cache;

    for (const auto &site : program.fn_call_sites) {
        const auto symbol = program.find_symbol(site.fn_symbol);
        if (!symbol.has_value()) {
            // A call site whose fn symbol is not in the snapshot cannot be
            // attributed to a declaration; skip it rather than synthesising a
            // bogus cache entry. The typecheck pass records only resolved
            // FnCallTarget references, so this branch is defensive.
            continue;
        }

        const auto &fn_symbol = symbol->get();
        const auto decl_canonical_name = fn_symbol.canonical_name;
        const auto type_args_canonical = canonical_type_args(site.type_args);
        const auto stdlib = is_stdlib_decl(decl_canonical_name);

        const InstanceKey key{decl_canonical_name, type_args_canonical};
        if (const auto found = cache.find(key); found != cache.end()) {
            // RFC §5.1 / §5.3 rule 2: a cache hit reuses the existing typed
            // instance at no budget cost. The instance is already in
            // `result.instances`; nothing more to record.
            continue;
        }

        // Budget enforcement (RFC §5.3). Each *new* instance counts +1 against
        // its originating budget (user or stdlib). Once exceeded the pass
        // stops counting further sites against that budget but keeps walking
        // so the result reflects the full call-site diversity.
        const auto budget =
            stdlib ? kMonomorphizationStdlibBudget : kMonomorphizationUserBudget;
        auto &counter = stdlib ? result.stdlib_instance_count : result.user_instance_count;
        if (options.enforce_budget && counter >= budget) {
            // Mark the result as over-budget and continue collecting so the
            // "largest contributors" hint is accurate. The over-budget site is
            // recorded as a cache entry so repeated identical sites still dedup
            // instead of inflating the reported diversity.
            cache.emplace(key, static_cast<std::uint32_t>(result.instances.size()));
            continue;
        }

        MonomorphizationInstance instance{
            .decl_canonical_name = decl_canonical_name,
            .fn_symbol = site.fn_symbol,
            .type_args_canonical = type_args_canonical,
            .instance_name = render_instance_name(decl_canonical_name, type_args_canonical),
            .from_stdlib = stdlib,
        };
        result.instances.push_back(std::move(instance));
        ++counter;
        ++result.instances_per_decl[decl_canonical_name];
        cache.emplace(key, static_cast<std::uint32_t>(result.instances.size() - 1));
    }

    return result;
}

// P2d overload: also instantiate each generic fn's typed body.
// We reuse the P2c pass to build the instance set, then walk the result and
// deep-clone + substitute bodies for every instance whose source fn has a
// typed body block and concrete type arguments.
[[nodiscard]] MonomorphizationResult
run_monomorphization(TypedProgram &program, TypeContext &types, MonomorphizationOptions options) {
    // Step 1: build the deduped instance set using the P2c logic.
    auto result = run_monomorphization(const_cast<const TypedProgram &>(program), options);

    // Step 2: for each instance, try to instantiate the body.
    // We skip:
    //   - non-generic instances (empty type_args) — no substitution needed,
    //     the original body is already typed with concrete types;
    //   - fns with no body block (prototypes/builtins).
    for (auto &instance : result.instances) {
        const auto fn_info = find_fn_type_info(program, instance.fn_symbol);
        if (!fn_info.has_value()) {
            continue;
        }
        const auto &info = fn_info->get();

        // No body — skip (e.g. prototype declaration).
        if (info.body_block_index == UINT32_MAX) {
            continue;
        }

        // Non-generic: the original body is already concrete. Point the
        // instance at the original block directly rather than cloning.
        if (info.type_param_names.empty()) {
            instance.body_block_index = info.body_block_index;
            continue;
        }

        // Generic with type args: build substitution map and instantiate.
        // First find a matching call site to recover the TypePtr-typed
        // type_args vector. O(n) scan is fine — fn_call_sites is small in
        // P2-scale programs.
        const std::vector<TypePtr> *type_args = nullptr;
        for (const auto &site : program.fn_call_sites) {
            if (site.fn_symbol == instance.fn_symbol &&
                canonical_type_args(site.type_args) == instance.type_args_canonical) {
                type_args = &site.type_args;
                break;
            }
        }
        if (type_args == nullptr) {
            continue;
        }

        if (type_args->size() != info.type_param_names.size()) {
            continue;
        }

        TypeSubstitutionMap subst;
        subst.reserve(info.type_param_names.size());
        for (std::size_t i = 0; i < info.type_param_names.size(); ++i) {
            subst.push_back((*type_args)[i]);
        }

        const auto inst = instantiate_fn_body(program, info.body_block_index, subst, types);
        instance.body_block_index = inst.body_block_index;
    }

    return result;
}

// ---------------------------------------------------------------------------
// Type substitution (P2d)
// ---------------------------------------------------------------------------

namespace {

// Helper: recursively substitute type vars in a type. Returns an interned
// TypePtr from the given TypeContext.
//
// Substitution uses index-based lookup (industry-standard: Rust Substs,
// Swift GenericTypeParamKey): O(1) vector access by TypeVarT::index, no
// string hashing, no name-confusion bugs.
[[nodiscard]] TypePtr substitute(TypePtr type,
                                 const TypeSubstitutionMap &subst,
                                 TypeContext &types) {
    if (type == nullptr) {
        return nullptr;
    }

    return type->visit(types::Overloads{
        [&](const types::TypeVarT &tv) -> TypePtr {
            if (tv.index < subst.size() && subst[tv.index] != nullptr) {
                return subst[tv.index];
            }
            // Not in substitution map — keep the TypeVar as-is.
            return types.type_var(tv.index, tv.name);
        },
        [&](const types::OptionalT &opt) -> TypePtr {
            return types.optional(substitute(opt.inner, subst, types));
        },
        [&](const types::ListT &list) -> TypePtr {
            return types.list(substitute(list.element, subst, types));
        },
        [&](const types::SetT &set) -> TypePtr {
            return types.set(substitute(set.element, subst, types));
        },
        [&](const types::MapT &map) -> TypePtr {
            return types.map(substitute(map.key, subst, types),
                             substitute(map.value, subst, types));
        },
        [&](const types::FnT &fn) -> TypePtr {
            std::vector<TypePtr> new_params;
            new_params.reserve(fn.params.size());
            for (const auto &p : fn.params) {
                new_params.push_back(substitute(p, subst, types));
            }
            return types.fn(std::move(new_params),
                            substitute(fn.return_type, subst, types),
                            fn.effect);
        },
        [&](const types::StructT &s) -> TypePtr {
            if (s.type_args.empty()) {
                return type;
            }
            std::vector<TypePtr> new_args;
            new_args.reserve(s.type_args.size());
            for (const auto *a : s.type_args) {
                new_args.push_back(substitute(a, subst, types));
            }
            return types.struct_type(s.canonical_name, s.symbol, std::move(new_args));
        },
        [&](const types::EnumT &e) -> TypePtr {
            if (e.type_args.empty()) {
                return type;
            }
            std::vector<TypePtr> new_args;
            new_args.reserve(e.type_args.size());
            for (const auto *a : e.type_args) {
                new_args.push_back(substitute(a, subst, types));
            }
            return types.enum_type(e.canonical_name, e.symbol, std::move(new_args));
        },
        [&](const types::EnumVariantT &v) -> TypePtr {
            if (v.type_args.empty()) {
                return type;
            }
            std::vector<TypePtr> new_args;
            new_args.reserve(v.type_args.size());
            for (const auto *a : v.type_args) {
                new_args.push_back(substitute(a, subst, types));
            }
            return types.enum_variant_type(
                v.canonical_name, v.variant_name, v.symbol, std::move(new_args));
        },
        [&](const auto &) -> TypePtr {
            // Leaf type (primitives, nominal struct/enum etc.) — no change.
            return type;
        },
    });
}

} // namespace

TypePtr substitute_type(TypePtr type, const TypeSubstitutionMap &subst, TypeContext &types) {
    return substitute(type, subst, types);
}

// ---------------------------------------------------------------------------
// Typed body instantiation (P2d)
// ---------------------------------------------------------------------------

namespace {

// Cloning context: maps old indexes (in the source TypedProgram) to new
// indexes (in the destination program, which is the same program — we
// append to the end). Each flat-store kind has its own index map.
struct CloneContext {
    TypedProgram &program;
    const TypeSubstitutionMap &subst;
    TypeContext &types;

    // old index -> new index
    std::unordered_map<std::uint32_t, std::uint32_t> expr_map;
    std::unordered_map<std::uint32_t, std::uint32_t> stmt_map;
    std::unordered_map<std::uint32_t, std::uint32_t> block_map;
};

// Forward declarations.
std::uint32_t clone_expr(CloneContext &ctx, std::uint32_t old_idx);
std::uint32_t clone_stmt(CloneContext &ctx, std::uint32_t old_idx);
std::uint32_t clone_block(CloneContext &ctx, std::uint32_t old_idx);

std::uint32_t clone_expr(CloneContext &ctx, std::uint32_t old_idx) {
    if (old_idx == UINT32_MAX) {
        return UINT32_MAX;
    }

    // Cache hit?
    auto it = ctx.expr_map.find(old_idx);
    if (it != ctx.expr_map.end()) {
        return it->second;
    }

    if (old_idx >= ctx.program.expressions.size()) {
        return UINT32_MAX;
    }

    const auto &old_expr = ctx.program.expressions[old_idx];
    TypedExpr new_expr = old_expr; // shallow copy

    // Substitute the expression's type.
    new_expr.type = substitute_type(old_expr.type, ctx.subst, ctx.types);

    // Clone children recursively.
    for (auto &child : new_expr.children) {
        child.expr_index = clone_expr(ctx, child.expr_index);
    }

    // Append to flat store.
    const std::uint32_t new_idx =
        static_cast<std::uint32_t>(ctx.program.expressions.size());
    ctx.program.expressions.push_back(std::move(new_expr));
    ctx.expr_map.emplace(old_idx, new_idx);
    return new_idx;
}

std::uint32_t clone_stmt(CloneContext &ctx, std::uint32_t old_idx) {
    if (old_idx == UINT32_MAX) {
        return UINT32_MAX;
    }

    auto it = ctx.stmt_map.find(old_idx);
    if (it != ctx.stmt_map.end()) {
        return it->second;
    }

    if (old_idx >= ctx.program.statements.size()) {
        return UINT32_MAX;
    }

    const auto &old_stmt = ctx.program.statements[old_idx];
    TypedStatement new_stmt = old_stmt; // shallow copy

    // Substitute let type annotation.
    new_stmt.let_type = substitute_type(old_stmt.let_type, ctx.subst, ctx.types);

    // Clone child expressions.
    for (auto &idx : new_stmt.children_expr_index) {
        idx = clone_expr(ctx, idx);
    }

    // Clone then/else blocks (If).
    new_stmt.then_block_index = clone_block(ctx, old_stmt.then_block_index);
    new_stmt.else_block_index = clone_block(ctx, old_stmt.else_block_index);

    const std::uint32_t new_idx =
        static_cast<std::uint32_t>(ctx.program.statements.size());
    ctx.program.statements.push_back(std::move(new_stmt));
    ctx.stmt_map.emplace(old_idx, new_idx);
    return new_idx;
}

std::uint32_t clone_block(CloneContext &ctx, std::uint32_t old_idx) {
    if (old_idx == UINT32_MAX) {
        return UINT32_MAX;
    }

    auto it = ctx.block_map.find(old_idx);
    if (it != ctx.block_map.end()) {
        return it->second;
    }

    if (old_idx >= ctx.program.blocks.size()) {
        return UINT32_MAX;
    }

    const auto &old_block = ctx.program.blocks[old_idx];
    TypedBlock new_block = old_block; // shallow copy

    // Clone all statements in the block.
    new_block.statement_indexes.clear();
    new_block.statement_indexes.reserve(old_block.statement_indexes.size());
    for (const auto stmt_idx : old_block.statement_indexes) {
        new_block.statement_indexes.push_back(clone_stmt(ctx, stmt_idx));
    }

    const std::uint32_t new_idx =
        static_cast<std::uint32_t>(ctx.program.blocks.size());
    ctx.program.blocks.push_back(std::move(new_block));
    ctx.block_map.emplace(old_idx, new_idx);
    return new_idx;
}

} // namespace

BodyInstantiationResult
instantiate_fn_body(TypedProgram &program,
                    std::uint32_t body_block_index,
                    const TypeSubstitutionMap &subst,
                    TypeContext &types) {
    BodyInstantiationResult result;
    if (body_block_index >= program.blocks.size()) {
        return result;
    }

    const std::size_t expr_before = program.expressions.size();
    const std::size_t stmt_before = program.statements.size();
    const std::size_t block_before = program.blocks.size();

    CloneContext ctx{
        .program = program,
        .subst = subst,
        .types = types,
    };
    result.body_block_index = clone_block(ctx, body_block_index);

    result.expr_count = static_cast<std::uint32_t>(program.expressions.size() - expr_before);
    result.stmt_count = static_cast<std::uint32_t>(program.statements.size() - stmt_before);
    result.block_count = static_cast<std::uint32_t>(program.blocks.size() - block_before);

    return result;
}

} // namespace ahfl
