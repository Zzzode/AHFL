#pragma once

#include "ahfl/compiler/handoff/package.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "pipeline/execution/dry_run/runner.hpp"
#include "tooling/cli/command_catalog.hpp"

#include "pipeline/persistence/durable_store_import/provider.hpp"

#include <optional>
#include <string_view>

namespace ahfl::cli {

struct ReleaseGateArtifacts {
    ahfl::durable_store_import::ProviderCompatibilityReport compatibility;
    ahfl::durable_store_import::ProviderRegistry registry;
    ahfl::durable_store_import::ProviderProductionReadinessEvidence readiness_evidence;
    ahfl::durable_store_import::ProviderConformanceReport conformance;
    ahfl::durable_store_import::ProviderSchemaCompatibilityReport schema_report;
    ahfl::durable_store_import::ProviderSelectionPlan selection_plan;
    ahfl::durable_store_import::ProviderConfigBundleValidationReport config_report;
    ahfl::durable_store_import::ReleaseEvidenceArchiveManifest release_archive;
};

/// Memoizing cache for provider pipeline artifacts. Each artifact is built at
/// most once; subsequent accesses return the cached result. Follows the same
/// `bool loaded_ + std::optional<T>` pattern as CliPipelineArtifacts.
class ProviderPipelineCache {
  public:
    ProviderPipelineCache(const ahfl::ir::Program &program,
                          const ahfl::handoff::PackageMetadata &metadata,
                          const ahfl::dry_run::CapabilityMockSet &mock_set,
                          const CommandLineOptions &options,
                          std::string_view command_name);

    [[nodiscard]] const ReleaseGateArtifacts *get_release_gate_artifacts();

    // One memoized getter per provider artifact kind.
#define AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT(kind,                                      \
                                                        artifact_type,                             \
                                                        builder,                                   \
                                                        printer,                                   \
                                                        artifact_id,                               \
                                                        visibility,                                \
                                                        order,                                     \
                                                        dep_count,                                 \
                                                        dependencies)                              \
    [[nodiscard]] const ahfl::durable_store_import::artifact_type *get_##kind();
#include "pipeline_durable_store_import_provider_artifacts.def"
#undef AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT

  private:
    const ahfl::ir::Program &program_;
    const ahfl::handoff::PackageMetadata &metadata_;
    const ahfl::dry_run::CapabilityMockSet &mock_set_;
    const CommandLineOptions &options_;
    std::string_view command_name_;
    bool release_gate_artifacts_loaded_{false};
    std::optional<ReleaseGateArtifacts> release_gate_artifacts_;

    // Memoization storage — one loaded flag + optional per artifact.
#define AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT(kind,                                      \
                                                        artifact_type,                             \
                                                        builder,                                   \
                                                        printer,                                   \
                                                        artifact_id,                               \
                                                        visibility,                                \
                                                        order,                                     \
                                                        dep_count,                                 \
                                                        dependencies)                              \
    bool kind##_loaded_{false};                                                                    \
    std::optional<ahfl::durable_store_import::artifact_type> kind##_;
#include "pipeline_durable_store_import_provider_artifacts.def"
#undef AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT
};

} // namespace ahfl::cli
