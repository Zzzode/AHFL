#include "cli/cli_driver.hpp"

#include "assurance/assurance.hpp"
#include "ahfl/backends/driver.hpp"
#include "cli/diagnostic_consumer.hpp"
#include "cli/option_table.hpp"
#include "cli/pipeline_runner.hpp"
#include "dry_run/runner.hpp"
#include "formal/checker.hpp"
#include "ahfl/frontend/frontend.hpp"
#include "passes/pass_manager.hpp"
#include "ahfl/semantics/resolver.hpp"
#include "ahfl/semantics/typecheck.hpp"
#include "ahfl/semantics/validate.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ahfl::cli {

using ahfl::cli::command_list;
using ahfl::cli::command_name;
using ahfl::cli::CommandKind;
using ahfl::cli::CommandLineOptions;
using ahfl::cli::CommandListKind;
using ahfl::cli::core_backend_for_command;
using ahfl::cli::count_enabled_actions;
using ahfl::cli::format_comma_or_commands;
using ahfl::cli::handles_package_command;
using ahfl::cli::infer_effective_command;
using ahfl::cli::is_action_enabled;
using ahfl::cli::is_command_requiring_package;
using ahfl::cli::is_package_supported_command;
using ahfl::cli::print_usage;
using ahfl::cli::set_command_option;
using ahfl::cli::supports_capability_inputs;

