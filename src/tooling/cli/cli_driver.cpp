#include "tooling/cli/cli_driver.hpp"

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/ir/verify.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/validate.hpp"
#include "base/support/json.hpp"
#include "compiler/ir/opt/opt_json.hpp"
#include "compiler/ir/opt/opt_lower.hpp"
#include "compiler/ir/opt/opt_passes.hpp"
#include "compiler/ir/opt/opt_print.hpp"
#include "compiler/ir/opt/opt_verify.hpp"
#include "compiler/passes/pass_manager.hpp"
#include "pipeline/execution/dry_run/runner.hpp"
#include "tooling/cli/cli_analysis_helpers.hpp"
#include "tooling/cli/option_table.hpp"
#include "tooling/cli/pipeline_runner.hpp"
#include "tooling/cli/provider/pipeline_durable_store_import_provider.hpp"
#include "tooling/cli/workflow_run.hpp"
#include "tooling/formatter/format_config.hpp"
#include "tooling/formatter/formatter.hpp"
#include "tooling/profiling/memory_tracker.hpp"
#include "tooling/telemetry/logging.hpp"
#include "tooling/telemetry/metrics.hpp"
#include "tooling/telemetry/trace.hpp"
#include "verification/formal/checker.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace ahfl::cli {

namespace {

ahfl::passes::PassManager::RunResult
run_requested_semantic_optimization_pipeline(ahfl::ir::Program &program) {
    auto semantic_pipeline = ahfl::passes::create_default_pipeline();
    return semantic_pipeline->run(program);
}

void print_pass_timing_report(const ahfl::passes::PassManager::RunResult &result,
                              std::ostream &err) {
    err << "pass timings:\n";
    double total_ms = 0.0;
    for (const auto &[pass_name, duration_ms] : result.timings_ms) {
        total_ms += duration_ms;
        err << "  " << pass_name << ": " << duration_ms << " ms\n";
    }
    err << "  total: " << total_ms << " ms\n";
}

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

void run_requested_semantic_optimization_pipeline(ahfl::ir::Program &program,
                                                  const CommandLineOptions &options,
                                                  std::ostream &err) {
    const auto result = run_requested_semantic_optimization_pipeline(program);
    if (options.time_passes_requested) {
        print_pass_timing_report(result, err);
    }
}

void print_opt_verification_errors(const ahfl::ir::opt::VerificationResult &result,
                                   std::ostream &err) {
    for (const auto &diagnostic : result.diagnostics) {
        if (diagnostic.severity != ahfl::ir::opt::VerificationSeverity::Error) {
            continue;
        }
        err << "error: invalid Opt IR";
        if (!diagnostic.function_name.empty()) {
            err << " in " << diagnostic.function_name;
        }
        if (diagnostic.block_id.has_value()) {
            err << " bb" << *diagnostic.block_id;
        }
        err << ": " << diagnostic.message << '\n';
    }
}

void print_ir_verification_errors(const ahfl::ir::VerificationResult &result, std::ostream &err) {
    for (const auto &diagnostic : result.diagnostics) {
        if (diagnostic.severity != ahfl::ir::VerificationSeverity::Error) {
            continue;
        }
        err << "error: invalid IR";
        if (!diagnostic.path.empty()) {
            err << " at " << diagnostic.path;
        }
        err << ": " << diagnostic.message << '\n';
    }
}

template <typename InputT>
std::optional<ahfl::ir::Program>
lower_verified_ir_or_report(const InputT &input,
                            const ahfl::ResolveResult &resolve_result,
                            const ahfl::TypeCheckResult &type_check_result,
                            std::ostream &err) {
    auto ir_program = ahfl::lower_program_ir(input, resolve_result, type_check_result);
    const auto verification = ahfl::ir::verify_ir_program(ir_program);
    if (verification.has_errors()) {
        print_ir_verification_errors(verification, err);
        return std::nullopt;
    }
    return ir_program;
}

bool verify_opt_ir_or_report(const ahfl::ir::opt::OptProgram &program, std::ostream &err) {
    const auto verification = ahfl::ir::opt::verify_opt_program(program);
    if (!verification.has_errors()) {
        return true;
    }
    print_opt_verification_errors(verification, err);
    return false;
}

bool emit_opt_ir_artifact(ahfl::ir::Program &program,
                          const CommandLineOptions &options,
                          bool json,
                          std::ostream &out,
                          std::ostream &err) {
    const bool optimize = options.optimize_requested;
    if (optimize) {
        run_requested_semantic_optimization_pipeline(program, options, err);
    }
    auto opt_program = ahfl::ir::opt::lower_to_opt(program);
    if (!verify_opt_ir_or_report(opt_program, err)) {
        return false;
    }
    bool modified = false;
    for (auto &function : opt_program.functions) {
        if (optimize) {
            modified = ahfl::ir::opt::optimize(function) || modified;
        }
    }
    static_cast<void>(modified);
    if (optimize && !verify_opt_ir_or_report(opt_program, err)) {
        return false;
    }
    if (json) {
        ahfl::ir::opt::print_opt_program_json(opt_program, out);
    } else {
        ahfl::ir::opt::print_opt_program(opt_program, out);
    }
    return true;
}

bool read_formatter_source(const std::filesystem::path &path,
                           std::string &content,
                           std::ostream &err) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        err << "error: failed to open source file for formatting: " << path.string() << '\n';
        return false;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    content = buffer.str();
    return true;
}

