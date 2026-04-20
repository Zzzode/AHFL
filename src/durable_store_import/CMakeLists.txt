add_library(ahfl_durable_store_import STATIC
    decision.cpp
    decision_review.cpp
    receipt.cpp
    receipt_persistence.cpp
    receipt_persistence_review.cpp
    receipt_review.cpp
    request.cpp
    review.cpp
)

target_link_libraries(ahfl_durable_store_import
    PUBLIC
        ahfl_store_import
)

ahfl_apply_project_warnings(ahfl_durable_store_import)
