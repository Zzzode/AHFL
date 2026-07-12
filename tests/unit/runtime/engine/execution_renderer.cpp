#include "ahfl/runtime/execution_renderer.hpp"
#include "ahfl/runtime/execution_event.hpp"
#include "ahfl/runtime/execution_report.hpp"

#include "base/json/json_value.hpp"
#include "runtime/engine/workflow_runtime.hpp"
#include "runtime/evaluator/value.hpp"

#include <chrono>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace ahfl::runtime;
using namespace ahfl::evaluator;
using namespace std::chrono_literals;

int test_count = 0;
int pass_count = 0;

void check(bool condition, const char *name) {
    ++test_count;
    if (condition) {
        ++pass_count;
    } else {
        std::fprintf(stderr, "FAIL: %s\n", name);
    }
}

template <typename Payload>
void append_event(WorkflowResult &result,
                  Payload payload,
                  std::chrono::nanoseconds monotonic_offset) {
    (void)result.events.append(std::move(payload), monotonic_offset);
}

WorkflowResult make_result() {
    WorkflowResult result;
    const auto workflow = result.metadata.add_workflow("demo::IncidentWorkflow");
    const auto agent = result.metadata.add_agent("demo::ResponderAgent");
    const auto node = result.metadata.add_node("respond", workflow, agent);
    result.values.push_back(make_string("incident resolved"));
    const RuntimeValueId output{0};

    append_event(result, RunStarted{.run = RunId{0}}, 0ns);
    append_event(result, WorkflowStarted{.run = RunId{0}, .workflow = workflow}, 1ns);
    append_event(result,
                 NodeScheduled{
                     .workflow = workflow,
                     .node = node,
                     .dependencies = {},
                     .execution_slot = 0,
                 },
                 2ns);
    append_event(result, NodeStarted{.node = node, .agent = agent}, 3ns);
    const auto capability = result.metadata.add_capability("demo::ResolveIncident");
    const auto provider = result.metadata.add_provider("primary");
    const auto invocation = result.metadata.add_invocation(node, capability);
    append_event(result,
                 CapabilityStarted{
                     .invocation = invocation,
                     .node = node,
                     .capability = capability,
                     .provider = provider,
                     .attempt = 1,
                 },
                 4ns);
    append_event(result,
                 CapabilityUsageRecorded{
                     .invocation = invocation,
                     .prompt_tokens = 16,
                     .completion_tokens = 4,
                     .total_tokens = 20,
                     .total_cost_usd = 0.000012,
                     .cost_estimated = true,
                 },
                 5ns);
    append_event(result,
                 CapabilityCompleted{
                     .invocation = invocation,
                     .output = output,
                     .attempts = 1,
                     .cache_hit = false,
                 },
                 6ns);
    append_event(result, NodeCompleted{.node = node, .output = output}, 7ns);
    append_event(result, WorkflowCompleted{.workflow = workflow, .output = output}, 8ns);
    append_event(
        result, RunCompleted{.run = RunId{0}, .status = RunTerminalStatus::Completed}, 9ns);
    auto report = build_execution_report(result.events.events());
    if (report.has_value()) {
        result.report = std::move(*report);
    }
    return result;
}

WorkflowResult make_restored_result() {
    WorkflowResult result;
    const auto workflow = result.metadata.add_workflow("demo::IncidentWorkflow");
    const auto agent = result.metadata.add_agent("demo::IntakeAgent");
    const auto node = result.metadata.add_node("intake", workflow, agent);
    result.values.push_back(make_string("restored"));
    const RuntimeValueId output{0};
    const CheckpointId checkpoint{4};

    append_event(result, RunStarted{.run = RunId{0}}, 0ns);
    append_event(result, RunResumed{.run = RunId{0}, .checkpoint = checkpoint}, 1ns);
    append_event(result, WorkflowStarted{.run = RunId{0}, .workflow = workflow}, 2ns);
    append_event(result,
                 NodeScheduled{
                     .workflow = workflow,
                     .node = node,
                     .dependencies = {},
                     .execution_slot = 0,
                 },
                 3ns);
    append_event(result,
                 NodeRestored{
                     .node = node,
                     .agent = agent,
                     .output = output,
                     .checkpoint = checkpoint,
                 },
                 4ns);
    append_event(result, WorkflowCompleted{.workflow = workflow, .output = output}, 5ns);
    append_event(
        result, RunCompleted{.run = RunId{0}, .status = RunTerminalStatus::Completed}, 6ns);
    auto report = build_execution_report(result.events.events());
    if (report.has_value()) {
        result.report = std::move(*report);
    }
    return result;
}

