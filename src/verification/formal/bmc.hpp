#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace ahfl::formal {

enum class BmcStatus {
    Safe,
    Unsafe,
    Unknown,
    Error,
};

struct BmcOptions {
    std::size_t max_bound{10};
    bool use_k_induction{false};
    bool enable_cegar{false};
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

/// SAT-based Bounded Model Checking
[[nodiscard]] BmcResult
run_bmc(const BmcStateMachine &machine, const BmcOptions &options);

/// k-induction verification
[[nodiscard]] BmcResult
run_k_induction(const BmcStateMachine &machine, const BmcOptions &options);

/// CEGAR stub
[[nodiscard]] BmcResult
run_cegar(const BmcStateMachine &machine, const BmcOptions &options);

} // namespace ahfl::formal
