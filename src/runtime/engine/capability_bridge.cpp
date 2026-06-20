#include "runtime/engine/capability_bridge.hpp"

#include <memory>
#include <thread>
#include <utility>

namespace ahfl::runtime {

// ============================================================================
// CircuitBreakerState
// ============================================================================

CircuitBreakerState::State CircuitBreakerState::current_state() {
    std::lock_guard lock(mutex_);
    if (state_ == State::Closed) {
        return State::Closed;
    }
    // Check if recovery window has elapsed
    const auto now = std::chrono::steady_clock::now();
    if (now - opened_at_ >= config_.recovery_window) {
        state_ = State::HalfOpen;
        return State::HalfOpen;
    }
    return State::Open;
}

void CircuitBreakerState::record_success() {
    std::lock_guard lock(mutex_);
    failure_count_ = 0;
    state_ = State::Closed;
}

void CircuitBreakerState::record_failure() {
    std::lock_guard lock(mutex_);
    ++failure_count_;
    if (failure_count_ >= config_.failure_threshold) {
        state_ = State::Open;
        opened_at_ = std::chrono::steady_clock::now();
    }
}

// ============================================================================
// CapabilityRegistry
// ============================================================================

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

CapabilityCallResult
CapabilityRegistry::invoke_with_context(const CapabilityInvocationContext & /*context*/,
                                        const std::string &name,
                                        const std::vector<Value> &args) {
    return invoke(name, args);
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

CapabilityInvoker CapabilityRegistry::as_invoker() {
    auto *self = this;
    return [self](const std::string &name, const std::vector<Value> &args) -> CapabilityCallResult {
        return self->invoke(name, args);
    };
}

ContextualCapabilityInvoker CapabilityRegistry::as_contextual_invoker() {
    auto *self = this;
    return [self](const CapabilityInvocationContext &context,
                  const std::string &name,
                  const std::vector<Value> &args) -> CapabilityCallResult {
        return self->invoke_with_context(context, name, args);
    };
}

CapabilityCallResult CapabilityRegistry::invoke_with_retry(CapabilityBinding &binding,
                                                           const std::vector<Value> &args) {
    // Circuit breaker check
    if (binding.circuit_breaker.enabled && binding.circuit_state) {
        const auto cb_state = binding.circuit_state->current_state();
        if (cb_state == CircuitBreakerState::State::Open) {
            return CapabilityCallResult{
                .status = CapabilityCallStatus::CircuitOpen,
                .value = std::nullopt,
                .error_message = "circuit breaker is open for: " + binding.name,
                .attempts = 0,
            };
        }
    }

    const std::size_t max_attempts = binding.retry.max_retries + 1;
    CapabilityCallResult last_result{};

    for (std::size_t attempt = 1; attempt <= max_attempts; ++attempt) {
        last_result = binding.handler(args);
        last_result.attempts = attempt;

        if (last_result.status == CapabilityCallStatus::Success) {
            if (binding.circuit_breaker.enabled && binding.circuit_state) {
                binding.circuit_state->record_success();
            }
            return last_result;
        }

        // Exponential backoff before next attempt
        if (attempt < max_attempts) {
            auto delay = binding.retry.initial_delay;
            for (std::size_t i = 1; i < attempt; ++i) {
                delay = std::chrono::milliseconds(static_cast<long long>(
                    static_cast<double>(delay.count()) * binding.retry.backoff_multiplier));
            }
            if (delay.count() > 0) {
                std::this_thread::sleep_for(delay);
            }
        }
    }

    // Record failure for circuit breaker
    if (binding.circuit_breaker.enabled && binding.circuit_state) {
        binding.circuit_state->record_failure();
    }

    // Only mark as RetryExhausted when retries actually occurred (max_retries > 0)
    if (binding.retry.max_retries > 0) {
        last_result.status = CapabilityCallStatus::RetryExhausted;
    }
    return last_result;
}

} // namespace ahfl::runtime
