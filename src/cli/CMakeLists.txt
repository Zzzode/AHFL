add_executable(ahflc
    ahflc.cpp
)

target_link_libraries(ahflc
    PRIVATE
        ahfl_compiler
)

ahfl_apply_project_warnings(ahflc)
