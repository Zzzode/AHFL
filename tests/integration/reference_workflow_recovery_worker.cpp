#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/validate.hpp"
#include "ahfl/runtime/execution_otel.hpp"
#include "ahfl/runtime/execution_renderer.hpp"
#include "base/json/json_value.hpp"
#include "base/support/atomic_file.hpp"
#include "common/project_input_support.hpp"
#include "compiler/syntax/frontend/project.hpp"
#include "runtime/engine/workflow_recovery.hpp"
#include "runtime/engine/workflow_runtime.hpp"
#include "runtime/evaluator/value_json.hpp"
#include "runtime/providers/llm/llm_capability_provider.hpp"
#include "runtime/providers/llm/llm_provider_config.hpp"

#include <chrono>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(__APPLE__)
#include <malloc/malloc.h>
#include <sys/resource.h>
#elif defined(__linux__)
#include <malloc.h>
#include <sys/resource.h>
#endif

namespace {

using ahfl::json::JsonValue;

[[nodiscard]] std::optional<std::string> read_text(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return std::nullopt;
    }
    std::ostringstream content;
    content << input.rdbuf();
    return content.str();
}

void render_diagnostics(const ahfl::DiagnosticBag &diagnostics) {
    diagnostics.render(std::cerr);
}

[[nodiscard]] std::optional<ahfl::ir::Program>
compile_reference_workflow(const std::filesystem::path &repo) {
    const auto project = repo / "examples" / "execution-demo";
    const auto entry = project / "src" / "main.ahfl";
    const auto input = ahfl::test_support::project_input_from_manifest(
        project / "ahfl.toml", entry, repo);
    if (input.has_errors()) {
        ahfl::test_support::print_package_graph_diagnostics(input.diagnostics, std::cerr);
        return std::nullopt;
    }

    const ahfl::Frontend frontend;
    auto parsed = ahfl::parse_project(frontend, *input.input);
    if (parsed.has_errors()) {
        render_diagnostics(parsed.diagnostics);
        return std::nullopt;
    }
    const ahfl::Resolver resolver;
    auto resolved = resolver.resolve(parsed.graph);
    if (resolved.has_errors()) {
        render_diagnostics(resolved.diagnostics);
        return std::nullopt;
    }
    const ahfl::TypeChecker checker;
    auto typed = checker.check(parsed.graph, resolved);
    if (typed.has_errors()) {
        render_diagnostics(typed.diagnostics);
        return std::nullopt;
    }
    const ahfl::Validator validator;
    auto validated = validator.validate(parsed.graph, resolved, typed);
    if (validated.has_errors()) {
        render_diagnostics(validated.diagnostics);
        return std::nullopt;
    }
    return ahfl::lower_program_ir(parsed.graph, resolved, typed);
}

[[nodiscard]] bool approval_accepted(const std::filesystem::path &path,
                                     ahfl::runtime::CheckpointId checkpoint) {
    const auto content = read_text(path);
    if (!content.has_value()) {
        return false;
    }
    auto parsed = ahfl::json::parse_json(*content);
    if (!parsed.has_value() || !*parsed || !(*parsed)->is_object()) {
        return false;
    }
    const auto *schema = (*parsed)->get("schema");
    const auto *decision = (*parsed)->get("decision");
    const auto *checkpoint_id = (*parsed)->get("checkpoint_id");
    return schema != nullptr && schema->as_string() == "ahfl.operator-approval.v1" &&
           decision != nullptr && decision->as_string() == "approved" &&
           checkpoint_id != nullptr &&
           checkpoint_id->as_int() ==
               static_cast<std::int64_t>(checkpoint.index());
}

struct ProcessMetrics {
    std::uint64_t peak_rss_bytes{0};
    std::uint64_t allocator_in_use_bytes{0};
    std::uint64_t allocator_reserved_bytes{0};
};

