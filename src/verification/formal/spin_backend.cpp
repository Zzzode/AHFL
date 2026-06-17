#include "verification/formal/spin_backend.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace ahfl::formal {

[[nodiscard]] ModelCheckerKind SpinBackend::kind() const {
    return ModelCheckerKind::SPIN;
}

[[nodiscard]] std::string_view SpinBackend::name() const {
    return "SPIN";
}

[[nodiscard]] std::string_view SpinBackend::file_extension() const {
    return ".pml";
}

[[nodiscard]] ModelCheckerCapabilities SpinBackend::capabilities() const {
    return ModelCheckerCapabilities{
        .emits_model = true,
        .supports_external_verification = false,
        .supports_ahfl_smv_semantics = false,
        .required_binary = "spin",
        .property_semantics =
            {
                "state-machine control graph Promela emission",
            },
        .skip_reason = "SPIN external verification is not wired for AHFL property semantics yet",
    };
}

[[nodiscard]] ModelCheckerAvailability SpinBackend::availability() const {
    return ModelCheckerAvailability{
        .status = ModelCheckerAvailabilityStatus::VerificationUnsupported,
        .binary_path = {},
        .reason = capabilities().skip_reason,
    };
}

[[nodiscard]] ModelEmissionResult SpinBackend::emit_model(const BmcStateMachine &machine) {
    ModelEmissionResult result;

    if (machine.states.empty()) {
        result.success = false;
        result.error_message = "Cannot emit model: no states defined";
        return result;
    }

    // Map state names to numeric indices
    std::unordered_map<std::string, std::size_t> state_index;
    for (std::size_t i = 0; i < machine.states.size(); ++i) {
        state_index[machine.states[i]] = i;
    }

    std::string model;
    model += "/* Generated Promela model for: " + machine.name + " */\n\n";

    // Build transition map
    std::unordered_map<std::string, std::vector<std::string>> trans_map;
    for (const auto &t : machine.transitions) {
        trans_map[t.from].push_back(t.to);
    }

    model += "proctype Agent() {\n";
    model += "  byte state = " + std::to_string(state_index[machine.initial_state]) + ";\n";
    model += "  do\n";

    for (const auto &s : machine.states) {
        auto it = trans_map.find(s);
        if (it != trans_map.end()) {
            for (const auto &target : it->second) {
                model += "  :: state == " + std::to_string(state_index[s]);
                model += " -> state = " + std::to_string(state_index[target]) + ";\n";
            }
        }
    }

    // Add break condition for final states
    for (const auto &fs : machine.final_states) {
        model += "  :: state == " + std::to_string(state_index[fs]) + " -> break;\n";
    }

    if (machine.final_states.empty()) {
        model += "  :: else -> break;\n";
    }

    model += "  od;\n";
    model += "}\n\n";
    model += "init { run Agent(); }\n";

    result.success = true;
    result.model_text = std::move(model);
    return result;
}

[[nodiscard]] VerificationSummary SpinBackend::verify(const std::string & /*model_text*/) {
    VerificationSummary summary;
    summary.all_passed = false;
    summary.properties_checked = 0;
    summary.properties_passed = 0;
    summary.error_message = "External SPIN binary not available";
    return summary;
}

} // namespace ahfl::formal
