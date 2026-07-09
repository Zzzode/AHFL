#include "tooling/cli/cli_driver.hpp"

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/ir/verify.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/validate.hpp"
#include "base/json/json_value.hpp"
#include "base/support/json.hpp"
#include "base/support/sha256.hpp"
#include "compiler/ir/opt/opt_json.hpp"
#include "compiler/ir/opt/opt_lower.hpp"
#include "compiler/ir/opt/opt_passes.hpp"
#include "compiler/ir/opt/opt_print.hpp"
#include "compiler/ir/opt/opt_verify.hpp"
#include "compiler/manifest/manifest.hpp"
#include "compiler/package_graph/lockfile.hpp"
#include "compiler/package_graph/package_graph.hpp"
#include "compiler/passes/pass_manager.hpp"
#include "compiler/project_discovery/discovery.hpp"
#include "compiler/syntax/frontend/project.hpp"
#include "pipeline/execution/dry_run/runner.hpp"
#include "tooling/cli/cli_analysis_helpers.hpp"
#include "tooling/cli/option_table.hpp"
#include "tooling/cli/pipeline_runner.hpp"
#include "tooling/cli/provider/pipeline_durable_store_import_provider.hpp"
#include "tooling/cli/workflow_run.hpp"
#include "tooling/formatter/format_config.hpp"
#include "tooling/formatter/formatter.hpp"
#include "tooling/package/registry.hpp"
#include "tooling/package/registry_package_input.hpp"
#include "tooling/package/source_archive.hpp"
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
#include <random>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
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

struct PackageVersionCoordinate {
    std::string package_name;
    std::string version;
};

[[nodiscard]] bool is_cli_semver(std::string_view value) {
    std::size_t dots = 0;
    bool digit_in_part = false;
    for (const char item : value) {
        if (item >= '0' && item <= '9') {
            digit_in_part = true;
            continue;
        }
        if (item == '.') {
            if (!digit_in_part) {
                return false;
            }
            ++dots;
            digit_in_part = false;
            continue;
        }
        return false;
    }
    return dots == 2 && digit_in_part;
}

[[nodiscard]] bool is_cli_package_name(std::string_view value) {
    if (value.empty() || value.front() < 'a' || value.front() > 'z') {
        return false;
    }
    bool previous_dash = false;
    for (const char item : value) {
        const bool lower = item >= 'a' && item <= 'z';
        const bool digit = item >= '0' && item <= '9';
        if (lower || digit) {
            previous_dash = false;
            continue;
        }
        if (item == '-' && !previous_dash) {
            previous_dash = true;
            continue;
        }
        return false;
    }
    return value.back() != '-';
}

[[nodiscard]] std::optional<PackageVersionCoordinate>
parse_package_version_coordinate(std::string_view value) {
    const auto at = value.rfind('@');
    if (at == std::string_view::npos || at == 0 || at + 1 >= value.size()) {
        return std::nullopt;
    }
    auto version = value.substr(at + 1);
    if (!is_cli_semver(version)) {
        return std::nullopt;
    }
    auto package_name = value.substr(0, at);
    if (!is_cli_package_name(package_name)) {
        return std::nullopt;
    }
    return PackageVersionCoordinate{
        .package_name = std::string{package_name},
        .version = std::string{version},
    };
}

[[nodiscard]] std::string qualified_name_text(const ahfl::ast::QualifiedName &name) {
    return name.spelling();
}

[[nodiscard]] ahfl::DiagnosticBag
detached_source_unit_diagnostics(const ahfl::ast::Program &program,
                                 const ahfl::SourceFile &source) {
    ahfl::DiagnosticBag diagnostics;
    diagnostics
        .note(ahfl::SourceRange{.begin_offset = 0,
                                .end_offset = std::min<std::size_t>(source.content.size(), 1)})
        .code("N::detached_source_unit")
        .message("this file is not part of an AHFL package")
        .with_note("create ahfl.toml or pass --manifest to enable std imports and workspace "
                   "navigation")
        .emit();

    for (const auto &declaration : program.declarations) {
        if (!declaration || declaration->kind != ahfl::ast::NodeKind::ImportDecl) {
            continue;
        }
        const auto &import_decl = static_cast<const ahfl::ast::ImportDecl &>(*declaration);
        diagnostics.error(import_decl.range)
            .code("E::detached_import")
            .message("import declarations require an AHFL package manifest")
            .with_note("import: " +
                       (import_decl.path ? qualified_name_text(*import_decl.path) : std::string{}))
            .with_note("add ahfl.toml, or run ahflc with --manifest")
            .emit();
    }

    return diagnostics;
}

[[nodiscard]] bool is_ascii_alpha(char value) {
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
}

[[nodiscard]] bool is_ascii_digit(char value) {
    return value >= '0' && value <= '9';
}

[[nodiscard]] char to_ascii_lower(char value) {
    if (value >= 'A' && value <= 'Z') {
        return static_cast<char>(value - 'A' + 'a');
    }
    return value;
}

[[nodiscard]] std::string sanitize_kebab_name(std::string_view value) {
    std::string result;
    bool previous_separator = false;
    for (const char raw : value) {
        const char lowered = to_ascii_lower(raw);
        if ((lowered >= 'a' && lowered <= 'z') || is_ascii_digit(lowered)) {
            result.push_back(lowered);
            previous_separator = false;
            continue;
        }
        if (!result.empty() && !previous_separator) {
            result.push_back('-');
            previous_separator = true;
        }
    }
    while (!result.empty() && result.back() == '-') {
        result.pop_back();
    }
    if (result.empty()) {
        result = "scratch";
    }
    if (!is_ascii_alpha(result.front())) {
        result.insert(0, "ahfl-");
    }
    return result;
}

[[nodiscard]] std::string sanitize_identifier_name(std::string_view value) {
    std::string result;
    bool previous_separator = false;
    for (const char raw : value) {
        const char lowered = to_ascii_lower(raw);
        if ((lowered >= 'a' && lowered <= 'z') || is_ascii_digit(lowered) || lowered == '_') {
            result.push_back(lowered);
            previous_separator = false;
            continue;
        }
        if (!result.empty() && !previous_separator) {
            result.push_back('_');
            previous_separator = true;
        }
    }
    while (!result.empty() && result.back() == '_') {
        result.pop_back();
    }
    if (result.empty()) {
        result = "main";
    }
    if (!is_ascii_alpha(result.front()) && result.front() != '_') {
        result.insert(result.begin(), '_');
    }
    return result;
}

[[nodiscard]] std::string toml_basic_string(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (const char character : value) {
        if (character == '\\' || character == '"') {
            escaped.push_back('\\');
            escaped.push_back(character);
        } else if (character == '\n') {
            escaped += "\\n";
        } else if (character == '\r') {
            escaped += "\\r";
        } else if (character == '\t') {
            escaped += "\\t";
        } else {
            escaped.push_back(character);
        }
    }
    escaped.push_back('"');
    return escaped;
}

[[nodiscard]] std::optional<std::string>
module_name_from_program(const ahfl::ast::Program &program) {
    for (const auto &declaration : program.declarations) {
        if (declaration && declaration->kind == ahfl::ast::NodeKind::ModuleDecl) {
            const auto &module = static_cast<const ahfl::ast::ModuleDecl &>(*declaration);
            if (module.name) {
                return module.name->spelling();
            }
        }
    }
    return std::nullopt;
}

struct SingleFilePackageShape {
    std::string package_name;
    std::string module_prefix;
    std::string exported_module;
    std::string module_declaration;
};

[[nodiscard]] std::optional<SingleFilePackageShape>
single_file_shape_from_module(std::optional<std::string> declared_module,
                              const std::filesystem::path &source_path,
                              std::ostream &err) {
    const auto source_stem = sanitize_identifier_name(source_path.stem().generic_string());
    const auto parent_name = source_path.parent_path().filename().generic_string();
    const auto package_name = sanitize_kebab_name(parent_name.empty() ? source_stem : parent_name);

    if (declared_module.has_value()) {
        const auto separator = declared_module->find("::");
        if (separator == std::string::npos || separator == 0 ||
            separator + 2 >= declared_module->size()) {
            err << "error: existing source module '" << *declared_module
                << "' is not a package module; expected <prefix>::<module>\n";
            return std::nullopt;
        }
        return SingleFilePackageShape{
            .package_name = package_name,
            .module_prefix = declared_module->substr(0, separator),
            .exported_module = declared_module->substr(separator + 2),
            .module_declaration = *declared_module,
        };
    }

    std::string module_prefix = sanitize_identifier_name(package_name);
    std::replace(module_prefix.begin(), module_prefix.end(), '-', '_');
    return SingleFilePackageShape{
        .package_name = package_name,
        .module_prefix = module_prefix,
        .exported_module = source_stem,
        .module_declaration = module_prefix + "::" + source_stem,
    };
}

