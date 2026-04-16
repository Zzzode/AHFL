file(GLOB_RECURSE ANTLR4_RUNTIME_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/*.cpp"
)

add_library(antlr4_runtime STATIC
    ${ANTLR4_RUNTIME_SOURCES}
)

target_include_directories(antlr4_runtime
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/runtime/src
)

ahfl_enable_cxx23(antlr4_runtime)
target_compile_definitions(antlr4_runtime PUBLIC ANTLR4CPP_STATIC)
target_link_libraries(antlr4_runtime PUBLIC Threads::Threads)

if(APPLE)
    find_library(COREFOUNDATION_LIBRARY CoreFoundation)
    if(COREFOUNDATION_LIBRARY)
        target_link_libraries(antlr4_runtime PUBLIC ${COREFOUNDATION_LIBRARY})
    endif()
endif()

ahfl_apply_third_party_warnings(antlr4_runtime)
