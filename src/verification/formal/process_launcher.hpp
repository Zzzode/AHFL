#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ahfl::formal {

struct ProcessResult {
    int exit_code{-1};
    std::string stdout_output;
    std::string stderr_output;
    bool timed_out{false};
};

struct ProcessConfig {
    std::string executable;
    std::vector<std::string> arguments;
    std::optional<std::string> stdin_input;
    std::chrono::seconds timeout{60};
};

/// Launch external process (replaces popen)
[[nodiscard]] ProcessResult launch_process(const ProcessConfig &config);

/// Find executable in PATH
[[nodiscard]] std::optional<std::string> find_executable(std::string_view name);

} // namespace ahfl::formal
