#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace ahfl::formal {

/// Verdict vocabulary for a bounded-reachability run.
///
/// `Safe` and `Unsafe` are the only verdicts `run_bmc` produces; `Error` marks a
/// malformed input machine. `Unknown` ("inconclusive within the bound") is part
/// of the trichotomy a complete procedure must be able to express and is
/// reserved for the real k-induction work tracked as KR7.2 — nothing currently
/// returns it, and nothing may return it as a stand-in for a missing proof.
enum class BmcStatus {
    Safe,
    Unsafe,
    Unknown,
    Error,
};

struct BmcOptions {
    std::size_t max_bound{10};
};

struct BmcCounterexample {
    std::size_t depth{0};
    std::vector<std::string> trace_states;
    std::string violated_property;
};

struct BmcResult {
    BmcStatus status{BmcStatus::Error};
    std::size_t bound_reached{0};
    std::optional<BmcCounterexample> counterexample;
    std::string error_message;
    double elapsed_ms{0.0};
};

/// Representation of a simple state machine for BMC analysis
struct BmcStateMachine {
    std::string name;
    std::vector<std::string> states;
    std::string initial_state;
    std::vector<std::string> final_states;
    struct Transition {
        std::string from;
        std::string to;
    };
    std::vector<Transition> transitions;
    std::vector<std::string> properties; // LTL property strings
};

/// Bounded reachability check over the state graph.
///
/// A "never(X)" property declares X a bad state; the check is Unsafe when X is
/// reachable from the initial state within `options.max_bound` transitions, and
/// Safe when the bounded search exhausts without reaching a bad state.
///
/// NOTE: this is the *reachability* engine only. There is deliberately no
/// k-induction or CEGAR entry point here: the previous `run_k_induction` /
/// `run_cegar` functions were production-dead and unsound (the former was a
/// mislabelled one-step neighbour scan, the latter always returned Unknown).
/// Real k-induction is tracked as KR7.2 and must be implemented against the
/// SMT-BMC data semantics, not re-added as a state-graph shortcut.
[[nodiscard]] BmcResult run_bmc(const BmcStateMachine &machine, const BmcOptions &options);

} // namespace ahfl::formal
