#include "verification/formal/smv_output.hpp"

#include <cstddef>
#include <optional>
#include <string_view>

#include "verification/formal/smv_text.hpp"

namespace ahfl::formal {

namespace {

constexpr std::string_view ltl_bound_prefix = "no counterexample found with bound ";
constexpr std::string_view invar_bound_prefix = "no proof or counterexample found with bound ";

/// Extracts the numeric bound from a "-- ... found with bound <n>" line.
[[nodiscard]] std::optional<std::size_t> parse_bound(std::string_view line,
                                                     std::string_view prefix) {
    const auto position = line.find(prefix);
    if (position == std::string_view::npos) {
        return std::nullopt;
    }
    std::size_t value = 0;
    std::size_t index = position + prefix.size();
    bool saw_digit = false;
    while (index < line.size() && line[index] >= '0' && line[index] <= '9') {
        value = value * 10 + static_cast<std::size_t>(line[index] - '0');
        saw_digit = true;
        ++index;
    }
    return saw_digit ? std::optional{value} : std::nullopt;
}

/// Accumulates the bound-progress lines NuSMV prints while checking one
/// bounded specification. The counter restarts at 0 between specifications,
/// which is the only signal separating consecutive bounded LTL passes (the
/// checker never names the formula on those runs).
struct BoundedGroup {
    enum class Family { Ltl, Invar };

    Family family{Family::Ltl};
    std::size_t max_bound{0};
};

[[nodiscard]] SmvSpecificationVerdict
flush_bounded_verdict(const BoundedGroup &group) {
    SmvSpecificationVerdict verdict;
    verdict.bound = group.max_bound;
    if (group.family == BoundedGroup::Family::Ltl) {
        verdict.kind = SmvSpecificationVerdictKind::BoundedPass;
        verdict.summary =
            "-- bounded LTL specification: no counterexample found with bound 0.." +
            std::to_string(group.max_bound);
    } else {
        verdict.kind = SmvSpecificationVerdictKind::Inconclusive;
        verdict.summary =
            "-- bounded invariant search found no proof or counterexample with bound 0.." +
            std::to_string(group.max_bound);
    }
    return verdict;
}

} // namespace

SmvOutputParseResult parse_smv_specification_results(const std::string_view output) {
    SmvOutputParseResult result;
    std::optional<BoundedGroup> pending;

    auto flush_pending = [&]() {
        if (pending.has_value()) {
            result.specifications.push_back(flush_bounded_verdict(*pending));
            pending.reset();
        }
    };

    auto start_or_extend_group = [&](const BoundedGroup::Family family, const std::size_t bound) {
        if (!pending.has_value()) {
            pending = BoundedGroup{.family = family, .max_bound = bound};
            return;
        }
        if (pending->family != family || (bound == 0 && pending->max_bound > 0)) {
            // The bound counter restarted: the previous specification ended
            // without a terminal verdict line.
            flush_pending();
            pending = BoundedGroup{.family = family, .max_bound = bound};
            return;
        }
        if (bound > pending->max_bound) {
            pending->max_bound = bound;
        }
    };

    // A terminal verdict ("is true" / "is false" / "cannot prove") closes the
    // pending bounded group when it belongs to the same family (e.g. an LTL
    // refutation after "bound 0" progress lines); a family change (bounded LTL
    // pass followed by the invariant phase) flushes the pending group first so
    // the bounded pass is not swallowed.
    auto emit_terminal = [&](const SmvSpecificationVerdictKind kind,
                             const BoundedGroup::Family family, const std::string &line) {
        std::size_t bound = 0;
        if (pending.has_value() && pending->family == family) {
            bound = pending->max_bound;
            pending.reset();
        } else {
            flush_pending();
        }
        SmvSpecificationVerdict verdict;
        verdict.kind = kind;
        verdict.summary = line;
        verdict.bound = bound;
        result.specifications.push_back(std::move(verdict));
    };

    for (const std::string &raw_line : split_smv_lines(output)) {
        const std::string line = smv_trim_copy(raw_line);
        const std::string lowered = smv_lower_copy(line);

        if (auto bound = parse_bound(lowered, invar_bound_prefix)) {
            start_or_extend_group(BoundedGroup::Family::Invar, *bound);
            continue;
        }
        if (auto bound = parse_bound(lowered, ltl_bound_prefix)) {
            start_or_extend_group(BoundedGroup::Family::Ltl, *bound);
            continue;
        }

        // "cannot prove ... is true or false" contains both "is true" and
        // "false", so classify it before the unconditional verdict lines.
        if (smv_contains(lowered, "cannot prove the invariant")) {
            emit_terminal(SmvSpecificationVerdictKind::Inconclusive,
                          BoundedGroup::Family::Invar, line);
        } else if (smv_contains(lowered, " is false")) {
            const auto family = smv_contains(lowered, "invariant")
                                    ? BoundedGroup::Family::Invar
                                    : BoundedGroup::Family::Ltl;
            emit_terminal(SmvSpecificationVerdictKind::Refuted, family, line);
        } else if (smv_contains(lowered, " is true")) {
            const auto family = smv_contains(lowered, "invariant")
                                    ? BoundedGroup::Family::Invar
                                    : BoundedGroup::Family::Ltl;
            emit_terminal(SmvSpecificationVerdictKind::Proven, family, line);
        }
    }

    flush_pending();
    return result;
}

} // namespace ahfl::formal
