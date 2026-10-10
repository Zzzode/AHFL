# Plan: Vendor libcurl, replace the curl subprocess with an in-process HTTP/2 client

**Status:** Ready for implementation
**Date:** 2026-10-11
**Decision record:** `docs/design/native-runtime-architecture.zh.md` Decision History, 2026-10-10 ("gRPC transport — in-process HTTP/2 via vendored libcurl")
**Estimated effort:** L

This plan implements the gRPC transport decision in one big-bang, interface-preserving change (CLAUDE.md Principle 1).

## 1. Current architecture (what dies)

Call chain today:

```
ahfl::support::execute_http(HttpRequest)          src/base/support/http.cpp
  -> execute_curl(CurlRequest)                    src/base/support/curl.cpp
       -> launch_process("curl", {"--config","-"})  src/base/support/process.cpp
          config via stdin, body via temp file, headers via dump-header temp file
```

Defects confirmed in code:
1. **Fake streaming** — the production path `LLMCapabilityProvider` (`config_.stream=true`) goes through `HttpClient::chat_completions` → `execute_http`, which buffers the entire SSE body; `extract_stream_content` then parses SSE lines from the buffered string. No incremental delivery, no mid-stream cancellation. (`StreamingClient::stream` in `streaming.cpp` has the same defect but zero production callers — dead code, deleted in this change; `SSEParser` is retained and promoted to the incremental parser.)
2. **No connection reuse** — one `curl` process per request; `ConnectionPool` self-documents as "a concurrency limiter rather than a true TCP connection pool".
3. **Embeddability** — a runtime that shells out to `curl` violates RFC 0020 (single-binary, vendored-only deps).

The seven `execute_http` call sites (interface preserved, zero source changes):
- `src/tooling/package/registry.cpp`
- `src/runtime/providers/llm/http_client.cpp`
- `src/runtime/providers/secret/vault_provider.cpp`
- `src/runtime/providers/secret/cloud_secret_provider.cpp`
- `src/runtime/engine/wire_transport_adapter.cpp` (both `WireTransport::execute` overloads)
- `src/runtime/providers/llm/streaming.cpp` (deleted with `StreamingClient`)

## 2. Vendored dependency set

All four are vendored (offline-source-buildable; no `FetchContent`, no `find_package` for them). Layout follows the wasm3/z3 precedents: `third_party/<dep>/{CMakeLists.txt, VERSION, VENDORING.md, LICENSE, NOTICE.md, upstream/ or source subset}`.

| Dependency | Version pin | License | Why |
|---|---|---|---|
| **curl** | 8.19.x LTS line | curl (MIT/X-style) | The in-process HTTP/1.1 + HTTP/2 client |
| **nghttp2** | 1.6x latest | MIT | Required for HTTP/2, including the h2c prior-knowledge path used by gRPC-JSON transcoding |
| **mbedTLS** | 3.6.x LTS | Apache-2.0 | TLS backend. Chosen over OpenSSL (huge, build-complex), GnuTLS (LGPL), wolfSSL (GPL/commercial): Apache-2.0, designed for embedding, first-class curl backend, supports client certs (mTLS) |
| **zlib** | 1.3.x | zlib | gzip content-encoding; tiny (~100 KB), vendored for hermeticity |

`VERSION` files record upstream tag/commit, tarball SHA-256, vendored date, and source subset (wasm3 pattern). `VENDORING.md` records the patch manifest (z3 pattern) — expect build-system-only patches to curl's `FindMbedTLS.cmake` / option defaults.

## 3. CMake integration

Root `CMakeLists.txt` — always-on, next to antlr4/doctest (libcurl is now a core runtime dependency, like ANTLR4, not backend-gated like wasm3/Z3):

```cmake
add_subdirectory(third_party/mbedtls)
add_subdirectory(third_party/nghttp2)
add_subdirectory(third_party/zlib)
add_subdirectory(third_party/curl)
```

Each wrapper forces a static, minimal, warning-isolated build (`ahfl_apply_third_party_warnings(<target>)`, per wasm3 precedent):

- **mbedtls**: static `mbedtls`/`mbedx509`/`mbedcrypto` only; `ENABLE_PROGRAMS=OFF`, `ENABLE_TESTING=OFF`.
- **nghttp2**: static `nghttp2` lib only; examples/tests/docs OFF.
- **zlib**: static `zlibstatic`; examples/tests OFF.
- **curl** wrapper (the critical one — option-locked to HTTP/HTTPS):
  ```cmake
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(CURL_DISABLE_TESTS ON CACHE BOOL "" FORCE)
  set(HTTP_ONLY ON CACHE BOOL "" FORCE)
  set(CURL_USE_MBEDTLS ON CACHE BOOL "" FORCE)
  set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
  # ... all other TLS backends OFF
  set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
  set(CURL_USE_GSSAPI OFF CACHE BOOL "" FORCE)
  set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
  set(USE_NGHTTP2 ON CACHE BOOL "" FORCE)
  set(CURL_ZLIB ON CACHE BOOL "" FORCE)
  set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
  add_subdirectory(upstream)
  ```

`src/base/support/CMakeLists.txt`: remove `curl.cpp`, link `CURL::libcurl` PUBLIC on `ahfl_base_support`.

## 4. Transport rewrite (src/base/support/http.cpp)

Delete `curl.cpp` + `curl.hpp`. `http.cpp` becomes the libcurl implementation:

- **Global init**: `curl_global_init(CURL_GLOBAL_ALL)` under `std::call_once`.
- **Easy-handle pool** (connection reuse): a process-wide pool of ~8 `CURL*` handles guarded by mutex. An easy handle retains its connection cache, so handing the same handle to a later request to the same host reuses TCP+TLS.
- **Request mapping** (preserves every `HttpRequest` field): method, headers, body (binary-safe via `CURLOPT_COPYPOSTFIELDS`), timeout (`CURLOPT_TIMEOUT_MS` + `CURLOPT_NOSIGNAL`), HTTP version, TLS (verify, CA, client cert/key).
- **Response capture**: write callback appends to `std::string`; header callback replays existing block semantics.
- **Error mapping**: `CURLE_OPERATION_TIMEDOUT` → `timed_out=true`; other `CURLE_*` → `curl_easy_strerror` text.
- **Interface**: `execute_http(const HttpRequest&) -> HttpResponse` — exact signature unchanged.

## 5. True streaming (kills fake streaming)

New entry point in `base/support/http.hpp`:

```cpp
struct HttpStreamCallbacks {
    std::function<void(std::string_view bytes)> on_body;
    std::function<bool()> should_cancel;
};
[[nodiscard]] HttpResponse execute_http_streaming(const HttpRequest&, HttpStreamCallbacks);
```

- `SSEParser` gains `feed(std::string_view bytes)`: buffers partial lines, emits completed lines.
- Production path rewired: `llm_capability_provider.cpp` streaming branch calls `execute_http_streaming` with `on_body` feeding `SSEParser::feed`, appending `LLMStreamingEvent{Chunk}` per delta as it arrives.
- `StreamingClient` deleted (zero production callers, Principle 1); `extract_stream_content` deleted.

## 6. Describe-function renames (big-bang, no legacy names)

- `HttpTransport::build_curl_command` → `describe_request`
- `build_grpc_json_transcoding_curl_command` → `describe_grpc_json_transcoding_request`

## 7. Test changes (same big-bang)

Rewrite (subprocess-coupled):
- `tests/unit/runtime/engine/http_transport.cpp` — assert new `describe_request` format.
- `tests/unit/runtime/engine/grpc_transport.cpp` — same for renamed transcoding describe.
- `tests/scripts/llm_secret_manager_smoke.py` — replace fake-curl mTLS with real loopback HTTPS server.

Add (new coverage the subprocess could never give):
- C++ unit tests: `HttpRequest`→curl option mapping; `SSEParser::feed` incremental/partial-line; header-block reconstruction.
- Loopback integration tests: connection reuse, incremental streaming, mid-stream cancellation, mTLS.

## 8. Big-bang sequence (one change, ordered commits within it)

1. Vendor mbedTLS, nghttp2, zlib, curl (sources + wrapper CMake + VERSION + VENDORING.md + LICENSE/NOTICE).
2. Root `CMakeLists.txt`: four `add_subdirectory`; `src/base/support/CMakeLists.txt`: drop `curl.cpp`, link `CURL::libcurl`.
3. Rewrite `http.cpp` on libcurl; delete `curl.cpp`/`curl.hpp`.
4. Add `execute_http_streaming` + `SSEParser::feed`; rewire `llm_capability_provider.cpp`; delete `StreamingClient`, `streaming.cpp`, `extract_stream_content`.
5. Rename the two describe functions; fix `ConnectionPool` comment.
6. Rewrite coupled tests; add new unit + loopback tests.
7. Docs: CLAUDE.md dependency line, design doc Decision History landing hash, backlog.

## 9. Verification

- Fresh configure+build with `-DAHFL_WARNINGS_AS_ERRORS=ON` (CI parity); ASan build + test.
- Full suite: `ctest --preset test-dev -j$(nproc) --output-on-failure`.
- CI gate: `python3 scripts/check-native-grpc-gate.py` — passes unchanged.
- Offline build: wipe build tree, configure with no network access.
- macOS parity: CI's macOS job covers it.
- Behavioral: loopback tests prove connection reuse, incremental SSE, cancellation; Python smokes prove end-to-end flows unchanged.

## 10. Files touched (summary)

**Added**: `third_party/{mbedtls,nghttp2,zlib,curl}/`; `execute_http_streaming` in `src/base/support/http.hpp`; loopback test helper + new tests.
**Deleted**: `src/base/support/curl.cpp`, `src/base/support/curl.hpp`, `src/runtime/providers/llm/streaming.cpp` (+ CMake entry), `extract_stream_content`, the fake-curl mTLS case in `llm_secret_manager_smoke.py`.
**Modified**: `CMakeLists.txt` (root), `src/base/support/CMakeLists.txt`, `src/base/support/http.cpp`, `src/runtime/providers/llm/llm_capability_provider.cpp`, `src/runtime/providers/llm/streaming.hpp` (SSEParser only), `src/runtime/engine/{http_transport,grpc_transport}.{hpp,cpp}` (renames), `src/runtime/engine/connection_pool.hpp` (comment), the two describe-asserting unit tests, CLAUDE.md, design doc, backlog.
**Untouched**: all seven `execute_http` call sites, `HttpRequest`/`HttpResponse` structs, `wire_transport_adapter.cpp`, `wire_capability.cpp`, `capability_transport_adapter.hpp`.
