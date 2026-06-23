#include "tooling/cli/cli_analysis_helpers.hpp"

#include "ahfl/compiler/backends/driver.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/validate.hpp"
#include "compiler/assurance/assurance.hpp"
#include "verification/formal/checker.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace ahfl::cli {

namespace {

[[nodiscard]] std::optional<int> parse_positive_seconds(std::string_view value) {
    if (value.empty()) {
        return std::nullopt;
    }

    int seconds = 0;
    for (const char character : value) {
        if (character < '0' || character > '9') {
            return std::nullopt;
        }
        constexpr int kMaxAllowedSeconds = 24 * 60 * 60;
        seconds = (seconds * 10) + (character - '0');
        if (seconds > kMaxAllowedSeconds) {
            return std::nullopt;
        }
    }

    if (seconds <= 0) {
        return std::nullopt;
    }
    return seconds;
}

} // namespace

// ---------------------------------------------------------------------------
// compile_to_ir — shared compile pipeline
// ---------------------------------------------------------------------------

std::optional<ir::Program> compile_to_ir(const std::filesystem::path &file_path,
                                         std::ostream &diagnostics) {
    const Frontend frontend;
    const auto parse_result = frontend.parse_project(ProjectInput{
        .entry_files = {file_path},
    });
    if (parse_result.has_errors()) {
        parse_result.diagnostics.render(diagnostics);
        return std::nullopt;
    }

    const Resolver resolver;
    const auto resolve_result = resolver.resolve(parse_result.graph);
    if (resolve_result.has_errors()) {
        resolve_result.diagnostics.render(diagnostics);
        return std::nullopt;
    }

    const TypeChecker type_checker;
    const auto type_check_result = type_checker.check(parse_result.graph, resolve_result);
    if (type_check_result.has_errors()) {
        type_check_result.diagnostics.render(diagnostics);
        return std::nullopt;
    }

    const Validator validator;
    const auto validation_result =
        validator.validate(parse_result.graph, resolve_result, type_check_result);
    if (validation_result.has_errors()) {
        validation_result.diagnostics.render(diagnostics);
        return std::nullopt;
    }

    return lower_program_ir(parse_result.graph, resolve_result, type_check_result);
}

// ---------------------------------------------------------------------------
// lower_package_metadata
// ---------------------------------------------------------------------------

namespace {

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

} // namespace

namespace {

struct SmvSizeReport {
    std::size_t bytes{0};
    std::size_t lines{0};
    std::size_t ltlspecs{0};
};

[[nodiscard]] SmvSizeReport measure_smv_output(std::string_view output) {
    SmvSizeReport report{.bytes = output.size()};
    std::string_view remaining = output;
    while (!remaining.empty()) {
        const auto newline = remaining.find('\n');
        if (newline == std::string_view::npos) {
            break;
        }
        ++report.lines;
        remaining.remove_prefix(newline + 1);
    }
    if (!output.empty() && output.back() != '\n') {
        ++report.lines;
    }

    std::size_t offset = 0;
    while (offset < output.size()) {
        const auto found = output.find("LTLSPEC", offset);
        if (found == std::string_view::npos) {
            break;
        }
        if (found == 0 || output[found - 1] == '\n') {
            ++report.ltlspecs;
        }
        offset = found + std::string_view{"LTLSPEC"}.size();
    }
    return report;
}

void print_smv_size_report(const SmvSizeReport &report, std::ostream &err) {
    err << "smv size report:\n"
        << "  bytes: " << report.bytes << '\n'
        << "  lines: " << report.lines << '\n'
        << "  ltlspecs: " << report.ltlspecs << '\n';
}

} // namespace

ahfl::handoff::PackageMetadata
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

// ---------------------------------------------------------------------------
// emit_core_backend (non-template overload)
// ---------------------------------------------------------------------------

