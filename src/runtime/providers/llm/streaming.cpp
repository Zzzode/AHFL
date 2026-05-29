#include "runtime/providers/llm/streaming.hpp"

#include "base/json/json_value.hpp"
#include "base/support/curl.hpp"

#include <sstream>
#include <string>

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

StreamingClient::StreamingClient(std::string_view endpoint, std::string_view api_key,
                                 std::string_view model)
    : endpoint_(endpoint), api_key_(api_key), model_(model) {}

StreamResult StreamingClient::stream(const std::string &request_json, StreamChunkCallback cb) {
    StreamResult result;

    ahfl::support::CurlRequest request;
    request.method = "POST";
    request.url = endpoint_;
    request.headers = {
        {"Content-Type", "application/json"},
        {"Authorization", "Bearer " + api_key_},
    };
    request.body = request_json;

    const auto response = ahfl::support::execute_curl(request);
    if (response.status_code == 0 && !response.error.empty()) {
        result.error = response.error;
        return result;
    }

    SSEParser parser;
    std::string line;
    std::istringstream lines(response.body);
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        auto chunk = parser.feed_line(line);
        if (chunk.has_value()) {
            result.full_content += *chunk;
            if (cb) {
                cb(*chunk, false);
            }
        }
        if (parser.is_done()) {
            break;
        }
    }

    if (cb) {
        cb("", true);
    }

    result.success = response.is_success();
    if (!result.success) {
        result.error = response.error.empty() ? "stream request failed with status " +
                                                   std::to_string(response.status_code)
                                             : response.error;
    }
    return result;
}

} // namespace ahfl::llm_provider
