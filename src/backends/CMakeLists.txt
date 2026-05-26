# --- Formal verification backend ---
if(AHFL_ENABLE_BACKEND_FORMAL)
    add_library(ahfl_backend_smv STATIC
        formal/smv.cpp
    )

    target_link_libraries(ahfl_backend_smv
        PUBLIC
            ahfl_ir
    )

    ahfl_apply_project_warnings(ahfl_backend_smv)
endif()

# --- Pipeline observation/persistence/execution backends ---
if(AHFL_ENABLE_BACKEND_PIPELINE)
    add_library(ahfl_backend_assurance STATIC
        assurance.cpp
    )

    target_link_libraries(ahfl_backend_assurance
        PUBLIC
            ahfl_assurance
    )

    ahfl_apply_project_warnings(ahfl_backend_assurance)

    add_library(ahfl_backend_summary STATIC
        summary.cpp
    )

    target_link_libraries(ahfl_backend_summary
        PUBLIC
            ahfl_ir
    )

    ahfl_apply_project_warnings(ahfl_backend_summary)

    add_library(ahfl_backend_native_json STATIC
        native_json.cpp
    )

    target_link_libraries(ahfl_backend_native_json
        PUBLIC
            ahfl_handoff
    )

    ahfl_apply_project_warnings(ahfl_backend_native_json)

    add_library(ahfl_backend_execution_plan STATIC
        execution_plan.cpp
    )

    target_link_libraries(ahfl_backend_execution_plan
        PUBLIC
            ahfl_handoff
    )

    ahfl_apply_project_warnings(ahfl_backend_execution_plan)

    add_library(ahfl_backend_execution_journal STATIC
        execution_journal.cpp
    )

    target_link_libraries(ahfl_backend_execution_journal
        PUBLIC
            ahfl_execution_pipeline
    )

    ahfl_apply_project_warnings(ahfl_backend_execution_journal)

    add_library(ahfl_backend_replay_view STATIC
        replay_view.cpp
    )

    target_link_libraries(ahfl_backend_replay_view
        PUBLIC
            ahfl_execution_pipeline
    )

    ahfl_apply_project_warnings(ahfl_backend_replay_view)

    add_library(ahfl_backend_audit_report STATIC
        audit_report.cpp
    )

    target_link_libraries(ahfl_backend_audit_report
        PUBLIC
            ahfl_observation
    )

    ahfl_apply_project_warnings(ahfl_backend_audit_report)

    add_library(ahfl_backend_scheduler_snapshot STATIC
        scheduler_snapshot.cpp
    )

    target_link_libraries(ahfl_backend_scheduler_snapshot
        PUBLIC
            ahfl_observation
    )

    ahfl_apply_project_warnings(ahfl_backend_scheduler_snapshot)

    add_library(ahfl_backend_checkpoint_record STATIC
        checkpoint_record.cpp
    )

    target_link_libraries(ahfl_backend_checkpoint_record
        PUBLIC
            ahfl_observation
    )

    ahfl_apply_project_warnings(ahfl_backend_checkpoint_record)

    add_library(ahfl_backend_checkpoint_review STATIC
        checkpoint_review.cpp
    )

    target_link_libraries(ahfl_backend_checkpoint_review
        PUBLIC
            ahfl_observation
    )

    ahfl_apply_project_warnings(ahfl_backend_checkpoint_review)

    add_library(ahfl_backend_persistence_descriptor STATIC
        persistence_descriptor.cpp
    )

    target_link_libraries(ahfl_backend_persistence_descriptor
        PUBLIC
            ahfl_persistence
    )

    ahfl_apply_project_warnings(ahfl_backend_persistence_descriptor)

    add_library(ahfl_backend_persistence_review STATIC
        persistence_review.cpp
    )

    target_link_libraries(ahfl_backend_persistence_review
        PUBLIC
            ahfl_persistence
    )

    ahfl_apply_project_warnings(ahfl_backend_persistence_review)

    add_library(ahfl_backend_persistence_export_manifest STATIC
        persistence_export_manifest.cpp
    )

    target_link_libraries(ahfl_backend_persistence_export_manifest
        PUBLIC
            ahfl_persistence
    )

    ahfl_apply_project_warnings(ahfl_backend_persistence_export_manifest)

    add_library(ahfl_backend_persistence_export_review STATIC
        persistence_export_review.cpp
    )

    target_link_libraries(ahfl_backend_persistence_export_review
        PUBLIC
            ahfl_persistence
    )

    ahfl_apply_project_warnings(ahfl_backend_persistence_export_review)

    add_library(ahfl_backend_store_import_descriptor STATIC
        store_import_descriptor.cpp
    )

    target_link_libraries(ahfl_backend_store_import_descriptor
        PUBLIC
            ahfl_persistence
    )

    ahfl_apply_project_warnings(ahfl_backend_store_import_descriptor)

    add_library(ahfl_backend_store_import_review STATIC
        store_import_review.cpp
    )

    target_link_libraries(ahfl_backend_store_import_review
        PUBLIC
            ahfl_persistence
    )

    ahfl_apply_project_warnings(ahfl_backend_store_import_review)

    add_library(ahfl_backend_scheduler_review STATIC
        scheduler_review.cpp
    )

    target_link_libraries(ahfl_backend_scheduler_review
        PUBLIC
            ahfl_observation
    )

    ahfl_apply_project_warnings(ahfl_backend_scheduler_review)

    add_library(ahfl_backend_dry_run_trace STATIC
        dry_run_trace.cpp
    )

    target_link_libraries(ahfl_backend_dry_run_trace
        PUBLIC
            ahfl_execution_pipeline
    )

    ahfl_apply_project_warnings(ahfl_backend_dry_run_trace)

    add_library(ahfl_backend_runtime_session STATIC
        runtime_session.cpp
    )

    target_link_libraries(ahfl_backend_runtime_session
        PUBLIC
            ahfl_execution_pipeline
    )

    ahfl_apply_project_warnings(ahfl_backend_runtime_session)

    add_library(ahfl_backend_package_review STATIC
        package_review.cpp
    )

    target_link_libraries(ahfl_backend_package_review
        PUBLIC
            ahfl_handoff
    )

    ahfl_apply_project_warnings(ahfl_backend_package_review)
