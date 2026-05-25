#include "support/timer.hpp"

#include <utility>

namespace ahfl::support {

ScopedTimer::ScopedTimer(std::string label)
    : label_(std::move(label))
    , start_(std::chrono::steady_clock::now()) {}

TimerResult ScopedTimer::elapsed() const {
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(now - start_);
    return TimerResult{
        .label = label_,
        .elapsed_ms = static_cast<double>(duration.count()) / 1000.0,
    };
}

} // namespace ahfl::support
