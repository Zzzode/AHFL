# Z3 4.8.12 (MIT) — bundled SMT-BMC solver.
#
# RFC 0028: `ahflc check` / `ahflc run` run formal verification by default
# (fail-closed). The SMT-BMC data-predicate path discharges contract goals
# through Z3, so shipping Z3 in-tree means a default AHFL installation always
# has an SMT solver; the "missing solver" failure mode cannot occur in a
# default install. The Z3 binary is installed to bin/ alongside ahflc, and the
# runtime solver discovery checks the executable's own directory first (see
# src/verification/formal/smt_solver.cpp).
#
# The vendored source carries build-system patches for modern toolchains
# (CMake >= 3.5 minimum, Z3_BUILD_EXECUTABLE forced on for subdirectory builds,
# all install rules removed). See third_party/z3/VENDORING.md for the patch
# manifest.

# The vendored source is not a git checkout, so the git-hash / git-describe
# version stamping has nothing to read. Force it OFF to skip the .git probe
# (upstream self-disables in this case, but forcing avoids the warnings and
# keeps the version string deterministic).
set(Z3_INCLUDE_GIT_HASH OFF CACHE BOOL "" FORCE)
set(Z3_INCLUDE_GIT_DESCRIBE OFF CACHE BOOL "" FORCE)

# Static libz3. The `shell` executable links the component object files
# directly (not libz3), so this only affects the unused library artifact; a
# static archive keeps the build free of a shared-lib runtime dependency.
set(Z3_BUILD_LIBZ3_SHARED OFF CACHE BOOL "" FORCE)

# Only the `shell` (z3) executable is needed: no examples, documentation, or
# language bindings. (The bindings options would FATAL_ERROR under a static
# libz3 anyway.)
set(Z3_ENABLE_EXAMPLE_TARGETS OFF CACHE BOOL "" FORCE)
set(Z3_BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
set(Z3_BUILD_PYTHON_BINDINGS OFF CACHE BOOL "" FORCE)
set(Z3_BUILD_JAVA_BINDINGS OFF CACHE BOOL "" FORCE)
set(Z3_BUILD_DOTNET_BINDINGS OFF CACHE BOOL "" FORCE)
set(Z3_BUILD_JULIA_BINDINGS OFF CACHE BOOL "" FORCE)

# Z3_BUILD_EXECUTABLE is a cmake_dependent_option upstream that defaults ON
# only for a top-level build; in a subdirectory build the macro force-sets a
# normal variable to OFF that shadows the cache, so the `shell` target would
# never be created. The vendored source patches it to a plain option(); force
# it ON here so the z3 executable is always built.
set(Z3_BUILD_EXECUTABLE ON CACHE BOOL "" FORCE)

# Z3's own test executables are a cmake_dependent_option that already resolves
# OFF in a subdirectory build; force it explicitly for hermeticity.
set(Z3_BUILD_TEST_EXECUTABLES OFF CACHE BOOL "" FORCE)

# A vendored dependency must not break the AHFL build on a third-party
# compiler warning. Z3's WARNINGS_AS_ERRORS defaults to SERIOUS_ONLY, which
# adds -Werror= flags (e.g. -Werror=overloaded-virtual on Clang) that modern
# compilers can trip on this 2021 codebase.
set(WARNINGS_AS_ERRORS OFF CACHE STRING "" FORCE)

# Tracing is opt-in for all build types. Upstream force-enables it in Debug,
# which opens a stray empty `.z3-trace` file in the solver's CWD on every
# invocation and would pollute every `ahflc check` working directory.
set(Z3_ENABLE_TRACING_FOR_NON_DEBUG OFF CACHE BOOL "" FORCE)

add_subdirectory(upstream)

# Install the Z3 binary alongside ahflc. Gated on AHFL_INSTALL to match the
# CLI/LSP install rules (AhflInstall.cmake): a default dev build registers no
# install rules, while `cmake --install` with AHFL_INSTALL=ON ships the solver.
if(AHFL_INSTALL)
    install(TARGETS shell RUNTIME DESTINATION bin)
endif()
