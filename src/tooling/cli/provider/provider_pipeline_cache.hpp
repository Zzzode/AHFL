#pragma once

#include "ahfl/compiler/handoff/package.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "pipeline/execution/dry_run/runner.hpp"
#include "tooling/cli/command_catalog.hpp"

#include "tooling/cli/provider/provider_artifact_value.hpp"

#include <map>
#include <optional>
#include <string_view>
#include <variant>

namespace ahfl::cli {

struct ProviderPipelineInputs {
    const ahfl::ir::Program &program;
    const ahfl::handoff::PackageMetadata &metadata;
    const ahfl::dry_run::CapabilityMockSet &mock_set;
    const CommandLineOptions &options;
    std::string_view command_name;
};

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

    [[nodiscard]] const ProviderPipelineInputs &inputs() const;

    [[nodiscard]] const ProviderArtifact *get(ProviderArtifactKind kind);

    template <ProviderArtifactKind Kind> [[nodiscard]] const ProviderArtifactModelT<Kind> *get() {
        const auto *artifact = get(Kind);
        if (artifact == nullptr) {
            return nullptr;
        }
        return std::get_if<ProviderArtifactModelT<Kind>>(artifact);
    }

    [[nodiscard]] const ReleaseGateArtifacts *get_release_gate_artifacts();

  private:
    ProviderPipelineInputs inputs_;
    bool release_gate_artifacts_loaded_{false};
    std::optional<ReleaseGateArtifacts> release_gate_artifacts_;
    std::map<ProviderArtifactKind, std::optional<ProviderArtifact>> artifacts_;
};

} // namespace ahfl::cli
