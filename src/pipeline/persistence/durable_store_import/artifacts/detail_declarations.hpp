#pragma once

#include "model_includes.hpp"

#define AHFL_DURABLE_STORE_IMPORT_ARTIFACT_PRINTER(public_name,                                    \
                                                   artifact_type,                                  \
                                                   parameter_name,                                 \
                                                   detail_namespace,                               \
                                                   detail_name,                                    \
                                                   output_format,                                  \
                                                   domain,                                         \
                                                   artifact_id)                                    \
    namespace ahfl::durable_store_import_artifacts_detail::detail_namespace {                      \
    void detail_name(const ahfl::durable_store_import::artifact_type &parameter_name,              \
                     std::ostream &out);                                                           \
    }
#include "pipeline/persistence/durable_store_import/artifact_printers.def"
#undef AHFL_DURABLE_STORE_IMPORT_ARTIFACT_PRINTER
