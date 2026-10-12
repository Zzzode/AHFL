# Vendoring NuSMV 2.6.0

NuSMV 2.6.0 (LGPL-2.1) is vendored here as the default formal verification
model checker for AHFL (RFC 0028). The upstream source is at
<https://nusmv.fbk.eu/>.

## Directory layout

```
third_party/nusmv/
├── CMakeLists.txt          # AHFL wrapper (build options, install rule)
├── VENDORING.md            # this file
├── NuSMV/                  # upstream NuSMV 2.6.0 source (patched)
├── cudd-2.4.1.1/           # bundled CUDD 2.4.1.1 (patched)
├── MiniSat/                # MiniSat (patched, archive + build wrapper)
└── zchaff/                 # ZChaff SAT solver (patched)
```

## Why vendor

RFC 0028 integrates formal verification into the default compilation pipeline
(fail-closed). Shipping NuSMV in-tree means a default AHFL installation always
has a working SMV model checker; the "missing solver" failure mode cannot occur
in a default install. The NuSMV binary is installed to `bin/` alongside `ahflc`
during `cmake --install`.

## Build patches

The upstream build system targets CMake 2.8 and Python 2. The following patches
adapt it to modern toolchains (CMake ≥ 3.5, Python 3, modern glibc). Patches
cover build and toolchain compatibility; no solver algorithm is modified.

### NuSMV/CMakeLists.txt

- `cmake_minimum_required(VERSION 3.5)` + `cmake_policy(VERSION 3.5)` (upstream
  requires 2.8.8, rejected by modern CMake).
- Removed `get_target_property(... LOCATION)` loops (forbidden in modern CMake;
  the generated `nusmv-config.cmake` is for external consumers only).
- Removed `add_subdirectory` for `doc/`, `contrib/`, `examples/` (not needed for
  the binary).
- Removed all `install()` calls (headers, static libraries, docs, share/nusmv).
  The sole install rule is `install(TARGETS NuSMV RUNTIME DESTINATION bin)` in
  the AHFL wrapper, gated on `AHFL_INSTALL`.

### NuSMV/cmake/NuSMVUtils.cmake

- Removed `install(TARGETS ${name} DESTINATION bin)` from the
  `nusmv_add_executable` macro (would install every NuSMV utility, including
  `ltl2smv` which is not built in batch mode).
- Removed `install(FILES "${_a}" DESTINATION "include/${_rd}")` from the
  `nusmv_add_pkg_headers` macro (internal headers are not AHFL's public API).

### NuSMV/code/nusmv/shell/trace/traceCmd.c

- Included `nusmv/core/cinit/cinit.h`, which declares the existing
  `NuSMVCore_get_tool_name` call. Modern C compilers reject the missing declaration.

### cudd-2.4.1.1/CMakeLists.txt

- `cmake_minimum_required(VERSION 3.5)` (upstream requires 2.8).
- Removed dead `get_target_property(... LOCATION)` loop (the generated
  `cudd-config.cmake` is never included).
- Removed `install(TARGETS CUDD_${_n}_LIB DESTINATION lib)` from
  `cudd_add_library` (static libs are build-time only).
- Removed `install(FILES "${_h}" DESTINATION include/cudd)` (internal headers).

### cudd-2.4.1.1/util/pipefork.c

- `union wait status` → `int status` (glibc removed `union wait`; all uses —
  `waitpid`, `WIFEXITED`, `WEXITSTATUS` — work with `int`).

### MiniSat/CMakeLists.txt

- `cmake_minimum_required(VERSION 3.5)` (upstream requires 2.8).
- Added `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` to the nested `ExternalProject`
  configure (the nested build inherits the parent's policy version otherwise).
- Replaced the patch command with an idempotent `apply_minisat_patch.cmake`
  script that treats "already applied" (exit 1 without `.rej` files) as success.
- Track patch completion with `patched_<version>`, and declare the copied
  `Solver_C.h` as an output of the nested library build. The header is created
  by that build, not by patching; using it as the patch output caused retries
  after unrelated build failures to reapply the patch and produce rejects.
  The nested build uses the detected CPU count explicitly.
- Removed `install(FILES "${minisat_lib_name}" DESTINATION lib)` (static lib is
  build-time only).

### MiniSat/MiniSat_v37dc6c6_nusmv.patch

- Fixed `get_target_property(... LOCATION)` read (forbidden in modern CMake) to
  use `CMAKE_STATIC_LIBRARY_SUFFIX` + `$<TARGET_FILE:minisat-lib-static>`.
- Fixed `extern "C"void` → `extern "C" void` (C++11 requires whitespace between
  string literal and identifier).
- Added whitespace before `PRIi64` / `PRIu64` format macros in MiniSat options
  and solver statistics, as required by modern C++ lexical rules.
- Updated hunk header line count after the LOCATION removal.

### NuSMV/cmake/combine_grammar.py

- `d.iterkeys()` → `d.keys()` (Python 3 removed `dict.iterkeys`).

### NuSMV/scripts/create_help.py

- Python 2 `print ''` → `print('')` (Python 3 syntax). This script is unused in
  the AHFL build (`BUILD_CMD_HELP=OFF` — the on-line help array requires
  `html2text` + `sgmllib`, both removed in Python 3), but patched for
  completeness.

### zchaff/CMakeLists.txt

- Removed `install(FILES "${zchaff_lib_name}" DESTINATION lib)` (static lib is
  build-time only).

## Build options

The AHFL wrapper sets:

| Option | Value | Reason |
|--------|-------|--------|
| `ENABLE_READLINE` | `OFF` | Batch mode; no interactive shell dependency |
| `ENABLE_LIBXML2` | `OFF` | No XML trace parsing needed |
| `BUILD_CMD_HELP` | `OFF` | Help array generator is Python 2 + sgmllib |

## MiniSat archive

The MiniSat source archive
(`MiniSat/37dc6c67e2af26379d88ce349eb9c4c6160e8543.zip`) is committed to the
repository. The wrapper checks for a local archive before attempting a
download, so the build is hermetic and does not require network access.

## License

NuSMV 2.6.0 is distributed under the GNU Lesser General Public License
(LGPL-2.1). The vendored source is unmodified upstream except for the
build-system patches listed above. AHFL's Apache-2.0 license is compatible with
LGPL-2.1 for this use (separate works, combined at build time).
