add_library(ahfl_tooling_repl STATIC
    repl.cpp
)

target_link_libraries(ahfl_tooling_repl
    PUBLIC
        ahfl_runtime_evaluator
        ahfl_verification_formal
)

target_include_directories(ahfl_tooling_repl PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_tooling_repl)

add_executable(ahfl-repl main.cpp)
target_link_libraries(ahfl-repl
    PRIVATE
        ahfl_tooling_repl
)
target_include_directories(ahfl-repl PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl-repl)
