add_library(ahfl_formal STATIC
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

target_link_libraries(ahfl_formal
    PUBLIC
        ahfl_backend_smv
    PRIVATE
        ahfl_support_lib
)

ahfl_apply_project_warnings(ahfl_formal)
