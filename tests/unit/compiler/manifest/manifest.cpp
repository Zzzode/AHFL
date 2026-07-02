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

[[nodiscard]] bool has_message(const std::vector<ahfl::manifest::ManifestDiagnostic> &diagnostics,
                               std::string_view needle) {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [needle](const auto &diag) {
        return diag.message.find(needle) != std::string::npos;
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
exports = [
  { kind = "workflow", name = "refund_audit::main::RefundAuditWorkflow" },
  { kind = "agent", name = "refund_audit::agents::RefundAudit" },
]

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
    REQUIRE(result.manifest->targets[1].exports.size() == 2);
    CHECK(result.manifest->targets[1].exports[0].kind == "workflow");
    CHECK(result.manifest->targets[1].exports[0].name == "refund_audit::main::RefundAuditWorkflow");
    CHECK(result.manifest->targets[1].exports[1].kind == "agent");
    CHECK(result.manifest->targets[1].exports[1].name == "refund_audit::agents::RefundAudit");
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
exports = [{ kind = "workflow", name = "refund_audit::main::RefundAuditWorkflow" }]

[dependencies]
std = { source = "sysroot" }
audit-core = { source = "path", path = "packages/audit-core", version = "0.1.0" }
)TOML";
    constexpr std::string_view second = R"TOML(manifest_version = 1

[dependencies]
audit-core = { version = "0.1.0", path = "packages/audit-core", source = "path" }
std = { source = "sysroot" }

[targets.workflow]
exports = [{ kind = "workflow", name = "refund_audit::main::RefundAuditWorkflow" }]
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
    CHECK(canonical.find("exports = [{ kind = \"workflow\", name = "
                         "\"refund_audit::main::RefundAuditWorkflow\" }]") != std::string::npos);
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

TEST_CASE("Package manifest schema accepts snake case target names") {
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
modules = ["main"]

[targets.agent_entry]
kind = "test"
entry = "src/agent_entry_test.ahfl"

[dependencies]
std = { source = "sysroot" }
)TOML";

    const auto result = ahfl::manifest::parse_package_manifest(input);
    REQUIRE_FALSE(result.has_errors());
    REQUIRE(result.manifest.has_value());
    REQUIRE(result.manifest->targets.size() == 1);
    CHECK(result.manifest->targets.front().name == "agent_entry");
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

TEST_CASE("Package manifest schema rejects invalid target names") {
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
modules = ["main"]

[targets."BadName"]
kind = "library"
entry = "src/lib.ahfl"

[targets."bad-name_case"]
kind = "test"
entry = "src/bad.ahfl"

[dependencies]
std = { source = "sysroot" }
)TOML";

    const auto result = ahfl::manifest::parse_package_manifest(input);
    REQUIRE(result.has_errors());
    CHECK(has_code(result.diagnostics, "E::manifest_invalid_value"));
    CHECK(has_message(result.diagnostics, "target name must be kebab-case or snake_case"));
}

TEST_CASE("Package manifest schema rejects invalid dependency keys") {
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
modules = ["main"]

[targets.workflow]
kind = "handoff"
entry = "refund_audit::main::RefundAuditWorkflow"
exports = [{ kind = "workflow", name = "refund_audit::main::RefundAuditWorkflow" }]

[dependencies]
std = { source = "sysroot" }
audit_core = { source = "path", path = "packages/audit-core", version = "0.1.0" }
)TOML";

    const auto result = ahfl::manifest::parse_package_manifest(input);
    REQUIRE(result.has_errors());
    CHECK(has_code(result.diagnostics, "E::manifest_invalid_value"));
    CHECK(has_message(result.diagnostics, "dependency key must be kebab-case"));
}