WorkflowResult make_failed_result() {
    WorkflowResult result;
    const auto workflow = result.metadata.add_workflow("demo::IncidentWorkflow");
    const auto agent = result.metadata.add_agent("demo::ResponderAgent");
    const auto node = result.metadata.add_node("respond", workflow, agent);
    const auto capability = result.metadata.add_capability("demo::Notify");
    const auto provider = result.metadata.add_provider("primary");
    const auto invocation = result.metadata.add_invocation(node, capability);
    result.diagnostics.error()
        .code("runtime.PROVIDER_UNAVAILABLE")
        .message("provider returned status=401")
        .range(ahfl::SourceRange{.begin_offset = 12, .end_offset = 24})
        .source_name(
            "demo.ahfl", ahfl::SourcePosition{.offset = 12, .line = 2, .column = 5})
        .with_note("configure a valid secret handle",
                   ahfl::SourceRange{.begin_offset = 30, .end_offset = 42})
        .emit();
    const DiagnosticId diagnostic{0};

    append_event(result, RunStarted{.run = RunId{0}}, 0ns);
    append_event(result, WorkflowStarted{.run = RunId{0}, .workflow = workflow}, 1ns);
    append_event(result,
                 NodeScheduled{
                     .workflow = workflow,
                     .node = node,
                     .dependencies = {},
                     .execution_slot = 0,
                 },
                 2ns);
    append_event(result, NodeStarted{.node = node, .agent = agent}, 3ns);
    append_event(result,
                 CapabilityStarted{
                     .invocation = invocation,
                     .node = node,
                     .capability = capability,
                     .provider = provider,
                     .attempt = 1,
                 },
                 4ns);
    append_event(result,
                 CapabilityFailed{
                     .invocation = invocation,
                     .kind = CapabilityFailureKind::Error,
                     .diagnostic = diagnostic,
                     .attempts = 1,
                 },
                 5ns);
    append_event(result,
                 NodeFailed{
                     .node = node,
                     .diagnostic = diagnostic,
                     .kind = NodeFailureKind::CapabilityFailed,
                 },
                 6ns);
    append_event(result,
                 WorkflowFailed{
                     .workflow = workflow,
                     .diagnostic = diagnostic,
                     .kind = WorkflowFailureKind::NodeFailed,
                 },
                 7ns);
    append_event(result,
                 RunCompleted{.run = RunId{0}, .status = RunTerminalStatus::Failed},
                 8ns);
    auto report = build_execution_report(result.events.events());
    if (report.has_value()) {
        result.report = std::move(*report);
    }
    return result;
}

void test_human_renderer_uses_metadata_boundary() {
    const auto result = make_result();
    std::ostringstream out;
    const auto rendered = render_execution_result(
        result, ExecutionOutputOptions{.format = ExecutionOutputFormat::Human}, out);

    check(rendered.has_value(), "human render succeeds");
    const auto text = out.str();
    check(text.find("IncidentWorkflow") != std::string::npos, "human shows workflow");
    check(text.find("respond") != std::string::npos, "human shows node display name");
    check(text.find("ResponderAgent") != std::string::npos, "human shows agent display name");
    check(text.find("incident resolved") != std::string::npos, "human shows final value");
    check(text.find("LLM Config") == std::string::npos, "human does not leak config path");
    check(text.find("=== AHFL Workflow Execution ===") == std::string::npos,
          "human does not use old ad hoc title");
    const auto first = text.find("incident resolved");
    check(first == text.rfind("incident resolved"), "final value is not duplicated");
}

void test_json_renderer_emits_versioned_report() {
    const auto result = make_result();
    std::ostringstream out;
    const auto rendered = render_execution_result(
        result, ExecutionOutputOptions{.format = ExecutionOutputFormat::Json}, out);

    check(rendered.has_value(), "json render succeeds");
    const auto parsed = ahfl::json::parse_json(out.str());
    check(parsed.has_value() && *parsed && (*parsed)->is_object(), "json output parses");
    if (!parsed.has_value() || !*parsed) {
        return;
    }
    check((*parsed)->get("schema")->as_string() == "ahfl.run-report", "json schema name");
    check((*parsed)->get("schema_version")->as_int() == 1, "json schema version");
    const auto *run = (*parsed)->get("run");
    check(run != nullptr && run->is_object(), "json has run object");
    check(run != nullptr && run->get("id")->as_int() == 0, "json uses numeric run id");
    const auto *nodes = (*parsed)->get("nodes");
    check(nodes != nullptr && nodes->is_array() && nodes->array_items.size() == 1,
          "json has one node report");
    check((*parsed)->get("replay") != nullptr && (*parsed)->get("replay")->is_object(),
          "json has event-native replay projection");
    check((*parsed)->get("audit") != nullptr && (*parsed)->get("audit")->is_object(),
          "json has event-native audit projection");
    const auto *usage = (*parsed)->get("usage");
    check(usage != nullptr && usage->is_object(), "json has usage summary");
    check(usage != nullptr && usage->get("total_tokens")->as_int() == 20,
          "json usage comes from canonical event");
    check(out.str().find("incident resolved") != std::string::npos, "json has final value");
}

