add_executable(ahfl_project_parse_tests
    project/project_parse.cpp
)
target_link_libraries(ahfl_project_parse_tests
    PRIVATE
        ahfl_syntax
)
ahfl_apply_project_warnings(ahfl_project_parse_tests)

add_executable(ahfl_project_resolve_tests
    project/project_resolve.cpp
)
target_link_libraries(ahfl_project_resolve_tests
    PRIVATE
        ahfl_semantics
)
ahfl_apply_project_warnings(ahfl_project_resolve_tests)

add_executable(ahfl_project_check_tests
    project/project_check.cpp
)
target_link_libraries(ahfl_project_check_tests
    PRIVATE
        ahfl_ir
)
ahfl_apply_project_warnings(ahfl_project_check_tests)

add_executable(ahfl_handoff_package_tests
    handoff/package_model.cpp
)
target_link_libraries(ahfl_handoff_package_tests
    PRIVATE
        ahfl_handoff
)
ahfl_apply_project_warnings(ahfl_handoff_package_tests)

add_executable(ahfl_handoff_package_compat_tests
    handoff/package_compat.cpp
)
target_link_libraries(ahfl_handoff_package_compat_tests
    PRIVATE
        ahfl_backend_native_json
)
ahfl_apply_project_warnings(ahfl_handoff_package_compat_tests)

add_executable(ahfl_dry_run_tests
    dry_run/runner.cpp
)
target_link_libraries(ahfl_dry_run_tests
    PRIVATE
        ahfl_dry_run
)
ahfl_apply_project_warnings(ahfl_dry_run_tests)




