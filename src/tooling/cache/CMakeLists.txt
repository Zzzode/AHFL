# RFC 0016 persistence layer, re-anchored by RFC 0027 P5 (KR6.12) as the
# QueryEngine persistence contract. This library is the deterministic,
# secret-free, index-keyed on-disk cache that derived query artifacts (typed
# HIR) round-trip through; it deliberately carries NO dependency on the
# compiler frontend or the deleted hand-rolled incremental subsystem
# (dependency graph / IR cache / changed-compile / daemon), so any future
# query-persistence consumer can link it alone.
add_library(ahfl_tooling_cache STATIC
    cache_core.cpp
)

target_link_libraries(ahfl_tooling_cache
    PUBLIC
        ahfl_base_json
    PRIVATE
        ahfl_base_support
)

ahfl_enable_cxx23(ahfl_tooling_cache)
target_include_directories(ahfl_tooling_cache PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_tooling_cache)