std::optional<int> emit_core_backend(std::optional<CommandKind> effective_command,
                                     ahfl::ir::Program &program,
                                     const ahfl::ResolveResult &resolve_result,
                                     const ahfl::TypeCheckResult &type_check_result,
                                     const ahfl::handoff::PackageMetadata *package_metadata,
                                     const CommandLineOptions &options,
                                     std::ostream &out,
                                     std::ostream &err) {
    static_cast<void>(resolve_result);
    static_cast<void>(type_check_result);

    const auto backend = core_backend_for_command(effective_command);
    if (!backend.has_value()) {
        return std::nullopt;
    }

    const bool needs_smv_size_report =
        options.smv_size_report_requested && effective_command == CommandKind::EmitSmv;

    if (!needs_smv_size_report) {
        auto result = ahfl::emit_backend(*backend, program, out, package_metadata);
        if (!result.has_value()) {
            err << "internal error: core backend command ";
            if (effective_command.has_value()) {
                err << "'" << command_name(*effective_command) << "' ";
            }
            err << "failed: " << result.error() << "\n";
            return 1;
        }
        return 0;
    }

    std::ostringstream buffered_output;
    auto result = ahfl::emit_backend(*backend, program, buffered_output, package_metadata);
    if (!result.has_value()) {
        err << "internal error: core backend command ";
        if (effective_command.has_value()) {
            err << "'" << command_name(*effective_command) << "' ";
        }
        err << "failed: " << result.error() << "\n";
        return 1;
    }

    const auto output = buffered_output.str();
    out << output;
    print_smv_size_report(measure_smv_output(output), err);

    return 0;
}

// ---------------------------------------------------------------------------
// validate_assurance_program
// ---------------------------------------------------------------------------

int validate_assurance_program(const ahfl::ir::Program &program) {
    const auto bundle = ahfl::assurance::build_assurance_bundle(program);
    const auto validation = ahfl::assurance::validate_assurance_bundle(bundle);
    ahfl::assurance::print_assurance_validation_report(validation,
                                                       validation.ok ? std::cout : std::cerr);
    return validation.ok ? 0 : 1;
}

// ---------------------------------------------------------------------------
// verify_formal_program
// ---------------------------------------------------------------------------

int verify_formal_program(const ahfl::ir::Program &program, const CommandLineOptions &options) {
    ahfl::formal::FormalCheckerOptions formal_options;
    if (options.formal_backend.has_value()) {
        if (auto backend = ahfl::formal::parse_model_checker_kind(*options.formal_backend);
            backend.has_value()) {
            formal_options.backend_kind = *backend;
        }
    }
    if (options.model_checker.has_value()) {
        formal_options.checker_path = std::string(*options.model_checker);
    }
    if (options.formal_model_out.has_value()) {
        formal_options.model_output_path = std::filesystem::path(*options.formal_model_out);
    }
    if (options.checker_timeout_seconds.has_value()) {
        if (const auto seconds = parse_positive_seconds(*options.checker_timeout_seconds);
            seconds.has_value()) {
            formal_options.checker_timeout = std::chrono::seconds{*seconds};
        }
    }
    formal_options.explain = options.explain_requested;

    const auto result = ahfl::formal::verify_program_with_smv_checker(program, formal_options);

    ahfl::formal::print_formal_verification_report(
        result, ahfl::formal::is_formal_verification_success(result) ? std::cout : std::cerr);
    return ahfl::formal::is_formal_verification_success(result) ? 0 : 1;
}

// ---------------------------------------------------------------------------
// read_text_file
// ---------------------------------------------------------------------------

bool read_text_file(const std::filesystem::path &path,
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

// ---------------------------------------------------------------------------
// dump_ast_outline
// ---------------------------------------------------------------------------

void dump_ast_outline(const ahfl::ast::Program &program, std::ostream &out) {
    ahfl::dump_program_outline(program, out);
}

void dump_ast_outline(const ahfl::SourceGraph &graph, std::ostream &out) {
    ahfl::dump_project_ast_outline(graph, out);
}

// ---------------------------------------------------------------------------
// print_success_summary
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// load_project_input
// ---------------------------------------------------------------------------

int load_project_input(const CommandLineOptions &options,
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

} // namespace ahfl::cli
