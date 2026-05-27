#pragma once

#include "cli/command_catalog.hpp"
#include "dry_run/runner.hpp"
#include "ahfl/handoff/package.hpp"
#include "ahfl/ir/ir.hpp"

#include "durable_store_import/provider.hpp"

#include <optional>
#include <string_view>

namespace ahfl::cli {

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

    // One memoized getter per provider artifact kind.
#define AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT(kind,                                      \
                                                        artifact_type,                             \
                                                        builder,                                   \
                                                        printer,                                   \
                                                        command_token,                             \
                                                        visibility)                                \
    [[nodiscard]] const ahfl::durable_store_import::artifact_type *get_##kind();
#include "pipeline_durable_store_import_provider_artifacts.def"
#undef AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT

  private:
    const ahfl::ir::Program &program_;
    const ahfl::handoff::PackageMetadata &metadata_;
    const ahfl::dry_run::CapabilityMockSet &mock_set_;
    const CommandLineOptions &options_;
    std::string_view command_name_;

    // Memoization storage — one loaded flag + optional per artifact.
#define AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT(kind,                                      \
                                                        artifact_type,                             \
                                                        builder,                                   \
                                                        printer,                                   \
                                                        command_token,                             \
                                                        visibility)                                \
    bool kind##_loaded_{false};                                                                    \
    std::optional<ahfl::durable_store_import::artifact_type> kind##_;
#include "pipeline_durable_store_import_provider_artifacts.def"
#undef AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT
};

} // namespace ahfl::cli
