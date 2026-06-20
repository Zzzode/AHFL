#include "verification/formal/tlaplus_backend.hpp"

#include <string>

namespace ahfl::formal {

[[nodiscard]] ModelCheckerKind TLAPlusBackend::kind() const {
    return ModelCheckerKind::TLAPlus;
}

[[nodiscard]] std::string_view TLAPlusBackend::name() const {
    return "TLA+";
}

[[nodiscard]] std::string_view TLAPlusBackend::file_extension() const {
    return ".tla";
}

[[nodiscard]] ModelEmissionResult TLAPlusBackend::emit_model(const BmcStateMachine &machine) {
    ModelEmissionResult result;

    if (machine.states.empty()) {
        result.success = false;
        result.error_message = "Cannot emit model: no states defined";
        return result;
    }

    std::string model;
    model += "---- MODULE " + machine.name + " ----\n";
    model += "EXTENDS Naturals, Sequences\n\n";

    // State variable
    model += "VARIABLES state\n\n";

    // States as constants
    model += "States == {";
    for (std::size_t i = 0; i < machine.states.size(); ++i) {
        if (i > 0) {
            model += ", ";
        }
        model += "\"" + machine.states[i] + "\"";
    }
    model += "}\n\n";

    // Init
    model += "Init == state = \"" + machine.initial_state + "\"\n\n";

    // Next state relation
    model += "Next ==\n";
    for (std::size_t i = 0; i < machine.transitions.size(); ++i) {
        const auto &t = machine.transitions[i];
        if (i == 0) {
            model += "  \\/ (state = \"" + t.from + "\" /\\ state' = \"" + t.to + "\")\n";
        } else {
            model += "  \\/ (state = \"" + t.from + "\" /\\ state' = \"" + t.to + "\")\n";
        }
    }
    model += "  \\/ (state' = state)\n\n";

    // Spec
    model += "Spec == Init /\\ [][Next]_state\n\n";

    // Properties as invariants
    for (const auto &prop : machine.properties) {
        if (prop.size() > 6 && prop.substr(0, 6) == "never(") {
            auto bad_state = prop.substr(6, prop.size() - 7);
            model += "Safety == state /= \"" + bad_state + "\"\n";
        } else if (prop.size() > 10 && prop.substr(0, 10) == "reachable(") {
            auto target = prop.substr(10, prop.size() - 11);
            model += "Liveness == <>(state = \"" + target + "\")\n";
        }
    }

    model += "====\n";

    result.success = true;
    result.model_text = std::move(model);
    return result;
}

[[nodiscard]] VerificationSummary TLAPlusBackend::verify(const std::string & /*model_text*/) {
    VerificationSummary summary;
    summary.all_passed = false;
    summary.properties_checked = 0;
    summary.properties_passed = 0;
    summary.error_message = "TLA+/TLC verification is experimental";
    return summary;
}

} // namespace ahfl::formal