namespace {

using MaybeSourceFile = std::optional<std::reference_wrapper<const ahfl::SourceFile>>;

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

[[nodiscard]] std::optional<int>
emit_core_backend(std::optional<CommandKind> effective_command,
                  const ahfl::ir::Program &program,
                  const ahfl::ResolveResult &resolve_result,
                  const ahfl::TypeCheckResult &type_check_result,
                  const ahfl::handoff::PackageMetadata *package_metadata,
                  std::ostream &out) {
    static_cast<void>(resolve_result);
    static_cast<void>(type_check_result);

    const auto backend = core_backend_for_command(effective_command);
    if (!backend.has_value()) {
        return std::nullopt;
    }

    auto result = ahfl::emit_backend(*backend, program, out, package_metadata);
    if (!result.has_value()) {
        std::cerr << "internal error: core backend command ";
        if (effective_command.has_value()) {
            std::cerr << "'" << command_name(*effective_command) << "' ";
        }
        std::cerr << "failed: " << result.error() << "\n";
        return 1;
    }

    return 0;
}

template <typename InputT>
[[nodiscard]] std::optional<int>
emit_core_backend(std::optional<CommandKind> effective_command,
                  const InputT &input,
                  const ahfl::ResolveResult &resolve_result,
                  const ahfl::TypeCheckResult &type_check_result,
                  const ahfl::handoff::PackageMetadata *package_metadata,
                  std::ostream &out) {
    const auto backend = core_backend_for_command(effective_command);
    if (!backend.has_value()) {
        return std::nullopt;
    }

    const auto ir_program = ahfl::lower_program_ir(input, resolve_result, type_check_result);
    return emit_core_backend(
        effective_command, ir_program, resolve_result, type_check_result, package_metadata, out);
}

[[nodiscard]] int validate_assurance_program(const ahfl::ir::Program &program) {
    const auto bundle = ahfl::assurance::build_assurance_bundle(program);
    const auto validation = ahfl::assurance::validate_assurance_bundle(bundle);
    ahfl::assurance::print_assurance_validation_report(validation,
                                                       validation.ok ? std::cout : std::cerr);
    return validation.ok ? 0 : 1;
}

[[nodiscard]] int verify_formal_program(const ahfl::ir::Program &program,
                                        const CommandLineOptions &options) {
    ahfl::formal::FormalCheckerOptions formal_options;
    if (options.model_checker.has_value()) {
        formal_options.checker_path = std::string(*options.model_checker);
    }
    if (options.formal_model_out.has_value()) {
        formal_options.model_output_path = std::filesystem::path(*options.formal_model_out);
    }
    formal_options.explain = options.explain_requested;

    const auto result = ahfl::formal::verify_program_with_smv_checker(program, formal_options);

    if (options.explain_requested &&
        result.status == ahfl::formal::FormalVerificationStatus::Failed) {
        if (result.structured_explanation_json.has_value()) {
            std::cout << *result.structured_explanation_json << '\n';
        } else {
            ahfl::formal::print_formal_verification_report(result, std::cerr);
        }
        return 1;
    }

    ahfl::formal::print_formal_verification_report(
        result, ahfl::formal::is_formal_verification_success(result) ? std::cout : std::cerr);
    return ahfl::formal::is_formal_verification_success(result) ? 0 : 1;
}

[[nodiscard]] bool
read_text_file(const std::filesystem::path &path, std::string &content, std::ostream &diagnostics) {
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
void render_diagnostics(DiagnosticConsumer &consumer, const ResultT &result, MaybeSourceFile source_file) {
    consumer.consume(result.diagnostics, source_file);
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
                                     ahfl::ProjectInput &input,
                                     DiagnosticConsumer &consumer) {
    if (options.project_descriptor.has_value()) {
        auto descriptor_result =
            frontend.load_project_descriptor(std::string(*options.project_descriptor));
        consumer.consume(descriptor_result.diagnostics);
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
        consumer.consume(descriptor_result.diagnostics);
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

} // namespace

// ---------------------------------------------------------------------------
// CliDriver public interface
// ---------------------------------------------------------------------------

ExitCode CliDriver::run(std::span<const std::string_view> arguments) {
    if (auto status = parse_command_line(arguments); status.has_value()) {
        return *status;
    }

    diag_consumer_ = make_diagnostic_consumer("text", std::cerr);

    effective_command_ = infer_effective_command(options_);

    if (auto status = validate_options(); status.has_value()) {
        return *status;
    }

    if (auto status = load_package_and_mocks(); status.has_value()) {
        return *status;
    }

    return execute();
}

// ---------------------------------------------------------------------------
// CliDriver private methods
// ---------------------------------------------------------------------------

std::optional<ExitCode>
CliDriver::parse_command_line(std::span<const std::string_view> arguments) {
    auto result = parse_options_from_table(arguments, options_);
    if (result.has_value()) {
        return result->exit_code == 0 ? ExitCode::Success : ExitCode::UsageError;
    }
    return std::nullopt;
}

std::optional<ExitCode> CliDriver::validate_options() {
    const auto action_count = count_enabled_actions(options_);
    if (action_count > 1) {
        std::cerr << "error: choose at most one of "
                  << format_comma_or_commands(command_list(CommandListKind::Action)) << "\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.project_descriptor.has_value() && !options_.search_roots.empty()) {
        std::cerr << "error: --project cannot be combined with --search-root\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.workspace_descriptor.has_value() && options_.project_descriptor.has_value()) {
        std::cerr << "error: --workspace cannot be combined with --project\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.workspace_descriptor.has_value() && !options_.search_roots.empty()) {
        std::cerr << "error: --workspace cannot be combined with --search-root\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.project_name.has_value() && !options_.workspace_descriptor.has_value()) {
        std::cerr << "error: --project-name requires --workspace\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.workspace_descriptor.has_value() && !options_.project_name.has_value()) {
        std::cerr << "error: --workspace requires --project-name\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.project_descriptor.has_value() || options_.workspace_descriptor.has_value()
            ? !options_.positional.empty()
            : options_.positional.size() != 1) {
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.package_descriptor.has_value() &&
        (!effective_command_.has_value() || !is_package_supported_command(*effective_command_))) {
        std::cerr << "error: --package is only supported with "
                  << format_comma_or_commands(command_list(CommandListKind::PackageSupported))
                  << "\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.capability_mocks_descriptor.has_value() &&
        !supports_capability_inputs(effective_command_)) {
        std::cerr << "error: --capability-mocks is only supported with "
                  << format_comma_or_commands(
                         command_list(CommandListKind::CapabilityInputSupported))
                  << "\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.input_fixture.has_value() && !supports_capability_inputs(effective_command_)) {
        std::cerr << "error: --input-fixture is only supported with "
                  << format_comma_or_commands(
                         command_list(CommandListKind::CapabilityInputSupported))
                  << "\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.run_id.has_value() && !supports_capability_inputs(effective_command_)) {
        std::cerr << "error: --run-id is only supported with "
                  << format_comma_or_commands(
                         command_list(CommandListKind::CapabilityInputSupported))
                  << "\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.model_checker.has_value() && effective_command_ != CommandKind::VerifyFormal) {
        std::cerr << "error: --model-checker is only supported with verify-formal\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.formal_model_out.has_value() && effective_command_ != CommandKind::VerifyFormal) {
        std::cerr << "error: --formal-model-out is only supported with verify-formal\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.explain_requested && effective_command_ != CommandKind::VerifyFormal) {
        std::cerr << "error: --explain is only supported with verify-formal\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.workflow_name.has_value() && !supports_capability_inputs(effective_command_)) {
        std::cerr << "error: --workflow is only supported with "
                  << format_comma_or_commands(
                         command_list(CommandListKind::CapabilityInputSupported))
                  << "\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    return std::nullopt;
}

std::optional<ExitCode> CliDriver::load_package_and_mocks() {
    if (effective_command_.has_value() && is_command_requiring_package(*effective_command_)) {
        const auto selected_command_name = command_name(*effective_command_);
        if (!options_.package_descriptor.has_value()) {
            std::cerr << "error: " << selected_command_name << " requires --package\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
        if (!options_.capability_mocks_descriptor.has_value()) {
            std::cerr << "error: " << selected_command_name << " requires --capability-mocks\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
        if (!options_.input_fixture.has_value()) {
            std::cerr << "error: " << selected_command_name << " requires --input-fixture\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }

        std::string capability_mocks_content;
        if (!read_text_file(std::string(*options_.capability_mocks_descriptor),
                            capability_mocks_content,
                            std::cerr)) {
            return ExitCode::CompileError;
        }

        auto mock_parse_result =
            ahfl::dry_run::parse_capability_mock_set_json(capability_mocks_content);
        diag_consumer_->consume(mock_parse_result.diagnostics);
        if (mock_parse_result.has_errors() || !mock_parse_result.mock_set.has_value()) {
            return ExitCode::CompileError;
        }

        capability_mock_set_ = std::move(*mock_parse_result.mock_set);
    }

    if (options_.package_descriptor.has_value()) {
        auto package_result =
            frontend_.load_package_authoring_descriptor(std::string(*options_.package_descriptor));
        diag_consumer_->consume(package_result.diagnostics);
        if (package_result.has_errors() || !package_result.descriptor.has_value()) {
            return package_result.has_errors() ? ExitCode::CompileError : ExitCode::Success;
        }

        package_metadata_ = lower_package_metadata(*package_result.descriptor);
    }

    return std::nullopt;
}

ExitCode CliDriver::execute() {
    const bool project_mode = options_.project_descriptor.has_value() ||
                              options_.workspace_descriptor.has_value() ||
                              !options_.search_roots.empty();

    if (project_mode || is_action_enabled(options_, CommandKind::DumpProject)) {
        ahfl::ProjectInput input;
        if (const auto load_status = load_project_input(options_, frontend_, input, *diag_consumer_);
            load_status >= 0) {
            return load_status == 0 ? ExitCode::Success : ExitCode::CompileError;
        }

        auto project_result = frontend_.parse_project(input);
        render_diagnostics(*diag_consumer_, project_result, std::nullopt);
        if (project_result.has_errors()) {
            return ExitCode::CompileError;
        }

        if (effective_command_ == CommandKind::DumpProject) {
            ahfl::dump_project_outline(project_result.graph, std::cout);
            return ExitCode::Success;
        }

        if (effective_command_ == CommandKind::DumpAst) {
            dump_ast_outline(project_result.graph, std::cout);
            return ExitCode::Success;
        }

        return run_analysis(project_result.graph, std::nullopt);
    }

    auto parse_result = frontend_.parse_file(std::string(options_.positional.front()));
    render_diagnostics(*diag_consumer_, parse_result, std::cref(parse_result.source));

    if (parse_result.program && effective_command_ == CommandKind::DumpAst) {
        dump_ast_outline(*parse_result.program, std::cout);
    }

    if (parse_result.has_errors() || !parse_result.program) {
        return parse_result.has_errors() ? ExitCode::CompileError : ExitCode::Success;
    }

    return run_analysis(*parse_result.program, std::cref(parse_result.source));
}

template <typename InputT>
ExitCode CliDriver::run_analysis(const InputT &input, MaybeSourceFile source_file) {
    const auto *package_metadata_ptr =
        package_metadata_.has_value() ? &*package_metadata_ : nullptr;
    const auto *capability_mock_set_ptr =
        capability_mock_set_.has_value() ? &*capability_mock_set_ : nullptr;

    const ahfl::Resolver resolver;
    auto resolve_result = resolver.resolve(input);
    render_diagnostics(*diag_consumer_, resolve_result, source_file);
    if (resolve_result.has_errors()) {
        return ExitCode::CompileError;
    }

    const ahfl::TypeChecker type_checker;
    auto type_check_result = type_checker.check(input, resolve_result);
    render_diagnostics(*diag_consumer_, type_check_result, source_file);

    if (is_action_enabled(options_, CommandKind::DumpTypes)) {
        ahfl::dump_type_environment(
            type_check_result.environment, resolve_result.symbol_table, std::cout);
    }

    if (type_check_result.has_errors()) {
        return ExitCode::CompileError;
    }

    const ahfl::Validator validator;
    auto validation_result = validator.validate(input, resolve_result, type_check_result);
    render_diagnostics(*diag_consumer_, validation_result, source_file);
    if (validation_result.has_errors()) {
        return ExitCode::CompileError;
    }

    if (package_metadata_ptr != nullptr) {
        auto ir_program = ahfl::lower_program_ir(input, resolve_result, type_check_result);
        if (options_.optimize_requested) {
            auto pm = ahfl::passes::create_default_pipeline();
            static_cast<void>(pm->run(ir_program));
        }
        auto metadata_validation =
            ahfl::handoff::validate_package_metadata(ir_program, *package_metadata_ptr);
        render_diagnostics(*diag_consumer_, metadata_validation, std::nullopt);
        if (metadata_validation.has_errors()) {
            return ExitCode::CompileError;
        }

        if (effective_command_ == CommandKind::ValidateAssurance) {
            return validate_assurance_program(ir_program) == 0 ? ExitCode::Success
                                                               : ExitCode::CompileError;
        }

        if (effective_command_ == CommandKind::VerifyFormal) {
            return verify_formal_program(ir_program, options_) == 0 ? ExitCode::Success
                                                                    : ExitCode::CompileError;
        }

        if (effective_command_.has_value() && handles_package_command(*effective_command_)) {
            if (const auto command_status =
                    ahfl::cli::dispatch_package_command(*effective_command_,
                                                        ir_program,
                                                        metadata_validation.metadata,
                                                        capability_mock_set_ptr,
                                                        options_);
                command_status.has_value()) {
                return *command_status == 0 ? ExitCode::Success : ExitCode::CompileError;
            }

            std::cerr << "internal error: package pipeline command '"
                      << command_name(*effective_command_) << "' has no dispatcher\n";
            return ExitCode::CompileError;
        }

        if (const auto backend_status = emit_core_backend(effective_command_,
                                                          ir_program,
                                                          resolve_result,
                                                          type_check_result,
                                                          &metadata_validation.metadata,
                                                          std::cout);
            backend_status.has_value()) {
            return *backend_status == 0 ? ExitCode::Success : ExitCode::CompileError;
        }
    }

    if (effective_command_ == CommandKind::ValidateAssurance) {
        const auto ir_program = ahfl::lower_program_ir(input, resolve_result, type_check_result);
        return validate_assurance_program(ir_program) == 0 ? ExitCode::Success
                                                           : ExitCode::CompileError;
    }

    if (effective_command_ == CommandKind::VerifyFormal) {
        const auto ir_program = ahfl::lower_program_ir(input, resolve_result, type_check_result);
        return verify_formal_program(ir_program, options_) == 0 ? ExitCode::Success
                                                                : ExitCode::CompileError;
    }

    if (const auto backend_status = emit_core_backend(effective_command_,
                                                      input,
                                                      resolve_result,
                                                      type_check_result,
                                                      package_metadata_ptr,
                                                      std::cout);
        backend_status.has_value()) {
        return *backend_status == 0 ? ExitCode::Success : ExitCode::CompileError;
    }

    if (!is_action_enabled(options_, CommandKind::DumpTypes)) {
        print_success_summary(input, resolve_result, type_check_result, std::cout);
    }

    return ExitCode::Success;
}

// Explicit template instantiations for the two input types used.
template ExitCode CliDriver::run_analysis<ahfl::ast::Program>(const ahfl::ast::Program &,
                                                               MaybeSourceFile);
template ExitCode CliDriver::run_analysis<ahfl::SourceGraph>(const ahfl::SourceGraph &,
                                                              MaybeSourceFile);

} // namespace ahfl::cli
