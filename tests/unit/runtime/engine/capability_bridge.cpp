#include "runtime/engine/capability_bridge.hpp"
#include "ahfl/compiler/ir/core_wire_migration.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "runtime/engine/capability_eval.hpp"
#include "runtime/value/value.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
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
    // A distinctive JSON-number body (an Int under a String binding) doubles as a
    // no-echo probe: this exact token must never appear in the diagnostic.
    const std::string secret_body = "424242424242";
    transport->http_response = HttpResponse{
        .status_code = 200,
        .body = secret_body,
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
              "response schema validation failed: wire-codec: expected string") !=
              std::string::npos,
          "http_schema_mismatch.message");
    check(result.error_message.find(secret_body) == std::string::npos,
          "http_schema_mismatch.no_payload_echo");
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
    // A distinctive JSON string body (under an Int binding) doubles as a no-echo
    // probe: this exact token must never appear in the diagnostic.
    const std::string secret_token = "SECRET_GRPC_BODY_9f3a";
    transport->grpc_response = GrpcJsonTranscodingResponse{
        .status_code = GrpcStatusCode::Ok,
        .body = std::string("\"") + secret_token + "\"",
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
    // The response now flows through the exact wire codec (G4a): a JSON string body
    // under an Int-rooted binding is rejected with the schema-only codec text.
    check(result.error_message.find(
              "response schema validation failed: wire-codec: expected integer") !=
              std::string::npos,
          "grpc_schema_mismatch.message");
    check(result.error_message.find(secret_token) == std::string::npos,
          "grpc_schema_mismatch.no_payload_echo");
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
    // Option is represented canonically as a nominal EnumValue.
    const auto *optional_result_inner = ahfl::runtime::optional_inner(optional_result.value);
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
        if (value_field != struct_value->fields.end() && value_field->value) {
            field_string = std::get_if<StringValue>(&value_field->value->node);
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

    Expr ranged_expr;
    ranged_expr.source_range =
        ahfl::SourceRange{.begin_offset = 10, .end_offset = 20};
    CallExpr failing_call;
    failing_call.callee = "failing_cap";
    ranged_expr.node = std::move(failing_call);
    CapabilityInvoker failing_invoker =
        [](const std::string &, const std::vector<Value> &) -> CapabilityCallResult {
        return CapabilityCallResult{
            .status = CapabilityCallStatus::Error,
            .error_message = "provider unavailable",
        };
    };
    auto ranged_result =
        eval_expr_with_capabilities(ranged_expr, eval_ctx, failing_invoker);
    check(ranged_result.has_errors(), "eval_cap.ranged_failure_has_error");
    const auto &ranged_diagnostics = ranged_result.diagnostics.entries();
    check(!ranged_diagnostics.empty() && ranged_diagnostics.front().range.has_value() &&
              ranged_diagnostics.front().range->begin_offset == 10 &&
              ranged_diagnostics.front().range->end_offset == 20,
          "eval_cap.ranged_failure_preserves_call_site");
}

// ============================================================================
// Bare @builtin hooks under the capability-bridged dispatcher.
//
// The frontend lowers stdlib collection intrinsics (`xs.length`, `xs[i]`) to
// UNQUALIFIED CallExpr callees -- "list_raw_length" / "list_raw_get" -- which
// the default evaluator dispatcher serves from the global builtin table. The
// capability-bridged dispatcher (AgentRuntime/WorkflowRuntime always install
// one) used to route ONLY "std::"-prefixed callees to that table, so a bare
// hook fell through to the capability registry and failed with "unknown
// capability" for capability-free agents (the P6 conformance p6_collection
// failure). Bare hooks must resolve through the builtin table regardless of
// the "std::" prefix, both through a registry and through a plain invoker.
// ============================================================================
void test_eval_bare_builtin_hook_under_capability_dispatch() {
    evaluator::EvalContext eval_ctx;
    {
        std::vector<Value> xs;
        xs.push_back(make_int(90));
        xs.push_back(make_int(1));
        eval_ctx.bind_local("xs", make_list(std::move(xs)));
    }

    auto call_with_path_arg = [](std::string callee, std::string local,
                                 std::optional<int64_t> index) {
        CallExpr call;
        call.callee = std::move(callee);
        PathExpr path_node;
        path_node.path.root_name = std::move(local);
        call.arguments.push_back(make_expr_ptr(std::move(path_node)));
        if (index.has_value()) {
            call.arguments.push_back(
                make_expr_ptr(IntegerLiteralExpr{std::to_string(*index)}));
        }
        Expr root;
        root.node = std::move(call);
        return root;
    };

    CapabilityRegistry empty_registry;

    Expr length_expr = call_with_path_arg("list_raw_length", "xs", std::nullopt);
    auto length_result = eval_expr_with_capabilities(length_expr, eval_ctx, &empty_registry);
    check(!length_result.has_errors(), "bare_hook.length_no_errors");
    auto *length_int = std::get_if<IntValue>(&length_result.value.node);
    check(length_int != nullptr && length_int->value == 2, "bare_hook.length_is_2");

    Expr get_expr = call_with_path_arg("list_raw_get", "xs", int64_t{0});
    auto get_result = eval_expr_with_capabilities(get_expr, eval_ctx, empty_registry.as_invoker());
    check(!get_result.has_errors(), "bare_hook.get_no_errors");
    auto *get_int = std::get_if<IntValue>(&get_result.value.node);
    check(get_int != nullptr && get_int->value == 90, "bare_hook.get0_is_90");

    // A genuinely unknown bare callee still fails through the registry; the
    // builtin shortcut must not swallow non-builtin names.
    Expr unknown_expr;
    CallExpr unknown_call;
    unknown_call.callee = "not_a_capability_or_builtin";
    unknown_expr.node = std::move(unknown_call);
    auto unknown_result =
        eval_expr_with_capabilities(unknown_expr, eval_ctx, &empty_registry);
    check(unknown_result.has_errors(), "bare_hook.unknown_still_errors");
}

} // anonymous namespace

