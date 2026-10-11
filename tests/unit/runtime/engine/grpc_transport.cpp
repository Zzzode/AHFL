#include "runtime/engine/grpc_transport.hpp"

#include <arpa/inet.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

using namespace ahfl::runtime;

int test_count = 0;
int pass_count = 0;

void check(bool condition, const std::string &test_name) {
    ++test_count;
    if (condition) {
        ++pass_count;
    } else {
        std::cerr << "FAIL: " << test_name << "\n";
    }
}

bool has_header_pair(const std::vector<std::pair<std::string, std::string>> &headers,
                     const std::string &name,
                     const std::string &value) {
    for (const auto &[header_name, header_value] : headers) {
        if (header_name == name && header_value == value) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// Test 1: GrpcJsonTranscodingEndpoint::to_url() plaintext
// ============================================================================

void test_endpoint_to_url_plaintext() {
    GrpcJsonTranscodingEndpoint ep;
    ep.host = "localhost";
    ep.port = 50051;
    ep.service_name = "mypackage.MyService";
    ep.method_name = "DoThing";
    ep.use_tls = false;

    auto url = ep.to_url();
    check(url == "http://localhost:50051/mypackage.MyService/DoThing", "endpoint_url.plaintext");
}

// ============================================================================
// Test 2: GrpcJsonTranscodingEndpoint::to_url() TLS
// ============================================================================

void test_endpoint_to_url_tls() {
    GrpcJsonTranscodingEndpoint ep;
    ep.host = "grpc.example.com";
    ep.port = 443;
    ep.service_name = "api.v1.Users";
    ep.method_name = "GetUser";
    ep.use_tls = true;

    auto url = ep.to_url();
    check(url == "https://grpc.example.com:443/api.v1.Users/GetUser", "endpoint_url.tls");
}

// ============================================================================
// Test 3: GrpcJsonTranscodingEndpoint::to_path()
// ============================================================================

void test_endpoint_to_path() {
    GrpcJsonTranscodingEndpoint ep;
    ep.service_name = "package.ServiceName";
    ep.method_name = "MethodName";

    auto path = ep.to_path();
    check(path == "/package.ServiceName/MethodName", "endpoint_path.basic");
}

// ============================================================================
// Test 4: grpc_status_name
// ============================================================================

void test_grpc_status_name() {
    check(std::string(grpc_status_name(GrpcStatusCode::Ok)) == "OK", "status_name.ok");
    check(std::string(grpc_status_name(GrpcStatusCode::Cancelled)) == "CANCELLED",
          "status_name.cancelled");
    check(std::string(grpc_status_name(GrpcStatusCode::InvalidArgument)) == "INVALID_ARGUMENT",
          "status_name.invalid_argument");
    check(std::string(grpc_status_name(GrpcStatusCode::DeadlineExceeded)) == "DEADLINE_EXCEEDED",
          "status_name.deadline_exceeded");
    check(std::string(grpc_status_name(GrpcStatusCode::NotFound)) == "NOT_FOUND",
          "status_name.not_found");
    check(std::string(grpc_status_name(GrpcStatusCode::Unauthenticated)) == "UNAUTHENTICATED",
          "status_name.unauthenticated");
    check(std::string(grpc_status_name(GrpcStatusCode::Internal)) == "INTERNAL",
          "status_name.internal");
    check(std::string(grpc_status_name(GrpcStatusCode::Unavailable)) == "UNAVAILABLE",
          "status_name.unavailable");
}

// ============================================================================
// Test 5: GrpcJsonTranscodingResponse::is_ok()
// ============================================================================

void test_grpc_response_is_ok() {
    GrpcJsonTranscodingResponse ok_resp{
        .status_code = GrpcStatusCode::Ok,
        .body = "{}",
        .error_message = {},
    };
    check(ok_resp.is_ok(), "response.ok_is_ok");

    GrpcJsonTranscodingResponse err_resp{
        .status_code = GrpcStatusCode::Internal,
        .body = {},
        .error_message = "error",
    };
    check(!err_resp.is_ok(), "response.internal_not_ok");

    GrpcJsonTranscodingResponse not_found{
        .status_code = GrpcStatusCode::NotFound,
        .body = {},
        .error_message = "not found",
    };
    check(!not_found.is_ok(), "response.not_found_not_ok");
}

// ============================================================================
// Test 10: describe_grpc_json_transcoding_request - plaintext h2c
// ============================================================================

void test_describe_request_h2c() {
    GrpcJsonTranscodingRequest req;
    req.endpoint.host = "localhost";
    req.endpoint.port = 50051;
    req.endpoint.service_name = "test.Service";
    req.endpoint.method_name = "Call";
    req.endpoint.use_tls = false;
    req.serialized_body = R"({"key":"val"})";
    req.timeout = std::chrono::seconds{10};

    auto cmd = describe_grpc_json_transcoding_request(req);

    check(cmd.find("curl --config -") != std::string::npos, "curl_h2c.has_curl_config");
    check(cmd.find("POST") != std::string::npos, "curl_h2c.post_method");
    check(cmd.find("http://localhost:50051/test.Service/Call") != std::string::npos,
          "curl_h2c.url");
    check(cmd.find("headers=2") != std::string::npos, "curl_h2c.header_count");
    check(cmd.find("Content-Type: application/json") == std::string::npos,
          "curl_h2c.content_type_redacted");
    check(cmd.find("TE: trailers") == std::string::npos, "curl_h2c.te_trailers_redacted");
    check(cmd.find("--max-time 10") != std::string::npos, "curl_h2c.timeout");
}

// ============================================================================
// Test 11: describe_grpc_json_transcoding_request - TLS h2
// ============================================================================

void test_describe_request_tls() {
    GrpcJsonTranscodingRequest req;
    req.endpoint.host = "grpc.example.com";
    req.endpoint.port = 443;
    req.endpoint.service_name = "api.v1.Service";
    req.endpoint.method_name = "Get";
    req.endpoint.use_tls = true;
    req.serialized_body = "{}";
    req.timeout = std::chrono::seconds{30};

    auto cmd = describe_grpc_json_transcoding_request(req);

    check(cmd.find("https://grpc.example.com:443/api.v1.Service/Get") != std::string::npos,
          "curl_tls.url");
    check(cmd.find("--max-time 30") != std::string::npos, "curl_tls.timeout");
}

// ============================================================================
// Test 12: describe_grpc_json_transcoding_request - with metadata
// ============================================================================

void test_describe_request_metadata() {
    GrpcJsonTranscodingRequest req;
    req.endpoint.host = "localhost";
    req.endpoint.port = 8080;
    req.endpoint.service_name = "svc.Foo";
    req.endpoint.method_name = "Bar";
    req.endpoint.use_tls = false;
    req.serialized_body = "{}";
    req.metadata = {{"Authorization", "Bearer token123"}, {"x-request-id", "abc-def"}};
    req.timeout = std::chrono::seconds{5};

    auto cmd = describe_grpc_json_transcoding_request(req);

    check(cmd.find("headers=4") != std::string::npos, "curl_meta.header_count");
    check(cmd.find("Authorization: Bearer token123") == std::string::npos,
          "curl_meta.auth_redacted");
    check(cmd.find("x-request-id: abc-def") == std::string::npos, "curl_meta.request_id_redacted");
}

// ============================================================================
// Test 13: describe_grpc_json_transcoding_request - empty body sends {}
// ============================================================================

void test_describe_request_empty_body() {
    GrpcJsonTranscodingRequest req;
    req.endpoint.host = "localhost";
    req.endpoint.port = 50051;
    req.endpoint.service_name = "svc.A";
    req.endpoint.method_name = "B";
    req.endpoint.use_tls = false;
    req.serialized_body = "";
    req.timeout = std::chrono::seconds{30};

    auto cmd = describe_grpc_json_transcoding_request(req);

    check(cmd.find("body_bytes=2") != std::string::npos, "curl_empty_body.sends_empty_object");
    check(cmd.find("{}") == std::string::npos, "curl_empty_body.body_redacted");
}

// ============================================================================
// Test 15: parse_grpc_status_from_headers - finds grpc-status
// ============================================================================

void test_parse_grpc_status_found() {
    std::vector<std::pair<std::string, std::string>> headers = {
        {"content-type", "application/grpc"},
        {"grpc-status", "7"},
        {"grpc-message", "permission denied"},
    };
    auto status = parse_grpc_status_from_headers(headers);
    check(status == GrpcStatusCode::PermissionDenied, "parse_status.found_7");
}

// ============================================================================
// Test 16: parse_grpc_status_from_headers - missing header returns Ok
// ============================================================================

void test_parse_grpc_status_missing() {
    std::vector<std::pair<std::string, std::string>> headers = {
        {"content-type", "application/grpc"},
    };
    auto status = parse_grpc_status_from_headers(headers);
    check(status == GrpcStatusCode::Ok, "parse_status.missing_returns_ok");
}

// ============================================================================
// Test 17: parse_grpc_status_from_headers - case insensitive
// ============================================================================

void test_parse_grpc_status_case_insensitive() {
    std::vector<std::pair<std::string, std::string>> headers = {
        {"Grpc-Status", "14"},
    };
    auto status = parse_grpc_status_from_headers(headers);
    check(status == GrpcStatusCode::Unavailable, "parse_status.case_insensitive");
}

// ============================================================================
// Test 18: parse_grpc_message_from_headers - basic
// ============================================================================

void test_parse_grpc_message_basic() {
    std::vector<std::pair<std::string, std::string>> headers = {
        {"grpc-status", "3"},
        {"grpc-message", "bad request"},
    };
    auto msg = parse_grpc_message_from_headers(headers);
    check(msg == "bad request", "parse_message.basic");
}

// ============================================================================
// Test 19: parse_grpc_message_from_headers - percent encoded
// ============================================================================

void test_parse_grpc_message_percent_encoded() {
    std::vector<std::pair<std::string, std::string>> headers = {
        {"grpc-message", "hello%20world%21"},
    };
    auto msg = parse_grpc_message_from_headers(headers);
    check(msg == "hello world!", "parse_message.percent_decoded");
}

// ============================================================================
// Test 20: parse_grpc_message_from_headers - missing returns empty
// ============================================================================

void test_parse_grpc_message_missing() {
    std::vector<std::pair<std::string, std::string>> headers = {
        {"grpc-status", "2"},
    };
    auto msg = parse_grpc_message_from_headers(headers);
    check(msg.empty(), "parse_message.missing_empty");
}

// ============================================================================
// Minimal HTTP/2 loopback server (h2c with prior knowledge)
// ============================================================================

namespace {

void read_full(int fd, void *buf, size_t len) {
    auto *p = static_cast<uint8_t *>(buf);
    while (len > 0) {
        ssize_t n = ::read(fd, p, len);
        if (n <= 0) {
            return;
        }
        p += n;
        len -= static_cast<size_t>(n);
    }
}

void write_full(int fd, const void *buf, size_t len) {
    const auto *p = static_cast<const uint8_t *>(buf);
    while (len > 0) {
        ssize_t n = ::write(fd, p, len);
        if (n <= 0) {
            return;
        }
        p += n;
        len -= static_cast<size_t>(n);
    }
}

void send_frame(int fd, uint8_t type, uint8_t flags, uint32_t stream_id,
                const uint8_t *payload, size_t length) {
    uint8_t header[9];
    header[0] = (length >> 16) & 0xFF;
    header[1] = (length >> 8) & 0xFF;
    header[2] = length & 0xFF;
    header[3] = type;
    header[4] = flags;
    header[5] = (stream_id >> 24) & 0xFF;
    header[6] = (stream_id >> 16) & 0xFF;
    header[7] = (stream_id >> 8) & 0xFF;
    header[8] = stream_id & 0xFF;
    write_full(fd, header, 9);
    if (length > 0) {
        write_full(fd, payload, length);
    }
}

void hpack_encode_int(std::vector<uint8_t> &out, size_t value, int prefix_bits) {
    const size_t max_prefix = (1U << prefix_bits) - 1U;
    if (value < max_prefix) {
        out.push_back(static_cast<uint8_t>(value));
    } else {
        out.push_back(static_cast<uint8_t>(max_prefix));
        value -= max_prefix;
        while (value >= 128) {
            out.push_back(static_cast<uint8_t>((value % 128) + 128));
            value /= 128;
        }
        out.push_back(static_cast<uint8_t>(value));
    }
}

[[nodiscard]] std::vector<uint8_t>
hpack_encode(const std::vector<std::pair<std::string, std::string>> &headers) {
    std::vector<uint8_t> out;
    for (const auto &[name, value] : headers) {
        if (name == ":status" && value == "200") {
            out.push_back(0x88); // indexed: :status: 200 (static table index 8, high bit set)
        } else {
            out.push_back(0x00); // literal without indexing, new name
            hpack_encode_int(out, name.size(), 7);
            out.insert(out.end(), name.begin(), name.end());
            hpack_encode_int(out, value.size(), 7);
            out.insert(out.end(), value.begin(), value.end());
        }
    }
    return out;
}

/// Minimal HTTP/2 cleartext server that handles one request and returns a
/// canned response (headers + body + trailers). Used to test the gRPC JSON
/// transcoding transport without a fake curl binary.
class LoopbackHttp2Server {
  public:
    LoopbackHttp2Server(std::string body,
                        std::vector<std::pair<std::string, std::string>> response_headers,
                        std::vector<std::pair<std::string, std::string>> trailers)
        : body_(std::move(body)), response_headers_(std::move(response_headers)),
          trailers_(std::move(trailers)) {
        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        int opt = 1;
        ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        ::bind(listen_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
        ::listen(listen_fd_, 1);
        socklen_t len = sizeof(addr);
        ::getsockname(listen_fd_, reinterpret_cast<sockaddr *>(&addr), &len);
        port_ = ntohs(addr.sin_port);
    }

    ~LoopbackHttp2Server() {
        if (listen_fd_ >= 0) {
            ::close(listen_fd_);
        }
    }

    LoopbackHttp2Server(const LoopbackHttp2Server &) = delete;
    LoopbackHttp2Server &operator=(const LoopbackHttp2Server &) = delete;

    [[nodiscard]] uint16_t port() const { return port_; }

    /// Handle one HTTP/2 request (blocks until a connection arrives).
    void serve_once() {
        int conn = ::accept(listen_fd_, nullptr, nullptr);
        if (conn < 0) {
            return;
        }

        // Read the HTTP/2 client connection preface (24 bytes).
        char preface[24];
        read_full(conn, preface, sizeof(preface));

        // Read frames until we see END_STREAM on the request.
        bool request_complete = false;
        uint32_t request_stream = 0;
        while (!request_complete) {
            uint8_t fh[9];
            read_full(conn, fh, sizeof(fh));
            const uint32_t length = (static_cast<uint32_t>(fh[0]) << 16) |
                                    (static_cast<uint32_t>(fh[1]) << 8) | fh[2];
            const uint8_t type = fh[3];
            const uint8_t flags = fh[4];
            const uint32_t stream_id = ((static_cast<uint32_t>(fh[5]) << 24) |
                                        (static_cast<uint32_t>(fh[6]) << 16) |
                                        (static_cast<uint32_t>(fh[7]) << 8) | fh[8]) &
                                       0x7FFFFFFFU;
            std::vector<uint8_t> payload(length);
            if (length > 0) {
                read_full(conn, payload.data(), length);
            }

            if (type == 0x4 && !(flags & 0x1)) { // SETTINGS (not ACK)
                send_frame(conn, 0x4, 0x0, 0, nullptr, 0);       // SETTINGS (empty)
                send_frame(conn, 0x4, 0x1, 0, nullptr, 0);       // SETTINGS ACK
            } else if (type == 0x1) {                            // HEADERS
                request_stream = stream_id;
                if (flags & 0x1) { // END_STREAM
                    request_complete = true;
                }
            } else if (type == 0x0) { // DATA
                if (flags & 0x1) {  // END_STREAM
                    request_complete = true;
                }
            }
        }

        // Send response: HEADERS (status + metadata)
        auto hpack = hpack_encode(response_headers_);
        send_frame(conn, 0x1, 0x4, request_stream, hpack.data(), hpack.size());

        // DATA (body, no END_STREAM — trailers follow)
        if (!body_.empty()) {
            send_frame(conn, 0x0, 0x0, request_stream,
                       reinterpret_cast<const uint8_t *>(body_.data()), body_.size());
        }

        // Trailers HEADERS (END_STREAM)
        auto trailer_hpack = hpack_encode(trailers_);
        send_frame(conn, 0x1, 0x5, request_stream, trailer_hpack.data(),
                   trailer_hpack.size());

        ::shutdown(conn, SHUT_WR);
        ::close(conn);
    }

  private:
    std::string body_;
    std::vector<std::pair<std::string, std::string>> response_headers_;
    std::vector<std::pair<std::string, std::string>> trailers_;
    int listen_fd_{-1};
    uint16_t port_{0};
};

} // namespace

// ============================================================================
// Test 21: execute_grpc_json_transcoding captures response metadata and trailers
// ============================================================================

void test_execute_captures_metadata_and_trailers() {
    const std::string body = R"("ignored")";
    const std::vector<std::pair<std::string, std::string>> response_headers = {
        {":status", "200"},
        {"content-type", "application/json"},
        {"x-request-id", "req-123"},
    };
    const std::vector<std::pair<std::string, std::string>> trailers = {
        {"grpc-status", "7"},
        {"grpc-message", "permission%20denied"},
    };

    LoopbackHttp2Server server(body, response_headers, trailers);
    std::thread server_thread([&server] { server.serve_once(); });

    GrpcJsonTranscodingRequest request;
    request.endpoint.host = "127.0.0.1";
    request.endpoint.port = server.port();
    request.endpoint.service_name = "svc.Test";
    request.endpoint.method_name = "Call";
    request.endpoint.use_tls = false;
    request.serialized_body = "{}";
    request.timeout = std::chrono::seconds{5};

    const auto response = execute_grpc_json_transcoding(request);

    server_thread.join();

    check(response.status_code == GrpcStatusCode::Ok, "execute_capture.status_ok");
    check(response.body == R"("ignored")", "execute_capture.body");
    check(has_header_pair(response.response_metadata, "content-type", "application/json"),
          "execute_capture.metadata_content_type");
    check(has_header_pair(response.response_metadata, "x-request-id", "req-123"),
          "execute_capture.metadata_request_id");
    check(has_header_pair(response.trailers, "grpc-status", "7"),
          "execute_capture.trailer_status");
    check(has_header_pair(response.trailers, "grpc-message", "permission%20denied"),
          "execute_capture.trailer_message");
}

} // anonymous namespace

int main() {
    test_endpoint_to_url_plaintext();
    test_endpoint_to_url_tls();
    test_endpoint_to_path();
    test_grpc_status_name();
    test_grpc_response_is_ok();
    test_describe_request_h2c();
    test_describe_request_tls();
    test_describe_request_metadata();
    test_describe_request_empty_body();
    test_parse_grpc_status_found();
    test_parse_grpc_status_missing();
    test_parse_grpc_status_case_insensitive();
    test_parse_grpc_message_basic();
    test_parse_grpc_message_percent_encoded();
    test_parse_grpc_message_missing();
    test_execute_captures_metadata_and_trailers();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
