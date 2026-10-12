#include "base/json/json_value.hpp"
#include "base/support/http.hpp"
#include "runtime/providers/llm/context_compactor.hpp"
#include "runtime/providers/llm/llm_capability_provider.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace ahfl;
using namespace ahfl::llm_provider;
int checks = 0, failures = 0;
void check(bool condition, const char *name) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << name << '\n';
    }
}
struct TemporaryStore {
    std::filesystem::path path;
    TemporaryStore() {
        static int ordinal = 0;
        path = std::filesystem::temp_directory_path() /
               ("ahfl_llm_session_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
                std::to_string(++ordinal));
    }
    ~TemporaryStore() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};
ir::Program program() {
    ir::Program p;
    p.format = std::string(ir::kFormat);
    const auto string = []() {
        return ir::TypeRef{
            .kind = ir::TypeRefKind::String, .display_name = "String", .canonical_name = "String"};
    };
    ir::StructDecl result;
    result.name = "Result";
    result.fields.push_back({.name = "answer", .type_ref = string()});
    p.declarations.push_back(std::move(result));
    ir::CapabilityDecl cap;
    cap.name = "Think";
    cap.params.push_back({.name = "input", .type_ref = string()});
    cap.return_type_ref = {
        .kind = ir::TypeRefKind::Struct, .display_name = "Result", .canonical_name = "Result"};
    p.declarations.push_back(std::move(cap));
    return p;
}
LLMProviderConfig config() {
    LLMProviderConfig c;
    c.endpoint = "https://session.example/v1";
    c.model = "test";
    c.api_key = "not-real";
    c.max_retries = 0;
    c.model_context_window_tokens = 10000;
    c.model_max_output_tokens = 1024;
    c.max_tokens = 256;
    return c;
}
runtime::CapabilityInvocationContext context() {
    runtime::CapabilityInvocationContext c;
    c.run_id = runtime::RunId{0};
    c.workflow_id = runtime::WorkflowId{0};
    c.workflow_node_id = runtime::WorkflowNodeId{1};
    c.capability_id = runtime::CapabilityId{2};
    c.has_workflow_node_context = true;
    c.workflow_coordinate = runtime::WorkflowCapabilityCoordinate{
        runtime::WorkflowId{0}, runtime::WorkflowNodeId{1}, runtime::CapabilityId{2}, 0};
    c.module_digest = support::sha256(std::span<const std::uint8_t>{});
    c.workflow_input_digest = c.module_digest;
    return c;
}
std::vector<runtime::Value> args() {
    std::vector<runtime::Value> a;
    a.push_back(runtime::make_string("完整业务输入"));
    return a;
}
std::vector<ToolDefinition> tools() {
    return {{"lookup",
             "Read a durable record",
             "{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}"}};
}
HttpResponse response(std::string content) {
    auto root = json::JsonValue::make_object(), choices = json::JsonValue::make_array(),
         choice = json::JsonValue::make_object(), message = json::JsonValue::make_object();
    message->set("content", json::JsonValue::make_string(std::move(content)));
    choice->set("message", std::move(message));
    choices->push(std::move(choice));
    root->set("choices", std::move(choices));
    auto usage = json::JsonValue::make_object();
    usage->set("prompt_tokens", json::JsonValue::make_int(10));
    usage->set("completion_tokens", json::JsonValue::make_int(2));
    usage->set("total_tokens", json::JsonValue::make_int(12));
    root->set("usage", std::move(usage));
    return {200, json::serialize_json(*root)};
}
HttpResponse call(std::string id, std::string name = "lookup", std::string arguments = "{}") {
    auto root = json::JsonValue::make_object(), choices = json::JsonValue::make_array(),
         choice = json::JsonValue::make_object(), message = json::JsonValue::make_object(),
         calls = json::JsonValue::make_array(), tool = json::JsonValue::make_object(),
         fn = json::JsonValue::make_object();
    fn->set("name", json::JsonValue::make_string(std::move(name)));
    fn->set("arguments", json::JsonValue::make_string(std::move(arguments)));
    tool->set("id", json::JsonValue::make_string(std::move(id)));
    tool->set("type", json::JsonValue::make_string("function"));
    tool->set("function", std::move(fn));
    calls->push(std::move(tool));
    message->set("tool_calls", std::move(calls));
    message->set("content", json::JsonValue::make_null());
    choice->set("message", std::move(message));
    choices->push(std::move(choice));
    root->set("choices", std::move(choices));
    auto usage = json::JsonValue::make_object();
    usage->set("prompt_tokens", json::JsonValue::make_int(10));
    usage->set("completion_tokens", json::JsonValue::make_int(2));
    usage->set("total_tokens", json::JsonValue::make_int(12));
    root->set("usage", std::move(usage));
    return {200, json::serialize_json(*root)};
}
std::shared_ptr<LLMSessionStore> open(const TemporaryStore &temp) {
    auto store = LLMSessionStore::open(temp.path);
    check(store.has_value(), "store.open.private");
    if (!store)
        throw std::runtime_error(store.error());
    return *store;
}
void mutate_authenticated_record(const std::filesystem::path &root,
                                 std::uint64_t id,
                                 const std::function<void(json::JsonValue &)> &mutate) {
    const auto path = root / (std::to_string(id) + ".json");
    std::ifstream input(path, std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(input),
                            std::istreambuf_iterator<char>()};
    auto envelope = json::parse_json(bytes);
    auto payload = json::parse_json(*(**envelope).get("payload")->as_string());
    mutate(**payload);
    const auto encoded = json::serialize_json(**payload);
    support::Sha256Digest key{};
    std::ifstream key_file(root / "session.key", std::ios::binary);
    key_file.read(reinterpret_cast<char *>(key.data()), key.size());
    if (!key_file)
        throw std::runtime_error("cannot read private test fixture key");
    // These fixtures use the default all-zero public run binding. Authenticate
    // invalid schema data to exercise decoding rather than only a bad MAC.
    const auto authenticated = std::string(64, '0') + encoded;
    const auto tag = support::hmac_sha256(
        key,
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(authenticated.data()),
                                      authenticated.size()));
    std::ostringstream hex;
    for (const auto value : tag)
        hex << std::hex << std::setfill('0') << std::setw(2) << static_cast<unsigned>(value);
    (**envelope).set("payload", json::JsonValue::make_string(encoded));
    (**envelope).set("hmac_sha256", json::JsonValue::make_string(hex.str()));
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << json::serialize_json(**envelope);
    if (!output)
        throw std::runtime_error("cannot write authenticated test fixture");
}
void test_handshake_and_cold_resume() {
    TemporaryStore temp;
    auto store = open(temp);
    auto p = program();
    auto cfg = config();
    auto ctx = context();
    int http = 0, executions = 0;
    LLMCapabilityProvider first(p, cfg, [&](const support::HttpRequest &) {
        ++http;
        if (http == 1)
            return call("call-1");
        throw std::runtime_error("simulated process interruption");
    });
    first.set_session_store(store);
    std::uint64_t key = 0;
    first.set_tools(
        tools(),
        [&](const ToolCall &c, const LLMCapabilityProvider::ToolExecutionContext &execution) {
            ++executions;
            key = execution.idempotency_key;
            return ToolCallResult{c.id, "exact completed receipt", true, {}};
        });
    auto a = args();
    auto pending = first.invoke_with_context(ctx, "Think", a);
    check(pending.status == runtime::CapabilityCallStatus::Pending &&
              pending.automatically_resumable,
          "session.prepared.pending");
    check(http == 0 && executions == 0, "session.prepared.no_external_actions");
    try {
        auto result = first.resume_pending(ctx, "Think", a);
        (void)result;
    } catch (const std::runtime_error &) {
    }
    check(executions == 1 && key != 0, "session.tool.stable_numeric_context");
    auto records = store->sessions();
    check(records && records->size() == 1, "session.record.present");
    if (records && !records->empty())
        check((*records)[0].receipts.size() == 1 &&
                  (*records)[0].receipts[0].state == ToolReceiptState::Completed &&
                  (*records)[0].reserved_tokens > 0,
              "session.exact_receipt_and_crash_reservation");
    auto reopened = LLMSessionStore::open(temp.path);
    check(reopened.has_value(), "session.cold_reopen");
    check(reopened && (*reopened)->namespace_digest() == store->namespace_digest(),
          "session.namespace.stable_across_cold_reopen");
    auto cold_records = (*reopened)->sessions();
    check(cold_records && cold_records->size() == 1 &&
              (*cold_records)[0].receipts[0].idempotency_key == key,
          "session.tool.idempotency_key_durable_across_cold_reopen");
    LLMCapabilityProvider resumed(p, cfg, [&](const support::HttpRequest &) {
        ++http;
        return response("{\"answer\":\"done\"}");
    });
    resumed.set_session_store(*reopened);
    resumed.set_tools(tools(),
                      [&](const ToolCall &c, const LLMCapabilityProvider::ToolExecutionContext &) {
                          ++executions;
                          return ToolCallResult{c.id, "duplicate", true, {}};
                      });
    auto result = resumed.resume_pending(ctx, "Think", a);
    check(result.status == runtime::CapabilityCallStatus::Success, "session.cold_resume.success");
    check(executions == 1, "session.completed_tool.never_reexecuted");
    const auto before = http;
    auto completed = resumed.resume_pending(ctx, "Think", a);
    check(completed.status == runtime::CapabilityCallStatus::Success && http == before,
          "session.completed.canonical_reuse_without_http");
    check(completed.usage && completed.usage->total_tokens > 24,
          "session.crash_reservation.not_erased");
    check(result.usage && !result.usage->cost_estimated && completed.usage &&
              !completed.usage->cost_estimated,
          "session.default_zero_price.cost_is_not_estimated");
    auto pure = resumed.replay_completed(ctx, "Think", a);
    check(pure && *pure && (**pure).status == runtime::CapabilityCallStatus::Success &&
              http == before,
          "session.completed_metadata.pure_no_actions");
    auto matching = resumed.can_resume(ctx, "Think", a);
    check(matching && *matching, "session.completed.matches_outer_frontier_guard");
    auto cfg2 = cfg;
    cfg2.temperature = 0.2;
    LLMCapabilityProvider mismatch(p, cfg2, [&](const support::HttpRequest &) {
        ++http;
        return response("{}");
    });
    mismatch.set_session_store(*reopened);
    mismatch.set_tools(tools(), {});
    auto rejected = mismatch.resume_pending(ctx, "Think", a);
    check(rejected.status == runtime::CapabilityCallStatus::Error && http == before,
          "session.binding_mismatch.no_external_actions");
    auto entries = (*reopened)->sessions();
    if (entries && !entries->empty()) {
        auto invalid = (*entries)[0];
        invalid.completed_content = "{\"wrong\":true}";
        check((*reopened)->save(invalid).has_value(), "session.fixture.signed_invalid_output");
        auto bad = resumed.resume_pending(ctx, "Think", a);
        check(bad.status == runtime::CapabilityCallStatus::Error && http == before,
              "session.completed_output.revalidated");
    }
}
void test_inflight_and_integrity() {
    TemporaryStore temp;
    auto store = open(temp);
    auto p = program();
    auto cfg = config();
    auto ctx = context();
    auto a = args();
    int http = 0, execution = 0;
    LLMCapabilityProvider provider(p, cfg, [&](const support::HttpRequest &) {
        ++http;
        return call("commit");
    });
    provider.set_session_store(store);
    provider.set_tools(tools(),
                       [&](const ToolCall &,
                           const LLMCapabilityProvider::ToolExecutionContext &) -> ToolCallResult {
                           ++execution;
                           throw std::runtime_error("crash after external commit");
                       });
    auto prepared = provider.invoke_with_context(ctx, "Think", a);
    (void)prepared;
    try {
        auto result = provider.resume_pending(ctx, "Think", a);
        (void)result;
    } catch (const std::runtime_error &) {
    }
    const auto before = http;
    auto blocked = provider.resume_pending(ctx, "Think", a);
    check(blocked.status == runtime::CapabilityCallStatus::Error &&
              blocked.error_message.find("unknown commit") != std::string::npos && execution == 1 &&
              http == before,
          "session.inflight.unknown_commit_fail_closed");
    auto records = store->sessions();
    if (records && !records->empty()) {
        auto inconsistent = (*records)[0];
        inconsistent.terminal_failure = SessionTerminalFailure{
            runtime::CapabilityCallStatus::Error, std::nullopt, "claimed known failure", {}, true};
        check(!store->save(inconsistent), "session.inflight.terminal_conflict_rejected_on_save");
        mutate_authenticated_record(temp.path, inconsistent.id, [](json::JsonValue &value) {
            auto failure = json::JsonValue::make_object();
            failure->set("status", json::JsonValue::make_int(1));
            failure->set("failure_kind", json::JsonValue::make_null());
            failure->set("error_text", json::JsonValue::make_string("claimed known failure"));
            failure->set("diagnostic_code", json::JsonValue::make_string(""));
            failure->set("usage_present", json::JsonValue::make_bool(true));
            value.set("terminal_failure", std::move(failure));
        });
        check(!LLMSessionStore::open(temp.path), "session.inflight.terminal_conflict_rejected");
        const auto artifact = temp.path / (std::to_string((*records)[0].id) + ".json");
        std::ofstream stream(artifact, std::ios::binary | std::ios::trunc);
        stream << "{\"payload\":\"tampered\"}";
        stream.close();
        auto corrupt = provider.can_resume(ctx, "Think", a);
        check(!corrupt && http == before && execution == 1, "session.corrupt.hmac_no_actions");
    }
    std::filesystem::remove(temp.path / "session.key");
    auto missing = LLMSessionStore::open(temp.path);
    check(!missing, "session.missing_key.no_replacement");
}
void test_compact_and_exact_history(bool persistent, const std::string &summary_mode) {
    TemporaryStore temp;
    auto store = persistent ? open(temp) : std::shared_ptr<LLMSessionStore>{};
    auto p = program();
    auto cfg = config();
    auto ctx = context();
    auto a = args();
    int main_requests = 0, summaries = 0, executions = 0;
    bool exact_fact = false;
    LLMCapabilityProvider provider(p, cfg, [&](const support::HttpRequest &request) {
        auto parsed = json::parse_json(request.body.value_or(std::string{}));
        const auto *messages = parsed && *parsed ? (**parsed).get("messages") : nullptr;
        const auto *first = messages && !messages->array_items.empty()
                                ? messages->array_items[0]->get("content")
                                : nullptr;
        const auto system = first ? first->as_string() : std::nullopt;
        if (system && system->find("Summarize the supplied") != std::string_view::npos) {
            ++summaries;
            if (summary_mode == "malformed")
                return response("not JSON");
            if (summary_mode == "no-progress")
                return response("{\"summary\":\"" + std::string(25000, 'x') + "\"}");
            return response("{\"summary\":\"Preserve task. The first lookup fact is in immutable "
                            "history_id 3, chunk_index 4.\"}");
        }
        ++main_requests;
        if (main_requests <= 2)
            return call("read-" + std::to_string(main_requests));
        if (main_requests == 3) {
            check(request.body.value_or(std::string{}).find("ahfl_session_history_get") !=
                      std::string::npos,
                  "compact.history_read_tool.advertised");
            check(request.body.value_or(std::string{}).find("完整业务输入") != std::string::npos,
                  "compact.typed_input.unmodified");
            return call(
                "recover-fact", "ahfl_session_history_get", "{\"history_id\":3,\"chunk_index\":4}");
        }
        exact_fact =
            request.body.value_or(std::string{}).find("EXACT-OLD-FACT-739") != std::string::npos;
        return response("{\"answer\":\"done\"}");
    });
    if (store)
        provider.set_session_store(store);
    provider.set_tools(
        tools(), [&](const ToolCall &call, const LLMCapabilityProvider::ToolExecutionContext &) {
            ++executions;
            auto data = std::string(20000, 'a');
            data.replace(19000, 18, "EXACT-OLD-FACT-739");
            return ToolCallResult{call.id, data, true, {}};
        });
    auto result = provider.invoke_with_context(ctx, "Think", a);
    if (persistent) {
        check(result.status == runtime::CapabilityCallStatus::Pending,
              "compact.persistent.handshake");
        result = provider.resume_pending(ctx, "Think", a);
    }
    if (summary_mode == "valid") {
        check(result.status == runtime::CapabilityCallStatus::Success, "compact.semantic.success");
        check(summaries == 1 && executions == 2 && exact_fact,
              "compact.summary_and_exact_fact_retrieval");
        check(result.usage && result.usage->total_tokens == 60,
              "compact.summary_is_in_usage_ledger");
        if (store) {
            auto records = store->sessions();
            check(records && records->size() == 1 && (*records)[0].active_begin == 4 &&
                      (*records)[0].history[3].content.size() > 19000,
                  "compact.original_history_immutable");
        }
    } else
        check(result.status == runtime::CapabilityCallStatus::Error && summaries == 1 &&
                  executions == 2,
              "compact.malformed_or_no_progress.stops_without_tool_replay");
}
void test_persisted_metadata_fallback_and_cache() {
    TemporaryStore temp;
    auto store = open(temp);
    auto p = program();
    auto cfg = config();
    auto ctx = context();
    auto a = args();
    cfg.response_cache_enabled = true;
    cfg.fallback_providers.push_back(ProviderEntry{
        .name = "backup", .endpoint = "https://backup.example/v1", .model = "backup-test"});
    int http = 0;
    LLMCapabilityProvider provider(p, cfg, [&](const support::HttpRequest &request) {
        ++http;
        if (request.url.find("session.example") != std::string::npos)
            return HttpResponse{503, "unavailable"};
        return response("{\"answer\":\"done\"}");
    });
    provider.set_session_store(store);
    auto prepared = provider.invoke_with_context(ctx, "Think", a);
    (void)prepared;
    auto active = provider.replay_completed(ctx, "Think", a);
    check(!active && http == 0, "session.metadata_reader.active_rejected_without_actions");
    auto result = provider.resume_pending(ctx, "Think", a);
    check(result.status == runtime::CapabilityCallStatus::Success && result.attempts == 2 &&
              result.provider_degraded && result.degraded_provider_name == "primary" &&
              result.selected_provider_name == "backup",
          "session.metadata.live_fallback");
    auto completed = provider.replay_completed(ctx, "Think", a);
    check(completed && *completed && (**completed).attempts == 2 &&
              (**completed).provider_degraded && (**completed).selected_provider_name == "backup" &&
              http == 2,
          "session.metadata.pure_fallback_replay");
    ctx.workflow_coordinate->ordinal = 1;
    auto next = provider.invoke_with_context(ctx, "Think", a);
    (void)next;
    auto cached = provider.resume_pending(ctx, "Think", a);
    check(cached.status == runtime::CapabilityCallStatus::Success && cached.cache_hit &&
              cached.attempts == 0 && http == 2,
          "session.metadata.live_cache_hit");
    auto replay = provider.replay_completed(ctx, "Think", a);
    check(replay && *replay && (**replay).cache_hit && (**replay).attempts == 0 && http == 2,
          "session.metadata.pure_cache_replay");
    auto rotated_cfg = cfg;
    rotated_cfg.api_key = "rotated-real-credential";
    LLMCapabilityProvider rotated(p, rotated_cfg, [&](const support::HttpRequest &) {
        ++http;
        return response("{}");
    });
    rotated.set_session_store(store);
    auto rotation = rotated.replay_completed(ctx, "Think", a);
    check(rotation && *rotation && (**rotation).cache_hit && http == 2,
          "session.credential_rotation.independent_local_key_and_binding");
}
void test_private_files_and_numeric_index() {
    TemporaryStore temp;
    auto store = open(temp);
    auto one = store->resolve_id(std::string(64, 'a'), true),
         two = store->resolve_id(std::string(64, 'b'), true),
         again = store->resolve_id(std::string(64, 'a'), false);
    check(one && two && again && *one && *two && *again && **one == 1 && **two == 2 && **again == 1,
          "session.ids.numeric_allocation_and_full_binding_lookup");
    std::filesystem::permissions(temp.path / "session.key",
                                 std::filesystem::perms::group_read,
                                 std::filesystem::perm_options::add);
    auto rejected = LLMSessionStore::open(temp.path);
    check(!rejected, "session.key.insecure_permissions_rejected");
    auto symlink = temp.path / "symlink";
    std::filesystem::create_directory_symlink(temp.path, symlink);
    check(!LLMSessionStore::open(symlink), "session.symlink_directory_rejected");
}
void test_independent_namespace_tool_identity() {
    TemporaryStore first_path, second_path;
    auto first = open(first_path), second = open(second_path);
    auto p = program();
    auto cfg = config();
    cfg.prompt_token_cost_per_million = 2;
    cfg.completion_token_cost_per_million = 5;
    auto ctx = context();
    auto a = args();
    const auto execute = [&](const std::shared_ptr<LLMSessionStore> &store) {
        int http = 0;
        std::uint64_t key = 0;
        LLMCapabilityProvider provider(p, cfg, [&](const support::HttpRequest &) {
            return ++http == 1 ? call("same-call") : response("{\"answer\":\"done\"}");
        });
        provider.set_session_store(store);
        provider.set_tools(
            tools(),
            [&](const ToolCall &call, const LLMCapabilityProvider::ToolExecutionContext &context) {
                key = context.idempotency_key;
                return ToolCallResult{call.id, "same result", true, {}};
            });
        auto prepared = provider.invoke_with_context(ctx, "Think", a);
        check(prepared.status == runtime::CapabilityCallStatus::Pending,
              "session.namespace.independent_run_prepared");
        auto completed = provider.resume_pending(ctx, "Think", a);
        check(completed.status == runtime::CapabilityCallStatus::Success && key != 0,
              "session.namespace.independent_run_completed");
        check(completed.usage && completed.usage->cost_estimated &&
                  completed.usage->total_cost_usd > 0,
              "session.priced_success.live_cost_estimated");
        auto restored = provider.replay_completed(ctx, "Think", a);
        check(restored && *restored && (**restored).usage && (**restored).usage->cost_estimated &&
                  (**restored).usage->total_cost_usd == completed.usage->total_cost_usd &&
                  http == 2,
              "session.priced_success.pure_cost_metadata");
        return key;
    };
    auto first_key = execute(first), second_key = execute(second);
    auto first_records = first->sessions(), second_records = second->sessions();
    check(first_records && second_records && first_records->size() == 1 &&
              second_records->size() == 1 &&
              (*first_records)[0].binding == (*second_records)[0].binding,
          "session.namespace.same_public_binding_for_independent_intents");
    check(first->namespace_digest() != second->namespace_digest() && first_key != second_key,
          "session.namespace.independent_intents_have_distinct_tool_keys");
}
bool same_failure(const runtime::CapabilityCallResult &live,
                  const runtime::CapabilityCallResult &cold) {
    if (live.status != cold.status || live.error_message != cold.error_message ||
        live.attempts != cold.attempts || live.failure_kind != cold.failure_kind ||
        live.diagnostic_code != cold.diagnostic_code ||
        live.provider_degraded != cold.provider_degraded ||
        live.degraded_provider_name != cold.degraded_provider_name ||
        live.selected_provider_name != cold.selected_provider_name ||
        live.cache_hit != cold.cache_hit || live.usage.has_value() != cold.usage.has_value())
        return false;
    if (!live.usage)
        return true;
    return live.usage->prompt_tokens == cold.usage->prompt_tokens &&
           live.usage->completion_tokens == cold.usage->completion_tokens &&
           live.usage->total_tokens == cold.usage->total_tokens &&
           live.usage->total_cost_usd == cold.usage->total_cost_usd &&
           live.usage->cost_estimated == cold.usage->cost_estimated &&
           live.usage->notices.size() == cold.usage->notices.size() &&
           std::equal(live.usage->notices.begin(),
                      live.usage->notices.end(),
                      cold.usage->notices.begin(),
                      [](const auto &a, const auto &b) {
                          return a.diagnostic_code == b.diagnostic_code && a.message == b.message;
                      });
}
void test_terminal_failures_cold_resume() {
    for (const std::string mode : {"retry",
                                   "prompt",
                                   "usage",
                                   "after-tool",
                                   "tool",
                                   "parse",
                                   "priced-parse",
                                   "tool-budget"}) {
        TemporaryStore temp;
        auto store = open(temp);
        auto p = program();
        auto cfg = config();
        auto ctx = context();
        auto a = args();
        if (mode == "retry") {
            cfg.max_retries = 1;
            cfg.endpoint = "https://openrouter.ai/api/v1";
            cfg.model = "vendor/failing-model";
            cfg.model_context_window_tokens.reset();
            cfg.model_max_output_tokens.reset();
            cfg.fallback_providers.push_back(ProviderEntry{
                .name = "backup", .endpoint = "https://backup.example/v1", .model = "backup-test"});
        } else if (mode == "prompt")
            cfg.max_prompt_tokens = 32;
        else if (mode == "usage") {
            cfg.max_tokens = 200;
            cfg.max_prompt_tokens = 1000;
            cfg.max_total_tokens = 1200;
        } else if (mode == "tool-budget")
            cfg.max_prompt_tokens = 1000;
        else if (mode == "priced-parse") {
            cfg.prompt_token_cost_per_million = 2;
            cfg.completion_token_cost_per_million = 5;
        }
        int http = 0, model_requests = 0, executions = 0;
        const bool has_tools = mode == "after-tool" || mode == "tool" || mode == "tool-budget";
        LLMCapabilityProvider provider(p, cfg, [&](const support::HttpRequest &request) {
            ++http;
            if (request.method == "GET")
                return HttpResponse{200,
                                    "{\"data\":{\"context_length\":10000,\"top_provider\":{\"max_"
                                    "completion_tokens\":1024}}}"};
            ++model_requests;
            if (has_tools && model_requests == 1)
                return call("failure-call");
            if (mode == "retry" || mode == "after-tool")
                return HttpResponse{503, "unavailable"};
            if (mode == "usage") {
                auto result = response("{\"answer\":\"done\"}");
                auto body = json::parse_json(result.body);
                auto usage = json::JsonValue::make_object();
                usage->set("prompt_tokens", json::JsonValue::make_int(1500));
                usage->set("completion_tokens", json::JsonValue::make_int(500));
                usage->set("total_tokens", json::JsonValue::make_int(2000));
                (**body).set("usage", std::move(usage));
                result.body = json::serialize_json(**body);
                return result;
            }
            return response("{\"wrong\":true}");
        });
        provider.set_session_store(store);
        if (has_tools)
            provider.set_tools(
                tools(),
                [&](const ToolCall &call, const LLMCapabilityProvider::ToolExecutionContext &) {
                    ++executions;
                    return mode == "tool"
                               ? ToolCallResult{call.id, {}, false, "lookup rejected"}
                               : ToolCallResult{call.id,
                                                mode == "tool-budget" ? std::string(10000, 'x')
                                                                      : "completed receipt",
                                                true,
                                                {}};
                });
        auto pending = provider.invoke_with_context(ctx, "Think", a);
        check(pending.status == runtime::CapabilityCallStatus::Pending && http == 0,
              "session.terminal.prepared_before_actions");
        auto live = provider.resume_pending(ctx, "Think", a);
        if (mode == "retry")
            check(live.status == runtime::CapabilityCallStatus::RetryExhausted &&
                      live.attempts == 4 && live.provider_degraded &&
                      live.degraded_provider_name == "primary" && live.usage &&
                      live.usage->total_tokens > 0,
                  "session.terminal.retry_exhausted_metadata");
        else if (mode == "prompt" || mode == "usage" || mode == "tool-budget")
            check(live.status == runtime::CapabilityCallStatus::Error &&
                      live.failure_kind == runtime::CapabilityFailureKind::BudgetRejected &&
                      !live.diagnostic_code.empty() &&
                      (mode == "prompt" ? live.attempts == 0 && !live.usage
                                        : live.attempts > 0 && live.usage.has_value()),
                  "session.terminal.budget_metadata_and_usage_presence");
        else
            check(live.status == runtime::CapabilityCallStatus::Error && live.usage &&
                      !live.error_message.empty(),
                  "session.terminal.parse_or_tool_failure");
        if (live.usage)
            check(live.usage->cost_estimated == (mode == "priced-parse"),
                  "session.terminal.cost_estimated_tracks_actual_candidate_prices");
        const auto actions = http, tool_actions = executions;
        auto records = store->sessions();
        check(records && records->size() == 1 && (*records)[0].terminal_failure &&
                  !(*records)[0].completed,
              "session.terminal.single_failed_state");
        const auto record_path = temp.path / (std::to_string((*records)[0].id) + ".json");
        const auto modified = std::filesystem::last_write_time(record_path);
        auto reopened = LLMSessionStore::open(temp.path);
        check(reopened.has_value(), "session.terminal.cold_authenticated_open");
        LLMCapabilityProvider cold(p, cfg, [&](const support::HttpRequest &) {
            ++http;
            return response("{\"answer\":\"must not request\"}");
        });
        cold.set_session_store(*reopened);
        if (has_tools)
            cold.set_tools(
                tools(),
                [&](const ToolCall &call, const LLMCapabilityProvider::ToolExecutionContext &) {
                    ++executions;
                    return ToolCallResult{call.id, "must not execute", true, {}};
                });
        auto restored = cold.resume_pending(ctx, "Think", a);
        check(same_failure(live, restored), "session.terminal.cold_result_exact_metadata");
        auto reinvoked = cold.invoke_with_context(ctx, "Think", a);
        check(same_failure(live, reinvoked), "session.terminal.reinvoke_exact_metadata");
        check(http == actions && executions == tool_actions &&
                  modified == std::filesystem::last_write_time(record_path),
              "session.terminal.pure_restore_no_actions_accounting_or_write");
    }
}
void test_terminal_failure_schema() {
    for (const std::string mode :
         {"success", "pending", "unknown-status", "unknown-kind", "completed", "missing-usage"}) {
        TemporaryStore temp;
        auto store = open(temp);
        LLMSession record;
        record.binding = std::string(64, 'a');
        auto id = store->resolve_id(record.binding, true);
        record.id = **id;
        record.prepared = false;
        record.history = {{"system", "system"}, {"user", "typed input"}};
        record.terminal_failure = SessionTerminalFailure{
            runtime::CapabilityCallStatus::Error, std::nullopt, "failure", {}, false};
        check(store->save(record).has_value(), "session.terminal.valid_fixture_saved");
        if (mode == "success")
            record.terminal_failure->status = runtime::CapabilityCallStatus::Success;
        else if (mode == "pending")
            record.terminal_failure->status = runtime::CapabilityCallStatus::Pending;
        else if (mode == "unknown-status")
            record.terminal_failure->status = static_cast<runtime::CapabilityCallStatus>(255);
        else if (mode == "unknown-kind")
            record.terminal_failure->failure_kind =
                static_cast<runtime::CapabilityFailureKind>(255);
        else if (mode == "completed") {
            record.completed = true;
            record.completed_content = record.completed_wire = "{\"answer\":\"done\"}";
            record.completed_wire_digest = support::sha256_hex(record.completed_wire);
        } else
            record.attempts = 1;
        check(!store->save(record), "session.terminal.invalid_state_rejected_on_save");
        mutate_authenticated_record(temp.path, record.id, [&](json::JsonValue &value) {
            auto *failure = value.get_mut("terminal_failure");
            if (mode == "unknown-kind")
                failure->set("failure_kind", json::JsonValue::make_int(255));
            else if (mode == "completed") {
                value.set("completed", json::JsonValue::make_bool(true));
                value.set("completed_content",
                          json::JsonValue::make_string(record.completed_content));
                value.set("completed_wire", json::JsonValue::make_string(record.completed_wire));
                value.set("completed_wire_digest",
                          json::JsonValue::make_string(record.completed_wire_digest));
            } else if (mode == "missing-usage")
                value.set("attempts", json::JsonValue::make_int(1));
            else
                failure->set("status",
                             json::JsonValue::make_int(
                                 static_cast<std::int64_t>(record.terminal_failure->status)));
        });
        check(!LLMSessionStore::open(temp.path),
              "session.terminal.invalid_enum_or_conflict_rejected");
    }
}
} // namespace
int main() {
    try {
        test_handshake_and_cold_resume();
        test_inflight_and_integrity();
        test_compact_and_exact_history(false, "valid");
        test_compact_and_exact_history(true, "valid");
        test_compact_and_exact_history(true, "malformed");
        test_compact_and_exact_history(true, "no-progress");
        test_private_files_and_numeric_index();
        test_independent_namespace_tool_identity();
        test_persisted_metadata_fallback_and_cache();
        test_terminal_failures_cold_resume();
        test_terminal_failure_schema();
    } catch (const std::exception &error) {
        ++failures;
        std::cerr << "Unexpected exception: " << error.what() << '\n';
    }
    std::cout << checks - failures << "/" << checks << " LLM session checks passed\n";
    return failures == 0 ? 0 : 1;
}
