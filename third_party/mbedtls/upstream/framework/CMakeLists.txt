# Minimal framework stub for vendored mbedTLS 3.6.4.
#
# The upstream mbedTLS build expects the `framework` git submodule at this
# path. The submodule contains test infrastructure and code-generation scripts
# that are only needed when ENABLE_TESTING, ENABLE_PROGRAMS, or GEN_FILES are
# ON — all of which the vendored build forces OFF. This stub satisfies the
# `add_subdirectory(framework)` call in the root CMakeLists.txt without
# pulling in the submodule (GitHub archive downloads are blocked in this
# environment; see VENDORING.md).
