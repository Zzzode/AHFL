add_library(ahfl_lsp STATIC
    json_rpc.cpp
    protocol_types.cpp
    document_store.cpp
    server.cpp
)

target_link_libraries(ahfl_lsp
    PUBLIC
        ahfl_semantics
        ahfl_json
)

ahfl_apply_project_warnings(ahfl_lsp)

add_executable(ahfl-lsp main.cpp)
target_link_libraries(ahfl-lsp
    PRIVATE
        ahfl_lsp
)
