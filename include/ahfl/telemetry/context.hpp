#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace ahfl::telemetry {

struct TraceContext {
    std::string version = "00";
    std::string trace_id;
    std::string parent_id;
    std::string trace_flags = "00";
};

[[nodiscard]] std::string serialize_traceparent(const TraceContext& ctx);
[[nodiscard]] std::optional<TraceContext> parse_traceparent(std::string_view header);
[[nodiscard]] TraceContext generate_context();

} // namespace ahfl::telemetry
