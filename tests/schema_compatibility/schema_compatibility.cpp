#include "ahfl/durable_store_import/provider_schema_compatibility.hpp"
#include "ahfl/backends/durable_store_import_provider_schema_compatibility_report.hpp"

#include <cassert>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

// 测试：所有版本兼容时应成功构建报告
int test_all_compatible() {
    using namespace ahfl::durable_store_import;

    std::vector<ArtifactVersionCheck> version_checks = {
        {
            "provider-production-readiness-evidence",
            "evidence-id-001",
            std::string(kProviderSchemaCompatibilityReportFormatVersion),
            SchemaCompatibilityStatus::Compatible,
            std::string(kProviderSchemaCompatibilityReportFormatVersion),
            std::nullopt,
        },
    };

    std::vector<SourceChainCheck> source_chain_checks = {
        {
            "provider-compatibility-report",
            "compatibility-id-001",
            "ahfl.durable-store-import-provider-compatibility-report.v1",
            "ahfl.durable-store-import-provider-compatibility-report.v1",
            true,
            std::nullopt,
        },
    };

    std::vector<ReferenceVersionCheck> reference_checks;

    auto result = build_provider_schema_compatibility_report(
        version_checks, source_chain_checks, reference_checks,
        "app::main::TestWorkflow", "session-001", std::nullopt);

    if (result.has_errors()) {
        result.diagnostics.render(std::cerr);
        return 1;
    }
    assert(result.report.has_value());
    assert(result.report->compatible_count == 1);
    assert(result.report->incompatible_count == 0);
    assert(result.report->unknown_count == 0);
    assert(!result.report->has_schema_drift);
    assert(!result.report->drift_details.has_value());

    // 验证 JSON 输出可以正常执行
    std::ostringstream oss;
    ahfl::print_durable_store_import_provider_schema_compatibility_report_json(
        *result.report, oss);
    assert(!oss.str().empty());

    std::cout << "PASS: test_all_compatible\n";
    return 0;
}

// 测试：存在不兼容版本时应检测到 schema drift
int test_incompatible_version() {
    using namespace ahfl::durable_store_import;

    std::vector<ArtifactVersionCheck> version_checks = {
        {
            "provider-production-readiness-evidence",
            "evidence-id-001",
            "ahfl.durable-store-import-provider-production-readiness-evidence.v0",
            SchemaCompatibilityStatus::Incompatible,
            std::string(kProviderSchemaCompatibilityReportFormatVersion),
            "version mismatch: expected v1, got v0",
        },
    };

    std::vector<SourceChainCheck> source_chain_checks;
    std::vector<ReferenceVersionCheck> reference_checks;

    auto result = build_provider_schema_compatibility_report(
        version_checks, source_chain_checks, reference_checks,
        "app::main::TestWorkflow", "session-002", std::nullopt);

    if (result.has_errors()) {
        result.diagnostics.render(std::cerr);
        return 1;
    }
    assert(result.report.has_value());
    assert(result.report->compatible_count == 0);
    assert(result.report->incompatible_count == 1);
    assert(result.report->unknown_count == 0);
    assert(result.report->has_schema_drift);
    assert(result.report->drift_details.has_value());

    std::cout << "PASS: test_incompatible_version\n";
    return 0;
}

// 测试：source chain 不兼容时应检测到 schema drift
int test_source_chain_incompatible() {
    using namespace ahfl::durable_store_import;

    std::vector<ArtifactVersionCheck> version_checks = {
        {
            "provider-production-readiness-evidence",
            "evidence-id-001",
            "some-version",
            SchemaCompatibilityStatus::Compatible,
            "some-version",
            std::nullopt,
        },
    };

    std::vector<SourceChainCheck> source_chain_checks = {
        {
            "provider-compatibility-report",
            "compatibility-id-001",
            "ahfl.durable-store-import-provider-compatibility-report.v0",
            "ahfl.durable-store-import-provider-compatibility-report.v1",
            false,
            "source chain version mismatch",
        },
    };

    std::vector<ReferenceVersionCheck> reference_checks;

    auto result = build_provider_schema_compatibility_report(
        version_checks, source_chain_checks, reference_checks,
        "app::main::TestWorkflow", "session-003", std::nullopt);

    if (result.has_errors()) {
        result.diagnostics.render(std::cerr);
        return 1;
    }
    assert(result.report.has_value());
    assert(result.report->has_schema_drift);
    assert(result.report->drift_details.has_value());
    assert(result.report->drift_details->find("source chain") != std::string::npos);

    std::cout << "PASS: test_source_chain_incompatible\n";
    return 0;
}

// 测试：空的 workflow 或 session 应返回错误
int test_empty_identity_fields() {
    using namespace ahfl::durable_store_import;

    std::vector<ArtifactVersionCheck> version_checks;
    std::vector<SourceChainCheck> source_chain_checks;
    std::vector<ReferenceVersionCheck> reference_checks;

    auto result = build_provider_schema_compatibility_report(
        version_checks, source_chain_checks, reference_checks,
        "", "session-004", std::nullopt);

    assert(result.has_errors());

    std::cout << "PASS: test_empty_identity_fields\n";
    return 0;
}

// 测试：验证报告 format version 不正确时应返回错误
int test_validation_wrong_format_version() {
    using namespace ahfl::durable_store_import;

    ProviderSchemaCompatibilityReport report;
    report.format_version = "wrong-version";
    report.workflow_canonical_name = "app::main::TestWorkflow";
    report.session_id = "session-005";
    report.compatibility_summary = "test summary";

    auto validation_result = validate_provider_schema_compatibility_report(report);
    assert(validation_result.has_errors());

    std::cout << "PASS: test_validation_wrong_format_version\n";
    return 0;
}

} // namespace

int main(int argc, char *argv[]) {
    // 支持通过命令行参数选择单个测试用例（CTest 集成）
    if (argc == 2) {
        std::string cmd = argv[1];
        if (cmd == "test-all-compatible") return test_all_compatible();
        if (cmd == "test-incompatible-version") return test_incompatible_version();
        if (cmd == "test-source-chain-incompatible") return test_source_chain_incompatible();
        if (cmd == "test-empty-identity-fields") return test_empty_identity_fields();
        if (cmd == "test-validation-wrong-format-version") return test_validation_wrong_format_version();
        std::cerr << "unknown test: " << cmd << "\n";
        return 1;
    }

    // 无参数时运行全部测试
    int failures = 0;
    failures += test_all_compatible();
    failures += test_incompatible_version();
    failures += test_source_chain_incompatible();
    failures += test_empty_identity_fields();
    failures += test_validation_wrong_format_version();

    if (failures == 0) {
        std::cout << "\nAll schema_compatibility tests passed.\n";
    }
    return failures;
}
