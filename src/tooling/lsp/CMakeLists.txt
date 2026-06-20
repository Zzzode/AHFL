add_library(ahfl_tooling_lsp STATIC
    analysis_service.cpp
    code_action.cpp
    code_lens.cpp
    document_highlight.cpp
    document_store.cpp
    hover_index.cpp
    hover_markup.cpp
    hover_service.cpp
    json_rpc.cpp
    protocol_types.cpp
    selection_range.cpp
    semantic_tokens.cpp
    server.cpp
)

target_link_libraries(ahfl_tooling_lsp
    PUBLIC
        ahfl_compiler_semantics
        ahfl_base_json
        ahfl_tooling_formatter
)

target_include_directories(ahfl_tooling_lsp PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_tooling_lsp)

add_executable(ahfl-lsp main.cpp)
target_link_libraries(ahfl-lsp
    PRIVATE
        ahfl_tooling_lsp
)
target_include_directories(ahfl-lsp PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl-lsp)
