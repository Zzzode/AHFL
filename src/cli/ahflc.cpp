#include "ahfl/backends/driver.hpp"
#include "ahfl/dry_run/runner.hpp"
#include "ahfl/frontend/frontend.hpp"
#include "ahfl/cli/command_catalog.hpp"
#include "ahfl/cli/pipeline_runner.hpp"
#include "ahfl/semantics/resolver.hpp"
#include "ahfl/semantics/typecheck.hpp"
#include "ahfl/semantics/validate.hpp"

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using MaybeSourceFile = std::optional<std::reference_wrapper<const ahfl::SourceFile>>;
using ahfl::cli::CommandKind;
using ahfl::cli::CommandLineOptions;
using ahfl::cli::CommandListKind;
using ahfl::cli::command_list;
using ahfl::cli::command_name;
using ahfl::cli::command_token_to_kind;
using ahfl::cli::count_enabled_actions;
using ahfl::cli::format_comma_or_commands;
using ahfl::cli::infer_effective_command;
using ahfl::cli::is_action_enabled;
using ahfl::cli::is_command_requiring_package;
using ahfl::cli::is_package_supported_command;
using ahfl::cli::print_usage;
using ahfl::cli::selected_backend_for_command;
using ahfl::cli::set_command_option;
using ahfl::cli::supports_capability_inputs;

[[nodiscard]] ahfl::handoff::ExecutableKind
to_executable_kind(ahfl::PackageAuthoringTargetKind kind) {
    switch (kind) {
    case ahfl::PackageAuthoringTargetKind::Agent:
        return ahfl::handoff::ExecutableKind::Agent;
    case ahfl::PackageAuthoringTargetKind::Workflow:
        return ahfl::handoff::ExecutableKind::Workflow;
    }

    return ahfl::handoff::ExecutableKind::Workflow;
}

[[nodiscard]] ahfl::handoff::PackageMetadata
lower_package_metadata(const ahfl::PackageAuthoringDescriptor &descriptor) {
    ahfl::handoff::PackageMetadata metadata;
    metadata.identity = ahfl::handoff::PackageIdentity{
        .format_version = std::string(ahfl::handoff::kFormatVersion),
        .name = descriptor.package_name,
        .version = descriptor.package_version,
    };
    metadata.entry_target = ahfl::handoff::ExecutableRef{
        .kind = to_executable_kind(descriptor.entry.kind),
        .canonical_name = descriptor.entry.name,
    };
    metadata.export_targets.reserve(descriptor.exports.size());
    for (const auto &target : descriptor.exports) {
        metadata.export_targets.push_back(ahfl::handoff::ExecutableRef{
            .kind = to_executable_kind(target.kind),
            .canonical_name = target.name,
        });
    }
    for (const auto &binding : descriptor.capability_bindings) {
        metadata.capability_binding_keys.emplace(binding.capability, binding.binding_key);
    }
    return metadata;
}

template <typename InputT>
[[nodiscard]] bool emit_selected_backend(std::optional<CommandKind> effective_command,
                                         const InputT &input,
                                         const ahfl::ResolveResult &resolve_result,
                                         const ahfl::TypeCheckResult &type_check_result,
                                         const ahfl::handoff::PackageMetadata *package_metadata,
                                         std::ostream &out) {
    const auto backend = selected_backend_for_command(effective_command);
    if (!backend.has_value()) {
        return false;
    }

    ahfl::emit_backend(
        *backend, input, resolve_result, type_check_result, out, package_metadata);
    return true;
}

[[nodiscard]] bool read_text_file(const std::filesystem::path &path,
                                  std::string &content,
                                  std::ostream &diagnostics) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        diagnostics << "error: failed to open capability mock descriptor: "
                    << ahfl::display_path(path) << '\n';
        return false;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    content = buffer.str();
    return true;
}

template <typename ResultT>
void render_diagnostics(const ResultT &result, MaybeSourceFile source_file, std::ostream &out) {
    if (source_file.has_value()) {
        result.diagnostics.render(out, *source_file);
        return;
    }

    result.diagnostics.render(out);
}

void dump_ast_outline(const ahfl::ast::Program &program, std::ostream &out) {
    ahfl::dump_program_outline(program, out);
}

void dump_ast_outline(const ahfl::SourceGraph &graph, std::ostream &out) {
    ahfl::dump_project_ast_outline(graph, out);
}

void print_success_summary(const ahfl::ast::Program &program,
                           const ahfl::ResolveResult &resolve_result,
                           const ahfl::TypeCheckResult &type_check_result,
                           std::ostream &out) {
    out << "ok: checked " << program.declarations.size() << " top-level declaration(s), "
        << resolve_result.symbol_table.symbols().size() << " symbol(s), "
        << resolve_result.references().size() << " reference(s), "
        << type_check_result.environment.structs().size() +
               type_check_result.environment.enums().size()
        << " named type(s)\n";
}

