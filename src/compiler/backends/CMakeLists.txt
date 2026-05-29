# --- Formal verification backend ---
if(AHFL_ENABLE_BACKEND_FORMAL)
    add_library(ahfl_compiler_backend_smv STATIC
        smv/smv.cpp
        smv/smv_indexing.cpp
        smv/smv_collection.cpp
        smv/smv_specs.cpp
        smv/smv_formula.cpp
    )

    target_link_libraries(ahfl_compiler_backend_smv
        PUBLIC
            ahfl_compiler_ir
    )

    ahfl_apply_project_warnings(ahfl_compiler_backend_smv)
endif()

# --- Pipeline observation/persistence/execution backends ---
if(AHFL_ENABLE_BACKEND_PIPELINE)
    add_library(ahfl_compiler_backend_pipeline_common STATIC
        pipeline/json_helpers.cpp
        pipeline/review_helpers.cpp
    )

    target_link_libraries(ahfl_compiler_backend_pipeline_common
        PUBLIC
            ahfl_compiler_ir
            ahfl_pipeline_observation
            ahfl_pipeline_persistence
            ahfl_pipeline_execution
            ahfl_compiler_handoff
    )

    ahfl_apply_project_warnings(ahfl_compiler_backend_pipeline_common)

    # -- Observation domain --
    add_library(ahfl_compiler_backend_pipeline_observation STATIC
        pipeline/audit_report.cpp
        pipeline/scheduler_snapshot.cpp
        pipeline/checkpoint_record.cpp
        pipeline/checkpoint_review.cpp
        pipeline/scheduler_review.cpp
    )

    target_link_libraries(ahfl_compiler_backend_pipeline_observation
        PUBLIC ahfl_compiler_backend_pipeline_common
    )

    ahfl_apply_project_warnings(ahfl_compiler_backend_pipeline_observation)

    # -- Persistence domain --
    add_library(ahfl_compiler_backend_pipeline_persistence STATIC
        pipeline/persistence_descriptor.cpp
        pipeline/persistence_review.cpp
        pipeline/persistence_export_manifest.cpp
        pipeline/persistence_export_review.cpp
        pipeline/store_import_descriptor.cpp
        pipeline/store_import_review.cpp
    )

    target_link_libraries(ahfl_compiler_backend_pipeline_persistence
        PUBLIC ahfl_compiler_backend_pipeline_common
    )

    ahfl_apply_project_warnings(ahfl_compiler_backend_pipeline_persistence)

    # -- Execution domain --
    add_library(ahfl_compiler_backend_pipeline_execution STATIC
        pipeline/execution_journal.cpp
        pipeline/replay_view.cpp
        pipeline/dry_run_trace.cpp
        pipeline/runtime_session.cpp
    )

    target_link_libraries(ahfl_compiler_backend_pipeline_execution
        PUBLIC ahfl_compiler_backend_pipeline_common
    )

    ahfl_apply_project_warnings(ahfl_compiler_backend_pipeline_execution)

    # -- Handoff domain --
    add_library(ahfl_compiler_backend_pipeline_handoff STATIC
        pipeline/native_json.cpp
        pipeline/execution_plan.cpp
        pipeline/package_review.cpp
    )

    target_link_libraries(ahfl_compiler_backend_pipeline_handoff
        PUBLIC ahfl_compiler_backend_pipeline_common
    )

    ahfl_apply_project_warnings(ahfl_compiler_backend_pipeline_handoff)

    # -- Standalone backends (do NOT use pipeline_common) --
    add_library(ahfl_compiler_backend_assurance STATIC
        pipeline/assurance.cpp
    )

    target_link_libraries(ahfl_compiler_backend_assurance
        PUBLIC ahfl_compiler_assurance
    )

    ahfl_apply_project_warnings(ahfl_compiler_backend_assurance)

    add_library(ahfl_compiler_backend_summary STATIC
        pipeline/summary.cpp
    )

    target_link_libraries(ahfl_compiler_backend_summary
        PUBLIC ahfl_compiler_ir
    )

    ahfl_apply_project_warnings(ahfl_compiler_backend_summary)
endif()