namespace {

// ============================================================================
// RFC 0026 C2b G4a: verified response-binding fixtures + admission/poison/exact
// decode matrix. Bindings are projected from a real Program+TypeRef via the SAME
// production migration path (build_core_type_environment + migrate_type_ref_to_
// wire_binding) — no test-only production API.
// ============================================================================

// Build a capability whose return type is `return_type`, into a Program, and
// project its verified wire-schema binding. Returns nullopt if the type does not
// project (used for negative fixtures too).
[[nodiscard]] std::optional<ir::core::VerifiedWireSchemaBinding>
project_return_binding(TypeRef return_type) {
    Program program;
    CapabilityDecl cap;
    cap.name = "probe";
    cap.return_type_ref = std::move(return_type);
    cap.symbol_ref = SymbolRef{.kind = SymbolRefKind::Capability,
                               .canonical_name = "probe",
                               .local_name = "probe",
                               .id = 7};
    program.declarations.push_back(std::move(cap));

    auto env = ir::core::build_core_type_environment(program);
    if (!env.ok() || !env.environment.has_value()) {
        return std::nullopt;
    }
    const CapabilityDecl *decl = nullptr;
    for (const auto &d : program.declarations) {
        if (const auto *c = std::get_if<CapabilityDecl>(&d); c != nullptr && c->name == "probe") {
            decl = c;
            break;
        }
    }
    if (decl == nullptr) {
        return std::nullopt;
    }
    auto migration =
        ir::core::migrate_type_ref_to_wire_binding(decl->return_type_ref, *env.environment);
    if (!migration.ok() || !migration.binding.has_value()) {
        return std::nullopt;
    }
    return std::move(*migration.binding);
}

[[nodiscard]] TypeRef prim_type(TypeRefKind kind) {
    TypeRef t;
    t.kind = kind;
    return t;
}

[[nodiscard]] TypeRef bounded_string_type(std::int64_t min_len, std::int64_t max_len) {
    TypeRef t;
    t.kind = TypeRefKind::BoundedString;
    t.string_bounds = std::pair<std::int64_t, std::int64_t>{min_len, max_len};
    return t;
}

// Builtin-nominal TypeRef builders (descriptor-backed, resolve by canonical_name;
// same spelling the migration test uses). Reuse the migrator's builtin nominal
// table rather than a heavy user-decl fixture.
[[nodiscard]] TypeRef option_of(TypeRef inner) {
    TypeRef t;
    t.kind = TypeRefKind::Enum;
    t.canonical_name = "std::option::Option";
    t.nominal_ref.kind = SymbolRefKind::Type;
    t.nominal_ref.canonical_name = "std::option::Option";
    t.params.push_back(std::make_unique<TypeRef>(std::move(inner)));
    return t;
}

[[nodiscard]] TypeRef set_of(TypeRef element) {
    TypeRef t;
    t.kind = TypeRefKind::Struct;
    t.canonical_name = "std::collections::Set";
    t.nominal_ref.kind = SymbolRefKind::Type;
    t.nominal_ref.canonical_name = "std::collections::Set";
    t.params.push_back(std::make_unique<TypeRef>(std::move(element)));
    return t;
}

[[nodiscard]] TypeRef map_of(TypeRef key, TypeRef value) {
    TypeRef t;
    t.kind = TypeRefKind::Struct;
    t.canonical_name = "std::collections::Map";
    t.nominal_ref.kind = SymbolRefKind::Type;
    t.nominal_ref.canonical_name = "std::collections::Map";
    t.params.push_back(std::make_unique<TypeRef>(std::move(key)));
    t.params.push_back(std::make_unique<TypeRef>(std::move(value)));
    return t;
}

// G4a: a poison binding (four-state conflict: both legacy schema AND verified
// binding present) fails closed BEFORE transport, with retry=0/CB-off, and never
// becomes RetryExhausted even if the config requested retries / a circuit breaker.
void test_http_poison_conflict_fails_closed_pre_transport() {
    auto transport = std::make_shared<FakeCapabilityTransport>();
    transport->http_response = HttpResponse{.status_code = 200, .body = "42", .error = {}};

    auto binding_opt = project_return_binding(prim_type(TypeRefKind::Int));
    check(binding_opt.has_value(), "http_poison.fixture_projected");
    if (!binding_opt.has_value()) {
        return;
    }

    HTTPCapabilityConfig config;
    config.url = "https://example.com/capability";
    config.retry = RetryConfig{.max_retries = 3}; // would retry if reached
    config.circuit_breaker = CircuitBreakerConfig{.failure_threshold = 1, .enabled = true};
    config.response_schema = make_response_schema(TypeRefKind::Int); // legacy schema
    config.response_wire_binding = *binding_opt;                     // AND verified binding
    auto binding = make_http_capability("http_poison", std::move(config), transport);

    // The poison binding pins retry=0 / CB-off / no CB state on the CapabilityBinding
    // itself, so invoke_with_retry runs exactly once and never rewrites the status to
    // RetryExhausted or records a circuit-breaker failure. Assert on the binding
    // fields directly (not just the invoke result), BEFORE it is moved into registry.
    check(binding.retry.max_retries == 0, "http_poison.binding_retry_zero");
    check(!binding.circuit_breaker.enabled, "http_poison.binding_cb_disabled");
    check(binding.circuit_state == nullptr, "http_poison.binding_no_cb_state");

    CapabilityRegistry registry;
    registry.register_capability(std::move(binding));
    auto result = registry.invoke("http_poison", {});

    check(result.status == CapabilityCallStatus::Error, "http_poison.status_error");
    check(result.status != CapabilityCallStatus::RetryExhausted, "http_poison.not_retry_exhausted");
    check(result.attempts == 1, "http_poison.attempts_1");
    check(!result.value.has_value(), "http_poison.no_value");
    check(result.error_message.find("response schema admission failed") != std::string::npos,
          "http_poison.message");
    // Pre-transport: the fake transport was never touched.
    check(transport->http_requests.empty(), "http_poison.zero_transport");
}

void test_grpc_poison_conflict_fails_closed_pre_transport() {
    auto transport = std::make_shared<FakeCapabilityTransport>();
    transport->grpc_response = GrpcJsonTranscodingResponse{};

    auto binding_opt = project_return_binding(prim_type(TypeRefKind::Int));
    check(binding_opt.has_value(), "grpc_poison.fixture_projected");
    if (!binding_opt.has_value()) {
        return;
    }

    GrpcJsonTranscodingCapabilityConfig config;
    config.endpoint = "https://example.com:50051";
    config.service = "svc";
    config.method = "m";
    config.retry = RetryConfig{.max_retries = 3};
    config.circuit_breaker = CircuitBreakerConfig{.failure_threshold = 1, .enabled = true};
    config.response_schema = make_response_schema(TypeRefKind::Int);
    config.response_wire_binding = *binding_opt;
    auto binding =
        make_grpc_json_transcoding_capability("grpc_poison", std::move(config), transport);

    check(binding.retry.max_retries == 0, "grpc_poison.binding_retry_zero");
    check(!binding.circuit_breaker.enabled, "grpc_poison.binding_cb_disabled");
    check(binding.circuit_state == nullptr, "grpc_poison.binding_no_cb_state");

    CapabilityRegistry registry;
    registry.register_capability(std::move(binding));
    auto result = registry.invoke("grpc_poison", {});

    check(result.status == CapabilityCallStatus::Error, "grpc_poison.status_error");
    check(result.status != CapabilityCallStatus::RetryExhausted, "grpc_poison.not_retry_exhausted");
    check(result.attempts == 1, "grpc_poison.attempts_1");
    check(transport->grpc_requests.empty(), "grpc_poison.zero_transport");
}

// G4a four-state (via HTTP): neither => no-schema legacy behavior; binding-only =>
// exact decode; legacy-only => empty-Program migration exact; both => poison (above).
void test_http_four_state_neither_and_binding_only() {
    // neither: JSON body decodes schema-free (an Int literal -> IntValue).
    {
        auto transport = std::make_shared<FakeCapabilityTransport>();
        transport->http_response = HttpResponse{.status_code = 200, .body = "42", .error = {}};
        HTTPCapabilityConfig config;
        config.url = "https://example.com/capability";
        auto binding = make_http_capability("http_neither", std::move(config), transport);
        CapabilityRegistry registry;
        registry.register_capability(std::move(binding));
        auto result = registry.invoke("http_neither", {});
        check(result.status == CapabilityCallStatus::Success, "http_neither.status");
        const auto *iv =
            result.value.has_value() ? std::get_if<IntValue>(&result.value->node) : nullptr;
        check(iv != nullptr && iv->value == 42, "http_neither.value_42");
    }
    // binding-only: Int binding, Int body -> exact IntValue.
    {
        auto binding_opt = project_return_binding(prim_type(TypeRefKind::Int));
        check(binding_opt.has_value(), "http_binding_only.fixture");
        auto transport = std::make_shared<FakeCapabilityTransport>();
        transport->http_response = HttpResponse{.status_code = 200, .body = "42", .error = {}};
        HTTPCapabilityConfig config;
        config.url = "https://example.com/capability";
        if (binding_opt.has_value()) {
            config.response_wire_binding = *binding_opt;
        }
        auto binding = make_http_capability("http_binding_only", std::move(config), transport);
        CapabilityRegistry registry;
        registry.register_capability(std::move(binding));
        auto result = registry.invoke("http_binding_only", {});
        check(result.status == CapabilityCallStatus::Success, "http_binding_only.status");
        const auto *iv =
            result.value.has_value() ? std::get_if<IntValue>(&result.value->node) : nullptr;
        check(iv != nullptr && iv->value == 42, "http_binding_only.value_42");
    }
    // legacy-only: legacy Int TypeRef migrates (empty-Program) and exact-decodes.
    {
        auto transport = std::make_shared<FakeCapabilityTransport>();
        transport->http_response = HttpResponse{.status_code = 200, .body = "42", .error = {}};
        HTTPCapabilityConfig config;
        config.url = "https://example.com/capability";
        config.response_schema = make_response_schema(TypeRefKind::Int);
        auto binding = make_http_capability("http_legacy_only", std::move(config), transport);
        CapabilityRegistry registry;
        registry.register_capability(std::move(binding));
        auto result = registry.invoke("http_legacy_only", {});
        check(result.status == CapabilityCallStatus::Success, "http_legacy_only.status");
        const auto *iv =
            result.value.has_value() ? std::get_if<IntValue>(&result.value->node) : nullptr;
        check(iv != nullptr && iv->value == 42, "http_legacy_only.value_42");
    }
}

// G4a response table: TextPlain + String binding, JSON/TextPlain empty-body, and
// TextPlain non-String root poison.
void test_g4a_response_table() {
    // TextPlain + String binding: a non-empty body is a PRESENT String value.
    {
        auto binding_opt = project_return_binding(prim_type(TypeRefKind::String));
        check(binding_opt.has_value(), "rt.textplain_string.fixture");
        auto transport = std::make_shared<FakeCapabilityTransport>();
        transport->http_response = HttpResponse{.status_code = 200, .body = "hello", .error = {}};
        HTTPCapabilityConfig config;
        config.url = "https://example.com/capability";
        config.response_format = CapabilityResponseFormat::TextPlain;
        if (binding_opt.has_value()) {
            config.response_wire_binding = *binding_opt;
        }
        auto binding = make_http_capability("rt_textplain_string", std::move(config), transport);
        CapabilityRegistry registry;
        registry.register_capability(std::move(binding));
        auto result = registry.invoke("rt_textplain_string", {});
        check(result.status == CapabilityCallStatus::Success, "rt.textplain_string.status");
        const auto *sv =
            result.value.has_value() ? std::get_if<StringValue>(&result.value->node) : nullptr;
        check(sv != nullptr && sv->value == "hello", "rt.textplain_string.value");
    }
    // TextPlain + String binding, EMPTY body: present String("") (NOT None).
    {
        auto binding_opt = project_return_binding(prim_type(TypeRefKind::String));
        auto transport = std::make_shared<FakeCapabilityTransport>();
        transport->http_response = HttpResponse{.status_code = 200, .body = "", .error = {}};
        HTTPCapabilityConfig config;
        config.url = "https://example.com/capability";
        config.response_format = CapabilityResponseFormat::TextPlain;
        if (binding_opt.has_value()) {
            config.response_wire_binding = *binding_opt;
        }
        auto binding = make_http_capability("rt_textplain_empty", std::move(config), transport);
        CapabilityRegistry registry;
        registry.register_capability(std::move(binding));
        auto result = registry.invoke("rt_textplain_empty", {});
        check(result.status == CapabilityCallStatus::Success, "rt.textplain_empty.status");
        const auto *sv =
            result.value.has_value() ? std::get_if<StringValue>(&result.value->node) : nullptr;
        check(sv != nullptr && sv->value.empty(), "rt.textplain_empty.present_empty_string");
    }
    // TextPlain + NON-String binding: poison at construction (root gate).
    {
        auto binding_opt = project_return_binding(prim_type(TypeRefKind::Int));
        check(binding_opt.has_value(), "rt.textplain_nonstring.fixture");
        auto transport = std::make_shared<FakeCapabilityTransport>();
        transport->http_response = HttpResponse{.status_code = 200, .body = "5", .error = {}};
        HTTPCapabilityConfig config;
        config.url = "https://example.com/capability";
        config.response_format = CapabilityResponseFormat::TextPlain;
        if (binding_opt.has_value()) {
            config.response_wire_binding = *binding_opt;
        }
        auto binding = make_http_capability("rt_textplain_nonstring", std::move(config), transport);
        check(binding.retry.max_retries == 0, "rt.textplain_nonstring.binding_retry_zero");
        CapabilityRegistry registry;
        registry.register_capability(std::move(binding));
        auto result = registry.invoke("rt_textplain_nonstring", {});
        check(result.status == CapabilityCallStatus::Error, "rt.textplain_nonstring.status_error");
        check(result.error_message.find("response schema admission failed") != std::string::npos,
              "rt.textplain_nonstring.message");
        check(transport->http_requests.empty(), "rt.textplain_nonstring.zero_transport");
    }
    // JSON + binding, EMPTY body: hard error (never silent None).
    {
        auto binding_opt = project_return_binding(prim_type(TypeRefKind::Int));
        auto transport = std::make_shared<FakeCapabilityTransport>();
        transport->http_response = HttpResponse{.status_code = 200, .body = "", .error = {}};
        HTTPCapabilityConfig config;
        config.url = "https://example.com/capability";
        if (binding_opt.has_value()) {
            config.response_wire_binding = *binding_opt;
        }
        auto binding = make_http_capability("rt_json_binding_empty", std::move(config), transport);
        CapabilityRegistry registry;
        registry.register_capability(std::move(binding));
        auto result = registry.invoke("rt_json_binding_empty", {});
        check(result.status == CapabilityCallStatus::Error, "rt.json_binding_empty.status_error");
        check(result.error_message.find("invalid wire JSON response body") != std::string::npos,
              "rt.json_binding_empty.message");
    }
    // JSON + NO schema, EMPTY body: preserved None.
    {
        auto transport = std::make_shared<FakeCapabilityTransport>();
        transport->http_response = HttpResponse{.status_code = 200, .body = "", .error = {}};
        HTTPCapabilityConfig config;
        config.url = "https://example.com/capability";
        auto binding = make_http_capability("rt_json_noschema_empty", std::move(config), transport);
        CapabilityRegistry registry;
        registry.register_capability(std::move(binding));
        auto result = registry.invoke("rt_json_noschema_empty", {});
        check(result.status == CapabilityCallStatus::Success, "rt.json_noschema_empty.status");
        check(result.value.has_value() && is_none(*result.value), "rt.json_noschema_empty.none");
    }
    // TextPlain + NO schema, EMPTY body: preserved None.
    {
        auto transport = std::make_shared<FakeCapabilityTransport>();
        transport->http_response = HttpResponse{.status_code = 200, .body = "", .error = {}};
        HTTPCapabilityConfig config;
        config.url = "https://example.com/capability";
        config.response_format = CapabilityResponseFormat::TextPlain;
        auto binding = make_http_capability("rt_text_noschema_empty", std::move(config), transport);
        CapabilityRegistry registry;
        registry.register_capability(std::move(binding));
        auto result = registry.invoke("rt_text_noschema_empty", {});
        check(result.status == CapabilityCallStatus::Success, "rt.text_noschema_empty.status");
        check(result.value.has_value() && is_none(*result.value), "rt.text_noschema_empty.none");
    }

    // gRPC empty-body four-branch matrix (real Grpc factory+registry+FakeTransport,
    // not inferred from HTTP): JSON+binding empty => Error; TextPlain+String binding
    // empty => present String(""); JSON no-binding empty => None; TextPlain
    // no-binding empty => None.
    auto grpc_empty =
        [](const std::string &name,
           CapabilityResponseFormat format,
           std::optional<ir::core::VerifiedWireSchemaBinding> binding) -> CapabilityCallResult {
        auto transport = std::make_shared<FakeCapabilityTransport>();
        transport->grpc_response = GrpcJsonTranscodingResponse{
            .status_code = GrpcStatusCode::Ok, .body = "", .error_message = {}};
        GrpcJsonTranscodingCapabilityConfig config;
        config.endpoint = "https://example.com:50051";
        config.service = "svc";
        config.method = "m";
        config.response_format = format;
        config.response_wire_binding = std::move(binding);
        auto b = make_grpc_json_transcoding_capability(name, std::move(config), transport);
        CapabilityRegistry registry;
        registry.register_capability(std::move(b));
        return registry.invoke(name, {});
    };
    // JSON + binding empty => Error.
    {
        auto binding_opt = project_return_binding(prim_type(TypeRefKind::Int));
        check(binding_opt.has_value(), "rt.grpc_json_binding_empty.fixture");
        if (binding_opt.has_value()) {
            auto result = grpc_empty(
                "rt_grpc_json_binding_empty", CapabilityResponseFormat::Json, *binding_opt);
            check(result.status == CapabilityCallStatus::Error,
                  "rt.grpc_json_binding_empty.status_error");
            check(result.error_message.find("invalid wire JSON response body") != std::string::npos,
                  "rt.grpc_json_binding_empty.message");
        }
    }
    // TextPlain + String binding empty => present String("").
    {
        auto binding_opt = project_return_binding(prim_type(TypeRefKind::String));
        check(binding_opt.has_value(), "rt.grpc_textplain_binding_empty.fixture");
        if (binding_opt.has_value()) {
            auto result = grpc_empty("rt_grpc_textplain_binding_empty",
                                     CapabilityResponseFormat::TextPlain,
                                     *binding_opt);
            check(result.status == CapabilityCallStatus::Success,
                  "rt.grpc_textplain_binding_empty.status");
            const auto *sv =
                result.value.has_value() ? std::get_if<StringValue>(&result.value->node) : nullptr;
            check(sv != nullptr && sv->value.empty(),
                  "rt.grpc_textplain_binding_empty.present_empty_string");
        }
    }
    // JSON + no binding empty => None.
    {
        auto result =
            grpc_empty("rt_grpc_json_noschema_empty", CapabilityResponseFormat::Json, std::nullopt);
        check(result.status == CapabilityCallStatus::Success, "rt.grpc_json_noschema_empty.status");
        check(result.value.has_value() && is_none(*result.value),
              "rt.grpc_json_noschema_empty.none");
    }
    // TextPlain + no binding empty => None.
    {
        auto result = grpc_empty(
            "rt_grpc_text_noschema_empty", CapabilityResponseFormat::TextPlain, std::nullopt);
        check(result.status == CapabilityCallStatus::Success, "rt.grpc_text_noschema_empty.status");
        check(result.value.has_value() && is_none(*result.value),
              "rt.grpc_text_noschema_empty.none");
    }
}

// G4a exact rich-shape response decode via builtin nominals (HTTP + gRPC), plus
// hostile numeric provenance triad and legacy-only/negative admission cases.
void test_g4a_rich_shape_and_negatives() {
    // HTTP binding-only Option<Int>: null -> Option None; a number -> Some(Int).
    {
        auto binding_opt = project_return_binding(option_of(prim_type(TypeRefKind::Int)));
        check(binding_opt.has_value(), "rich.http_option.fixture");
        if (binding_opt.has_value()) {
            // None
            {
                auto transport = std::make_shared<FakeCapabilityTransport>();
                transport->http_response =
                    HttpResponse{.status_code = 200, .body = "null", .error = {}};
                HTTPCapabilityConfig config;
                config.url = "https://example.com/capability";
                config.response_wire_binding = *binding_opt;
                auto binding =
                    make_http_capability("rich_http_option_none", std::move(config), transport);
                CapabilityRegistry registry;
                registry.register_capability(std::move(binding));
                auto result = registry.invoke("rich_http_option_none", {});
                check(result.status == CapabilityCallStatus::Success,
                      "rich.http_option_none.status");
                check(result.value.has_value() && is_optional_none(*result.value),
                      "rich.http_option_none.is_option_none");
            }
            // Some(3)
            {
                auto transport = std::make_shared<FakeCapabilityTransport>();
                transport->http_response =
                    HttpResponse{.status_code = 200, .body = "3", .error = {}};
                HTTPCapabilityConfig config;
                config.url = "https://example.com/capability";
                config.response_wire_binding = *binding_opt;
                auto binding =
                    make_http_capability("rich_http_option_some", std::move(config), transport);
                CapabilityRegistry registry;
                registry.register_capability(std::move(binding));
                auto result = registry.invoke("rich_http_option_some", {});
                check(result.status == CapabilityCallStatus::Success,
                      "rich.http_option_some.status");
                const Value *inner =
                    result.value.has_value() ? optional_inner(*result.value) : nullptr;
                check(inner != nullptr && std::holds_alternative<IntValue>(inner->node) &&
                          std::get<IntValue>(inner->node).value == 3,
                      "rich.http_option_some.inner_int_3");
            }
        }
    }
    // HTTP binding-only Set<Int>: a JSON array -> canonical SetValue.
    {
        auto binding_opt = project_return_binding(set_of(prim_type(TypeRefKind::Int)));
        check(binding_opt.has_value(), "rich.http_set.fixture");
        if (binding_opt.has_value()) {
            auto transport = std::make_shared<FakeCapabilityTransport>();
            transport->http_response =
                HttpResponse{.status_code = 200, .body = "[1,2]", .error = {}};
            HTTPCapabilityConfig config;
            config.url = "https://example.com/capability";
            config.response_wire_binding = *binding_opt;
            auto binding = make_http_capability("rich_http_set", std::move(config), transport);
            CapabilityRegistry registry;
            registry.register_capability(std::move(binding));
            auto result = registry.invoke("rich_http_set", {});
            check(result.status == CapabilityCallStatus::Success, "rich.http_set.status");
            const auto *set =
                result.value.has_value() ? std::get_if<SetValue>(&result.value->node) : nullptr;
            check(set != nullptr && set->items.size() == 2, "rich.http_set.is_set_size2");
        }
    }
    // gRPC binding-only Map<String,Int> with a reserved-marker-looking key.
    {
        auto binding_opt = project_return_binding(
            map_of(prim_type(TypeRefKind::String), prim_type(TypeRefKind::Int)));
        check(binding_opt.has_value(), "rich.grpc_map.fixture");
        if (binding_opt.has_value()) {
            auto transport = std::make_shared<FakeCapabilityTransport>();
            transport->grpc_response = GrpcJsonTranscodingResponse{
                .status_code = GrpcStatusCode::Ok,
                .body = R"({"_timestamp":7,"plain":9})",
                .error_message = {},
            };
            GrpcJsonTranscodingCapabilityConfig config;
            config.endpoint = "https://example.com:50051";
            config.service = "svc";
            config.method = "m";
            config.response_wire_binding = *binding_opt;
            auto binding = make_grpc_json_transcoding_capability(
                "rich_grpc_map", std::move(config), transport);
            CapabilityRegistry registry;
            registry.register_capability(std::move(binding));
            auto result = registry.invoke("rich_grpc_map", {});
            check(result.status == CapabilityCallStatus::Success, "rich.grpc_map.status");
            const auto *map =
                result.value.has_value() ? std::get_if<MapValue>(&result.value->node) : nullptr;
            check(map != nullptr && map->entries.size() == 2, "rich.grpc_map.is_map_size2");
            // The reserved-marker-looking key "_timestamp" is an ordinary String key
            // here (Map<String,Int>), decoded to StringValue -> Int(7), NOT a Timestamp
            // marker.
            if (map != nullptr) {
                const Value *ts_value = nullptr;
                for (const auto &entry : map->entries) {
                    if (entry.first && std::holds_alternative<StringValue>(entry.first->node) &&
                        std::get<StringValue>(entry.first->node).value == "_timestamp") {
                        ts_value = entry.second.get();
                        break;
                    }
                }
                check(ts_value != nullptr && std::holds_alternative<IntValue>(ts_value->node) &&
                          std::get<IntValue>(ts_value->node).value == 7,
                      "rich.grpc_map.timestamp_key_int_7");
            }
        }
    }
    // Hostile numeric provenance triad under an Int binding: high-uint,
    // +IntegerFallback, -IntegerFallback all reject with no payload echo.
    {
        auto binding_opt = project_return_binding(prim_type(TypeRefKind::Int));
        check(binding_opt.has_value(), "rich.hostile.fixture");
        if (binding_opt.has_value()) {
            const std::vector<std::string> hostile = {
                "18446744073709551615", // high-uint (fits uint64, not int64)
                "-9223372036854775809", // - IntegerFallback (below int64 min)
            };
            for (std::size_t i = 0; i < hostile.size(); ++i) {
                auto transport = std::make_shared<FakeCapabilityTransport>();
                transport->http_response =
                    HttpResponse{.status_code = 200, .body = hostile[i], .error = {}};
                HTTPCapabilityConfig config;
                config.url = "https://example.com/capability";
                config.response_wire_binding = *binding_opt;
                auto binding = make_http_capability("rich_hostile", std::move(config), transport);
                CapabilityRegistry registry;
                registry.register_capability(std::move(binding));
                auto result = registry.invoke("rich_hostile", {});
                const auto tag = "rich.hostile[" + std::to_string(i) + "]";
                check(result.status == CapabilityCallStatus::Error, tag + ".status_error");
                check(result.error_message.find("response schema validation failed:") !=
                          std::string::npos,
                      tag + ".message_prefix");
                check(result.error_message.find(hostile[i]) == std::string::npos,
                      tag + ".no_payload_echo");
            }
        }
    }
    // gRPC hostile exact rejection (+IntegerFallback under Int binding), no echo.
    {
        auto binding_opt = project_return_binding(prim_type(TypeRefKind::Int));
        check(binding_opt.has_value(), "rich.grpc_hostile.fixture");
        if (binding_opt.has_value()) {
            const std::string hostile =
                "18446744073709551616"; // + IntegerFallback (exceeds uint64)
            auto transport = std::make_shared<FakeCapabilityTransport>();
            transport->grpc_response = GrpcJsonTranscodingResponse{
                .status_code = GrpcStatusCode::Ok,
                .body = hostile,
                .error_message = {},
            };
            GrpcJsonTranscodingCapabilityConfig config;
            config.endpoint = "https://example.com:50051";
            config.service = "svc";
            config.method = "m";
            config.response_wire_binding = *binding_opt;
            auto binding = make_grpc_json_transcoding_capability(
                "rich_grpc_hostile", std::move(config), transport);
            CapabilityRegistry registry;
            registry.register_capability(std::move(binding));
            auto result = registry.invoke("rich_grpc_hostile", {});
            check(result.status == CapabilityCallStatus::Error, "rich.grpc_hostile.status_error");
            check(result.error_message.find("response schema validation failed:") !=
                      std::string::npos,
                  "rich.grpc_hostile.message_prefix");
            check(result.error_message.find(hostile) == std::string::npos,
                  "rich.grpc_hostile.no_payload_echo");
        }
    }
    // Legacy-only closed primitive positive: an Int TypeRef migrates + exact-decodes.
    {
        auto transport = std::make_shared<FakeCapabilityTransport>();
        transport->http_response = HttpResponse{.status_code = 200, .body = "7", .error = {}};
        HTTPCapabilityConfig config;
        config.url = "https://example.com/capability";
        config.response_schema = make_response_schema(TypeRefKind::Int);
        auto binding = make_http_capability("legacy_int_pos", std::move(config), transport);
        CapabilityRegistry registry;
        registry.register_capability(std::move(binding));
        auto result = registry.invoke("legacy_int_pos", {});
        check(result.status == CapabilityCallStatus::Success, "legacy.int_pos.status");
        const auto *iv =
            result.value.has_value() ? std::get_if<IntValue>(&result.value->node) : nullptr;
        check(iv != nullptr && iv->value == 7, "legacy.int_pos.value_7");
    }
    // Legacy-only BoundedString positive: migrates + exact-decodes a JSON string.
    {
        auto transport = std::make_shared<FakeCapabilityTransport>();
        transport->http_response =
            HttpResponse{.status_code = 200, .body = R"("abc")", .error = {}};
        HTTPCapabilityConfig config;
        config.url = "https://example.com/capability";
        config.response_schema = std::make_shared<const ir::TypeRef>(bounded_string_type(1, 8));
        auto binding = make_http_capability("legacy_bstr_pos", std::move(config), transport);
        CapabilityRegistry registry;
        registry.register_capability(std::move(binding));
        auto result = registry.invoke("legacy_bstr_pos", {});
        check(result.status == CapabilityCallStatus::Success, "legacy.bstr_pos.status");
        const auto *sv =
            result.value.has_value() ? std::get_if<StringValue>(&result.value->node) : nullptr;
        check(sv != nullptr && sv->value == "abc", "legacy.bstr_pos.value_abc");
    }
    // TextPlain + BoundedString binding: in-bounds body succeeds; out-of-bounds fails.
    {
        auto binding_opt = project_return_binding(bounded_string_type(1, 4));
        check(binding_opt.has_value(), "rich.textplain_bstr.fixture");
        if (binding_opt.has_value()) {
            // in-bounds: "abcd" (len 4) present String.
            {
                auto transport = std::make_shared<FakeCapabilityTransport>();
                transport->http_response =
                    HttpResponse{.status_code = 200, .body = "abcd", .error = {}};
                HTTPCapabilityConfig config;
                config.url = "https://example.com/capability";
                config.response_format = CapabilityResponseFormat::TextPlain;
                config.response_wire_binding = *binding_opt;
                auto binding =
                    make_http_capability("rich_textplain_bstr_in", std::move(config), transport);
                CapabilityRegistry registry;
                registry.register_capability(std::move(binding));
                auto result = registry.invoke("rich_textplain_bstr_in", {});
                check(result.status == CapabilityCallStatus::Success,
                      "rich.textplain_bstr_in.status");
                const auto *sv = result.value.has_value()
                                     ? std::get_if<StringValue>(&result.value->node)
                                     : nullptr;
                check(sv != nullptr && sv->value == "abcd", "rich.textplain_bstr_in.value");
            }
            // out-of-bounds: "abcde" (len 5) rejected by length bounds.
            {
                auto transport = std::make_shared<FakeCapabilityTransport>();
                transport->http_response =
                    HttpResponse{.status_code = 200, .body = "abcde", .error = {}};
                HTTPCapabilityConfig config;
                config.url = "https://example.com/capability";
                config.response_format = CapabilityResponseFormat::TextPlain;
                config.response_wire_binding = *binding_opt;
                auto binding =
                    make_http_capability("rich_textplain_bstr_out", std::move(config), transport);
                CapabilityRegistry registry;
                registry.register_capability(std::move(binding));
                auto result = registry.invoke("rich_textplain_bstr_out", {});
                check(result.status == CapabilityCallStatus::Error,
                      "rich.textplain_bstr_out.status");
                check(result.error_message.find("response schema validation failed:") !=
                          std::string::npos,
                      "rich.textplain_bstr_out.message_prefix");
                check(result.error_message.find("abcde") == std::string::npos,
                      "rich.textplain_bstr_out.no_payload_echo");
            }
        }
    }
    // Legacy-only NON-projectable negatives: each must poison at construction
    // (retry=0) and never touch transport.
    {
        struct NegCase {
            std::string tag;
            TypeRef schema;
        };
        std::vector<NegCase> cases;
        cases.push_back({"any", prim_type(TypeRefKind::Any)});
        cases.push_back({"unresolved", prim_type(TypeRefKind::Unresolved)});
        cases.push_back({"never", prim_type(TypeRefKind::Never)});
        cases.push_back({"fn", prim_type(TypeRefKind::Fn)});
        // user nominal with no declaration in the (empty) program.
        {
            TypeRef bogus;
            bogus.kind = TypeRefKind::Struct;
            bogus.canonical_name = "app::main::DoesNotExist";
            cases.push_back({"user_nominal_no_decl", std::move(bogus)});
        }
        // Option with a missing type argument.
        {
            TypeRef opt;
            opt.kind = TypeRefKind::Enum;
            opt.canonical_name = "std::option::Option";
            opt.nominal_ref.kind = SymbolRefKind::Type;
            opt.nominal_ref.canonical_name = "std::option::Option";
            cases.push_back({"option_missing_arg", std::move(opt)});
        }
        // List with no element type argument (generic-arity negative).
        {
            TypeRef list;
            list.kind = TypeRefKind::Struct;
            list.canonical_name = "std::collections::List";
            list.nominal_ref.kind = SymbolRefKind::Type;
            list.nominal_ref.canonical_name = "std::collections::List";
            cases.push_back({"list_missing_arg", std::move(list)});
        }
        // Set with no element type argument.
        {
            TypeRef set;
            set.kind = TypeRefKind::Struct;
            set.canonical_name = "std::collections::Set";
            set.nominal_ref.kind = SymbolRefKind::Type;
            set.nominal_ref.canonical_name = "std::collections::Set";
            cases.push_back({"set_missing_arg", std::move(set)});
        }
        // Map with only one type argument (missing value type).
        {
            TypeRef map;
            map.kind = TypeRefKind::Struct;
            map.canonical_name = "std::collections::Map";
            map.nominal_ref.kind = SymbolRefKind::Type;
            map.nominal_ref.canonical_name = "std::collections::Map";
            map.params.push_back(std::make_unique<TypeRef>(prim_type(TypeRefKind::String)));
            cases.push_back({"map_one_arg", std::move(map)});
        }
        // Map with two slots but one is a null param (must fail closed, no crash).
        {
            TypeRef map;
            map.kind = TypeRefKind::Struct;
            map.canonical_name = "std::collections::Map";
            map.nominal_ref.kind = SymbolRefKind::Type;
            map.nominal_ref.canonical_name = "std::collections::Map";
            map.params.push_back(std::make_unique<TypeRef>(prim_type(TypeRefKind::String)));
            map.params.push_back(nullptr); // null value slot
            cases.push_back({"map_null_value_slot", std::move(map)});
        }
        for (auto &c : cases) {
            auto transport = std::make_shared<FakeCapabilityTransport>();
            transport->http_response = HttpResponse{.status_code = 200, .body = "1", .error = {}};
            HTTPCapabilityConfig config;
            config.url = "https://example.com/capability";
            config.response_schema = std::make_shared<const ir::TypeRef>(std::move(c.schema));
            auto binding =
                make_http_capability("legacy_neg_" + c.tag, std::move(config), transport);
            check(binding.retry.max_retries == 0, "legacy_neg." + c.tag + ".binding_retry_zero");
            check(!binding.circuit_breaker.enabled, "legacy_neg." + c.tag + ".binding_cb_disabled");
            check(binding.circuit_state == nullptr, "legacy_neg." + c.tag + ".binding_no_cb_state");
            CapabilityRegistry registry;
            registry.register_capability(std::move(binding));
            auto result = registry.invoke("legacy_neg_" + c.tag, {});
            check(result.status == CapabilityCallStatus::Error,
                  "legacy_neg." + c.tag + ".status_error");
            check(result.error_message.find("response schema admission failed") !=
                      std::string::npos,
                  "legacy_neg." + c.tag + ".message");
            check(transport->http_requests.empty(), "legacy_neg." + c.tag + ".zero_transport");
        }
    }
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
    test_eval_bare_builtin_hook_under_capability_dispatch();
    test_http_poison_conflict_fails_closed_pre_transport();
    test_grpc_poison_conflict_fails_closed_pre_transport();
    test_http_four_state_neither_and_binding_only();
    test_g4a_response_table();
    test_g4a_rich_shape_and_negatives();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
