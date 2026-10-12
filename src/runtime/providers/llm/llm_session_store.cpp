#include "runtime/providers/llm/llm_session_store.hpp"

#include "base/json/json_value.hpp"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <system_error>
#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace ahfl::llm_provider {
namespace {
using json::JsonValue;
std::string hex(const support::Sha256Digest &digest) {
    constexpr char chars[] = "0123456789abcdef";
    std::string value;
    for (const auto byte : digest) {
        value += chars[byte >> 4];
        value += chars[byte & 15];
    }
    return value;
}
bool unique_fields(const JsonValue &value) {
    if (value.is_object()) {
        if (value.object_fields.size() > 128)
            return false;
        for (std::size_t i = 0; i < value.object_fields.size(); ++i) {
            for (std::size_t j = 0; j < i; ++j)
                if (value.object_fields[i].first == value.object_fields[j].first)
                    return false;
            if (!value.object_fields[i].second || !unique_fields(*value.object_fields[i].second))
                return false;
        }
    } else if (value.is_array())
        for (const auto &child : value.array_items)
            if (!child || !unique_fields(*child))
                return false;
    return true;
}
std::expected<void, std::string> private_artifact(const std::filesystem::path &path,
                                                  bool directory = false) {
#if defined(__unix__) || defined(__APPLE__)
    struct stat st {};
    if (::lstat(path.c_str(), &st) != 0 ||
        (directory ? !S_ISDIR(st.st_mode) : !S_ISREG(st.st_mode)) || st.st_uid != ::geteuid() ||
        (st.st_mode & 0077) != 0)
        return std::unexpected(
            "LLM session artifact must be owned by the current user with private permissions");
    if (!directory && (st.st_size < 0 || st.st_size > 64 * 1024 * 1024))
        return std::unexpected("LLM session artifact exceeds the 64 MiB admission limit");
    return {};
#else
    (void)path;
    (void)directory;
    return std::unexpected("private LLM sessions require POSIX file permissions");
#endif
}
std::optional<std::unique_ptr<JsonValue>> canonical_json(std::string_view bytes) {
    auto parsed = json::parse_json(bytes);
    // The shared JSON DOM uses last-key-wins. Store artifacts have one exact
    // writer encoding: reserialization rejects duplicate keys and alternate
    // spellings without changing the shared parser's public contract.
    if (!parsed || !*parsed || json::serialize_json(**parsed) != bytes)
        return std::nullopt;
    return parsed;
}
bool numeric_record_path(const std::filesystem::path &path) {
    const auto name = path.stem().string();
    return path.extension() == ".json" && !name.empty() &&
           std::all_of(name.begin(), name.end(), [](char c) { return c >= '0' && c <= '9'; });
}
std::string read_file(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return {};
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
std::expected<void, std::string> private_write(const std::filesystem::path &path,
                                               std::string_view content) {
#if defined(__unix__) || defined(__APPLE__)
    auto temp = path;
    temp += ".tmp";
    const int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
    if (fd < 0)
        return std::unexpected("cannot create private LLM session artifact");
    bool ok = ::fchmod(fd, 0600) == 0;
    std::size_t written = 0;
    while (ok && written < content.size()) {
        const auto count = ::write(fd, content.data() + written, content.size() - written);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0) {
            ok = false;
            break;
        }
        written += static_cast<std::size_t>(count);
    }
    if (ok)
        ok = ::fsync(fd) == 0;
    ::close(fd);
    if (!ok || ::rename(temp.c_str(), path.c_str()) != 0)
        return std::unexpected("cannot durably save LLM session artifact");
    const int directory = ::open(path.parent_path().c_str(), O_RDONLY);
    if (directory < 0)
        return std::unexpected("cannot sync LLM session directory");
    const bool synced = ::fsync(directory) == 0;
    ::close(directory);
    if (!synced)
        return std::unexpected("cannot durably sync LLM session directory");
    return {};
#else
    (void)path;
    (void)content;
    return std::unexpected("durable LLM sessions require a POSIX filesystem");
#endif
}
void integer(JsonValue &value, const char *name, std::uint64_t number) {
    value.set(name, JsonValue::make_int(static_cast<std::int64_t>(number)));
}
std::unique_ptr<JsonValue> encode(const LLMSession &s) {
    auto v = JsonValue::make_object();
    v->set("format", JsonValue::make_string("ahfl.llm-session"));
    integer(*v, "id", s.id);
    v->set("binding", JsonValue::make_string(s.binding));
    integer(*v, "workflow", s.workflow);
    integer(*v, "node", s.node);
    integer(*v, "capability", s.capability);
    integer(*v, "ordinal", s.ordinal);
    v->set("prepared", JsonValue::make_bool(s.prepared));
    v->set("completed", JsonValue::make_bool(s.completed));
    auto history = JsonValue::make_array();
    for (const auto &m : s.history) {
        auto parsed = json::parse_json(session_message_json(m));
        history->push(std::move(*parsed));
    }
    v->set("history", std::move(history));
    integer(*v, "active_begin", s.active_begin);
    integer(*v, "summary_through", s.summary_through);
    v->set("summary", JsonValue::make_string(s.summary));
    integer(*v, "round", s.round);
    auto receipts = JsonValue::make_array();
    for (const auto &r : s.receipts) {
        auto receipt = JsonValue::make_object();
        integer(*receipt, "turn", r.turn);
        integer(*receipt, "ordinal", r.ordinal);
        integer(*receipt, "definition", r.definition);
        integer(*receipt, "state", static_cast<std::uint64_t>(r.state));
        receipt->set("id", JsonValue::make_string(r.result.tool_call_id));
        receipt->set("content", JsonValue::make_string(r.result.content));
        receipt->set("success", JsonValue::make_bool(r.result.success));
        receipt->set("error", JsonValue::make_string(r.result.error_message));
        receipt->set("call_id", JsonValue::make_string(r.call.id));
        receipt->set("call_name", JsonValue::make_string(r.call.name));
        receipt->set("call_arguments", JsonValue::make_string(r.call.arguments_json));
        receipt->set("schema_digest", JsonValue::make_string(r.schema_digest));
        auto key = JsonValue::make_int(0);
        if (r.idempotency_key <= static_cast<std::uint64_t>(INT64_MAX))
            key = JsonValue::make_int(static_cast<std::int64_t>(r.idempotency_key));
        else {
            key->number_provenance = json::NumberProvenance::UnsignedInteger;
            key->uint_val = r.idempotency_key;
        }
        receipt->set("idempotency_key", std::move(key));
        receipts->push(std::move(receipt));
    }
    v->set("receipts", std::move(receipts));
    v->set("completed_wire", JsonValue::make_string(s.completed_wire));
    v->set("completed_wire_digest", JsonValue::make_string(s.completed_wire_digest));
    v->set("completed_content", JsonValue::make_string(s.completed_content));
    v->set("response_wire", JsonValue::make_string(s.response_wire));
    v->set("response_pending", JsonValue::make_bool(s.response_pending));
    v->set("summary_request", JsonValue::make_bool(s.summary_request));
    integer(*v, "summary_prefix_end", s.summary_prefix_end);
    integer(*v, "summary_before_tokens", s.summary_before_tokens);
    auto failure = JsonValue::make_null();
    if (s.terminal_failure) {
        failure = JsonValue::make_object();
        integer(*failure, "status", static_cast<std::uint64_t>(s.terminal_failure->status));
        failure->set(
            "failure_kind",
            s.terminal_failure->failure_kind
                ? JsonValue::make_int(static_cast<std::int64_t>(*s.terminal_failure->failure_kind))
                : JsonValue::make_null());
        failure->set("error_text", JsonValue::make_string(s.terminal_failure->error_text));
        failure->set("diagnostic_code",
                     JsonValue::make_string(s.terminal_failure->diagnostic_code));
        failure->set("usage_present", JsonValue::make_bool(s.terminal_failure->usage_present));
    }
    v->set("terminal_failure", std::move(failure));
    v->set("request_digest", JsonValue::make_string(s.request_digest));
    v->set("request_in_flight", JsonValue::make_bool(s.request_in_flight));
    integer(*v, "reserved_tokens", s.reserved_tokens);
    integer(*v, "prompt_tokens", s.prompt_tokens);
    integer(*v, "completion_tokens", s.completion_tokens);
    integer(*v, "total_tokens", s.total_tokens);
    v->set("total_cost_usd", JsonValue::make_float(s.total_cost_usd));
    v->set("cost_estimated", JsonValue::make_bool(s.cost_estimated));
    integer(*v, "attempts", s.attempts);
    auto notices = JsonValue::make_array();
    for (const auto &notice : s.usage_notices) {
        auto value = JsonValue::make_object();
        value->set("diagnostic_code", JsonValue::make_string(notice.diagnostic_code));
        value->set("message", JsonValue::make_string(notice.message));
        notices->push(std::move(value));
    }
    v->set("usage_notices", std::move(notices));
    v->set("cache_hit", JsonValue::make_bool(s.cache_hit));
    v->set("provider_degraded", JsonValue::make_bool(s.provider_degraded));
    v->set("degraded_provider_name", JsonValue::make_string(s.degraded_provider_name));
    v->set("selected_provider_name", JsonValue::make_string(s.selected_provider_name));

    return v;
}
std::expected<LLMSession, std::string> decode(const JsonValue &v) {
    const std::string error = "LLM session record is malformed; restore a valid checkpoint";
    bool valid = v.is_object() && unique_fields(v);
    constexpr std::string_view allowed[] = {"active_begin",
                                            "attempts",
                                            "binding",
                                            "capability",
                                            "completed",
                                            "completed_content",
                                            "completed_wire",
                                            "completed_wire_digest",
                                            "completion_tokens",
                                            "history",
                                            "id",
                                            "node",
                                            "ordinal",
                                            "prepared",
                                            "prompt_tokens",
                                            "receipts",
                                            "request_digest",
                                            "request_in_flight",
                                            "reserved_tokens",
                                            "response_pending",
                                            "response_wire",
                                            "round",
                                            "format",
                                            "summary",
                                            "summary_before_tokens",
                                            "summary_prefix_end",
                                            "summary_request",
                                            "summary_through",
                                            "terminal_failure",
                                            "total_cost_usd",
                                            "cost_estimated",
                                            "total_tokens",
                                            "workflow",
                                            "usage_notices",
                                            "cache_hit",
                                            "provider_degraded",
                                            "degraded_provider_name",
                                            "selected_provider_name"};
    for (const auto &[field, _] : v.object_fields)
        if (std::find(std::begin(allowed), std::end(allowed), field) == std::end(allowed))
            valid = false;
    const auto number = [&](const JsonValue &value, const char *field) -> std::uint64_t {
        const auto *f = value.get(field);
        auto n = f ? f->as_uint() : std::nullopt;
        if (!n || *n > static_cast<std::uint64_t>(INT64_MAX)) {
            valid = false;
            return 0;
        }
        return *n;
    };
    const auto string = [&](const JsonValue &value, const char *field) {
        const auto *f = value.get(field);
        auto s = f ? f->as_string() : std::nullopt;
        if (!s) {
            valid = false;
            return std::string{};
        }
        return std::string(*s);
    };
    const auto boolean = [&](const JsonValue &value, const char *field) {
        const auto *f = value.get(field);
        auto b = f ? f->as_bool() : std::nullopt;
        if (!b) {
            valid = false;
            return false;
        }
        return *b;
    };
    if (string(v, "format") != "ahfl.llm-session")
        valid = false;
    LLMSession s;
    s.id = number(v, "id");
    s.binding = string(v, "binding");
    s.workflow = number(v, "workflow");
    s.node = number(v, "node");
    s.capability = number(v, "capability");
    s.ordinal = number(v, "ordinal");
    s.prepared = boolean(v, "prepared");
    s.completed = boolean(v, "completed");
    const auto *history = v.get("history");
    if (!history || !history->is_array() || history->array_items.size() > 65536)
        return std::unexpected(error);
    for (const auto &m : history->array_items) {
        if (!m || !m->is_object())
            return std::unexpected(error);
        SessionMessage message;
        message.role = string(*m, "role");
        if (const auto *c = m->get("content"); c && c->is_null())
            message.content = {};
        else
            message.content = string(*m, "content");
        if (const auto *id = m->get("tool_call_id"); id)
            message.tool_call_id = string(*m, "tool_call_id");
        if (const auto *calls = m->get("tool_calls"); calls) {
            if (!calls->is_array() || calls->array_items.size() > 4096)
                return std::unexpected(error);
            for (const auto &call : calls->array_items) {
                if (!call)
                    return std::unexpected(error);
                const auto *fn = call->get("function");
                if (!fn)
                    return std::unexpected(error);
                message.tool_calls.push_back(
                    {string(*call, "id"), string(*fn, "name"), string(*fn, "arguments")});
            }
        }
        s.history.push_back(std::move(message));
    }
    s.active_begin = number(v, "active_begin");
    s.summary_through = number(v, "summary_through");
    s.summary = string(v, "summary");
    s.round = number(v, "round");
    const auto *receipts = v.get("receipts");
    if (!receipts || !receipts->is_array() || receipts->array_items.size() > 16384)
        return std::unexpected(error);
    for (const auto &r : receipts->array_items) {
        if (!r)
            return std::unexpected(error);
        if (!r->get("idempotency_key") || !r->get("idempotency_key")->as_uint())
            return std::unexpected(error);
        const auto state = number(*r, "state");
        if (state > 2)
            valid = false;
        s.receipts.push_back(
            {number(*r, "turn"),
             number(*r, "ordinal"),
             number(*r, "definition"),
             static_cast<ToolReceiptState>(state),
             {string(*r, "id"), string(*r, "content"), boolean(*r, "success"), string(*r, "error")},
             {string(*r, "call_id"), string(*r, "call_name"), string(*r, "call_arguments")},
             string(*r, "schema_digest"),
             r->get("idempotency_key") && r->get("idempotency_key")->as_uint()
                 ? *r->get("idempotency_key")->as_uint()
                 : 0});
    }
    s.completed_wire = string(v, "completed_wire");
    s.completed_wire_digest = string(v, "completed_wire_digest");
    s.completed_content = string(v, "completed_content");
    s.request_digest = string(v, "request_digest");
    s.response_wire = string(v, "response_wire");
    s.response_pending = boolean(v, "response_pending");
    s.summary_request = boolean(v, "summary_request");
    const auto *failure = v.get("terminal_failure");
    if (!failure)
        return std::unexpected(error);
    if (!failure->is_null()) {
        if (!failure->is_object() || failure->object_fields.size() != 5)
            return std::unexpected(error);
        for (const auto &[field, _] : failure->object_fields)
            if (field != "status" && field != "failure_kind" && field != "error_text" &&
                field != "diagnostic_code" && field != "usage_present")
                return std::unexpected(error);
        const auto status = number(*failure, "status");
        if (status < static_cast<std::uint64_t>(runtime::CapabilityCallStatus::Error) ||
            status > static_cast<std::uint64_t>(runtime::CapabilityCallStatus::CircuitOpen))
            return std::unexpected(error);
        SessionTerminalFailure terminal;
        terminal.status = static_cast<runtime::CapabilityCallStatus>(status);
        const auto *kind = failure->get("failure_kind");
        if (!kind)
            return std::unexpected(error);
        if (!kind->is_null()) {
            const auto value = kind->as_uint();
            if (!value ||
                *value > static_cast<std::uint64_t>(runtime::CapabilityFailureKind::Interrupted))
                return std::unexpected(error);
            terminal.failure_kind = static_cast<runtime::CapabilityFailureKind>(*value);
        }
        terminal.error_text = string(*failure, "error_text");
        terminal.diagnostic_code = string(*failure, "diagnostic_code");
        terminal.usage_present = boolean(*failure, "usage_present");
        if (terminal.error_text.empty())
            valid = false;
        s.terminal_failure = std::move(terminal);
    }
    s.summary_prefix_end = number(v, "summary_prefix_end");
    s.summary_before_tokens = number(v, "summary_before_tokens");
    s.request_in_flight = boolean(v, "request_in_flight");
    s.reserved_tokens = number(v, "reserved_tokens");
    s.prompt_tokens = number(v, "prompt_tokens");
    s.completion_tokens = number(v, "completion_tokens");
    s.total_tokens = number(v, "total_tokens");
    s.attempts = number(v, "attempts");
    s.cost_estimated = boolean(v, "cost_estimated");
    if (s.terminal_failure && (s.completed || s.prepared || s.request_in_flight ||
                               s.response_pending || !s.completed_content.empty() ||
                               !s.completed_wire.empty() || !s.completed_wire_digest.empty() ||
                               (s.attempts != 0 && !s.terminal_failure->usage_present)))
        valid = false;
    const auto *notices = v.get("usage_notices");
    if (!notices || !notices->is_array() || notices->array_items.size() > 4096)
        return std::unexpected(error);
    for (const auto &notice : notices->array_items) {
        if (!notice)
            return std::unexpected(error);
        s.usage_notices.push_back({string(*notice, "diagnostic_code"), string(*notice, "message")});
    }

    s.cache_hit = boolean(v, "cache_hit");
    s.provider_degraded = boolean(v, "provider_degraded");
    s.degraded_provider_name = string(v, "degraded_provider_name");
    s.selected_provider_name = string(v, "selected_provider_name");
    const auto *cost = v.get("total_cost_usd");
    const auto c = cost ? cost->as_float() : std::nullopt;
    if (!c || !std::isfinite(*c) || *c < 0)
        valid = false;
    else
        s.total_cost_usd = *c;
    if (s.history.size() < 2 || s.history[0].role != "system" || s.history[1].role != "user" ||
        s.active_begin < 2 || s.active_begin > s.history.size() ||
        s.summary_through != s.active_begin || s.binding.size() != 64 || s.id == 0)
        valid = false;
    const auto is_digest = [](std::string_view digest) {
        return digest.size() == 64 && std::all_of(digest.begin(), digest.end(), [](char c) {
                   return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
               });
    };
    if (!is_digest(s.binding))
        valid = false;
    if (s.request_in_flight && (s.response_pending || !is_digest(s.request_digest)))
        valid = false;
    if (s.response_pending && s.response_wire.empty())
        valid = false;
    if (s.summary.empty() ? s.active_begin != 2 : s.active_begin <= 2)
        valid = false;
    if (s.summary_prefix_end < 2 || s.summary_prefix_end > s.history.size())
        valid = false;
    if (s.completed && (s.prepared || s.request_in_flight || s.response_pending ||
                        s.completed_content.empty() || s.completed_wire.empty() ||
                        s.completed_wire_digest != support::sha256_hex(s.completed_wire)))
        valid = false;
    if (s.prepared && (s.history.size() != 2 || !s.receipts.empty() || s.round != 0 ||
                       s.attempts != 0 || s.request_in_flight || s.response_pending ||
                       s.total_tokens != 0 || s.reserved_tokens != 0))
        valid = false;
    std::size_t exchanges = 0, expected_receipts = 0;
    bool active_boundary = s.active_begin == 2;
    for (std::size_t turn = 2; turn < s.history.size();) {
        const auto &assistant = s.history[turn];
        if (assistant.role != "assistant" || assistant.tool_calls.empty()) {
            valid = false;
            break;
        }
        ++exchanges;
        std::size_t end = turn + 1;
        for (std::size_t ordinal = 0; ordinal < assistant.tool_calls.size(); ++ordinal) {
            const auto &call = assistant.tool_calls[ordinal];
            const auto receipt =
                std::find_if(s.receipts.begin(), s.receipts.end(), [&](const auto &r) {
                    return r.turn == turn && r.ordinal == ordinal;
                });
            if (receipt == s.receipts.end()) {
                valid = false;
                break;
            }
            ++expected_receipts;
            if (std::count_if(
                    s.receipts.begin(),
                    s.receipts.end(),
                    [&](const auto &r) { return r.turn == turn && r.ordinal == ordinal; }) != 1 ||
                receipt->call.id != call.id || receipt->call.name != call.name ||
                receipt->call.arguments_json != call.arguments_json ||
                !is_digest(receipt->schema_digest) || call.id.empty())
                valid = false;
            if (receipt->state != ToolReceiptState::Prepared && receipt->idempotency_key == 0)
                valid = false;
            if (s.terminal_failure && receipt->state == ToolReceiptState::InFlight &&
                call.name != "ahfl_session_history_get")
                valid = false;
            if (receipt->state == ToolReceiptState::Completed &&
                receipt->result.tool_call_id != call.id)
                valid = false;
            if (end < s.history.size() && s.history[end].role == "tool") {
                if (receipt->state != ToolReceiptState::Completed ||
                    s.history[end].tool_call_id != call.id ||
                    s.history[end].content != receipt->result.content)
                    valid = false;
                ++end;
            } else if (end < s.history.size() || ordinal + 1 < assistant.tool_calls.size()) {
                // More prepared calls are allowed only at the very end of the
                // current assistant turn, with no later conversation messages.
                if (end < s.history.size())
                    valid = false;
            }
        }
        if (end == turn + 1 && end < s.history.size()) {
            valid = false;
            break;
        }
        if (end == s.active_begin && end == turn + 1 + assistant.tool_calls.size())
            active_boundary = true;
        turn = end;
    }
    if (exchanges != s.round || expected_receipts != s.receipts.size() || !active_boundary)
        valid = false;
    if (!valid)
        return std::unexpected(error);
    return s;
}
} // namespace
std::string session_message_json(const SessionMessage &m) {
    auto v = JsonValue::make_object();
    v->set("role", JsonValue::make_string(m.role));
    v->set("content",
           m.content.empty() && !m.tool_calls.empty() ? JsonValue::make_null()
                                                      : JsonValue::make_string(m.content));
    if (m.role == "tool")
        v->set("tool_call_id", JsonValue::make_string(m.tool_call_id));
    if (!m.tool_calls.empty()) {
        auto calls = JsonValue::make_array();
        for (const auto &c : m.tool_calls) {
            auto call = JsonValue::make_object();
            call->set("id", JsonValue::make_string(c.id));
            call->set("type", JsonValue::make_string("function"));
            auto fn = JsonValue::make_object();
            fn->set("name", JsonValue::make_string(c.name));
            fn->set("arguments", JsonValue::make_string(c.arguments_json));
            call->set("function", std::move(fn));
            calls->push(std::move(call));
        }
        v->set("tool_calls", std::move(calls));
    }
    return json::serialize_json(*v);
}
std::expected<std::shared_ptr<LLMSessionStore>, std::string>
LLMSessionStore::open(const std::filesystem::path &root, support::Sha256Digest binding) {
    auto store = std::make_shared<LLMSessionStore>();
    store->root_ = root;
    store->run_binding_ = binding;
    std::error_code ec;
    if (std::filesystem::is_symlink(root, ec))
        return std::unexpected("LLM session directory must not be a symbolic link");
    const bool existed = std::filesystem::exists(root, ec);
    std::filesystem::create_directories(root, ec);
    if (ec)
        return std::unexpected("cannot create LLM session directory");
    if (!existed)
        std::filesystem::permissions(
            root, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, ec);
    if (auto admitted = private_artifact(root, true); !admitted)
        return std::unexpected(admitted.error());
    if (ec)
        return std::unexpected("cannot restrict LLM session directory permissions");
    const auto keypath = root / "session.key";
    if (std::filesystem::is_symlink(keypath, ec))
        return std::unexpected("LLM session key must not be a symbolic link");
    if (std::filesystem::exists(keypath, ec)) {
        if (auto admitted = private_artifact(keypath); !admitted)
            return std::unexpected(admitted.error());
        const auto bytes = read_file(keypath);
        if (bytes.size() != store->key_.size())
            return std::unexpected("LLM session key is invalid; restore the original local key");
        std::copy(bytes.begin(), bytes.end(), store->key_.begin());
    } else {
        if (std::filesystem::exists(root / "session.index", ec))
            return std::unexpected("LLM session key is missing; restore the original local key");
        for (const auto &entry : std::filesystem::directory_iterator(root, ec))
            if (numeric_record_path(entry.path()))
                return std::unexpected(
                    "LLM session key is missing; existing sessions cannot be authenticated");
        std::ifstream random("/dev/urandom", std::ios::binary);
        random.read(reinterpret_cast<char *>(store->key_.data()), store->key_.size());
        if (!random)
            return std::unexpected("cannot obtain randomness for LLM session key");
        auto saved = private_write(
            keypath, {reinterpret_cast<const char *>(store->key_.data()), store->key_.size()});
        if (!saved)
            return std::unexpected(saved.error());
    }
    auto admitted = store->sessions();
    if (!admitted)
        return std::unexpected(admitted.error());
    return store;
}
namespace {
std::expected<std::unique_ptr<JsonValue>, std::string>
read_index(const std::filesystem::path &root,
           const support::Sha256Digest &key,
           const support::Sha256Digest &run_binding) {
    if (auto admitted = private_artifact(root, true); !admitted)
        return std::unexpected(admitted.error());
    if (auto admitted = private_artifact(root / "session.key"); !admitted)
        return std::unexpected(admitted.error());
    if (read_file(root / "session.key") !=
        std::string(reinterpret_cast<const char *>(key.data()), key.size()))
        return std::unexpected("LLM session key changed after opening the store");
    const auto path = root / "session.index";
    auto index = JsonValue::make_object();
    index->set("format", JsonValue::make_string("ahfl.llm-session-index"));
    integer(*index, "next_id", 1);
    index->set("entries", JsonValue::make_array());
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        if (auto admitted = private_artifact(path); !admitted)
            return std::unexpected(admitted.error());
        auto envelope = canonical_json(read_file(path));
        if (!envelope || !*envelope || !unique_fields(**envelope))
            return std::unexpected("LLM session index is malformed");
        const auto *p = (**envelope).get("payload"), *t = (**envelope).get("hmac_sha256");
        const auto payload = p ? p->as_string() : std::nullopt,
                   tag = t ? t->as_string() : std::nullopt;
        if (!payload || !tag)
            return std::unexpected("LLM session index is malformed");
        const auto input = hex(run_binding) + std::string(*payload);
        const auto expected = hex(support::hmac_sha256(
            key,
            std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(input.data()),
                                          input.size())));
        if (!support::fixed_work_equal(
                std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(tag->data()),
                                              tag->size()),
                std::span<const std::uint8_t>(
                    reinterpret_cast<const std::uint8_t *>(expected.data()), expected.size())))
            return std::unexpected("LLM session index integrity check failed");
        auto parsed = canonical_json(*payload);
        if (!parsed || !*parsed || !unique_fields(**parsed))
            return std::unexpected("LLM session index is malformed");
        index = std::move(*parsed);
    } else if (ec)
        return std::unexpected("cannot inspect LLM session index");
    else {
        for (const auto &entry : std::filesystem::directory_iterator(root, ec))
            if (numeric_record_path(entry.path()))
                return std::unexpected(
                    "LLM session index is missing; existing sessions cannot be selected safely");
        if (ec)
            return std::unexpected("cannot inspect LLM session directory");
    }
    const auto *schema = index->get("format"), *next = index->get("next_id");
    auto *entries = index->get_mut("entries");
    if (!schema ||
        schema->as_string() != std::optional<std::string_view>{"ahfl.llm-session-index"} ||
        !next || !next->as_uint() || *next->as_uint() == 0 ||
        *next->as_uint() > static_cast<std::uint64_t>(INT64_MAX) || !entries ||
        !entries->is_array() || entries->array_items.size() > 100000)
        return std::unexpected("LLM session index schema or ID allocation is invalid");
    std::vector<std::uint64_t> ids;
    std::vector<std::string> bindings;
    for (const auto &entry : entries->array_items) {
        const auto *id = entry ? entry->get("id") : nullptr,
                   *b = entry ? entry->get("binding") : nullptr,
                   *m = entry ? entry->get("materialized") : nullptr;
        const auto number = id ? id->as_uint() : std::nullopt;
        const auto text = b ? b->as_string() : std::nullopt;
        if (!number || *number == 0 || *number >= *next->as_uint() || !text || text->size() != 64 ||
            !m || !m->as_bool() || std::find(ids.begin(), ids.end(), *number) != ids.end() ||
            std::find(bindings.begin(), bindings.end(), *text) != bindings.end())
            return std::unexpected("LLM session index contains duplicate or invalid bindings");
        ids.push_back(*number);
        bindings.emplace_back(*text);
    }
    return index;
}
} // namespace
std::expected<std::optional<std::uint64_t>, std::string>
LLMSessionStore::resolve_id(std::string_view binding, bool create) const {
    if (binding.size() != 64)
        return std::unexpected("LLM session binding digest is malformed");
    auto loaded = read_index(root_, key_, run_binding_);
    if (!loaded)
        return std::unexpected(loaded.error());
    auto index = std::move(*loaded);
    const auto *next = index->get("next_id");
    auto *entries = index->get_mut("entries");
    const auto path = root_ / "session.index";
    std::error_code ec;
    std::optional<std::uint64_t> found;
    for (const auto &entry : entries->array_items)
        if (entry->get("binding")->as_string() == binding)
            found = *entry->get("id")->as_uint();
    if (found || !create)
        return found;
    auto id = *next->as_uint();
    while (std::filesystem::exists(root_ / (std::to_string(id) + ".json"), ec)) {
        if (id == static_cast<std::uint64_t>(INT64_MAX) - 1)
            return std::unexpected("LLM session ID allocation is exhausted");
        ++id;
    }
    if (ec || id >= static_cast<std::uint64_t>(INT64_MAX))
        return std::unexpected("cannot allocate a numeric LLM session ID");
    auto entry = JsonValue::make_object();
    integer(*entry, "id", id);
    entry->set("binding", JsonValue::make_string(std::string(binding)));
    entry->set("materialized", JsonValue::make_bool(false));
    entries->push(std::move(entry));
    integer(*index, "next_id", id + 1);
    const auto payload = json::serialize_json(*index);
    const auto input = hex(run_binding_) + payload;
    auto envelope = JsonValue::make_object();
    envelope->set("payload", JsonValue::make_string(payload));
    envelope->set("hmac_sha256",
                  JsonValue::make_string(hex(support::hmac_sha256(
                      key_,
                      std::span<const std::uint8_t>(
                          reinterpret_cast<const std::uint8_t *>(input.data()), input.size())))));
    auto saved = private_write(path, json::serialize_json(*envelope));
    if (!saved)
        return std::unexpected(saved.error());
    return std::optional<std::uint64_t>{id};
}
std::expected<void, std::string> LLMSessionStore::save(const LLMSession &s) const {
    if (auto admitted = private_artifact(root_, true); !admitted)
        return admitted;
    auto value = encode(s);
    auto checked = decode(*value);
    if (!checked)
        return std::unexpected(checked.error());
    const auto payload = json::serialize_json(*value);
    if (payload.size() > 64 * 1024 * 1024)
        return std::unexpected("LLM session record exceeds the 64 MiB admission limit");
    const auto input = hex(run_binding_) + payload;
    const auto tag = hex(support::hmac_sha256(
        key_,
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(input.data()),
                                      input.size())));
    auto envelope = JsonValue::make_object();
    envelope->set("payload", JsonValue::make_string(payload));
    envelope->set("hmac_sha256", JsonValue::make_string(tag));
    auto index = read_index(root_, key_, run_binding_);
    if (!index)
        return std::unexpected(index.error());
    bool found = false, changed = false;
    for (auto &entry : (**index).get_mut("entries")->array_items)
        if (entry->get("id")->as_uint() == s.id) {
            if (entry->get("binding")->as_string() != s.binding)
                return std::unexpected("LLM numeric session ID does not match its full binding");
            found = true;
            if (!*entry->get("materialized")->as_bool()) {
                entry->set("materialized", JsonValue::make_bool(true));
                changed = true;
            }
        }
    if (!found)
        return std::unexpected("LLM session has no authenticated numeric allocation");
    auto saved =
        private_write(root_ / (std::to_string(s.id) + ".json"), json::serialize_json(*envelope));
    if (!saved)
        return saved;
    if (!changed)
        return {};
    const auto index_payload = json::serialize_json(**index);
    const auto index_input = hex(run_binding_) + index_payload;
    auto index_envelope = JsonValue::make_object();
    index_envelope->set("payload", JsonValue::make_string(index_payload));
    index_envelope->set(
        "hmac_sha256",
        JsonValue::make_string(hex(support::hmac_sha256(
            key_,
            std::span<const std::uint8_t>(
                reinterpret_cast<const std::uint8_t *>(index_input.data()), index_input.size())))));
    return private_write(root_ / "session.index", json::serialize_json(*index_envelope));
}
std::expected<std::optional<LLMSession>, std::string>
LLMSessionStore::load(std::uint64_t id, std::string_view binding) const {
    const auto path = root_ / (std::to_string(id) + ".json");
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        if (ec)
            return std::unexpected("cannot inspect LLM session artifact");
        return std::nullopt;
    }
    if (std::filesystem::is_symlink(path, ec))
        return std::unexpected("LLM session artifact must not be a symbolic link");
    if (auto admitted = private_artifact(root_, true); !admitted)
        return std::unexpected(admitted.error());
    if (auto admitted = private_artifact(root_ / "session.key"); !admitted)
        return std::unexpected(admitted.error());
    if (read_file(root_ / "session.key") !=
        std::string(reinterpret_cast<const char *>(key_.data()), key_.size()))
        return std::unexpected("LLM session key changed after opening the store");
    if (auto admitted = private_artifact(path); !admitted)
        return std::unexpected(admitted.error());
    const auto envelope = canonical_json(read_file(path));
    if (!envelope || !*envelope || !unique_fields(**envelope))
        return std::unexpected("LLM session integrity check failed");
    const auto *p = (**envelope).get("payload"), *t = (**envelope).get("hmac_sha256");
    auto payload = p ? p->as_string() : std::nullopt, tag = t ? t->as_string() : std::nullopt;
    if (!payload || !tag)
        return std::unexpected("LLM session integrity check failed");
    const auto input = hex(run_binding_) + std::string(*payload);
    const auto expected = hex(support::hmac_sha256(
        key_,
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(input.data()),
                                      input.size())));
    if (!support::fixed_work_equal(
            std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(tag->data()),
                                          tag->size()),
            std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(expected.data()),
                                          expected.size())))
        return std::unexpected(
            "LLM session integrity check failed; no model or tool request was made");
    auto value = canonical_json(*payload);
    if (!value || !*value)
        return std::unexpected("LLM session payload is malformed");
    auto session = decode(**value);
    if (!session)
        return std::unexpected(session.error());
    if (session->id != id || (!binding.empty() && session->binding != binding))
        return std::unexpected(
            "LLM session binding does not match this workflow, input, or provider configuration");
    return std::optional<LLMSession>{std::move(*session)};
}
std::expected<std::vector<LLMSession>, std::string> LLMSessionStore::sessions() const {
    std::vector<LLMSession> result;
    auto index = read_index(root_, key_, run_binding_);
    if (!index)
        return std::unexpected(index.error());
    for (const auto &entry : (**index).get("entries")->array_items) {
        const auto id = *entry->get("id")->as_uint();
        const auto binding = *entry->get("binding")->as_string();
        auto session = load(id, binding);
        if (!session)
            return std::unexpected(session.error());
        if (*session)
            result.push_back(std::move(**session));
        else if (*entry->get("materialized")->as_bool())
            return std::unexpected(
                "LLM session record is missing after its durable Prepared checkpoint");
    }
    return result;
}
std::expected<std::string, std::string>
session_history_chunk(const LLMSession &s, std::uint64_t id, std::uint64_t chunk) {
    if (id >= s.history.size())
        return std::unexpected("history_id is outside this session");
    const auto bytes = session_message_json(s.history[id]);
    std::size_t start = 0, index = 0;
    while (start < bytes.size()) {
        std::size_t end = std::min(start + 4096, bytes.size());
        while (end < bytes.size() && (static_cast<unsigned char>(bytes[end]) & 0xc0) == 0x80)
            --end;
        if (index == chunk) {
            auto v = JsonValue::make_object();
            integer(*v, "history_id", id);
            integer(*v, "chunk_index", chunk);
            integer(*v, "next_chunk", end < bytes.size() ? chunk + 1 : chunk);
            v->set("has_more", JsonValue::make_bool(end < bytes.size()));
            v->set("sha256", JsonValue::make_string(support::sha256_hex(bytes)));
            v->set("content", JsonValue::make_string(bytes.substr(start, end - start)));
            return json::serialize_json(*v);
        }
        start = end;
        ++index;
    }
    return std::unexpected("chunk_index is outside this history record");
}
} // namespace ahfl::llm_provider
