#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "compiler/manifest/manifest.hpp"
#include "compiler/package_graph/lockfile.hpp"
#include "compiler/package_graph/package_graph.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using ahfl::manifest::PackageManifest;
using ahfl::package_graph::BuildInput;
using ahfl::package_graph::PackageInput;
using ahfl::package_graph::PackageSourceKind;

[[nodiscard]] bool write_text_file(const std::filesystem::path &path, std::string_view text) {
    std::ofstream output(path);
    if (!output) {
        return false;
    }
    output << text;
    return static_cast<bool>(output);
}

[[nodiscard]] PackageManifest parse_package(std::string_view input) {
    auto result = ahfl::manifest::parse_package_manifest(input);
    INFO("package manifest diagnostics count: " << result.diagnostics.size());
    REQUIRE_FALSE(result.has_errors());
    REQUIRE(result.manifest.has_value());
    return *result.manifest;
}

[[nodiscard]] PackageInput package_input(std::string_view text,
                                         std::filesystem::path root,
                                         PackageSourceKind source,
                                         std::string checksum) {
    const auto manifest_path = root / "ahfl.toml";
    return PackageInput{
        .manifest = parse_package(text),
        .package_root = root,
        .source = source,
        .manifest_path = manifest_path,
        .checksum = std::move(checksum),
    };
}

[[nodiscard]] PackageInput std_input() {
    return package_input(R"TOML(manifest_version = 1

[package]
name = "std"
version = "0.1.0"
edition = "2026"
kind = "standard-library"

[module]
prefix = "std"
root = "."

[prelude]
module = "std::prelude"
injection = "explicit"

[exports]
modules = ["prelude", "option"]

[compiler_intrinsics]
allow = ["option_*"]
)TOML",
                         "sysroot/std",
                         PackageSourceKind::Sysroot,
                         "sha256:0000000000000000000000000000000000000000000000000000000000000000");
}

[[nodiscard]] PackageInput app_input(std::string_view extra_deps = "") {
    std::string text = R"TOML(manifest_version = 1

[package]
name = "refund-audit"
version = "0.1.0"
edition = "2026"
kind = "application"

[module]
prefix = "refund_audit"
root = "src"

[exports]
modules = ["main"]

[targets.workflow]
kind = "handoff"
entry = "refund_audit::main::RefundAuditWorkflow"
exports = ["refund_audit::main::RefundAuditWorkflow"]

[dependencies]
std = { source = "sysroot" }
)TOML";
    text += extra_deps;
    return package_input(text,
                         "workspace/packages/refund-audit",
                         PackageSourceKind::Root,
                         "sha256:1111111111111111111111111111111111111111111111111111111111111111");
}

[[nodiscard]] PackageInput library_input(std::string_view name,
                                         std::string_view prefix,
                                         PackageSourceKind source,
                                         std::string_view deps = "") {
    std::string text = "manifest_version = 1\n\n";
    text += "[package]\n";
    text += "name = \"";
    text += name;
    text += "\"\nversion = \"0.1.0\"\nedition = \"2026\"\nkind = \"library\"\n\n";
    text += "[module]\nprefix = \"";
    text += prefix;
    text += "\"\nroot = \"src\"\n\n";
    text += "[exports]\nmodules = [\"lib\"]\n\n";
    text += "[targets.lib]\nkind = \"library\"\nentry = \"src/lib.ahfl\"\n\n";
    if (!deps.empty()) {
        text += "[dependencies]\n";
        text += deps;
        text += "\n";
    }
    std::filesystem::path root = std::filesystem::path{"workspace/packages"} / std::string{name};
    return package_input(text,
                         root,
                         source,
                         "sha256:2222222222222222222222222222222222222222222222222222222222222222");
}

[[nodiscard]] bool has_diagnostic(const std::vector<ahfl::package_graph::Diagnostic> &diagnostics,
                                  std::string_view needle) {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [needle](const auto &diag) {
        return diag.message.find(needle) != std::string::npos;
    });
}

[[nodiscard]] bool has_error(const ahfl::package_graph::BuildResult &result,
                             std::string_view needle) {
    return has_diagnostic(result.diagnostics, needle);
}

