add_library(ahfl_durable_store_import STATIC
    adapter_execution.cpp
    decision.cpp
    decision_review.cpp
    receipt.cpp
    receipt_persistence.cpp
    receipt_persistence_response.cpp
    receipt_persistence_response_review.cpp
    receipt_persistence_review.cpp
    provider_adapter.cpp
    provider_driver.cpp
    provider_host_execution.cpp
    provider_runtime.cpp
    provider_sdk.cpp
    recovery_preview.cpp
    receipt_review.cpp
    request.cpp
    review.cpp
)

target_link_libraries(ahfl_durable_store_import
    PUBLIC
        ahfl_store_import
)

ahfl_apply_project_warnings(ahfl_durable_store_import)
