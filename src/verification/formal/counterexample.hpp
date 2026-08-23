#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ahfl::formal {

/// Structured source mapping parsed from AHFL_MAP comments.
struct SourceMapping {
    std::string smv_symbol;
    std::string description;
    std::string source_path;
    std::size_t begin_offset{0};
    std::size_t end_offset{0};
};

/// A single variable assignment within a counterexample state.
struct CounterexampleAssignment {
    std::string variable;
    std::string value;
    std::optional<SourceMapping> mapping;
};

/// A single state in the counterexample trace.
struct CounterexampleState {
    std::string label; // e.g. "1.1", "1.2"
    std::vector<CounterexampleAssignment> assignments;
};

/// (h-12 D1) Enhanced source attachment for a state transition step.
/// Aggregated from the `agent__<AgentName>__state` and
/// `workflow__<WfName>__node__<Node>__phase` AHFL_MAP entries that
/// change between state (i-1) and state i.
struct StateTransitionSource {
    /// Smv-level role key. Values: "agent_state", "workflow_phase", or
    /// "other" for unmapped AHFL_MAP entries.
    std::string role;
    /// e.g. "MyAgent" (from `agent__MyAgent__state`) or "Pipeline.node.dispatch".
    std::string owner_name;
    /// e.g. "Init" -> "Working".  Empty string for unknown.
    std::string value_from;
    std::string value_to;
    /// Mirrors SourceMapping.  source_path.empty() means unavailable.
    std::string source_path;
    std::size_t begin_offset{0};
    std::size_t end_offset{0};
};

/// (h-12 D2 / D3) One reconstructed AHFL-level variable projection.
///
/// D2 (trigger_input): prefix `input.` (smv_symbol prefix `input__`,
/// collected from state 1.1 assignments).
/// D3 (faulty_ctx_field): prefix `context.` (smv_symbol prefix `context__`,
/// collected from the first/last state diff).
struct ProjectedVariable {
    /// Dot-separated AHFL logical path, e.g. "input.retry_count" or
    /// "context.balance".
    std::string logical_path;
    std::string value;
    /// For D3 only: non-empty iff the value differs between the initial
    /// state and the final trace state.
    std::string initial_value;
    std::string source_path;
    std::size_t begin_offset{0};
    std::size_t end_offset{0};
};

/// Light-weight source range (mirrors SourceMapping offsets, no symbol name).
struct SourceRange {
    std::string source_path;
    std::size_t begin_offset{0};
    std::size_t end_offset{0};
    [[nodiscard]] bool empty() const {
        return source_path.empty() && begin_offset == 0 && end_offset == 0;
    }
};

/// (h-12 D4) Kind classification for violated_spec.
enum class ViolatedContractKind {
    /// Global safety: `never(BAD_STATE)` or `G (...)`.
    Invariant,
    /// Eventuality / progress obligation: `reachable(TARGET)` or `F (...)`.
    Ensures,
    /// Custom LTL/CTL the backend could not classify.
    Custom,
    /// Classification has not been run yet.
    Unknown,
};

/// (h-12 D4) Structured violated-contract metadata.
struct ViolatedContractInfo {
    ViolatedContractKind kind{ViolatedContractKind::Unknown};
    /// Raw smv-level violated spec, same as CounterexampleTrace::violated_spec.
    std::string raw_spec;
    /// Human-readable one-liner, e.g. "agent OrderBot never reaches state Paid".
    std::string description;
    /// Optional user-visible contract / invariant name.  Populated by the
    /// backend via AHFL_MAP when a named `invariant` / `ensures` block is
    /// emitted; empty otherwise.
    std::string name;
    /// (P2 §3.5) AHFL source range of the violated contract clause, recovered
    /// from the `spec__<...>` AHFL_MAP entry the SMV backend emits alongside
    /// each LTLSPEC.  Empty when the backend did not emit a mapped source for
    /// the clause (e.g. a synthesized lifecycle/control obligation, or a
    /// backend that does not carry AHFL_MAP source offsets).
    SourceRange source;
};

/// (h-12 D5) A single fired transition reconstructed from an SMV trace.
///
/// One entry per `__ahfl_transition__<wf>__<node>__<label>_fired = TRUE`
/// variable observed between state i and state i+1.  Multiple transitions
/// may fire in a single step.
struct ProjectedAction {
    /// Dot-separated AHFL path identifying the workflow/actor that owns the
    /// transition, e.g. "workflow.Checkout.node.pay".
    std::string logical_path;
    /// The transition / action label, e.g. "authorize" or "dispatch".
    std::string action_name;
    /// Value of the transition guard at the step the action fired.
    bool guard_value{false};
    /// Source location of the transition definition, if known.
    SourceRange source;
};

/// (P2 §3.5) A single fired capability call reconstructed from an SMV trace.
///
/// One entry per capability-call define whose value went TRUE at a given
/// step.  Recognized symbol shapes (both emitted by the SMV backend):
///   - `agent__<Agent>__called__<Cap>`
///   - `workflow__<Wf>__node__<Node>__call__<Cap>` (and its `__committed` /
///     `__failed` lifecycle siblings)
/// The source range is recovered from the symbol's AHFL_MAP entry when the
/// backend attached one; otherwise it stays empty (never fabricated).
struct ProjectedCapabilityCall {
    /// Dot-separated AHFL path of the caller, e.g. "agent.OrderBot" or
    /// "workflow.Checkout.node.pay".
    std::string logical_path;
    /// The capability name that was called, e.g. "charge_card".
    std::string capability_name;
    /// Source location of the capability call site, if known.
    SourceRange source;
};

