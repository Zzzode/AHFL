# --- Formal verification backend ---
if(AHFL_ENABLE_BACKEND_FORMAL)
    add_library(ahfl_backend_smv STATIC
        formal/smv.cpp
        formal/smv_indexing.cpp
        formal/smv_collection.cpp
        formal/smv_specs.cpp
        formal/smv_formula.cpp
    )

    target_link_libraries(ahfl_backend_smv
        PUBLIC
            ahfl_ir
    )

    ahfl_apply_project_warnings(ahfl_backend_smv)
endif()

# --- Pipeline observation/persistence/execution backends ---
if(AHFL_ENABLE_BACKEND_PIPELINE)
    add_library(ahfl_backend_pipeline_common STATIC
        pipeline/json_helpers.cpp
        pipeline/review_helpers.cpp
    )

    target_link_libraries(ahfl_backend_pipeline_common
        PUBLIC
            ahfl_ir
            ahfl_observation
            ahfl_persistence
            ahfl_execution_pipeline
            ahfl_handoff
    )

    ahfl_apply_project_warnings(ahfl_backend_pipeline_common)

    # -- Observation domain --
    add_library(ahfl_backend_pipeline_observation STATIC
        pipeline/audit_report.cpp
        pipeline/scheduler_snapshot.cpp
        pipeline/checkpoint_record.cpp
        pipeline/checkpoint_review.cpp
        pipeline/scheduler_review.cpp
    )

    target_link_libraries(ahfl_backend_pipeline_observation
        PUBLIC ahfl_backend_pipeline_common
    )

    ahfl_apply_project_warnings(ahfl_backend_pipeline_observation)

    # -- Persistence domain --
    add_library(ahfl_backend_pipeline_persistence STATIC
        pipeline/persistence_descriptor.cpp
        pipeline/persistence_review.cpp
        pipeline/persistence_export_manifest.cpp
        pipeline/persistence_export_review.cpp
        pipeline/store_import_descriptor.cpp
        pipeline/store_import_review.cpp
    )

    target_link_libraries(ahfl_backend_pipeline_persistence
        PUBLIC ahfl_backend_pipeline_common
    )

    ahfl_apply_project_warnings(ahfl_backend_pipeline_persistence)

    # -- Execution domain --
    add_library(ahfl_backend_pipeline_execution STATIC
        pipeline/execution_journal.cpp
        pipeline/replay_view.cpp
        pipeline/dry_run_trace.cpp
        pipeline/runtime_session.cpp
    )

    target_link_libraries(ahfl_backend_pipeline_execution
        PUBLIC ahfl_backend_pipeline_common
    )

    ahfl_apply_project_warnings(ahfl_backend_pipeline_execution)

    # -- Handoff domain --
    add_library(ahfl_backend_pipeline_handoff STATIC
        pipeline/native_json.cpp
        pipeline/execution_plan.cpp
        pipeline/package_review.cpp
    )

    target_link_libraries(ahfl_backend_pipeline_handoff
        PUBLIC ahfl_backend_pipeline_common
    )

    ahfl_apply_project_warnings(ahfl_backend_pipeline_handoff)

    # -- Standalone backends (do NOT use pipeline_common) --
    add_library(ahfl_backend_assurance STATIC
        pipeline/assurance.cpp
    )

    target_link_libraries(ahfl_backend_assurance
        PUBLIC ahfl_assurance
    )

    ahfl_apply_project_warnings(ahfl_backend_assurance)

    add_library(ahfl_backend_summary STATIC
        pipeline/summary.cpp
    )

    target_link_libraries(ahfl_backend_summary
        PUBLIC ahfl_ir
    )

    ahfl_apply_project_warnings(ahfl_backend_summary)
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
        ahfl_backend_pipeline_common
        ahfl_backend_pipeline_observation
        ahfl_backend_pipeline_persistence
        ahfl_backend_pipeline_execution
        ahfl_backend_pipeline_handoff
        ahfl_backend_assurance
        ahfl_backend_summary
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
        ahfl_backend_pipeline_observation
        ahfl_backend_pipeline_persistence
        ahfl_backend_pipeline_execution
        ahfl_backend_pipeline_handoff
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

if(AHFL_ENABLE_BACKEND_INFRA)
    target_compile_definitions(ahfl_backend PRIVATE AHFL_ENABLE_BACKEND_INFRA=1)
    target_sources(ahfl_backend PRIVATE infra/lower.cpp)
endif()

ahfl_apply_project_warnings(ahfl_backend)