void print_success_summary(const ahfl::SourceGraph &graph,
                           const ahfl::ResolveResult &resolve_result,
                           const ahfl::TypeCheckResult &type_check_result,
                           std::ostream &out) {
    out << "ok: checked " << graph.sources.size() << " source(s), "
        << resolve_result.symbol_table.symbols().size() << " symbol(s), "
        << resolve_result.references().size() << " reference(s), "
        << type_check_result.environment.structs().size() +
               type_check_result.environment.enums().size()
        << " named type(s)\n";
}

[[nodiscard]] int load_project_input(const CommandLineOptions &options,
                                     const ahfl::Frontend &frontend,
                                     ahfl::ProjectInput &input) {
    if (options.project_descriptor.has_value()) {
        auto descriptor_result =
            frontend.load_project_descriptor(std::string(*options.project_descriptor));
        descriptor_result.diagnostics.render(std::cerr);
        if (descriptor_result.has_errors() || !descriptor_result.descriptor.has_value()) {
            return descriptor_result.has_errors() ? 1 : 0;
        }

        input.entry_files = descriptor_result.descriptor->entry_files;
        input.search_roots = descriptor_result.descriptor->search_roots;
        return -1;
    }

    if (options.workspace_descriptor.has_value()) {
        auto descriptor_result = frontend.load_project_descriptor_from_workspace(
            std::string(*options.workspace_descriptor), *options.project_name);
        descriptor_result.diagnostics.render(std::cerr);
        if (descriptor_result.has_errors() || !descriptor_result.descriptor.has_value()) {
            return descriptor_result.has_errors() ? 1 : 0;
        }

        input.entry_files = descriptor_result.descriptor->entry_files;
        input.search_roots = descriptor_result.descriptor->search_roots;
        return -1;
    }

    input.entry_files.push_back(std::string(options.positional.front()));
    input.search_roots.reserve(options.search_roots.size());
    for (const auto search_root : options.search_roots) {
        input.search_roots.push_back(std::string(search_root));
    }

    return -1;
}

template <typename InputT>
[[nodiscard]] int run_analysis_pipeline(const CommandLineOptions &options,
                                        std::optional<CommandKind> effective_command,
                                        const InputT &input,
                                        MaybeSourceFile source_file,
                                        const ahfl::handoff::PackageMetadata *package_metadata,
                                        const ahfl::dry_run::CapabilityMockSet *capability_mock_set) {
    const ahfl::Resolver resolver;
    auto resolve_result = resolver.resolve(input);
    render_diagnostics(resolve_result, source_file, std::cerr);
    if (resolve_result.has_errors()) {
        return 1;
    }

    const ahfl::TypeChecker type_checker;
    auto type_check_result = type_checker.check(input, resolve_result);
    render_diagnostics(type_check_result, source_file, std::cerr);

    if (is_action_enabled(options, CommandKind::DumpTypes)) {
        ahfl::dump_type_environment(
            type_check_result.environment, resolve_result.symbol_table, std::cout);
    }

    if (type_check_result.has_errors()) {
        return 1;
    }

    const ahfl::Validator validator;
    auto validation_result = validator.validate(input, resolve_result, type_check_result);
    render_diagnostics(validation_result, source_file, std::cerr);
    if (validation_result.has_errors()) {
        return 1;
    }

    if (package_metadata != nullptr) {
        auto ir_program = ahfl::lower_program_ir(input, resolve_result, type_check_result);
        auto metadata_validation =
            ahfl::handoff::validate_package_metadata(ir_program, *package_metadata);
        render_diagnostics(metadata_validation, std::nullopt, std::cerr);
        if (metadata_validation.has_errors()) {
            return 1;
        }

        if (effective_command.has_value()) {
            if (const auto command_status =
                    ahfl::cli::dispatch_package_command(*effective_command,
                                                        ir_program,
                                                        metadata_validation.metadata,
                                                        capability_mock_set,
                                                        options);
                command_status.has_value()) {
                return *command_status;
            }
        }

        if (emit_selected_backend(effective_command,
                                  ir_program,
                                  resolve_result,
                                  type_check_result,
                                  &metadata_validation.metadata,
                                  std::cout)) {
            return 0;
        }
    }

    if (emit_selected_backend(effective_command,
                              input,
                              resolve_result,
                              type_check_result,
                              package_metadata,
                              std::cout)) {
        return 0;
    }

    if (!is_action_enabled(options, CommandKind::DumpTypes)) {
        print_success_summary(input, resolve_result, type_check_result, std::cout);
    }

    return 0;
}

