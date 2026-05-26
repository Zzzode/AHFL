add_library(ahfl_support_lib STATIC
    curl.cpp
    process.cpp
    thread_pool.cpp
    timer.cpp
    version.cpp
)

target_link_libraries(ahfl_support_lib
    PUBLIC
        ahfl_support
)

find_package(Threads REQUIRED)
target_link_libraries(ahfl_support_lib PUBLIC Threads::Threads)

target_include_directories(ahfl_support_lib PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_support_lib)