[[nodiscard]] ahfl::package_graph::PackageGraph graph_with_workspace_dependency() {
    auto result = ahfl::package_graph::build_package_graph(BuildInput{
        .sysroot_std = std_input(),
        .root_package = app_input("audit-core = { source = \"workspace\" }\n"),
        .workspace_packages = {library_input(
            "audit-core", "audit_core", PackageSourceKind::Workspace)},
    });

    REQUIRE_FALSE(result.has_errors());
    REQUIRE(result.graph.has_value());
    return std::move(*result.graph);
}

[[nodiscard]] ahfl::package_graph::BuildResult build_manifest_graph_with_exports(
    std::string_view exported_modules, bool write_main, bool write_ambiguous_lib) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root_dir = std::filesystem::temp_directory_path() /
                          ("ahfl-package-graph-export-validation-" + std::to_string(stamp));
    const auto sysroot_dir = root_dir / "sysroot" / "std";
    const auto package_dir = root_dir / "app";

    std::error_code error;
    std::filesystem::remove_all(root_dir, error);
    REQUIRE(std::filesystem::create_directories(sysroot_dir, error));
    REQUIRE_FALSE(error);
    REQUIRE(std::filesystem::create_directories(package_dir / "src", error));
    REQUIRE_FALSE(error);

    REQUIRE(write_text_file(sysroot_dir / "prelude.ahfl", "module std::prelude;\n"));
    REQUIRE(write_text_file(sysroot_dir / "option.ahfl", "module std::option;\n"));
    if (write_main) {
        REQUIRE(write_text_file(package_dir / "src" / "main.ahfl", "module refund_audit::main;\n"));
    }
    if (write_ambiguous_lib) {
        REQUIRE(write_text_file(package_dir / "src" / "lib.ahfl", "module refund_audit::lib;\n"));
        REQUIRE(std::filesystem::create_directories(package_dir / "src" / "lib", error));
        REQUIRE_FALSE(error);
        REQUIRE(write_text_file(package_dir / "src" / "lib" / "mod.ahfl",
                                "module refund_audit::lib;\n"));
    }

    REQUIRE(write_text_file(sysroot_dir / "ahfl.toml",
                            R"TOML(manifest_version = 1

[package]
name = "std"
version = "0.1.0"
edition = "2026"
kind = "standard-library"

[module]
prefix = "std"
root = "."

[prelude]
module = "std::prelude"
injection = "explicit"

[exports]
modules = ["prelude", "option"]

[compiler_intrinsics]
allow = ["option_*"]
)TOML"));

    std::string manifest = R"TOML(manifest_version = 1

[package]
name = "refund-audit"
version = "0.1.0"
edition = "2026"
kind = "application"

[module]
prefix = "refund_audit"
root = "src"

[exports]
modules = )TOML";
    manifest += exported_modules;
    manifest += R"TOML(

[targets.workflow]
kind = "handoff"
entry = "refund_audit::main::RefundAuditWorkflow"
exports = ["refund_audit::main::RefundAuditWorkflow"]

[dependencies]
std = { source = "sysroot" }
)TOML";
    REQUIRE(write_text_file(package_dir / "ahfl.toml", manifest));

    auto result = ahfl::package_graph::build_package_graph_from_manifests(
        ahfl::package_graph::ManifestBuildInput{
            .root_manifest_path = package_dir / "ahfl.toml",
            .sysroot_manifest_path = sysroot_dir / "ahfl.toml",
        });
    std::filesystem::remove_all(root_dir, error);
    return result;
}

} // namespace

TEST_CASE("PackageGraph assigns sysroot std to PackageId(0) and root to PackageId(1)") {
    auto result = ahfl::package_graph::build_package_graph(BuildInput{
        .sysroot_std = std_input(),
        .root_package = app_input(),
    });

    REQUIRE_FALSE(result.has_errors());
    REQUIRE(result.graph.has_value());
    REQUIRE(result.graph->packages.size() == 2);
    CHECK(result.graph->packages[0].id.value == 0);
    CHECK(result.graph->packages[0].name == "std");
    CHECK(result.graph->packages[1].id.value == 1);
    CHECK(result.graph->packages[1].name == "refund-audit");
    REQUIRE(result.graph->packages[1].targets.size() == 1);
    REQUIRE(result.graph->packages[1].targets[0].exports.size() == 1);
    CHECK(result.graph->packages[1].targets[0].exports[0].kind == "workflow");
    CHECK(result.graph->packages[1].targets[0].exports[0].name ==
          "refund_audit::main::RefundAuditWorkflow");
    REQUIRE(result.graph->dependencies.size() == 1);
    CHECK(result.graph->dependencies.front().from.value == 1);
    CHECK(result.graph->dependencies.front().to.value == 0);
    REQUIRE(result.graph->module_roots.size() == 2);
    CHECK(result.graph->module_roots[0].prefix == "std");
    CHECK(result.graph->module_roots[1].prefix == "refund_audit");
}

