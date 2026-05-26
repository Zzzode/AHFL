add_library(ahfl_ir STATIC
    ir.cpp
    ir_json.cpp
)

target_link_libraries(ahfl_ir
    PUBLIC
        ahfl_semantics
)

target_include_directories(ahfl_ir PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_ir)
