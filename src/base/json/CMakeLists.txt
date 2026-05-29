add_library(ahfl_base_json STATIC
    json_value.cpp
)

target_link_libraries(ahfl_base_json
    PUBLIC
        ahfl_base_public
)

target_include_directories(ahfl_base_json PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_base_json)
