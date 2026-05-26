add_library(ahfl_repl STATIC
    repl.cpp
)

target_link_libraries(ahfl_repl
    PUBLIC
        ahfl_evaluator
        ahfl_formal
)

target_include_directories(ahfl_repl PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_repl)
