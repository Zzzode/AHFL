#include "tooling/cli/command_catalog.hpp"

#include <cstddef>

namespace ahfl::cli {

// ---------------------------------------------------------------------------
// format_comma_or_commands
// ---------------------------------------------------------------------------

[[nodiscard]] std::string format_comma_or_commands(std::span<const CommandKind> commands) {
    std::string result;
    for (std::size_t index = 0; index < commands.size(); ++index) {
        if (index > 0) {
            result += (index + 1 == commands.size()) ? ", or " : ", ";
        }
        // Format as two-token CLI syntax: "emit <artifact-id>", "dump <target>", etc.
        auto token = command_name(commands[index]);
        if (token.starts_with("emit-")) {
            result += "emit ";
            result += command_short_name(commands[index]);
        } else if (token.starts_with("dump-")) {
            result += "dump ";
            result += command_short_name(commands[index]);
        } else if (token.starts_with("verify-")) {
            result += "verify";
        } else if (token.starts_with("validate-")) {
            result += "validate";
        } else {
            result += token;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// print_usage
// ---------------------------------------------------------------------------

void print_usage(std::ostream &out, bool show_internal) {
    out << "Usage:\n"
        << "  ahflc check [options] <input.ahfl>\n"
        << "  ahflc run --workflow <name> --input '<json>' [options] <input.ahfl>\n"
        << "  ahflc emit <artifact> [options] <input.ahfl>\n"
        << "  ahflc dump <target> [options] <input.ahfl>\n"
        << "  ahflc verify [options] <input.ahfl>\n"
        << "  ahflc validate [options] <input.ahfl>\n"
        << "\n"
        << "Actions:\n"
        << "  check               Type-check source files\n"
        << "  run                 Execute a workflow with configured LLM capabilities\n"
        << "  emit <artifact>     Emit a build artifact (see list below)\n"
        << "  dump <target>       Diagnostic dump (ast, types, project)\n"
        << "  verify              Formal verification via NuSMV/nuXmv\n"
        << "  validate            Assurance validation checks\n"
        << "\n"
        << "Artifacts (ahflc emit <artifact>):\n";

    // Core artifacts with descriptions
    out << "  Core:\n"
        << "    ir                         AHFL intermediate representation\n"
        << "    ir-json                    IR in JSON format\n"
        << "    opt-ir                     Optimization IR diagnostic dump\n"
        << "    opt-ir-json                Optimization IR JSON artifact\n"
        << "    native-json                Native backend JSON\n"
        << "    execution-plan             Execution plan for workflows\n"
        << "    execution-journal          Execution journal trace\n"
        << "    replay-view                Replay view snapshot\n"
        << "    audit-report               Audit trail report\n"
        << "    scheduler-snapshot         Scheduler state snapshot\n"
        << "    scheduler-review           Scheduler review output\n"
        << "    checkpoint-record          Checkpoint record\n"
        << "    checkpoint-review          Checkpoint review\n"
        << "    persistence-descriptor     Persistence layer descriptor\n"
        << "    persistence-review         Persistence review\n"
        << "    export-manifest            Export manifest\n"
        << "    export-review              Export review\n"
        << "    store-import-descriptor    Store import descriptor\n"
        << "    store-import-review        Store import review\n"
        << "    runtime-session            Runtime session snapshot\n"
        << "    dry-run-trace              Dry-run execution trace\n"
        << "    package-review             Package-level review\n"
        << "    summary                    Human-readable summary\n"
        << "    smv                        NuSMV model (for verify)\n"
        << "    assurance-json             Assurance model (for validate)\n";

    // Store pipeline (durable-store-import artifacts under store/)
    out << "\n  Store Pipeline (store/...):\n";
    for (const auto command : command_list(CommandListKind::Action)) {
        auto name = command_name(command);
        if (name.starts_with("emit-durable-store-import-")) {
            out << "    " << command_short_name(command) << '\n';
        }
    }

    if (show_internal) {
        out << "\n  Internal Provider Artifacts "
               "(ahflc emit-provider-artifact <provider/artifact>):\n";
        for (const auto &artifact : provider_artifact_descriptors()) {
            out << "    provider/" << artifact.artifact_id << '\n';
        }
    }

    // Dump targets
    out << "\n  Dump targets: ast, types, project\n";

    // Options grouped by scope
    out << "\nInput Options:\n"
        << "  --package <path>            Package descriptor\n"
        << "  --project <path>            Project descriptor\n"
        << "  --workspace <path>          Workspace descriptor\n"
        << "  --project-name <name>       Target project in workspace\n"
        << "  --capability-mocks <path>   Capability mock input\n"
        << "  --search-root <dir>         Additional source search path (repeatable)\n";

    out << "\nRuntime Options:\n"
        << "  --workflow <canonical>      Target workflow (multi-workflow packages)\n"
        << "  --input <json>              Runtime input JSON for run\n"
        << "  --llm-config <path>         LLM config for run (default: ~/.ahfl/llm_config.json)\n"
        << "  --input-fixture <fixture>   Runtime fixture selection\n"
        << "  --run-id <id>              Stable run identity\n";

    out << "\nVerification Options:\n"
        << "  --model-checker <path>     NuSMV/nuXmv binary path\n"
        << "  --formal-model-out <path>  Write SMV model to file\n";

    out << "\nGeneral Options:\n"
        << "  --show-hidden              Show hidden internal diagnostic artifacts\n"
        << "  --explain                  Verbose diagnostic output\n"
        << "  -O                         Enable optimization passes\n"
        << "  -h, --help                 Show this help\n";
}

} // namespace ahfl::cli
