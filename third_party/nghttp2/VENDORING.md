# nghttp2 1.70.0 (MIT) — bundled HTTP/2 framing library.

Vendored as the HTTP/2 backend for libcurl (gRPC plan,
`docs/plans/grpc-libcurl-vendoring-plan.md`). Required for gRPC's
HTTP/2 transport; without it curl falls back to HTTP/1.1 and gRPC
calls fail.

## Patch manifest

No source patches. The wrapper `CMakeLists.txt` forces build-system options:
- `ENABLE_LIB_ONLY=ON` (library only — no nghttp/nghttpd/nghttpx apps)
- `ENABLE_SHARED_LIB=OFF` (static only)
- `ENABLE_STATIC_LIB=ON`
- `ENABLE_DOC=OFF`, `ENABLE_EXAMPLES=OFF`, `ENABLE_HPACK_TOOLS=OFF`
- `WITH_JEMALLOC=OFF`, `WITH_JANSSON=OFF`, `WITH_LIBXML2=OFF`,
  `WITH_MRUBY=OFF`, `WITH_NEVERBLEED=OFF`, `WITH_SPDYLAY=OFF`,
  `WITH_SYSTEMD=OFF`, `WITH_LIBBPF=OFF`, `WITH_CARES=OFF`,
  `WITH_TCMALLOC=OFF`

## Why vendored

- Hermetic, offline-source-buildable (no `FetchContent`, no `find_package`).
- nghttp2 has no external dependencies beyond libc (for the core library).
- Follows the wasm3/z3/antlr4 vendoring precedents in this repository.