[[nodiscard]] std::string single_file_manifest_text(const SingleFilePackageShape &shape,
                                                    std::string_view source_filename) {
    std::ostringstream manifest;
    manifest << "manifest_version = 1\n\n"
             << "[package]\n"
             << "name = " << toml_basic_string(shape.package_name) << "\n"
             << "version = \"0.1.0\"\n"
             << "edition = \"2026\"\n"
             << "kind = \"library\"\n\n"
             << "[module]\n"
             << "prefix = " << toml_basic_string(shape.module_prefix) << "\n"
             << "root = \".\"\n\n"
             << "[exports]\n"
             << "modules = [" << toml_basic_string(shape.exported_module) << "]\n\n"
             << "[targets.lib]\n"
             << "kind = \"library\"\n"
             << "entry = " << toml_basic_string(source_filename) << "\n\n"
             << "[dependencies]\n"
             << "std = { source = \"sysroot\" }\n";
    return manifest.str();
}

[[nodiscard]] bool
write_text_file(const std::filesystem::path &path, std::string_view content, std::ostream &err) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        err << "error: failed to write " << path.generic_string() << '\n';
        return false;
    }
    output << content;
    if (!output) {
        err << "error: failed to finish writing " << path.generic_string() << '\n';
        return false;
    }
    return true;
}

[[nodiscard]] std::filesystem::path normalize_manifest_path(const std::filesystem::path &path) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    const auto candidate = error ? path.lexically_normal() : absolute.lexically_normal();
    const auto canonical = std::filesystem::weakly_canonical(candidate, error);
    return (error ? candidate : canonical).lexically_normal();
}

struct SysrootManifestSelection {
    std::optional<std::filesystem::path> manifest;
    bool had_error{false};
};

void print_toolchain_diagnostics(const std::vector<ahfl::package_graph::Diagnostic> &diagnostics,
                                 std::ostream &err) {
    for (const auto &diagnostic : diagnostics) {
        err << "error";
        if (!diagnostic.code.empty()) {
            err << " [" << diagnostic.code << "]";
        }
        err << ": " << diagnostic.message << '\n';
    }
}

[[nodiscard]] SysrootManifestSelection
sysroot_manifest_from_options(const CommandLineOptions &options, std::ostream &err) {
    if (options.sysroot_path.has_value()) {
        auto result = ahfl::project_discovery::toolchain_profile_from_sysroot_input(
            std::filesystem::path{std::string{*options.sysroot_path}},
            ahfl::project_discovery::ToolchainProfileOrigin::CliFlag);
        if (result.has_errors()) {
            print_toolchain_diagnostics(result.diagnostics, err);
            return SysrootManifestSelection{.had_error = true};
        }
        if (result.profile.has_value()) {
            return SysrootManifestSelection{.manifest = result.profile->std_manifest};
        }
        return SysrootManifestSelection{};
    }

    if (const char *env_root = std::getenv("AHFL_SYSROOT");
        env_root != nullptr && *env_root != '\0') {
        auto result = ahfl::project_discovery::toolchain_profile_from_sysroot_input(
            std::filesystem::path{env_root},
            ahfl::project_discovery::ToolchainProfileOrigin::Environment);
        if (result.has_errors()) {
            print_toolchain_diagnostics(result.diagnostics, err);
            return SysrootManifestSelection{.had_error = true};
        }
        if (result.profile.has_value()) {
            return SysrootManifestSelection{.manifest = result.profile->std_manifest};
        }
    }

    if (auto profile = ahfl::project_discovery::default_toolchain_profile_from_compile_default();
        profile.has_value()) {
        return SysrootManifestSelection{.manifest = profile->std_manifest};
    }
    return SysrootManifestSelection{};
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

[[nodiscard]] std::optional<std::filesystem::path>
entry_file_from_package_graph_target(const ahfl::package_graph::PackageGraph &graph,
                                     const ahfl::package_graph::PackageNode &package,
                                     const ahfl::package_graph::TargetNode &target,
                                     std::ostream &err) {
    if (target.kind == "library" || target.kind == "test") {
        return normalize_manifest_path(package.package_root / target.entry);
    }

    if (target.kind == "handoff") {
        const auto entry_module = module_name_from_entry(target.entry);
        if (!entry_module.has_value()) {
            err << "error: target '" << target.name << "' entry '" << target.entry
                << "' must be a canonical symbol name\n";
            return std::nullopt;
        }

        const auto entry_file = resolve_module_file_from_graph(graph, *entry_module);
        if (!entry_file.has_value()) {
            err << "error: failed to resolve target '" << target.name << "' entry module '"
                << *entry_module << "' from PackageGraph module roots\n";
            return std::nullopt;
        }
        return entry_file;
    }

    err << "error: target '" << target.name << "' has unsupported kind '" << target.kind << "'\n";
    return std::nullopt;
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

[[nodiscard]] std::vector<std::string>
artifact_exports_for_package(const ahfl::package_graph::PackageGraph &graph,
                             ahfl::package_graph::PackageId package_id) {
    std::vector<std::string> exports;
    const auto *package = graph.find_package(package_id);
    if (package == nullptr) {
        return exports;
    }
    for (const auto &target : package->targets) {
        for (const auto &export_item : target.exports) {
            exports.push_back(export_item.name);
        }
    }

    std::sort(exports.begin(), exports.end());
    exports.erase(std::unique(exports.begin(), exports.end()), exports.end());
    return exports;
}

[[nodiscard]] const ahfl::package_graph::PackageNode *
root_package(const ahfl::package_graph::PackageGraph &graph) {
    return graph.find_package(ahfl::package_graph::PackageId{1});
}

[[nodiscard]] const ahfl::package_graph::PackageNode *
sysroot_package(const ahfl::package_graph::PackageGraph &graph) {
    const auto *package = graph.find_package(ahfl::package_graph::PackageId{0});
    if (package == nullptr || package->source != ahfl::package_graph::PackageSourceKind::Sysroot) {
        return nullptr;
    }
    return package;
}

[[nodiscard]] bool path_has_filename(std::string_view value, std::string_view filename) {
    return std::filesystem::path{std::string{value}}.filename().generic_string() == filename;
}

[[nodiscard]] bool is_workspace_manifest_path(const CommandLineOptions &options) {
    return options.workspace_manifest_path.has_value() &&
           path_has_filename(*options.workspace_manifest_path, "ahfl.workspace.toml");
}

[[nodiscard]] bool command_supports_package_graph_input(std::optional<CommandKind> command) {
    return command == CommandKind::Check || command == CommandKind::Format ||
           (command.has_value() && is_package_supported_command(*command));
}

[[nodiscard]] bool is_public_api_artifact_command(std::optional<CommandKind> command) {
    return command == CommandKind::EmitPublicApi || command == CommandKind::EmitPublicApiDocs;
}

[[nodiscard]] bool is_public_api_diff_command(std::optional<CommandKind> command) {
    return command == CommandKind::EmitPublicApiDiff;
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
             is_workspace_manifest_path(options)));
}

[[nodiscard]] bool is_package_graph_descriptor_dump(std::optional<CommandKind> command) {
    return command == CommandKind::DumpPackageGraph || command == CommandKind::DumpLockfile;
}

[[nodiscard]] bool command_can_discover_package_graph(const CommandLineOptions &options,
                                                      std::optional<CommandKind> command) {
    return command != CommandKind::Format && !is_public_api_diff_command(command) &&
           selected_action_supports_package_graph_input(options, command) &&
           !options.manifest_path.has_value() && !options.workspace_manifest_path.has_value() &&
           options.positional.size() == 1;
}

[[nodiscard]] bool command_can_default_to_cwd_manifest(const CommandLineOptions &options,
                                                       std::optional<CommandKind> command) {
    return command != CommandKind::Format && !is_public_api_diff_command(command) &&
           (selected_action_supports_package_graph_input(options, command) ||
            is_package_graph_descriptor_dump(command)) &&
           !options.manifest_path.has_value() && !options.workspace_manifest_path.has_value() &&
           options.positional.empty();
}

