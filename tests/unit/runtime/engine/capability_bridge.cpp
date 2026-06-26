#include "runtime/engine/capability_bridge.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "runtime/engine/capability_eval.hpp"
#include "runtime/evaluator/value.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace {

using namespace ahfl;
using namespace ahfl::evaluator;
using namespace ahfl::runtime;
using namespace ahfl::ir;

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

ExprArena &test_expr_arena() {
    static ExprArena arena;
    return arena;
}

ExprRef make_expr_ptr(ExprNode node) {
    return test_expr_arena().make(std::move(node));
}

class FakeCapabilityTransport final : public CapabilityTransportAdapter {
  public:
    HttpResponse http_response;
    GrpcJsonTranscodingResponse grpc_response;
    mutable std::vector<HttpRequest> http_requests;
    mutable std::vector<GrpcJsonTranscodingRequest> grpc_requests;

    [[nodiscard]] HttpResponse execute_http(const HttpRequest &request) const override {
        http_requests.push_back(request);
        return http_response;
    }

    [[nodiscard]] GrpcJsonTranscodingResponse
    execute_grpc_json_transcoding(const GrpcJsonTranscodingRequest &request) const override {
        grpc_requests.push_back(request);
        return grpc_response;
    }
};

class StaticSecretProvider final : public ahfl::secret::SecretProvider {
  public:
    explicit StaticSecretProvider(std::unordered_map<std::string, std::string> secrets)
        : secrets_(std::move(secrets)) {}

