#include "base/support/http.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace ahfl::support {
namespace {

// ---------------------------------------------------------------------------
// Global init (once per process)
// ---------------------------------------------------------------------------

void ensure_curl_global_init() {
    static std::once_flag flag;
    std::call_once(flag, [] {
        curl_global_init(CURL_GLOBAL_ALL);
        std::atexit(curl_global_cleanup);
    });
}

// ---------------------------------------------------------------------------
// Easy-handle pool (connection reuse)
// ---------------------------------------------------------------------------
//
// A libcurl easy handle retains its connection cache after a transfer.
// Handing the same handle to a later request to the same host reuses the
// TCP+TLS connection. The pool is process-wide and mutex-guarded.

class EasyHandlePool {
  public:
    static constexpr std::size_t kPoolSize = 8;

    [[nodiscard]] static EasyHandlePool &instance() {
        static EasyHandlePool pool;
        return pool;
    }

    /// Acquire a handle from the pool (or create a new one if empty).
    [[nodiscard]] CURL *acquire() {
        std::lock_guard lock(mutex_);
        if (handles_.empty()) {
            return curl_easy_init();
        }
        CURL *handle = handles_.back();
        handles_.pop_back();
        curl_easy_reset(handle);
        return handle;
    }

    /// Return a handle to the pool for reuse.
    void release(CURL *handle) {
        if (handle == nullptr) {
            return;
        }
        std::lock_guard lock(mutex_);
        if (handles_.size() < kPoolSize) {
            handles_.push_back(handle);
        } else {
            curl_easy_cleanup(handle);
        }
    }

  private:
    EasyHandlePool() = default;
    ~EasyHandlePool() {
        for (CURL *handle : handles_) {
            curl_easy_cleanup(handle);
        }
    }

    std::mutex mutex_;
    std::vector<CURL *> handles_;
};

/// RAII wrapper that returns the handle to the pool on destruction.
class PooledHandle {
  public:
    PooledHandle() : handle_(EasyHandlePool::instance().acquire()) {}
    ~PooledHandle() { EasyHandlePool::instance().release(handle_); }

    PooledHandle(const PooledHandle &) = delete;
    PooledHandle &operator=(const PooledHandle &) = delete;
    PooledHandle(PooledHandle &&) = delete;
    PooledHandle &operator=(PooledHandle &&) = delete;

    [[nodiscard]] CURL *get() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }

  private:
    CURL *handle_;
};

// ---------------------------------------------------------------------------
// Write callbacks
// ---------------------------------------------------------------------------

struct WriteContext {
    std::string *body_buffer{nullptr};
    const HttpStreamCallbacks *stream_callbacks{nullptr};
    bool *body_started{nullptr};
};

std::size_t write_callback(char *ptr, std::size_t size, std::size_t nmemb, void *userdata) {
    auto *ctx = static_cast<WriteContext *>(userdata);
    const std::size_t total = size * nmemb;

    if (ctx->body_started) {
        *ctx->body_started = true;
    }
    if (ctx->stream_callbacks && ctx->stream_callbacks->on_body) {
        ctx->stream_callbacks->on_body(std::string_view(ptr, total));
    }
    if (ctx->body_buffer) {
        ctx->body_buffer->append(ptr, total);
    }
    return total;
}

// ---------------------------------------------------------------------------
// Header capture
// ---------------------------------------------------------------------------

struct HeaderContext {
    std::vector<std::pair<std::string, std::string>> *headers{nullptr};
    std::vector<HttpHeaderBlock> *blocks{nullptr};
    bool capture{false};
    bool *body_started{nullptr};
    struct curl_slist *slist{nullptr};
};

std::size_t header_callback(char *ptr, std::size_t size, std::size_t nmemb, void *userdata) {
    auto *ctx = static_cast<HeaderContext *>(userdata);
    const std::size_t total = size * nmemb;
    if (!ctx->capture || total == 0) {
        return total;
    }

    std::string_view line(ptr, total);
    // Strip trailing CRLF
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
        line.remove_suffix(1);
    }
    if (line.empty()) {
        return total;
    }

    // Status line: "HTTP/1.1 200 OK" or "HTTP/2 200"
    if (line.starts_with("HTTP/")) {
        if (ctx->blocks) {
            ctx->blocks->push_back(HttpHeaderBlock{
                .has_status_line = true,
                .headers = {},
            });
        }
        return total;
    }

    // HTTP/2 trailers arrive after the body. Start a new block so the
    // adapter can split response headers from trailers.
    if (ctx->body_started && *ctx->body_started && ctx->blocks &&
        !ctx->blocks->empty() && !ctx->blocks->back().headers.empty() &&
        ctx->blocks->back().has_status_line) {
        ctx->blocks->push_back(HttpHeaderBlock{
            .has_status_line = false,
            .headers = {},
        });
    }

    // Header: "Name: Value"
    const auto colon = line.find(':');
    if (colon == std::string_view::npos) {
        return total;
    }
    std::string name(line.substr(0, colon));
    std::string_view value = line.substr(colon + 1);
    while (!value.empty() && value.front() == ' ') {
        value.remove_prefix(1);
    }

    if (ctx->headers) {
        ctx->headers->emplace_back(name, std::string(value));
    }
    if (ctx->blocks && !ctx->blocks->empty()) {
        ctx->blocks->back().headers.emplace_back(std::move(name), std::string(value));
    }
    return total;
}

// ---------------------------------------------------------------------------
// Request configuration
// ---------------------------------------------------------------------------

