#include "ahfl/formal/nuxmv_backend.hpp"

#include <string>

namespace ahfl::formal {

[[nodiscard]] ModelCheckerKind NuXmvBackend::kind() const {
    return ModelCheckerKind::NuXmv;
}

[[nodiscard]] std::string_view NuXmvBackend::name() const {
    return "nuXmv";
}

[[nodiscard]] std::string_view NuXmvBackend::file_extension() const {
    return ".smv";
}

[[nodiscard]] ModelEmissionResult
NuXmvBackend::emit_model(const BmcStateMachine &machine) {
    ModelEmissionResult result;

    if (machine.states.empty()) {
        result.success = false;
        result.error_message = "Cannot emit model: no states defined";
        return result;
    }

    std::string model;
    model += "MODULE main\n";

    // VAR declaration
    model += "VAR state : {";
    for (std::size_t i = 0; i < machine.states.size(); ++i) {
        if (i > 0) {
            model += ", ";
        }
        model += machine.states[i];
    }
    model += "};\n";

    // INIT
    model += "INIT state = " + machine.initial_state + ";\n";

    // TRANS
    model += "TRANS next(state) = case\n";
    for (const auto &t : machine.transitions) {
        model += "  state = " + t.from + " : " + t.to + ";\n";
    }
    model += "  TRUE : state;\nesac;\n";

    // Properties as LTLSPEC
    for (const auto &prop : machine.properties) {
        if (prop.size() > 6 && prop.substr(0, 6) == "never(") {
            auto bad_state = prop.substr(6, prop.size() - 7);
            model += "LTLSPEC G (state != " + bad_state + ");\n";
        } else if (prop.size() > 10 && prop.substr(0, 10) == "reachable(") {
            auto target = prop.substr(10, prop.size() - 11);
            model += "LTLSPEC F (state = " + target + ");\n";
        } else {
            model += "LTLSPEC " + prop + ";\n";
        }
    }

    result.success = true;
    result.model_text = std::move(model);
    return result;
}

[[nodiscard]] VerificationSummary
NuXmvBackend::verify(const std::string & /*model_text*/) {
    VerificationSummary summary;
    summary.all_passed = false;
    summary.properties_checked = 0;
    summary.properties_passed = 0;
    summary.error_message = "External nuXmv binary not available";
    return summary;
}

} // namespace ahfl::formal