/// A fully parsed counterexample trace from NuSMV output.
struct CounterexampleTrace {
    std::string trace_description;
    std::string trace_type;
    std::string violated_spec;
    std::vector<CounterexampleState> states;
    std::optional<std::size_t> loop_start_index; // index into states where loop begins

    // ============== h-12 QW-3 4-dim mapping fields ==============
    // (populated lazily by `enhance_counterexample_mapping(...)`; consumers
    // MUST treat them as optional — empty vectors just mean "not emitted
    // by this backend / not reconstructable".)

    /// (D1) One entry per state step[1..n].  Entry 0 is absent because
    /// no transition precedes the initial state — the vector therefore
    /// has states.empty() ? 0 : (states.size() - 1) elements.
    std::vector<std::vector<StateTransitionSource>> state_transitions;

    /// (D2) Input-trigger reconstruction: all assignments from state 1.1
    /// whose smv_symbol has prefix `input__`, projected to AHFL path form.
    std::vector<ProjectedVariable> trigger_input;

    /// (D3) Faulty context fields: all `context__`-prefixed variables whose
    /// value differs between the initial state and the last trace state.
    std::vector<ProjectedVariable> faulty_ctx_fields;

    /// (D5) Fired-transition trace: one element per state step[1..n].  Entry i
    /// holds the transitions that fired between state i and state i+1.  The
    /// vector length therefore matches state_transitions.length (i.e.
    /// states.empty() ? 0 : (states.size() - 1).
    std::vector<std::vector<ProjectedAction>> action_trace;

    /// (P2 §3.5) Fired-capability-call trace: one element per state step[1..n],
    /// parallel to action_trace.  Entry i holds the capability calls whose
    /// call-event define went TRUE between state i and state i+1.  Empty
    /// per-step vectors are normal; a backend that emits no capability-call
    /// symbols yields an all-empty outer vector.
    std::vector<std::vector<ProjectedCapabilityCall>> capability_calls;
};

/// Human-readable explanation of why verification failed.
struct ViolationExplanation {
    std::string summary;
    std::vector<std::string> steps;
    std::string violated_property;

    // ============== h-12 QW-3 4-dim mapping fields ==============
    /// (D4) Structured violated-contract classification.
    ViolatedContractInfo violated_contract;

    // ============== h-12 D6 natural-language expansion ==============
    /// (D6) Multi-line, heuristic natural-language summary of the failure.
    /// At most 4 lines; lines separated by `\n`.
    std::string natural_language_summary;
};

/// (h-12) Reconstruct the 4-dim projection fields of `trace` and
/// populate `explanation.violated_contract`.
///
/// - D1 state_transitions:  derived from the AHFL_MAP descriptors of the
///   `agent__*__state` / `workflow__*__node__*__phase` symbols whose
///   assignments change between consecutive states.
/// - D2 trigger_input:      initial-state `input__*` assignments, logical
///   path form.
/// - D3 faulty_ctx_fields:  `context__*` assignments with differing
///   initial/final values.
/// - D4 violated_contract:  enum + description derived from violated_spec.
///   Its `source` range is recovered from the `spec__*` AHFL_MAP entry whose
///   description matches the violated LTL formula, when present.
/// - capability_calls:      per-step fired capability-call symbols
///   (`agent__*__called__*` / `workflow__*__node__*__call__*`), with the
///   source range from their AHFL_MAP entry when the backend emits one.
///
/// `mappings` is the same AHFL_MAP index produced by
/// parse_structured_symbol_mappings; it is used to recover the violated
/// contract clause source range (D4) and is optional — pass an empty map when
/// the backend emits no AHFL_MAP source offsets and the contract source stays
/// empty.
void enhance_counterexample_mapping(
    CounterexampleTrace &trace,
    ViolationExplanation &explanation,
    const std::unordered_map<std::string, SourceMapping> &mappings = {});

/// (h-12 D4 helper) Derive ViolatedContractInfo from the raw smv
/// violated-spec string.  Never returns std::nullopt; unknown specs get
/// kind = Unknown and an empty description.
ViolatedContractInfo classify_violated_contract(std::string_view violated_spec);

/// Parse AHFL_MAP comments from SMV model text into structured mappings.
[[nodiscard]] std::unordered_map<std::string, SourceMapping>
parse_structured_symbol_mappings(std::string_view model);

/// Parse NuSMV checker output into a structured counterexample trace.
/// Returns nullopt if no counterexample found in the output.
[[nodiscard]] std::optional<CounterexampleTrace>
parse_counterexample_trace(std::string_view checker_output,
                           const std::unordered_map<std::string, SourceMapping> &mappings);

/// Generate a human-readable explanation from a counterexample trace.
[[nodiscard]] ViolationExplanation explain_counterexample(const CounterexampleTrace &trace);

} // namespace ahfl::formal
