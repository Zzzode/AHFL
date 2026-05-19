#include "ahfl/telemetry/logging.hpp"

#include <chrono>
#include <sstream>
#include <utility>

namespace ahfl::telemetry {

std::string_view log_level_string(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
    }
    return "UNKNOWN";
}

std::string format_log_json(const LogRecord& record) {
    std::ostringstream oss;
    oss << "{\"level\":\"" << log_level_string(record.level) << "\"";
    oss << ",\"message\":\"" << record.message << "\"";

    auto time_t_val = std::chrono::system_clock::to_time_t(record.timestamp);
    oss << ",\"timestamp\":" << time_t_val;

    if (!record.trace_id.empty()) {
        oss << ",\"trace_id\":\"" << record.trace_id << "\"";
    }
    if (!record.span_id.empty()) {
        oss << ",\"span_id\":\"" << record.span_id << "\"";
    }
    if (!record.fields.empty()) {
        oss << ",\"fields\":{";
        bool first = true;
        for (const auto& [key, value] : record.fields) {
            if (!first) oss << ",";
            oss << "\"" << key << "\":\"" << value << "\"";
            first = false;
        }
        oss << "}";
    }
    oss << "}";
    return oss.str();
}

StructuredLogger::StructuredLogger(LogLevel min_level) : min_level_(min_level) {}

void StructuredLogger::log(LogLevel level, std::string message,
                           std::vector<std::pair<std::string, std::string>> fields) {
    if (static_cast<int>(level) < static_cast<int>(min_level_)) {
        return;
    }
    LogRecord record;
    record.level = level;
    record.message = std::move(message);
    record.timestamp = std::chrono::system_clock::now();
    record.fields = std::move(fields);
    records_.push_back(std::move(record));
}

const std::vector<LogRecord>& StructuredLogger::records() const {
    return records_;
}

void StructuredLogger::clear() {
    records_.clear();
}

std::size_t StructuredLogger::count() const {
    return records_.size();
}

} // namespace ahfl::telemetry
