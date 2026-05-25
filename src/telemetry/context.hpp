#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ahfl::telemetry {

struct TraceContext {
    std::string version = "00";
    std::string trace_id;
    std::string parent_id;
    std::string trace_flags = "00";
};

[[nodiscard]] std::string serialize_traceparent(const TraceContext &ctx);
[[nodiscard]] std::optional<TraceContext> parse_traceparent(std::string_view header);
[[nodiscard]] TraceContext generate_context();

void inject_trace_context(const TraceContext &ctx,
                          std::unordered_map<std::string, std::string> &headers);
[[nodiscard]] std::optional<TraceContext>
extract_trace_context(const std::unordered_map<std::string, std::string> &headers);

} // namespace ahfl::telemetry
