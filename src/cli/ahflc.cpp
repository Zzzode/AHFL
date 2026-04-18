#include "ahfl/backend.hpp"
#include "ahfl/audit_report/report.hpp"
#include "ahfl/backends/audit_report.hpp"
#include "ahfl/backends/dry_run_trace.hpp"
#include "ahfl/backends/execution_plan.hpp"
#include "ahfl/backends/execution_journal.hpp"
#include "ahfl/backends/replay_view.hpp"
#include "ahfl/backends/runtime_session.hpp"
#include "ahfl/dry_run/runner.hpp"
#include "ahfl/execution_journal/journal.hpp"
#include "ahfl/frontend/frontend.hpp"
#include "ahfl/replay_view/replay.hpp"
#include "ahfl/runtime_session/session.hpp"
#include "ahfl/semantics/resolver.hpp"
#include "ahfl/semantics/typecheck.hpp"
#include "ahfl/semantics/validate.hpp"

#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using MaybeSourceFile = std::optional<std::reference_wrapper<const ahfl::SourceFile>>;

struct CommandLineOptions {
    bool dump_ast{false};
    bool dump_types{false};
    bool dump_project{false};
    bool emit_ir{false};
    bool emit_ir_json{false};
    bool emit_native_json{false};
    bool emit_execution_plan{false};
    bool emit_execution_journal{false};
    bool emit_replay_view{false};
    bool emit_audit_report{false};
    bool emit_runtime_session{false};
    bool emit_dry_run_trace{false};
    bool emit_package_review{false};
    bool emit_summary{false};
    bool emit_smv{false};
    std::optional<std::string_view> package_descriptor;
    std::optional<std::string_view> capability_mocks_descriptor;
    std::optional<std::string_view> project_descriptor;
    std::optional<std::string_view> workspace_descriptor;
    std::optional<std::string_view> project_name;
    std::optional<std::string_view> workflow_name;
    std::optional<std::string_view> input_fixture;
    std::optional<std::string_view> run_id;
    std::vector<std::string_view> search_roots;
    std::vector<std::string_view> positional;
};

void print_usage(std::ostream &out) {
    out << "Usage:\n"
        << "  ahflc "
           "<check|dump-ast|dump-project|dump-types|emit-ir|emit-ir-json|emit-native-json|emit-"
           "execution-plan|emit-execution-journal|emit-replay-view|emit-audit-report|emit-runtime-session|emit-dry-run-trace|emit-"
           "package-review|emit-summary|emit-smv> [--package <ahfl.package.json>] --project "
           "<ahfl.project.json>\n"
        << "  ahflc "
           "<check|dump-ast|dump-project|dump-types|emit-ir|emit-ir-json|emit-native-json|emit-"
           "execution-plan|emit-execution-journal|emit-replay-view|emit-audit-report|emit-runtime-session|emit-dry-run-trace|emit-"
           "package-review|emit-summary|emit-smv> [--package <ahfl.package.json>] --workspace "
           "<ahfl.workspace.json> --project-name <name>\n"
        << "  ahflc check [--search-root <dir>]... [--dump-ast] <input.ahfl>\n"
        << "  ahflc dump-ast [--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc dump-types [--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc dump-project [--search-root <dir>]... <entry.ahfl>\n"
        << "  ahflc emit-ir [--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc emit-ir-json [--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc emit-native-json [--package <ahfl.package.json>] [--search-root <dir>]... "
           "<input.ahfl>\n"
        << "  ahflc emit-execution-plan [--package <ahfl.package.json>] [--search-root <dir>]... "
           "<input.ahfl>\n"
        << "  ahflc emit-execution-journal --package <ahfl.package.json> --capability-mocks "
           "<mocks.json> --input-fixture <fixture> [--workflow <canonical>] [--run-id <id>] "
           "[--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc emit-replay-view --package <ahfl.package.json> --capability-mocks "
           "<mocks.json> --input-fixture <fixture> [--workflow <canonical>] [--run-id <id>] "
           "[--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc emit-audit-report --package <ahfl.package.json> --capability-mocks "
           "<mocks.json> --input-fixture <fixture> [--workflow <canonical>] [--run-id <id>] "
           "[--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc emit-runtime-session --package <ahfl.package.json> --capability-mocks "
           "<mocks.json> --input-fixture <fixture> [--workflow <canonical>] [--run-id <id>] "
           "[--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc emit-dry-run-trace --package <ahfl.package.json> --capability-mocks "
           "<mocks.json> --input-fixture <fixture> [--workflow <canonical>] [--run-id <id>] "
           "[--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc emit-package-review [--package <ahfl.package.json>] [--search-root <dir>]... "
           "<input.ahfl>\n"
        << "  ahflc emit-summary [--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc emit-smv [--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc [--dump-ast] <input.ahfl>\n";
}

