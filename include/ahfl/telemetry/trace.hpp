#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace ahfl::telemetry {

struct SpanContext {
    std::string trace_id;   // 32 hex chars
    std::string span_id;    // 16 hex chars
    uint8_t flags = 0;
};

struct Span {
    std::string name;
    SpanContext context;
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point end_time;
    std::vector<std::pair<std::string, std::string>> attributes;
    std::string parent_span_id;
};

class TraceExporter {
public:
    virtual ~TraceExporter() = default;
    virtual void export_span(const Span& span) = 0;
    [[nodiscard]] virtual std::size_t span_count() const = 0;
};

class InMemoryTraceExporter final : public TraceExporter {
public:
    void export_span(const Span& span) override;
    [[nodiscard]] std::size_t span_count() const override;
    [[nodiscard]] const std::vector<Span>& spans() const;
    void clear();
private:
    std::vector<Span> spans_;
};

[[nodiscard]] Span create_span(std::string name, std::string parent_span_id = "");
void end_span(Span& span);

} // namespace ahfl::telemetry
