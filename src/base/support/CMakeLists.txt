add_library(ahfl_base_support STATIC
    curl.cpp
    process.cpp
    thread_pool.cpp
    timer.cpp
    version.cpp
)

target_link_libraries(ahfl_base_support
    PUBLIC
        ahfl_base_public
)

find_package(Threads REQUIRED)
target_link_libraries(ahfl_base_support PUBLIC Threads::Threads)

target_include_directories(ahfl_base_support PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_base_support)
