#include "runtime/engine/standard_capabilities.hpp"

#include "runtime/value/value.hpp"

#include <chrono>
#include <random>
#include <string>
#include <string_view>
#include <utility>

namespace ahfl::runtime {

namespace {

// Default Clock provider: wall-clock milliseconds since the Unix epoch.
// (Relocated from the former builtin_wall_clock_now.)
runtime::Value default_clock() {
    const auto now = std::chrono::system_clock::now();
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return runtime::make_timestamp(ms);
}

// Default UuidV4 provider: a v4-shaped random UUID (not cryptographically
// secure — a real host overrides this). Relocated from the former
// builtin_uuid_new.
runtime::Value default_uuid_v4() {
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<std::uint32_t> dist(0, 0xF);
    std::string hex;
    hex.reserve(32);
    const char *hex_chars = "0123456789abcdef";
    for (int i = 0; i < 32; ++i) {
        hex.push_back(hex_chars[dist(rng)]);
    }
    hex[12] = '4';
    const int variant_nibble = static_cast<int>(dist(rng) & 0x3);
    hex[16] = "89ab"[variant_nibble];
    if (auto value = runtime::make_uuid(hex)) {
        return std::move(*value);
    }
    // make_uuid only fails on malformed input; the constructed spelling is
    // always 32 lowercase hex chars, so this is unreachable.
    return runtime::make_uuid(std::string(32, '0')).value_or(runtime::make_none());
}

} // namespace

bool is_standard_capability(std::string_view name) {
    return name == kStdCapabilityClock || name == kStdCapabilityUuidV4;
}

CapabilityCallResult invoke_standard_capability(std::string_view name) {
    CapabilityCallResult result;
    result.status = CapabilityCallStatus::Success;
    result.attempts = 1;
    if (name == kStdCapabilityClock) {
        result.value = default_clock();
    } else if (name == kStdCapabilityUuidV4) {
        result.value = default_uuid_v4();
    } else {
        result.status = CapabilityCallStatus::Error;
        result.error_message = "not a standard capability: " + std::string(name);
    }
    return result;
}

ContextualCapabilityInvoker with_standard_capabilities(ContextualCapabilityInvoker fallback) {
    return [fallback = std::move(fallback)](const CapabilityInvocationContext &context,
                                            const std::string &name,
                                            const std::vector<Value> &args) -> CapabilityCallResult {
        if (is_standard_capability(name)) {
            return invoke_standard_capability(name);
        }
        if (fallback) {
            return fallback(context, name, args);
        }
        CapabilityCallResult miss;
        miss.status = CapabilityCallStatus::Error;
        miss.error_message = "no provider for capability: " + name;
        return miss;
    };
}

} // namespace ahfl::runtime
