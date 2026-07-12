#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <ostream>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "ahfl/runtime/execution_event.hpp"

namespace ahfl::runtime {

enum class ExecutionOtelSpanKind {
    Internal,
    Client,
};

enum class ExecutionOtelStatus {
    Unset,
    Ok,
    Error,
};

using ExecutionOtelAttributes = std::vector<std::pair<std::string, std::string>>;

struct ExecutionOtelEvent {
    std::string name;
    std::uint64_t time_unix_nano{0};
    ExecutionOtelAttributes attributes;
};

struct ExecutionOtelSpan {
    std::string trace_id;
    std::string span_id;
    std::string parent_span_id;
    std::string name;
    ExecutionOtelSpanKind kind{ExecutionOtelSpanKind::Internal};
    ExecutionOtelStatus status{ExecutionOtelStatus::Unset};
    std::uint64_t start_time_unix_nano{0};
    std::uint64_t end_time_unix_nano{0};
    ExecutionOtelAttributes attributes;
    std::vector<ExecutionOtelEvent> events;
};

struct ExecutionOtelTrace {
    std::string service_name{"ahfl.runtime"};
    std::string scope_name{"ahfl.runtime.execution"};
    std::string trace_id;
    std::vector<ExecutionOtelSpan> spans;
};

enum class ExecutionOtelError {
    InvalidEventStream,
    MissingParentSpan,
    InvalidTimestamp,
};

using ExecutionOtelResult = std::expected<ExecutionOtelTrace, ExecutionOtelError>;

[[nodiscard]] ExecutionOtelResult
build_execution_otel_trace(std::span<const ExecutionEvent> events,
                           std::chrono::system_clock::time_point run_wall_time);

void render_execution_otel_json(const ExecutionOtelTrace &trace, std::ostream &out);

} // namespace ahfl::runtime
