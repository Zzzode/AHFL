add_library(ahfl_tooling_registry STATIC
    http.cpp
    registry_handler.cpp
    server.cpp
    store.cpp
)

target_link_libraries(ahfl_tooling_registry
    PUBLIC
        ahfl_tooling_package
    PRIVATE
        ahfl_base_json
        ahfl_base_support
)

target_include_directories(ahfl_tooling_registry PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_tooling_registry)

add_executable(ahfl-registry
    main.cpp
)

target_link_libraries(ahfl-registry
    PRIVATE
        ahfl_tooling_registry
)

target_include_directories(ahfl-registry PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl-registry)
