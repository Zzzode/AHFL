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
#include "compiler/manifest/manifest.hpp"
#include "compiler/package_graph/lockfile.hpp"
#include "compiler/package_graph/package_graph.hpp"
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

[[nodiscard]] std::optional<std::size_t> parse_bmc_depth(std::string_view value) {
    if (value.empty()) {
        return std::nullopt;
    }
    constexpr std::size_t kMaxBmcDepth = 1'000'000;
    std::size_t depth = 0;
    for (const char character : value) {
        if (character < '0' || character > '9') {
            return std::nullopt;
        }
        const std::size_t digit = static_cast<std::size_t>(character - '0');
        if (depth > (kMaxBmcDepth - digit) / 10) {
            return std::nullopt;
        }
        depth = depth * 10 + digit;
    }
    if (depth == 0) {
        return std::nullopt;
    }
    return depth;
}

[[nodiscard]] std::optional<bool> parse_bool_flag(std::string_view value) {
    if (value == "true" || value == "1" || value == "yes" || value == "on") {
        return true;
    }
    if (value == "false" || value == "0" || value == "no" || value == "off") {
        return false;
    }
    return std::nullopt;
}

[[nodiscard]] bool has_module_declaration(const ahfl::ast::Program &program) noexcept {
    return std::any_of(
        program.declarations.begin(), program.declarations.end(), [](const auto &decl) {
            return decl && decl->kind == ahfl::ast::NodeKind::ModuleDecl;
        });
}

[[nodiscard]] std::filesystem::path normalize_manifest_path(const std::filesystem::path &path) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    const auto candidate = error ? path.lexically_normal() : absolute.lexically_normal();
    const auto canonical = std::filesystem::weakly_canonical(candidate, error);
    return (error ? candidate : canonical).lexically_normal();
}

