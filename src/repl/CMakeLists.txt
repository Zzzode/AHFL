add_library(ahfl_repl STATIC
    repl.cpp
)

target_link_libraries(ahfl_repl
    PUBLIC
        ahfl_evaluator
        ahfl_formal
)

ahfl_apply_project_warnings(ahfl_repl)