[[nodiscard]] int parse_command_line(std::span<const std::string_view> arguments,
                                     CommandLineOptions &options) {
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto argument = arguments[index];
        if (argument == "--help" || argument == "-h") {
            print_usage(std::cout);
            return 0;
        }

        if (argument == "--project") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "error: --project requires a descriptor path\n";
                print_usage(std::cerr);
                return 2;
            }

            options.project_descriptor = arguments[++index];
            continue;
        }

        if (argument == "--package") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "error: --package requires a descriptor path\n";
                print_usage(std::cerr);
                return 2;
            }

            options.package_descriptor = arguments[++index];
            continue;
        }

        if (argument == "--capability-mocks") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "error: --capability-mocks requires a descriptor path\n";
                print_usage(std::cerr);
                return 2;
            }

            options.capability_mocks_descriptor = arguments[++index];
            continue;
        }

        if (argument == "--workspace") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "error: --workspace requires a descriptor path\n";
                print_usage(std::cerr);
                return 2;
            }

            options.workspace_descriptor = arguments[++index];
            continue;
        }

        if (argument == "--project-name") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "error: --project-name requires a project name\n";
                print_usage(std::cerr);
                return 2;
            }

            options.project_name = arguments[++index];
            continue;
        }

        if (argument == "--workflow") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "error: --workflow requires a canonical workflow name\n";
                print_usage(std::cerr);
                return 2;
            }

            options.workflow_name = arguments[++index];
            continue;
        }

        if (argument == "--input-fixture") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "error: --input-fixture requires a fixture string\n";
                print_usage(std::cerr);
                return 2;
            }

            options.input_fixture = arguments[++index];
            continue;
        }

        if (argument == "--run-id") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "error: --run-id requires an id string\n";
                print_usage(std::cerr);
                return 2;
            }

            options.run_id = arguments[++index];
            continue;
        }

        if (argument == "--search-root") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "error: --search-root requires a directory path\n";
                print_usage(std::cerr);
                return 2;
            }

            options.search_roots.push_back(arguments[++index]);
            continue;
        }

        if (argument == "--dump-ast") {
            options.dump_ast_requested = true;
            continue;
        }

        if (argument == "--dump-types") {
            options.dump_types_requested = true;
            continue;
        }

        const auto command = command_token_to_kind(argument);
        if (command.has_value() && !options.selected_command.has_value() &&
            options.positional.empty()) {
            set_command_option(options, *command);
            continue;
        }

        if (!argument.empty() && argument.front() == '-') {
            std::cerr << "error: unknown option '" << argument << "'\n";
            print_usage(std::cerr);
            return 2;
        }

        options.positional.push_back(argument);
    }

    return -1;
}

