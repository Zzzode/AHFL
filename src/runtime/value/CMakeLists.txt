add_library(ahfl_runtime_value STATIC
    value.cpp
    value_json.cpp
    scalar_spelling.cpp
)

# The host wire-value layer: interpreter-free Value / value_json /
# scalar_spelling. The engine/providers/tooling tiers and the (future) embedded
# WASM host all terminate at this Value, so it is a peer support target, not a
# part of the retiring tree-walking evaluator.
#
# Link deps are exactly the includes: value_json.hpp exposes
# ahfl::json::JsonValue in its interface (`value_from_json(const JsonValue &)`),
# so ahfl_base_json is PUBLIC. ahfl_base_support supplies the header-only
# write_escaped_json_string, which is included from value_json.cpp only, so it
# is PRIVATE: its sole include dir is a build-tree src/ path and must not leak
# into the installed target's interface. There is NO ahfl_compiler_ir
# dependency: the closure arm names no ir:: type (an opaque handle plus a
# monotonic id), so the target is free of both the evaluator and the compiler
# IR.
target_link_libraries(ahfl_runtime_value
    PUBLIC
        ahfl_base_json
    PRIVATE
        ahfl_base_support
)

# Internal src/ headers ("runtime/value/..."), build-tree usage only; see
# src/base/toml/CMakeLists.txt for the BUILD_INTERFACE rationale. These headers
# are NOT part of the installed SDK surface.
target_include_directories(ahfl_runtime_value PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

ahfl_apply_project_warnings(ahfl_runtime_value)
