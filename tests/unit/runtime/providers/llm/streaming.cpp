#include "runtime/providers/llm/streaming.hpp"

#include <cassert>
#include <iostream>
#include <string>

namespace {

using namespace ahfl::llm_provider;

bool test_feed_line_data_event() {
    SSEParser parser;
    auto result = parser.feed_line("data: {\"choices\":[{\"delta\":{\"content\":\"hello\"}}]}");
    if (!result.has_value())
        return false;
    if (*result != "hello")
        return false;
    return true;
}

bool test_feed_line_unescapes_json_content() {
    SSEParser parser;
    auto result =
        parser.feed_line("data: {\"choices\":[{\"delta\":{\"content\":\"line\\nnext\"}}]}");
    if (!result.has_value())
        return false;
    if (*result != "line\nnext")
        return false;
    return true;
}

bool test_feed_line_empty_returns_nullopt() {
    SSEParser parser;
    auto result = parser.feed_line("");
    if (result.has_value())
        return false;
    return true;
}

bool test_feed_line_done_signal() {
    SSEParser parser;
    auto result = parser.feed_line("data: [DONE]");
    // [DONE] returns nullopt but sets done flag
    if (result.has_value())
        return false;
    if (!parser.is_done())
        return false;
    return true;
}

bool test_feed_line_comment_returns_nullopt() {
    SSEParser parser;
    auto result = parser.feed_line(": comment");
    if (result.has_value())
        return false;
    // Parser should not be marked as done for comments
    if (parser.is_done())
        return false;
    return true;
}

bool test_content_without_done_keeps_stream_open() {
    SSEParser parser;
    auto result = parser.feed_line("data: {\"choices\":[{\"delta\":{\"content\":\"hello\"}}]}");
    if (!result.has_value())
        return false;
    if (*result != "hello")
        return false;
    if (parser.is_done())
        return false;
    return true;
}

// --- feed() incremental byte-feeding tests ---

bool test_feed_incremental_partial_lines() {
    SSEParser parser;
    std::string accumulated;
    auto on_chunk = [&](std::string_view chunk) { accumulated += chunk; };

    // Feed the SSE stream one byte at a time to prove partial-line buffering.
    const std::string stream =
        "data: {\"choices\":[{\"delta\":{\"content\":\"hel\"}}]}\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"lo\"}}]}\n"
        "data: [DONE]\n";
    for (char c : stream) {
        parser.feed(std::string_view(&c, 1), on_chunk);
    }
    if (accumulated != "hello")
        return false;
    if (!parser.is_done())
        return false;
    return true;
}

bool test_feed_multiple_lines_one_chunk() {
    SSEParser parser;
    std::string accumulated;
    auto on_chunk = [&](std::string_view chunk) { accumulated += chunk; };

    // Feed the entire stream in one call.
    parser.feed(
        "data: {\"choices\":[{\"delta\":{\"content\":\"abc\"}}]}\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"def\"}}]}\n"
        "data: [DONE]\n",
        on_chunk);
    if (accumulated != "abcdef")
        return false;
    if (!parser.is_done())
        return false;
    return true;
}

bool test_feed_split_across_calls() {
    SSEParser parser;
    std::string accumulated;
    auto on_chunk = [&](std::string_view chunk) { accumulated += chunk; };

    // Split a single SSE line across two feed calls.
    parser.feed("data: {\"choices\":[{\"delta\":{\"content\":\"spl", on_chunk);
    parser.feed("it\"}}]}\n", on_chunk);
    if (accumulated != "split")
        return false;
    if (parser.is_done())
        return false;
    return true;
}

bool test_feed_reset_clears_state() {
    SSEParser parser;
    std::string accumulated;
    auto on_chunk = [&](std::string_view chunk) { accumulated += chunk; };

    // Feed a partial line, then reset.
    parser.feed("data: {\"choices\":[{\"delta\":{\"content\":\"orphan", on_chunk);
    parser.reset();
    if (parser.is_done())
        return false;

    // After reset, a fresh stream should parse correctly.
    parser.feed(
        "data: {\"choices\":[{\"delta\":{\"content\":\"fresh\"}}]}\n"
        "data: [DONE]\n",
        on_chunk);
    if (accumulated != "fresh")
        return false;
    if (!parser.is_done())
        return false;
    return true;
}

bool test_feed_done_stops_processing() {
    SSEParser parser;
    std::string accumulated;
    auto on_chunk = [&](std::string_view chunk) { accumulated += chunk; };

    // [DONE] should stop all further processing.
    parser.feed("data: [DONE]\n", on_chunk);
    parser.feed("data: {\"choices\":[{\"delta\":{\"content\":\"after-done\"}}]}\n", on_chunk);
    if (!accumulated.empty())
        return false;
    if (!parser.is_done())
        return false;
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
    run(test_content_without_done_keeps_stream_open, "test_content_without_done_keeps_stream_open");
    run(test_feed_incremental_partial_lines, "test_feed_incremental_partial_lines");
    run(test_feed_multiple_lines_one_chunk, "test_feed_multiple_lines_one_chunk");
    run(test_feed_split_across_calls, "test_feed_split_across_calls");
    run(test_feed_reset_clears_state, "test_feed_reset_clears_state");
    run(test_feed_done_stops_processing, "test_feed_done_stops_processing");

    if (failures > 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cerr << "All tests passed\n";
    return 0;
}