void apply_default_cwd_manifest(CommandLineOptions &options,
                                std::optional<CommandKind> command,
                                std::string &default_manifest_path) {
    if (!command_can_default_to_cwd_manifest(options, command)) {
        return;
    }

    std::error_code error;
    const auto current_directory = std::filesystem::current_path(error);
    if (error) {
        return;
    }
    const auto manifest_path = normalize_manifest_path(current_directory / "ahfl.toml");
    const auto manifest_exists = std::filesystem::is_regular_file(manifest_path, error);
    if (!error && manifest_exists) {
        default_manifest_path = manifest_path.generic_string();
        options.manifest_path = default_manifest_path;
    }
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

[[nodiscard]] bool declares_std_identity(const ahfl::manifest::PackageManifest &manifest) {
    return manifest.package_name == "std" || manifest.package_kind == "standard-library" ||
           manifest.module_prefix == "std";
}

[[nodiscard]] bool
reject_mismatched_std_manifest(const std::filesystem::path &package_manifest_path,
                               const std::filesystem::path &sysroot_manifest_path,
                               const ahfl::manifest::PackageManifest &package_manifest,
                               std::ostream &err) {
    if (normalize_manifest_path(package_manifest_path) ==
        normalize_manifest_path(sysroot_manifest_path)) {
        return false;
    }
    if (!declares_std_identity(package_manifest)) {
        return false;
    }
    const auto normalized_package_manifest = normalize_manifest_path(package_manifest_path);
    const auto normalized_sysroot_manifest = normalize_manifest_path(sysroot_manifest_path);
    err << "error [E::toolchain_sysroot_mismatch]: this standard-library package is not the "
           "active AHFL sysroot\n"
        << "  active sysroot: " << normalized_sysroot_manifest.generic_string() << '\n'
        << "  opened package: " << normalized_package_manifest.generic_string() << '\n'
        << "  help: configure --sysroot to "
        << normalized_package_manifest.parent_path().parent_path().generic_string()
        << " when developing corelib\n";
    return true;
}

[[nodiscard]] bool
reject_mismatched_std_manifest(const std::filesystem::path &package_manifest_path,
                               const std::filesystem::path &sysroot_manifest_path,
                               std::ostream &err) {
    auto manifest = load_package_manifest_for_discovery(package_manifest_path, err);
    if (!manifest.has_value()) {
        return false;
    }
    return reject_mismatched_std_manifest(
        package_manifest_path, sysroot_manifest_path, *manifest, err);
}

[[nodiscard]] bool has_registry_dependencies(const ahfl::manifest::PackageManifest &manifest) {
    return std::any_of(manifest.dependencies.begin(),
                       manifest.dependencies.end(),
                       [](const auto &dependency) { return dependency.source == "registry"; });
}

[[nodiscard]] std::filesystem::path make_registry_materialization_root() {
    std::random_device random;
    const auto suffix =
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
        std::to_string(random()) + "-" + std::to_string(random());
    return std::filesystem::temp_directory_path() / ("ahfl-registry-materialize-" + suffix);
}

[[nodiscard]] std::vector<ahfl::package_graph::PackageInput>
resolve_registry_packages_for_manifest(const std::filesystem::path &root_manifest_path,
                                       std::vector<ahfl::package_graph::Diagnostic> &diagnostics) {
    std::vector<ahfl::package_graph::PackageInput> packages;
    auto manifest = load_package_manifest_for_discovery(root_manifest_path, std::cerr);
    if (!manifest.has_value() || !has_registry_dependencies(*manifest)) {
        return packages;
    }

    const ahfl::package::Registry registry;
    auto result = ahfl::package::resolve_registry_package_inputs(
        ahfl::package::RegistryPackageInputResolution{
            .root_manifest = &*manifest,
            .registry = &registry,
            .materialization_root = make_registry_materialization_root(),
        });
    if (result.has_errors()) {
        for (const auto &diagnostic : result.diagnostics) {
            diagnostics.push_back(ahfl::package_graph::Diagnostic{
                .code = "package.registry_resolution",
                .message = diagnostic,
            });
        }
        return {};
    }
    return std::move(result.packages);
}

[[nodiscard]] ahfl::package_graph::BuildResult
build_manifest_or_sysroot_package_graph(const std::filesystem::path &root_manifest_path,
                                        const std::filesystem::path &sysroot_manifest_path) {
    if (normalize_manifest_path(root_manifest_path) ==
        normalize_manifest_path(sysroot_manifest_path)) {
        return ahfl::package_graph::build_package_graph_from_sysroot(
            ahfl::package_graph::SysrootBuildInput{
                .sysroot_manifest_path = sysroot_manifest_path,
            });
    }
    std::vector<ahfl::package_graph::Diagnostic> registry_diagnostics;
    auto registry_packages =
        resolve_registry_packages_for_manifest(root_manifest_path, registry_diagnostics);
    if (!registry_diagnostics.empty()) {
        return ahfl::package_graph::BuildResult{.diagnostics = std::move(registry_diagnostics)};
    }
    return ahfl::package_graph::build_package_graph_from_manifests(
        ahfl::package_graph::ManifestBuildInput{
            .root_manifest_path = root_manifest_path,
            .sysroot_manifest_path = sysroot_manifest_path,
            .registry_packages = std::move(registry_packages),
        });
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

    const auto sysroot_manifest = sysroot_manifest_from_options(options, err);
    if (!sysroot_manifest.manifest.has_value()) {
        if (!sysroot_manifest.had_error) {
            err << "error: failed to locate sysroot std/ahfl.toml; pass --sysroot <path>\n";
        }
        discovery.exit_code = ExitCode::UsageError;
        return discovery;
    }

    auto package_manifest = load_package_manifest_for_discovery(*manifest_path, err);
    if (!package_manifest.has_value()) {
        discovery.exit_code = ExitCode::CompileError;
        return discovery;
    }
    if (reject_mismatched_std_manifest(
            *manifest_path, *sysroot_manifest.manifest, *package_manifest, err)) {
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
                    .sysroot_manifest_path = *sysroot_manifest.manifest,
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

    auto manifest_result =
        build_manifest_or_sysroot_package_graph(*manifest_path, *sysroot_manifest.manifest);
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
                                 std::vector<std::filesystem::path> entry_files) {
    ahfl::ProjectInput input;
    input.entry_files = std::move(entry_files);
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
            .artifact_exports = artifact_exports_for_package(graph, root.package),
            .dependency_prefixes = dependency_prefixes_for_package(graph, root.package),
            .compiler_intrinsics_allow =
                package == nullptr
                    ? std::nullopt
                    : std::optional<std::vector<std::string>>{package->compiler_intrinsics_allow},
        });
    }
    return input;
}

[[nodiscard]] ahfl::ProjectInput
project_input_from_package_graph(const ahfl::package_graph::PackageGraph &graph,
                                 std::filesystem::path entry_file) {
    std::vector<std::filesystem::path> entry_files;
    entry_files.push_back(std::move(entry_file));
    return project_input_from_package_graph(graph, std::move(entry_files));
}

[[nodiscard]] bool source_unit_has_role(const ahfl::package_graph::SourceUnitNode &source_unit,
                                        ahfl::package_graph::SourceUnitRole role) {
    return std::find(source_unit.roles.begin(), source_unit.roles.end(), role) !=
           source_unit.roles.end();
}

[[nodiscard]] std::vector<std::filesystem::path>
public_api_entry_files_from_package_graph(const ahfl::package_graph::PackageGraph &graph,
                                          const ahfl::package_graph::PackageNode &package) {
    std::vector<std::filesystem::path> entry_files;
    for (const auto &source_unit : graph.source_units) {
        if (source_unit.package != package.id ||
            !source_unit_has_role(source_unit, ahfl::package_graph::SourceUnitRole::Export)) {
            continue;
        }
        entry_files.push_back(source_unit.path);
    }

    std::sort(entry_files.begin(), entry_files.end());
    entry_files.erase(std::unique(entry_files.begin(), entry_files.end()), entry_files.end());
    return entry_files;
}

[[nodiscard]] PublicApiPackageContext
public_api_context_from_package(const ahfl::package_graph::PackageNode &package) {
    return PublicApiPackageContext{
        .name = package.name,
        .version = package.version,
        .module_prefix = package.module_prefix,
        .package_root = package.package_root,
        .manifest_path = package.manifest_path,
    };
}

[[nodiscard]] bool
package_graph_action_requires_handoff_metadata(const CommandLineOptions &options,
                                               std::optional<CommandKind> command) {
    if (is_public_api_artifact_command(command)) {
        return false;
    }
    return selected_action_supports_package(selected_action_from_options(options, command));
}