[[nodiscard]] ProcessMetrics process_metrics() {
    ProcessMetrics metrics;
#if defined(__APPLE__) || defined(__linux__)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
#if defined(__APPLE__)
        metrics.peak_rss_bytes = static_cast<std::uint64_t>(usage.ru_maxrss);
#else
        metrics.peak_rss_bytes =
            static_cast<std::uint64_t>(usage.ru_maxrss) * 1024ULL;
#endif
    }
#endif

#if defined(__APPLE__)
    malloc_statistics_t statistics{};
    malloc_zone_statistics(malloc_default_zone(), &statistics);
    metrics.allocator_in_use_bytes =
        static_cast<std::uint64_t>(statistics.size_in_use);
    metrics.allocator_reserved_bytes =
        static_cast<std::uint64_t>(statistics.size_allocated);
#elif defined(__linux__) && defined(__GLIBC__)
    const auto statistics = mallinfo2();
    metrics.allocator_in_use_bytes =
        static_cast<std::uint64_t>(statistics.uordblks);
    metrics.allocator_reserved_bytes =
        static_cast<std::uint64_t>(statistics.arena) +
        static_cast<std::uint64_t>(statistics.hblkhd);
#endif
    if (metrics.allocator_reserved_bytes < metrics.allocator_in_use_bytes) {
        metrics.allocator_reserved_bytes = metrics.allocator_in_use_bytes;
    }
    return metrics;
}

[[nodiscard]] bool write_process_metrics(const std::filesystem::path &path) {
    const auto metrics = process_metrics();
    auto root = JsonValue::make_object();
    root->set("schema",
              JsonValue::make_string("ahfl.reference-worker-process-metrics.v1"));
    root->set("peak_rss_bytes",
              JsonValue::make_int(
                  static_cast<std::int64_t>(metrics.peak_rss_bytes)));
    root->set("allocator_in_use_bytes",
              JsonValue::make_int(
                  static_cast<std::int64_t>(metrics.allocator_in_use_bytes)));
    root->set("allocator_reserved_bytes",
              JsonValue::make_int(
                  static_cast<std::int64_t>(metrics.allocator_reserved_bytes)));
    return ahfl::support::atomic_replace_text(
               path, ahfl::json::serialize_json(*root))
        .has_value();
}

struct SoakControl {
    double minimum_duration_seconds{0.0};
    std::size_t minimum_iterations{0};
};

[[nodiscard]] std::optional<SoakControl>
load_soak_control(const std::filesystem::path &path) {
    const auto content = read_text(path);
    if (!content.has_value()) {
        return std::nullopt;
    }
    auto parsed = ahfl::json::parse_json(*content);
    if (!parsed.has_value() || !*parsed || !(*parsed)->is_object()) {
        return std::nullopt;
    }
    const auto *schema = (*parsed)->get("schema");
    const auto *duration = (*parsed)->get("minimum_duration_seconds");
    const auto *iterations = (*parsed)->get("minimum_iterations");
    if (schema == nullptr ||
        schema->as_string() != "ahfl.reference-soak-control.v1" ||
        duration == nullptr || iterations == nullptr) {
        return std::nullopt;
    }
    const auto duration_value = duration->as_float();
    const auto iteration_value = iterations->as_int();
    if (!duration_value.has_value() || !iteration_value.has_value() ||
        *duration_value <= 0.0 || *iteration_value <= 0) {
        return std::nullopt;
    }
    return SoakControl{
        .minimum_duration_seconds = *duration_value,
        .minimum_iterations = static_cast<std::size_t>(*iteration_value),
    };
}

[[nodiscard]] double mean(const std::vector<std::uint64_t> &values,
                          std::size_t begin,
                          std::size_t end) {
    if (begin >= end || end > values.size()) {
        return 0.0;
    }
    const auto sum = std::accumulate(
        values.begin() + static_cast<std::ptrdiff_t>(begin),
        values.begin() + static_cast<std::ptrdiff_t>(end),
        0.0);
    return sum / static_cast<double>(end - begin);
}

