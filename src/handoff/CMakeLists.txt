add_library(ahfl_handoff STATIC
    package.cpp
)

target_link_libraries(ahfl_handoff
    PUBLIC
        ahfl_ir
)

ahfl_apply_project_warnings(ahfl_handoff)