bool write_formatter_source(const std::filesystem::path &path,
                            const std::string &content,
                            std::ostream &err) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        err << "error: failed to write formatted source file: " << path.string() << '\n';
        return false;
    }
    output << content;
    return true;
}

std::optional<std::filesystem::path>
find_nearest_format_config(const std::filesystem::path &input_path) {
    std::error_code ec;
    auto directory =
        input_path.has_parent_path() ? input_path.parent_path() : std::filesystem::current_path(ec);
    if (directory.empty()) {
        directory = std::filesystem::current_path(ec);
    }
    if (directory.is_relative()) {
        directory = std::filesystem::absolute(directory, ec);
    }
    if (ec) {
        return std::nullopt;
    }

    while (!directory.empty()) {
        const auto candidate = directory / ".ahfl-format";
        if (std::filesystem::exists(candidate, ec) && !ec) {
            return candidate;
        }
        ec.clear();

        const auto parent = directory.parent_path();
        if (parent.empty() || parent == directory) {
            break;
        }
        directory = parent;
    }

    return std::nullopt;
}

std::optional<ahfl::formatter::FormatOptions>
load_formatter_options(const std::filesystem::path &input_path, std::ostream &err) {
    auto options = ahfl::formatter::default_options();
    const auto config_path = find_nearest_format_config(input_path);
    if (!config_path.has_value()) {
        return options;
    }

    try {
        auto loaded = ahfl::formatter::load_config(config_path->string());
        if (!loaded.has_value()) {
            err << "error: failed to read formatter config: " << config_path->string() << '\n';
            return std::nullopt;
        }
        options = *loaded;
    } catch (const std::exception &ex) {
        err << "error: invalid formatter config " << config_path->string() << ": " << ex.what()
            << '\n';
        return std::nullopt;
    }

    return options;
}

struct FormatterInputCollection {
    std::vector<std::filesystem::path> files;
    std::set<std::string> identities;
    std::size_t invalid_inputs{0};
    bool batch_source{false};
};

struct FormatterFileResult {
    bool failed{false};
    bool changed{false};
    bool check_failed{false};
};

[[nodiscard]] bool is_ahfl_source_path(const std::filesystem::path &path) {
    return path.extension() == ".ahfl";
}

[[nodiscard]] std::string formatter_path_identity(const std::filesystem::path &path) {
    std::error_code ec;
    auto normalized = std::filesystem::weakly_canonical(path, ec);
    if (!ec) {
        return normalized.lexically_normal().generic_string();
    }

    ec.clear();
    normalized = std::filesystem::absolute(path, ec);
    if (!ec) {
        return normalized.lexically_normal().generic_string();
    }

    return path.lexically_normal().generic_string();
}

void append_unique_formatter_file(FormatterInputCollection &collection,
                                  const std::filesystem::path &path) {
    if (collection.identities.insert(formatter_path_identity(path)).second) {
        collection.files.push_back(path);
    }
}