[[nodiscard]] ahfl::handoff::PackageMetadata
package_metadata_from_package_graph_target(const ahfl::package_graph::PackageNode &package,
                                           const ahfl::package_graph::TargetNode &target) {
    auto entry_kind = ahfl::handoff::ExecutableKind::Workflow;
    if (const auto export_entry =
            std::find_if(target.exports.begin(),
                         target.exports.end(),
                         [&](const auto &item) { return item.name == target.entry; });
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

[[nodiscard]] std::optional<std::vector<std::filesystem::path>>
collect_ahfl_source_files_in_directory(const std::filesystem::path &directory,
                                       std::string_view purpose,
                                       std::ostream &err) {
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    try {
        for (const auto &entry : std::filesystem::recursive_directory_iterator(
                 directory, std::filesystem::directory_options::skip_permission_denied)) {
            if (!entry.is_regular_file(ec) || ec) {
                ec.clear();
                continue;
            }
            if (is_ahfl_source_path(entry.path())) {
                files.push_back(entry.path());
            }
        }
    } catch (const std::filesystem::filesystem_error &ex) {
        err << "error: failed to scan " << purpose << " directory " << directory.string() << ": "
            << ex.what() << '\n';
        return std::nullopt;
    }

    std::sort(files.begin(),
              files.end(),
              [](const std::filesystem::path &lhs, const std::filesystem::path &rhs) {
                  return lhs.generic_string() < rhs.generic_string();
              });
    return files;
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

    auto directory_files = collect_ahfl_source_files_in_directory(path, "formatter", err);
    if (!directory_files.has_value()) {
        ++collection.invalid_inputs;
        return;
    }

    for (const auto &file : *directory_files) {
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
    apply_default_cwd_manifest(options_, effective_command_, default_manifest_path_);

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

    if (is_public_api_diff_command(effective_command_)) {
        if (options_.manifest_path.has_value() || options_.workspace_manifest_path.has_value() ||
            options_.package_name.has_value() || options_.target_name.has_value() ||
            options_.sysroot_path.has_value() || options_.lockfile_path.has_value()) {
            std::cerr << "error: emit public-api-diff accepts exactly two snapshot files and no "
                         "package/workspace selectors\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
        if (options_.positional.size() != 2) {
            std::cerr << "error: emit public-api-diff requires <old-public-api.json> "
                         "<new-public-api.json>\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
        if (options_.public_api_semver_gate_requested) {
            if (!options_.public_api_from_version.has_value() ||
                !options_.public_api_to_version.has_value()) {
                std::cerr << "error: emit public-api-diff --semver-gate requires --from "
                             "<old-version> and --to <new-version>\n";
                print_usage(std::cerr);
                return ExitCode::UsageError;
            }
        } else if (options_.public_api_from_version.has_value() ||
                   options_.public_api_to_version.has_value()) {
            std::cerr << "error: --from and --to are only valid with emit public-api-diff "
                         "--semver-gate\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
        return std::nullopt;
    }

    if ((options_.public_api_semver_gate_requested ||
         options_.public_api_from_version.has_value() ||
         options_.public_api_to_version.has_value()) &&
        effective_command_ != CommandKind::PackagePublish) {
        std::cerr << "error: --semver-gate, --from, and --to are only valid with emit "
                     "public-api-diff or package publish\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (effective_command_ == CommandKind::InitSingleFile) {
        if (options_.manifest_path.has_value() || options_.workspace_manifest_path.has_value() ||
            options_.package_name.has_value() || options_.target_name.has_value() ||
            options_.sysroot_path.has_value() || options_.lockfile_path.has_value()) {
            std::cerr << "error: init --single-file creates an AHFL package and does not accept "
                         "package/workspace selectors\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
        if (options_.positional.size() != 1) {
            std::cerr << "error: init --single-file requires exactly one <input.ahfl>\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
        return std::nullopt;
    }

    if (effective_command_ == CommandKind::PackageArchive) {
        if (!options_.manifest_path.has_value()) {
            std::cerr << "error: package archive requires --manifest <ahfl.toml>\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
        if (!options_.package_archive_output_path.has_value()) {
            std::cerr << "error: package archive requires --out <dir>\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
        if (options_.workspace_manifest_path.has_value() || options_.package_name.has_value() ||
            options_.target_name.has_value() || options_.sysroot_path.has_value() ||
            options_.lockfile_path.has_value()) {
            std::cerr << "error: package archive accepts --manifest and --out only\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
        if (!options_.positional.empty()) {
            std::cerr << "error: package archive does not accept positional input files\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
        return std::nullopt;
    }

    if (effective_command_ == CommandKind::PackagePublish) {
        if (!options_.manifest_path.has_value()) {
            std::cerr << "error: package publish requires --manifest <ahfl.toml>\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
        if (!options_.package_registry_id.has_value()) {
            std::cerr << "error: package publish requires --registry <id>\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
        if (!options_.package_archive_output_path.has_value()) {
            std::cerr << "error: package publish requires --out <dir>\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
        if (options_.workspace_manifest_path.has_value() || options_.package_name.has_value() ||
            options_.target_name.has_value() || options_.package_yank_reason.has_value() ||
            options_.lockfile_path.has_value()) {
            std::cerr << "error: package publish accepts --manifest, --registry, "
                         "--out, --sysroot, --dry-run, and optional --semver-gate --from only\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
        if (options_.public_api_semver_gate_requested) {
            if (!options_.public_api_from_version.has_value()) {
                std::cerr << "error: package publish --semver-gate requires --from "
                             "<previous-version>\n";
                print_usage(std::cerr);
                return ExitCode::UsageError;
            }
            if (options_.public_api_to_version.has_value()) {
                std::cerr << "error: package publish --semver-gate uses the manifest package "
                             "version as --to; do not pass --to\n";
                print_usage(std::cerr);
                return ExitCode::UsageError;
            }
        } else if (options_.public_api_from_version.has_value() ||
                   options_.public_api_to_version.has_value()) {
            std::cerr << "error: package publish --from is only valid with --semver-gate, and "
                         "--to is never accepted\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
        if (!options_.positional.empty()) {
            std::cerr << "error: package publish does not accept positional input files\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
        return std::nullopt;
    }

    if (effective_command_ == CommandKind::PackageYank) {
        if (options_.manifest_path.has_value() || options_.workspace_manifest_path.has_value() ||
            options_.package_name.has_value() || options_.target_name.has_value() ||
            options_.sysroot_path.has_value() || options_.package_archive_output_path.has_value() ||
            options_.lockfile_path.has_value() || options_.package_publish_dry_run_requested ||
            options_.public_api_semver_gate_requested ||
            options_.public_api_from_version.has_value() ||
            options_.public_api_to_version.has_value()) {
            std::cerr << "error: package yank accepts <package>@<version>, --registry, and "
                         "optional --reason only\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
        if (!options_.package_registry_id.has_value()) {
            std::cerr << "error: package yank requires --registry <id>\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
        if (options_.positional.size() != 1 ||
            !parse_package_version_coordinate(options_.positional.front()).has_value()) {
            std::cerr << "error: package yank requires exactly one <package>@<version> "
                         "coordinate\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
        return std::nullopt;
    }

    if (effective_command_ == CommandKind::RegistryResolve) {
        if (!options_.manifest_path.has_value()) {
            std::cerr << "error: registry resolve requires --manifest <ahfl.toml>\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
        if (!options_.lockfile_path.has_value()) {
            std::cerr << "error: registry resolve requires --lockfile <ahfl.lock>\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
        if (!path_has_filename(*options_.manifest_path, "ahfl.toml")) {
            std::cerr << "error: --manifest expects ahfl.toml\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
        if (!path_has_filename(*options_.lockfile_path, "ahfl.lock")) {
            std::cerr << "error: registry resolve --lockfile expects ahfl.lock\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
        if (options_.workspace_manifest_path.has_value() || options_.package_name.has_value() ||
            options_.target_name.has_value() || options_.package_archive_output_path.has_value() ||
            options_.package_registry_id.has_value() || options_.package_yank_reason.has_value() ||
            options_.package_publish_dry_run_requested) {
            std::cerr << "error: registry resolve accepts --manifest, --lockfile, and optional "
                         "--sysroot only\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
        if (!options_.positional.empty()) {
            std::cerr << "error: registry resolve does not accept positional input files\n";
            print_usage(std::cerr);
            return ExitCode::UsageError;
        }
        return std::nullopt;
    }

    if (options_.package_archive_output_path.has_value()) {
        std::cerr << "error: --out is only supported with package archive or package publish\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.package_registry_id.has_value()) {
        std::cerr << "error: --registry is only supported with package publish or package yank\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.lockfile_path.has_value()) {
        std::cerr << "error: --lockfile is only supported with registry resolve\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.package_yank_reason.has_value()) {
        std::cerr << "error: --reason is only supported with package yank\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.package_publish_dry_run_requested) {
        std::cerr << "error: --dry-run is only supported with package publish\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.workspace_manifest_path.has_value() && !is_workspace_manifest_path(options_)) {
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

    if (options_.workspace_manifest_path.has_value() &&
        !workspace_package_name(options_, effective_command_).has_value()) {
        std::cerr << "error: --workspace requires --package <name>\n";
        print_usage(std::cerr);
        return ExitCode::UsageError;
    }

    if (options_.manifest_path.has_value()) {
        if (!path_has_filename(*options_.manifest_path, "ahfl.toml")) {
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
        if (options_.workspace_manifest_path.has_value()) {
            std::cerr << "error: --manifest cannot be combined with --workspace\n";
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
    } else if (!package_graph_descriptor_dump &&
               (manifest_input        ? !options_.positional.empty()
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
         package_graph_descriptor_dump)) {
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

    const bool run_can_use_package_entry_workflow =
        effective_command_ == CommandKind::RunWorkflow &&
        (options_.manifest_path.has_value() || package_graph_workspace);
    if (effective_command_ == CommandKind::RunWorkflow && !options_.workflow_name.has_value() &&
        !run_can_use_package_entry_workflow) {
        std::cerr << "error: run requires --workflow or package workflow entry\n";
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
    if (effective_command_ == CommandKind::InitSingleFile) {
        return init_single_file_package();
    }

    if (effective_command_ == CommandKind::PackageArchive) {
        return archive_package();
    }

    if (effective_command_ == CommandKind::PackagePublish) {
        return publish_package();
    }

    if (effective_command_ == CommandKind::PackageYank) {
        return yank_package();
    }

    if (effective_command_ == CommandKind::RegistryResolve) {
        return resolve_registry_dependencies();
    }

    if (is_public_api_diff_command(effective_command_)) {
        std::optional<PublicApiSemVerGate> semver_gate;
        if (options_.public_api_semver_gate_requested) {
            semver_gate = PublicApiSemVerGate{
                .from_version = std::string{*options_.public_api_from_version},
                .to_version = std::string{*options_.public_api_to_version},
            };
        }
        const auto status =
            emit_public_api_diff(std::filesystem::path{std::string{options_.positional[0]}},
                                 std::filesystem::path{std::string{options_.positional[1]}},
                                 std::move(semver_gate),
                                 std::cout,
                                 std::cerr);
        return status == 0 ? ExitCode::Success : ExitCode::CompileError;
    }

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
        if (options_.target_name.has_value() ||
            (options_.sysroot_path.has_value() && effective_command_ != CommandKind::Check)) {
            std::cerr << "error: failed to discover ahfl.toml from input file; pass --manifest or "
                         "--workspace\n";
            return ExitCode::UsageError;
        }
        if (is_public_api_artifact_command(effective_command_)) {
            std::cerr << "error: emit " << command_short_name(*effective_command_)
                      << " requires an AHFL package manifest; pass --manifest or --workspace\n";
            return ExitCode::UsageError;
        }
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
    const auto detached_diagnostics =
        detached_source_unit_diagnostics(*parse_result.program, parse_result.source);
    diag_consumer_->consume(detached_diagnostics, std::cref(parse_result.source));
    if (detached_diagnostics.has_error()) {
        return ExitCode::CompileError;
    }

    return run_analysis(*parse_result.program, std::cref(parse_result.source));
}

ExitCode CliDriver::init_single_file_package() {
    const auto source_path =
        normalize_manifest_path(std::filesystem::path{std::string{options_.positional.front()}});
    if (source_path.extension() != ".ahfl") {
        std::cerr << "error: init --single-file expects a .ahfl source path\n";
        return ExitCode::UsageError;
    }

    const auto package_root = source_path.parent_path().empty()
                                  ? normalize_manifest_path(std::filesystem::path{"."})
                                  : source_path.parent_path();
    const auto manifest_path = package_root / "ahfl.toml";

    std::error_code error;
    const bool manifest_exists = std::filesystem::exists(manifest_path, error);
    if (error) {
        std::cerr << "error: failed to inspect " << manifest_path.generic_string() << '\n';
        return ExitCode::CompileError;
    }
    if (manifest_exists) {
        std::cerr << "error: AHFL package manifest already exists: "
                  << manifest_path.generic_string() << '\n';
        return ExitCode::UsageError;
    }

    const bool source_exists = std::filesystem::exists(source_path, error);
    if (error) {
        std::cerr << "error: failed to inspect " << source_path.generic_string() << '\n';
        return ExitCode::CompileError;
    }
    if (source_exists && (!std::filesystem::is_regular_file(source_path, error) || error)) {
        std::cerr << "error: init --single-file expects a regular .ahfl file path\n";
        return ExitCode::UsageError;
    }

    if (!std::filesystem::exists(package_root, error)) {
        std::filesystem::create_directories(package_root, error);
        if (error) {
            std::cerr << "error: failed to create package directory "
                      << package_root.generic_string() << '\n';
            return ExitCode::CompileError;
        }
    }

    std::string source_content;
    std::optional<std::string> declared_module;
    if (source_exists) {
        if (!read_plain_file(source_path, source_content, std::cerr)) {
            return ExitCode::CompileError;
        }
        auto parse_result = frontend_.parse_file(source_path);
        render_diagnostics(*diag_consumer_, parse_result, std::cref(parse_result.source));
        if (parse_result.has_errors() || !parse_result.program) {
            return ExitCode::CompileError;
        }
        declared_module = module_name_from_program(*parse_result.program);
    }

    auto shape = single_file_shape_from_module(declared_module, source_path, std::cerr);
    if (!shape.has_value()) {
        return ExitCode::UsageError;
    }

    if (!source_exists) {
        source_content = "module " + shape->module_declaration + ";\n";
        if (!write_text_file(source_path, source_content, std::cerr)) {
            return ExitCode::CompileError;
        }
        std::cout << "created " << source_path.generic_string() << '\n';
    } else if (!declared_module.has_value()) {
        source_content = "module " + shape->module_declaration + ";\n\n" + source_content;
        if (!write_text_file(source_path, source_content, std::cerr)) {
            return ExitCode::CompileError;
        }
        std::cout << "updated " << source_path.generic_string() << '\n';
    }

    const auto manifest =
        single_file_manifest_text(*shape, source_path.filename().generic_string());
    if (!write_text_file(manifest_path, manifest, std::cerr)) {
        return ExitCode::CompileError;
    }
    std::cout << "created " << manifest_path.generic_string() << '\n';
    return ExitCode::Success;
}

ExitCode CliDriver::archive_package() {
    const auto manifest_path =
        normalize_manifest_path(std::filesystem::path{std::string{*options_.manifest_path}});
    const auto package_root = manifest_path.parent_path();

    auto manifest = load_package_manifest_for_discovery(manifest_path, std::cerr);
    if (!manifest.has_value()) {
        return ExitCode::CompileError;
    }

    auto archive = ahfl::package::build_source_archive(package_root, manifest->package_name);
    if (archive.has_errors() || !archive.archive.has_value()) {
        for (const auto &diagnostic : archive.diagnostics) {
            std::cerr << "error: " << diagnostic << '\n';
        }
        return ExitCode::CompileError;
    }

    auto output_directory = normalize_manifest_path(
        std::filesystem::path{std::string{*options_.package_archive_output_path}});
    std::error_code error;
    std::filesystem::create_directories(output_directory, error);
    if (error) {
        std::cerr << "error: failed to create package archive output directory "
                  << output_directory.generic_string() << '\n';
        return ExitCode::CompileError;
    }
    if (!std::filesystem::is_directory(output_directory, error) || error) {
        std::cerr << "error: package archive --out must be a directory: "
                  << output_directory.generic_string() << '\n';
        return ExitCode::UsageError;
    }

    const auto artifact_stem = manifest->package_name + "-" + manifest->package_version;
    const auto manifest_output = output_directory / (artifact_stem + ".source-archive.json");
    const auto payload_output = output_directory / (artifact_stem + ".source-archive.payload");

    const auto manifest_json = ahfl::package::serialize_source_archive_manifest(*archive.archive);
    if (!write_text_file(manifest_output, manifest_json, std::cerr)) {
        return ExitCode::CompileError;
    }

    std::ofstream payload(payload_output, std::ios::binary | std::ios::trunc);
    if (!payload) {
        std::cerr << "error: failed to write " << payload_output.generic_string() << '\n';
        return ExitCode::CompileError;
    }
    payload.write(archive.archive->payload.data(),
                  static_cast<std::streamsize>(archive.archive->payload.size()));
    if (!payload) {
        std::cerr << "error: failed to finish writing " << payload_output.generic_string() << '\n';
        return ExitCode::CompileError;
    }

    std::cout << "wrote " << manifest_output.generic_string() << '\n'
              << "wrote " << payload_output.generic_string() << '\n'
              << "archive-sha256: " << archive.archive->archive_sha256 << '\n'
              << "manifest-sha256: " << archive.archive->manifest_sha256 << '\n';
    return ExitCode::Success;
}

ExitCode CliDriver::publish_package() {
    const auto manifest_path =
        normalize_manifest_path(std::filesystem::path{std::string{*options_.manifest_path}});
    const auto package_root = manifest_path.parent_path();

    auto manifest = load_package_manifest_for_discovery(manifest_path, std::cerr);
    if (!manifest.has_value()) {
        return ExitCode::CompileError;
    }
    if (manifest->package_kind == "standard-library") {
        std::cerr << "error: package publish cannot publish standard-library packages; use the "
                     "toolchain release workflow\n";
        return ExitCode::UsageError;
    }

    const auto sysroot_manifest = sysroot_manifest_from_options(options_, std::cerr);
    if (!sysroot_manifest.manifest.has_value()) {
        if (!sysroot_manifest.had_error) {
            std::cerr << "error: failed to locate sysroot std/ahfl.toml; pass --sysroot <path>\n";
        }
        return ExitCode::UsageError;
    }
    if (reject_mismatched_std_manifest(
            manifest_path, *sysroot_manifest.manifest, *manifest, std::cerr)) {
        return ExitCode::CompileError;
    }

    auto graph_result =
        build_manifest_or_sysroot_package_graph(manifest_path, *sysroot_manifest.manifest);
    if (graph_result.has_errors() || !graph_result.graph.has_value()) {
        print_package_graph_diagnostics(graph_result.diagnostics, std::cerr);
        return ExitCode::CompileError;
    }
    if (auto lock_status =
            check_lockfile_if_present(*graph_result.graph, manifest_path.parent_path(), std::cerr);
        lock_status.has_value()) {
        return *lock_status;
    }

    const auto *package = root_package(*graph_result.graph);
    if (package == nullptr) {
        std::cerr << "error: PackageGraph is missing root package\n";
        return ExitCode::CompileError;
    }
    auto entry_files = public_api_entry_files_from_package_graph(*graph_result.graph, *package);
    if (entry_files.empty()) {
        std::cerr << "error: package publish --dry-run requires at least one exported module in "
                     "ahfl.toml\n";
        return ExitCode::CompileError;
    }

    auto project_input =
        project_input_from_package_graph(*graph_result.graph, std::move(entry_files));
    auto project_result = ahfl::parse_project(frontend_, project_input);
    render_diagnostics(*diag_consumer_, project_result, std::nullopt);
    if (project_result.has_errors()) {
        return ExitCode::CompileError;
    }

    const ahfl::Resolver resolver;
    auto resolve_result = resolver.resolve(project_result.graph);
    render_diagnostics(*diag_consumer_, resolve_result, std::nullopt);
    if (resolve_result.has_errors()) {
        return ExitCode::CompileError;
    }

    const ahfl::TypeChecker type_checker;
    auto type_check_result = type_checker.check(project_result.graph, resolve_result);
    render_diagnostics(*diag_consumer_, type_check_result, std::nullopt);
    if (type_check_result.has_errors()) {
        return ExitCode::CompileError;
    }

    const ahfl::Validator validator;
    auto validation_result =
        validator.validate(project_result.graph, resolve_result, type_check_result);
    render_diagnostics(*diag_consumer_, validation_result, std::nullopt);
    if (validation_result.has_errors()) {
        return ExitCode::CompileError;
    }

    std::ostringstream public_api_snapshot;
    const auto public_api_status =
        emit_public_api_snapshot(project_result.graph,
                                 resolve_result,
                                 type_check_result,
                                 public_api_context_from_package(*package),
                                 public_api_snapshot,
                                 std::cerr);
    if (public_api_status != 0) {
        return ExitCode::CompileError;
    }
    const auto public_api_payload = public_api_snapshot.str();
    const auto public_api_sha256 = "sha256:" + ahfl::support::sha256_hex(public_api_payload);

    auto archive = ahfl::package::build_source_archive(package_root, manifest->package_name);
    if (archive.has_errors() || !archive.archive.has_value()) {
        for (const auto &diagnostic : archive.diagnostics) {
            std::cerr << "error: " << diagnostic << '\n';
        }
        return ExitCode::CompileError;
    }

    ahfl::package::RegistryIndexEntry registry_entry{
        .registry_id = std::string{*options_.package_registry_id},
        .package = manifest->package_name,
        .version = manifest->package_version,
        .yanked = false,
        .source_archive_sha256 = archive.archive->archive_sha256,
        .manifest_sha256 = archive.archive->manifest_sha256,
        .public_api_sha256 = public_api_sha256,
    };
    for (const auto &dependency : manifest->dependencies) {
        if (dependency.source != "sysroot" && dependency.source != "registry") {
            std::cerr << "error: package publish cannot publish local dependency '"
                      << dependency.key << "' with source '" << dependency.source << "'\n";
            return ExitCode::CompileError;
        }
        ahfl::package::RegistryDependencyMetadata metadata{
            .name = dependency.key,
            .source = dependency.source,
        };
        if (dependency.registry.has_value()) {
            metadata.registry = *dependency.registry;
        }
        if (dependency.version.has_value()) {
            metadata.version_requirement = *dependency.version;
        }
        registry_entry.dependencies.push_back(std::move(metadata));
    }

    std::string semver_gate_status = "not-run";
    std::optional<std::string> previous_public_api_payload;
    std::optional<std::string> previous_public_api_sha256;
    std::optional<std::string> previous_public_api_version;
    if (options_.public_api_semver_gate_requested) {
        const auto previous_version = std::string{*options_.public_api_from_version};
        const ahfl::package::Registry registry;
        const auto index = registry.fetch_package_index(manifest->package_name);
        if (!index.success() || !index.index.has_value()) {
            std::cerr << "error: failed to fetch previous release metadata for "
                      << manifest->package_name << "@" << previous_version << "\n";
            for (const auto &diagnostic : index.diagnostics) {
                std::cerr << "error: " << diagnostic << "\n";
            }
            return ExitCode::CompileError;
        }

        std::optional<ahfl::package::RegistryIndexEntry> previous_entry;
        for (const auto &entry : index.index->versions) {
            if (entry.registry_id == registry_entry.registry_id &&
                entry.package == manifest->package_name && entry.version == previous_version) {
                previous_entry = entry;
                break;
            }
        }
        if (!previous_entry.has_value()) {
            std::cerr << "error: previous release " << manifest->package_name << "@"
                      << previous_version << " was not found in registry '"
                      << registry_entry.registry_id << "'\n";
            return ExitCode::CompileError;
        }

        const auto previous_snapshot = registry.fetch_public_api_snapshot(
            previous_entry->package, previous_entry->version, previous_entry->public_api_sha256);
        if (!previous_snapshot.success() || !previous_snapshot.snapshot.has_value()) {
            std::cerr << "error: failed to fetch previous public API snapshot for "
                      << previous_entry->package << "@" << previous_entry->version << "\n";
            for (const auto &diagnostic : previous_snapshot.diagnostics) {
                std::cerr << "error: " << diagnostic << "\n";
            }
            return ExitCode::CompileError;
        }

        const auto previous_label =
            std::string{"registry:"} + previous_entry->package + "@" + previous_entry->version;
        const auto current_label =
            std::string{"current:"} + manifest->package_name + "@" + manifest->package_version;
        const auto semver_status = emit_public_api_diff_from_snapshots(
            previous_snapshot.snapshot->snapshot,
            previous_label,
            public_api_payload,
            current_label,
            PublicApiSemVerGate{.from_version = previous_entry->version,
                                .to_version = manifest->package_version},
            std::cout,
            std::cerr);
        if (semver_status != 0) {
            return ExitCode::CompileError;
        }
        semver_gate_status = "pass";
        previous_public_api_payload = previous_snapshot.snapshot->snapshot;
        previous_public_api_sha256 = previous_entry->public_api_sha256;
        previous_public_api_version = previous_entry->version;
    }

    auto output_directory = normalize_manifest_path(
        std::filesystem::path{std::string{*options_.package_archive_output_path}});
    std::error_code error;
    std::filesystem::create_directories(output_directory, error);
    if (error) {
        std::cerr << "error: failed to create package publish output directory "
                  << output_directory.generic_string() << '\n';
        return ExitCode::CompileError;
    }
    if (!std::filesystem::is_directory(output_directory, error) || error) {
        std::cerr << "error: package publish --out must be a directory: "
                  << output_directory.generic_string() << '\n';
        return ExitCode::UsageError;
    }

    const auto artifact_stem = manifest->package_name + "-" + manifest->package_version;
    const auto archive_manifest_output =
        output_directory / (artifact_stem + ".source-archive.json");
    const auto archive_payload_output =
        output_directory / (artifact_stem + ".source-archive.payload");
    const auto public_api_output = output_directory / (artifact_stem + ".public-api.json");
    const auto registry_index_output = output_directory / (artifact_stem + ".registry-index.json");
    const bool dry_run = options_.package_publish_dry_run_requested;
    const auto publish_evidence_output =
        output_directory / (artifact_stem + (dry_run ? ".publish-dry-run.json" : ".publish.json"));
    const auto previous_public_api_output =
        previous_public_api_version.has_value()
            ? std::optional<std::filesystem::path>{output_directory /
                                                   (manifest->package_name + "-" +
                                                    *previous_public_api_version +
                                                    ".previous-public-api.json")}
            : std::nullopt;

    if (!write_text_file(archive_manifest_output,
                         ahfl::package::serialize_source_archive_manifest(*archive.archive),
                         std::cerr)) {
        return ExitCode::CompileError;
    }
    {
        std::ofstream payload(archive_payload_output, std::ios::binary | std::ios::trunc);
        if (!payload) {
            std::cerr << "error: failed to write " << archive_payload_output.generic_string()
                      << '\n';
            return ExitCode::CompileError;
        }
        payload.write(archive.archive->payload.data(),
                      static_cast<std::streamsize>(archive.archive->payload.size()));
        if (!payload) {
            std::cerr << "error: failed to finish writing "
                      << archive_payload_output.generic_string() << '\n';
            return ExitCode::CompileError;
        }
    }
    if (!write_text_file(public_api_output, public_api_payload, std::cerr)) {
        return ExitCode::CompileError;
    }
    if (previous_public_api_payload.has_value() && previous_public_api_output.has_value() &&
        !write_text_file(*previous_public_api_output, *previous_public_api_payload, std::cerr)) {
        return ExitCode::CompileError;
    }
    if (!write_text_file(registry_index_output,
                         ahfl::package::serialize_registry_index_entry(registry_entry),
                         std::cerr)) {
        return ExitCode::CompileError;
    }

    auto evidence = ahfl::json::JsonValue::make_object();
    if (!dry_run) {
        const ahfl::package::Registry registry;
        auto upload = registry.publish_package(ahfl::package::RegistryPublishPackageRequest{
            .registry = registry_entry,
            .archive = *archive.archive,
            .public_api_snapshot = public_api_payload,
        });
        if (!upload.success()) {
            std::cerr << "error: package publish upload failed for " << registry_entry.package
                      << "@" << registry_entry.version << "\n";
            for (const auto &diagnostic : upload.diagnostics) {
                std::cerr << "error: " << diagnostic << "\n";
            }
            return ExitCode::CompileError;
        }
    }

    evidence->set("format_version",
                  ahfl::json::JsonValue::make_string(dry_run ? "ahfl.publish_dry_run.v1"
                                                             : "ahfl.publish.v1"));
    evidence->set("package", ahfl::json::JsonValue::make_string(manifest->package_name));
    evidence->set("version", ahfl::json::JsonValue::make_string(manifest->package_version));
    evidence->set("registry_id", ahfl::json::JsonValue::make_string(registry_entry.registry_id));
    evidence->set("source_archive_sha256",
                  ahfl::json::JsonValue::make_string(archive.archive->archive_sha256));
    evidence->set("manifest_sha256",
                  ahfl::json::JsonValue::make_string(archive.archive->manifest_sha256));
    evidence->set("public_api_sha256", ahfl::json::JsonValue::make_string(public_api_sha256));
    evidence->set("upload_performed", ahfl::json::JsonValue::make_bool(!dry_run));
    evidence->set("semver_gate", ahfl::json::JsonValue::make_string(semver_gate_status));
    if (previous_public_api_version.has_value()) {
        evidence->set("semver_from",
                      ahfl::json::JsonValue::make_string(*previous_public_api_version));
        evidence->set("semver_to", ahfl::json::JsonValue::make_string(manifest->package_version));
    }
    if (previous_public_api_sha256.has_value()) {
        evidence->set("previous_public_api_sha256",
                      ahfl::json::JsonValue::make_string(*previous_public_api_sha256));
    }

    auto artifacts = ahfl::json::JsonValue::make_object();
    artifacts->set("source_archive_manifest",
                   ahfl::json::JsonValue::make_string(archive_manifest_output.generic_string()));
    artifacts->set("source_archive_payload",
                   ahfl::json::JsonValue::make_string(archive_payload_output.generic_string()));
    artifacts->set("public_api_snapshot",
                   ahfl::json::JsonValue::make_string(public_api_output.generic_string()));
    if (previous_public_api_output.has_value()) {
        artifacts->set(
            "previous_public_api_snapshot",
            ahfl::json::JsonValue::make_string(previous_public_api_output->generic_string()));
    }
    artifacts->set("registry_index",
                   ahfl::json::JsonValue::make_string(registry_index_output.generic_string()));
    evidence->set("artifacts", std::move(artifacts));

    if (!write_text_file(
            publish_evidence_output, ahfl::json::serialize_json(*evidence), std::cerr)) {
        return ExitCode::CompileError;
    }

    std::cout << (dry_run ? "publish-dry-run: pass\n" : "publish: pass\n")
              << "registry: " << registry_entry.registry_id << '\n'
              << "wrote " << archive_manifest_output.generic_string() << '\n'
              << "wrote " << archive_payload_output.generic_string() << '\n'
              << "wrote " << public_api_output.generic_string() << '\n'
              << (previous_public_api_output.has_value()
                      ? "wrote " + previous_public_api_output->generic_string() + "\n"
                      : "")
              << "wrote " << registry_index_output.generic_string() << '\n'
              << "wrote " << publish_evidence_output.generic_string() << '\n'
              << "source-archive-sha256: " << archive.archive->archive_sha256 << '\n'
              << "manifest-sha256: " << archive.archive->manifest_sha256 << '\n'
              << "public-api-sha256: " << public_api_sha256 << '\n'
              << "upload: " << (dry_run ? "skipped" : "performed") << '\n';
    return ExitCode::Success;
}

ExitCode CliDriver::yank_package() {
    auto coordinate = parse_package_version_coordinate(options_.positional.front());
    if (!coordinate.has_value()) {
        std::cerr << "error: package yank requires <package>@<version>\n";
        return ExitCode::UsageError;
    }

    const auto reason = options_.package_yank_reason.has_value()
                            ? std::string{*options_.package_yank_reason}
                            : std::string{};
    const ahfl::package::Registry registry;
    auto result = registry.yank_package(coordinate->package_name,
                                        coordinate->version,
                                        std::string{*options_.package_registry_id},
                                        reason);
    if (!result.success() || !result.entry.has_value()) {
        std::cerr << "error: package yank failed for " << coordinate->package_name << "@"
                  << coordinate->version << "\n";
        for (const auto &diagnostic : result.diagnostics) {
            std::cerr << "error: " << diagnostic << "\n";
        }
        return ExitCode::CompileError;
    }

    std::cout << "package-yank: pass\n"
              << "registry: " << result.entry->registry_id << '\n'
              << "package: " << result.entry->package << '\n'
              << "version: " << result.entry->version << '\n'
              << "yanked: true\n";
    return ExitCode::Success;
}

ExitCode CliDriver::resolve_registry_dependencies() {
    const auto sysroot_manifest = sysroot_manifest_from_options(options_, std::cerr);
    if (!sysroot_manifest.manifest.has_value()) {
        if (!sysroot_manifest.had_error) {
            std::cerr << "error: failed to locate sysroot std/ahfl.toml; pass --sysroot <path>\n";
        }
        return ExitCode::UsageError;
    }

    const auto manifest_path =
        normalize_manifest_path(std::filesystem::path{std::string{*options_.manifest_path}});
    if (reject_mismatched_std_manifest(manifest_path, *sysroot_manifest.manifest, std::cerr)) {
        return ExitCode::CompileError;
    }

    auto graph_result =
        build_manifest_or_sysroot_package_graph(manifest_path, *sysroot_manifest.manifest);
    if (graph_result.has_errors() || !graph_result.graph.has_value()) {
        print_package_graph_diagnostics(graph_result.diagnostics, std::cerr);
        return ExitCode::CompileError;
    }

    const auto lockfile_path =
        normalize_manifest_path(std::filesystem::path{std::string{*options_.lockfile_path}});
    const auto lockfile =
        ahfl::package_graph::make_lockfile(*graph_result.graph, lockfile_path.parent_path());
    const auto payload = ahfl::package_graph::serialize_lockfile(lockfile);
    if (!write_text_file(lockfile_path, payload, std::cerr)) {
        return ExitCode::CompileError;
    }

    std::cout << "registry-resolve: pass\n"
              << "wrote " << lockfile_path.generic_string() << '\n';
    return ExitCode::Success;
}

ExitCode CliDriver::run_manifest_package() {
    const auto sysroot_manifest = sysroot_manifest_from_options(options_, std::cerr);
    if (!sysroot_manifest.manifest.has_value()) {
        if (!sysroot_manifest.had_error) {
            std::cerr << "error: failed to locate sysroot std/ahfl.toml; pass --sysroot <path>\n";
        }
        return ExitCode::UsageError;
    }

    const auto root_manifest_path =
        normalize_manifest_path(std::filesystem::path{std::string{*options_.manifest_path}});
    if (reject_mismatched_std_manifest(root_manifest_path, *sysroot_manifest.manifest, std::cerr)) {
        return ExitCode::CompileError;
    }
    const auto graph_result =
        build_manifest_or_sysroot_package_graph(root_manifest_path, *sysroot_manifest.manifest);
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
    const auto sysroot_manifest = sysroot_manifest_from_options(options_, std::cerr);
    if (!sysroot_manifest.manifest.has_value()) {
        if (!sysroot_manifest.had_error) {
            std::cerr << "error: failed to locate sysroot std/ahfl.toml; pass --sysroot <path>\n";
        }
        return ExitCode::UsageError;
    }

    const auto package_name = workspace_package_name(options_, effective_command_);
    if (!package_name.has_value()) {
        std::cerr << "error: --workspace requires --package <name>\n";
        return ExitCode::UsageError;
    }

    const auto workspace_manifest_path = normalize_manifest_path(
        std::filesystem::path{std::string{*options_.workspace_manifest_path}});
    const auto graph_result = ahfl::package_graph::build_package_graph_from_workspace(
        ahfl::package_graph::WorkspaceBuildInput{
            .workspace_manifest_path = workspace_manifest_path,
            .package_name = std::string{*package_name},
            .sysroot_manifest_path = *sysroot_manifest.manifest,
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

ExitCode CliDriver::run_source_sysroot_check(const ahfl::package_graph::PackageGraph &graph,
                                             const ahfl::package_graph::PackageNode &package) {
    std::vector<std::filesystem::path> entry_files;

    if (options_.target_name.has_value()) {
        const auto *target = select_target(package, options_, std::cerr);
        if (target == nullptr) {
            return ExitCode::UsageError;
        }
        const auto entry_file =
            entry_file_from_package_graph_target(graph, package, *target, std::cerr);
        if (!entry_file.has_value()) {
            return ExitCode::CompileError;
        }
        entry_files.push_back(*entry_file);
    } else if (!options_.manifest_path.has_value() && !options_.positional.empty()) {
        entry_files.push_back(
            normalize_manifest_path(std::filesystem::path{std::string{options_.positional[0]}}));
    } else {
        auto files = collect_ahfl_source_files_in_directory(
            package.module_root, "source sysroot", std::cerr);
        if (!files.has_value()) {
            return ExitCode::CompileError;
        }
        if (files->empty()) {
            std::cerr << "error: source sysroot package contains no .ahfl files\n";
            return ExitCode::CompileError;
        }
        entry_files = std::move(*files);
    }

    auto input = project_input_from_package_graph(graph, std::move(entry_files));
    auto project_result = ahfl::parse_project(frontend_, input);
    render_diagnostics(*diag_consumer_, project_result, std::nullopt);
    if (project_result.has_errors()) {
        return ExitCode::CompileError;
    }

    return run_analysis(project_result.graph, std::nullopt);
}

ExitCode CliDriver::run_package_graph_package(const ahfl::package_graph::PackageGraph &graph) {
    const auto *package = root_package(graph);
    if (package == nullptr) {
        if (effective_command_ == CommandKind::Check) {
            if (const auto *active_sysroot = sysroot_package(graph); active_sysroot != nullptr) {
                return run_source_sysroot_check(graph, *active_sysroot);
            }
        }
        std::cerr << "error: PackageGraph is missing root package\n";
        return ExitCode::CompileError;
    }

    if (is_public_api_artifact_command(effective_command_)) {
        auto entry_files = public_api_entry_files_from_package_graph(graph, *package);
        if (entry_files.empty()) {
            std::cerr << "error: emit " << command_short_name(*effective_command_)
                      << " requires at least one exported module in ahfl.toml\n";
            return ExitCode::CompileError;
        }

        public_api_package_context_ = public_api_context_from_package(*package);
        auto input = project_input_from_package_graph(graph, std::move(entry_files));
        auto project_result = ahfl::parse_project(frontend_, input);
        render_diagnostics(*diag_consumer_, project_result, std::nullopt);
        if (project_result.has_errors()) {
            return ExitCode::CompileError;
        }

        return run_analysis(project_result.graph, std::nullopt);
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

    const auto entry_file =
        entry_file_from_package_graph_target(graph, *package, *target, std::cerr);
    if (!entry_file.has_value()) {
        return ExitCode::CompileError;
    }

    auto input = project_input_from_package_graph(graph, *entry_file);
    auto project_result = ahfl::parse_project(frontend_, input);
    render_diagnostics(*diag_consumer_, project_result, std::nullopt);
    if (project_result.has_errors()) {
        return ExitCode::CompileError;
    }

    return run_analysis(project_result.graph, std::nullopt);
}

ExitCode CliDriver::dump_package_graph() {
    const auto sysroot_manifest = sysroot_manifest_from_options(options_, std::cerr);
    if (!sysroot_manifest.manifest.has_value()) {
        if (!sysroot_manifest.had_error) {
            std::cerr << "error: failed to locate sysroot std/ahfl.toml; pass --sysroot <path>\n";
        }
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
                .sysroot_manifest_path = *sysroot_manifest.manifest,
            });
        if (result.has_errors() || !result.graph.has_value()) {
            print_package_graph_diagnostics(result.diagnostics, std::cerr);
            return ExitCode::CompileError;
        }

        std::cout << ahfl::package_graph::serialize_package_graph_json(*result.graph) << '\n';
        return ExitCode::Success;
    }

    const auto root_manifest_path =
        normalize_manifest_path(std::filesystem::path{std::string{*options_.manifest_path}});
    if (reject_mismatched_std_manifest(root_manifest_path, *sysroot_manifest.manifest, std::cerr)) {
        return ExitCode::CompileError;
    }
    const auto result =
        build_manifest_or_sysroot_package_graph(root_manifest_path, *sysroot_manifest.manifest);
    if (result.has_errors() || !result.graph.has_value()) {
        print_package_graph_diagnostics(result.diagnostics, std::cerr);
        return ExitCode::CompileError;
    }

    std::cout << ahfl::package_graph::serialize_package_graph_json(*result.graph) << '\n';
    return ExitCode::Success;
}

ExitCode CliDriver::dump_lockfile() {
    const auto sysroot_manifest = sysroot_manifest_from_options(options_, std::cerr);
    if (!sysroot_manifest.manifest.has_value()) {
        if (!sysroot_manifest.had_error) {
            std::cerr << "error: failed to locate sysroot std/ahfl.toml; pass --sysroot <path>\n";
        }
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
                .sysroot_manifest_path = *sysroot_manifest.manifest,
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

    const auto root_manifest_path =
        normalize_manifest_path(std::filesystem::path{std::string{*options_.manifest_path}});
    if (reject_mismatched_std_manifest(root_manifest_path, *sysroot_manifest.manifest, std::cerr)) {
        return ExitCode::CompileError;
    }
    const auto result =
        build_manifest_or_sysroot_package_graph(root_manifest_path, *sysroot_manifest.manifest);
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
        const auto sysroot_manifest = sysroot_manifest_from_options(options_, std::cerr);
        if (!sysroot_manifest.manifest.has_value()) {
            if (!sysroot_manifest.had_error) {
                std::cerr
                    << "error: failed to locate sysroot std/ahfl.toml; pass --sysroot <path>\n";
            }
            return ExitCode::UsageError;
        }

        ahfl::package_graph::BuildResult graph_result;
        std::filesystem::path lockfile_directory;
        if (options_.manifest_path.has_value()) {
            const auto root_manifest_path = normalize_manifest_path(
                std::filesystem::path{std::string{*options_.manifest_path}});
            if (reject_mismatched_std_manifest(
                    root_manifest_path, *sysroot_manifest.manifest, std::cerr)) {
                return ExitCode::CompileError;
            }
            graph_result = build_manifest_or_sysroot_package_graph(root_manifest_path,
                                                                   *sysroot_manifest.manifest);
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
                    .sysroot_manifest_path = *sysroot_manifest.manifest,
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

    if (is_public_api_artifact_command(effective_command_)) {
        if constexpr (std::is_same_v<InputT, ahfl::SourceGraph>) {
            if (!public_api_package_context_.has_value()) {
                std::cerr << "internal error: public API emission missing package context\n";
                return ExitCode::CompileError;
            }
            const auto status = effective_command_ == CommandKind::EmitPublicApi
                                    ? emit_public_api_snapshot(input,
                                                               resolve_result,
                                                               type_check_result,
                                                               *public_api_package_context_,
                                                               std::cout,
                                                               std::cerr)
                                    : emit_public_api_docs(input,
                                                           resolve_result,
                                                           type_check_result,
                                                           *public_api_package_context_,
                                                           std::cout,
                                                           std::cerr);
            return status == 0 ? ExitCode::Success : ExitCode::CompileError;
        } else {
            std::cerr << "error: emit " << command_short_name(*effective_command_)
                      << " requires an AHFL package manifest; pass --manifest or --workspace\n";
            return ExitCode::UsageError;
        }
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
        auto run_options = options_;
        if (!run_options.workflow_name.has_value()) {
            if (package_metadata_ptr == nullptr ||
                !package_metadata_ptr->entry_target.has_value()) {
                std::cerr << "error: run requires --workflow or package workflow entry\n";
                return ExitCode::UsageError;
            }
            const auto &entry = *package_metadata_ptr->entry_target;
            if (entry.kind != ahfl::handoff::ExecutableKind::Workflow) {
                std::cerr << "error: run package entry '" << entry.canonical_name
                          << "' is an agent; target entry must be a workflow\n";
                return ExitCode::UsageError;
            }
            run_options.workflow_name = std::string_view{entry.canonical_name};
        }
        if (options_.optimize_requested) {
            run_requested_semantic_optimization_pipeline(ir_program, run_options, std::cerr);
        }
        const auto status = run_workflow_with_llm(ir_program, run_options, std::cout, std::cerr);
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
