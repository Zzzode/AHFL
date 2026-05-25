#include "llm_provider/streaming.hpp"

#include <cassert>
#include <iostream>
#include <string>

namespace {

using namespace ahfl::llm_provider;

bool test_feed_line_data_event() {
    SSEParser parser;
    auto result = parser.feed_line("data: {\"choices\":[{\"delta\":{\"content\":\"hello\"}}]}");
    if (!result.has_value()) return false;
    if (*result != "hello") return false;
    return true;
}

bool test_feed_line_unescapes_json_content() {
    SSEParser parser;
    auto result =
        parser.feed_line("data: {\"choices\":[{\"delta\":{\"content\":\"line\\nnext\"}}]}");
    if (!result.has_value()) return false;
    if (*result != "line\nnext") return false;
    return true;
}

bool test_feed_line_empty_returns_nullopt() {
    SSEParser parser;
    auto result = parser.feed_line("");
    if (result.has_value()) return false;
    return true;
}

bool test_feed_line_done_signal() {
    SSEParser parser;
    auto result = parser.feed_line("data: [DONE]");
    // [DONE] returns nullopt but sets done flag
    if (result.has_value()) return false;
    if (!parser.is_done()) return false;
    return true;
}

bool test_feed_line_comment_returns_nullopt() {
    SSEParser parser;
    auto result = parser.feed_line(": comment");
    if (result.has_value()) return false;
    // Parser should not be marked as done for comments
    if (parser.is_done()) return false;
    return true;
}

} // namespace

int main() {
    int failures = 0;
    auto run = [&](bool (*fn)(), const char *name) {
        if (!fn()) {
            std::cerr << "FAIL: " << name << "\n";
            ++failures;
        }
    };

    run(test_feed_line_data_event, "test_feed_line_data_event");
    run(test_feed_line_unescapes_json_content, "test_feed_line_unescapes_json_content");
    run(test_feed_line_empty_returns_nullopt, "test_feed_line_empty_returns_nullopt");
    run(test_feed_line_done_signal, "test_feed_line_done_signal");
    run(test_feed_line_comment_returns_nullopt, "test_feed_line_comment_returns_nullopt");

    if (failures > 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cerr << "All tests passed\n";
    return 0;
}
