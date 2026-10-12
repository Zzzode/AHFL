# mbedTLS 3.6.4 (Apache-2.0) — bundled TLS library.

Vendored as the TLS backend for libcurl (gRPC plan,
`docs/plans/grpc-libcurl-vendoring-plan.md`). Provides HTTPS transport
for gRPC-Web and HTTP/2 connections.

## Patch manifest

No source or build-system patches. The `upstream/framework/` directory contains
the complete mbedtls-framework submodule pinned by the upstream
`mbedtls-3.6.4` tag:

- Source: `https://github.com/Mbed-TLS/mbedtls-framework.git`
- Commit: `2a3e2c5ea053c14b745dbdf41f609b1edc6a72fa`
- Git tree: `f92e801b75b5d42b7ccb0f195c7b30e657d84a63`
- License: `upstream/framework/LICENSE` (Apache-2.0 OR GPL-2.0-or-later).

The GitHub source archive excludes submodule contents, so this dependency must
be vendored separately at the exact pinned commit. Even with programs, tests,
and generated files disabled, upstream CMake invokes `scripts/config.py` to
check the CTR_DRBG configuration. That script imports
`framework/scripts/mbedtls_framework/config_common.py`; an empty framework stub
does not satisfy the dependency.

To reproduce the framework snapshot, fetch the commit above and export it with
`git archive --format=tar <commit>`. The snapshot contains no nested Git checkout
and requires no network access or system Python package during configuration.

The wrapper `CMakeLists.txt` forces build-system options:
- `USE_STATIC_MBEDTLS_LIBRARY=ON`, `USE_SHARED_MBEDTLS_LIBRARY=OFF`
- `ENABLE_PROGRAMS=OFF`, `ENABLE_TESTING=OFF`
- `GEN_FILES=OFF` (pre-generated source files are shipped)
- `MBEDTLS_FATAL_WARNINGS=OFF` (avoids -Werror on vendored code)
- `DISABLE_PACKAGE_CONFIG_AND_INSTALL=ON`

## Why vendored

- Hermetic, offline-source-buildable (no `FetchContent`, no `find_package`).
- mbedTLS 3.6.x is the LTS line (supported until 2027-03).
- Apache-2.0 license is compatible with AHFL's Apache-2.0.
- Follows the wasm3/z3/antlr4 vendoring precedents in this repository.

## Verification

`ahfl.build.mbedtls_config_smoke` runs the upstream configuration script's
`--help` command through CTest. It fails if the vendored Python dependency is
missing, without changing the TLS configuration header. Configure and build
with the standard AHFL presets, then run this test alongside the HTTP/gRPC
transport tests.
