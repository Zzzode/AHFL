#include "tooling/telemetry/trace.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

namespace ahfl::telemetry {

namespace {

std::string generate_hex(std::size_t length) {
    thread_local std::mt19937 rng(std::random_device{}());
    thread_local std::uniform_int_distribution<int> dist(0, 15);
    std::string result;
    result.reserve(length);
    static constexpr char hex_chars[] = "0123456789abcdef";
    for (std::size_t i = 0; i < length; ++i) {
        result += hex_chars[dist(rng)];
    }
    return result;
}

std::string span_to_json(const Span &span) {
    std::ostringstream oss;
    oss << "{\"name\":\"" << span.name << "\"";
    oss << ",\"traceId\":\"" << span.context.trace_id << "\"";
    oss << ",\"spanId\":\"" << span.context.span_id << "\"";
    if (!span.parent_span_id.empty()) {
        oss << ",\"parentSpanId\":\"" << span.parent_span_id << "\"";
    }
    auto start_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(span.start_time.time_since_epoch())
            .count();
    auto end_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(span.end_time.time_since_epoch())
            .count();
    oss << ",\"startTimeUnixNano\":" << start_ns;
    oss << ",\"endTimeUnixNano\":" << end_ns;
    if (!span.attributes.empty()) {
        oss << ",\"attributes\":[";
        bool first = true;
        for (const auto &[key, value] : span.attributes) {
            if (!first)
                oss << ",";
            oss << "{\"key\":\"" << key << "\",\"value\":\"" << value << "\"}";
            first = false;
        }
        oss << "]";
    }
    oss << "}";
    return oss.str();
}

} // namespace

void InMemoryTraceExporter::export_span(const Span &span) {
    spans_.push_back(span);
}

std::size_t InMemoryTraceExporter::span_count() const {
    return spans_.size();
}

const std::vector<Span> &InMemoryTraceExporter::spans() const {
    return spans_;
}

void InMemoryTraceExporter::clear() {
    spans_.clear();
}

FileTraceExporter::FileTraceExporter(std::string path) : path_(std::move(path)) {}

void FileTraceExporter::export_span(const Span &span) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ofstream ofs(path_, std::ios::app);
    if (ofs) {
        ofs << span_to_json(span) << '\n';
    }
    ++count_;
}

std::size_t FileTraceExporter::span_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return count_;
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

void end_span(Span &span) {
    span.end_time = std::chrono::steady_clock::now();
}

} // namespace ahfl::telemetry
