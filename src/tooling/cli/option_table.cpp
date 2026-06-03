#include "tooling/cli/option_table.hpp"

#include <algorithm>
#include <cstddef>
#include <iostream>

namespace ahfl::cli {
namespace {

// ---------------------------------------------------------------------------
// Setter functions — one per option
// ---------------------------------------------------------------------------

void set_project(CommandLineOptions &opts, std::optional<std::string_view> val) {
    opts.project_descriptor = val;
}

void set_package(CommandLineOptions &opts, std::optional<std::string_view> val) {
    opts.package_descriptor = val;
}

void set_capability_mocks(CommandLineOptions &opts, std::optional<std::string_view> val) {
    opts.capability_mocks_descriptor = val;
}

void set_workspace(CommandLineOptions &opts, std::optional<std::string_view> val) {
    opts.workspace_descriptor = val;
}

void set_project_name(CommandLineOptions &opts, std::optional<std::string_view> val) {
    opts.project_name = val;
}

void set_workflow(CommandLineOptions &opts, std::optional<std::string_view> val) {
    opts.workflow_name = val;
}

void set_input_fixture(CommandLineOptions &opts, std::optional<std::string_view> val) {
    opts.input_fixture = val;
}

void set_run_id(CommandLineOptions &opts, std::optional<std::string_view> val) {
    opts.run_id = val;
}

void set_model_checker(CommandLineOptions &opts, std::optional<std::string_view> val) {
    opts.model_checker = val;
}

void set_formal_model_out(CommandLineOptions &opts, std::optional<std::string_view> val) {
    opts.formal_model_out = val;
}

void set_search_root(CommandLineOptions &opts, std::optional<std::string_view> val) {
    opts.search_roots.push_back(*val);
}

void set_dump_ast(CommandLineOptions &opts, std::optional<std::string_view>) {
    opts.dump_ast_requested = true;
}

void set_dump_types(CommandLineOptions &opts, std::optional<std::string_view>) {
    opts.dump_types_requested = true;
}

void set_explain(CommandLineOptions &opts, std::optional<std::string_view>) {
    opts.explain_requested = true;
}

void set_optimize(CommandLineOptions &opts, std::optional<std::string_view>) {
    opts.optimize_requested = true;
}

void set_show_internal(CommandLineOptions &opts, std::optional<std::string_view>) {
    opts.show_internal_artifacts = true;
}

// ---------------------------------------------------------------------------
// Constexpr option table
// ---------------------------------------------------------------------------

constexpr OptionSpec kOptionSpecs[] = {
    {"--project",
     "",
     OptionArgKind::RequiredValue,
     set_project,
     "Path to project descriptor",
     "a descriptor path"},
    {"--package",
     "",
     OptionArgKind::RequiredValue,
     set_package,
     "Path to package descriptor",
     "a descriptor path"},
    {"--capability-mocks",
     "",
     OptionArgKind::RequiredValue,
     set_capability_mocks,
     "Path to capability mocks descriptor",
     "a descriptor path"},
    {"--workspace",
     "",
     OptionArgKind::RequiredValue,
     set_workspace,
     "Path to workspace descriptor",
     "a descriptor path"},
    {"--project-name",
     "",
     OptionArgKind::RequiredValue,
     set_project_name,
     "Project name within workspace",
     "a project name"},
    {"--workflow",
     "",
     OptionArgKind::RequiredValue,
     set_workflow,
     "Canonical workflow name",
     "a canonical workflow name"},
    {"--input-fixture",
     "",
     OptionArgKind::RequiredValue,
     set_input_fixture,
     "Input fixture string",
     "a fixture string"},
    {"--run-id", "", OptionArgKind::RequiredValue, set_run_id, "Run identifier", "an id string"},
    {"--model-checker",
     "",
     OptionArgKind::RequiredValue,
     set_model_checker,
     "Path to model checker executable",
     "an executable path"},
    {"--formal-model-out",
     "",
     OptionArgKind::RequiredValue,
     set_formal_model_out,
     "Output path for formal model",
     "an output path"},
    {"--search-root",
     "",
     OptionArgKind::RepeatableValue,
     set_search_root,
     "Additional search root directory",
     "a directory path"},
    {"--dump-ast", "", OptionArgKind::Flag, set_dump_ast, "Dump AST outline", ""},
    {"--dump-types", "", OptionArgKind::Flag, set_dump_types, "Dump type environment", ""},
    {"--explain", "", OptionArgKind::Flag, set_explain, "Enable structured explanations", ""},
    {"--optimize", "-O", OptionArgKind::Flag, set_optimize, "Enable optimization passes", ""},
    {"--show-hidden",
     "",
     OptionArgKind::Flag,
     set_show_internal,
     "Show hidden internal artifacts in help and allow their emission",
     ""},
};

// ---------------------------------------------------------------------------
// Static validation — no duplicate long_names
// ---------------------------------------------------------------------------

[[nodiscard]] consteval bool option_table_has_unique_long_names() {
    for (std::size_t i = 0; i < std::size(kOptionSpecs); ++i) {
        for (std::size_t j = i + 1; j < std::size(kOptionSpecs); ++j) {
            if (kOptionSpecs[i].long_name == kOptionSpecs[j].long_name) {
                return false;
            }
        }
    }
    return true;
}

static_assert(option_table_has_unique_long_names(), "duplicate long_name in option table");

// ---------------------------------------------------------------------------
// Table lookup helper
// ---------------------------------------------------------------------------

[[nodiscard]] const OptionSpec *find_option_spec(std::string_view argument) {
    for (const auto &spec : kOptionSpecs) {
        if (argument == spec.long_name) {
            return &spec;
        }
        if (!spec.short_name.empty() && argument == spec.short_name) {
            return &spec;
        }
    }
    return nullptr;
}

} // namespace

// ---------------------------------------------------------------------------
// parse_options_from_table
// ---------------------------------------------------------------------------

[[nodiscard]] std::optional<ParseResult>
parse_options_from_table(std::span<const std::string_view> arguments, CommandLineOptions &options) {
    for (const auto argument : arguments) {
        if (argument == "--show-hidden") {
            options.show_internal_artifacts = true;
            break;
        }
    }

    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto argument = arguments[index];

        // Special case: --help / -h triggers immediate exit.
        if (argument == "--help" || argument == "-h") {
            print_usage(std::cout, options.show_internal_artifacts);
            return ParseResult{true, 0};
        }

        // Try matching against the option table.
        if (const auto *spec = find_option_spec(argument); spec != nullptr) {
            if (spec->arg_kind == OptionArgKind::Flag) {
                spec->setter(options, std::nullopt);
            } else {
                // RequiredValue or RepeatableValue — consume next argument.
                if (index + 1 >= arguments.size()) {
                    std::cerr << "error: " << spec->long_name << " requires " << spec->metavar
                              << "\n";
                    print_usage(std::cerr);
                    return ParseResult{true, 2};
                }
                spec->setter(options, arguments[++index]);
            }
            continue;
        }

        // Try two-token subcommand dispatch (ahflc emit <artifact-id>).
        if (auto group = action_group_from_token(argument); group.has_value()) {
            // Zero-target actions: verify and validate have a single implicit target.
            if (*group == ActionGroup::Verify) {
                if (!options.selected_command.has_value() && options.positional.empty()) {
                    set_command_option(options, CommandKind::VerifyFormal);
                    continue;
                }
            } else if (*group == ActionGroup::Validate) {
                if (!options.selected_command.has_value() && options.positional.empty()) {
                    set_command_option(options, CommandKind::ValidateAssurance);
                    continue;
                }
            } else {
                // emit/dump require a target artifact-id.
                if (index + 1 < arguments.size() && !arguments[index + 1].empty() &&
                    arguments[index + 1].front() != '-') {
                    auto artifact_id = arguments[++index];
                    if (auto cmd = resolve_subcommand(
                            *group, artifact_id, options.show_internal_artifacts);
                        cmd.has_value()) {
                        if (!options.selected_command.has_value() && options.positional.empty()) {
                            set_command_option(options, *cmd);
                            continue;
                        }
                    }
                    std::cerr << "error: unknown artifact '" << artifact_id << "' for action '"
                              << argument << "'\n";
                    print_usage(std::cerr);
                    return ParseResult{true, 2};
                }
                std::cerr << "error: '" << argument << "' requires an artifact name\n";
                print_usage(std::cerr);
                return ParseResult{true, 2};
            }
        }

        // Recognize "check" as a standalone command.
        if (argument == "check" && !options.selected_command.has_value() &&
            options.positional.empty()) {
            set_command_option(options, CommandKind::Check);
            continue;
        }

        // Unknown option (starts with '-').
        if (!argument.empty() && argument.front() == '-') {
            std::cerr << "error: unknown option '" << argument << "'\n";
            print_usage(std::cerr);
            return ParseResult{true, 2};
        }

        // Positional argument.
        options.positional.push_back(argument);
    }

    return std::nullopt;
}

// ---------------------------------------------------------------------------
// print_generated_usage — delegates to existing print_usage for now
// ---------------------------------------------------------------------------

void print_generated_usage(std::ostream &out) {
    print_usage(out);
}

} // namespace ahfl::cli