[[nodiscard]] double slope(const std::vector<std::uint64_t> &values) {
    if (values.size() < 2) {
        return 0.0;
    }
    const double mean_x = static_cast<double>(values.size() - 1) / 2.0;
    const double mean_y = mean(values, 0, values.size());
    double numerator = 0.0;
    double denominator = 0.0;
    for (std::size_t index = 0; index < values.size(); ++index) {
        const double offset = static_cast<double>(index) - mean_x;
        numerator += offset * (static_cast<double>(values[index]) - mean_y);
        denominator += offset * offset;
    }
    return denominator == 0.0 ? 0.0 : numerator / denominator;
}

[[nodiscard]] std::unique_ptr<JsonValue>
metric_summary(const std::vector<std::uint64_t> &values) {
    auto object = JsonValue::make_object();
    const auto [minimum, maximum] =
        std::minmax_element(values.begin(), values.end());
    const auto quartile = std::max<std::size_t>(1, values.size() / 4);
    object->set("samples",
                JsonValue::make_int(static_cast<std::int64_t>(values.size())));
    object->set("minimum_bytes",
                JsonValue::make_int(static_cast<std::int64_t>(*minimum)));
    object->set("maximum_bytes",
                JsonValue::make_int(static_cast<std::int64_t>(*maximum)));
    object->set("mean_bytes", JsonValue::make_float(mean(values, 0, values.size())));
    object->set("slope_bytes_per_iteration",
                JsonValue::make_float(slope(values)));
    object->set("first_quartile_mean_bytes",
                JsonValue::make_float(mean(values, 0, quartile)));
    object->set(
        "last_quartile_mean_bytes",
        JsonValue::make_float(mean(values, values.size() - quartile, values.size())));
    return object;
}

struct LatencySummary {
    std::size_t samples{0};
    double minimum{0.0};
    double maximum{0.0};
    double total{0.0};

    void record(double value) {
        if (samples == 0) {
            minimum = value;
            maximum = value;
        } else {
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
        }
        total += value;
        ++samples;
    }
};

