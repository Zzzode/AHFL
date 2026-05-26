add_library(ahfl_json STATIC
    json_value.cpp
)

target_link_libraries(ahfl_json
    PUBLIC
        ahfl_support
)

target_include_directories(ahfl_json PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_json)
