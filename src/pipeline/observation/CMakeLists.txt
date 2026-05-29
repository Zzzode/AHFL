add_library(ahfl_pipeline_observation STATIC
    audit_report/report.cpp
    scheduler_snapshot/snapshot.cpp
    scheduler_snapshot/review.cpp
    checkpoint_record/record.cpp
    checkpoint_record/review.cpp
)

target_link_libraries(ahfl_pipeline_observation PUBLIC ahfl_pipeline_execution)
target_include_directories(ahfl_pipeline_observation PRIVATE ${PROJECT_SOURCE_DIR}/src)
ahfl_apply_project_warnings(ahfl_pipeline_observation)

