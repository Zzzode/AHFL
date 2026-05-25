#include "durable_store_import/artifacts.hpp"

#include "artifacts/detail_declarations.hpp"

namespace ahfl {

namespace {

constexpr DurableStoreImportArtifactPrinterDescriptor kDurableStoreImportArtifactPrinters[] = {
#define AHFL_DURABLE_STORE_IMPORT_ARTIFACT_PRINTER(public_name,                                    \
                                                   artifact_type,                                  \
                                                   parameter_name,                                 \
                                                   detail_namespace,                               \
                                                   detail_name,                                    \
                                                   output_format,                                  \
                                                   domain,                                         \
                                                   artifact_id)                                    \
    {#public_name,                                                                                 \
     #artifact_type,                                                                               \
     #detail_namespace,                                                                            \
     #detail_name,                                                                                 \
     artifact_id,                                                                                  \
     output_format,                                                                                \
     domain},
#include "durable_store_import/artifact_printers.def"
#undef AHFL_DURABLE_STORE_IMPORT_ARTIFACT_PRINTER
};

} // namespace

std::string_view
durable_store_import_artifact_output_format_name(DurableStoreImportArtifactOutputFormat format) {
    switch (format) {
    case DurableStoreImportArtifactOutputFormat::Json:
        return "json";
    case DurableStoreImportArtifactOutputFormat::Text:
        return "text";
    }
    return "unknown";
}

std::string_view
durable_store_import_artifact_domain_name(DurableStoreImportArtifactDomain domain) {
    switch (domain) {
    case DurableStoreImportArtifactDomain::CoreWorkflow:
        return "core_workflow";
    case DurableStoreImportArtifactDomain::Persistence:
        return "persistence";
    case DurableStoreImportArtifactDomain::ProviderConfiguration:
        return "provider_configuration";
    case DurableStoreImportArtifactDomain::ProviderGovernance:
        return "provider_governance";
    case DurableStoreImportArtifactDomain::ProviderHostExecution:
        return "provider_host_execution";
    case DurableStoreImportArtifactDomain::ProviderReliability:
        return "provider_reliability";
    case DurableStoreImportArtifactDomain::ProviderRuntime:
        return "provider_runtime";
    case DurableStoreImportArtifactDomain::ProviderSdk:
        return "provider_sdk";
    }
    return "unknown";
}

std::span<const DurableStoreImportArtifactPrinterDescriptor>
durable_store_import_artifact_printers() {
    return std::span<const DurableStoreImportArtifactPrinterDescriptor>{
        kDurableStoreImportArtifactPrinters};
}

const DurableStoreImportArtifactPrinterDescriptor *
find_durable_store_import_artifact_printer(std::string_view artifact_id) {
    for (const auto &printer : durable_store_import_artifact_printers()) {
        if (printer.artifact_id == artifact_id) {
            return &printer;
        }
    }
    return nullptr;
}

#define AHFL_DURABLE_STORE_IMPORT_ARTIFACT_PRINTER(public_name,                                    \
                                                   artifact_type,                                  \
                                                   parameter_name,                                 \
                                                   detail_namespace,                               \
                                                   detail_name,                                    \
                                                   output_format,                                  \
                                                   domain,                                         \
                                                   artifact_id)                                    \
    void public_name(const durable_store_import::artifact_type &parameter_name,                    \
                     std::ostream &out) {                                                          \
        durable_store_import_artifacts_detail::detail_namespace::detail_name(parameter_name, out); \
    }
#include "durable_store_import/artifact_printers.def"
#undef AHFL_DURABLE_STORE_IMPORT_ARTIFACT_PRINTER

} // namespace ahfl