void collect_formatter_input_path(FormatterInputCollection &collection,
                                  const std::filesystem::path &path,
                                  std::ostream &err) {
    std::error_code ec;
    const auto status = std::filesystem::status(path, ec);
    if (ec || !std::filesystem::exists(status)) {
        err << "error: formatter input does not exist: " << path.string() << '\n';
        ++collection.invalid_inputs;
        return;
    }

    if (std::filesystem::is_regular_file(status)) {
        append_unique_formatter_file(collection, path);
        return;
    }

    if (!std::filesystem::is_directory(status)) {
        err << "error: formatter input is not a file or directory: " << path.string() << '\n';
        ++collection.invalid_inputs;
        return;
    }

    collection.batch_source = true;

    std::vector<std::filesystem::path> directory_files;
    try {
        for (const auto &entry : std::filesystem::recursive_directory_iterator(
                 path, std::filesystem::directory_options::skip_permission_denied)) {
            if (!entry.is_regular_file(ec) || ec) {
                ec.clear();
                continue;
            }
            if (is_ahfl_source_path(entry.path())) {
                directory_files.push_back(entry.path());
            }
        }
    } catch (const std::filesystem::filesystem_error &ex) {
        err << "error: failed to scan formatter directory " << path.string() << ": " << ex.what()
            << '\n';
        ++collection.invalid_inputs;
        return;
    }

    std::sort(directory_files.begin(),
              directory_files.end(),
              [](const std::filesystem::path &lhs, const std::filesystem::path &rhs) {
                  return lhs.generic_string() < rhs.generic_string();
              });
    for (const auto &file : directory_files) {
        append_unique_formatter_file(collection, file);
    }
}

FormatterFileResult format_single_source_file(const std::filesystem::path &input_path,
                                              bool check_only,
                                              std::ostream &out,
                                              std::ostream &err) {
    FormatterFileResult file_result;

    std::string source;
    if (!read_formatter_source(input_path, source, err)) {
        file_result.failed = true;
        return file_result;
    }

    const auto options = load_formatter_options(input_path, err);
    if (!options.has_value()) {
        file_result.failed = true;
        return file_result;
    }

    const auto result = ahfl::formatter::format_source(source, *options);
    if (!result.success) {
        err << "error: failed to format " << input_path.string();
        if (!result.error.empty()) {
            err << ": " << result.error;
        }
        err << '\n';
        file_result.failed = true;
        return file_result;
    }

    if (result.formatted == source) {
        out << "ok: format check passed " << input_path.string() << '\n';
        return file_result;
    }

    if (check_only) {
        err << "error: formatting check failed for " << input_path.string() << '\n';
        file_result.failed = true;
        file_result.check_failed = true;
        return file_result;
    }

    if (!write_formatter_source(input_path, result.formatted, err)) {
        file_result.failed = true;
        return file_result;
    }

    const auto diffs = ahfl::formatter::compute_diff(source, result.formatted);
    out << "formatted " << input_path.string() << " (" << diffs.size() << " line(s) changed)\n";
    file_result.changed = true;
    return file_result;
}

std::string current_action_label(const CommandLineOptions &options,
                                 std::optional<CommandKind> effective_command) {
    return selected_action_name(selected_action_from_options(options, effective_command));
}

double duration_ms(std::chrono::steady_clock::time_point start,
                   std::chrono::steady_clock::time_point end) {
    return static_cast<double>(
               std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()) /
           1000.0;
}

void export_cli_trace(const CommandLineOptions &options,
                      std::optional<CommandKind> effective_command,
                      ExitCode exit_code,
                      double elapsed_ms) {
    if (!options.trace_export_path.has_value()) {
        return;
    }

    auto span = ahfl::telemetry::create_span("ahflc.command");
    span.attributes.emplace_back("command", current_action_label(options, effective_command));
    span.attributes.emplace_back("exit_code", std::to_string(static_cast<int>(exit_code)));
    span.attributes.emplace_back("duration_ms", std::to_string(elapsed_ms));
    ahfl::telemetry::end_span(span);

    ahfl::telemetry::FileTraceExporter exporter(std::string(*options.trace_export_path));
    exporter.export_span(span);
}

