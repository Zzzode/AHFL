#include <cstdio>
#include <string>
#include <thread>
#include <chrono>

#include "tooling/telemetry/trace.hpp"
#include "tooling/telemetry/metrics.hpp"
#include "tooling/telemetry/logging.hpp"
#include "tooling/telemetry/context.hpp"

static int test_count = 0;
static int pass_count = 0;

static void check(bool condition, const char* name) {
    ++test_count;
    if (condition) { ++pass_count; std::printf("  PASS: %s\n", name); }
    else { std::printf("  FAIL: %s\n", name); }
}

static void test_span_creation() {
    ahfl::telemetry::InMemoryTraceExporter exporter;
    auto span = ahfl::telemetry::create_span("test-operation");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ahfl::telemetry::end_span(span);

    exporter.export_span(span);

    check(exporter.span_count() == 1, "span exported to in-memory store");
    check(span.name == "test-operation", "span has correct name");
    check(span.context.trace_id.size() == 32, "trace_id is 32 hex chars");
    check(span.context.span_id.size() == 16, "span_id is 16 hex chars");
    check(span.end_time > span.start_time, "span end_time > start_time");
}

static void test_metric_counter() {
    ahfl::telemetry::Counter counter("request_count");
    counter.increment();
    counter.increment(5.0);

    check(counter.name() == "request_count", "counter has correct name");
    check(counter.value() == 6.0, "counter value is 6.0 after increments");
}

static void test_metric_histogram() {
    ahfl::telemetry::Histogram hist("latency_ms", {10.0, 50.0, 100.0, 500.0});
    hist.record(5.0);
    hist.record(25.0);
    hist.record(75.0);
    hist.record(200.0);
    hist.record(1000.0);

    check(hist.name() == "latency_ms", "histogram has correct name");
    check(hist.count() == 5, "histogram count is 5");
    check(hist.sum() == 1305.0, "histogram sum is 1305.0");
}

static void test_log_formatting() {
    ahfl::telemetry::LogRecord record;
    record.level = ahfl::telemetry::LogLevel::Info;
    record.message = "test message";
    record.timestamp = std::chrono::system_clock::now();
    record.trace_id = "abcd1234abcd1234abcd1234abcd1234";
    record.span_id = "efgh5678efgh5678";
    record.fields = {{"key1", "value1"}};

    auto json = ahfl::telemetry::format_log_json(record);

    check(json.find("\"level\":\"INFO\"") != std::string::npos, "JSON contains level INFO");
    check(json.find("\"message\":\"test message\"") != std::string::npos, "JSON contains message");
    check(json.find("\"trace_id\":\"abcd1234abcd1234abcd1234abcd1234\"") != std::string::npos, "JSON contains trace_id");
    check(json.find("\"key1\":\"value1\"") != std::string::npos, "JSON contains fields");

    // Test StructuredLogger filtering
    ahfl::telemetry::StructuredLogger logger(ahfl::telemetry::LogLevel::Warn);
    logger.log(ahfl::telemetry::LogLevel::Info, "should be filtered");
    logger.log(ahfl::telemetry::LogLevel::Error, "should be kept");
    check(logger.count() == 1, "logger filters below min_level");
}

static void test_context_propagation() {
    // Test serialization
    ahfl::telemetry::TraceContext ctx;
    ctx.version = "00";
    ctx.trace_id = "4bf92f3577b34da6a3ce929d0e0e4736";
    ctx.parent_id = "00f067aa0ba902b7";
    ctx.trace_flags = "01";

    auto serialized = ahfl::telemetry::serialize_traceparent(ctx);
    check(serialized == "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01",
          "serialize_traceparent produces W3C format");

    // Test parsing
    auto parsed = ahfl::telemetry::parse_traceparent(serialized);
    check(parsed.has_value(), "parse_traceparent succeeds on valid input");
    check(parsed->trace_id == ctx.trace_id, "parsed trace_id matches");
    check(parsed->parent_id == ctx.parent_id, "parsed parent_id matches");
    check(parsed->trace_flags == "01", "parsed trace_flags matches");

    // Test invalid input
    auto invalid = ahfl::telemetry::parse_traceparent("invalid");
    check(!invalid.has_value(), "parse_traceparent returns nullopt on invalid input");

    // Test generate
    auto generated = ahfl::telemetry::generate_context();
    check(generated.trace_id.size() == 32, "generated trace_id is 32 chars");
    check(generated.parent_id.size() == 16, "generated parent_id is 16 chars");
}

int main() {
    std::printf("=== Telemetry Tests ===\n\n");

    test_span_creation();
    test_metric_counter();
    test_metric_histogram();
    test_log_formatting();
    test_context_propagation();

    std::printf("\n%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
