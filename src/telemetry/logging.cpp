#include "ahfl/telemetry/logging.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <utility>

namespace ahfl::telemetry {

namespace {

std::string json_escape(std::string_view input) {
    std::string output;
    output.reserve(input.size() + 8);
    for (char c : input) {
        switch (c) {
        case '"':
            output += "\\\"";
            break;
        case '\\':
            output += "\\\\";
            break;
        case '\n':
            output += "\\n";
            break;
        case '\r':
            output += "\\r";
            break;
        case '\t':
            output += "\\t";
            break;
        case '\b':
            output += "\\b";
            break;
        case '\f':
            output += "\\f";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf,
                              sizeof(buf),
                              "\\u%04x",
                              static_cast<unsigned>(static_cast<unsigned char>(c)));
                output += buf;
            } else {
                output += c;
            }
        }
    }
    return output;
}

} // namespace

std::string_view log_level_string(LogLevel level) {
    switch (level) {
    case LogLevel::Trace:
        return "TRACE";
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warn:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    case LogLevel::Fatal:
        return "FATAL";
    }
    return "UNKNOWN";
}

std::string format_log_json(const LogRecord &record) {
    std::ostringstream oss;
    oss << "{\"level\":\"" << log_level_string(record.level) << "\"";
    oss << ",\"message\":\"" << json_escape(record.message) << "\"";

    auto time_t_val = std::chrono::system_clock::to_time_t(record.timestamp);
    oss << ",\"timestamp\":" << time_t_val;

    if (!record.trace_id.empty()) {
        oss << ",\"trace_id\":\"" << json_escape(record.trace_id) << "\"";
    }
    if (!record.span_id.empty()) {
        oss << ",\"span_id\":\"" << json_escape(record.span_id) << "\"";
    }
    if (!record.fields.empty()) {
        oss << ",\"fields\":{";
        bool first = true;
        for (const auto &[key, value] : record.fields) {
            if (!first)
                oss << ",";
            oss << "\"" << json_escape(key) << "\":\"" << json_escape(value) << "\"";
            first = false;
        }
        oss << "}";
    }
    oss << "}";
    return oss.str();
}

StructuredLogger::StructuredLogger(LogLevel min_level) : min_level_(min_level) {}

void StructuredLogger::set_file_sink(const std::string &path) {
    file_sink_path_ = path;
}

void StructuredLogger::set_stdout_sink(bool enable) {
    stdout_sink_ = enable;
}

void StructuredLogger::log(LogLevel level,
                           std::string message,
                           std::vector<std::pair<std::string, std::string>> fields) {
    if (static_cast<int>(level) < static_cast<int>(min_level_)) {
        return;
    }
    LogRecord record;
    record.level = level;
    record.message = std::move(message);
    record.timestamp = std::chrono::system_clock::now();
    record.fields = std::move(fields);
    records_.push_back(record);

    // Write to sinks
    if (stdout_sink_ || file_sink_path_.has_value()) {
        auto json = format_log_json(record);
        if (stdout_sink_) {
            std::cout << json << '\n';
        }
        if (file_sink_path_.has_value()) {
            std::ofstream ofs(*file_sink_path_, std::ios::app);
            if (ofs) {
                ofs << json << '\n';
            }
        }
    }
}

const std::vector<LogRecord> &StructuredLogger::records() const {
    return records_;
}

void StructuredLogger::clear() {
    records_.clear();
}

std::size_t StructuredLogger::count() const {
    return records_.size();
}

} // namespace ahfl::telemetry