    [[nodiscard]] std::optional<std::string> resolve(std::string_view key) override {
        auto it = secrets_.find(std::string(key));
        if (it == secrets_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    void refresh(std::string_view) override {}

  private:
    std::unordered_map<std::string, std::string> secrets_;
};

std::shared_ptr<ahfl::secret::SecretManager>
make_secret_manager(std::unordered_map<std::string, std::string> secrets) {
    return std::make_shared<ahfl::secret::SecretManager>(
        std::make_unique<StaticSecretProvider>(std::move(secrets)));
}

bool has_request_header(const HttpRequest &request,
                        const std::string &name,
                        const std::string &value) {
    auto it = request.headers.find(name);
    return it != request.headers.end() && it->second == value;
}

bool has_request_metadata(const GrpcJsonTranscodingRequest &request,
                          const std::string &name,
                          const std::string &value) {
    for (const auto &[key, metadata_value] : request.metadata) {
        if (key == name && metadata_value == value) {
            return true;
        }
    }
    return false;
}

bool has_request_metadata_name(const GrpcJsonTranscodingRequest &request, const std::string &name) {
    for (const auto &metadata : request.metadata) {
        if (metadata.first == name) {
            return true;
        }
    }
    return false;
}

std::shared_ptr<const TypeRef> make_response_schema(TypeRefKind kind) {
    auto schema = std::make_shared<TypeRef>();
    schema->kind = kind;
    return schema;
}

// ============================================================================
// Test 1: register_and_invoke_mock
// ============================================================================

void test_register_and_invoke_mock() {
    CapabilityRegistry registry;
    registry.register_mock("get_answer", make_int(42));

    auto result = registry.invoke("get_answer", {});

    check(result.status == CapabilityCallStatus::Success, "mock.status_success");
    check(result.value.has_value(), "mock.has_value");
    if (result.value.has_value()) {
        auto *iv = std::get_if<IntValue>(&result.value->node);
        check(iv != nullptr && iv->value == 42, "mock.value_is_42");
    }
    check(result.attempts == 1, "mock.attempts_1");
}

// ============================================================================
// Test 2: register_and_invoke_function
// ============================================================================

void test_register_and_invoke_function() {
    CapabilityRegistry registry;
    registry.register_function("add_one", [](const std::vector<Value> &args) -> Value {
        if (!args.empty()) {
            if (auto *iv = std::get_if<IntValue>(&args[0].node)) {
                return make_int(iv->value + 1);
            }
        }
        return make_none();
    });

    std::vector<Value> args;
    args.push_back(make_int(10));
    auto result = registry.invoke("add_one", args);

    check(result.status == CapabilityCallStatus::Success, "function.status_success");
    check(result.value.has_value(), "function.has_value");
    if (result.value.has_value()) {
        auto *iv = std::get_if<IntValue>(&result.value->node);
        check(iv != nullptr && iv->value == 11, "function.value_is_11");
    }
}

// ============================================================================
// Test 3: invoke_not_found
// ============================================================================

void test_invoke_not_found() {
    CapabilityRegistry registry;

    auto result = registry.invoke("nonexistent", {});

    check(result.status == CapabilityCallStatus::Error, "not_found.status_error");
    check(!result.value.has_value(), "not_found.no_value");
    check(!result.error_message.empty(), "not_found.has_error_message");
}

// ============================================================================
// Test 4: retry_success_on_second
// ============================================================================

void test_retry_success_on_second() {
    CapabilityRegistry registry;

    int call_count = 0;
    CapabilityBinding binding;
    binding.name = "flaky";
    binding.retry.max_retries = 2;
    binding.retry.initial_delay = std::chrono::milliseconds{0};
    binding.handler = [&call_count](const std::vector<Value> & /*args*/) -> CapabilityCallResult {
        ++call_count;
        if (call_count == 1) {
            return CapabilityCallResult{
                .status = CapabilityCallStatus::Error,
                .value = std::nullopt,
                .error_message = "transient error",
                .attempts = 1,
            };
        }
        return CapabilityCallResult{
            .status = CapabilityCallStatus::Success,
            .value = make_string("ok"),
            .error_message = {},
            .attempts = 1,
        };
    };
    registry.register_capability(std::move(binding));

    auto result = registry.invoke("flaky", {});

    check(result.status == CapabilityCallStatus::Success, "retry_second.status_success");
    check(result.attempts == 2, "retry_second.attempts_2");
    check(call_count == 2, "retry_second.call_count_2");
}

// ============================================================================
// Test 5: retry_exhausted
// ============================================================================

void test_retry_exhausted() {
    CapabilityRegistry registry;

    int call_count = 0;
    CapabilityBinding binding;
    binding.name = "always_fail";
    binding.retry.max_retries = 2;
    binding.retry.initial_delay = std::chrono::milliseconds{0};
    binding.handler = [&call_count](const std::vector<Value> & /*args*/) -> CapabilityCallResult {
        ++call_count;
        return CapabilityCallResult{
            .status = CapabilityCallStatus::Error,
            .value = std::nullopt,
            .error_message = "permanent error",
            .attempts = 1,
        };
    };
    registry.register_capability(std::move(binding));

    auto result = registry.invoke("always_fail", {});

    check(result.status == CapabilityCallStatus::RetryExhausted, "exhausted.status");
    check(result.attempts == 3, "exhausted.attempts_3");
    check(call_count == 3, "exhausted.call_count_3");
}

// ============================================================================
// Test 6: as_invoker_preserves_structured_result
// ============================================================================

void test_as_invoker_preserves_structured_result() {
    CapabilityRegistry registry;
    registry.register_mock("greeting", make_string("hello"));

    auto invoker = registry.as_invoker();
    auto result = invoker("greeting", {});

    check(result.status == CapabilityCallStatus::Success, "invoker.status_success");
    check(result.value.has_value(), "invoker.has_value");
    auto *sv = result.value.has_value() ? std::get_if<StringValue>(&result.value->node) : nullptr;
    check(sv != nullptr && sv->value == "hello", "invoker.value_hello");

    // Unregistered capabilities preserve structured failure rather than being collapsed into NoneValue.
    auto missing = invoker("unknown", {});
    check(missing.status == CapabilityCallStatus::Error, "invoker.missing_status_error");
    check(!missing.value.has_value(), "invoker.missing_no_value");
    check(!missing.error_message.empty(), "invoker.missing_has_error_message");
}

// ============================================================================
// Test 7: http_capability_binding_construction
// ============================================================================

void test_http_capability_binding() {
    auto transport = std::make_shared<FakeCapabilityTransport>();
    transport->http_response = HttpResponse{
        .status_code = 503,
        .body = "unavailable",
        .error = {},
    };

    HTTPCapabilityConfig config;
    config.url = "https://example.com/api";
    config.method = "PUT";
    config.retry.max_retries = 3;
    config.retry.initial_delay = std::chrono::milliseconds{0};
    config.timeout.deadline = std::chrono::milliseconds{5000};
    auto binding = make_http_capability("http_call", std::move(config), transport);

    check(binding.name == "http_call", "http_binding.name");
    check(binding.retry.max_retries == 3, "http_binding.retry");
    check(binding.timeout.deadline == std::chrono::milliseconds{5000}, "http_binding.timeout");
    check(binding.handler != nullptr, "http_binding.has_handler");

    CapabilityRegistry registry;
    registry.register_capability(std::move(binding));
    auto result = registry.invoke("http_call", {});
    check(result.status == CapabilityCallStatus::RetryExhausted, "http_binding.retry_exhausted");
    check(result.error_message == "HTTP 503: unavailable", "http_binding.error_message");
    check(transport->http_requests.size() == 4, "http_binding.retry_request_count");
    if (!transport->http_requests.empty()) {
        check(transport->http_requests.front().url == "https://example.com/api",
              "http_binding.request_url");
        check(transport->http_requests.front().method == "PUT", "http_binding.request_method");
    }
}

void test_http_capability_injected_transport_success() {
    auto transport = std::make_shared<FakeCapabilityTransport>();
    transport->http_response = HttpResponse{
        .status_code = 200,
        .body = R"("ok")",
        .error = {},
    };

    HTTPCapabilityConfig config;
    config.url = "https://example.com/capability";
    config.timeout.deadline = std::chrono::milliseconds{2500};
    auto binding = make_http_capability("http_success", std::move(config), transport);

    CapabilityRegistry registry;
    registry.register_capability(std::move(binding));
    std::vector<Value> args;
    args.push_back(make_int(7));
    auto result = registry.invoke("http_success", args);

    check(result.status == CapabilityCallStatus::Success, "http_success.status");
    auto *value =
        result.value.has_value() ? std::get_if<StringValue>(&result.value->node) : nullptr;
    check(value != nullptr && value->value == "ok", "http_success.value");
    check(transport->http_requests.size() == 1, "http_success.request_captured");
    if (!transport->http_requests.empty()) {
        const auto &request = transport->http_requests.front();
        check(request.headers.count("Content-Type") == 1, "http_success.content_type");
        check(request.body.find("\"value\":7") != std::string::npos, "http_success.body");
        check(request.timeout_seconds == 2, "http_success.timeout_seconds");
    }
}

void test_http_capability_rejects_malformed_json_response() {
    auto transport = std::make_shared<FakeCapabilityTransport>();
    transport->http_response = HttpResponse{
        .status_code = 200,
        .body = "not json",
        .error = {},
    };

    HTTPCapabilityConfig config;
    config.url = "https://example.com/capability";
    auto binding = make_http_capability("http_malformed", std::move(config), transport);

    CapabilityRegistry registry;
    registry.register_capability(std::move(binding));
    auto result = registry.invoke("http_malformed", {});

    check(result.status == CapabilityCallStatus::Error, "http_malformed.status_error");
    check(!result.value.has_value(), "http_malformed.no_value");
    check(result.error_message == "invalid wire JSON response body",
          "http_malformed.error_message");
}

void test_http_capability_timeout_fails_closed() {
    auto transport = std::make_shared<FakeCapabilityTransport>();
    transport->http_response = HttpResponse{
        .status_code = 0,
        .body = {},
        .error = "operation timeout",
    };

    HTTPCapabilityConfig config;
    config.url = "https://example.com/capability";
    config.timeout.deadline = std::chrono::milliseconds{1500};
    auto binding = make_http_capability("http_timeout", std::move(config), transport);

    CapabilityRegistry registry;
    registry.register_capability(std::move(binding));
    auto result = registry.invoke("http_timeout", {});

    check(result.status == CapabilityCallStatus::Timeout, "http_timeout.status");
    check(!result.value.has_value(), "http_timeout.no_value");
    check(result.error_message == "HTTP request timed out", "http_timeout.message");
    check(transport->http_requests.size() == 1, "http_timeout.request_count");
    if (!transport->http_requests.empty()) {
        check(transport->http_requests.front().timeout_seconds == 1, "http_timeout.request_budget");
    }
}

void test_http_capability_rejects_response_schema_mismatch() {
    auto transport = std::make_shared<FakeCapabilityTransport>();
    transport->http_response = HttpResponse{
        .status_code = 200,
        .body = "42",
        .error = {},
    };

    HTTPCapabilityConfig config;
    config.url = "https://example.com/capability";
    config.response_schema = make_response_schema(TypeRefKind::String);
    auto binding = make_http_capability("http_schema_mismatch", std::move(config), transport);

    CapabilityRegistry registry;
    registry.register_capability(std::move(binding));
    auto result = registry.invoke("http_schema_mismatch", {});

    check(result.status == CapabilityCallStatus::Error, "http_schema_mismatch.status");
    check(!result.value.has_value(), "http_schema_mismatch.no_value");
    check(result.error_message.find(
              "response schema validation failed: expected String but got Int") !=
              std::string::npos,
          "http_schema_mismatch.message");
    check(transport->http_requests.size() == 1, "http_schema_mismatch.request_count");
}

void test_http_capability_text_plain_response() {
    auto transport = std::make_shared<FakeCapabilityTransport>();
    transport->http_response = HttpResponse{
        .status_code = 200,
        .body = "plain response",
        .error = {},
    };

    HTTPCapabilityConfig config;
    config.url = "https://example.com/capability";
    config.response_format = CapabilityResponseFormat::TextPlain;
    auto binding = make_http_capability("http_text", std::move(config), transport);

    CapabilityRegistry registry;
    registry.register_capability(std::move(binding));
    auto result = registry.invoke("http_text", {});

    check(result.status == CapabilityCallStatus::Success, "http_text.status");
    auto *value =
        result.value.has_value() ? std::get_if<StringValue>(&result.value->node) : nullptr;
    check(value != nullptr && value->value == "plain response", "http_text.value");
}

void test_http_capability_bearer_auth_header() {
    auto transport = std::make_shared<FakeCapabilityTransport>();
    transport->http_response = HttpResponse{
        .status_code = 200,
        .body = R"("ok")",
        .error = {},
    };

    HTTPCapabilityConfig config;
    config.url = "https://example.com/secure";
    config.auth = ahfl::secret::AuthConfig{
        .scheme = ahfl::secret::AuthScheme::BearerToken,
        .token_key = "HTTP_TOKEN",
    };
    config.secret_manager = make_secret_manager({{"HTTP_TOKEN", "token-123"}});

    auto binding = make_http_capability("http_auth", std::move(config), transport);
    CapabilityRegistry registry;
    registry.register_capability(std::move(binding));
    auto result = registry.invoke("http_auth", {});

    check(result.status == CapabilityCallStatus::Success, "http_auth.status");
    check(transport->http_requests.size() == 1, "http_auth.request_count");
    if (!transport->http_requests.empty()) {
        check(has_request_header(
                  transport->http_requests.front(), "Authorization", "Bearer token-123"),
              "http_auth.authorization_header");
    }
}

void test_http_capability_auth_missing_secret_fails_closed() {
    auto transport = std::make_shared<FakeCapabilityTransport>();
    transport->http_response = HttpResponse{
        .status_code = 200,
        .body = R"("should_not_call")",
        .error = {},
    };

    HTTPCapabilityConfig config;
    config.url = "https://example.com/secure";
    config.auth = ahfl::secret::AuthConfig{
        .scheme = ahfl::secret::AuthScheme::BearerToken,
        .token_key = "MISSING_TOKEN",
    };
    config.secret_manager = make_secret_manager({});

    auto binding = make_http_capability("http_auth_missing", std::move(config), transport);
    CapabilityRegistry registry;
    registry.register_capability(std::move(binding));
    auto result = registry.invoke("http_auth_missing", {});

    check(result.status == CapabilityCallStatus::Error, "http_auth_missing.status");
    check(result.error_message ==
              "HTTP capability auth failed: bearer token secret not found: MISSING_TOKEN",
          "http_auth_missing.message");
    check(transport->http_requests.empty(), "http_auth_missing.no_transport_call");
}

void test_http_capability_auth_missing_secret_manager_fails_closed() {
    auto transport = std::make_shared<FakeCapabilityTransport>();
    transport->http_response = HttpResponse{
        .status_code = 200,
        .body = R"("should_not_call")",
        .error = {},
    };

    HTTPCapabilityConfig config;
    config.url = "https://example.com/secure";
    config.auth = ahfl::secret::AuthConfig{
        .scheme = ahfl::secret::AuthScheme::BearerToken,
        .token_key = "HTTP_TOKEN",
    };

    auto binding = make_http_capability("http_auth_no_manager", std::move(config), transport);
    CapabilityRegistry registry;
    registry.register_capability(std::move(binding));
    auto result = registry.invoke("http_auth_no_manager", {});

    check(result.status == CapabilityCallStatus::Error, "http_auth_no_manager.status");
    check(result.error_message == "HTTP capability auth failed: secret manager is required",
          "http_auth_no_manager.message");
    check(transport->http_requests.empty(), "http_auth_no_manager.no_transport_call");
}

void test_http_capability_mtls_auth_paths() {
    auto transport = std::make_shared<FakeCapabilityTransport>();
    transport->http_response = HttpResponse{
        .status_code = 200,
        .body = R"("ok")",
        .error = {},
    };

    HTTPCapabilityConfig config;
    config.url = "https://example.com/secure";
    config.auth = ahfl::secret::AuthConfig{
        .scheme = ahfl::secret::AuthScheme::MTLS,
        .cert_path_key = "CERT_PATH",
        .key_path_key = "KEY_PATH",
    };
    config.secret_manager =
        make_secret_manager({{"CERT_PATH", "/tmp/client.pem"}, {"KEY_PATH", "/tmp/client.key"}});

    auto binding = make_http_capability("http_mtls", std::move(config), transport);
    CapabilityRegistry registry;
    registry.register_capability(std::move(binding));
    auto result = registry.invoke("http_mtls", {});

    check(result.status == CapabilityCallStatus::Success, "http_mtls.status");
    check(transport->http_requests.size() == 1, "http_mtls.request_count");
    if (!transport->http_requests.empty()) {
        const auto &request = transport->http_requests.front();
        check(request.tls_client_certificate_path == "/tmp/client.pem", "http_mtls.cert_path");
        check(request.tls_client_key_path == "/tmp/client.key", "http_mtls.key_path");
        check(request.headers.find("Authorization") == request.headers.end(),
              "http_mtls.no_bearer");
    }
}

// ============================================================================
// Test 8: grpc_capability_json_transcoding
// ============================================================================

void test_grpc_capability_json_transcoding() {
    auto transport = std::make_shared<FakeCapabilityTransport>();
    transport->grpc_response = GrpcJsonTranscodingResponse{
        .status_code = GrpcStatusCode::Unavailable,
        .body = {},
        .error_message = "unavailable",
    };

    GrpcJsonTranscodingCapabilityConfig config;
    config.endpoint = "http://localhost:50051";
    config.service = "TestService";
    config.method = "TestMethod";
    config.retry.max_retries = 1;
    auto binding = make_grpc_json_transcoding_capability("grpc_call", std::move(config), transport);

    check(binding.name == "grpc_call", "grpc_transcoding.name");
    check(binding.retry.max_retries == 1, "grpc_transcoding.retry");
    check(binding.handler != nullptr, "grpc_transcoding.has_handler");

    CapabilityRegistry registry;
    registry.register_capability(std::move(binding));
    auto result = registry.invoke("grpc_call", {});
    check(result.status == CapabilityCallStatus::RetryExhausted,
          "grpc_transcoding.retry_exhausted");
    check(result.error_message == "unavailable", "grpc_transcoding.has_error");
    check(transport->grpc_requests.size() == 2, "grpc_transcoding.retry_request_count");
    if (!transport->grpc_requests.empty()) {
        const auto &request = transport->grpc_requests.front();
        check(request.endpoint.host == "localhost", "grpc_transcoding.host");
        check(request.endpoint.port == 50051, "grpc_transcoding.port");
        check(request.endpoint.service_name == "TestService", "grpc_transcoding.service");
        check(request.endpoint.method_name == "TestMethod", "grpc_transcoding.method");
    }
}

void test_grpc_capability_injected_transport_success() {
    auto transport = std::make_shared<FakeCapabilityTransport>();
    transport->grpc_response = GrpcJsonTranscodingResponse{
        .status_code = GrpcStatusCode::Ok,
        .body = "42",
        .error_message = {},
    };

    GrpcJsonTranscodingCapabilityConfig config;
    config.endpoint = "https://grpc.example.com";
    config.service = "Example.Service";
    config.method = "Compute";
    config.timeout.deadline = std::chrono::milliseconds{3000};
    auto binding =
        make_grpc_json_transcoding_capability("grpc_success", std::move(config), transport);

    CapabilityRegistry registry;
    registry.register_capability(std::move(binding));
    std::vector<Value> args;
    args.push_back(make_string("payload"));
    auto result = registry.invoke("grpc_success", args);

    check(result.status == CapabilityCallStatus::Success, "grpc_success.status");
    auto *value = result.value.has_value() ? std::get_if<IntValue>(&result.value->node) : nullptr;
    check(value != nullptr && value->value == 42, "grpc_success.value");
    check(transport->grpc_requests.size() == 1, "grpc_success.request_captured");
    if (!transport->grpc_requests.empty()) {
        const auto &request = transport->grpc_requests.front();
        check(request.endpoint.host == "grpc.example.com", "grpc_success.host");
        check(request.endpoint.port == 443, "grpc_success.port");
        check(request.endpoint.use_tls, "grpc_success.tls");
        check(request.endpoint.service_name == "Example.Service", "grpc_success.service");
        check(request.endpoint.method_name == "Compute", "grpc_success.method");
        check(request.serialized_body.find("\"value\":\"payload\"") != std::string::npos,
              "grpc_success.body");
        check(request.timeout == std::chrono::seconds{3}, "grpc_success.timeout");
    }
}

void test_grpc_capability_rejects_malformed_json_response() {
    auto transport = std::make_shared<FakeCapabilityTransport>();
    transport->grpc_response = GrpcJsonTranscodingResponse{
        .status_code = GrpcStatusCode::Ok,
        .body = "not json",
        .error_message = {},
    };

    GrpcJsonTranscodingCapabilityConfig config;
    config.endpoint = "https://grpc.example.com";
    config.service = "Example.Service";
    config.method = "Compute";
    auto binding =
        make_grpc_json_transcoding_capability("grpc_malformed", std::move(config), transport);

    CapabilityRegistry registry;
    registry.register_capability(std::move(binding));
    auto result = registry.invoke("grpc_malformed", {});

    check(result.status == CapabilityCallStatus::Error, "grpc_malformed.status_error");
    check(!result.value.has_value(), "grpc_malformed.no_value");
    check(result.error_message == "invalid wire JSON response body",
          "grpc_malformed.error_message");
}

void test_grpc_capability_timeout_fails_closed() {
    auto transport = std::make_shared<FakeCapabilityTransport>();
    transport->grpc_response = GrpcJsonTranscodingResponse{
        .status_code = GrpcStatusCode::DeadlineExceeded,
        .body = {},
        .error_message = "deadline exceeded",
    };

    GrpcJsonTranscodingCapabilityConfig config;
    config.endpoint = "https://grpc.example.com";
    config.service = "Example.Service";
    config.method = "Compute";
    config.timeout.deadline = std::chrono::milliseconds{2500};
    auto binding = make_grpc_json_transcoding_capability(
        "grpc_timeout", std::move(config), transport);

    CapabilityRegistry registry;
    registry.register_capability(std::move(binding));
    auto result = registry.invoke("grpc_timeout", {});

    check(result.status == CapabilityCallStatus::Timeout, "grpc_timeout.status");
    check(!result.value.has_value(), "grpc_timeout.no_value");
    check(result.error_message == "deadline exceeded", "grpc_timeout.message");
    check(transport->grpc_requests.size() == 1, "grpc_timeout.request_count");
    if (!transport->grpc_requests.empty()) {
        check(transport->grpc_requests.front().timeout == std::chrono::seconds{2},
              "grpc_timeout.request_budget");
    }
}

void test_grpc_capability_rejects_response_schema_mismatch() {
    auto transport = std::make_shared<FakeCapabilityTransport>();
    transport->grpc_response = GrpcJsonTranscodingResponse{
        .status_code = GrpcStatusCode::Ok,
        .body = R"("not an int")",
        .error_message = {},
    };

    GrpcJsonTranscodingCapabilityConfig config;
    config.endpoint = "https://grpc.example.com";
    config.service = "Example.Service";
    config.method = "Compute";
    config.response_schema = make_response_schema(TypeRefKind::Int);
    auto binding = make_grpc_json_transcoding_capability(
        "grpc_schema_mismatch", std::move(config), transport);

    CapabilityRegistry registry;
    registry.register_capability(std::move(binding));
    auto result = registry.invoke("grpc_schema_mismatch", {});

    check(result.status == CapabilityCallStatus::Error, "grpc_schema_mismatch.status");
    check(!result.value.has_value(), "grpc_schema_mismatch.no_value");
    check(result.error_message.find(
              "response schema validation failed: expected Int but got String") !=
              std::string::npos,
          "grpc_schema_mismatch.message");
    check(transport->grpc_requests.size() == 1, "grpc_schema_mismatch.request_count");
}

void test_grpc_capability_trailer_status_overrides_http_ok() {
    auto transport = std::make_shared<FakeCapabilityTransport>();
    transport->grpc_response = GrpcJsonTranscodingResponse{
        .status_code = GrpcStatusCode::Ok,
        .body = R"("ignored")",
        .error_message = {},
        .response_metadata = {{"content-type", "application/json"}},
        .trailers = {{"grpc-status", "7"}, {"grpc-message", "permission%20denied"}},
    };

    GrpcJsonTranscodingCapabilityConfig config;
    config.endpoint = "https://grpc.example.com";
    config.service = "Example.Service";
    config.method = "Compute";
    auto binding = make_grpc_json_transcoding_capability(
        "grpc_trailer_status", std::move(config), transport);

    CapabilityRegistry registry;
    registry.register_capability(std::move(binding));
    auto result = registry.invoke("grpc_trailer_status", {});

    check(result.status == CapabilityCallStatus::Error, "grpc_trailer_status.status_error");
    check(result.error_message == "permission denied", "grpc_trailer_status.message");
    check(transport->grpc_requests.size() == 1, "grpc_trailer_status.request_count");
}

void test_grpc_capability_metadata_status_overrides_http_ok() {
    auto transport = std::make_shared<FakeCapabilityTransport>();
    transport->grpc_response = GrpcJsonTranscodingResponse{
        .status_code = GrpcStatusCode::Ok,
        .body = R"("ignored")",
        .error_message = {},
        .response_metadata = {{"grpc-status", "3"}, {"grpc-message", "invalid%20input"}},
        .trailers = {},
    };

    GrpcJsonTranscodingCapabilityConfig config;
    config.endpoint = "https://grpc.example.com";
    config.service = "Example.Service";
    config.method = "Compute";
    auto binding = make_grpc_json_transcoding_capability(
        "grpc_metadata_status", std::move(config), transport);

    CapabilityRegistry registry;
    registry.register_capability(std::move(binding));
    auto result = registry.invoke("grpc_metadata_status", {});

    check(result.status == CapabilityCallStatus::Error, "grpc_metadata_status.status_error");
    check(result.error_message == "invalid input", "grpc_metadata_status.message");
    check(transport->grpc_requests.size() == 1, "grpc_metadata_status.request_count");
}

void test_grpc_capability_auth_missing_secret_fails_closed() {
    auto transport = std::make_shared<FakeCapabilityTransport>();
    transport->grpc_response = GrpcJsonTranscodingResponse{
        .status_code = GrpcStatusCode::Ok,
        .body = R"("should_not_call")",
        .error_message = {},
    };

    GrpcJsonTranscodingCapabilityConfig config;
    config.endpoint = "https://grpc.example.com";
    config.service = "Example.Service";
    config.method = "Compute";
    config.auth = ahfl::secret::AuthConfig{
        .scheme = ahfl::secret::AuthScheme::BearerToken,
        .token_key = "MISSING_TOKEN",
    };
    config.secret_manager = make_secret_manager({});

    auto binding =
        make_grpc_json_transcoding_capability("grpc_auth_missing", std::move(config), transport);
    CapabilityRegistry registry;
    registry.register_capability(std::move(binding));
    auto result = registry.invoke("grpc_auth_missing", {});

    check(result.status == CapabilityCallStatus::Error, "grpc_auth_missing.status");
    check(result.error_message ==
              "gRPC capability auth failed: bearer token secret not found: MISSING_TOKEN",
          "grpc_auth_missing.message");
    check(transport->grpc_requests.empty(), "grpc_auth_missing.no_transport_call");
}

void test_grpc_capability_auth_missing_secret_manager_fails_closed() {
    auto transport = std::make_shared<FakeCapabilityTransport>();
    transport->grpc_response = GrpcJsonTranscodingResponse{
        .status_code = GrpcStatusCode::Ok,
        .body = R"("should_not_call")",
        .error_message = {},
    };

    GrpcJsonTranscodingCapabilityConfig config;
    config.endpoint = "https://grpc.example.com";
    config.service = "Example.Service";
    config.method = "Compute";
    config.auth = ahfl::secret::AuthConfig{
        .scheme = ahfl::secret::AuthScheme::BearerToken,
        .token_key = "GRPC_TOKEN",
    };

    auto binding =
        make_grpc_json_transcoding_capability("grpc_auth_no_manager", std::move(config), transport);
    CapabilityRegistry registry;
    registry.register_capability(std::move(binding));
    auto result = registry.invoke("grpc_auth_no_manager", {});

    check(result.status == CapabilityCallStatus::Error, "grpc_auth_no_manager.status");
    check(result.error_message == "gRPC capability auth failed: secret manager is required",
          "grpc_auth_no_manager.message");
    check(transport->grpc_requests.empty(), "grpc_auth_no_manager.no_transport_call");
}

void test_grpc_capability_bearer_auth_metadata() {
    auto transport = std::make_shared<FakeCapabilityTransport>();
    transport->grpc_response = GrpcJsonTranscodingResponse{
        .status_code = GrpcStatusCode::Ok,
        .body = R"("ok")",
        .error_message = {},
    };

    GrpcJsonTranscodingCapabilityConfig config;
    config.endpoint = "https://grpc.example.com";
    config.service = "Example.Service";
    config.method = "Compute";
    config.auth = ahfl::secret::AuthConfig{
        .scheme = ahfl::secret::AuthScheme::BearerToken,
        .token_key = "GRPC_TOKEN",
    };
    config.secret_manager = make_secret_manager({{"GRPC_TOKEN", "grpc-token"}});

    auto binding = make_grpc_json_transcoding_capability("grpc_auth", std::move(config), transport);
    CapabilityRegistry registry;
    registry.register_capability(std::move(binding));
    auto result = registry.invoke("grpc_auth", {});

    check(result.status == CapabilityCallStatus::Success, "grpc_auth.status");
    check(transport->grpc_requests.size() == 1, "grpc_auth.request_count");
    if (!transport->grpc_requests.empty()) {
        check(has_request_metadata(
                  transport->grpc_requests.front(), "Authorization", "Bearer grpc-token"),
              "grpc_auth.authorization_metadata");
    }
}

void test_grpc_capability_mtls_auth_paths() {
    auto transport = std::make_shared<FakeCapabilityTransport>();
    transport->grpc_response = GrpcJsonTranscodingResponse{
        .status_code = GrpcStatusCode::Ok,
        .body = R"("ok")",
        .error_message = {},
    };

    GrpcJsonTranscodingCapabilityConfig config;
    config.endpoint = "https://grpc.example.com";
    config.service = "Example.Service";
    config.method = "Compute";
    config.auth = ahfl::secret::AuthConfig{
        .scheme = ahfl::secret::AuthScheme::MTLS,
        .cert_path_key = "CERT_PATH",
        .key_path_key = "KEY_PATH",
    };
    config.secret_manager = make_secret_manager(
        {{"CERT_PATH", "/tmp/grpc-client.pem"}, {"KEY_PATH", "/tmp/grpc-client.key"}});

    auto binding = make_grpc_json_transcoding_capability("grpc_mtls", std::move(config), transport);
    CapabilityRegistry registry;
    registry.register_capability(std::move(binding));
    auto result = registry.invoke("grpc_mtls", {});

    check(result.status == CapabilityCallStatus::Success, "grpc_mtls.status");
    check(transport->grpc_requests.size() == 1, "grpc_mtls.request_count");
    if (!transport->grpc_requests.empty()) {
        const auto &request = transport->grpc_requests.front();
        check(request.tls_client_certificate_path == "/tmp/grpc-client.pem", "grpc_mtls.cert_path");
        check(request.tls_client_key_path == "/tmp/grpc-client.key", "grpc_mtls.key_path");
        check(!has_request_metadata_name(request, "Authorization"), "grpc_mtls.no_bearer");
    }
}

// ============================================================================
// Test 11: circuit_breaker_state_transitions
// ============================================================================

void test_circuit_breaker_state_transitions() {
    CircuitBreakerConfig cb_config;
    cb_config.failure_threshold = 3;
    cb_config.recovery_window = std::chrono::seconds{1};
    cb_config.enabled = true;

    // Create a capability that always fails
    int call_count = 0;
    CapabilityBinding binding;
    binding.name = "cb_test";
    binding.retry.max_retries = 0; // no retries — one call per invoke
    binding.circuit_breaker = cb_config;
    binding.circuit_state = std::make_shared<CircuitBreakerState>(cb_config);
    binding.handler = [&call_count](const std::vector<Value> & /*args*/) -> CapabilityCallResult {
        ++call_count;
        return CapabilityCallResult{
            .status = CapabilityCallStatus::Error,
            .value = std::nullopt,
            .error_message = "always fails",
            .attempts = 1,
        };
    };

    CapabilityRegistry registry;
    registry.register_capability(std::move(binding));

    // First 3 calls should go through (failure_threshold = 3)
    auto r1 = registry.invoke("cb_test", {});
    check(r1.status == CapabilityCallStatus::Error, "cb.r1_error");
    auto r2 = registry.invoke("cb_test", {});
    check(r2.status == CapabilityCallStatus::Error, "cb.r2_error");
    auto r3 = registry.invoke("cb_test", {});
    check(r3.status == CapabilityCallStatus::Error, "cb.r3_error");
    check(call_count == 3, "cb.3_calls_made");

    // 4th call: circuit should be open now
    auto r4 = registry.invoke("cb_test", {});
    check(r4.status == CapabilityCallStatus::CircuitOpen, "cb.r4_circuit_open");
    check(call_count == 3, "cb.no_4th_call"); // handler was NOT called
}

// ============================================================================
// Test 9: multiple_capabilities
// ============================================================================

void test_multiple_capabilities() {
    CapabilityRegistry registry;
    registry.register_mock("cap_a", make_int(1));
    registry.register_mock("cap_b", make_int(2));
    registry.register_mock("cap_c", make_int(3));

    check(registry.has("cap_a"), "multi.has_a");
    check(registry.has("cap_b"), "multi.has_b");
    check(registry.has("cap_c"), "multi.has_c");
    check(!registry.has("cap_d"), "multi.not_has_d");

    auto names = registry.registered_names();
    check(names.size() == 3, "multi.names_count_3");

    auto result_a = registry.invoke("cap_a", {});
    auto result_b = registry.invoke("cap_b", {});
    auto result_c = registry.invoke("cap_c", {});

    check(result_a.status == CapabilityCallStatus::Success, "multi.a_success");
    check(result_b.status == CapabilityCallStatus::Success, "multi.b_success");
    check(result_c.status == CapabilityCallStatus::Success, "multi.c_success");

    if (result_a.value.has_value()) {
        auto *iv = std::get_if<IntValue>(&result_a.value->node);
        check(iv != nullptr && iv->value == 1, "multi.a_value_1");
    }
}

// ============================================================================
// Test 10: eval_with_capability_call
// ============================================================================

void test_eval_with_capability_call() {
    // Build an IR CallExpr
    Expr expr;
    CallExpr call;
    call.callee = "my_capability";

    // Append a string argument
    call.arguments.push_back(make_expr_ptr(StringLiteralExpr{"hello"}));
    expr.node = std::move(call);

    // Configure the registry
    CapabilityRegistry registry;
    registry.register_function("my_capability", [](const std::vector<Value> &args) -> Value {
        if (!args.empty()) {
            if (auto *sv = std::get_if<StringValue>(&args[0].node)) {
                return make_string(sv->value + "_processed");
            }
        }
        return make_none();
    });

    evaluator::EvalContext eval_ctx;

    auto result = eval_expr_with_capabilities(expr, eval_ctx, &registry);

    check(!result.has_errors(), "eval_cap.no_errors");
    auto *sv = std::get_if<StringValue>(&result.value.node);
    check(sv != nullptr && sv->value == "hello_processed", "eval_cap.value_processed");

    auto invoker = registry.as_invoker();
    auto invoker_result = eval_expr_with_capabilities(expr, eval_ctx, invoker);
    check(!invoker_result.has_errors(), "eval_cap.invoker_no_errors");
    auto *invoker_sv = std::get_if<StringValue>(&invoker_result.value.node);
    check(invoker_sv != nullptr && invoker_sv->value == "hello_processed",
          "eval_cap.invoker_value_processed");

    registry.register_function("inner_cap", [](const std::vector<Value> & /*args*/) -> Value {
        return make_string("inner");
    });
    registry.register_function("outer_cap", [](const std::vector<Value> &args) -> Value {
        if (!args.empty()) {
            if (auto *arg = std::get_if<StringValue>(&args[0].node)) {
                return make_string(arg->value + "_outer");
            }
        }
        return make_none();
    });

    Expr nested_expr;
    CallExpr outer_call;
    outer_call.callee = "outer_cap";
    CallExpr inner_call;
    inner_call.callee = "inner_cap";
    outer_call.arguments.push_back(make_expr_ptr(std::move(inner_call)));
    nested_expr.node = std::move(outer_call);

    auto nested_result = eval_expr_with_capabilities(nested_expr, eval_ctx, registry.as_invoker());
    check(!nested_result.has_errors(), "eval_cap.nested_invoker_no_errors");
    auto *nested_sv = std::get_if<StringValue>(&nested_result.value.node);
    check(nested_sv != nullptr && nested_sv->value == "inner_outer",
          "eval_cap.nested_invoker_value");

    registry.register_function(
        "is_ready", [](const std::vector<Value> & /*args*/) -> Value { return make_bool(true); });

    Expr binary_expr;
    BinaryExpr equality;
    equality.op = ExprBinaryOp::Equal;
    CallExpr ready_call;
    ready_call.callee = "is_ready";
    equality.lhs = make_expr_ptr(std::move(ready_call));
    equality.rhs = make_expr_ptr(BoolLiteralExpr{true});
    binary_expr.node = std::move(equality);

    auto binary_result = eval_expr_with_capabilities(binary_expr, eval_ctx, registry.as_invoker());
    check(!binary_result.has_errors(), "eval_cap.binary_call_no_errors");
    auto *binary_bool = std::get_if<BoolValue>(&binary_result.value.node);
    check(binary_bool != nullptr && binary_bool->value, "eval_cap.binary_call_value");

    Expr optional_expr;
    CallExpr optional_call;
    optional_call.callee = "inner_cap";
    CallExpr some_call;
    some_call.callee = "std::option::Option::Some";
    some_call.arguments.push_back(make_expr_ptr(std::move(optional_call)));
    optional_expr.node = std::move(some_call);

    auto optional_result =
        eval_expr_with_capabilities(optional_expr, eval_ctx, registry.as_invoker());
    check(!optional_result.has_errors(), "eval_cap.optional_call_no_errors");
    // P5.11a + P5.11b dual-aware: nominal accessor optional_inner() covers
    // both legacy OptionalValue and nominal EnumValue Option.
    const auto *optional_result_inner = ahfl::evaluator::optional_inner(optional_result.value);
    auto *optional_inner = optional_result_inner != nullptr
                               ? std::get_if<StringValue>(&optional_result_inner->node)
                               : nullptr;
    check(optional_inner != nullptr && optional_inner->value == "inner",
          "eval_cap.optional_call_value");

    Expr struct_expr;
    StructLiteralExpr literal;
    literal.type_name = "CapabilityResult";
    CallExpr field_call;
    field_call.callee = "inner_cap";
    literal.fields.push_back(StructFieldInit{
        .name = "value",
        .value = make_expr_ptr(std::move(field_call)),
    });
    struct_expr.node = std::move(literal);

    auto struct_result = eval_expr_with_capabilities(struct_expr, eval_ctx, registry.as_invoker());
    check(!struct_result.has_errors(), "eval_cap.struct_call_no_errors");
    auto *struct_value = std::get_if<StructValue>(&struct_result.value.node);
    const StringValue *field_string = nullptr;
    if (struct_value != nullptr) {
        auto value_field = struct_value->fields.find("value");
        if (value_field != struct_value->fields.end() && value_field->second) {
            field_string = std::get_if<StringValue>(&value_field->second->node);
        }
    }
    check(field_string != nullptr && field_string->value == "inner", "eval_cap.struct_call_value");

    // Test that a null registry reports an error
    auto null_result = eval_expr_with_capabilities(expr, eval_ctx, nullptr);
    check(null_result.has_errors(), "eval_cap.null_registry_error");

    // Test that an unregistered capability reports an error
    Expr expr2;
    CallExpr call2;
    call2.callee = "unknown_cap";
    expr2.node = std::move(call2);

    auto unknown_result = eval_expr_with_capabilities(expr2, eval_ctx, &registry);
    check(unknown_result.has_errors(), "eval_cap.unknown_cap_error");
}

} // anonymous namespace

int main() {
    test_register_and_invoke_mock();
    test_register_and_invoke_function();
    test_invoke_not_found();
    test_retry_success_on_second();
    test_retry_exhausted();
    test_as_invoker_preserves_structured_result();
    test_http_capability_binding();
    test_http_capability_injected_transport_success();
    test_http_capability_rejects_malformed_json_response();
    test_http_capability_timeout_fails_closed();
    test_http_capability_rejects_response_schema_mismatch();
    test_http_capability_text_plain_response();
    test_http_capability_bearer_auth_header();
    test_http_capability_auth_missing_secret_fails_closed();
    test_http_capability_auth_missing_secret_manager_fails_closed();
    test_http_capability_mtls_auth_paths();
    test_grpc_capability_json_transcoding();
    test_grpc_capability_injected_transport_success();
    test_grpc_capability_rejects_malformed_json_response();
    test_grpc_capability_timeout_fails_closed();
    test_grpc_capability_rejects_response_schema_mismatch();
    test_grpc_capability_trailer_status_overrides_http_ok();
    test_grpc_capability_metadata_status_overrides_http_ok();
    test_grpc_capability_auth_missing_secret_fails_closed();
    test_grpc_capability_auth_missing_secret_manager_fails_closed();
    test_grpc_capability_bearer_auth_metadata();
    test_grpc_capability_mtls_auth_paths();
    test_circuit_breaker_state_transitions();
    test_multiple_capabilities();
    test_eval_with_capability_call();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
