# NuSMV 2.6.0 (LGPL-2.1) — bundled formal verification model checker.
#
# RFC 0028: `ahflc check` / `ahflc run` run formal verification by default
# (fail-closed). Shipping NuSMV in-tree means a default AHFL installation
# always has a solver; the "missing solver" failure mode cannot occur in a
# default install. The NuSMV binary is installed to bin/ alongside ahflc, and
# the runtime solver discovery checks the executable's own directory first
# (see src/verification/formal/nuxmv_backend.cpp).
#
# The vendored source carries build-system patches for modern toolchains
# (CMake >= 3.5 LOCATION-property removal, Python 3 grammar/help scripts,
# glibc `union wait` removal, MiniSat idempotent patch step). See
# third_party/nusmv/VENDORING.md for the patch manifest.

# NuSMV is C + C++; enable C here (the parent project may be C++-only).
enable_language(C)

# Batch-mode build: no interactive-shell readline dependency, no on-line help
# array (its generator is Python 2 + sgmllib, removed in Python 3). The
# default BDD engine and the MiniSat-backed BMC engine are both built.
set(ENABLE_READLINE OFF CACHE BOOL "" FORCE)
set(ENABLE_LIBXML2 OFF CACHE BOOL "" FORCE)
set(BUILD_CMD_HELP OFF CACHE BOOL "" FORCE)

add_subdirectory(NuSMV)

# Install the NuSMV binary alongside ahflc. Gated on AHFL_INSTALL to match the
# CLI/LSP install rules (AhflInstall.cmake): a default dev build registers no
# install rules, while `cmake --install` with AHFL_INSTALL=ON ships the solver.
if(AHFL_INSTALL)
    install(TARGETS NuSMV RUNTIME DESTINATION bin)
endif()
