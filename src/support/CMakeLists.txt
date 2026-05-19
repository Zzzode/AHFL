add_library(ahfl_support_lib STATIC
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

ahfl_apply_project_warnings(ahfl_support_lib)
