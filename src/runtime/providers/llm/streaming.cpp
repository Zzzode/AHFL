#include "runtime/providers/llm/streaming.hpp"

#include "base/json/json_value.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace ahfl::llm_provider {

std::optional<std::string> SSEParser::feed_line(std::string_view line) {
    if (done_) {
        return std::nullopt;
    }

    // SSE format: "data: <json>" or "data: [DONE]"
    if (line.starts_with("data: ")) {
        auto data = line.substr(6);
        if (data == "[DONE]") {
            done_ = true;
            return std::nullopt;
        }
        auto parsed = ahfl::json::parse_json(data);
        if (!parsed.has_value() || !*parsed) {
            return std::nullopt;
        }

        const auto *root = parsed->get();
        if (const auto *content = root->get("content")) {
            if (auto value = content->as_string(); value.has_value()) {
                return std::string(*value);
            }
        }

        const auto *choices = root->get("choices");
        if (choices == nullptr || !choices->is_array() || choices->array_items.empty()) {
            return std::nullopt;
        }
        const auto *choice = choices->array_items.front().get();
        const auto *delta = choice != nullptr ? choice->get("delta") : nullptr;
        const auto *content = delta != nullptr ? delta->get("content") : nullptr;
        if (content == nullptr) {
            return std::nullopt;
        }
        if (auto value = content->as_string(); value.has_value()) {
            return std::string(*value);
        }
    }
    return std::nullopt;
}

void SSEParser::feed(std::string_view bytes,
                     const std::function<void(std::string_view chunk)> &on_chunk) {
    if (done_) {
        return;
    }

    line_buffer_.append(bytes);

    // Process all complete lines in the buffer.
    std::size_t pos = 0;
    while (true) {
        const auto newline = line_buffer_.find('\n', pos);
        if (newline == std::string::npos) {
            break;
        }
        std::string_view line(line_buffer_.data() + pos, newline - pos);
        // Strip trailing \r
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (!line.empty()) {
            auto chunk = feed_line(line);
            if (chunk.has_value() && on_chunk) {
                on_chunk(*chunk);
            }
        }
        if (done_) {
            line_buffer_.clear();
            return;
        }
        pos = newline + 1;
    }

    // Remove consumed lines from the buffer.
    if (pos > 0) {
        line_buffer_.erase(0, pos);
    }
}

} // namespace ahfl::llm_provider
