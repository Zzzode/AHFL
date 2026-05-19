#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ahfl::telemetry {

enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Fatal
};

struct LogRecord {
    LogLevel level;
    std::string message;
    std::chrono::system_clock::time_point timestamp;
    std::vector<std::pair<std::string, std::string>> fields;
    std::string trace_id;
    std::string span_id;
};

[[nodiscard]] std::string format_log_json(const LogRecord &record);
[[nodiscard]] std::string_view log_level_string(LogLevel level);

class StructuredLogger {
  public:
    explicit StructuredLogger(LogLevel min_level = LogLevel::Info);
    void log(LogLevel level,
             std::string message,
             std::vector<std::pair<std::string, std::string>> fields = {});
    void set_file_sink(const std::string &path);
    void set_stdout_sink(bool enable);
    [[nodiscard]] const std::vector<LogRecord> &records() const;
    void clear();
    [[nodiscard]] std::size_t count() const;

  private:
    LogLevel min_level_;
    std::vector<LogRecord> records_;
    std::optional<std::string> file_sink_path_;
    bool stdout_sink_ = false;
};

} // namespace ahfl::telemetry