TEST_CASE("Package manifest schema rejects absolute path dependencies") {
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
modules = ["main"]

[targets.workflow]
kind = "handoff"
entry = "refund_audit::main::RefundAuditWorkflow"
exports = [{ kind = "workflow", name = "refund_audit::main::RefundAuditWorkflow" }]

[dependencies]
std = { source = "sysroot" }
audit-core = { source = "path", path = "/tmp/audit-core", version = "0.1.0" }
)TOML";

    const auto result = ahfl::manifest::parse_package_manifest(input);
    REQUIRE(result.has_errors());
    CHECK(has_code(result.diagnostics, "E::manifest_path_escape"));
    CHECK(has_message(result.diagnostics,
                      "manifest field 'dependencies.path' must be relative to manifest directory"));
}

TEST_CASE("Package manifest schema allows relative parent path dependencies") {
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
modules = ["main"]

[targets.workflow]
kind = "handoff"
entry = "refund_audit::main::RefundAuditWorkflow"
exports = [{ kind = "workflow", name = "refund_audit::main::RefundAuditWorkflow" }]

[dependencies]
std = { source = "sysroot" }
audit-core = { source = "path", path = "../audit-core", version = "0.1.0" }
)TOML";

    const auto result = ahfl::manifest::parse_package_manifest(input);
    REQUIRE_FALSE(result.has_errors());
    REQUIRE(result.manifest.has_value());
    REQUIRE(result.manifest->dependencies.size() == 2);
    CHECK(result.manifest->dependencies[1].path == "../audit-core");
}

TEST_CASE("Package manifest schema accepts normalized non-escaping paths") {
    constexpr std::string_view input = R"TOML(manifest_version = 1

[package]
name = "refund-audit"
version = "0.1.0"
edition = "2026"
kind = "library"

[module]
prefix = "refund_audit"
root = "src/../src"

[exports]
modules = ["main"]

[targets.lib]
kind = "library"
entry = "src/../src/lib.ahfl"
)TOML";

    const auto result = ahfl::manifest::parse_package_manifest(input);
    REQUIRE_FALSE(result.has_errors());
    REQUIRE(result.manifest.has_value());
    CHECK(result.manifest->module_root == "src/../src");
    REQUIRE(result.manifest->targets.size() == 1);
    CHECK(result.manifest->targets.front().entry == "src/../src/lib.ahfl");
}

TEST_CASE("Package manifest schema rejects string handoff exports") {
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
modules = ["main"]

[targets.workflow]
kind = "handoff"
entry = "refund_audit::main::RefundAuditWorkflow"
exports = ["refund_audit::main::RefundAuditWorkflow"]

[dependencies]
std = { source = "sysroot" }
)TOML";

    const auto result = ahfl::manifest::parse_package_manifest(input);
    REQUIRE(result.has_errors());
    CHECK(has_code(result.diagnostics, "E::manifest_type"));
    CHECK(has_message(result.diagnostics, "manifest field 'targets.exports' items must be tables"));
}

TEST_CASE("Package manifest schema rejects non-canonical handoff export names") {
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
modules = ["main"]

[targets.workflow]
kind = "handoff"
entry = "refund_audit::main::RefundAuditWorkflow"
exports = [{ kind = "workflow", name = "src/workflow.ahfl" }]

[dependencies]
std = { source = "sysroot" }
)TOML";

    const auto result = ahfl::manifest::parse_package_manifest(input);
    REQUIRE(result.has_errors());
    CHECK(has_code(result.diagnostics, "E::manifest_invalid_value"));
    CHECK(has_message(
        result.diagnostics,
        "targets.exports.name must be a canonical AHFL symbol name for handoff targets"));
}

TEST_CASE("Package manifest schema rejects empty handoff exports") {
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
modules = ["main"]

[targets.workflow]
kind = "handoff"
entry = "refund_audit::main::RefundAuditWorkflow"
exports = []

[dependencies]
std = { source = "sysroot" }
)TOML";

    const auto result = ahfl::manifest::parse_package_manifest(input);
    REQUIRE(result.has_errors());
    CHECK(has_code(result.diagnostics, "E::manifest_invalid_value"));
    CHECK(has_message(result.diagnostics, "manifest field 'targets.exports' must not be empty"));
}

