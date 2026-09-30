#include "conformance/conformance_mock_registry.hpp"

#include <memory>
#include <string>
#include <utility>

#include "runtime/value/value.hpp"
#include "runtime/value/value_json.hpp"

namespace ahfl::conformance {

using ahfl::runtime::CapabilityBinding;
using ahfl::runtime::CapabilityCallResult;
using ahfl::runtime::CapabilityCallStatus;
using ahfl::runtime::CapabilityRegistry;
using ahfl::runtime::Value;

std::optional<ahfl::runtime::CapabilityRegistry>
build_mock_registry(const ConformanceCase &manifest, std::string &error_out) {
    CapabilityRegistry registry;
    for (const auto &capability : manifest.capabilities) {
        switch (capability.status) {
        case CapabilityOutcomeStatus::Ok: {
            if (!capability.result_json.has_value()) {
                error_out = "manifest capability '" + capability.name +
                            "' is 'ok' without a result frame";
                return std::nullopt;
            }
            auto canned = runtime::value_from_json(*capability.result_json);
            if (!canned.has_value()) {
                error_out = "failed to decode result_json for capability '" + capability.name +
                            "'";
                return std::nullopt;
            }
            auto shared = std::make_shared<Value>(std::move(*canned));
            registry.register_function(
                capability.name,
                [shared](const std::vector<Value> &) -> Value {
                    return runtime::clone_value(*shared);
                });
            break;
        }
        case CapabilityOutcomeStatus::Error: {
            // Register directly so the mock can return a non-Success terminal
            // (register_function only models an `ok` outcome).
            const std::string cap_name = capability.name;
            const std::string detail = capability.result_json.value_or("");
            ahfl::runtime::CapabilityBinding binding;
            binding.name = cap_name;
            binding.handler = [cap_name, detail](const std::vector<Value> &) -> CapabilityCallResult {
                CapabilityCallResult result;
                result.status = CapabilityCallStatus::Error;
                result.error_message =
                    "conformance mock error for capability '" + cap_name + "'" +
                    (detail.empty() ? std::string{} : ": " + detail);
                return result;
            };
            registry.register_capability(std::move(binding));
            break;
        }
        case CapabilityOutcomeStatus::Pending: {
            ahfl::runtime::CapabilityBinding binding;
            binding.name = capability.name;
            binding.handler = [](const std::vector<Value> &) -> CapabilityCallResult {
                CapabilityCallResult result;
                result.status = CapabilityCallStatus::Pending;
                return result;
            };
            registry.register_capability(std::move(binding));
            break;
        }
        }
    }
    return registry;
}

} // namespace ahfl::conformance
