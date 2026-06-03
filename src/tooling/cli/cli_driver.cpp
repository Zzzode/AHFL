#include "tooling/cli/cli_driver.hpp"

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/validate.hpp"
#include "compiler/passes/pass_manager.hpp"
#include "pipeline/execution/dry_run/runner.hpp"
#include "tooling/cli/cli_analysis_helpers.hpp"
#include "tooling/cli/option_table.hpp"
#include "tooling/cli/pipeline_runner.hpp"
#include "tooling/cli/provider/pipeline_durable_store_import_provider.hpp"
#include "tooling/cli/workflow_run.hpp"

#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ahfl::cli {

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

std::optional<ExitCode> CliDriver::parse_command_line(std::span<const std::string_view> arguments) {
    auto result = parse_options_from_table(arguments, options_);
    if (result.has_value()) {
        return result->exit_code == 0 ? ExitCode::Success : ExitCode::UsageError;
    }
    return std::nullopt;
}

std::optional<ExitCode> CliDriver::validate_options() {
    const auto action_count = count_enabled_actions(options_);
    const auto selected_action = selected_action_from_options(options_, effective_command_);
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
        !selected_action_supports_package(selected_action)) {
        std::cerr << "error: --package is only supported with "
                  << format_comma_or_commands(command_list(CommandListKind::PackageSupported))
                  << ", or emit provider/<artifact>"
                  << "\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.capability_mocks_descriptor.has_value() &&
        !selected_action_supports_capability_inputs(selected_action)) {
        std::cerr << "error: --capability-mocks is only supported with "
                  << format_comma_or_commands(
                         command_list(CommandListKind::CapabilityInputSupported))
                  << ", or emit provider/<artifact>"
                  << "\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.input_fixture.has_value() &&
        !selected_action_supports_capability_inputs(selected_action)) {
        std::cerr << "error: --input-fixture is only supported with "
                  << format_comma_or_commands(
                         command_list(CommandListKind::CapabilityInputSupported))
                  << ", or emit provider/<artifact>"
                  << "\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.run_id.has_value() &&
        !selected_action_supports_capability_inputs(selected_action)) {
        std::cerr << "error: --run-id is only supported with "
                  << format_comma_or_commands(
                         command_list(CommandListKind::CapabilityInputSupported))
                  << ", or emit provider/<artifact>"
                  << "\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.runtime_input_json.has_value() && effective_command_ != CommandKind::RunWorkflow) {
        std::cerr << "error: --input is only supported with run\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.llm_config_descriptor.has_value() &&
        effective_command_ != CommandKind::RunWorkflow) {
        std::cerr << "error: --llm-config is only supported with run\n";
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

    if (options_.workflow_name.has_value() && effective_command_ != CommandKind::RunWorkflow &&
        !selected_action_supports_capability_inputs(selected_action)) {
        std::cerr << "error: --workflow is only supported with "
                  << format_comma_or_commands(
                         command_list(CommandListKind::CapabilityInputSupported))
                  << ", or emit provider/<artifact>"
                  << "\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (effective_command_ == CommandKind::RunWorkflow && !options_.workflow_name.has_value()) {
        std::cerr << "error: run requires --workflow\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (effective_command_ == CommandKind::RunWorkflow &&
        !options_.runtime_input_json.has_value()) {
        std::cerr << "error: run requires --input\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    return std::nullopt;
}

std::optional<ExitCode> CliDriver::load_package_and_mocks() {
    const auto selected_action = selected_action_from_options(options_, effective_command_);
    if (selected_action_requires_package(selected_action)) {
        const auto action_name = selected_action_name(selected_action);
        if (!options_.package_descriptor.has_value()) {
            std::cerr << "error: " << action_name << " requires --package\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
        if (!options_.capability_mocks_descriptor.has_value()) {
            std::cerr << "error: " << action_name << " requires --capability-mocks\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
        if (!options_.input_fixture.has_value()) {
            std::cerr << "error: " << action_name << " requires --input-fixture\n";
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
        if (const auto load_status =
                load_project_input(options_, frontend_, input, *diag_consumer_);
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

    if (effective_command_ == CommandKind::RunWorkflow) {
        auto ir_program = ahfl::lower_program_ir(input, resolve_result, type_check_result);
        if (options_.optimize_requested) {
            auto pm = ahfl::passes::create_default_pipeline();
            static_cast<void>(pm->run(ir_program));
        }
        const auto status = run_workflow_with_llm(ir_program, options_, std::cout, std::cerr);
        return status == 0 ? ExitCode::Success : ExitCode::CompileError;
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

        if (options_.selected_provider_artifact.has_value()) {
            if (capability_mock_set_ptr == nullptr) {
                std::cerr << "internal error: provider artifact command missing capability mocks\n";
                return ExitCode::CompileError;
            }
            const auto status =
                emit_provider_artifact_with_diagnostics(*options_.selected_provider_artifact,
                                                        ir_program,
                                                        metadata_validation.metadata,
                                                        *capability_mock_set_ptr,
                                                        options_);
            return status == 0 ? ExitCode::Success : ExitCode::CompileError;
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