# --- Infrastructure backends ---
if(AHFL_ENABLE_BACKEND_INFRA)
    add_library(ahfl_compiler_backend_infra_wasm STATIC
        infra/wasm_backend.cpp
        infra/wasm_runtime.cpp
    )

    target_link_libraries(ahfl_compiler_backend_infra_wasm PUBLIC ahfl_base_support)

    ahfl_apply_project_warnings(ahfl_compiler_backend_infra_wasm)

    add_library(ahfl_compiler_backend_infra_k8s_crd STATIC
        infra/k8s_crd.cpp
    )

    target_link_libraries(ahfl_compiler_backend_infra_k8s_crd PUBLIC ahfl_base_support)

    ahfl_apply_project_warnings(ahfl_compiler_backend_infra_k8s_crd)

    add_library(ahfl_compiler_backend_infra_openapi_spec STATIC
        infra/openapi_spec.cpp
    )

    target_link_libraries(ahfl_compiler_backend_infra_openapi_spec PUBLIC ahfl_base_support)

    ahfl_apply_project_warnings(ahfl_compiler_backend_infra_openapi_spec)

    add_library(ahfl_compiler_backend_infra_terraform_gen STATIC
        infra/terraform_gen.cpp
    )

    target_link_libraries(ahfl_compiler_backend_infra_terraform_gen PUBLIC ahfl_base_support)

    ahfl_apply_project_warnings(ahfl_compiler_backend_infra_terraform_gen)
endif()

# All backend sub-targets need internal src/ headers (support/, backends/, etc.)
set(_backend_targets "")
if(AHFL_ENABLE_BACKEND_FORMAL)
    list(APPEND _backend_targets ahfl_compiler_backend_smv)
endif()
if(AHFL_ENABLE_BACKEND_PIPELINE)
    list(APPEND _backend_targets
        ahfl_compiler_backend_pipeline_common
        ahfl_compiler_backend_pipeline_observation
        ahfl_compiler_backend_pipeline_persistence
        ahfl_compiler_backend_pipeline_execution
        ahfl_compiler_backend_pipeline_handoff
        ahfl_compiler_backend_assurance
        ahfl_compiler_backend_summary
    )
endif()
if(AHFL_ENABLE_BACKEND_INFRA)
    list(APPEND _backend_targets
        ahfl_compiler_backend_infra_wasm
        ahfl_compiler_backend_infra_k8s_crd
        ahfl_compiler_backend_infra_openapi_spec
        ahfl_compiler_backend_infra_terraform_gen
    )
endif()
foreach(_tgt ${_backend_targets})
    target_include_directories(${_tgt} PRIVATE ${PROJECT_SOURCE_DIR}/src)
endforeach()

# --- Aggregator ---
add_library(ahfl_compiler_backends STATIC
    driver.cpp
    registry.cpp
)

target_link_libraries(ahfl_compiler_backends PUBLIC ahfl_compiler_ir)

if(AHFL_ENABLE_BACKEND_FORMAL)
    target_link_libraries(ahfl_compiler_backends PUBLIC ahfl_compiler_backend_smv)
    target_compile_definitions(ahfl_compiler_backends PRIVATE AHFL_ENABLE_BACKEND_FORMAL=1)
endif()
if(AHFL_ENABLE_BACKEND_PIPELINE)
    target_link_libraries(ahfl_compiler_backends PUBLIC
        ahfl_compiler_backend_pipeline_observation
        ahfl_compiler_backend_pipeline_persistence
        ahfl_compiler_backend_pipeline_execution
        ahfl_compiler_backend_pipeline_handoff
        ahfl_compiler_backend_assurance
        ahfl_compiler_backend_summary
    )
endif()
if(AHFL_ENABLE_BACKEND_INFRA)
    target_link_libraries(ahfl_compiler_backends PUBLIC
        ahfl_compiler_backend_infra_wasm
        ahfl_compiler_backend_infra_k8s_crd
        ahfl_compiler_backend_infra_openapi_spec
        ahfl_compiler_backend_infra_terraform_gen
    )
endif()

target_include_directories(ahfl_compiler_backends PRIVATE ${PROJECT_SOURCE_DIR}/src)

if(AHFL_ENABLE_BACKEND_INFRA)
    target_compile_definitions(ahfl_compiler_backends PRIVATE AHFL_ENABLE_BACKEND_INFRA=1)
    target_sources(ahfl_compiler_backends PRIVATE infra/lower.cpp)
endif()

ahfl_apply_project_warnings(ahfl_compiler_backends)
