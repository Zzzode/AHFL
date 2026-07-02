#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "compiler/manifest/manifest.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

[[nodiscard]] bool has_code(const std::vector<ahfl::manifest::ManifestDiagnostic> &diagnostics,
                            std::string_view code) {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [code](const auto &diag) {
        return diag.code == code;
    });
}

} // namespace

TEST_CASE("Package manifest schema accepts RFC 0005 minimal package") {
    constexpr std::string_view input = R"TOML(manifest_version = 1

[package]
name = "refund-audit"
version = "0.1.0"
edition = "2026"
kind = "application"

[module]
prefix = "refund_audit"
root = "src"

[exports]
modules = ["main", "agents"]

[targets.lib]
kind = "library"
entry = "src/lib.ahfl"

[targets.workflow]
kind = "handoff"
entry = "refund_audit::main::RefundAuditWorkflow"
exports = ["refund_audit::main::RefundAuditWorkflow"]

[[targets.workflow.capability_bindings]]
capability = "OrderQuery"
binding_key = "order.query"

[dependencies]
std = { source = "sysroot" }
)TOML";

    const auto result = ahfl::manifest::parse_package_manifest(input);
    REQUIRE_FALSE(result.has_errors());
    REQUIRE(result.manifest.has_value());
    CHECK(result.manifest->package_name == "refund-audit");
    CHECK(result.manifest->module_prefix == "refund_audit");
    REQUIRE(result.manifest->targets.size() == 2);
    CHECK(result.manifest->targets[1].capability_bindings.size() == 1);
    REQUIRE(result.manifest->dependencies.size() == 1);
    CHECK(result.manifest->dependencies.front().source == "sysroot");
}

TEST_CASE("Package manifest canonicalization is schema ordered and formatting insensitive") {
    constexpr std::string_view first = R"TOML(manifest_version = 1

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
)TOML";
    constexpr std::string_view second = R"TOML(manifest_version = 1

[dependencies]
audit-core = { version = "0.1.0", path = "packages/audit-core", source = "path" }
std = { source = "sysroot" }

[targets.workflow]
exports = ["refund_audit::main::RefundAuditWorkflow"]
entry = "refund_audit::main::RefundAuditWorkflow"
kind = "handoff"

[exports]
modules = ["main"]

[module]
root = "src"
prefix = "refund_audit"

[package]
kind = "application"
edition = "2026"
version = "0.1.0"
name = "refund-audit"
)TOML";

    const auto first_result = ahfl::manifest::parse_package_manifest(first);
    const auto second_result = ahfl::manifest::parse_package_manifest(second);
    REQUIRE_FALSE(first_result.has_errors());
    REQUIRE_FALSE(second_result.has_errors());
    REQUIRE(first_result.manifest.has_value());
    REQUIRE(second_result.manifest.has_value());

    const auto canonical = ahfl::manifest::canonicalize_package_manifest(*first_result.manifest);
    CHECK(canonical == ahfl::manifest::canonicalize_package_manifest(*second_result.manifest));
    CHECK(canonical.find("[package]\nname = \"refund-audit\"\nversion = \"0.1.0\"") !=
          std::string::npos);
    CHECK(canonical.find("[dependencies]\naudit-core = { source = \"path\"") != std::string::npos);
}

TEST_CASE("Package manifest schema accepts repository sysroot std manifest") {
    const auto path = std::filesystem::path{AHFL_SOURCE_DIR} / "std" / "ahfl.toml";
    std::ifstream input(path);
    REQUIRE(input.good());
    std::ostringstream buffer;
    buffer << input.rdbuf();

    const auto result = ahfl::manifest::parse_package_manifest(buffer.str());
    INFO("std manifest diagnostics count: " << result.diagnostics.size());
    REQUIRE_FALSE(result.has_errors());
    REQUIRE(result.manifest.has_value());
    CHECK(result.manifest->package_name == "std");
    CHECK(result.manifest->package_kind == "standard-library");
    CHECK(result.manifest->module_prefix == "std");
    CHECK(result.manifest->module_root == ".");
    REQUIRE(result.manifest->prelude_injection.has_value());
    CHECK(*result.manifest->prelude_injection == "explicit");
    CHECK(std::find(result.manifest->exported_modules.begin(),
                    result.manifest->exported_modules.end(),
                    "option") != result.manifest->exported_modules.end());
    CHECK(std::find(result.manifest->compiler_intrinsics_allow.begin(),
                    result.manifest->compiler_intrinsics_allow.end(),
                    "option_*") != result.manifest->compiler_intrinsics_allow.end());
    CHECK(result.manifest->targets.empty());
}

