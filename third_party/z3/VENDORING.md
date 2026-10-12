# Vendoring Z3 4.8.12

Z3 4.8.12 (MIT) is vendored here as the default SMT-BMC solver for AHFL's
contract data-predicate path (RFC 0028). The upstream source is at
<https://github.com/Z3Prover/z3>.

## Directory layout

```
third_party/z3/
├── CMakeLists.txt          # AHFL wrapper (build options, install rule)
├── VENDORING.md            # this file
└── upstream/               # Z3 4.8.12 source (patched)
```

## Why vendor

RFC 0028 integrates formal verification into the default compilation pipeline
(fail-closed). The SMT-BMC path discharges contract `requires`/`ensures`/
`invariant` data predicates through Z3. Shipping Z3 in-tree means a default
AHFL installation always has a working SMT solver; the "missing solver"
failure mode cannot occur in a default install. The Z3 binary is installed to
`bin/` alongside `ahflc` during `cmake --install`, and copied next to `ahflc`
in the build tree by a POST_BUILD step so executable-directory discovery works
without an install step.

## Build patches

The upstream build system targets CMake 3.4 and assumes a top-level build. The
following patches adapt it to modern toolchains (CMake >= 3.5 / CMake 4.x)
and to being built as a subdirectory of AHFL. The build-system changes below
are accompanied by source-level corrections required by modern Clang.

### upstream/src/math/lp/column_info.h

- Corrected `column_info::operator==` to compare `c.m_lower_bound`, replacing
  the nonexistent member `c.m_low_bound`. Modern Clang diagnoses this invalid
  template definition while compiling the bundled solver. The change restores
  the intended lower-bound equality comparison; no solver algorithm is changed.

### upstream/scripts/mk_util.py

- Replaced the removed `distutils.sysconfig` API with standard-library
  `sysconfig` for package paths and Python version discovery. This allows API
  generation under Python 3.12 and later without an extra Python dependency.

### upstream/CMakeLists.txt

- `cmake_minimum_required(VERSION 3.5)` + `cmake_policy(VERSION 3.5)` (upstream
  requires 3.4, rejected by CMake 4.0+ which removed compatibility with
  CMake < 3.5).
- Removed the build-tree/install-tree `Z3Config.cmake`, `Z3Targets.cmake`, and
  `z3.pc` generation and all `install(EXPORT ...)` / `install(FILES ...)`
  rules. `Z3_EXPORTED_TARGETS` is populated only by `install(TARGETS libz3
  EXPORT ...)` (removed below), so `export(EXPORT Z3_EXPORTED_TARGETS ...)`
  would fail on an empty export set. AHFL builds only the `shell` executable
  and never consumes libz3 as a CMake package, so this whole consumer-facing
  section is dead code.
- Removed the `uninstall` custom target (and its `cmake_uninstall.cmake`
  generation). The target name collides with NuSMV's `uninstall` target (CMake
  policy CMP0002: target names must be unique across the project), and no Z3
  component adds dependencies to it. AHFL's own install rules own
  uninstallation.
- Removed the unconditional Debug `_TRACE` define. Z3 is vendored as a solver,
  not a development target; the Debug tracing opens a stray empty `.z3-trace`
  file in the solver's CWD on every invocation, which would pollute the working
  directory of every `ahflc check` run. Tracing stays opt-in via
  `Z3_ENABLE_TRACING_FOR_NON_DEBUG` for all build types.

### upstream/src/CMakeLists.txt

- Removed `install(TARGETS libz3 EXPORT Z3_EXPORTED_TARGETS ...)`. AHFL does
  not install libz3; only the `shell` (z3) executable is installed, via the
  AHFL wrapper's `install(TARGETS shell ...)`.
- Replaced `cmake_dependent_option(Z3_BUILD_EXECUTABLE ... ON
  "CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR" OFF)` with a plain
  `option(Z3_BUILD_EXECUTABLE ... ON)`. The dependent option defaults ON only
  for a top-level build; in a subdirectory build the macro force-sets a *normal*
  variable to OFF that shadows the cache, so the `shell` target would never be
  created. The wrapper forces `Z3_BUILD_EXECUTABLE=ON`; this patch makes that
  override take effect.

### upstream/src/shell/CMakeLists.txt

- Removed `install(TARGETS shell ...)`. The AHFL wrapper owns the sole install
  rule (`install(TARGETS shell RUNTIME DESTINATION bin)`, gated on
  `AHFL_INSTALL`).

### upstream/src/api/{julia,python,java,dotnet}/CMakeLists.txt and upstream/doc/CMakeLists.txt

- Removed every `install(...)` call. These subdirectories are only added when
  the corresponding bindings/documentation options are ON (the wrapper forces
  them all OFF), so the rules are unreachable in the AHFL build; they are
  removed for a clean, dead-code-free vendored tree.

## Build options

The AHFL wrapper sets:

| Option | Value | Reason |
|--------|-------|--------|
| `Z3_INCLUDE_GIT_HASH` | `OFF` | Vendored source is not a git checkout |
| `Z3_INCLUDE_GIT_DESCRIBE` | `OFF` | Vendored source is not a git checkout |
| `Z3_BUILD_LIBZ3_SHARED` | `OFF` | Static; `shell` links component objects, not libz3 |
| `Z3_ENABLE_EXAMPLE_TARGETS` | `OFF` | Only the z3 executable is needed |
| `Z3_BUILD_DOCUMENTATION` | `OFF` | No documentation build |
| `Z3_BUILD_PYTHON_BINDINGS` | `OFF` | No bindings (would FATAL_ERROR under static libz3) |
| `Z3_BUILD_JAVA_BINDINGS` | `OFF` | No bindings |
| `Z3_BUILD_DOTNET_BINDINGS` | `OFF` | No bindings |
| `Z3_BUILD_JULIA_BINDINGS` | `OFF` | No bindings |
| `Z3_BUILD_EXECUTABLE` | `ON` | Force the `shell` (z3) target in a subdirectory build |
| `Z3_BUILD_TEST_EXECUTABLES` | `OFF` | Z3's own tests are not part of AHFL's suite |
| `WARNINGS_AS_ERRORS` | `OFF` | A third-party warning must not break the AHFL build |
| `Z3_ENABLE_TRACING_FOR_NON_DEBUG` | `OFF` | Avoids a stray `.z3-trace` file in the solver's CWD |

## Target naming

The Z3 executable target is named `shell` with `OUTPUT_NAME z3` (see
`upstream/src/shell/CMakeLists.txt`). The AHFL wrapper and the CLI POST_BUILD
step reference the `shell` target; the installed/copied artifact is `z3`.

## License

Z3 is distributed under the MIT License. The vendored source is unmodified
upstream except for the patches listed above. AHFL's Apache-2.0
license is compatible with MIT for this use.

### upstream/src/math/lp/tail_matrix.h, static_matrix.h and static_matrix_def.h

- Restrict the debug matrix row projection to `Z3DEBUG`, matching the base
  matrix interface it uses. Release builds do not provide that interface.
- Correct the static matrix reference assignment to use `get_elem`, the actual
  element accessor. This closes modern Clang release template diagnostics.
- Correct the column-copy constructor to use `get_val`, the existing column
  cell accessor, instead of the removed `get_value_of_column_cell` name.
