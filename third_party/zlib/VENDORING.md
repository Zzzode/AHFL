# zlib 1.3.1 (zlib License) — bundled compression library.

Vendored for the libcurl HTTP/2 transport (gRPC plan, `docs/plans/grpc-libcurl-vendoring-plan.md`):
curl's `Content-Encoding: gzip` handling requires zlib so that `Accept-Encoding`
behavior matches the system curl the subprocess path used today.

## Patch manifest

No patches. The upstream CMake build is used as-is, with options forced in the
wrapper `CMakeLists.txt` (static library, no examples/tests, no install rules).

## Why vendored

- Hermetic, offline-source-buildable (no `FetchContent`, no `find_package`).
- Tiny (~100 KB source), permissive license, no transitive dependencies.
- Follows the wasm3/z3/antlr4 vendoring precedents in this repository.