[[nodiscard]] std::optional<std::filesystem::path>
find_sysroot_manifest_from_directory(const std::filesystem::path &start) {
    std::error_code error;
    const auto candidate = normalize_manifest_path(start / "std" / "ahfl.toml");
    if (std::filesystem::exists(candidate, error) && !error) {
        return candidate;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::filesystem::path>
sysroot_manifest_from_options(const CommandLineOptions &options) {
    if (options.sysroot_path.has_value()) {
        const auto raw = std::filesystem::path{std::string{*options.sysroot_path}};
        const auto normalized = normalize_manifest_path(raw);
        if (normalized.filename() == "ahfl.toml") {
            return normalized;
        }
        return normalize_manifest_path(normalized / "std" / "ahfl.toml");
    }

    if (const char *env_root = std::getenv("AHFL_SYSROOT");
        env_root != nullptr && *env_root != '\0') {
        if (auto manifest = find_sysroot_manifest_from_directory(env_root); manifest.has_value()) {
            return manifest;
        }
    }

    std::error_code error;
    auto current = std::filesystem::current_path(error);
    if (error) {
        return std::nullopt;
    }
    current = normalize_manifest_path(current);
    while (!current.empty()) {
        if (auto manifest = find_sysroot_manifest_from_directory(current); manifest.has_value()) {
            return manifest;
        }
        const auto parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }

    return std::nullopt;
}

void print_package_graph_diagnostics(
    const std::vector<ahfl::package_graph::Diagnostic> &diagnostics, std::ostream &err) {
    for (const auto &diagnostic : diagnostics) {
        err << "error";
        if (!diagnostic.code.empty()) {
            err << " [" << diagnostic.code << "]";
        }
        err << ": " << diagnostic.message << '\n';
    }
}

[[nodiscard]] bool
read_plain_file(const std::filesystem::path &path, std::string &content, std::ostream &err) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        err << "error: failed to open " << path.generic_string() << '\n';
        return false;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    content = buffer.str();
    return true;
}

[[nodiscard]] std::optional<ExitCode>
check_lockfile_if_present(const ahfl::package_graph::PackageGraph &graph,
                          const std::filesystem::path &descriptor_directory,
                          std::ostream &err) {
    const auto lockfile_path = normalize_manifest_path(descriptor_directory / "ahfl.lock");
    std::error_code error;
    if (!std::filesystem::exists(lockfile_path, error)) {
        if (error) {
            err << "error: failed to inspect lockfile " << lockfile_path.generic_string() << '\n';
            return ExitCode::CompileError;
        }
        return std::nullopt;
    }
    if (!std::filesystem::is_regular_file(lockfile_path, error) || error) {
        err << "error: lockfile must be a regular file: " << lockfile_path.generic_string() << '\n';
        return ExitCode::CompileError;
    }

    std::string content;
    if (!read_plain_file(lockfile_path, content, err)) {
        return ExitCode::CompileError;
    }

    auto parsed = ahfl::package_graph::parse_lockfile_json(content);
    if (parsed.has_errors() || !parsed.lockfile.has_value()) {
        print_package_graph_diagnostics(parsed.diagnostics, err);
        return ExitCode::CompileError;
    }

    const auto drift =
        ahfl::package_graph::check_lockfile_drift(graph, *parsed.lockfile, descriptor_directory);
    if (!drift.empty()) {
        print_package_graph_diagnostics(drift, err);
        return ExitCode::CompileError;
    }
    return std::nullopt;
}

[[nodiscard]] bool module_has_prefix(std::string_view module_name, std::string_view prefix) {
    return module_name == prefix ||
           (module_name.size() > prefix.size() && module_name.starts_with(prefix) &&
            module_name.substr(prefix.size(), 2) == "::");
}

[[nodiscard]] std::filesystem::path module_relative_path(std::string_view module_name) {
    std::filesystem::path relative;
    std::size_t start = 0;
    while (start < module_name.size()) {
        const auto separator = module_name.find("::", start);
        if (separator == std::string_view::npos) {
            relative /= std::string(module_name.substr(start));
            break;
        }
        relative /= std::string(module_name.substr(start, separator - start));
        start = separator + 2;
    }
    relative += ".ahfl";
    return relative;
}

[[nodiscard]] std::filesystem::path module_relative_path_after_prefix(std::string_view module_name,
                                                                      std::string_view prefix) {
    if (module_name == prefix) {
        return std::filesystem::path{"mod.ahfl"};
    }
    return module_relative_path(module_name.substr(prefix.size() + 2));
}

[[nodiscard]] std::optional<std::string> module_name_from_entry(std::string_view entry) {
    const auto separator = entry.rfind("::");
    if (separator == std::string_view::npos || separator == 0) {
        return std::nullopt;
    }
    return std::string(entry.substr(0, separator));
}

[[nodiscard]] std::optional<std::filesystem::path>
resolve_module_file_from_graph(const ahfl::package_graph::PackageGraph &graph,
                               std::string_view module_name) {
    std::vector<std::filesystem::path> candidates;
    for (const auto &root : graph.module_roots) {
        if (!module_has_prefix(module_name, root.prefix)) {
            continue;
        }

        const auto relative = module_relative_path_after_prefix(module_name, root.prefix);
        std::error_code error;
        const auto single_file = normalize_manifest_path(root.root / relative);
        if (std::filesystem::exists(single_file, error) && !error) {
            candidates.push_back(single_file);
            continue;
        }

        const auto directory_module = normalize_manifest_path(root.root / relative.parent_path() /
                                                              relative.stem() / "mod.ahfl");
        if (std::filesystem::exists(directory_module, error) && !error) {
            candidates.push_back(directory_module);
        }
    }

    if (candidates.size() != 1) {
        return std::nullopt;
    }
    return candidates.front();
}

[[nodiscard]] std::vector<std::string>
dependency_prefixes_for_package(const ahfl::package_graph::PackageGraph &graph,
                                ahfl::package_graph::PackageId package_id) {
    std::vector<std::string> prefixes;
    for (const auto &dependency : graph.dependencies) {
        if (dependency.from != package_id) {
            continue;
        }

        const auto *target = graph.find_package(dependency.to);
        if (target != nullptr) {
            prefixes.push_back(target->module_prefix);
        }
    }

    std::sort(prefixes.begin(), prefixes.end());
    prefixes.erase(std::unique(prefixes.begin(), prefixes.end()), prefixes.end());
    return prefixes;
}

[[nodiscard]] const ahfl::package_graph::PackageNode *
root_package(const ahfl::package_graph::PackageGraph &graph) {
    return graph.find_package(ahfl::package_graph::PackageId{1});
}

[[nodiscard]] bool path_has_extension(std::string_view value, std::string_view extension) {
    return std::filesystem::path{std::string{value}}.extension().generic_string() == extension;
}

[[nodiscard]] bool is_toml_workspace_manifest_path(const CommandLineOptions &options) {
    return options.workspace_manifest_path.has_value() &&
           path_has_extension(*options.workspace_manifest_path, ".toml");
}

[[nodiscard]] bool command_supports_package_graph_input(std::optional<CommandKind> command) {
    return command == CommandKind::Check || command == CommandKind::Format ||
           (command.has_value() && is_package_supported_command(*command));
}

[[nodiscard]] bool
selected_action_supports_package_graph_input(const CommandLineOptions &options,
                                             std::optional<CommandKind> command) {
    return command_supports_package_graph_input(command) ||
           options.selected_provider_artifact.has_value();
}

[[nodiscard]] bool uses_package_graph_workspace(const CommandLineOptions &options,
                                                std::optional<CommandKind> command) {
    return options.workspace_manifest_path.has_value() &&
           (command == CommandKind::DumpPackageGraph || command == CommandKind::DumpLockfile ||
            (selected_action_supports_package_graph_input(options, command) &&
             is_toml_workspace_manifest_path(options)));
}

[[nodiscard]] bool is_package_graph_descriptor_dump(std::optional<CommandKind> command) {
    return command == CommandKind::DumpPackageGraph || command == CommandKind::DumpLockfile;
}

[[nodiscard]] bool command_can_discover_package_graph(const CommandLineOptions &options,
                                                      std::optional<CommandKind> command) {
    return command == CommandKind::Check && !options.manifest_path.has_value() &&
           !options.workspace_manifest_path.has_value() && options.search_roots.empty() &&
           options.positional.size() == 1;
}

[[nodiscard]] bool workspace_package_is_selected(const CommandLineOptions &options,
                                                 std::optional<CommandKind> command) {
    return options.package_name.has_value() && uses_package_graph_workspace(options, command);
}

[[nodiscard]] std::optional<std::string_view>
workspace_package_name(const CommandLineOptions &options, std::optional<CommandKind> command) {
    if (uses_package_graph_workspace(options, command)) {
        if (options.package_name.has_value()) {
            return *options.package_name;
        }
        return std::nullopt;
    }
    return std::nullopt;
}

enum class DiscoveredPackageGraphKind {
    Manifest,
    Workspace,
};

struct PackageGraphInvocation {
    ahfl::package_graph::PackageGraph graph;
    std::filesystem::path lockfile_directory;
    DiscoveredPackageGraphKind kind{DiscoveredPackageGraphKind::Manifest};
};

struct PackageGraphDiscoveryResult {
    std::optional<PackageGraphInvocation> invocation;
    std::optional<ExitCode> exit_code;
};

[[nodiscard]] std::optional<std::filesystem::path>
find_nearest_named_file(const std::filesystem::path &start, std::string_view filename) {
    std::error_code error;
    auto current = normalize_manifest_path(start);
    if (std::filesystem::is_regular_file(current, error) && !error) {
        current = current.parent_path();
    }
    error.clear();

    while (!current.empty()) {
        const auto candidate = normalize_manifest_path(current / std::string{filename});
        if (std::filesystem::is_regular_file(candidate, error) && !error) {
            return candidate;
        }
        error.clear();

        const auto parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }
    return std::nullopt;
}

void print_manifest_diagnostics(const std::vector<ahfl::manifest::ManifestDiagnostic> &diagnostics,
                                std::ostream &err) {
    for (const auto &diagnostic : diagnostics) {
        err << "error";
        if (!diagnostic.code.empty()) {
            err << " [" << diagnostic.code << "]";
        }
        err << ": " << diagnostic.message << '\n';
    }
}

[[nodiscard]] std::optional<ahfl::manifest::WorkspaceManifest>
load_workspace_manifest_for_discovery(const std::filesystem::path &workspace_manifest_path,
                                      std::ostream &err) {
    std::string content;
    if (!read_plain_file(workspace_manifest_path, content, err)) {
        return std::nullopt;
    }

    auto result = ahfl::manifest::parse_workspace_manifest(content);
    if (result.has_errors() || !result.manifest.has_value()) {
        print_manifest_diagnostics(result.diagnostics, err);
        return std::nullopt;
    }
    return std::move(*result.manifest);
}

[[nodiscard]] std::optional<ahfl::manifest::PackageManifest>
load_package_manifest_for_discovery(const std::filesystem::path &package_manifest_path,
                                    std::ostream &err) {
    std::string content;
    if (!read_plain_file(package_manifest_path, content, err)) {
        return std::nullopt;
    }

    auto result = ahfl::manifest::parse_package_manifest(content);
    if (result.has_errors() || !result.manifest.has_value()) {
        print_manifest_diagnostics(result.diagnostics, err);
        return std::nullopt;
    }
    return std::move(*result.manifest);
}

[[nodiscard]] bool
workspace_contains_package_manifest(const ahfl::manifest::WorkspaceManifest &workspace,
                                    const std::filesystem::path &workspace_manifest_path,
                                    const std::filesystem::path &package_manifest_path) {
    const auto workspace_root = normalize_manifest_path(workspace_manifest_path.parent_path());
    const auto package_manifest = normalize_manifest_path(package_manifest_path);
    for (const auto &member : workspace.members) {
        const auto member_manifest = normalize_manifest_path(workspace_root / member / "ahfl.toml");
        if (member_manifest == package_manifest) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] PackageGraphDiscoveryResult discover_package_graph_for_input(
    const CommandLineOptions &options, std::optional<CommandKind> command, std::ostream &err) {
    PackageGraphDiscoveryResult discovery;
    if (!command_can_discover_package_graph(options, command)) {
        return discovery;
    }

    const auto input_path =
        normalize_manifest_path(std::filesystem::path{std::string{options.positional.front()}});
    const auto manifest_path = find_nearest_named_file(input_path, "ahfl.toml");
    if (!manifest_path.has_value()) {
        return discovery;
    }

    const auto sysroot_manifest = sysroot_manifest_from_options(options);
    if (!sysroot_manifest.has_value()) {
        err << "error: failed to locate sysroot std/ahfl.toml; pass --sysroot <path>\n";
        discovery.exit_code = ExitCode::UsageError;
        return discovery;
    }

    auto package_manifest = load_package_manifest_for_discovery(*manifest_path, err);
    if (!package_manifest.has_value()) {
        discovery.exit_code = ExitCode::CompileError;
        return discovery;
    }

    const auto workspace_path =
        find_nearest_named_file(manifest_path->parent_path(), "ahfl.workspace.toml");
    if (workspace_path.has_value()) {
        auto workspace = load_workspace_manifest_for_discovery(*workspace_path, err);
        if (!workspace.has_value()) {
            discovery.exit_code = ExitCode::CompileError;
            return discovery;
        }
        if (workspace_contains_package_manifest(*workspace, *workspace_path, *manifest_path)) {
            auto workspace_result = ahfl::package_graph::build_package_graph_from_workspace(
                ahfl::package_graph::WorkspaceBuildInput{
                    .workspace_manifest_path = *workspace_path,
                    .package_name = package_manifest->package_name,
                    .sysroot_manifest_path = *sysroot_manifest,
                });
            if (workspace_result.has_errors() || !workspace_result.graph.has_value()) {
                print_package_graph_diagnostics(workspace_result.diagnostics, err);
                discovery.exit_code = ExitCode::CompileError;
                return discovery;
            }
            discovery.invocation = PackageGraphInvocation{
                .graph = std::move(*workspace_result.graph),
                .lockfile_directory = workspace_path->parent_path(),
                .kind = DiscoveredPackageGraphKind::Workspace,
            };
            return discovery;
        }
    }

    auto manifest_result = ahfl::package_graph::build_package_graph_from_manifests(
        ahfl::package_graph::ManifestBuildInput{
            .root_manifest_path = *manifest_path,
            .sysroot_manifest_path = *sysroot_manifest,
        });
    if (manifest_result.has_errors() || !manifest_result.graph.has_value()) {
        print_package_graph_diagnostics(manifest_result.diagnostics, err);
        discovery.exit_code = ExitCode::CompileError;
        return discovery;
    }

    discovery.invocation = PackageGraphInvocation{
        .graph = std::move(*manifest_result.graph),
        .lockfile_directory = manifest_path->parent_path(),
        .kind = DiscoveredPackageGraphKind::Manifest,
    };
    return discovery;
}

[[nodiscard]] const ahfl::package_graph::TargetNode *
select_target(const ahfl::package_graph::PackageNode &package,
              const CommandLineOptions &options,
              std::ostream &err) {
    if (options.target_name.has_value()) {
        const auto name = std::string_view{*options.target_name};
        const auto found = std::find_if(package.targets.begin(),
                                        package.targets.end(),
                                        [&](const auto &target) { return target.name == name; });
        if (found == package.targets.end()) {
            err << "error: package '" << package.name << "' does not contain target '" << name
                << "'\n";
            return nullptr;
        }
        return &*found;
    }

    if (package.targets.size() == 1) {
        return &package.targets.front();
    }

    err << "error: package '" << package.name << "' contains " << package.targets.size()
        << " targets; pass --target <name>\n";
    return nullptr;
}

[[nodiscard]] ahfl::ProjectInput
project_input_from_package_graph(const ahfl::package_graph::PackageGraph &graph,
                                 std::filesystem::path entry_file) {
    ahfl::ProjectInput input;
    input.entry_files.push_back(std::move(entry_file));
    input.include_stdlib = false;
    input.inject_prelude = false;
    input.enforce_package_dependencies = true;
    input.module_roots.reserve(graph.module_roots.size());
    for (const auto &root : graph.module_roots) {
        const auto *package = graph.find_package(root.package);
        input.module_roots.push_back(ahfl::ProjectInput::ModuleRoot{
            .prefix = root.prefix,
            .root = root.root,
            .exported_modules =
                package != nullptr ? package->exported_modules : std::vector<std::string>{},
            .dependency_prefixes = dependency_prefixes_for_package(graph, root.package),
        });
    }
    return input;
}

[[nodiscard]] bool
package_graph_action_requires_handoff_metadata(const CommandLineOptions &options,
                                               std::optional<CommandKind> command) {
    return selected_action_supports_package(selected_action_from_options(options, command));
}

[[nodiscard]] ahfl::handoff::PackageMetadata
package_metadata_from_package_graph_target(const ahfl::package_graph::PackageNode &package,
                                           const ahfl::package_graph::TargetNode &target) {
    auto entry_kind = ahfl::handoff::ExecutableKind::Workflow;
    if (const auto export_entry =
            std::find_if(target.exports.begin(), target.exports.end(), [&](const auto &item) {
                return item.name == target.entry;
            });
        export_entry != target.exports.end() && export_entry->kind == "agent") {
        entry_kind = ahfl::handoff::ExecutableKind::Agent;
    }

    ahfl::handoff::PackageMetadata metadata;
    metadata.identity = ahfl::handoff::PackageIdentity{
        .format_version = std::string{ahfl::handoff::kFormatVersion},
        .name = package.name,
        .version = package.version,
    };
    metadata.entry_target = ahfl::handoff::ExecutableRef{
        .kind = entry_kind,
        .canonical_name = target.entry,
    };
    metadata.export_targets.reserve(target.exports.size());
    for (const auto &export_target : target.exports) {
        metadata.export_targets.push_back(ahfl::handoff::ExecutableRef{
            .kind = export_target.kind == "agent" ? ahfl::handoff::ExecutableKind::Agent
                                                  : ahfl::handoff::ExecutableKind::Workflow,
            .canonical_name = export_target.name,
        });
    }
    for (const auto &binding : target.capability_bindings) {
        metadata.capability_binding_keys.emplace(binding.capability, binding.binding_key);
    }
    return metadata;
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
    const auto verification =
        ahfl::ir::verify_ir_program(ir_program, ahfl::ir::IrVerificationMode::BackendReady);
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
    const bool package_graph_workspace = uses_package_graph_workspace(options_, effective_command_);
    const bool package_graph_descriptor_dump = is_package_graph_descriptor_dump(effective_command_);
    const bool package_graph_discovery =
        command_can_discover_package_graph(options_, effective_command_);
    const bool workspace_package_selector =
        workspace_package_is_selected(options_, effective_command_);
    if (action_count > 1) {
        std::cerr << "error: choose at most one of "
                  << format_comma_or_commands(command_list(CommandListKind::Action)) << "\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.workspace_manifest_path.has_value() &&
        !is_toml_workspace_manifest_path(options_)) {
        std::cerr << "error: --workspace expects ahfl.workspace.toml\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.workspace_manifest_path.has_value() && !package_graph_workspace) {
        std::cerr << "error: --workspace is only supported with check, fmt, emit native-json, "
                     "package artifact commands, provider artifact commands, and dump "
                     "package-graph/lockfile\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.workspace_manifest_path.has_value() && !options_.search_roots.empty()) {
        std::cerr << "error: --workspace cannot be combined with --search-root\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.workspace_manifest_path.has_value() &&
        !workspace_package_name(options_, effective_command_).has_value()) {
        std::cerr << "error: --workspace requires --package <name>\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.manifest_path.has_value()) {
        if (!path_has_extension(*options_.manifest_path, ".toml")) {
            std::cerr << "error: --manifest expects ahfl.toml\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
        if (!selected_action_supports_package_graph_input(options_, effective_command_) &&
            !package_graph_descriptor_dump) {
            std::cerr << "error: --manifest is currently only supported with check, fmt, emit "
                         "native-json, package artifact commands, provider artifact commands, "
                         "and dump package-graph/lockfile\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
        if (options_.workspace_manifest_path.has_value() || !options_.search_roots.empty()) {
            std::cerr << "error: --manifest cannot be combined with --workspace or --search-root\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
        if (options_.package_name.has_value()) {
            std::cerr << "error: --manifest cannot be combined with --package; use --target "
                         "<name>\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
    }

    if (options_.target_name.has_value() && !options_.manifest_path.has_value() &&
        !package_graph_workspace && !package_graph_discovery) {
        std::cerr << "error: --target requires --manifest, ahfl.workspace.toml, or discovered "
                     "ahfl.toml\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.sysroot_path.has_value() && !options_.manifest_path.has_value() &&
        !package_graph_workspace && !package_graph_discovery) {
        std::cerr << "error: --sysroot requires --manifest, ahfl.workspace.toml, or discovered "
                     "ahfl.toml\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (package_graph_descriptor_dump) {
        if (!options_.manifest_path.has_value() && !options_.workspace_manifest_path.has_value()) {
            std::cerr << "error: dump " << command_short_name(*effective_command_)
                      << " requires --manifest or --workspace\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
        if (!options_.positional.empty()) {
            std::cerr << "error: dump " << command_short_name(*effective_command_)
                      << " does not accept positional input files\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
    }

    const bool package_graph_input =
        (options_.manifest_path.has_value() && !package_graph_descriptor_dump) ||
        package_graph_workspace;
    const bool manifest_input =
        options_.manifest_path.has_value() && !package_graph_descriptor_dump;
    if (effective_command_ == CommandKind::Format) {
        if (package_graph_input && !options_.positional.empty()) {
            std::cerr << "error: fmt accepts either positional files/directories or a "
                         "package manifest/workspace, not both\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
        if (!package_graph_input && options_.positional.empty()) {
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
        if (!options_.search_roots.empty()) {
            std::cerr << "error: --search-root is not supported with fmt; pass directories or "
                         "use --manifest or --workspace --package\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
    } else if (!package_graph_descriptor_dump &&
               (manifest_input     ? !options_.positional.empty()
                : package_graph_input ? !options_.positional.empty()
                                      : options_.positional.size() != 1)) {
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.package_name.has_value() && !workspace_package_selector) {
        std::cerr << "error: --package is only supported with --workspace <ahfl.workspace.toml>\n";
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

    if (options_.bmc_depth.has_value() && effective_command_ != CommandKind::VerifyFormal &&
        effective_command_ != CommandKind::EmitSmv) {
        std::cerr << "error: --bmc-depth is only supported with verify-formal or emit smv\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.bmc_depth.has_value() && !parse_bmc_depth(*options_.bmc_depth).has_value()) {
        std::cerr << "error: --bmc-depth expects a positive integer K in the range 1..1000000\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.bmc_boundary_invariants.has_value() &&
        effective_command_ != CommandKind::VerifyFormal &&
        effective_command_ != CommandKind::EmitSmv) {
        std::cerr << "error: --bmc-boundary-invariants is only supported with verify-formal or "
                     "emit smv\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.bmc_boundary_invariants.has_value() &&
        !parse_bool_flag(*options_.bmc_boundary_invariants).has_value()) {
        std::cerr << "error: --bmc-boundary-invariants expects true or false\n";
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
         effective_command_ == CommandKind::DumpProject || package_graph_descriptor_dump)) {
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
    const bool workspace_package_selector =
        workspace_package_is_selected(options_, effective_command_);
    const bool package_metadata_from_package_graph =
        (options_.manifest_path.has_value() || workspace_package_selector) &&
        selected_action_supports_package(selected_action);
    if (selected_action_requires_package(selected_action)) {
        const auto action_name = selected_action_name(selected_action);
        if (!package_metadata_from_package_graph) {
            std::cerr << "error: " << action_name
                      << " requires --manifest <ahfl.toml> --target <name>, --workspace "
                         "<ahfl.workspace.toml> --package <name> --target <name>\n";
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

    if (effective_command_ == CommandKind::DumpPackageGraph) {
        return dump_package_graph();
    }

    if (effective_command_ == CommandKind::DumpLockfile) {
        return dump_lockfile();
    }

    if (options_.manifest_path.has_value()) {
        return run_manifest_package();
    }

    if (uses_package_graph_workspace(options_, effective_command_)) {
        return run_workspace_package();
    }

    if (command_can_discover_package_graph(options_, effective_command_)) {
        auto discovery = discover_package_graph_for_input(options_, effective_command_, std::cerr);
        if (discovery.exit_code.has_value()) {
            return *discovery.exit_code;
        }
        if (discovery.invocation.has_value()) {
            if (auto lock_status =
                    check_lockfile_if_present(discovery.invocation->graph,
                                              discovery.invocation->lockfile_directory,
                                              std::cerr);
                lock_status.has_value()) {
                return *lock_status;
            }
            return run_package_graph_package(discovery.invocation->graph);
        }
        if (options_.target_name.has_value() || options_.sysroot_path.has_value()) {
            std::cerr << "error: failed to discover ahfl.toml from input file; pass --manifest or "
                         "--workspace\n";
            return ExitCode::UsageError;
        }
    }

    const bool project_mode = !options_.search_roots.empty();

    if (project_mode || is_action_enabled(options_, CommandKind::DumpProject)) {
        ahfl::ProjectInput input;
        if (const auto load_status = load_project_input(options_, input);
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

    if (effective_command_ == CommandKind::DumpAst) {
        auto parse_result = frontend_.parse_file(std::string(options_.positional.front()));
        render_diagnostics(*diag_consumer_, parse_result, std::cref(parse_result.source));
        if (parse_result.program) {
            dump_ast_outline(*parse_result.program, std::cout);
        }
        if (parse_result.has_errors() || !parse_result.program) {
            return parse_result.has_errors() ? ExitCode::CompileError : ExitCode::Success;
        }
        return ExitCode::Success;
    }

    auto parse_result = frontend_.parse_file(std::string(options_.positional.front()));
    render_diagnostics(*diag_consumer_, parse_result, std::cref(parse_result.source));
    if (parse_result.has_errors() || !parse_result.program) {
        return parse_result.has_errors() ? ExitCode::CompileError : ExitCode::Success;
    }
    if (!has_module_declaration(*parse_result.program)) {
        return run_analysis(*parse_result.program, std::cref(parse_result.source));
    }

    ahfl::ProjectInput input;
    input.inject_prelude = true;
    input.entry_files.push_back(std::string(options_.positional.front()));
    input.search_roots.reserve(options_.search_roots.size());
    for (const auto search_root : options_.search_roots) {
        input.search_roots.push_back(std::string(search_root));
    }

    auto project_result = frontend_.parse_project(input);
    render_diagnostics(*diag_consumer_, project_result, std::nullopt);
    if (project_result.has_errors()) {
        return ExitCode::CompileError;
    }

    return run_analysis(project_result.graph, std::nullopt);
}

ExitCode CliDriver::run_manifest_package() {
    const auto sysroot_manifest = sysroot_manifest_from_options(options_);
    if (!sysroot_manifest.has_value()) {
        std::cerr << "error: failed to locate sysroot std/ahfl.toml; pass --sysroot <path>\n";
        return ExitCode::UsageError;
    }

    const auto root_manifest_path =
        normalize_manifest_path(std::filesystem::path{std::string{*options_.manifest_path}});
    const auto graph_result = ahfl::package_graph::build_package_graph_from_manifests(
        ahfl::package_graph::ManifestBuildInput{
            .root_manifest_path = root_manifest_path,
            .sysroot_manifest_path = *sysroot_manifest,
        });
    if (graph_result.has_errors() || !graph_result.graph.has_value()) {
        print_package_graph_diagnostics(graph_result.diagnostics, std::cerr);
        return ExitCode::CompileError;
    }

    if (auto lock_status = check_lockfile_if_present(
            *graph_result.graph, root_manifest_path.parent_path(), std::cerr);
        lock_status.has_value()) {
        return *lock_status;
    }

    return run_package_graph_package(*graph_result.graph);
}

ExitCode CliDriver::run_workspace_package() {
    const auto sysroot_manifest = sysroot_manifest_from_options(options_);
    if (!sysroot_manifest.has_value()) {
        std::cerr << "error: failed to locate sysroot std/ahfl.toml; pass --sysroot <path>\n";
        return ExitCode::UsageError;
    }

    const auto package_name = workspace_package_name(options_, effective_command_);
    if (!package_name.has_value()) {
        std::cerr << "error: --workspace requires --package <name>\n";
        return ExitCode::UsageError;
    }

    const auto workspace_manifest_path =
        normalize_manifest_path(std::filesystem::path{std::string{*options_.workspace_manifest_path}});
    const auto graph_result = ahfl::package_graph::build_package_graph_from_workspace(
        ahfl::package_graph::WorkspaceBuildInput{
            .workspace_manifest_path = workspace_manifest_path,
            .package_name = std::string{*package_name},
            .sysroot_manifest_path = *sysroot_manifest,
        });
    if (graph_result.has_errors() || !graph_result.graph.has_value()) {
        print_package_graph_diagnostics(graph_result.diagnostics, std::cerr);
        return ExitCode::CompileError;
    }

    if (auto lock_status = check_lockfile_if_present(
            *graph_result.graph, workspace_manifest_path.parent_path(), std::cerr);
        lock_status.has_value()) {
        return *lock_status;
    }

    return run_package_graph_package(*graph_result.graph);
}

ExitCode CliDriver::run_package_graph_package(const ahfl::package_graph::PackageGraph &graph) {
    const auto *package = root_package(graph);
    if (package == nullptr) {
        std::cerr << "error: PackageGraph is missing root package\n";
        return ExitCode::CompileError;
    }

    const auto *target = select_target(*package, options_, std::cerr);
    if (target == nullptr) {
        return ExitCode::UsageError;
    }
    if (package_graph_action_requires_handoff_metadata(options_, effective_command_)) {
        if (target->kind != "handoff") {
            std::cerr << "error: target '" << target->name << "' has kind '" << target->kind
                      << "'; package artifact commands require a handoff target\n";
            return ExitCode::UsageError;
        }
        package_metadata_ = package_metadata_from_package_graph_target(*package, *target);
    }

    const auto entry_module = module_name_from_entry(target->entry);
    if (!entry_module.has_value()) {
        std::cerr << "error: target '" << target->name << "' entry '" << target->entry
                  << "' must be a canonical symbol name\n";
        return ExitCode::CompileError;
    }

    const auto entry_file = resolve_module_file_from_graph(graph, *entry_module);
    if (!entry_file.has_value()) {
        std::cerr << "error: failed to resolve target '" << target->name << "' entry module '"
                  << *entry_module << "' from PackageGraph module roots\n";
        return ExitCode::CompileError;
    }

    auto input = project_input_from_package_graph(graph, *entry_file);
    auto project_result = frontend_.parse_project(input);
    render_diagnostics(*diag_consumer_, project_result, std::nullopt);
    if (project_result.has_errors()) {
        return ExitCode::CompileError;
    }

    return run_analysis(project_result.graph, std::nullopt);
}

ExitCode CliDriver::dump_package_graph() {
    const auto sysroot_manifest = sysroot_manifest_from_options(options_);
    if (!sysroot_manifest.has_value()) {
        std::cerr << "error: failed to locate sysroot std/ahfl.toml; pass --sysroot <path>\n";
        return ExitCode::UsageError;
    }

    if (options_.workspace_manifest_path.has_value()) {
        const auto package_name = workspace_package_name(options_, effective_command_);
        if (!package_name.has_value()) {
            std::cerr << "error: dump package-graph --workspace requires --package <name>\n";
            return ExitCode::UsageError;
        }
        const auto result = ahfl::package_graph::build_package_graph_from_workspace(
            ahfl::package_graph::WorkspaceBuildInput{
                .workspace_manifest_path = normalize_manifest_path(
                    std::filesystem::path{std::string{*options_.workspace_manifest_path}}),
                .package_name = std::string{*package_name},
                .sysroot_manifest_path = *sysroot_manifest,
            });
        if (result.has_errors() || !result.graph.has_value()) {
            print_package_graph_diagnostics(result.diagnostics, std::cerr);
            return ExitCode::CompileError;
        }

        std::cout << ahfl::package_graph::serialize_package_graph_json(*result.graph) << '\n';
        return ExitCode::Success;
    }

    const auto result = ahfl::package_graph::build_package_graph_from_manifests(
        ahfl::package_graph::ManifestBuildInput{
            .root_manifest_path = normalize_manifest_path(
                std::filesystem::path{std::string{*options_.manifest_path}}),
            .sysroot_manifest_path = *sysroot_manifest,
        });
    if (result.has_errors() || !result.graph.has_value()) {
        print_package_graph_diagnostics(result.diagnostics, std::cerr);
        return ExitCode::CompileError;
    }

    std::cout << ahfl::package_graph::serialize_package_graph_json(*result.graph) << '\n';
    return ExitCode::Success;
}

ExitCode CliDriver::dump_lockfile() {
    const auto sysroot_manifest = sysroot_manifest_from_options(options_);
    if (!sysroot_manifest.has_value()) {
        std::cerr << "error: failed to locate sysroot std/ahfl.toml; pass --sysroot <path>\n";
        return ExitCode::UsageError;
    }

    if (options_.workspace_manifest_path.has_value()) {
        const auto package_name = workspace_package_name(options_, effective_command_);
        if (!package_name.has_value()) {
            std::cerr << "error: dump lockfile --workspace requires --package <name>\n";
            return ExitCode::UsageError;
        }
        const auto result = ahfl::package_graph::build_package_graph_from_workspace(
            ahfl::package_graph::WorkspaceBuildInput{
                .workspace_manifest_path = normalize_manifest_path(
                    std::filesystem::path{std::string{*options_.workspace_manifest_path}}),
                .package_name = std::string{*package_name},
                .sysroot_manifest_path = *sysroot_manifest,
            });
        if (result.has_errors() || !result.graph.has_value()) {
            print_package_graph_diagnostics(result.diagnostics, std::cerr);
            return ExitCode::CompileError;
        }

        std::cout << ahfl::package_graph::serialize_lockfile(ahfl::package_graph::make_lockfile(
                         *result.graph,
                         normalize_manifest_path(
                             std::filesystem::path{std::string{*options_.workspace_manifest_path}})
                             .parent_path()))
                  << '\n';
        return ExitCode::Success;
    }

    const auto result = ahfl::package_graph::build_package_graph_from_manifests(
        ahfl::package_graph::ManifestBuildInput{
            .root_manifest_path = normalize_manifest_path(
                std::filesystem::path{std::string{*options_.manifest_path}}),
            .sysroot_manifest_path = *sysroot_manifest,
        });
    if (result.has_errors() || !result.graph.has_value()) {
        print_package_graph_diagnostics(result.diagnostics, std::cerr);
        return ExitCode::CompileError;
    }

    std::cout << ahfl::package_graph::serialize_lockfile(ahfl::package_graph::make_lockfile(
                     *result.graph,
                     normalize_manifest_path(
                         std::filesystem::path{std::string{*options_.manifest_path}})
                         .parent_path()))
              << '\n';
    return ExitCode::Success;
}

ExitCode CliDriver::format_source_file() {
    FormatterInputCollection collection;
    const bool package_graph_workspace = uses_package_graph_workspace(options_, effective_command_);

    if (options_.manifest_path.has_value() || package_graph_workspace) {
        const auto sysroot_manifest = sysroot_manifest_from_options(options_);
        if (!sysroot_manifest.has_value()) {
            std::cerr << "error: failed to locate sysroot std/ahfl.toml; pass --sysroot <path>\n";
            return ExitCode::UsageError;
        }

        ahfl::package_graph::BuildResult graph_result;
        std::filesystem::path lockfile_directory;
        if (options_.manifest_path.has_value()) {
            const auto root_manifest_path = normalize_manifest_path(
                std::filesystem::path{std::string{*options_.manifest_path}});
            graph_result = ahfl::package_graph::build_package_graph_from_manifests(
                ahfl::package_graph::ManifestBuildInput{
                    .root_manifest_path = root_manifest_path,
                    .sysroot_manifest_path = *sysroot_manifest,
                });
            lockfile_directory = root_manifest_path.parent_path();
        } else {
            const auto package_name = workspace_package_name(options_, effective_command_);
            if (!package_name.has_value()) {
                std::cerr << "error: --workspace requires --package <name>\n";
                return ExitCode::UsageError;
            }
            const auto workspace_manifest_path = normalize_manifest_path(
                std::filesystem::path{std::string{*options_.workspace_manifest_path}});
            graph_result = ahfl::package_graph::build_package_graph_from_workspace(
                ahfl::package_graph::WorkspaceBuildInput{
                    .workspace_manifest_path = workspace_manifest_path,
                    .package_name = std::string{*package_name},
                    .sysroot_manifest_path = *sysroot_manifest,
                });
            lockfile_directory = workspace_manifest_path.parent_path();
        }

        if (graph_result.has_errors() || !graph_result.graph.has_value()) {
            print_package_graph_diagnostics(graph_result.diagnostics, std::cerr);
            return ExitCode::CompileError;
        }
        if (auto lock_status =
                check_lockfile_if_present(*graph_result.graph, lockfile_directory, std::cerr);
            lock_status.has_value()) {
            return *lock_status;
        }

        const auto *package = root_package(*graph_result.graph);
        if (package == nullptr) {
            std::cerr << "error: PackageGraph is missing root package\n";
            return ExitCode::CompileError;
        }
        collection.batch_source = true;
        collect_formatter_input_path(collection, package->module_root, std::cerr);
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
