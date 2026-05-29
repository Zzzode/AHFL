#include "runtime/engine/capability_bridge.hpp"
#include "runtime/engine/capability_eval.hpp"
#include "runtime/evaluator/value.hpp"
#include "ahfl/compiler/ir/ir.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <variant>

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
// Test 6: as_invoker_works
// ============================================================================

void test_as_invoker_works() {
    CapabilityRegistry registry;
    registry.register_mock("greeting", make_string("hello"));

    auto invoker = registry.as_invoker();
    auto value = invoker("greeting", {});

    auto *sv = std::get_if<StringValue>(&value.node);
    check(sv != nullptr && sv->value == "hello", "invoker.value_hello");

    // 未注册的 capability 返回 NoneValue
    auto missing = invoker("unknown", {});
    check(std::holds_alternative<NoneValue>(missing.node), "invoker.missing_is_none");
}

// ============================================================================
// Test 7: http_capability_binding_construction
// ============================================================================

void test_http_capability_binding() {
    HTTPCapabilityConfig config;
    config.url = "https://example.com/api";
    config.method = "PUT";
    config.retry.max_retries = 3;
    config.timeout.deadline = std::chrono::milliseconds{5000};
    auto binding = make_http_capability("http_call", std::move(config));

    check(binding.name == "http_call", "http_binding.name");
    check(binding.retry.max_retries == 3, "http_binding.retry");
    check(binding.timeout.deadline == std::chrono::milliseconds{5000}, "http_binding.timeout");
    check(binding.handler != nullptr, "http_binding.has_handler");

    // Invoking will fail (no real server) but should produce a meaningful error
    CapabilityRegistry registry;
    registry.register_capability(std::move(binding));
    auto result = registry.invoke("http_call", {});
    check(result.status != CapabilityCallStatus::Success, "http_binding.not_success");
    check(!result.error_message.empty(), "http_binding.has_error");
}

// ============================================================================
// Test 8: grpc_capability_json_transcoding
// ============================================================================

void test_grpc_capability_json_transcoding() {
    GRPCCapabilityConfig config;
    config.endpoint = "http://localhost:50051";
    config.service = "TestService";
    config.method = "TestMethod";
    config.retry.max_retries = 1;
    auto binding = make_grpc_capability("grpc_call", std::move(config));

    check(binding.name == "grpc_call", "grpc_transcoding.name");
    check(binding.retry.max_retries == 1, "grpc_transcoding.retry");
    check(binding.handler != nullptr, "grpc_transcoding.has_handler");

    // Invoking will fail (no real server) but should produce a meaningful error
    CapabilityRegistry registry;
    registry.register_capability(std::move(binding));
    auto result = registry.invoke("grpc_call", {});
    check(result.status != CapabilityCallStatus::Success, "grpc_transcoding.not_success");
    check(!result.error_message.empty(), "grpc_transcoding.has_error");
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
    // 构造 IR CallExpr
    Expr expr;
    CallExpr call;
    call.callee = "my_capability";

    // 添加一个字符串参数
    auto arg = std::make_unique<Expr>();
    arg->node = StringLiteralExpr{"hello"};
    call.arguments.push_back(std::move(arg));
    expr.node = std::move(call);

    // 设置 registry
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

    // 测试 null registry 报错
    auto null_result = eval_expr_with_capabilities(expr, eval_ctx, nullptr);
    check(null_result.has_errors(), "eval_cap.null_registry_error");

    // 测试未注册的 capability 报错
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
    test_as_invoker_works();
    test_http_capability_binding();
    test_grpc_capability_json_transcoding();
    test_circuit_breaker_state_transitions();
    test_multiple_capabilities();
    test_eval_with_capability_call();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