TEST_CASE("PackageGraph resolves explicit workspace dependencies only") {
    auto result = ahfl::package_graph::build_package_graph(BuildInput{
        .sysroot_std = std_input(),
        .root_package = app_input("audit-core = { source = \"workspace\" }\n"),
        .workspace_packages = {library_input(
            "audit-core", "audit_core", PackageSourceKind::Workspace)},
    });

    REQUIRE_FALSE(result.has_errors());
    REQUIRE(result.graph.has_value());
    REQUIRE(result.graph->packages.size() == 3);
    REQUIRE(result.graph->dependencies.size() == 2);
    CHECK(result.graph->dependencies[1].dependency_key == "audit-core");
    CHECK(result.graph->dependencies[1].from.value == 1);
    CHECK(result.graph->dependencies[1].to.value == 2);
}

TEST_CASE("PackageGraph rejects duplicate module prefix before resolver") {
    auto result = ahfl::package_graph::build_package_graph(BuildInput{
        .sysroot_std = std_input(),
        .root_package = app_input(),
        .workspace_packages = {library_input(
            "audit-core", "refund_audit", PackageSourceKind::Workspace)},
    });

    REQUIRE(result.has_errors());
    CHECK(has_error(result, "duplicate module prefix"));
}

TEST_CASE("PackageGraph rejects missing dependencies") {
    auto result = ahfl::package_graph::build_package_graph(BuildInput{
        .sysroot_std = std_input(),
        .root_package = app_input("audit-core = { source = \"workspace\" }\n"),
    });

    REQUIRE(result.has_errors());
    CHECK(has_error(result, "missing dependency package 'audit-core'"));
}

TEST_CASE("PackageGraph rejects dependency cycles") {
    auto result = ahfl::package_graph::build_package_graph(BuildInput{
        .sysroot_std = std_input(),
        .root_package = app_input("audit-core = { source = \"workspace\" }\n"),
        .workspace_packages =
            {
                library_input("audit-core",
                              "audit_core",
                              PackageSourceKind::Workspace,
                              "refund-audit = { source = \"workspace\" }"),
            },
    });

    REQUIRE(result.has_errors());
    CHECK(has_error(result, "dependency cycle"));
}

TEST_CASE("PackageGraph rejects user package named std") {
    auto result = ahfl::package_graph::build_package_graph(BuildInput{
        .sysroot_std = std_input(),
        .root_package = library_input("std", "user_std", PackageSourceKind::Root),
    });

    REQUIRE(result.has_errors());
    CHECK(has_error(result, "user package cannot be named 'std'"));
}

