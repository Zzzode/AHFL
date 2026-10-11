# mbedTLS 3.6.4 (Apache-2.0) — bundled TLS library.

Vendored as the TLS backend for libcurl (gRPC plan,
`docs/plans/grpc-libcurl-vendoring-plan.md`). Provides HTTPS transport
for gRPC-Web and HTTP/2 connections.

## Patch manifest

One build-system stub (no source patches):
- `framework/CMakeLists.txt` — empty stub. The upstream build expects the
  `framework` git submodule at this path, but the GitHub archive tarball does
  not include submodules and GitHub downloads are blocked in this environment.
  The submodule contains only test infrastructure and code-generation scripts,
  all of which are disabled (`ENABLE_TESTING=OFF`, `ENABLE_PROGRAMS=OFF`,
  `GEN_FILES=OFF`). The stub satisfies the `add_subdirectory(framework)` call.

The wrapper `CMakeLists.txt` forces build-system options:
- `USE_STATIC_MBEDTLS_LIBRARY=ON`, `USE_SHARED_MBEDTLS_LIBRARY=OFF`
- `ENABLE_PROGRAMS=OFF`, `ENABLE_TESTING=OFF`
- `MBEDTLS_FATAL_WARNINGS=OFF` (avoids -Werror on vendored code)
- `DISABLE_PACKAGE_CONFIG_AND_INSTALL=ON`

## Why vendored

- Hermetic, offline-source-buildable (no `FetchContent`, no `find_package`).
- mbedTLS 3.6.x is the LTS line (supported until 2027-03).
- Apache-2.0 license is compatible with AHFL's Apache-2.0.
- Follows the wasm3/z3/antlr4 vendoring precedents in this repository.