[[nodiscard]] bool is_subcommand(std::string_view argument) {
    return argument == "check" || argument == "dump-ast" || argument == "dump-types" ||
           argument == "dump-project" || argument == "emit-ir" || argument == "emit-ir-json" ||
           argument == "emit-native-json" || argument == "emit-execution-plan" ||
           argument == "emit-execution-journal" || argument == "emit-replay-view" ||
           argument == "emit-audit-report" || argument == "emit-runtime-session" ||
           argument == "emit-dry-run-trace" ||
           argument == "emit-package-review" ||
           argument == "emit-summary" || argument == "emit-smv";
}

[[nodiscard]] std::optional<ahfl::BackendKind> selected_backend(const CommandLineOptions &options) {
    if (options.emit_ir) {
        return ahfl::BackendKind::Ir;
    }

    if (options.emit_ir_json) {
        return ahfl::BackendKind::IrJson;
    }

    if (options.emit_native_json) {
        return ahfl::BackendKind::NativeJson;
    }

    if (options.emit_execution_plan) {
        return ahfl::BackendKind::ExecutionPlan;
    }

    if (options.emit_execution_journal) {
        return std::nullopt;
    }

    if (options.emit_replay_view) {
        return std::nullopt;
    }

    if (options.emit_audit_report) {
        return std::nullopt;
    }

    if (options.emit_runtime_session) {
        return std::nullopt;
    }

    if (options.emit_dry_run_trace) {
        return std::nullopt;
    }

    if (options.emit_package_review) {
        return ahfl::BackendKind::PackageReview;
    }

    if (options.emit_summary) {
        return ahfl::BackendKind::Summary;
    }

    if (options.emit_smv) {
        return ahfl::BackendKind::Smv;
    }

    return std::nullopt;
}

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
[[nodiscard]] bool emit_selected_backend(const CommandLineOptions &options,
                                         const InputT &input,
                                         const ahfl::ResolveResult &resolve_result,
                                         const ahfl::TypeCheckResult &type_check_result,
                                         const ahfl::handoff::PackageMetadata *package_metadata,
                                         std::ostream &out) {
    const auto backend = selected_backend(options);
    if (!backend.has_value()) {
        return false;
    }

    ahfl::emit_backend(
        *backend, input, resolve_result, type_check_result, out, package_metadata);
    return true;
}

[[nodiscard]] int emit_execution_plan_with_diagnostics(const ahfl::ir::Program &program,
                                                       const ahfl::handoff::PackageMetadata &metadata) {
    const auto result = ahfl::handoff::build_execution_plan(
        ahfl::handoff::lower_package(program, metadata));
    result.diagnostics.render(std::cerr);
    if (result.has_errors() || !result.plan.has_value()) {
        return 1;
    }

    ahfl::print_execution_plan_json(*result.plan, std::cout);
    return 0;
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

[[nodiscard]] int emit_dry_run_trace_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto plan_result = ahfl::handoff::build_execution_plan(
        ahfl::handoff::lower_package(program, metadata));
    plan_result.diagnostics.render(std::cerr);
    if (plan_result.has_errors() || !plan_result.plan.has_value()) {
        return 1;
    }

    auto workflow_name =
        options.workflow_name.transform([](std::string_view value) { return std::string(value); });
    if (!workflow_name.has_value()) {
        workflow_name = plan_result.plan->entry_workflow_canonical_name;
    }

    if (!workflow_name.has_value()) {
        std::cerr << "error: emit-dry-run-trace requires --workflow or package workflow entry\n";
        return 1;
    }

    const auto dry_run = ahfl::dry_run::run_local_dry_run(
        *plan_result.plan,
        ahfl::dry_run::DryRunRequest{
            .workflow_canonical_name = std::move(*workflow_name),
            .input_fixture = std::string(*options.input_fixture),
            .run_id =
                options.run_id.transform([](std::string_view value) { return std::string(value); }),
        },
        mock_set);
    dry_run.diagnostics.render(std::cerr);
    if (dry_run.has_errors() || !dry_run.trace.has_value()) {
        return 1;
    }

    ahfl::print_dry_run_trace_json(*dry_run.trace, std::cout);
    return 0;
}

