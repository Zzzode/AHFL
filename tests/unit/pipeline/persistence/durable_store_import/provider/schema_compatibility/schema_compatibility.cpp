#include "pipeline/persistence/durable_store_import/artifacts.hpp"
#include "pipeline/persistence/durable_store_import/provider/artifacts.hpp"
#include "pipeline/persistence/durable_store_import/provider/governance/schema_compatibility.hpp"

#include <cassert>
#include <cstddef>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

void assert_artifact_printer_registry() {
    const auto printers = ahfl::durable_store_import_artifact_printers();
    assert(printers.size() == 74);

    std::size_t json_count = 0;
    std::size_t text_count = 0;
    std::size_t provider_sdk_count = 0;
    bool found_adapter_execution = false;
    bool found_schema_compatibility = false;

    for (const auto &printer : printers) {
        assert(!printer.public_name.empty());
        assert(!printer.artifact_type_name.empty());
        assert(!printer.detail_namespace.empty());
        assert(!printer.detail_name.empty());
        assert(!printer.artifact_id.empty());

        if (printer.output_format == ahfl::DurableStoreImportArtifactOutputFormat::Json) {
            ++json_count;
            assert(ahfl::durable_store_import_artifact_output_format_name(printer.output_format) ==
                   "json");
        } else {
            ++text_count;
            assert(ahfl::durable_store_import_artifact_output_format_name(printer.output_format) ==
                   "text");
        }

        if (printer.domain == ahfl::DurableStoreImportArtifactDomain::ProviderSdk) {
            ++provider_sdk_count;
        }

        if (printer.public_name == "print_durable_store_import_adapter_execution_json") {
            found_adapter_execution = true;
            assert(printer.artifact_type_name == "AdapterExecutionReceipt");
            assert(printer.artifact_id == "durable-store-import-adapter-execution");
        }

        if (printer.public_name ==
            "print_durable_store_import_provider_schema_compatibility_report_json") {
            found_schema_compatibility = true;
            assert(printer.domain == ahfl::DurableStoreImportArtifactDomain::ProviderGovernance);
        }
    }

    assert(json_count > text_count);
    assert(text_count > 0);
    assert(provider_sdk_count > 0);
    assert(found_adapter_execution);
    assert(found_schema_compatibility);
    assert(ahfl::durable_store_import_artifact_domain_name(
               ahfl::DurableStoreImportArtifactDomain::ProviderSdk) == "provider_sdk");
}

void assert_provider_artifact_registry() {
    using ahfl::durable_store_import::ProviderArtifactDomain;
    using ahfl::durable_store_import::ProviderArtifactRole;

    const auto artifacts = ahfl::durable_store_import::provider_artifacts();
    assert(artifacts.size() == 70);

    for (const auto &artifact : artifacts) {
        assert(!artifact.artifact_id.empty());
        assert(!artifact.type_name.empty());
        assert(!artifact.format_version.empty());
        assert(!artifact.source_header.empty());
        assert(artifact.artifact_id.starts_with("durable-store-import-provider-"));
        assert(artifact.format_version.starts_with("ahfl.durable-store-import-provider-"));
        assert(artifact.format_version.ends_with(".v1"));
    }

    assert(ahfl::durable_store_import::provider_artifact_count(ProviderArtifactDomain::Binding) ==
           7);
    assert(ahfl::durable_store_import::provider_artifact_count(ProviderArtifactDomain::Runtime) ==
           6);
    assert(ahfl::durable_store_import::provider_artifact_count(
               ProviderArtifactDomain::HostExecution) == 11);
    assert(ahfl::durable_store_import::provider_artifact_count(ProviderArtifactDomain::Sdk) == 10);
    assert(ahfl::durable_store_import::provider_artifact_count(
               ProviderArtifactDomain::Configuration) == 7);
    assert(ahfl::durable_store_import::provider_artifact_count(
               ProviderArtifactDomain::Reliability) == 8);
    assert(ahfl::durable_store_import::provider_artifact_count(
               ProviderArtifactDomain::Governance) == 11);
    assert(ahfl::durable_store_import::provider_artifact_count(
               ProviderArtifactDomain::Production) == 10);

    const auto *runtime_preflight = ahfl::durable_store_import::find_provider_artifact_by_id(
        "durable-store-import-provider-runtime-preflight-plan");
    assert(runtime_preflight != nullptr);
    assert(runtime_preflight->type_name == "ProviderRuntimePreflightPlan");
    assert(runtime_preflight->domain == ProviderArtifactDomain::Runtime);
    assert(runtime_preflight->role == ProviderArtifactRole::Plan);
    assert(runtime_preflight->source_header == "durable_store_import/provider/runtime/runtime.hpp");

    const auto *sdk_adapter = ahfl::durable_store_import::find_provider_artifact_by_format_version(
        "ahfl.durable-store-import-provider-sdk-adapter-request-plan.v1");
    assert(sdk_adapter != nullptr);
    assert(sdk_adapter->artifact_id == "durable-store-import-provider-sdk-adapter-request-plan");
    assert(sdk_adapter->domain == ProviderArtifactDomain::Sdk);

    assert(ahfl::durable_store_import::find_provider_artifact_by_id(
               "durable-store-import-provider-missing") == nullptr);
    assert(ahfl::durable_store_import::provider_artifact_domain_name(
               ProviderArtifactDomain::Configuration) == "configuration");
    assert(ahfl::durable_store_import::provider_artifact_role_name(ProviderArtifactRole::Record) ==
           "record");
}