TEST_CASE("Package manifest schema rejects exports on non-handoff targets") {
    constexpr std::string_view input = R"TOML(manifest_version = 1

[package]
name = "refund-audit"
version = "0.1.0"
edition = "2026"
kind = "library"

[module]
prefix = "refund_audit"
root = "src"

[exports]
modules = ["main"]

[targets.lib]
kind = "library"
entry = "src/lib.ahfl"
exports = [{ kind = "workflow", name = "refund_audit::main::RefundAuditWorkflow" }]

[dependencies]
std = { source = "sysroot" }
)TOML";

    const auto result = ahfl::manifest::parse_package_manifest(input);
    REQUIRE(result.has_errors());
    CHECK(has_code(result.diagnostics, "E::manifest_invalid_value"));
    CHECK(has_message(result.diagnostics, "targets.exports are only allowed on handoff targets"));
}

TEST_CASE("Package manifest schema rejects sysroot dependency payload fields") {
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
modules = ["main"]

[targets.workflow]
kind = "handoff"
entry = "refund_audit::main::RefundAuditWorkflow"
exports = [{ kind = "workflow", name = "refund_audit::main::RefundAuditWorkflow" }]

[dependencies]
std = { source = "sysroot", path = "../std", version = "0.1.0" }
)TOML";

    const auto result = ahfl::manifest::parse_package_manifest(input);
    REQUIRE(result.has_errors());
    CHECK(has_code(result.diagnostics, "E::manifest_invalid_value"));
    CHECK(has_message(result.diagnostics, "sysroot dependency must not declare path or version"));
}

TEST_CASE("Package manifest schema validates target entry shapes") {
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
modules = ["main"]

[targets.bad-lib]
kind = "library"
entry = "../lib.ahfl"

[targets.bad-test]
kind = "test"
entry = "/tmp/test.ahfl"

[targets.bad-workflow]
kind = "handoff"
entry = "src/workflow.ahfl"
exports = [{ kind = "workflow", name = "refund_audit::main::RefundAuditWorkflow" }]

[dependencies]
std = { source = "sysroot" }
)TOML";

    const auto result = ahfl::manifest::parse_package_manifest(input);
    REQUIRE(result.has_errors());
    CHECK(has_code(result.diagnostics, "E::manifest_path_escape"));
    CHECK(has_code(result.diagnostics, "E::manifest_invalid_value"));
    CHECK(has_message(result.diagnostics,
                      "manifest field 'targets.entry' must not escape package root"));
    CHECK(has_message(result.diagnostics,
                      "targets.entry must be a canonical AHFL symbol name for handoff targets"));
}

TEST_CASE("Package manifest schema rejects normalized escaping paths") {
    constexpr std::string_view input = R"TOML(manifest_version = 1

[package]
name = "refund-audit"
version = "0.1.0"
edition = "2026"
kind = "library"

[module]
prefix = "refund_audit"
root = "src/../../outside"

[exports]
modules = ["main"]

[targets.lib]
kind = "library"
entry = "src/../../lib.ahfl"
)TOML";

    const auto result = ahfl::manifest::parse_package_manifest(input);
    REQUIRE(result.has_errors());
    CHECK(has_code(result.diagnostics, "E::manifest_path_escape"));
    CHECK(has_message(result.diagnostics,
                      "manifest field 'module.root' must not escape package root"));
    CHECK(has_message(result.diagnostics,
                      "manifest field 'targets.entry' must not escape package root"));
}

TEST_CASE("Package manifest schema rejects prelude on non-standard-library packages") {
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
modules = ["main"]

[prelude]
module = "std::prelude"
injection = "explicit"

[targets.workflow]
kind = "handoff"
entry = "refund_audit::main::RefundAuditWorkflow"
exports = [{ kind = "workflow", name = "refund_audit::main::RefundAuditWorkflow" }]

[dependencies]
std = { source = "sysroot" }
)TOML";

    const auto result = ahfl::manifest::parse_package_manifest(input);
    REQUIRE(result.has_errors());
    CHECK(has_code(result.diagnostics, "E::manifest_invalid_value"));
    CHECK(has_message(result.diagnostics, "prelude is only allowed for standard-library packages"));
}

