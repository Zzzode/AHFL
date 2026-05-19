#include "ahfl/telemetry/trace.hpp"

#include <algorithm>
#include <random>
#include <sstream>
#include <iomanip>

namespace ahfl::telemetry {

namespace {

std::string generate_hex(std::size_t length) {
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

} // namespace

void InMemoryTraceExporter::export_span(const Span& span) {
    spans_.push_back(span);
}

std::size_t InMemoryTraceExporter::span_count() const {
    return spans_.size();
}

const std::vector<Span>& InMemoryTraceExporter::spans() const {
    return spans_;
}

void InMemoryTraceExporter::clear() {
    spans_.clear();
}

Span create_span(std::string name, std::string parent_span_id) {
    Span span;
    span.name = std::move(name);
    span.context.trace_id = generate_hex(32);
    span.context.span_id = generate_hex(16);
    span.context.flags = 1;
    span.start_time = std::chrono::steady_clock::now();
    span.parent_span_id = std::move(parent_span_id);
    return span;
}

void end_span(Span& span) {
    span.end_time = std::chrono::steady_clock::now();
}

} // namespace ahfl::telemetry
