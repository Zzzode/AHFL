#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace ahfl::formal {

/// Outcome of a single specification checked by a NuSMV/nuXmv-compatible
/// checker.
enum class SmvSpecificationVerdictKind {
    /// Unconditional proof: a BDD "is true" line, or an een-sorensson
    /// inductive invariant proof.
    Proven,
    /// Refuted: an "is false" line accompanied by a counterexample trace.
    Refuted,
    /// Bounded-model-checking pass: no counterexample existed within the
    /// unrolling bound. This is NOT an unbounded proof.
    BoundedPass,
    /// Bounded invariant search exhausted the bound without either a proof or
    /// a counterexample ("cannot prove the invariant ... is true or false").
    Inconclusive,
};

struct SmvSpecificationVerdict {
    SmvSpecificationVerdictKind kind{SmvSpecificationVerdictKind::Inconclusive};
    /// Checker verdict line, or a synthesized description for bounded LTL
    /// passes whose checker output names no formula.
    std::string summary;
    /// Unrolling depth of the bounded group that produced this verdict; 0 for
    /// unconditional BDD verdicts.
    std::size_t bound{0};
};

struct SmvOutputParseResult {
    std::vector<SmvSpecificationVerdict> specifications;
};

/// Parses the per-specification verdict stream emitted by NuSMV 2.6.0 /
/// nuXmv in either BDD or BMC mode.
///
/// BMC runs do not name the formula on a bounded pass: `check_ltlspec_bmc`
/// prints only "-- no counterexample found with bound <n>" lines and emits no
/// terminal line. Specifications are therefore attributed by detecting the
/// bound-counter reset back to 0 between per-specification runs.
[[nodiscard]] SmvOutputParseResult parse_smv_specification_results(std::string_view output);

} // namespace ahfl::formal
