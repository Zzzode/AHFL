// P2c (RFC §3.2.2 / §3.5 / §5): monomorphization pass implementation.
//
// See the header (monomorphization.hpp) for the design. This file implements
// the cache-key construction, dedup, and budget enforcement described in
// corelib-type-system.zh.md §5.

#include "ahfl/compiler/semantics/monomorphization.hpp"

#include <string>
#include <string_view>
#include <unordered_map>

namespace ahfl {

namespace {

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

} // namespace ahfl
