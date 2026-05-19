add_library(ahfl_llm_provider STATIC
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

target_link_libraries(ahfl_llm_provider
    PUBLIC
        ahfl_runtime
)

ahfl_apply_project_warnings(ahfl_llm_provider)
