#pragma once

#include "cli/command_catalog.hpp"
#include "dry_run/runner.hpp"
#include "ahfl/handoff/package.hpp"
#include "ahfl/ir/ir.hpp"

#include "durable_store_import/provider.hpp"

#include <iosfwd>
#include <optional>
#include <string_view>
#include <variant>

namespace ahfl::cli {

enum class ProviderArtifactKind {
#define AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT(kind,                                     \
                                                        artifact_type,                            \
                                                        builder,                                  \
                                                        printer,                                  \
                                                        command_token)                            \
    kind,
#include "pipeline_durable_store_import_provider_artifacts.def"
#undef AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT
};

using ProviderArtifact = std::variant<
    std::monostate
#define AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT(kind,                                     \
                                                        artifact_type,                            \
                                                        builder,                                  \
                                                        printer,                                  \
                                                        command_token)                            \
    , ahfl::durable_store_import::artifact_type
#include "pipeline_durable_store_import_provider_artifacts.def"
#undef AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT
    >;

class ProviderPipeline {
  public:
    ProviderPipeline(const ahfl::ir::Program &program,
                     const ahfl::handoff::PackageMetadata &metadata,
                     const ahfl::dry_run::CapabilityMockSet &mock_set,
                     const CommandLineOptions &options,
                     std::string_view command_name);

    [[nodiscard]] std::optional<ProviderArtifact> build(ProviderArtifactKind kind) const;

  private:
    const ahfl::ir::Program &program_;
    const ahfl::handoff::PackageMetadata &metadata_;
    const ahfl::dry_run::CapabilityMockSet &mock_set_;
    const CommandLineOptions &options_;
    std::string_view command_name_;
};

[[nodiscard]] std::optional<ProviderArtifactKind>
provider_artifact_for_command(CommandKind command);
[[nodiscard]] std::string_view provider_artifact_command_token(ProviderArtifactKind kind);
[[nodiscard]] bool print_provider_artifact(ProviderArtifactKind kind,
                                           const ProviderArtifact &artifact,
                                           std::ostream &out);

[[nodiscard]] int emit_provider_artifact_with_diagnostics(
    ProviderArtifactKind kind,
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options);

} // namespace ahfl::cli
