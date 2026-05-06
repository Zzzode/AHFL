#include "ahfl/runtime/capability_bridge.hpp"

#include <memory>
#include <utility>

namespace ahfl::runtime {

void CapabilityRegistry::register_capability(CapabilityBinding binding) {
    auto name = binding.name;
    bindings_.emplace(std::move(name), std::move(binding));
}

void CapabilityRegistry::register_function(
    const std::string &name, std::function<Value(const std::vector<Value> &args)> fn) {
    CapabilityBinding binding;
    binding.name = name;
    binding.handler = [f = std::move(fn)](const std::vector<Value> &args) -> CapabilityCallResult {
        return CapabilityCallResult{
            .status = CapabilityCallStatus::Success,
            .value = f(args),
            .error_message = {},
            .attempts = 1,
        };
    };
    bindings_.emplace(name, std::move(binding));
}

void CapabilityRegistry::register_mock(const std::string &name, Value mock_result) {
    CapabilityBinding binding;
    binding.name = name;
    binding.handler = [result = std::make_shared<Value>(std::move(mock_result))](
                          const std::vector<Value> & /*args*/) -> CapabilityCallResult {
        return CapabilityCallResult{
            .status = CapabilityCallStatus::Success,
            .value = evaluator::clone_value(*result),
            .error_message = {},
            .attempts = 1,
        };
    };
    bindings_.emplace(name, std::move(binding));
}

CapabilityCallResult CapabilityRegistry::invoke(const std::string &name,
                                                const std::vector<Value> &args) {
    auto it = bindings_.find(name);
    if (it == bindings_.end()) {
        return CapabilityCallResult{
            .status = CapabilityCallStatus::Error,
            .value = std::nullopt,
            .error_message = "capability not found: " + name,
            .attempts = 0,
        };
    }
    return invoke_with_retry(it->second, args);
}

bool CapabilityRegistry::has(const std::string &name) const {
    return bindings_.find(name) != bindings_.end();
}

std::vector<std::string> CapabilityRegistry::registered_names() const {
    std::vector<std::string> names;
    names.reserve(bindings_.size());
    for (const auto &[key, _] : bindings_) {
        names.push_back(key);
    }
    return names;
}

std::function<Value(const std::string &, const std::vector<Value> &)>
CapabilityRegistry::as_invoker() const {
    // 捕获 this 指针（const 不够，invoke 需要 non-const），使用 mutable copy
    auto *self = const_cast<CapabilityRegistry *>(this);
    return [self](const std::string &name, const std::vector<Value> &args) -> Value {
        auto result = self->invoke(name, args);
        if (result.status == CapabilityCallStatus::Success && result.value.has_value()) {
            return std::move(*result.value);
        }
        return evaluator::make_none();
    };
}

CapabilityCallResult CapabilityRegistry::invoke_with_retry(const CapabilityBinding &binding,
                                                           const std::vector<Value> &args) {
    const std::size_t max_attempts = binding.retry.max_retries + 1;
    CapabilityCallResult last_result{};

    for (std::size_t attempt = 1; attempt <= max_attempts; ++attempt) {
        last_result = binding.handler(args);
        last_result.attempts = attempt;

        if (last_result.status == CapabilityCallStatus::Success) {
            return last_result;
        }

        // 如果还有重试机会，继续（不实际 sleep，保证可测试性）
    }

    // 只有当实际进行了重试（max_retries > 0）时，才标记为 RetryExhausted
    if (binding.retry.max_retries > 0) {
        last_result.status = CapabilityCallStatus::RetryExhausted;
    }
    return last_result;
}

CapabilityBinding make_http_capability(const std::string &name, HTTPCapabilityConfig config) {
    CapabilityBinding binding;
    binding.name = name;
    binding.retry = config.retry;
    binding.timeout = config.timeout;
    binding.handler = [](const std::vector<Value> & /*args*/) -> CapabilityCallResult {
        return CapabilityCallResult{
            .status = CapabilityCallStatus::Error,
            .value = std::nullopt,
            .error_message = "HTTP capability not connected",
            .attempts = 1,
        };
    };
    return binding;
}

CapabilityBinding make_grpc_capability(const std::string &name, GRPCCapabilityConfig config) {
    CapabilityBinding binding;
    binding.name = name;
    binding.retry = config.retry;
    binding.timeout = config.timeout;
    binding.handler = [](const std::vector<Value> & /*args*/) -> CapabilityCallResult {
        return CapabilityCallResult{
            .status = CapabilityCallStatus::Error,
            .value = std::nullopt,
            .error_message = "gRPC capability not connected",
            .attempts = 1,
        };
    };
    return binding;
}

} // namespace ahfl::runtime
