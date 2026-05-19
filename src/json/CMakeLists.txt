add_library(ahfl_json STATIC
    json_value.cpp
)

target_link_libraries(ahfl_json
    PUBLIC
        ahfl_support
)

ahfl_apply_project_warnings(ahfl_json)
