#include "tooling/telemetry/context.hpp"

#include <random>
#include <sstream>
#include <unordered_map>

namespace ahfl::telemetry {

namespace {

std::string generate_hex_id(std::size_t length) {
    static std::mt19937 rng(std::random_device{}());
    static std::uniform_int_distribution<int> dist(0, 15);
    std::string result;
    result.reserve(length);
    for (std::size_t i = 0; i < length; ++i) {
        static constexpr char hex_chars[] = "0123456789abcdef";
        result += hex_chars[dist(rng)];
    }
    return result;
}

bool is_valid_hex(std::string_view s) {
    for (char c : s) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    return true;
}

} // namespace

std::string serialize_traceparent(const TraceContext &ctx) {
    std::string result;
    result.reserve(55); // 2 + 1 + 32 + 1 + 16 + 1 + 2 = 55
    result += ctx.version;
    result += '-';
    result += ctx.trace_id;
    result += '-';
    result += ctx.parent_id;
    result += '-';
    result += ctx.trace_flags;
    return result;
}

std::optional<TraceContext> parse_traceparent(std::string_view header) {
    // Format: version-traceid-parentid-flags
    // Example: 00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01
    if (header.size() != 55) {
        return std::nullopt;
    }
    if (header[2] != '-' || header[35] != '-' || header[52] != '-') {
        return std::nullopt;
    }

    TraceContext ctx;
    ctx.version = std::string(header.substr(0, 2));
    ctx.trace_id = std::string(header.substr(3, 32));
    ctx.parent_id = std::string(header.substr(36, 16));
    ctx.trace_flags = std::string(header.substr(53, 2));

    // Validate hex
    if (!is_valid_hex(ctx.version) || !is_valid_hex(ctx.trace_id) || !is_valid_hex(ctx.parent_id) ||
        !is_valid_hex(ctx.trace_flags)) {
        return std::nullopt;
    }

    return ctx;
}

TraceContext generate_context() {
    TraceContext ctx;
    ctx.version = "00";
    ctx.trace_id = generate_hex_id(32);
    ctx.parent_id = generate_hex_id(16);
    ctx.trace_flags = "01";
    return ctx;
}

void inject_trace_context(const TraceContext &ctx,
                          std::unordered_map<std::string, std::string> &headers) {
    headers["traceparent"] = serialize_traceparent(ctx);
}

std::optional<TraceContext>
extract_trace_context(const std::unordered_map<std::string, std::string> &headers) {
    auto it = headers.find("traceparent");
    if (it == headers.end())
        return std::nullopt;
    return parse_traceparent(it->second);
}

} // namespace ahfl::telemetry
