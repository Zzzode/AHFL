#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace ahfl::llm_provider {

/// Callback invoked for each streaming chunk.
/// @param chunk The content delta text.
/// @param done  True if this is the final chunk.
using StreamChunkCallback = std::function<void(std::string_view chunk, bool done)>;

/// Parses Server-Sent Events (SSE) lines from an LLM streaming response.
class SSEParser {
  public:
    /// Feed a single line from the SSE stream.
    /// Returns the extracted content delta if a data event was parsed, or nullopt.
    [[nodiscard]] std::optional<std::string> feed_line(std::string_view line);

    /// Check if the stream has signaled completion ([DONE]).
    [[nodiscard]] bool is_done() const { return done_; }

  private:
    bool done_{false};
};

/// Result of a streaming request.
struct StreamResult {
    bool success{false};
    std::string full_content;
    std::string error;
};

/// Streaming client for SSE chat completion responses.
class StreamingClient {
  public:
    explicit StreamingClient(std::string_view endpoint, std::string_view api_key,
                             std::string_view model);

    /// Execute a streaming chat completion request.
    [[nodiscard]] StreamResult stream(const std::string &request_json, StreamChunkCallback cb);

  private:
    std::string endpoint_;
    std::string api_key_;
    std::string model_;
};

} // namespace ahfl::llm_provider