TEST_CASE("Package manifest schema rejects invalid compiler intrinsic allow patterns") {
    constexpr std::string_view input = R"TOML(manifest_version = 1

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
allow = ["option_*", "*", "string-raw", "time_**", "Bad"]
)TOML";

    const auto result = ahfl::manifest::parse_package_manifest(input);
    REQUIRE(result.has_errors());
    CHECK(has_code(result.diagnostics, "E::manifest_invalid_value"));
    CHECK(has_message(result.diagnostics,
                      "manifest field 'compiler_intrinsics.allow' items must be builtin hook "
                      "names or trailing-* prefix patterns"));
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

TEST_CASE("Package manifest schema rejects empty string fields") {
    constexpr std::string_view input = R"TOML(manifest_version = 1

[package]
name = ""
version = "0.1.0"
edition = "2026"
kind = "library"

[module]
prefix = "refund_audit"
root = "src"

[exports]
modules = ["main", ""]

[targets.""]
kind = "library"
entry = ""

[dependencies]
audit-core = { source = "path", path = "", version = "" }
)TOML";

    const auto result = ahfl::manifest::parse_package_manifest(input);
    REQUIRE(result.has_errors());
    CHECK(has_code(result.diagnostics, "E::manifest_invalid_value"));
    CHECK(has_message(result.diagnostics, "manifest field 'package.name' must not be empty"));
    CHECK(has_message(result.diagnostics,
                      "manifest field 'exports.modules' must not contain empty strings"));
    CHECK(has_message(result.diagnostics, "target name must not be empty"));
    CHECK(has_message(result.diagnostics, "manifest field 'targets.entry' must not be empty"));
    CHECK(has_message(result.diagnostics, "manifest field 'dependencies.path' must not be empty"));
    CHECK(
        has_message(result.diagnostics, "manifest field 'dependencies.version' must not be empty"));
}

TEST_CASE("Manifest path validation treats parent traversal as a path component") {
    constexpr std::string_view input = R"TOML(manifest_version = 1

[package]
name = "refund-audit"
version = "0.1.0"
edition = "2026"
kind = "library"

[module]
prefix = "refund_audit"
root = "src/a..b"

[exports]
modules = ["main"]

[targets.lib]
kind = "library"
entry = "src/a..b/lib.ahfl"
)TOML";

    const auto result = ahfl::manifest::parse_package_manifest(input);
    REQUIRE_FALSE(result.has_errors());
    REQUIRE(result.manifest.has_value());
    CHECK(result.manifest->module_root == "src/a..b");
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
    const auto found = std::find_if(result.diagnostics.begin(),
                                    result.diagnostics.end(),
                                    [](const auto &diagnostic) {
                                        return diagnostic.code == "E::manifest_path_escape";
    });
    REQUIRE(found != result.diagnostics.end());
    constexpr std::string_view invalid_member = "\"../outside\"";
    const auto invalid_member_offset = input.find(invalid_member);
    CHECK(found->range.begin_offset == invalid_member_offset);
    CHECK(found->range.end_offset == invalid_member_offset + invalid_member.size());
}

TEST_CASE("Workspace manifest schema accepts normalized non-escaping members") {
    constexpr std::string_view input = R"TOML(manifest_version = 1

[workspace]
name = "commerce-workflows"
members = ["packages/../refund-audit"]

[resolver]
version = 1
)TOML";

    const auto result = ahfl::manifest::parse_workspace_manifest(input);
    REQUIRE_FALSE(result.has_errors());
    REQUIRE(result.manifest.has_value());
    REQUIRE(result.manifest->members.size() == 1);
    CHECK(result.manifest->members.front() == "packages/../refund-audit");
}

