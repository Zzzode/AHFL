# curl 8.19.0 (curl License) — bundled HTTP/1.1 + HTTP/2 client.

Vendored for the in-process HTTP transport (gRPC plan,
`docs/plans/grpc-libcurl-vendoring-plan.md`): replaces the curl subprocess
(`src/base/support/curl.cpp`) with an in-process libcurl easy-handle pool,
enabling true streaming, connection reuse, and embeddability (RFC 0020).

## Patch manifest

Three build-system patches in `upstream/CMake/` (no source patches):

- `FindMbedTLS.cmake` — replaced. When mbedTLS is built from source via
  `add_subdirectory`, the `MbedTLS::mbedtls` / `MbedTLS::mbedx509` /
  `MbedTLS::mbedcrypto` targets already exist at configure time, but the
  library files do not. The upstream module uses `find_library` / `find_path`
  which cannot see not-yet-built libraries. The replacement creates the
  `CURL::mbedtls` interface target directly from the vendored targets.
- `FindNGHTTP2.cmake` — replaced. Same rationale: creates `CURL::nghttp2`
  from the `NGHTTP2::nghttp2` alias.
- `FindZLIB.cmake` — new file. CMake's built-in `FindZLIB.cmake` uses
  `find_library` which cannot see the not-yet-built `zlibstatic`. This
  module short-circuits to the existing `ZLIB::ZLIB` alias.

Reference copies live in `cmake/` (not on the module path; the active
copies are in `upstream/CMake/`).

The wrapper `CMakeLists.txt` forces build-system options:
- `HTTP_ONLY=ON` (disables FTP/SMTP/IMAP/POP3/LDAP/TELNET/DICT/TFTP/GOPHER/
  RTSP/SMB/MQTT and every other protocol — attack-surface and binary-size lock)
- `BUILD_CURL_EXE=OFF` (library only)
- `CURL_USE_MBEDTLS=ON`, all other TLS backends OFF
- `USE_NGHTTP2=ON`, `CURL_ZLIB=ON`
- `CURL_DISABLE_INSTALL=ON`
- `CURL_USE_PKGCONFIG=OFF` (vendored targets, not system libraries)

## Why vendored

- Hermetic, offline-source-buildable (no `FetchContent`, no `find_package`
  for system libraries).
- curl 8.19.x is the LTS line (supported until 2027-03).
- Follows the wasm3/z3/antlr4 vendoring precedents in this repository.