// Test: report should be built successfully when all versions are compatible
int test_all_compatible() {
    assert_artifact_printer_registry();
    assert_provider_artifact_registry();

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

    auto result = build_provider_schema_compatibility_report(version_checks,
                                                             source_chain_checks,
                                                             reference_checks,
                                                             "app::main::TestWorkflow",
                                                             "session-001",
                                                             std::nullopt);

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

    // Verify the JSON output runs normally
    std::ostringstream oss;
    ahfl::print_durable_store_import_provider_schema_compatibility_report_json(*result.report, oss);
    assert(!oss.str().empty());

    std::cout << "PASS: test_all_compatible\n";
    return 0;
}

// Test: schema drift should be detected when an incompatible version exists
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

    auto result = build_provider_schema_compatibility_report(version_checks,
                                                             source_chain_checks,
                                                             reference_checks,
                                                             "app::main::TestWorkflow",
                                                             "session-002",
                                                             std::nullopt);

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

// Test: schema drift should be detected when the source chain is incompatible
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

    auto result = build_provider_schema_compatibility_report(version_checks,
                                                             source_chain_checks,
                                                             reference_checks,
                                                             "app::main::TestWorkflow",
                                                             "session-003",
                                                             std::nullopt);

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

// Test: empty workflow or session should return an error
int test_empty_identity_fields() {
    using namespace ahfl::durable_store_import;

    std::vector<ArtifactVersionCheck> version_checks;
    std::vector<SourceChainCheck> source_chain_checks;
    std::vector<ReferenceVersionCheck> reference_checks;

    auto result = build_provider_schema_compatibility_report(
        version_checks, source_chain_checks, reference_checks, "", "session-004", std::nullopt);

    assert(result.has_errors());

    std::cout << "PASS: test_empty_identity_fields\n";
    return 0;
}

// Test: should return an error when the report's format version is incorrect
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
    // Allow selecting a single test case via command-line argument (CTest integration)
    if (argc == 2) {
        std::string cmd = argv[1];
        if (cmd == "test-all-compatible")
            return test_all_compatible();
        if (cmd == "test-incompatible-version")
            return test_incompatible_version();
        if (cmd == "test-source-chain-incompatible")
            return test_source_chain_incompatible();
        if (cmd == "test-empty-identity-fields")
            return test_empty_identity_fields();
        if (cmd == "test-validation-wrong-format-version")
            return test_validation_wrong_format_version();
        std::cerr << "unknown test: " << cmd << "\n";
        return 1;
    }

    // Run all tests when no argument is provided
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
