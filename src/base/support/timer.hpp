#pragma once

#include <chrono>
#include <string>

namespace ahfl::support {

struct TimerResult {
    std::string label;
    double elapsed_ms{0.0};
};

class ScopedTimer {
public:
    explicit ScopedTimer(std::string label);
    [[nodiscard]] TimerResult elapsed() const;

private:
    std::string label_;
    std::chrono::steady_clock::time_point start_;
};

} // namespace ahfl::support
