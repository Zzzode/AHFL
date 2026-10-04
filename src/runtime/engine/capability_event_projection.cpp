#include "runtime/engine/capability_event_projection.hpp"

#include <algorithm>
#include <string_view>
#include <utility>

namespace ahfl::runtime {

void project_capability_call_events(
    WorkflowResult &result,
    std::function<std::chrono::nanoseconds()> monotonic_clock,
    WorkflowNodeId node,
    CapabilityId capability,
    ProviderId provider,
    const CapabilityCallResult &call,
    std::optional<RuntimeValueId> output_value_id,
    InvocationId first_invocation) {
    // Pending: no events. The invocation has no terminal yet (it completes on
    // resume), so opening its lifecycle here would leave a dangling Start.
    // The node-level NodeSuspended terminal records the pause instead.
    if (call.status == CapabilityCallStatus::Pending) {
        return;
    }

    auto emit = [&](auto payload) {
        (void)result.events.append(std::move(payload), monotonic_clock());
    };

    const auto attempts = std::max<std::size_t>(call.attempts, 1U);
    InvocationId previous;
    for (std::size_t attempt = 1; attempt <= attempts; ++attempt) {
        const auto attempt_invocation =
            attempt == 1 && first_invocation.valid()
                ? first_invocation
                : result.metadata.add_invocation(node, capability);
        if (attempt > 1) {
            emit(CapabilityRetryScheduled{
                .previous_invocation = previous,
                .next_invocation = attempt_invocation,
                .next_attempt = attempt,
            });
        }
        emit(CapabilityStarted{
            .invocation = attempt_invocation,
            .node = node,
            .capability = capability,
            .provider = provider,
            .attempt = attempt,
        });

        if (attempt == attempts && call.usage.has_value()) {
            emit(CapabilityUsageRecorded{
                .invocation = attempt_invocation,
                .prompt_tokens = call.usage->prompt_tokens,
                .completion_tokens = call.usage->completion_tokens,
                .total_tokens = call.usage->total_tokens,
                .total_cost_usd = call.usage->total_cost_usd,
                .cost_estimated = call.usage->cost_estimated,
                .notices = call.usage->notices,
            });
        }

        const bool terminal_success =
            attempt == attempts && call.status == CapabilityCallStatus::Success;
        if (!terminal_success) {
            emit(CapabilityFailed{
                .invocation = attempt_invocation,
                .kind = call.failure_kind.value_or(
                    capability_failure_kind(call.status)),
                .diagnostic = std::nullopt,
                .attempts = attempt,
                .retryable = attempt < attempts ||
                             call.status == CapabilityCallStatus::Timeout ||
                             call.status == CapabilityCallStatus::RetryExhausted,
            });
        }
        previous = attempt_invocation;
    }

    if (call.provider_degraded) {
        const auto degraded = result.metadata.add_provider(
            call.degraded_provider_name.empty()
                ? std::string_view{"degraded"}
                : std::string_view{call.degraded_provider_name});
        const auto selected = result.metadata.add_provider(
            call.selected_provider_name.empty()
                ? std::string_view{"fallback"}
                : std::string_view{call.selected_provider_name});
        emit(ProviderDegraded{
            .invocation = previous,
            .provider = degraded,
            .fallback_provider = selected,
            .reason = ProviderDegradationReason::RetryExhausted,
        });
    }

    if (call.status == CapabilityCallStatus::Success) {
        emit(CapabilityCompleted{
            .invocation = previous,
            .output = output_value_id,
            .attempts = call.attempts,
            .cache_hit = call.cache_hit,
        });
    }

    // usage.notices -> WARNING diagnostics.
    if (call.usage.has_value()) {
        for (const auto &notice : call.usage->notices) {
            result.diagnostics.warning()
                .message(notice.message)
                .code(notice.diagnostic_code)
                .emit();
        }
    }
}

} // namespace ahfl::runtime