void configure_request(CURL *handle, const HttpRequest &request, WriteContext &write_ctx,
                       HeaderContext &header_ctx) {
    curl_easy_setopt(handle, CURLOPT_URL, request.url.c_str());

    // Method
    if (request.method == "POST") {
        curl_easy_setopt(handle, CURLOPT_POST, 1L);
    } else if (request.method == "HEAD") {
        curl_easy_setopt(handle, CURLOPT_NOBODY, 1L);
    } else if (request.method == "PUT") {
        curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, "PUT");
    } else if (request.method == "DELETE") {
        curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, "DELETE");
    } else if (request.method == "PATCH") {
        curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, "PATCH");
    } else if (request.method != "GET") {
        curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, request.method.c_str());
    }

    // Headers
    if (!request.headers.empty()) {
        struct curl_slist *header_list = nullptr;
        for (const auto &[name, value] : request.headers) {
            std::string header = name + ": " + value;
            header_list = curl_slist_append(header_list, header.c_str());
        }
        curl_easy_setopt(handle, CURLOPT_HTTPHEADER, header_list);
        // Note: curl_slist is freed by the caller after curl_easy_perform.
        // We store it in the header context for cleanup.
        header_ctx.slist = header_list;
    }

    // Body (binary-safe). Set POSTFIELDSIZE before COPYPOSTFIELDS: curl's
    // CURLOPT_POSTFIELDSIZE handler frees the COPYPOSTFIELDS buffer when the
    // new size exceeds the old one (0 on a fresh/reset handle), which would
    // leave postfields NULL and cause CURLE_READ_ERROR at perform time.
    if (request.body.has_value()) {
        curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE,
                         static_cast<long>(request.body->size()));
        curl_easy_setopt(handle, CURLOPT_COPYPOSTFIELDS, request.body->c_str());
    }

    // Timeout
    curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS,
                     static_cast<long>(request.timeout_seconds * 1000));
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);

    // HTTP version
    switch (request.version) {
    case HttpVersion::Http2:
        curl_easy_setopt(handle, CURLOPT_HTTP_VERSION,
                         static_cast<long>(CURL_HTTP_VERSION_2_0));
        break;
    case HttpVersion::Http2PriorKnowledge:
        curl_easy_setopt(handle, CURLOPT_HTTP_VERSION,
                         static_cast<long>(CURL_HTTP_VERSION_2_PRIOR_KNOWLEDGE));
        break;
    case HttpVersion::Default:
        break;
    }

    // TLS
    if (!request.tls_ca_certificate_path.empty()) {
        curl_easy_setopt(handle, CURLOPT_CAINFO, request.tls_ca_certificate_path.c_str());
    }
    if (!request.tls_client_certificate_path.empty()) {
        curl_easy_setopt(handle, CURLOPT_SSLCERT,
                         request.tls_client_certificate_path.c_str());
    }
    if (!request.tls_client_key_path.empty()) {
        curl_easy_setopt(handle, CURLOPT_SSLKEY, request.tls_client_key_path.c_str());
    }
    if (!request.verify_tls) {
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    // Callbacks
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &write_ctx);
    curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(handle, CURLOPT_HEADERDATA, &header_ctx);

    // Follow redirects (common for HTTP APIs)
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle, CURLOPT_MAXREDIRS, 10L);

    // Accept gzip/deflate (zlib is vendored)
    curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, "gzip, deflate");
}

// ---------------------------------------------------------------------------
// Response mapping
// ---------------------------------------------------------------------------

[[nodiscard]] HttpResponse perform_request(const HttpRequest &request,
                                            HttpStreamCallbacks *stream_callbacks) {
    ensure_curl_global_init();

    HttpResponse response;
    PooledHandle pooled;
    if (!pooled) {
        response.error = "failed to initialize libcurl handle";
        return response;
    }
    CURL *handle = pooled.get();

    WriteContext write_ctx;
    write_ctx.body_buffer = &response.body;
    write_ctx.stream_callbacks = stream_callbacks;

    bool body_started = false;
    write_ctx.body_started = &body_started;

    HeaderContext header_ctx;
    header_ctx.headers = &response.response_headers;
    header_ctx.blocks = &response.response_header_blocks;
    header_ctx.capture = request.capture_headers;
    header_ctx.body_started = &body_started;

    configure_request(handle, request, write_ctx, header_ctx);

    // XFERINFO callback for cancellation
    if (stream_callbacks && stream_callbacks->should_cancel) {
        curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(
            handle, CURLOPT_XFERINFOFUNCTION,
            +[](void *userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
                auto *cb = static_cast<const HttpStreamCallbacks *>(userdata);
                return cb->should_cancel() ? 1 : 0;
            });
        curl_easy_setopt(handle, CURLOPT_XFERINFODATA, stream_callbacks);
    }

    const CURLcode result = curl_easy_perform(handle);

    // Clean up header list
    if (header_ctx.slist) {
        curl_slist_free_all(header_ctx.slist);
    }

    if (result == CURLE_OK) {
        curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &response.status_code);
        response.exit_code = 0;
    } else if (result == CURLE_OPERATION_TIMEDOUT) {
        response.timed_out = true;
        response.error = "request timed out";
        response.exit_code = static_cast<int>(result);
    } else {
        response.error = curl_easy_strerror(result);
        response.exit_code = static_cast<int>(result);
    }

    return response;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

HttpResponse execute_http(const HttpRequest &request) {
    return perform_request(request, nullptr);
}

HttpResponse execute_http_streaming(const HttpRequest &request,
                                     HttpStreamCallbacks callbacks) {
    return perform_request(request, &callbacks);
}

std::string describe_http_command(const HttpRequest &request) {
    std::ostringstream command;
    command << "curl --config -";
    command << " # " << request.method << " " << request.url;
    command << " headers=" << request.headers.size();
    if (request.body.has_value()) {
        command << " body_bytes=" << request.body->size();
    }
    command << " --max-time " << request.timeout_seconds;
    return command.str();
}

} // namespace ahfl::support