int run_cli(std::span<const std::string_view> arguments) {
    CommandLineOptions options;
    if (const auto parse_status = parse_command_line(arguments, options); parse_status >= 0) {
        return parse_status;
    }

    const auto effective_command = infer_effective_command(options);

    const auto action_count = count_enabled_actions(options);
    if (action_count > 1) {
        std::cerr << "error: choose at most one of "
                  << format_comma_or_commands(command_list(CommandListKind::Action)) << "\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.project_descriptor.has_value() && !options.search_roots.empty()) {
        std::cerr << "error: --project cannot be combined with --search-root\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.workspace_descriptor.has_value() && options.project_descriptor.has_value()) {
        std::cerr << "error: --workspace cannot be combined with --project\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.workspace_descriptor.has_value() && !options.search_roots.empty()) {
        std::cerr << "error: --workspace cannot be combined with --search-root\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.project_name.has_value() && !options.workspace_descriptor.has_value()) {
        std::cerr << "error: --project-name requires --workspace\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.workspace_descriptor.has_value() && !options.project_name.has_value()) {
        std::cerr << "error: --workspace requires --project-name\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.project_descriptor.has_value() || options.workspace_descriptor.has_value()
            ? !options.positional.empty()
            : options.positional.size() != 1) {
        print_usage(std::cerr);
        return 2;
    }

    if (options.package_descriptor.has_value() &&
        (!effective_command.has_value() ||
         !is_package_supported_command(*effective_command))) {
        std::cerr << "error: --package is only supported with "
                  << format_comma_or_commands(command_list(CommandListKind::PackageSupported))
                  << "\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.capability_mocks_descriptor.has_value() &&
        !supports_capability_inputs(effective_command)) {
        std::cerr << "error: --capability-mocks is only supported with "
                  << format_comma_or_commands(command_list(CommandListKind::CapabilityInputSupported))
                  << "\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.input_fixture.has_value() &&
        !supports_capability_inputs(effective_command)) {
        std::cerr << "error: --input-fixture is only supported with "
                  << format_comma_or_commands(command_list(CommandListKind::CapabilityInputSupported))
                  << "\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.run_id.has_value() &&
        !supports_capability_inputs(effective_command)) {
        std::cerr << "error: --run-id is only supported with "
                  << format_comma_or_commands(command_list(CommandListKind::CapabilityInputSupported))
                  << "\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.workflow_name.has_value() &&
        !supports_capability_inputs(effective_command)) {
        std::cerr << "error: --workflow is only supported with "
                  << format_comma_or_commands(command_list(CommandListKind::CapabilityInputSupported))
                  << "\n";
        print_usage(std::cerr);
        return 2;
    }

    std::optional<ahfl::dry_run::CapabilityMockSet> capability_mock_set;
    if (effective_command.has_value() &&
        is_command_requiring_package(*effective_command)) {
        const auto selected_command_name = command_name(*effective_command);
        if (!options.package_descriptor.has_value()) {
            std::cerr << "error: " << selected_command_name << " requires --package\n";
            print_usage(std::cerr);
            return 2;
        }
        if (!options.capability_mocks_descriptor.has_value()) {
            std::cerr << "error: " << selected_command_name << " requires --capability-mocks\n";
            print_usage(std::cerr);
            return 2;
        }
        if (!options.input_fixture.has_value()) {
            std::cerr << "error: " << selected_command_name << " requires --input-fixture\n";
            print_usage(std::cerr);
            return 2;
        }

        std::string capability_mocks_content;
        if (!read_text_file(std::string(*options.capability_mocks_descriptor),
                            capability_mocks_content,
                            std::cerr)) {
            return 1;
        }

        auto mock_parse_result =
            ahfl::dry_run::parse_capability_mock_set_json(capability_mocks_content);
        mock_parse_result.diagnostics.render(std::cerr);
        if (mock_parse_result.has_errors() || !mock_parse_result.mock_set.has_value()) {
            return 1;
        }

        capability_mock_set = std::move(*mock_parse_result.mock_set);
    }

    std::optional<ahfl::handoff::PackageMetadata> package_metadata;

    const bool project_mode = options.project_descriptor.has_value() ||
                              options.workspace_descriptor.has_value() ||
                              !options.search_roots.empty();
    const ahfl::Frontend frontend;
    if (options.package_descriptor.has_value()) {
        auto package_result =
            frontend.load_package_authoring_descriptor(std::string(*options.package_descriptor));
        package_result.diagnostics.render(std::cerr);
        if (package_result.has_errors() || !package_result.descriptor.has_value()) {
            return package_result.has_errors() ? 1 : 0;
        }

        package_metadata = lower_package_metadata(*package_result.descriptor);
    }
    if (project_mode || is_action_enabled(options, CommandKind::DumpProject)) {
        ahfl::ProjectInput input;
        if (const auto load_status = load_project_input(options, frontend, input);
            load_status >= 0) {
            return load_status;
        }

        auto project_result = frontend.parse_project(input);
        render_diagnostics(project_result, std::nullopt, std::cerr);
        if (project_result.has_errors()) {
            return 1;
        }

        if (effective_command == CommandKind::DumpProject) {
            ahfl::dump_project_outline(project_result.graph, std::cout);
            return 0;
        }

        if (effective_command == CommandKind::DumpAst) {
            dump_ast_outline(project_result.graph, std::cout);
            return 0;
        }

        return run_analysis_pipeline(options,
                                     effective_command,
                                     project_result.graph,
                                     std::nullopt,
                                     package_metadata ? &*package_metadata : nullptr,
                                     capability_mock_set ? &*capability_mock_set : nullptr);
    }

    auto parse_result = frontend.parse_file(std::string(options.positional.front()));
    render_diagnostics(parse_result, std::cref(parse_result.source), std::cerr);

    if (parse_result.program && effective_command == CommandKind::DumpAst) {
        dump_ast_outline(*parse_result.program, std::cout);
    }

    if (parse_result.has_errors() || !parse_result.program) {
        return parse_result.has_errors() ? 1 : 0;
    }

    return run_analysis_pipeline(options,
                                 effective_command,
                                 *parse_result.program,
                                 std::cref(parse_result.source),
                                 package_metadata ? &*package_metadata : nullptr,
                                 capability_mock_set ? &*capability_mock_set : nullptr);
}

} // namespace

int main(int argc, char **argv) {
    std::vector<std::string_view> arguments;
    arguments.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0));

    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }

    return run_cli(arguments);
}
