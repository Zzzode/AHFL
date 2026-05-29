#include "runtime/engine/http_transport.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

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

// ============================================================================
// Test 1: build_curl_command basic POST
// ============================================================================

void test_build_curl_basic_post() {
    HttpRequest request;
    request.url = "https://api.example.com/v1/data";
    request.method = "POST";
    request.body = R"({"key":"value"})";
    request.timeout_seconds = 30;

    auto cmd = HttpTransport::build_curl_command(request);

    check(cmd.find("curl --config -") != std::string::npos, "basic_post.has_curl_config");
    check(cmd.find("POST") != std::string::npos, "basic_post.method_post");
    check(cmd.find("api.example.com/v1/data") != std::string::npos, "basic_post.url");
    check(cmd.find("--max-time 30") != std::string::npos, "basic_post.timeout");
    check(cmd.find("body_bytes=") != std::string::npos, "basic_post.has_body");
    check(cmd.find(R"({"key":"value"})") == std::string::npos, "basic_post.body_redacted");
}

// ============================================================================
// Test 2: build_curl_command with headers
// ============================================================================

void test_build_curl_with_headers() {
    HttpRequest request;
    request.url = "https://api.example.com/endpoint";
    request.method = "POST";
    request.headers["Content-Type"] = "application/json";
    request.headers["Authorization"] = "Bearer token123";
    request.timeout_seconds = 10;

    auto cmd = HttpTransport::build_curl_command(request);

    check(cmd.find("headers=2") != std::string::npos, "headers.count");
    check(cmd.find("Content-Type: application/json") == std::string::npos,
          "headers.content_type_redacted");
    check(cmd.find("Authorization: Bearer token123") == std::string::npos,
          "headers.auth_redacted");
    check(cmd.find("--max-time 10") != std::string::npos, "headers.timeout");
}

// ============================================================================
// Test 3: build_curl_command GET with no body
// ============================================================================

void test_build_curl_get_no_body() {
    HttpRequest request;
    request.url = "https://api.example.com/read";
    request.method = "GET";
    request.timeout_seconds = 5;

    auto cmd = HttpTransport::build_curl_command(request);

    check(cmd.find("GET") != std::string::npos, "get.method");
    check(cmd.find("body_bytes=") == std::string::npos, "get.no_body_flag");
    check(cmd.find("--max-time 5") != std::string::npos, "get.timeout");
}

// ============================================================================
// Test 4: build_curl_command with special characters in body
// ============================================================================

void test_build_curl_shell_quoting() {
    HttpRequest request;
    request.url = "https://api.example.com/post";
    request.method = "POST";
    request.body = R"({"msg":"it's a test"})";
    request.timeout_seconds = 30;

    auto cmd = HttpTransport::build_curl_command(request);

    check(cmd.find("it's a test") == std::string::npos, "quoting.body_redacted");
}

// ============================================================================
// Test 5: HttpResponse helpers
// ============================================================================

void test_http_response_helpers() {
    HttpResponse success{.status_code = 200, .body = "ok", .error = {}};
    check(success.is_success(), "response.200_is_success");
    check(!success.is_timeout(), "response.200_not_timeout");

    HttpResponse created{.status_code = 201, .body = "{}", .error = {}};
    check(created.is_success(), "response.201_is_success");

    HttpResponse not_found{.status_code = 404, .body = "not found", .error = {}};
    check(!not_found.is_success(), "response.404_not_success");

    HttpResponse server_error{.status_code = 500, .body = "error", .error = {}};
    check(!server_error.is_success(), "response.500_not_success");

    HttpResponse timeout{.status_code = 0, .body = {}, .error = "timeout"};
    check(timeout.is_timeout(), "response.timeout_detected");
    check(!timeout.is_success(), "response.timeout_not_success");

    HttpResponse other_error{.status_code = 0, .body = {}, .error = "connection refused"};
    check(!other_error.is_timeout(), "response.other_not_timeout");
    check(!other_error.is_success(), "response.other_not_success");
}

// ============================================================================
// Test 6: build_curl_command PUT method
// ============================================================================

void test_build_curl_put() {
    HttpRequest request;
    request.url = "https://api.example.com/resource/123";
    request.method = "PUT";
    request.body = R"({"name":"updated"})";
    request.timeout_seconds = 15;

    auto cmd = HttpTransport::build_curl_command(request);

    check(cmd.find("PUT") != std::string::npos, "put.method");
    check(cmd.find("--max-time 15") != std::string::npos, "put.timeout");
}

} // anonymous namespace

int main() {
    test_build_curl_basic_post();
    test_build_curl_with_headers();
    test_build_curl_get_no_body();
    test_build_curl_shell_quoting();
    test_http_response_helpers();
    test_build_curl_put();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
