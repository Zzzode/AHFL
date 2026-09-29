#include "runtime/engine/grpc_transport.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
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
// Test 10: build_grpc_json_transcoding_curl_command - plaintext h2c
// ============================================================================

void test_build_curl_command_h2c() {
    GrpcJsonTranscodingRequest req;
    req.endpoint.host = "localhost";
    req.endpoint.port = 50051;
    req.endpoint.service_name = "test.Service";
    req.endpoint.method_name = "Call";
    req.endpoint.use_tls = false;
    req.serialized_body = R"({"key":"val"})";
    req.timeout = std::chrono::seconds{10};

    auto cmd = build_grpc_json_transcoding_curl_command(req);

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
// Test 11: build_grpc_json_transcoding_curl_command - TLS h2
// ============================================================================

void test_build_curl_command_tls() {
    GrpcJsonTranscodingRequest req;
    req.endpoint.host = "grpc.example.com";
    req.endpoint.port = 443;
    req.endpoint.service_name = "api.v1.Service";
    req.endpoint.method_name = "Get";
    req.endpoint.use_tls = true;
    req.serialized_body = "{}";
    req.timeout = std::chrono::seconds{30};

    auto cmd = build_grpc_json_transcoding_curl_command(req);

    check(cmd.find("https://grpc.example.com:443/api.v1.Service/Get") != std::string::npos,
          "curl_tls.url");
    check(cmd.find("--max-time 30") != std::string::npos, "curl_tls.timeout");
}

// ============================================================================
// Test 12: build_grpc_json_transcoding_curl_command - with metadata
// ============================================================================

void test_build_curl_command_metadata() {
    GrpcJsonTranscodingRequest req;
    req.endpoint.host = "localhost";
    req.endpoint.port = 8080;
    req.endpoint.service_name = "svc.Foo";
    req.endpoint.method_name = "Bar";
    req.endpoint.use_tls = false;
    req.serialized_body = "{}";
    req.metadata = {{"Authorization", "Bearer token123"}, {"x-request-id", "abc-def"}};
    req.timeout = std::chrono::seconds{5};

    auto cmd = build_grpc_json_transcoding_curl_command(req);

    check(cmd.find("headers=4") != std::string::npos, "curl_meta.header_count");
    check(cmd.find("Authorization: Bearer token123") == std::string::npos,
          "curl_meta.auth_redacted");
    check(cmd.find("x-request-id: abc-def") == std::string::npos, "curl_meta.request_id_redacted");
}

// ============================================================================
// Test 13: build_grpc_json_transcoding_curl_command - empty body sends {}
// ============================================================================

void test_build_curl_command_empty_body() {
    GrpcJsonTranscodingRequest req;
    req.endpoint.host = "localhost";
    req.endpoint.port = 50051;
    req.endpoint.service_name = "svc.A";
    req.endpoint.method_name = "B";
    req.endpoint.use_tls = false;
    req.serialized_body = "";
    req.timeout = std::chrono::seconds{30};

    auto cmd = build_grpc_json_transcoding_curl_command(req);

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
// Test 21: execute_grpc_json_transcoding captures response metadata and trailers
// ============================================================================

void test_execute_captures_metadata_and_trailers() {
    const auto test_dir = std::filesystem::temp_directory_path() /
                          ("ahfl-grpc-curl-test-" + std::to_string(getpid()));
    std::filesystem::remove_all(test_dir);
    std::filesystem::create_directories(test_dir);
    const auto curl_path = test_dir / "curl";

    {
        std::ofstream script(curl_path);
        script << "#!/bin/sh\n";
        script << "config=$(cat)\n";
        script << "header_path=$(printf '%s\\n' \"$config\" | "
                  "sed -n 's/^dump-header = \"\\(.*\\)\"$/\\1/p' | tail -n 1)\n";
        script << "if [ -n \"$header_path\" ]; then\n";
        script << "cat > \"$header_path\" <<'EOF'\n";
        script << "HTTP/2 200\r\n";
        script << "content-type: application/json\r\n";
        script << "x-request-id: req-123\r\n";
        script << "\r\n";
        script << "grpc-status: 7\r\n";
        script << "grpc-message: permission%20denied\r\n";
        script << "\r\n";
        script << "EOF\n";
        script << "fi\n";
        script << "printf '\"ignored\"\\n200\\n'\n";
    }
    chmod(curl_path.c_str(), S_IRWXU);

    const char *old_path_cstr = std::getenv("PATH");
    const std::string old_path = old_path_cstr != nullptr ? old_path_cstr : "";
    const std::string scoped_path = test_dir.string() + ":" + old_path;
    setenv("PATH", scoped_path.c_str(), 1);

    GrpcJsonTranscodingRequest request;
    request.endpoint.host = "localhost";
    request.endpoint.port = 50051;
    request.endpoint.service_name = "svc.Test";
    request.endpoint.method_name = "Call";
    request.endpoint.use_tls = false;
    request.serialized_body = "{}";
    request.timeout = std::chrono::seconds{5};

    const auto response = execute_grpc_json_transcoding(request);

    if (old_path_cstr != nullptr) {
        setenv("PATH", old_path.c_str(), 1);
    } else {
        unsetenv("PATH");
    }
    std::filesystem::remove_all(test_dir);

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
    test_build_curl_command_h2c();
    test_build_curl_command_tls();
    test_build_curl_command_metadata();
    test_build_curl_command_empty_body();
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