[[nodiscard]] int emit_runtime_session_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto plan_result = ahfl::handoff::build_execution_plan(
        ahfl::handoff::lower_package(program, metadata));
    plan_result.diagnostics.render(std::cerr);
    if (plan_result.has_errors() || !plan_result.plan.has_value()) {
        return 1;
    }

    auto workflow_name =
        options.workflow_name.transform([](std::string_view value) { return std::string(value); });
    if (!workflow_name.has_value()) {
        workflow_name = plan_result.plan->entry_workflow_canonical_name;
    }

    if (!workflow_name.has_value()) {
        std::cerr
            << "error: emit-runtime-session requires --workflow or package workflow entry\n";
        return 1;
    }

    const auto session = ahfl::runtime_session::build_runtime_session(
        *plan_result.plan,
        ahfl::dry_run::DryRunRequest{
            .workflow_canonical_name = std::move(*workflow_name),
            .input_fixture = std::string(*options.input_fixture),
            .run_id =
                options.run_id.transform([](std::string_view value) { return std::string(value); }),
        },
        mock_set);
    session.diagnostics.render(std::cerr);
    if (session.has_errors() || !session.session.has_value()) {
        return 1;
    }

    ahfl::print_runtime_session_json(*session.session, std::cout);
    return 0;
}

[[nodiscard]] int emit_execution_journal_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto plan_result = ahfl::handoff::build_execution_plan(
        ahfl::handoff::lower_package(program, metadata));
    plan_result.diagnostics.render(std::cerr);
    if (plan_result.has_errors() || !plan_result.plan.has_value()) {
        return 1;
    }

    auto workflow_name =
        options.workflow_name.transform([](std::string_view value) { return std::string(value); });
    if (!workflow_name.has_value()) {
        workflow_name = plan_result.plan->entry_workflow_canonical_name;
    }

    if (!workflow_name.has_value()) {
        std::cerr
            << "error: emit-execution-journal requires --workflow or package workflow entry\n";
        return 1;
    }

    const auto session = ahfl::runtime_session::build_runtime_session(
        *plan_result.plan,
        ahfl::dry_run::DryRunRequest{
            .workflow_canonical_name = std::move(*workflow_name),
            .input_fixture = std::string(*options.input_fixture),
            .run_id =
                options.run_id.transform([](std::string_view value) { return std::string(value); }),
        },
        mock_set);
    session.diagnostics.render(std::cerr);
    if (session.has_errors() || !session.session.has_value()) {
        return 1;
    }

    const auto journal = ahfl::execution_journal::build_execution_journal(*session.session);
    journal.diagnostics.render(std::cerr);
    if (journal.has_errors() || !journal.journal.has_value()) {
        return 1;
    }

    ahfl::print_execution_journal_json(*journal.journal, std::cout);
    return 0;
}

