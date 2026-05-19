#include "ahfl/runtime/grpc_transport.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace ahfl::runtime;
using namespace ahfl::evaluator;

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
// Test 1: GrpcEndpoint::to_url() plaintext
// ============================================================================

void test_endpoint_to_url_plaintext() {
    GrpcEndpoint ep;
    ep.host = "localhost";
    ep.port = 50051;
    ep.service_name = "mypackage.MyService";
    ep.method_name = "DoThing";
    ep.use_tls = false;

    auto url = ep.to_url();
    check(url == "http://localhost:50051/mypackage.MyService/DoThing",
          "endpoint_url.plaintext");
}

// ============================================================================
// Test 2: GrpcEndpoint::to_url() TLS
// ============================================================================

void test_endpoint_to_url_tls() {
    GrpcEndpoint ep;
    ep.host = "grpc.example.com";
    ep.port = 443;
    ep.service_name = "api.v1.Users";
    ep.method_name = "GetUser";
    ep.use_tls = true;

    auto url = ep.to_url();
    check(url == "https://grpc.example.com:443/api.v1.Users/GetUser",
          "endpoint_url.tls");
}

// ============================================================================
// Test 3: GrpcEndpoint::to_path()
// ============================================================================

void test_endpoint_to_path() {
    GrpcEndpoint ep;
    ep.service_name = "package.ServiceName";
    ep.method_name = "MethodName";

    auto path = ep.to_path();
    check(path == "/package.ServiceName/MethodName", "endpoint_path.basic");
}

// ============================================================================
// Test 4: grpc_status_name
// ============================================================================

