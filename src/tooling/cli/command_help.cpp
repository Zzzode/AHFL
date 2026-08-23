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
        } else if (token.starts_with("package-")) {
            result += "package ";
            result += command_short_name(commands[index]);
        } else if (token.starts_with("registry-")) {
            result += "registry ";
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

void print_usage(std::ostream &out) {
    out << "Usage:\n"
        << "  ahflc check [options] [<input.ahfl>]\n"
        << "  ahflc run [options]\n"
        << "  ahflc run --manifest <ahfl.toml> [options]\n"
        << "  ahflc run --workflow <name> --input '<json>' [options] <input.ahfl>\n"
        << "  ahflc fmt [--check] <input.ahfl|dir>...\n"
        << "  ahflc fmt [--check] --manifest <ahfl.toml>\n"
        << "  ahflc fmt [--check] --workspace <ahfl.workspace.toml> --package <name>\n"
        << "  ahflc init --single-file <input.ahfl>\n"
        << "  ahflc package archive --manifest <ahfl.toml> --out <dir>\n"
        << "  ahflc package publish [--dry-run] --manifest <ahfl.toml> --registry <id> "
           "--out <dir> [--sysroot <path>] [--semver-gate --from <previous>]\n"
        << "  ahflc package yank <package>@<version> --registry <id> [--reason <text>]\n"
        << "  ahflc registry resolve --manifest <ahfl.toml> --lockfile <ahfl.lock> "
           "[--sysroot <path>]\n"
        << "  ahflc emit <artifact> [options] [<input.ahfl>]\n"
        << "  ahflc emit public-api-diff [--semver-gate --from <old> --to <new>] "
           "<old-public-api.json> <new-public-api.json>\n"
        << "  ahflc dump <target> [options] [<input.ahfl>]\n"
        << "  ahflc verify [options] [<input.ahfl>]\n"
        << "  ahflc validate [options] [<input.ahfl>]\n"
        << "\n"
        << "Actions:\n"
        << "  check               Type-check source files\n"
        << "  run                 Execute a workflow with configured LLM capabilities\n"
        << "  fmt                 Format files/directories in place, or check with --check\n"
        << "  init --single-file  Create ahfl.toml for an explicit single-file package\n"
        << "  package archive     Build a normalized source archive for publishing\n"
        << "  package publish     Run package publish gates and optionally upload to registry\n"
        << "  package yank        Mark an immutable registry package version as yanked\n"
        << "  registry resolve    Resolve manifest registry dependencies and write ahfl.lock\n"
        << "  emit <artifact>     Emit a build artifact (see list below)\n"
        << "  dump <target>       Diagnostic dump (ast, types, project, package-graph, lockfile)\n"
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
        << "    dry-run-trace              Dry-run execution trace\n"
        << "    package-review             Package-level review\n"
        << "    public-api                 Package public API JSON snapshot\n"
        << "    public-api-docs            Package public API Markdown docs\n"
        << "    public-api-diff            Diff two public API JSON snapshots; optional SemVer "
           "gate\n"
        << "    summary                    Human-readable summary\n"
        << "    smv                        NuSMV model (for verify)\n"
        << "    assurance-json             Assurance model (for validate)\n";

    // Dump targets
    out << "\n  Dump targets: ast, types, package-graph, lockfile\n";

    // Options grouped by scope
    out << "\nInput Options:\n"
        << "  --manifest <path>           AHFL package manifest (default: ./ahfl.toml)\n"
        << "  --workspace <path>          AHFL workspace manifest (ahfl.workspace.toml)\n"
        << "  --package <name>            Workspace package name with --workspace\n"
        << "  --sysroot <path>            AHFL sysroot root or std/ahfl.toml\n"
        << "  --target <name>             Target name within an AHFL package manifest\n"
        << "  --capability-mocks <path>   Capability mock input; run uses it as LLM tools\n";

    out << "\nRuntime Options:\n"
        << "  --workflow <canonical>      Target workflow; package run defaults to manifest entry\n"
        << "  --input <json>              Runtime input JSON for run\n"
        << "  --input-file <path>         Runtime input JSON file for run\n"
        << "  --llm-config <path>         LLM config for run (default: ~/.ahfl/llm_config.json)\n"
        << "  --profile <name>            Named run profile from ahfl.toml\n"
        << "  --output-format <format>    human, json, jsonl, or quiet\n"
        << "  --verbosity <level>         normal, verbose, or trace\n"
        << "  --tool-catalog <path>       Runtime tool catalog exposed as LLM tools\n"
        << "  --capability-bindings <path>  HTTP/gRPC capability binding config for run\n"
        << "  --input-fixture <fixture>   Runtime fixture selection\n"
        << "  --run-id <id>              Stable run identity\n";

    out << "\nPackage Options:\n"
        << "  --out <dir>                 Output directory for package archive/publish artifacts\n"
        << "  --registry <id>             Registry id for package publish/yank\n"
        << "  --lockfile <path>           Output lockfile path for registry resolve\n"
        << "  --dry-run                   Plan package publish without uploading\n"
        << "  --reason <text>             Reason recorded for package yank\n"
        << "  --semver-gate               Enforce public API SemVer gate\n"
        << "  --from <version>            Previous version for public API SemVer gate\n"
        << "  --to <version>              Next version for emit public-api-diff only\n";

    out << "\nVerification Options:\n"
        << "  --formal-backend <name>    Backend: nuxmv, nusmv, spin, or tlaplus\n"
        << "  --model-checker <path>     NuSMV/nuXmv binary path\n"
        << "  --checker-timeout-seconds <n>  Formal checker process timeout\n"
        << "  --formal-model-out <path>  Write SMV model to file\n";

    out << "\nGeneral Options:\n"
        << "  --check                    Check formatting without writing (fmt only)\n"
        << "  --explain                  Verbose diagnostic output\n"
        << "  -O                         Enable optimization passes\n"
        << "  --time-passes              Print optimization pass timings (requires -O)\n"
        << "  --smv-size-report          Print SMV output size statistics (emit smv only)\n"
        << "  --trace-export <path>      Write CLI trace spans as JSON lines\n"
        << "  --metrics-export <path>    Write CLI metrics as JSON lines\n"
        << "  --structured-log <path>    Write CLI structured logs as JSON lines\n"
        << "  --memory-report <path>     Write memory report (structural proxy + platform RSS) as JSON\n"
        << "  -h, --help                 Show this help\n";
}

} // namespace ahfl::cli
