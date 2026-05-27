#include "provider_pipeline_cache.hpp"

#include "pipeline_durable_store_import_provider_steps.hpp"

namespace ahfl::cli {

ProviderPipelineCache::ProviderPipelineCache(const ahfl::ir::Program &program,
                                             const ahfl::handoff::PackageMetadata &metadata,
                                             const ahfl::dry_run::CapabilityMockSet &mock_set,
                                             const CommandLineOptions &options,
                                             std::string_view command_name)
    : program_(program), metadata_(metadata), mock_set_(mock_set), options_(options),
      command_name_(command_name) {}

#define AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT(kind,                                      \
                                                        artifact_type,                             \
                                                        builder,                                   \
                                                        printer,                                   \
                                                        command_token,                             \
                                                        visibility)                                \
    const ahfl::durable_store_import::artifact_type *ProviderPipelineCache::get_##kind() {         \
        if (!kind##_loaded_) {                                                                     \
            kind##_loaded_ = true;                                                                 \
            kind##_ = builder(program_, metadata_, mock_set_, options_, command_name_, *this);      \
        }                                                                                          \
        return kind##_ ? &*kind##_ : nullptr;                                                      \
    }
#include "pipeline_durable_store_import_provider_artifacts.def"
#undef AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT

} // namespace ahfl::cli