void test_jsonl_renderer_emits_terminal_event_last() {
    const auto result = make_result();
    std::ostringstream out;
    const auto rendered = render_execution_result(
        result, ExecutionOutputOptions{.format = ExecutionOutputFormat::JsonLines}, out);

    check(rendered.has_value(), "jsonl render succeeds");
    std::istringstream input(out.str());
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(input, line)) {
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    check(lines.size() == result.events.size(), "jsonl emits one line per event");
    for (const auto &event_line : lines) {
        const auto parsed = ahfl::json::parse_json(event_line);
        check(parsed.has_value() && *parsed && (*parsed)->is_object(), "jsonl line parses");
        if (parsed.has_value() && *parsed) {
            check((*parsed)->get("schema")->as_string() == "ahfl.run-event",
                  "jsonl event schema");
        }
    }
    check(lines.back().find("\"type\":\"run_completed\"") != std::string::npos,
          "jsonl terminal event is last");
    check(out.str().find("\"type\":\"capability_usage_recorded\"") != std::string::npos,
          "jsonl exposes canonical usage event");
    check(out.str().find("\"total_tokens\":20") != std::string::npos,
          "jsonl usage event exposes total tokens");
}

void test_jsonl_renderer_materializes_referenced_diagnostics() {
    const auto result = make_failed_result();
    std::ostringstream out;
    const auto rendered = render_execution_result(
        result, ExecutionOutputOptions{.format = ExecutionOutputFormat::JsonLines}, out);

    check(rendered.has_value(), "failure jsonl render succeeds");
    const auto text = out.str();
    check(text.find("\"type\":\"capability_failed\"") != std::string::npos,
          "failure jsonl has capability terminal");
    check(text.find("\"diagnostic\":{\"severity\":\"error\"") != std::string::npos,
          "failure jsonl materializes diagnostic");
    check(text.find("\"diagnostic_id\":0") != std::string::npos,
          "failure jsonl retains diagnostic id");
    check(text.find("\"code\":\"runtime.PROVIDER_UNAVAILABLE\"") != std::string::npos,
          "failure jsonl exposes diagnostic code");
    check(text.find("\"message\":\"provider returned status=401\"") != std::string::npos,
          "failure jsonl exposes diagnostic message");
    check(text.find("\"range\":{\"begin\":12,\"end\":24}") !=
              std::string::npos,
          "failure jsonl exposes source range");
    check(text.find("\"position\":{\"offset\":12,\"line\":2,\"column\":5}") !=
              std::string::npos,
          "failure jsonl exposes source position");
    check(text.find("\"related\":[{\"message\":\"configure a valid secret handle\"") !=
              std::string::npos,
          "failure jsonl exposes related notes");
}

void test_json_renderer_exposes_recovery_facts() {
    const auto result = make_restored_result();
    std::ostringstream out;
    const auto rendered = render_execution_result(
        result, ExecutionOutputOptions{.format = ExecutionOutputFormat::Json}, out);
    check(rendered.has_value(), "recovery json render succeeds");
    check(out.str().find("\"restored_from_checkpoint_id\":4") != std::string::npos,
          "report exposes restored checkpoint");
    check(out.str().find("\"restored\":true") != std::string::npos,
          "replay exposes restored node");
    check(out.str().find("\"node_restored\":1") != std::string::npos,
          "audit counts restored node");
}

void test_quiet_renderer_only_emits_value_json() {
    const auto result = make_result();
    std::ostringstream out;
    const auto rendered = render_execution_result(
        result, ExecutionOutputOptions{.format = ExecutionOutputFormat::Quiet}, out);

    check(rendered.has_value(), "quiet render succeeds");
    check(out.str().find("incident resolved") != std::string::npos, "quiet emits value");
    check(out.str().find("ahfl.run-report") == std::string::npos,
          "quiet omits report envelope");
    check(out.str().find("IncidentWorkflow") == std::string::npos,
          "quiet omits workflow metadata");
}

} // namespace

int main() {
    test_human_renderer_uses_metadata_boundary();
    test_json_renderer_emits_versioned_report();
    test_jsonl_renderer_emits_terminal_event_last();
    test_jsonl_renderer_materializes_referenced_diagnostics();
    test_json_renderer_exposes_recovery_facts();
    test_quiet_renderer_only_emits_value_json();

    std::printf("%d/%d tests passed\n", pass_count, test_count);
    return pass_count == test_count ? 0 : 1;
}
