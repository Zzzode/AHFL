#pragma once

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ahfl/evaluator/value.hpp"
#include "ahfl/support/diagnostics.hpp"

namespace ahfl::runtime {

using evaluator::Value;

// Capability 调用状态
enum class CapabilityCallStatus {
    Success,
    Error,
    Timeout,
    RetryExhausted,
};

// Capability 调用结果
struct CapabilityCallResult {
    CapabilityCallStatus status{CapabilityCallStatus::Error};
    std::optional<Value> value;
    std::string error_message;
    std::size_t attempts{1};
};

// Retry 配置
struct RetryConfig {
    std::size_t max_retries{0};
    std::chrono::milliseconds initial_delay{100};
    double backoff_multiplier{2.0};
};

// Timeout 配置
struct TimeoutConfig {
    std::chrono::milliseconds deadline{30000};
};

// 单个 Capability 的绑定定义
struct CapabilityBinding {
    std::string name;
    std::function<CapabilityCallResult(const std::vector<Value> &args)> handler;
    RetryConfig retry;
    TimeoutConfig timeout;
};

// Capability 注册中心
class CapabilityRegistry {
  public:
    void register_capability(CapabilityBinding binding);
    void register_function(const std::string &name,
                           std::function<Value(const std::vector<Value> &args)> fn);
    void register_mock(const std::string &name, Value mock_result);

    [[nodiscard]] CapabilityCallResult invoke(const std::string &name,
                                              const std::vector<Value> &args);
    [[nodiscard]] bool has(const std::string &name) const;
    [[nodiscard]] std::vector<std::string> registered_names() const;
    [[nodiscard]] std::function<Value(const std::string &, const std::vector<Value> &)>
    as_invoker() const;

  private:
    std::unordered_map<std::string, CapabilityBinding> bindings_;

    [[nodiscard]] CapabilityCallResult invoke_with_retry(const CapabilityBinding &binding,
                                                         const std::vector<Value> &args);
};

// HTTP Capability 工厂 (stub)
struct HTTPCapabilityConfig {
    std::string url;
    std::string method{"POST"};
    std::unordered_map<std::string, std::string> headers;
    RetryConfig retry;
    TimeoutConfig timeout;
};
[[nodiscard]] CapabilityBinding make_http_capability(const std::string &name,
                                                     HTTPCapabilityConfig config);

// gRPC Capability 工厂 (stub)
struct GRPCCapabilityConfig {
    std::string endpoint;
    std::string service;
    std::string method;
    RetryConfig retry;
    TimeoutConfig timeout;
};
[[nodiscard]] CapabilityBinding make_grpc_capability(const std::string &name,
                                                     GRPCCapabilityConfig config);

} // namespace ahfl::runtime
