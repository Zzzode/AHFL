#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace ahfl::llm_provider {

/// Parses Server-Sent Events (SSE) lines from an LLM streaming response.
class SSEParser {
  public:
    /// Feed raw bytes from the HTTP stream. Buffers partial lines and
    /// calls on_chunk for each content delta extracted from completed
    /// data events.
    void feed(std::string_view bytes,
              const std::function<void(std::string_view chunk)> &on_chunk);

    /// Feed a single complete line from the SSE stream.
    /// Returns the extracted content delta if a data event was parsed, or nullopt.
    [[nodiscard]] std::optional<std::string> feed_line(std::string_view line);

    /// Reset the parser to its initial state. Call before reusing the
    /// parser for a new HTTP request so leftover bytes from a previous
    /// (e.g. error) response do not contaminate the new stream.
    void reset() {
        done_ = false;
        line_buffer_.clear();
    }

    /// Check if the stream has signaled completion ([DONE]).
    [[nodiscard]] bool is_done() const {
        return done_;
    }

  private:
    bool done_{false};
    std::string line_buffer_;
};

} // namespace ahfl::llm_provider