[[nodiscard]] std::unique_ptr<JsonValue>
latency_summary(const LatencySummary &values) {
    auto object = JsonValue::make_object();
    object->set("minimum", JsonValue::make_float(values.minimum));
    object->set("maximum", JsonValue::make_float(values.maximum));
    object->set(
        "mean",
        JsonValue::make_float(values.samples == 0
                                  ? 0.0
                                  : values.total /
                                        static_cast<double>(values.samples)));
    return object;
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 6) {
        std::cerr << "usage: reference_workflow_recovery_worker "
                     "<run|crash|resume|soak> <repo-root> <work-dir> <endpoint> "
                     "<approval-or-control-file>\n";
        return 2;
    }
    const std::string mode = argv[1];
    const std::filesystem::path repo = argv[2];
    const std::filesystem::path work = argv[3];
    const std::string endpoint = argv[4];
    const std::filesystem::path approval = argv[5];
    if (mode != "run" && mode != "crash" && mode != "resume" &&
        mode != "soak") {
        return 2;
    }
    std::filesystem::create_directories(work);

    auto program = compile_reference_workflow(repo);
    if (!program.has_value()) {
        return 1;
    }
    const auto input_text =
        read_text(repo / "examples" / "execution-demo" / "inputs" / "high-severity.json");
    if (!input_text.has_value()) {
        return 1;
    }
    auto input = ahfl::evaluator::value_from_json(*input_text);
    if (!input.has_value()) {
        return 1;
    }

    ahfl::runtime::WorkflowRecoveryStore recovery_store(work / "workflow-recovery.json");
    std::optional<ahfl::runtime::WorkflowRecoverySnapshot> recovery;
    if (mode == "resume") {
        auto loaded = recovery_store.load();
        if (!loaded.has_value()) {
            std::cerr << "recovery snapshot unavailable\n";
            return 3;
        }
        if (!approval_accepted(approval, loaded->checkpoint)) {
            std::cerr << "operator approval required for checkpoint "
                      << loaded->checkpoint.index() << '\n';
            return 4;
        }
        recovery = std::move(*loaded);
    }

    ahfl::llm_provider::LLMProviderConfig provider_config;
    provider_config.endpoint = endpoint;
    provider_config.model = "local-reference-provider";
    provider_config.api_key = "local-test-secret";
    provider_config.max_retries = 0;
    provider_config.timeout_seconds = 5;
    provider_config.max_tokens = 1024;
    provider_config.max_prompt_tokens = 4096;
    provider_config.max_total_tokens = 5120;
    provider_config.max_workflow_total_tokens = 128;
    provider_config.max_node_total_tokens = 128;
    provider_config.prompt_token_cost_per_million = 0.5;
    provider_config.completion_token_cost_per_million = 1.0;
    provider_config.max_workflow_total_cost_usd = 0.01;
    provider_config.max_node_total_cost_usd = 0.01;
    provider_config.response_cache_enabled = true;
    provider_config.response_cache_max_entries = 8;
    provider_config.response_cache_ttl_seconds = 300;
    provider_config.response_cache_path = (work / "capability-receipts.json").string();

    if (mode == "soak") {
        const auto control = load_soak_control(approval);
        if (!control.has_value()) {
            std::cerr << "invalid soak control\n";
            return 2;
        }
        provider_config.response_cache_enabled = false;
        provider_config.response_cache_path.clear();
        std::vector<std::uint64_t> peak_rss;
        std::vector<std::uint64_t> allocator_in_use;
        std::vector<std::uint64_t> allocator_reserved;
        LatencySummary latencies;
        std::optional<std::size_t> stable_event_count;
        const auto started = std::chrono::steady_clock::now();
        auto next_metrics_sample = started;
        std::size_t iterations = 0;
        while (iterations < control->minimum_iterations ||
               std::chrono::duration<double>(
                   std::chrono::steady_clock::now() - started)
                       .count() < control->minimum_duration_seconds) {
            const auto iteration_started = std::chrono::steady_clock::now();
            ahfl::llm_provider::LLMCapabilityProvider provider(
                *program, provider_config);
            ahfl::runtime::WorkflowRuntimeConfig soak_config;
            soak_config.contextual_capability_invoker =
                provider.as_contextual_invoker();
            ahfl::runtime::WorkflowRuntime runtime(*program, std::move(soak_config));
            auto iteration_result = runtime.run(
                "execution_demo::main::IncidentWorkflow",
                ahfl::evaluator::clone_value(*input));
            if (iteration_result.status() !=
                    ahfl::runtime::WorkflowStatus::Completed ||
                iteration_result.has_errors() ||
                !ahfl::runtime::validate_execution_events(
                     iteration_result.events.events())
                     .ok()) {
                iteration_result.diagnostics.render(std::cerr);
                return 1;
            }
            const auto count = iteration_result.events.size();
            if (!stable_event_count.has_value()) {
                stable_event_count = count;
            } else if (*stable_event_count != count) {
                std::cerr << "soak event count drift\n";
                return 1;
            }
            const auto now = std::chrono::steady_clock::now();
            latencies.record(
                std::chrono::duration<double>(now - iteration_started).count());
            if (now >= next_metrics_sample || iterations == 0) {
                const auto metrics = process_metrics();
                peak_rss.push_back(metrics.peak_rss_bytes);
                allocator_in_use.push_back(metrics.allocator_in_use_bytes);
                allocator_reserved.push_back(metrics.allocator_reserved_bytes);
                next_metrics_sample = now + std::chrono::seconds{1};
            }
            ++iterations;
        }
        const auto final_metrics = process_metrics();
        peak_rss.push_back(final_metrics.peak_rss_bytes);
        allocator_in_use.push_back(final_metrics.allocator_in_use_bytes);
        allocator_reserved.push_back(final_metrics.allocator_reserved_bytes);
        const auto elapsed = std::chrono::duration<double>(
                                 std::chrono::steady_clock::now() - started)
                                 .count();
        auto report = JsonValue::make_object();
        report->set("schema",
                    JsonValue::make_string("ahfl.reference-worker-soak.v1"));
        report->set("process_model",
                    JsonValue::make_string("single-long-lived-worker"));
        report->set("duration_seconds", JsonValue::make_float(elapsed));
        report->set("iterations",
                    JsonValue::make_int(static_cast<std::int64_t>(iterations)));
        report->set(
            "stable_event_count",
            JsonValue::make_int(static_cast<std::int64_t>(
                stable_event_count.value_or(0))));
        report->set("latency_seconds", latency_summary(latencies));
        report->set("peak_rss", metric_summary(peak_rss));
        report->set("allocator_in_use", metric_summary(allocator_in_use));
        report->set("allocator_reserved", metric_summary(allocator_reserved));
        std::cout << ahfl::json::serialize_json(*report) << '\n';
        return 0;
    }

    ahfl::llm_provider::LLMCapabilityProvider provider(*program, std::move(provider_config));
    ahfl::runtime::WorkflowRuntimeConfig runtime_config;
    runtime_config.contextual_capability_invoker = provider.as_contextual_invoker();
    runtime_config.recovery_store = &recovery_store;
    runtime_config.recovery_snapshot = std::move(recovery);
    if (mode == "crash") {
        runtime_config.checkpoint_after_node =
            [](ahfl::runtime::WorkflowNodeId node)
            -> std::optional<ahfl::runtime::CheckpointId> {
            return node.index() < 2
                       ? std::optional{ahfl::runtime::CheckpointId{node.index()}}
                       : std::nullopt;
        };
        runtime_config.capability_result_observer =
            [&work](const ahfl::runtime::CapabilityInvocationContext &context,
                    const ahfl::runtime::CapabilityCallResult &result) {
            if (context.workflow_node_id != ahfl::runtime::WorkflowNodeId{2} ||
                result.status != ahfl::runtime::CapabilityCallStatus::Success) {
                return;
            }
            auto marker = JsonValue::make_object();
            marker->set("schema", JsonValue::make_string("ahfl.reference-fault-marker.v1"));
            marker->set("workflow_node_id",
                        JsonValue::make_int(
                            static_cast<std::int64_t>(context.workflow_node_id.index())));
            marker->set("cache_hit", JsonValue::make_bool(result.cache_hit));
            marker->set("fault", JsonValue::make_string("process_kill_after_provider_commit"));
            (void)ahfl::support::atomic_replace_text(
                work / "fault-ready.json", ahfl::json::serialize_json(*marker));
            std::cout << "FAULT_READY\n" << std::flush;
            for (;;) {
                std::this_thread::sleep_for(std::chrono::seconds{1});
            }
        };
    }

    ahfl::runtime::WorkflowRuntime runtime(*program, std::move(runtime_config));
    auto result = runtime.run(
        "execution_demo::main::IncidentWorkflow", std::move(*input));
    if (!write_process_metrics(work / "process-metrics.json")) {
        return 1;
    }
    {
        const auto trace = ahfl::runtime::build_execution_otel_trace(
            result.events.events(), std::chrono::system_clock::now());
        if (!trace.has_value()) {
            return 1;
        }
        std::ofstream otel(work / "otel-trace.json", std::ios::binary | std::ios::trunc);
        ahfl::runtime::render_execution_otel_json(*trace, otel);
        if (!otel) {
            return 1;
        }
    }
    {
        std::ofstream events(work / "events.jsonl", std::ios::binary | std::ios::trunc);
        const auto rendered = ahfl::runtime::render_execution_result(
            result,
            ahfl::runtime::ExecutionOutputOptions{
                .format = ahfl::runtime::ExecutionOutputFormat::JsonLines,
            },
            events);
        if (!rendered.has_value()) {
            return 1;
        }
    }
    const auto rendered = ahfl::runtime::render_execution_result(
        result,
        ahfl::runtime::ExecutionOutputOptions{
            .format = ahfl::runtime::ExecutionOutputFormat::Json,
        },
        std::cout);
    return rendered.has_value() &&
                   result.status() == ahfl::runtime::WorkflowStatus::Completed &&
                   !result.has_errors()
               ? 0
               : 1;
}
