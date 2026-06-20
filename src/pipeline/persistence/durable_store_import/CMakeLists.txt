set(AHFL_DURABLE_STORE_IMPORT_CORE_SOURCES
    adapter_execution.cpp
    decision.cpp
    decision_review.cpp
    receipt.cpp
    receipt_persistence.cpp
    receipt_persistence_response.cpp
    receipt_persistence_response_review.cpp
    receipt_persistence_stage.cpp
    receipt_persistence_review.cpp
    recovery_preview.cpp
    receipt_review.cpp
    request.cpp
    review.cpp
)

set(AHFL_DURABLE_STORE_IMPORT_PROVIDER_SOURCES
    provider/binding.cpp
    provider/artifacts.cpp
    provider/runtime.cpp
    provider/execution.cpp
    provider/sdk.cpp
    provider/configuration.cpp
    provider/reliability.cpp
    provider/governance.cpp
    provider/production.cpp
)

set(AHFL_DURABLE_STORE_IMPORT_ARTIFACT_SOURCES
    artifacts.cpp
    artifacts/core_workflow.cpp
    artifacts/persistence.cpp
    artifacts/provider_configuration.cpp
    artifacts/provider_governance.cpp
    artifacts/provider_host_execution.cpp
    artifacts/provider_reliability.cpp
    artifacts/provider_runtime.cpp
    artifacts/provider_sdk.cpp
)

# --- Core: decision/receipt/request types ---
add_library(ahfl_pipeline_durable_store_import_core STATIC
    ${AHFL_DURABLE_STORE_IMPORT_CORE_SOURCES}
)
target_link_libraries(ahfl_pipeline_durable_store_import_core PUBLIC ahfl_pipeline_persistence)
target_include_directories(ahfl_pipeline_durable_store_import_core PRIVATE ${PROJECT_SOURCE_DIR}/src)
ahfl_apply_project_warnings(ahfl_pipeline_durable_store_import_core)

# --- Providers: binding/runtime/execution/sdk/config/reliability/governance/production ---
add_library(ahfl_pipeline_durable_store_import_providers STATIC
    ${AHFL_DURABLE_STORE_IMPORT_PROVIDER_SOURCES}
)
target_link_libraries(ahfl_pipeline_durable_store_import_providers PUBLIC ahfl_pipeline_durable_store_import_core)
target_include_directories(ahfl_pipeline_durable_store_import_providers PRIVATE ${PROJECT_SOURCE_DIR}/src)
ahfl_apply_project_warnings(ahfl_pipeline_durable_store_import_providers)

# --- Artifacts: emission/serialization of all artifact types ---
add_library(ahfl_pipeline_durable_store_import_artifacts STATIC
    ${AHFL_DURABLE_STORE_IMPORT_ARTIFACT_SOURCES}
)
target_link_libraries(ahfl_pipeline_durable_store_import_artifacts
    PUBLIC
        ahfl_pipeline_durable_store_import_providers
)
target_include_directories(ahfl_pipeline_durable_store_import_artifacts PRIVATE ${PROJECT_SOURCE_DIR}/src)
ahfl_apply_project_warnings(ahfl_pipeline_durable_store_import_artifacts)

# --- Aggregator (backward-compatible) ---
add_library(ahfl_pipeline_durable_store_import INTERFACE)
target_link_libraries(ahfl_pipeline_durable_store_import INTERFACE
    ahfl_pipeline_durable_store_import_core
    ahfl_pipeline_durable_store_import_providers
    ahfl_pipeline_durable_store_import_artifacts
)
