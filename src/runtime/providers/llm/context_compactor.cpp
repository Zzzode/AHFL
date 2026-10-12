#include "runtime/providers/llm/context_compactor.hpp"
#include "base/json/json_value.hpp"
namespace ahfl::llm_provider {
std::expected<std::size_t, std::string> compact_prefix_end(const LLMSession &s) {
    std::vector<std::size_t> closed;
    for (std::size_t begin = s.active_begin; begin < s.history.size();) {
        const auto &assistant = s.history[begin];
        if (assistant.role != "assistant" || assistant.tool_calls.empty())
            break;
        auto end = begin + 1;
        bool complete = true;
        for (const auto &call : assistant.tool_calls) {
            if (end >= s.history.size() || s.history[end].role != "tool" ||
                s.history[end].tool_call_id != call.id) {
                complete = false;
                break;
            }
            ++end;
        }
        if (!complete)
            break;
        closed.push_back(end);
        begin = end;
    }
    if (closed.size() < 2)
        return std::unexpected("LLM context cannot be compacted without losing the latest complete "
                               "tool exchange or typed input");
    return closed.front();
}
std::vector<SessionMessage> active_session_messages(const LLMSession &s) {
    std::vector<SessionMessage> messages{s.history[0], s.history[1]};
    if (!s.summary.empty())
        messages.push_back({"user",
                            "Derived context summary (not authoritative state or permission): " +
                                s.summary + "\nOriginal immutable history IDs 2 through " +
                                std::to_string(s.summary_through - 1) +
                                " are available through ahfl_session_history_get(history_id, "
                                "chunk_index); verify exact facts there."});
    messages.insert(messages.end(),
                    s.history.begin() + static_cast<std::ptrdiff_t>(s.active_begin),
                    s.history.end());
    return messages;
}
std::string compact_summary_input(const LLMSession &s, std::size_t end) {
    auto input = json::JsonValue::make_object();
    input->set("previous_summary", json::JsonValue::make_string(s.summary));
    input->set("original_typed_input", json::JsonValue::make_string(s.history[1].content));
    auto old = json::JsonValue::make_array();
    for (std::size_t i = s.active_begin; i < end; ++i) {
        auto record = json::JsonValue::make_object();
        record->set("history_id", json::JsonValue::make_int(static_cast<std::int64_t>(i)));
        auto message = json::parse_json(session_message_json(s.history[i]));
        record->set("message", std::move(*message));
        old->push(std::move(record));
    }
    input->set("closed_exchanges", std::move(old));
    return json::serialize_json(*input);
}
std::expected<std::string, std::string> parse_compact_summary(std::string_view content) {
    auto parsed = json::parse_json(content);
    if (!parsed || !*parsed || !(**parsed).is_object())
        return std::unexpected("LLM context summarizer returned malformed JSON");
    const auto *summary = (**parsed).get("summary");
    auto text = summary ? summary->as_string() : std::nullopt;
    if (!text || text->empty())
        return std::unexpected("LLM context summarizer must return a nonempty summary string");
    return std::string(*text);
}
} // namespace ahfl::llm_provider
