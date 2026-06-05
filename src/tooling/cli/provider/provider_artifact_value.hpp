#pragma once

#include "pipeline/persistence/durable_store_import/provider.hpp"
#include "tooling/cli/provider/provider_artifact_catalog.hpp"

#include <variant>

namespace ahfl::cli {

using ProviderArtifact = std::variant<std::monostate
#define AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT(kind,                                      \
                                                        artifact_type,                             \
                                                        builder,                                   \
                                                        printer,                                   \
                                                        artifact_id,                               \
                                                        visibility,                                \
                                                        order,                                     \
                                                        dep_count,                                 \
                                                        dependencies)                              \
    , ahfl::durable_store_import::artifact_type
#include "tooling/cli/provider/pipeline_durable_store_import_provider_artifacts.def"
#undef AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT
                                      >;

template <ProviderArtifactKind Kind> struct ProviderArtifactModel;

#define AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT(kind,                                      \
                                                        artifact_type,                             \
                                                        builder,                                   \
                                                        printer,                                   \
                                                        artifact_id,                               \
                                                        visibility,                                \
                                                        order,                                     \
                                                        dep_count,                                 \
                                                        dependencies)                              \
    template <> struct ProviderArtifactModel<ProviderArtifactKind::kind> {                         \
        using type = ahfl::durable_store_import::artifact_type;                                    \
    };
#include "tooling/cli/provider/pipeline_durable_store_import_provider_artifacts.def"
#undef AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT

template <ProviderArtifactKind Kind>
using ProviderArtifactModelT = typename ProviderArtifactModel<Kind>::type;

} // namespace ahfl::cli