void test_grpc_status_name() {
    check(std::string(grpc_status_name(GrpcStatusCode::Ok)) == "OK",
          "status_name.ok");
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
// Test 5: GrpcResponse::is_ok()
// ============================================================================

void test_grpc_response_is_ok() {
    GrpcResponse ok_resp{.status_code = GrpcStatusCode::Ok, .body = "{}", .error_message = {}};
    check(ok_resp.is_ok(), "response.ok_is_ok");

    GrpcResponse err_resp{.status_code = GrpcStatusCode::Internal, .body = {}, .error_message = "error"};
    check(!err_resp.is_ok(), "response.internal_not_ok");

    GrpcResponse not_found{.status_code = GrpcStatusCode::NotFound, .body = {}, .error_message = "not found"};
    check(!not_found.is_ok(), "response.not_found_not_ok");
}

// ============================================================================
// Test 6: serialize_args_for_grpc - empty args
// ============================================================================

void test_serialize_args_empty() {
    std::vector<Value> args;
    auto result = serialize_args_for_grpc(args);
    check(result == "{}", "serialize_args.empty");
}

// ============================================================================
// Test 7: serialize_args_for_grpc - single struct value
// ============================================================================

void test_serialize_args_single_struct() {
    Value v;
    StructValue sv;
    sv.type_name = "Request";
    sv.fields["name"] = std::make_unique<Value>();
    sv.fields["name"]->node = StringValue{"alice"};
    v.node = std::move(sv);

    std::vector<Value> args;
    args.push_back(std::move(v));

    auto result = serialize_args_for_grpc(args);
    // Single struct should be serialized directly (not wrapped in {value:...})
    check(result.find("\"name\"") != std::string::npos, "serialize_args.single_struct.has_field");
    check(result.find("\"alice\"") != std::string::npos, "serialize_args.single_struct.has_value");
    check(result.find("\"value\"") == std::string::npos, "serialize_args.single_struct.not_wrapped");
}

// ============================================================================
// Test 8: serialize_args_for_grpc - single non-struct value
// ============================================================================

void test_serialize_args_single_non_struct() {
    Value v;
    v.node = IntValue{42};

    std::vector<Value> args;
    args.push_back(std::move(v));

    auto result = serialize_args_for_grpc(args);
    // Non-struct single value is wrapped: {"value": 42}
    check(result.find("\"value\"") != std::string::npos, "serialize_args.single_int.wrapped");
    check(result.find("42") != std::string::npos, "serialize_args.single_int.has_value");
}

// ============================================================================
// Test 9: serialize_args_for_grpc - multiple args
// ============================================================================

void test_serialize_args_multiple() {
    Value v1;
    v1.node = IntValue{1};
    Value v2;
    v2.node = StringValue{"hello"};

    std::vector<Value> args;
    args.push_back(std::move(v1));
    args.push_back(std::move(v2));

    auto result = serialize_args_for_grpc(args);
    // Multiple args are wrapped as {"args": [...]}
    check(result.find("\"args\"") != std::string::npos, "serialize_args.multi.has_args_key");
    check(result.find('[') != std::string::npos, "serialize_args.multi.has_array");
    check(result.find("1") != std::string::npos, "serialize_args.multi.has_first");
    check(result.find("\"hello\"") != std::string::npos, "serialize_args.multi.has_second");
}

// ============================================================================
// Test 10: build_grpc_curl_command - plaintext h2c
// ============================================================================

void test_build_curl_command_h2c() {
    GrpcRequest req;
    req.endpoint.host = "localhost";
    req.endpoint.port = 50051;
    req.endpoint.service_name = "test.Service";
    req.endpoint.method_name = "Call";
    req.endpoint.use_tls = false;
    req.serialized_body = R"({"key":"val"})";
    req.timeout = std::chrono::seconds{10};

    auto cmd = build_grpc_curl_command(req);

    check(cmd.find("curl -s -w") != std::string::npos, "curl_h2c.has_curl");
    check(cmd.find("--http2-prior-knowledge") != std::string::npos, "curl_h2c.h2c_flag");
    check(cmd.find("--http2 ") == std::string::npos || cmd.find("--http2-prior-knowledge") != std::string::npos,
          "curl_h2c.not_plain_http2");
    check(cmd.find("-X POST") != std::string::npos, "curl_h2c.post_method");
    check(cmd.find("http://localhost:50051/test.Service/Call") != std::string::npos,
          "curl_h2c.url");
    check(cmd.find("Content-Type: application/json") != std::string::npos,
          "curl_h2c.content_type");
    check(cmd.find("TE: trailers") != std::string::npos, "curl_h2c.te_trailers");
    check(cmd.find("--max-time 10") != std::string::npos, "curl_h2c.timeout");
}

// ============================================================================
// Test 11: build_grpc_curl_command - TLS h2
// ============================================================================

void test_build_curl_command_tls() {
    GrpcRequest req;
    req.endpoint.host = "grpc.example.com";
    req.endpoint.port = 443;
    req.endpoint.service_name = "api.v1.Service";
    req.endpoint.method_name = "Get";
    req.endpoint.use_tls = true;
    req.serialized_body = "{}";
    req.timeout = std::chrono::seconds{30};

    auto cmd = build_grpc_curl_command(req);

    check(cmd.find("--http2 ") != std::string::npos, "curl_tls.http2_flag");
    check(cmd.find("--http2-prior-knowledge") == std::string::npos, "curl_tls.no_h2c");
    check(cmd.find("https://grpc.example.com:443/api.v1.Service/Get") != std::string::npos,
          "curl_tls.url");
    check(cmd.find("--max-time 30") != std::string::npos, "curl_tls.timeout");
}

// ============================================================================
// Test 12: build_grpc_curl_command - with metadata
// ============================================================================

void test_build_curl_command_metadata() {
    GrpcRequest req;
    req.endpoint.host = "localhost";
    req.endpoint.port = 8080;
    req.endpoint.service_name = "svc.Foo";
    req.endpoint.method_name = "Bar";
    req.endpoint.use_tls = false;
    req.serialized_body = "{}";
    req.metadata = {{"Authorization", "Bearer token123"}, {"x-request-id", "abc-def"}};
    req.timeout = std::chrono::seconds{5};

    auto cmd = build_grpc_curl_command(req);

    check(cmd.find("Authorization: Bearer token123") != std::string::npos,
          "curl_meta.auth_header");
    check(cmd.find("x-request-id: abc-def") != std::string::npos,
          "curl_meta.request_id");
}

// ============================================================================
// Test 13: build_grpc_curl_command - empty body sends {}
// ============================================================================

void test_build_curl_command_empty_body() {
    GrpcRequest req;
    req.endpoint.host = "localhost";
    req.endpoint.port = 50051;
    req.endpoint.service_name = "svc.A";
    req.endpoint.method_name = "B";
    req.endpoint.use_tls = false;
    req.serialized_body = "";
    req.timeout = std::chrono::seconds{30};

    auto cmd = build_grpc_curl_command(req);

    // When body is empty, should still send {} for gRPC
    check(cmd.find("-d ") != std::string::npos, "curl_empty_body.has_data_flag");
    check(cmd.find("{}") != std::string::npos, "curl_empty_body.sends_empty_object");
}

// ============================================================================
// Test 14: serialize_value_for_grpc
// ============================================================================

void test_serialize_value_for_grpc() {
    Value v;
    v.node = StringValue{"hello world"};
    auto result = serialize_value_for_grpc(v);
    check(result.find("hello world") != std::string::npos, "serialize_value.string");

    Value v_int;
    v_int.node = IntValue{99};
    auto result_int = serialize_value_for_grpc(v_int);
    check(result_int.find("99") != std::string::npos, "serialize_value.int");

    Value v_bool;
    v_bool.node = BoolValue{true};
    auto result_bool = serialize_value_for_grpc(v_bool);
    check(result_bool.find("true") != std::string::npos, "serialize_value.bool");
}

} // anonymous namespace

int main() {
    test_endpoint_to_url_plaintext();
    test_endpoint_to_url_tls();
    test_endpoint_to_path();
    test_grpc_status_name();
    test_grpc_response_is_ok();
    test_serialize_args_empty();
    test_serialize_args_single_struct();
    test_serialize_args_single_non_struct();
    test_serialize_args_multiple();
    test_build_curl_command_h2c();
    test_build_curl_command_tls();
    test_build_curl_command_metadata();
    test_build_curl_command_empty_body();
    test_serialize_value_for_grpc();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
