add_library(ahfl_verification_formal STATIC
    checker.cpp
    counterexample.cpp
    counterexample_json.cpp
    bmc.cpp
    model_checker_backend.cpp
    nuxmv_backend.cpp
    spin_backend.cpp
    tlaplus_backend.cpp
    process_launcher.cpp
    incremental_verifier.cpp
    state_space_estimator.cpp
)

target_link_libraries(ahfl_verification_formal
    PUBLIC
        ahfl_compiler_ir
    PRIVATE
        ahfl_base_support
)

if(TARGET ahfl_compiler_backend_smv)
    target_link_libraries(ahfl_verification_formal PRIVATE ahfl_compiler_backend_smv)
    target_compile_definitions(ahfl_verification_formal PRIVATE AHFL_ENABLE_BACKEND_FORMAL=1)
endif()

target_include_directories(ahfl_verification_formal PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_verification_formal)
