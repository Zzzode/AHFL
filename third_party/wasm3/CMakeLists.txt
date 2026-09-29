# wasm3 v0.9.0 interpreter core — vendored third-party dependency (MIT).
#
# See VERSION for the exact upstream tag/commit and the vendored source subset,
# NOTICE.md for attribution, LICENSE for the upstream licence text. This target
# is the portable threaded interpreter only: no WASI/libc/tracer adapters, no
# platforms/ CLI, no extensions/. AHFL's embedded host registers its own
# host imports (ahfl_cap) through the wasm3 C API.
#
# Wired from the root list only when AHFL_ENABLE_BACKEND_WASM is ON: the
# embedded host requires the wasm backend, so WASM=OFF must produce no wasm3
# target at all.
#
# wasm3 is plain C99 in a C++ project. This directory is the highest (and only)
# directory holding a C target, which is exactly where the language must be
# enabled.
enable_language(C)

add_library(wasm3 STATIC
    source/m3_bind.c
    source/m3_code.c
    source/m3_compile.c
    source/m3_core.c
    source/m3_env.c
    source/m3_exec.c
    source/m3_function.c
    source/m3_info.c
    source/m3_module.c
    source/m3_parse.c
    source/m3_validate.c
)

target_include_directories(wasm3
    SYSTEM
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/source>
        $<INSTALL_INTERFACE:include/ahfl/third_party/wasm3>
)

set_target_properties(wasm3 PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

# Third-party warning profile (antlr4 precedent): isolated from the project's
# -Wall -Wextra -Werror. Vendored C is not ours to keep warning-clean.
ahfl_apply_third_party_warnings(wasm3)