TEST_CASE("Package manifest schema keeps TOML syntax separate from schema diagnostics") {
    constexpr std::string_view input = R"TOML(manifest_version = "1"

[package]
name = "RefundAudit"
version = "0.1"
edition = "2025"
kind = "service"

[module]
prefix = "bad::prefix"
root = "../src"

[dependencies]
registry = { source = "registry" }
)TOML";

    const auto result = ahfl::manifest::parse_package_manifest(input);
    REQUIRE(result.has_errors());
    CHECK(has_code(result.diagnostics, "E::manifest_type"));
    CHECK(has_code(result.diagnostics, "E::manifest_invalid_value"));
    CHECK(has_code(result.diagnostics, "E::manifest_path_escape"));
}

TEST_CASE("Package manifest schema rejects unknown fields") {
    constexpr std::string_view input = R"TOML(manifest_version = 1

[package]
name = "refund-audit"
version = "0.1.0"
edition = "2026"
kind = "library"
license = "Apache-2.0"

[module]
prefix = "refund_audit"
root = "src"

[exports]
modules = ["main"]

[targets.lib]
kind = "library"
entry = "src/lib.ahfl"
)TOML";

    const auto result = ahfl::manifest::parse_package_manifest(input);
    REQUIRE(result.has_errors());
    CHECK(has_code(result.diagnostics, "E::manifest_unknown_field"));
}

TEST_CASE("Workspace manifest schema accepts members and sysroot default") {
    constexpr std::string_view input = R"TOML(manifest_version = 1

[workspace]
name = "commerce-workflows"
members = ["packages/refund-audit", "packages/risk-review"]

[resolver]
version = 1

[dependencies]
std = { source = "sysroot" }
)TOML";

    const auto result = ahfl::manifest::parse_workspace_manifest(input);
    REQUIRE_FALSE(result.has_errors());
    REQUIRE(result.manifest.has_value());
    CHECK(result.manifest->workspace_name == "commerce-workflows");
    CHECK(result.manifest->members.size() == 2);
    REQUIRE(result.manifest->dependencies.size() == 1);
    CHECK(result.manifest->dependencies.front().key == "std");
}

TEST_CASE("Workspace manifest canonicalization is schema ordered") {
    constexpr std::string_view input = R"TOML(manifest_version = 1

[dependencies]
std = { source = "sysroot" }

[resolver]
version = 1

[workspace]
members = ["packages/refund-audit", "packages/audit-core"]
name = "commerce-workflows"
)TOML";

    const auto result = ahfl::manifest::parse_workspace_manifest(input);
    REQUIRE_FALSE(result.has_errors());
    REQUIRE(result.manifest.has_value());

    CHECK(ahfl::manifest::canonicalize_workspace_manifest(*result.manifest) ==
          "manifest_version = 1\n\n"
          "[workspace]\n"
          "name = \"commerce-workflows\"\n"
          "members = [\"packages/refund-audit\", \"packages/audit-core\"]\n\n"
          "[resolver]\n"
          "version = 1\n\n"
          "[dependencies]\n"
          "std = { source = \"sysroot\" }\n");
}

TEST_CASE("Workspace manifest schema rejects path escape") {
    constexpr std::string_view input = R"TOML(manifest_version = 1

[workspace]
name = "commerce-workflows"
members = ["../outside"]

[resolver]
version = 1
)TOML";

    const auto result = ahfl::manifest::parse_workspace_manifest(input);
    REQUIRE(result.has_errors());
    CHECK(has_code(result.diagnostics, "E::manifest_path_escape"));
}