TEST_CASE("Manifest PackageGraph loader resolves path dependencies") {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root_dir = std::filesystem::temp_directory_path() /
                          ("ahfl-package-graph-manifest-loader-" + std::to_string(stamp));
    const auto sysroot_dir = root_dir / "sysroot" / "std";
    const auto package_dir = root_dir / "app";
    const auto dependency_dir = package_dir / "packages" / "audit-core";

    std::error_code error;
    std::filesystem::remove_all(root_dir, error);
    REQUIRE(std::filesystem::create_directories(sysroot_dir, error));
    REQUIRE_FALSE(error);
    REQUIRE(std::filesystem::create_directories(package_dir / "src", error));
    REQUIRE_FALSE(error);
    REQUIRE(std::filesystem::create_directories(dependency_dir, error));
    REQUIRE_FALSE(error);
    REQUIRE(std::filesystem::create_directories(dependency_dir / "src", error));
    REQUIRE_FALSE(error);

    REQUIRE(write_text_file(sysroot_dir / "prelude.ahfl", "module std::prelude;\n"));
    REQUIRE(write_text_file(sysroot_dir / "option.ahfl", "module std::option;\n"));
    REQUIRE(write_text_file(package_dir / "src" / "main.ahfl", "module refund_audit::main;\n"));
    REQUIRE(write_text_file(dependency_dir / "src" / "lib.ahfl", "module audit_core::lib;\n"));
    REQUIRE(write_text_file(sysroot_dir / "ahfl.toml",
                            R"TOML(manifest_version = 1

[package]
name = "std"
version = "0.1.0"
edition = "2026"
kind = "standard-library"

[module]
prefix = "std"
root = "."

[prelude]
module = "std::prelude"
injection = "explicit"

[exports]
modules = ["prelude", "option"]

[compiler_intrinsics]
allow = ["option_*"]
)TOML"));
    REQUIRE(write_text_file(package_dir / "ahfl.toml",
                            R"TOML(manifest_version = 1

[package]
name = "refund-audit"
version = "0.1.0"
edition = "2026"
kind = "application"

[module]
prefix = "refund_audit"
root = "src"

[exports]
modules = ["main"]

[targets.workflow]
kind = "handoff"
entry = "refund_audit::main::RefundAuditWorkflow"
exports = ["refund_audit::main::RefundAuditWorkflow"]

[dependencies]
std = { source = "sysroot" }
audit-core = { source = "path", path = "packages/audit-core", version = "0.1.0" }
)TOML"));
    REQUIRE(write_text_file(dependency_dir / "ahfl.toml",
                            R"TOML(manifest_version = 1

[package]
name = "audit-core"
version = "0.1.0"
edition = "2026"
kind = "library"

[module]
prefix = "audit_core"
root = "src"

[exports]
modules = ["lib"]

[targets.lib]
kind = "library"
entry = "src/lib.ahfl"
)TOML"));

    auto result = ahfl::package_graph::build_package_graph_from_manifests(
        ahfl::package_graph::ManifestBuildInput{
            .root_manifest_path = package_dir / "ahfl.toml",
            .sysroot_manifest_path = sysroot_dir / "ahfl.toml",
        });
    std::filesystem::remove_all(root_dir, error);

    INFO("package graph diagnostics count: " << result.diagnostics.size());
    REQUIRE_FALSE(result.has_errors());
    REQUIRE(result.graph.has_value());
    REQUIRE(result.graph->packages.size() == 3);
    CHECK(result.graph->packages[0].name == "std");
    CHECK(result.graph->packages[0].checksum.starts_with("sha256:"));
    CHECK(result.graph->packages[0].checksum.size() == 71);
    CHECK(result.graph->packages[1].name == "refund-audit");
    CHECK(result.graph->packages[1].checksum.starts_with("sha256:"));
    CHECK(result.graph->packages[1].checksum.size() == 71);
    CHECK(result.graph->packages[2].name == "audit-core");
    CHECK(result.graph->packages[2].checksum.starts_with("sha256:"));
    CHECK(result.graph->packages[2].checksum.size() == 71);
    REQUIRE(result.graph->dependencies.size() == 2);
    CHECK(result.graph->dependencies[1].dependency_key == "audit-core");
    CHECK(result.graph->dependencies[1].from.value == 1);
    CHECK(result.graph->dependencies[1].to.value == 2);
}

TEST_CASE("Manifest PackageGraph loader rejects invalid exported modules") {
    SUBCASE("missing exported module") {
        auto result = build_manifest_graph_with_exports("[\"missing\"]", false, false);

        REQUIRE(result.has_errors());
        CHECK(has_diagnostic(result.diagnostics, "export module 'missing' does not exist"));
    }

    SUBCASE("duplicate exported module") {
        auto result = build_manifest_graph_with_exports("[\"main\", \"main\"]", true, false);

        REQUIRE(result.has_errors());
        CHECK(has_diagnostic(result.diagnostics, "exports duplicate module 'main'"));
    }

    SUBCASE("export path escape") {
        auto result = build_manifest_graph_with_exports("[\"../secret\"]", true, false);

        REQUIRE(result.has_errors());
        CHECK(has_diagnostic(result.diagnostics, "must be a relative module path"));
    }

    SUBCASE("single-file and directory module ambiguity") {
        auto result = build_manifest_graph_with_exports("[\"lib\"]", false, true);

        REQUIRE(result.has_errors());
        CHECK(has_diagnostic(result.diagnostics, "both single-file and directory-module"));
    }
}

TEST_CASE("Workspace PackageGraph loader resolves members and sysroot defaults") {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root_dir = std::filesystem::temp_directory_path() /
                          ("ahfl-package-graph-workspace-loader-" + std::to_string(stamp));
    const auto sysroot_dir = root_dir / "sysroot" / "std";
    const auto workspace_dir = root_dir / "workspace";
    const auto app_dir = workspace_dir / "packages" / "refund-audit";
    const auto dependency_dir = workspace_dir / "packages" / "audit-core";

    std::error_code error;
    std::filesystem::remove_all(root_dir, error);
    REQUIRE(std::filesystem::create_directories(sysroot_dir, error));
    REQUIRE_FALSE(error);
    REQUIRE(std::filesystem::create_directories(app_dir, error));
    REQUIRE_FALSE(error);
    REQUIRE(std::filesystem::create_directories(app_dir / "src", error));
    REQUIRE_FALSE(error);
    REQUIRE(std::filesystem::create_directories(dependency_dir, error));
    REQUIRE_FALSE(error);
    REQUIRE(std::filesystem::create_directories(dependency_dir / "src", error));
    REQUIRE_FALSE(error);

    REQUIRE(write_text_file(sysroot_dir / "prelude.ahfl", "module std::prelude;\n"));
    REQUIRE(write_text_file(sysroot_dir / "option.ahfl", "module std::option;\n"));
    REQUIRE(write_text_file(app_dir / "src" / "main.ahfl", "module refund_audit::main;\n"));
    REQUIRE(write_text_file(dependency_dir / "src" / "lib.ahfl", "module audit_core::lib;\n"));
    REQUIRE(write_text_file(sysroot_dir / "ahfl.toml",
                            R"TOML(manifest_version = 1

[package]
name = "std"
version = "0.1.0"
edition = "2026"
kind = "standard-library"

[module]
prefix = "std"
root = "."

[prelude]
module = "std::prelude"
injection = "explicit"

[exports]
modules = ["prelude", "option"]

[compiler_intrinsics]
allow = ["option_*"]
)TOML"));
    REQUIRE(write_text_file(workspace_dir / "ahfl.workspace.toml",
                            R"TOML(manifest_version = 1

[workspace]
name = "commerce-workflows"
members = ["packages/refund-audit", "packages/audit-core"]

[resolver]
version = 1

[dependencies]
std = { source = "sysroot" }
)TOML"));
    REQUIRE(write_text_file(app_dir / "ahfl.toml",
                            R"TOML(manifest_version = 1

[package]
name = "refund-audit"
version = "0.1.0"
edition = "2026"
kind = "application"

[module]
prefix = "refund_audit"
root = "src"

[exports]
modules = ["main"]

[targets.workflow]
kind = "handoff"
entry = "refund_audit::main::RefundAuditWorkflow"
exports = ["refund_audit::main::RefundAuditWorkflow"]

[dependencies]
audit-core = { source = "workspace" }
)TOML"));
    REQUIRE(write_text_file(dependency_dir / "ahfl.toml",
                            R"TOML(manifest_version = 1

[package]
name = "audit-core"
version = "0.1.0"
edition = "2026"
kind = "library"

[module]
prefix = "audit_core"
root = "src"

[exports]
modules = ["lib"]

[targets.lib]
kind = "library"
entry = "src/lib.ahfl"
)TOML"));

    auto result = ahfl::package_graph::build_package_graph_from_workspace(
        ahfl::package_graph::WorkspaceBuildInput{
            .workspace_manifest_path = workspace_dir / "ahfl.workspace.toml",
            .package_name = "refund-audit",
            .sysroot_manifest_path = sysroot_dir / "ahfl.toml",
        });
    std::filesystem::remove_all(root_dir, error);

    INFO("package graph diagnostics count: " << result.diagnostics.size());
    REQUIRE_FALSE(result.has_errors());
    REQUIRE(result.graph.has_value());
    REQUIRE(result.graph->packages.size() == 3);
    CHECK(result.graph->packages[0].name == "std");
    CHECK(result.graph->packages[1].name == "refund-audit");
    CHECK(result.graph->packages[1].source == PackageSourceKind::Root);
    CHECK(result.graph->packages[2].name == "audit-core");
    CHECK(result.graph->packages[2].source == PackageSourceKind::Workspace);
    REQUIRE(result.graph->dependencies.size() == 3);
    CHECK(result.graph->dependencies[0].dependency_key == "audit-core");
    CHECK(result.graph->dependencies[0].from.value == 1);
    CHECK(result.graph->dependencies[0].to.value == 2);
    CHECK(result.graph->dependencies[1].dependency_key == "std");
    CHECK(result.graph->dependencies[1].from.value == 1);
    CHECK(result.graph->dependencies[1].to.value == 0);
    CHECK(result.graph->dependencies[2].dependency_key == "std");
    CHECK(result.graph->dependencies[2].from.value == 2);
    CHECK(result.graph->dependencies[2].to.value == 0);
}

TEST_CASE("Workspace PackageGraph loader rejects dependency default conflicts") {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root_dir = std::filesystem::temp_directory_path() /
                          ("ahfl-package-graph-workspace-conflict-" + std::to_string(stamp));
    const auto sysroot_dir = root_dir / "sysroot" / "std";
    const auto workspace_dir = root_dir / "workspace";
    const auto app_dir = workspace_dir / "packages" / "refund-audit";
    const auto dependency_dir = workspace_dir / "vendor" / "audit-core";

    std::error_code error;
    std::filesystem::remove_all(root_dir, error);
    REQUIRE(std::filesystem::create_directories(sysroot_dir, error));
    REQUIRE_FALSE(error);
    REQUIRE(std::filesystem::create_directories(app_dir, error));
    REQUIRE_FALSE(error);
    REQUIRE(std::filesystem::create_directories(dependency_dir, error));
    REQUIRE_FALSE(error);

    REQUIRE(write_text_file(sysroot_dir / "ahfl.toml",
                            R"TOML(manifest_version = 1

[package]
name = "std"
version = "0.1.0"
edition = "2026"
kind = "standard-library"

[module]
prefix = "std"
root = "."

[prelude]
module = "std::prelude"
injection = "explicit"

[exports]
modules = ["prelude", "option"]

[compiler_intrinsics]
allow = ["option_*"]
)TOML"));
    REQUIRE(write_text_file(workspace_dir / "ahfl.workspace.toml",
                            R"TOML(manifest_version = 1

[workspace]
name = "commerce-workflows"
members = ["packages/refund-audit"]

[resolver]
version = 1

[dependencies]
audit-core = { source = "workspace" }
)TOML"));
    REQUIRE(write_text_file(app_dir / "ahfl.toml",
                            R"TOML(manifest_version = 1

[package]
name = "refund-audit"
version = "0.1.0"
edition = "2026"
kind = "application"

[module]
prefix = "refund_audit"
root = "src"

[exports]
modules = ["main"]

[targets.workflow]
kind = "handoff"
entry = "refund_audit::main::RefundAuditWorkflow"
exports = ["refund_audit::main::RefundAuditWorkflow"]

[dependencies]
audit-core = { source = "path", path = "../../vendor/audit-core", version = "0.1.0" }
)TOML"));
    REQUIRE(write_text_file(dependency_dir / "ahfl.toml",
                            R"TOML(manifest_version = 1

[package]
name = "audit-core"
version = "0.1.0"
edition = "2026"
kind = "library"

[module]
prefix = "audit_core"
root = "src"

[exports]
modules = ["lib"]

[targets.lib]
kind = "library"
entry = "src/lib.ahfl"
)TOML"));

    auto result = ahfl::package_graph::build_package_graph_from_workspace(
        ahfl::package_graph::WorkspaceBuildInput{
            .workspace_manifest_path = workspace_dir / "ahfl.workspace.toml",
            .package_name = "refund-audit",
            .sysroot_manifest_path = sysroot_dir / "ahfl.toml",
        });
    std::filesystem::remove_all(root_dir, error);

    REQUIRE(result.has_errors());
    CHECK(has_diagnostic(result.diagnostics, "conflicts with package dependency declaration"));
}

TEST_CASE("Lockfile serializes and parses PackageGraph identity") {
    const auto graph = graph_with_workspace_dependency();

    const auto lockfile = ahfl::package_graph::make_lockfile(graph);
    CHECK(lockfile.format_version == "ahfl.lock.v1");
    CHECK(lockfile.resolver_version == 1);
    CHECK(lockfile.root_package == "refund-audit");
    REQUIRE(lockfile.packages.size() == 3);
    CHECK(lockfile.packages[0].id.value == 0);
    CHECK(lockfile.packages[0].name == "std");
    CHECK(lockfile.packages[0].source == "sysroot");
    CHECK(lockfile.packages[0].manifest == "sysroot/std/ahfl.toml");
    CHECK(lockfile.packages[1].name == "refund-audit");
    CHECK(lockfile.packages[1].source == "path");
    REQUIRE(lockfile.edges.size() == 2);
    CHECK(lockfile.edges[0].from.value == 1);
    CHECK(lockfile.edges[0].dependency == "std");
    CHECK(lockfile.edges[0].to.value == 0);

    const auto encoded = ahfl::package_graph::serialize_lockfile(lockfile);
    auto parsed = ahfl::package_graph::parse_lockfile_json(encoded);

    REQUIRE_FALSE(parsed.has_errors());
    REQUIRE(parsed.lockfile.has_value());
    CHECK(parsed.lockfile->packages.size() == lockfile.packages.size());
    CHECK(parsed.lockfile->edges.size() == lockfile.edges.size());
    CHECK(ahfl::package_graph::check_lockfile_drift(graph, *parsed.lockfile).empty());
}

TEST_CASE("Lockfile drift rejects checksum mismatch") {
    const auto graph = graph_with_workspace_dependency();
    auto lockfile = ahfl::package_graph::make_lockfile(graph);
    lockfile.packages[0].checksum =
        "sha256:ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";

    const auto diagnostics = ahfl::package_graph::check_lockfile_drift(graph, lockfile);

    REQUIRE_FALSE(diagnostics.empty());
    CHECK(has_diagnostic(diagnostics, "field 'checksum'"));
}

TEST_CASE("Lockfile drift rejects unused packages") {
    const auto graph = graph_with_workspace_dependency();
    auto lockfile = ahfl::package_graph::make_lockfile(graph);
    lockfile.packages.push_back(ahfl::package_graph::LockfilePackage{
        .id = ahfl::package_graph::PackageId{99},
        .name = "unused",
        .version = "0.1.0",
        .source = "path",
        .manifest = "unused/ahfl.toml",
        .checksum = "sha256:9999999999999999999999999999999999999999999999999999999999999999",
    });

    const auto diagnostics = ahfl::package_graph::check_lockfile_drift(graph, lockfile);

    REQUIRE_FALSE(diagnostics.empty());
    CHECK(has_diagnostic(diagnostics, "unused package id 99"));
}

TEST_CASE("Lockfile drift rejects dependency edge mismatch") {
    const auto graph = graph_with_workspace_dependency();
    auto lockfile = ahfl::package_graph::make_lockfile(graph);
    lockfile.edges[0].to = ahfl::package_graph::PackageId{1};

    const auto diagnostics = ahfl::package_graph::check_lockfile_drift(graph, lockfile);

    REQUIRE_FALSE(diagnostics.empty());
    CHECK(has_diagnostic(diagnostics, "missing dependency edge 1 --std--> 0"));
    CHECK(has_diagnostic(diagnostics, "unused dependency edge 1 --std--> 1"));
}

TEST_CASE("Lockfile drift rejects PackageId assignment drift") {
    const auto graph = graph_with_workspace_dependency();
    auto lockfile = ahfl::package_graph::make_lockfile(graph);
    lockfile.packages[0].name = "refund-audit";

    const auto diagnostics = ahfl::package_graph::check_lockfile_drift(graph, lockfile);

    REQUIRE_FALSE(diagnostics.empty());
    CHECK(has_diagnostic(diagnostics, "package id 0 is 'refund-audit'"));
}

TEST_CASE("Lockfile reader rejects schema errors with source ranges") {
    auto parsed = ahfl::package_graph::parse_lockfile_json(R"JSON({
  "format_version": "ahfl.lock.v1",
  "resolver_version": "1",
  "root_package": "refund-audit",
  "packages": [],
  "edges": []
})JSON");

    REQUIRE(parsed.has_errors());
    REQUIRE_FALSE(parsed.lockfile.has_value());
    CHECK(has_diagnostic(parsed.diagnostics, "resolver_version"));
    CHECK(parsed.diagnostics.front().range.end_offset >
          parsed.diagnostics.front().range.begin_offset);
}
