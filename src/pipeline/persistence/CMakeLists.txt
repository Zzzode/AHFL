add_library(ahfl_pipeline_persistence STATIC
    descriptor/descriptor.cpp
    descriptor/review.cpp
    export/manifest.cpp
    export/review.cpp
    store_import/descriptor.cpp
    store_import/review.cpp
)

target_link_libraries(ahfl_pipeline_persistence PUBLIC ahfl_pipeline_observation)
target_include_directories(ahfl_pipeline_persistence PRIVATE ${PROJECT_SOURCE_DIR}/src)
ahfl_apply_project_warnings(ahfl_pipeline_persistence)

add_subdirectory(durable_store_import)