TEST_CASE("Workspace manifest schema rejects normalized escaping members") {
    constexpr std::string_view input = R"TOML(manifest_version = 1

[workspace]
name = "commerce-workflows"
members = ["packages/../../outside"]

[resolver]
version = 1
)TOML";

    const auto result = ahfl::manifest::parse_workspace_manifest(input);
    REQUIRE(result.has_errors());
    const auto found = std::find_if(result.diagnostics.begin(),
                                    result.diagnostics.end(),
                                    [](const auto &diagnostic) {
                                        return diagnostic.code == "E::manifest_path_escape";
    });
    REQUIRE(found != result.diagnostics.end());
    constexpr std::string_view invalid_member = "\"packages/../../outside\"";
    const auto invalid_member_offset = input.find(invalid_member);
    CHECK(found->range.begin_offset == invalid_member_offset);
    CHECK(found->range.end_offset == invalid_member_offset + invalid_member.size());
}

TEST_CASE("Workspace manifest schema rejects duplicate member paths") {
    constexpr std::string_view input = R"TOML(manifest_version = 1

[workspace]
name = "commerce-workflows"
members = ["packages/refund-audit", "packages/refund-audit"]

[resolver]
version = 1
)TOML";

    const auto result = ahfl::manifest::parse_workspace_manifest(input);
    REQUIRE(result.has_errors());
    const auto found = std::find_if(result.diagnostics.begin(),
                                    result.diagnostics.end(),
                                    [](const auto &diagnostic) {
                                        return diagnostic.message.find("duplicate member paths") !=
                                               std::string::npos;
    });
    REQUIRE(found != result.diagnostics.end());
    constexpr std::string_view duplicate_member = "\"packages/refund-audit\"";
    const auto duplicate_member_offset =
        input.find(duplicate_member, input.find(duplicate_member) + duplicate_member.size());
    CHECK(found->range.begin_offset == duplicate_member_offset);
    CHECK(found->range.end_offset == duplicate_member_offset + duplicate_member.size());
}

TEST_CASE("Workspace manifest schema rejects normalized duplicate member paths") {
    constexpr std::string_view input = R"TOML(manifest_version = 1

[workspace]
name = "commerce-workflows"
members = ["packages/refund-audit", "packages/../packages/refund-audit"]

[resolver]
version = 1
)TOML";

    const auto result = ahfl::manifest::parse_workspace_manifest(input);
    REQUIRE(result.has_errors());
    CHECK(has_code(result.diagnostics, "E::manifest_invalid_value"));
    CHECK(has_message(result.diagnostics,
                      "workspace.members must not contain duplicate member paths"));
}

TEST_CASE("Workspace manifest schema rejects empty string fields") {
    constexpr std::string_view input = R"TOML(manifest_version = 1

[workspace]
name = ""
members = ["packages/refund-audit", ""]

[resolver]
version = 1
)TOML";

    const auto result = ahfl::manifest::parse_workspace_manifest(input);
    REQUIRE(result.has_errors());
    CHECK(has_code(result.diagnostics, "E::manifest_invalid_value"));
    CHECK(has_message(result.diagnostics, "manifest field 'workspace.name' must not be empty"));
    CHECK(has_message(result.diagnostics,
                      "manifest field 'workspace.members' must not contain empty strings"));
}

TEST_CASE("Workspace manifest schema allows dots inside member path names") {
    constexpr std::string_view input = R"TOML(manifest_version = 1

[workspace]
name = "commerce-workflows"
members = ["packages/a..b"]

[resolver]
version = 1
)TOML";

    const auto result = ahfl::manifest::parse_workspace_manifest(input);
    REQUIRE_FALSE(result.has_errors());
    REQUIRE(result.manifest.has_value());
    REQUIRE(result.manifest->members.size() == 1);
    CHECK(result.manifest->members.front() == "packages/a..b");
}
