add_library(ahfl_runtime_provider_llm STATIC
    llm_provider_config.cpp
    http_client.cpp
    prompt_builder.cpp
    response_parser.cpp
    llm_capability_provider.cpp
    streaming.cpp
    tool_calling.cpp
    provider_registry.cpp
    token_budget.cpp
    response_cache.cpp
    prompt_template.cpp
)

target_link_libraries(ahfl_runtime_provider_llm
    PUBLIC
        ahfl_runtime_engine
    PRIVATE
        ahfl_base_json
        ahfl_base_support
)

target_include_directories(ahfl_runtime_provider_llm PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_runtime_provider_llm)