void export_cli_metrics(const CommandLineOptions &options,
                        std::optional<CommandKind> effective_command,
                        ExitCode exit_code,
                        double elapsed_ms) {
    if (!options.metrics_export_path.has_value()) {
        return;
    }

    ahfl::telemetry::FileMetricsExporter exporter(std::string(*options.metrics_export_path));
    const auto now = std::chrono::steady_clock::now();
    const std::vector<std::pair<std::string, std::string>> labels{
        {"command", current_action_label(options, effective_command)},
    };
    exporter.export_metric(ahfl::telemetry::MetricPoint{
        .name = "ahfl.cli.duration_ms",
        .value = elapsed_ms,
        .timestamp = now,
        .labels = labels,
    });
    exporter.export_metric(ahfl::telemetry::MetricPoint{
        .name = "ahfl.cli.exit_code",
        .value = static_cast<double>(static_cast<int>(exit_code)),
        .timestamp = now,
        .labels = labels,
    });
}

void export_cli_structured_log(const CommandLineOptions &options,
                               std::optional<CommandKind> effective_command,
                               ExitCode exit_code,
                               double elapsed_ms) {
    if (!options.structured_log_path.has_value()) {
        return;
    }

    ahfl::telemetry::StructuredLogger logger;
    logger.set_file_sink(std::string(*options.structured_log_path));
    logger.log(exit_code == ExitCode::Success ? ahfl::telemetry::LogLevel::Info
                                              : ahfl::telemetry::LogLevel::Error,
               "ahflc command completed",
               {
                   {"command", current_action_label(options, effective_command)},
                   {"exit_code", std::to_string(static_cast<int>(exit_code))},
                   {"duration_ms", std::to_string(elapsed_ms)},
               });
}

using MaybeSourceFile = std::optional<std::reference_wrapper<const ahfl::SourceFile>>;

std::size_t input_source_count(const ahfl::ast::Program &, MaybeSourceFile source_file) {
    return source_file.has_value() ? 1U : 0U;
}

std::size_t input_source_count(const ahfl::SourceGraph &input, MaybeSourceFile) {
    return input.sources.size();
}

std::size_t input_source_bytes(const ahfl::ast::Program &, MaybeSourceFile source_file) {
    return source_file.has_value() ? source_file->get().content.size() : 0U;
}

std::size_t input_source_bytes(const ahfl::SourceGraph &input, MaybeSourceFile) {
    std::size_t bytes = 0;
    for (const auto &source : input.sources) {
        bytes += source.source.content.size();
    }
    return bytes;
}

void record_proxy_allocation(ahfl::profiling::MemoryTracker &tracker,
                             std::string tag,
                             std::size_t count,
                             std::size_t weight) {
    if (count == 0 || weight == 0) {
        return;
    }
    tracker.record_allocation(std::move(tag), count * weight);
}

template <typename InputT>
MemoryReportSnapshot build_memory_report_snapshot(const InputT &input,
                                                  MaybeSourceFile source_file,
                                                  const ahfl::TypeCheckResult &type_check_result,
                                                  const ahfl::ir::Program &ir_program) {
    const auto &typed = type_check_result.typed_program;
    const auto source_count = input_source_count(input, source_file);
    const auto source_bytes = input_source_bytes(input, source_file);

    ahfl::profiling::MemoryTracker tracker;
    tracker.record_allocation("source", source_bytes);
    record_proxy_allocation(tracker, "typed_declarations", typed.declarations.size(), 256);
    record_proxy_allocation(tracker, "typed_expressions", typed.expressions.size(), 256);
    record_proxy_allocation(tracker, "typed_blocks", typed.blocks.size(), 192);
    record_proxy_allocation(tracker, "typed_statements", typed.statements.size(), 192);
    record_proxy_allocation(
        tracker, "typed_temporal_expressions", typed.temporal_exprs.size(), 192);
    record_proxy_allocation(tracker, "typed_symbols", typed.symbols.size(), 160);
    record_proxy_allocation(tracker, "typed_references", typed.references.size(), 160);
    record_proxy_allocation(tracker, "typed_imports", typed.imports.size(), 128);
    record_proxy_allocation(tracker, "ir_declarations", ir_program.declarations.size(), 512);
    record_proxy_allocation(tracker, "ir_expressions", ir_program.expr_arena.size(), 256);

    return MemoryReportSnapshot{
        .source_count = source_count,
        .source_bytes = source_bytes,
        .typed_declarations = typed.declarations.size(),
        .typed_expressions = typed.expressions.size(),
        .typed_blocks = typed.blocks.size(),
        .typed_statements = typed.statements.size(),
        .typed_temporal_expressions = typed.temporal_exprs.size(),
        .typed_symbols = typed.symbols.size(),
        .typed_references = typed.references.size(),
        .typed_imports = typed.imports.size(),
        .ir_declarations = ir_program.declarations.size(),
        .ir_expressions = ir_program.expr_arena.size(),
        .proxy_current_bytes = tracker.current_usage(),
        .proxy_peak_bytes = tracker.peak_usage(),
        .proxy_allocation_count = tracker.allocation_count(),
    };
}