[[nodiscard]] int emit_replay_view_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto plan_result = ahfl::handoff::build_execution_plan(
        ahfl::handoff::lower_package(program, metadata));
    plan_result.diagnostics.render(std::cerr);
    if (plan_result.has_errors() || !plan_result.plan.has_value()) {
        return 1;
    }

    auto workflow_name =
        options.workflow_name.transform([](std::string_view value) { return std::string(value); });
    if (!workflow_name.has_value()) {
        workflow_name = plan_result.plan->entry_workflow_canonical_name;
    }

    if (!workflow_name.has_value()) {
        std::cerr << "error: emit-replay-view requires --workflow or package workflow entry\n";
        return 1;
    }

    const auto session = ahfl::runtime_session::build_runtime_session(
        *plan_result.plan,
        ahfl::dry_run::DryRunRequest{
            .workflow_canonical_name = std::move(*workflow_name),
            .input_fixture = std::string(*options.input_fixture),
            .run_id =
                options.run_id.transform([](std::string_view value) { return std::string(value); }),
        },
        mock_set);
    session.diagnostics.render(std::cerr);
    if (session.has_errors() || !session.session.has_value()) {
        return 1;
    }

    const auto journal = ahfl::execution_journal::build_execution_journal(*session.session);
    journal.diagnostics.render(std::cerr);
    if (journal.has_errors() || !journal.journal.has_value()) {
        return 1;
    }

    const auto replay =
        ahfl::replay_view::build_replay_view(*plan_result.plan, *session.session, *journal.journal);
    replay.diagnostics.render(std::cerr);
    if (replay.has_errors() || !replay.replay.has_value()) {
        return 1;
    }

    ahfl::print_replay_view_json(*replay.replay, std::cout);
    return 0;
}

[[nodiscard]] int emit_audit_report_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto plan_result = ahfl::handoff::build_execution_plan(
        ahfl::handoff::lower_package(program, metadata));
    plan_result.diagnostics.render(std::cerr);
    if (plan_result.has_errors() || !plan_result.plan.has_value()) {
        return 1;
    }

    auto workflow_name =
        options.workflow_name.transform([](std::string_view value) { return std::string(value); });
    if (!workflow_name.has_value()) {
        workflow_name = plan_result.plan->entry_workflow_canonical_name;
    }

    if (!workflow_name.has_value()) {
        std::cerr << "error: emit-audit-report requires --workflow or package workflow entry\n";
        return 1;
    }

    const auto request = ahfl::dry_run::DryRunRequest{
        .workflow_canonical_name = *workflow_name,
        .input_fixture = std::string(*options.input_fixture),
        .run_id =
            options.run_id.transform([](std::string_view value) { return std::string(value); }),
    };

    const auto session =
        ahfl::runtime_session::build_runtime_session(*plan_result.plan, request, mock_set);
    session.diagnostics.render(std::cerr);
    if (session.has_errors() || !session.session.has_value()) {
        return 1;
    }

    const auto journal = ahfl::execution_journal::build_execution_journal(*session.session);
    journal.diagnostics.render(std::cerr);
    if (journal.has_errors() || !journal.journal.has_value()) {
        return 1;
    }

    const auto trace = ahfl::dry_run::run_local_dry_run(*plan_result.plan, request, mock_set);
    trace.diagnostics.render(std::cerr);
    if (trace.has_errors() || !trace.trace.has_value()) {
        return 1;
    }

    const auto report = ahfl::audit_report::build_audit_report(
        *plan_result.plan, *session.session, *journal.journal, *trace.trace);
    report.diagnostics.render(std::cerr);
    if (report.has_errors() || !report.report.has_value()) {
        return 1;
    }

    ahfl::print_audit_report_json(*report.report, std::cout);
    return 0;
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
        << resolve_result.references.size() << " reference(s), "
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
        << resolve_result.references.size() << " reference(s), "
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

    if (options.dump_types) {
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

        if (options.emit_dry_run_trace) {
            return emit_dry_run_trace_with_diagnostics(
                ir_program, metadata_validation.metadata, *capability_mock_set, options);
        }

        if (options.emit_execution_journal) {
            return emit_execution_journal_with_diagnostics(
                ir_program, metadata_validation.metadata, *capability_mock_set, options);
        }

        if (options.emit_replay_view) {
            return emit_replay_view_with_diagnostics(
                ir_program, metadata_validation.metadata, *capability_mock_set, options);
        }

        if (options.emit_audit_report) {
            return emit_audit_report_with_diagnostics(
                ir_program, metadata_validation.metadata, *capability_mock_set, options);
        }

        if (options.emit_runtime_session) {
            return emit_runtime_session_with_diagnostics(
                ir_program, metadata_validation.metadata, *capability_mock_set, options);
        }

        if (options.emit_execution_plan) {
            return emit_execution_plan_with_diagnostics(ir_program, metadata_validation.metadata);
        }

        if (emit_selected_backend(options,
                                  ir_program,
                                  resolve_result,
                                  type_check_result,
                                  &metadata_validation.metadata,
                                  std::cout)) {
            return 0;
        }
    }

    if (emit_selected_backend(
            options, input, resolve_result, type_check_result, package_metadata, std::cout)) {
        return 0;
    }

    if (!options.dump_types) {
        print_success_summary(input, resolve_result, type_check_result, std::cout);
    }

    return 0;
}

