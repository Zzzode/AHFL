#include "ahfl/llm_provider/streaming.hpp"

#include <cstdio>
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
        // Extract "content" field from the JSON delta.
        // Minimal extraction: find "delta":{"content":"..."} pattern
        constexpr std::string_view content_key = "\"content\":\"";
        auto pos = data.find(content_key);
        if (pos == std::string_view::npos) {
            return std::nullopt;
        }
        pos += content_key.size();
        std::string result;
        while (pos < data.size() && data[pos] != '"') {
            if (data[pos] == '\\' && pos + 1 < data.size()) {
                ++pos;
                switch (data[pos]) {
                case 'n':
                    result += '\n';
                    break;
                case 't':
                    result += '\t';
                    break;
                case '"':
                    result += '"';
                    break;
                case '\\':
                    result += '\\';
                    break;
                default:
                    result += data[pos];
                    break;
                }
            } else {
                result += data[pos];
            }
            ++pos;
        }
        if (!result.empty()) {
            return result;
        }
    }
    return std::nullopt;
}

StreamingClient::StreamingClient(std::string_view endpoint, std::string_view api_key,
                                 std::string_view model)
    : endpoint_(endpoint), api_key_(api_key), model_(model) {}

StreamResult StreamingClient::stream(const std::string &request_json, StreamChunkCallback cb) {
    StreamResult result;

    // Build curl command for streaming
    std::string cmd = "curl -sS -N -X POST \"" + endpoint_ +
                      "\" -H \"Content-Type: application/json\" "
                      "-H \"Authorization: Bearer " +
                      api_key_ + "\" -d '" + request_json + "' 2>&1";

    FILE *pipe = popen(cmd.c_str(), "r");
    if (pipe == nullptr) {
        result.error = "failed to open pipe for curl";
        return result;
    }

    SSEParser parser;
    std::string line;
    char buf[4096];
    std::string buffer;

    while (fgets(buf, sizeof(buf), pipe) != nullptr) {
        buffer += buf;
        // Process complete lines
        std::size_t newline_pos = 0;
        while ((newline_pos = buffer.find('\n')) != std::string::npos) {
            line = buffer.substr(0, newline_pos);
            buffer.erase(0, newline_pos + 1);
            // Strip trailing \r
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
        if (parser.is_done()) {
            break;
        }
    }

    pclose(pipe);

    if (cb) {
        cb("", true);
    }

    result.success = true;
    return result;
}

} // namespace ahfl::llm_provider