void export_cli_memory_report(const CommandLineOptions &options,
                              std::optional<CommandKind> effective_command,
                              ExitCode exit_code,
                              double elapsed_ms,
                              const std::optional<MemoryReportSnapshot> &snapshot) {
    if (!options.memory_report_path.has_value()) {
        return;
    }

    std::ofstream out(std::string(*options.memory_report_path), std::ios::binary | std::ios::trunc);
    if (!out) {
        return;
    }

    out << "{\"schema\":\"ahfl.memory_report.v0\"";
    out << ",\"command\":";
    ahfl::write_escaped_json_string(out, current_action_label(options, effective_command));
    out << ",\"exit_code\":" << static_cast<int>(exit_code);
    out << ",\"duration_ms\":" << elapsed_ms;
    out << ",\"available\":" << (snapshot.has_value() ? "true" : "false");
    if (snapshot.has_value()) {
        const auto &value = *snapshot;
        out << ",\"source_count\":" << value.source_count;
        out << ",\"source_bytes\":" << value.source_bytes;
        out << ",\"typed_declarations\":" << value.typed_declarations;
        out << ",\"typed_expressions\":" << value.typed_expressions;
        out << ",\"typed_blocks\":" << value.typed_blocks;
        out << ",\"typed_statements\":" << value.typed_statements;
        out << ",\"typed_temporal_expressions\":" << value.typed_temporal_expressions;
        out << ",\"typed_symbols\":" << value.typed_symbols;
        out << ",\"typed_references\":" << value.typed_references;
        out << ",\"typed_imports\":" << value.typed_imports;
        out << ",\"ir_declarations\":" << value.ir_declarations;
        out << ",\"ir_expressions\":" << value.ir_expressions;
        out << ",\"proxy_current_bytes\":" << value.proxy_current_bytes;
        out << ",\"proxy_peak_bytes\":" << value.proxy_peak_bytes;
        out << ",\"proxy_allocation_count\":" << value.proxy_allocation_count;
    }
    out << "}\n";
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

    return run_observed();
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

    const bool descriptor_input =
        options_.project_descriptor.has_value() || options_.workspace_descriptor.has_value();
    if (effective_command_ == CommandKind::Format) {
        if (descriptor_input && !options_.positional.empty()) {
            std::cerr << "error: fmt accepts either positional files/directories or a "
                         "project/workspace descriptor, not both\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
        if (!descriptor_input && options_.positional.empty()) {
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
        if (!options_.search_roots.empty()) {
            std::cerr << "error: --search-root is not supported with fmt; pass directories or "
                         "use --project/--workspace\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
    } else if (descriptor_input ? !options_.positional.empty() : options_.positional.size() != 1) {
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.package_descriptor.has_value() &&
        !selected_action_supports_package(selected_action)) {
        std::cerr << "error: --package is only supported with "
                  << format_comma_or_commands(command_list(CommandListKind::PackageSupported))
                  << "\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    const bool supports_capability_mocks =
        selected_action_supports_capability_inputs(selected_action) ||
        effective_command_ == CommandKind::RunWorkflow;
    if (options_.capability_mocks_descriptor.has_value() && !supports_capability_mocks) {
        std::cerr << "error: --capability-mocks is only supported with "
                  << format_comma_or_commands(
                         command_list(CommandListKind::CapabilityInputSupported))
                  << " or run"
                  << "\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.input_fixture.has_value() &&
        !selected_action_supports_capability_inputs(selected_action)) {
        std::cerr << "error: --input-fixture is only supported with "
                  << format_comma_or_commands(
                         command_list(CommandListKind::CapabilityInputSupported))
                  << "\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.run_id.has_value() &&
        !selected_action_supports_capability_inputs(selected_action)) {
        std::cerr << "error: --run-id is only supported with "
                  << format_comma_or_commands(
                         command_list(CommandListKind::CapabilityInputSupported))
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

    if (options_.tool_catalog_descriptor.has_value() &&
        effective_command_ != CommandKind::RunWorkflow) {
        std::cerr << "error: --tool-catalog is only supported with run\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.capability_bindings_descriptor.has_value() &&
        effective_command_ != CommandKind::RunWorkflow) {
        std::cerr << "error: --capability-bindings is only supported with run\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.model_checker.has_value() && effective_command_ != CommandKind::VerifyFormal) {
        std::cerr << "error: --model-checker is only supported with verify-formal\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.checker_timeout_seconds.has_value() &&
        effective_command_ != CommandKind::VerifyFormal) {
        std::cerr << "error: --checker-timeout-seconds is only supported with verify-formal\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.checker_timeout_seconds.has_value() &&
        !parse_positive_seconds(*options_.checker_timeout_seconds).has_value()) {
        std::cerr << "error: --checker-timeout-seconds expects an integer in the range 1..86400\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.formal_backend.has_value() && effective_command_ != CommandKind::VerifyFormal) {
        std::cerr << "error: --formal-backend is only supported with verify-formal\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.formal_backend.has_value() &&
        !ahfl::formal::parse_model_checker_kind(*options_.formal_backend).has_value()) {
        std::cerr << "error: unsupported formal backend '" << *options_.formal_backend
                  << "'; expected nuxmv, nusmv, spin, or tlaplus\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.formal_model_out.has_value() && effective_command_ != CommandKind::VerifyFormal) {
        std::cerr << "error: --formal-model-out is only supported with verify-formal\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.time_passes_requested && !options_.optimize_requested) {
        std::cerr << "error: --time-passes requires -O or --optimize\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.time_passes_requested &&
        (effective_command_ == CommandKind::Format || effective_command_ == CommandKind::DumpAst ||
         effective_command_ == CommandKind::DumpProject)) {
        std::cerr << "error: --time-passes is only supported with commands that run "
                     "optimization passes\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.smv_size_report_requested && effective_command_ != CommandKind::EmitSmv) {
        std::cerr << "error: --smv-size-report is only supported with emit smv\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.format_check_requested && effective_command_ != CommandKind::Format) {
        std::cerr << "error: --check is only supported with fmt\n";
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

ExitCode CliDriver::run_observed() {
    const auto started = std::chrono::steady_clock::now();
    ExitCode status = ExitCode::Success;
    if (auto load_status = load_package_and_mocks(); load_status.has_value()) {
        status = *load_status;
    } else {
        status = execute();
    }

    const auto elapsed = duration_ms(started, std::chrono::steady_clock::now());
    export_cli_trace(options_, effective_command_, status, elapsed);
    export_cli_metrics(options_, effective_command_, status, elapsed);
    export_cli_structured_log(options_, effective_command_, status, elapsed);
    export_cli_memory_report(options_, effective_command_, status, elapsed, memory_report_);
    return status;
}

ExitCode CliDriver::execute() {
    if (effective_command_ == CommandKind::Format) {
        return format_source_file();
    }

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

ExitCode CliDriver::format_source_file() {
    FormatterInputCollection collection;
    const bool descriptor_input =
        options_.project_descriptor.has_value() || options_.workspace_descriptor.has_value();

    if (descriptor_input) {
        ahfl::ProjectInput input;
        if (const auto load_status =
                load_project_input(options_, frontend_, input, *diag_consumer_);
            load_status >= 0) {
            return load_status == 0 ? ExitCode::Success : ExitCode::CompileError;
        }

        collection.batch_source = true;
        for (const auto &entry_file : input.entry_files) {
            collect_formatter_input_path(collection, entry_file, std::cerr);
        }
    } else {
        collection.batch_source = options_.positional.size() > 1;
        for (const auto positional : options_.positional) {
            collect_formatter_input_path(
                collection, std::filesystem::path{std::string(positional)}, std::cerr);
        }
    }

    if (collection.files.empty()) {
        if (collection.invalid_inputs == 0) {
            std::cerr << "error: formatter found no .ahfl files\n";
        }
        return ExitCode::CompileError;
    }

    std::size_t changed_count = 0;
    std::size_t failed_count = collection.invalid_inputs;
    std::size_t check_failed_count = 0;
    for (const auto &file : collection.files) {
        const auto result =
            format_single_source_file(file, options_.format_check_requested, std::cout, std::cerr);
        if (result.changed) {
            ++changed_count;
        }
        if (result.failed) {
            ++failed_count;
        }
        if (result.check_failed) {
            ++check_failed_count;
        }
    }

    const auto input_count = collection.files.size() + collection.invalid_inputs;
    const bool batch_mode =
        collection.batch_source || collection.files.size() > 1 || collection.invalid_inputs > 0;
    if (failed_count > 0) {
        if (batch_mode) {
            if (check_failed_count > 0) {
                std::cerr << "error: format check failed for " << check_failed_count << " of "
                          << collection.files.size() << " file(s)\n";
            }
            std::cerr << "error: format failed for " << failed_count << " of " << input_count
                      << " input(s)\n";
        }
        return ExitCode::CompileError;
    }

    if (batch_mode) {
        if (options_.format_check_requested) {
            std::cout << "ok: format check passed " << collection.files.size() << " file(s)\n";
        } else {
            std::cout << "ok: formatted " << collection.files.size() << " file(s), "
                      << changed_count << " changed\n";
        }
    }

    return ExitCode::Success;
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

    auto verified_ir =
        lower_verified_ir_or_report(input, resolve_result, type_check_result, std::cerr);
    if (!verified_ir.has_value()) {
        return ExitCode::CompileError;
    }
    auto ir_program = std::move(*verified_ir);
    memory_report_ =
        build_memory_report_snapshot(input, source_file, type_check_result, ir_program);

    if (effective_command_ == CommandKind::RunWorkflow) {
        if (options_.optimize_requested) {
            run_requested_semantic_optimization_pipeline(ir_program, options_, std::cerr);
        }
        const auto status = run_workflow_with_llm(ir_program, options_, std::cout, std::cerr);
        return status == 0 ? ExitCode::Success : ExitCode::CompileError;
    }

    if (package_metadata_ptr != nullptr) {
        if (options_.optimize_requested) {
            run_requested_semantic_optimization_pipeline(ir_program, options_, std::cerr);
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
                                                          options_,
                                                          std::cout,
                                                          std::cerr);
            backend_status.has_value()) {
            return *backend_status == 0 ? ExitCode::Success : ExitCode::CompileError;
        }
    }

    if (effective_command_ == CommandKind::ValidateAssurance) {
        if (options_.optimize_requested) {
            run_requested_semantic_optimization_pipeline(ir_program, options_, std::cerr);
        }
        return validate_assurance_program(ir_program) == 0 ? ExitCode::Success
                                                           : ExitCode::CompileError;
    }

    if (effective_command_ == CommandKind::VerifyFormal) {
        if (options_.optimize_requested) {
            run_requested_semantic_optimization_pipeline(ir_program, options_, std::cerr);
        }
        return verify_formal_program(ir_program, options_) == 0 ? ExitCode::Success
                                                                : ExitCode::CompileError;
    }

    if (effective_command_ == CommandKind::EmitOptIr ||
        effective_command_ == CommandKind::EmitOptIrJson) {
        return emit_opt_ir_artifact(ir_program,
                                    options_,
                                    effective_command_ == CommandKind::EmitOptIrJson,
                                    std::cout,
                                    std::cerr)
                   ? ExitCode::Success
                   : ExitCode::CompileError;
    }
    if (options_.optimize_requested) {
        run_requested_semantic_optimization_pipeline(ir_program, options_, std::cerr);
    }
    if (const auto backend_status = emit_core_backend(effective_command_,
                                                      ir_program,
                                                      resolve_result,
                                                      type_check_result,
                                                      package_metadata_ptr,
                                                      options_,
                                                      std::cout,
                                                      std::cerr);
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