[[nodiscard]] int parse_command_line(std::span<const std::string_view> arguments,
                                     CommandLineOptions &options) {
    bool explicit_subcommand = false;
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
            options.dump_ast = true;
            continue;
        }

        if (argument == "--dump-types") {
            options.dump_types = true;
            continue;
        }

        if (is_subcommand(argument) && !explicit_subcommand && options.positional.empty()) {
            explicit_subcommand = true;
            if (argument == "dump-ast") {
                options.dump_ast = true;
            }
            if (argument == "dump-types") {
                options.dump_types = true;
            }
            if (argument == "dump-project") {
                options.dump_project = true;
            }
            if (argument == "emit-ir") {
                options.emit_ir = true;
            }
            if (argument == "emit-ir-json") {
                options.emit_ir_json = true;
            }
            if (argument == "emit-native-json") {
                options.emit_native_json = true;
            }
            if (argument == "emit-execution-plan") {
                options.emit_execution_plan = true;
            }
            if (argument == "emit-execution-journal") {
                options.emit_execution_journal = true;
            }
            if (argument == "emit-replay-view") {
                options.emit_replay_view = true;
            }
            if (argument == "emit-audit-report") {
                options.emit_audit_report = true;
            }
            if (argument == "emit-runtime-session") {
                options.emit_runtime_session = true;
            }
            if (argument == "emit-dry-run-trace") {
                options.emit_dry_run_trace = true;
            }
            if (argument == "emit-package-review") {
                options.emit_package_review = true;
            }
            if (argument == "emit-summary") {
                options.emit_summary = true;
            }
            if (argument == "emit-smv") {
                options.emit_smv = true;
            }
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

    const auto action_count =
        static_cast<int>(options.dump_ast) + static_cast<int>(options.dump_types) +
        static_cast<int>(options.dump_project) + static_cast<int>(options.emit_ir) +
        static_cast<int>(options.emit_ir_json) + static_cast<int>(options.emit_native_json) +
        static_cast<int>(options.emit_execution_plan) +
        static_cast<int>(options.emit_execution_journal) +
        static_cast<int>(options.emit_replay_view) +
        static_cast<int>(options.emit_audit_report) +
        static_cast<int>(options.emit_runtime_session) +
        static_cast<int>(options.emit_dry_run_trace) +
        static_cast<int>(options.emit_package_review) + static_cast<int>(options.emit_summary) +
        static_cast<int>(options.emit_smv);
    if (action_count > 1) {
        std::cerr << "error: choose at most one of dump-ast, dump-types, dump-project, emit-ir, "
           "emit-ir-json, emit-native-json, emit-execution-plan, emit-execution-journal, emit-replay-view, emit-audit-report, emit-runtime-session, emit-dry-run-trace, "
           "emit-package-review, emit-summary, or emit-smv\n";
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
        !options.emit_native_json &&
        !options.emit_execution_plan &&
        !options.emit_execution_journal &&
        !options.emit_replay_view &&
        !options.emit_audit_report &&
        !options.emit_runtime_session &&
        !options.emit_dry_run_trace &&
        !options.emit_package_review) {
        std::cerr << "error: --package is only supported with emit-native-json, emit-execution-plan, emit-execution-journal, emit-replay-view, emit-audit-report, emit-runtime-session, emit-dry-run-trace, or emit-package-review\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.capability_mocks_descriptor.has_value() && !options.emit_dry_run_trace &&
        !options.emit_execution_journal &&
        !options.emit_replay_view &&
        !options.emit_audit_report &&
        !options.emit_runtime_session) {
        std::cerr << "error: --capability-mocks is only supported with emit-execution-journal, emit-replay-view, emit-audit-report, emit-runtime-session, or emit-dry-run-trace\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.input_fixture.has_value() && !options.emit_dry_run_trace &&
        !options.emit_execution_journal &&
        !options.emit_replay_view &&
        !options.emit_audit_report &&
        !options.emit_runtime_session) {
        std::cerr << "error: --input-fixture is only supported with emit-execution-journal, emit-replay-view, emit-audit-report, emit-runtime-session, or emit-dry-run-trace\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.run_id.has_value() && !options.emit_dry_run_trace &&
        !options.emit_execution_journal &&
        !options.emit_replay_view &&
        !options.emit_audit_report &&
        !options.emit_runtime_session) {
        std::cerr << "error: --run-id is only supported with emit-execution-journal, emit-replay-view, emit-audit-report, emit-runtime-session, or emit-dry-run-trace\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.workflow_name.has_value() && !options.emit_dry_run_trace &&
        !options.emit_execution_journal &&
        !options.emit_replay_view &&
        !options.emit_audit_report &&
        !options.emit_runtime_session) {
        std::cerr << "error: --workflow is only supported with emit-execution-journal, emit-replay-view, emit-audit-report, emit-runtime-session, or emit-dry-run-trace\n";
        print_usage(std::cerr);
        return 2;
    }

    std::optional<ahfl::dry_run::CapabilityMockSet> capability_mock_set;
    if (options.emit_dry_run_trace || options.emit_execution_journal ||
        options.emit_replay_view || options.emit_audit_report ||
        options.emit_runtime_session) {
        if (!options.package_descriptor.has_value()) {
            std::cerr << "error: "
                      << (options.emit_execution_journal
                              ? "emit-execution-journal"
                              : options.emit_replay_view
                                    ? "emit-replay-view"
                              : options.emit_audit_report
                                    ? "emit-audit-report"
                              : options.emit_runtime_session ? "emit-runtime-session"
                                                             : "emit-dry-run-trace")
                      << " requires --package\n";
            print_usage(std::cerr);
            return 2;
        }
        if (!options.capability_mocks_descriptor.has_value()) {
            std::cerr << "error: "
                      << (options.emit_execution_journal
                              ? "emit-execution-journal"
                              : options.emit_replay_view
                                    ? "emit-replay-view"
                              : options.emit_audit_report
                                    ? "emit-audit-report"
                              : options.emit_runtime_session ? "emit-runtime-session"
                                                             : "emit-dry-run-trace")
                      << " requires --capability-mocks\n";
            print_usage(std::cerr);
            return 2;
        }
        if (!options.input_fixture.has_value()) {
            std::cerr << "error: "
                      << (options.emit_execution_journal
                              ? "emit-execution-journal"
                              : options.emit_replay_view
                                    ? "emit-replay-view"
                              : options.emit_audit_report
                                    ? "emit-audit-report"
                              : options.emit_runtime_session ? "emit-runtime-session"
                                                             : "emit-dry-run-trace")
                      << " requires --input-fixture\n";
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
    if (options.dump_project || project_mode) {
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

        if (options.dump_project) {
            ahfl::dump_project_outline(project_result.graph, std::cout);
            return 0;
        }

        if (options.dump_ast) {
            dump_ast_outline(project_result.graph, std::cout);
            return 0;
        }

        return run_analysis_pipeline(
            options,
            project_result.graph,
            std::nullopt,
            package_metadata ? &*package_metadata : nullptr,
            capability_mock_set ? &*capability_mock_set : nullptr);
    }

    auto parse_result = frontend.parse_file(std::string(options.positional.front()));
    render_diagnostics(parse_result, std::cref(parse_result.source), std::cerr);

    if (parse_result.program && options.dump_ast) {
        dump_ast_outline(*parse_result.program, std::cout);
    }

    if (parse_result.has_errors() || !parse_result.program) {
        return parse_result.has_errors() ? 1 : 0;
    }

    return run_analysis_pipeline(options,
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