endif()

# --- Infrastructure backends ---
if(AHFL_ENABLE_BACKEND_INFRA)
    add_library(ahfl_backend_wasm STATIC
        infra/wasm_backend.cpp
        infra/wasm_runtime.cpp
    )

    target_link_libraries(ahfl_backend_wasm PUBLIC ahfl_support_lib)

    ahfl_apply_project_warnings(ahfl_backend_wasm)

    add_library(ahfl_backend_k8s_crd STATIC
        infra/k8s_crd.cpp
    )

    target_link_libraries(ahfl_backend_k8s_crd PUBLIC ahfl_support_lib)

    ahfl_apply_project_warnings(ahfl_backend_k8s_crd)

    add_library(ahfl_backend_openapi_spec STATIC
        infra/openapi_spec.cpp
    )

    target_link_libraries(ahfl_backend_openapi_spec PUBLIC ahfl_support_lib)

    ahfl_apply_project_warnings(ahfl_backend_openapi_spec)

    add_library(ahfl_backend_terraform_gen STATIC
        infra/terraform_gen.cpp
    )

    target_link_libraries(ahfl_backend_terraform_gen PUBLIC ahfl_support_lib)

    ahfl_apply_project_warnings(ahfl_backend_terraform_gen)
endif()

# All backend sub-targets need internal src/ headers (support/, backends/, etc.)
set(_backend_targets "")
if(AHFL_ENABLE_BACKEND_FORMAL)
    list(APPEND _backend_targets ahfl_backend_smv)
endif()
if(AHFL_ENABLE_BACKEND_PIPELINE)
    list(APPEND _backend_targets
        ahfl_backend_assurance
        ahfl_backend_summary
        ahfl_backend_native_json
        ahfl_backend_execution_plan
        ahfl_backend_execution_journal
        ahfl_backend_replay_view
        ahfl_backend_audit_report
        ahfl_backend_scheduler_snapshot
        ahfl_backend_checkpoint_record
        ahfl_backend_checkpoint_review
        ahfl_backend_persistence_descriptor
        ahfl_backend_persistence_review
        ahfl_backend_persistence_export_manifest
        ahfl_backend_persistence_export_review
        ahfl_backend_store_import_descriptor
        ahfl_backend_store_import_review
        ahfl_backend_scheduler_review
        ahfl_backend_dry_run_trace
        ahfl_backend_runtime_session
        ahfl_backend_package_review
    )
endif()
if(AHFL_ENABLE_BACKEND_INFRA)
    list(APPEND _backend_targets
        ahfl_backend_wasm
        ahfl_backend_k8s_crd
        ahfl_backend_openapi_spec
        ahfl_backend_terraform_gen
    )
endif()
foreach(_tgt ${_backend_targets})
    target_include_directories(${_tgt} PRIVATE ${PROJECT_SOURCE_DIR}/src)
endforeach()

# --- Aggregator ---
add_library(ahfl_backend STATIC
    driver.cpp
    registry.cpp
)

target_link_libraries(ahfl_backend PUBLIC ahfl_ir)

if(AHFL_ENABLE_BACKEND_FORMAL)
    target_link_libraries(ahfl_backend PUBLIC ahfl_backend_smv)
endif()
if(AHFL_ENABLE_BACKEND_PIPELINE)
    target_link_libraries(ahfl_backend PUBLIC
        ahfl_backend_audit_report
        ahfl_backend_scheduler_review
        ahfl_backend_dry_run_trace
        ahfl_backend_execution_plan
        ahfl_backend_execution_journal
        ahfl_backend_replay_view
        ahfl_backend_scheduler_snapshot
        ahfl_backend_checkpoint_record
        ahfl_backend_checkpoint_review
        ahfl_backend_persistence_descriptor
        ahfl_backend_persistence_review
        ahfl_backend_persistence_export_manifest
        ahfl_backend_persistence_export_review
        ahfl_backend_store_import_descriptor
        ahfl_backend_store_import_review
        ahfl_backend_native_json
        ahfl_backend_package_review
        ahfl_backend_runtime_session
        ahfl_backend_assurance
        ahfl_backend_summary
    )
endif()
if(AHFL_ENABLE_BACKEND_INFRA)
    target_link_libraries(ahfl_backend PUBLIC
        ahfl_backend_wasm
        ahfl_backend_k8s_crd
        ahfl_backend_openapi_spec
        ahfl_backend_terraform_gen
    )
endif()

target_include_directories(ahfl_backend PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_backend)
