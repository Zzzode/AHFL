set(AHFL_DURABLE_STORE_IMPORT_CORE_SOURCES
    adapter_execution.cpp
    decision.cpp
    decision_review.cpp
    receipt.cpp
    receipt_persistence.cpp
    receipt_persistence_response.cpp
    receipt_persistence_response_review.cpp
    receipt_persistence_review.cpp
    recovery_preview.cpp
    receipt_review.cpp
    request.cpp
    review.cpp
)

set(AHFL_DURABLE_STORE_IMPORT_PROVIDER_BINDING_SOURCES
    provider/binding.cpp
    provider/artifacts.cpp
)

set(AHFL_DURABLE_STORE_IMPORT_PROVIDER_RUNTIME_SOURCES
    provider/runtime.cpp
)

set(AHFL_DURABLE_STORE_IMPORT_PROVIDER_EXECUTION_SOURCES
    provider/execution.cpp
)

set(AHFL_DURABLE_STORE_IMPORT_PROVIDER_SDK_SOURCES
    provider/sdk.cpp
)

set(AHFL_DURABLE_STORE_IMPORT_PROVIDER_CONFIGURATION_SOURCES
    provider/configuration.cpp
)

set(AHFL_DURABLE_STORE_IMPORT_PROVIDER_RELIABILITY_SOURCES
    provider/reliability.cpp
)

set(AHFL_DURABLE_STORE_IMPORT_PROVIDER_GOVERNANCE_SOURCES
    provider/governance.cpp
)

set(AHFL_DURABLE_STORE_IMPORT_PROVIDER_PRODUCTION_SOURCES
    provider/production.cpp
)

add_library(ahfl_durable_store_import STATIC
    ${AHFL_DURABLE_STORE_IMPORT_CORE_SOURCES}
    ${AHFL_DURABLE_STORE_IMPORT_PROVIDER_BINDING_SOURCES}
    ${AHFL_DURABLE_STORE_IMPORT_PROVIDER_RUNTIME_SOURCES}
    ${AHFL_DURABLE_STORE_IMPORT_PROVIDER_EXECUTION_SOURCES}
    ${AHFL_DURABLE_STORE_IMPORT_PROVIDER_SDK_SOURCES}
    ${AHFL_DURABLE_STORE_IMPORT_PROVIDER_CONFIGURATION_SOURCES}
    ${AHFL_DURABLE_STORE_IMPORT_PROVIDER_RELIABILITY_SOURCES}
    ${AHFL_DURABLE_STORE_IMPORT_PROVIDER_GOVERNANCE_SOURCES}
    ${AHFL_DURABLE_STORE_IMPORT_PROVIDER_PRODUCTION_SOURCES}
)

target_link_libraries(ahfl_durable_store_import
    PUBLIC
        ahfl_store_import
)

ahfl_apply_project_warnings(ahfl_durable_store_import)

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

add_library(ahfl_durable_store_import_artifacts STATIC
    ${AHFL_DURABLE_STORE_IMPORT_ARTIFACT_SOURCES}
)

target_link_libraries(ahfl_durable_store_import_artifacts
    PUBLIC
        ahfl_durable_store_import
        ahfl_support
)

ahfl_apply_project_warnings(ahfl_durable_store_import_artifacts)
