#include "visualize.hpp"
#include "visualize_html.hpp"

#include "base/json/json_value.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace ahfl::visualize {

namespace {

/// Extract a string field from a JSON object, with a fallback.
[[nodiscard]] std::string json_string(const json::JsonValue *obj,
                                      std::string_view key,
                                      std::string fallback = "") {
    if (obj == nullptr) {
        return fallback;
    }
    const auto *val = obj->get(key);
    if (val == nullptr) {
        return fallback;
    }
    auto s = val->as_string();
    return s ? std::string(*s) : fallback;
}

/// Extract an integer field from a JSON object, with a fallback.
[[nodiscard]] std::int64_t json_int(const json::JsonValue *obj,
                                    std::string_view key,
                                    std::int64_t fallback = 0) {
    if (obj == nullptr) {
        return fallback;
    }
    const auto *val = obj->get(key);
    if (val == nullptr) {
        return fallback;
    }
    auto i = val->as_int();
    return i ? *i : fallback;
}

/// Extract a float field from a JSON object, with a fallback.
[[nodiscard]] double json_float(const json::JsonValue *obj,
                                std::string_view key,
                                double fallback = 0.0) {
    if (obj == nullptr) {
        return fallback;
    }
    const auto *val = obj->get(key);
    if (val == nullptr) {
        return fallback;
    }
    auto f = val->as_float();
    return f ? *f : fallback;
}

/// Extract a boolean field from a JSON object, with a fallback.
[[nodiscard]] bool json_bool(const json::JsonValue *obj,
                             std::string_view key,
                             bool fallback = false) {
    if (obj == nullptr) {
        return fallback;
    }
    const auto *val = obj->get(key);
    if (val == nullptr) {
        return fallback;
    }
    auto b = val->as_bool();
    return b ? *b : fallback;
}

/// Parse an execution-plan JSON document into graph nodes and edges.
/// Returns nullopt on parse error.
struct ParsedPlan {
    std::vector<GraphNode> nodes;
    std::vector<GraphEdge> edges;
    std::string workflow_name;
    std::string input_type;
    std::string output_type;
};

[[nodiscard]] std::optional<ParsedPlan>
parse_execution_plan(std::string_view json_text) {
    auto parsed = json::parse_json(json_text);
    if (!parsed.has_value() || !*parsed) {
        return std::nullopt;
    }

    const auto &root = *parsed;
    const auto *workflows = root->get("workflows");
    if (workflows == nullptr || workflows->kind != json::Kind::Array ||
        workflows->array_items.empty()) {
        return std::nullopt;
    }

    // Use the first workflow.
    const auto &wf = workflows->array_items[0];
    if (!wf) {
        return std::nullopt;
    }

    ParsedPlan plan;
    plan.workflow_name = json_string(wf.get(), "workflow_canonical_name", "workflow");
    plan.input_type = json_string(wf.get(), "input_type");
    plan.output_type = json_string(wf.get(), "output_type");

    // Parse dependency edges.
    const auto *edges_json = wf->get("dependency_edges");
    if (edges_json != nullptr && edges_json->kind == json::Kind::Array) {
        for (const auto &edge : edges_json->array_items) {
            if (!edge) continue;
            GraphEdge e;
            e.from = json_string(edge.get(), "from_node");
            e.to = json_string(edge.get(), "to_node");
            if (!e.from.empty() && !e.to.empty()) {
                plan.edges.push_back(std::move(e));
            }
        }
    }

    // Parse nodes.
    const auto *nodes_json = wf->get("nodes");
    if (nodes_json != nullptr && nodes_json->kind == json::Kind::Array) {
        for (const auto &node : nodes_json->array_items) {
            if (!node) continue;
            GraphNode gn;
            gn.name = json_string(node.get(), "name");
            gn.target = json_string(node.get(), "target");
            gn.input_type = plan.input_type;
            gn.output_type = plan.output_type;

            // Parse "after" dependencies.
            const auto *after = node->get("after");
            if (after != nullptr && after->kind == json::Kind::Array) {
                for (const auto &dep : after->array_items) {
                    if (dep) {
                        auto s = dep->as_string();
                        if (s) {
                            gn.after.push_back(std::string(*s));
                        }
                    }
                }
            }

            // Parse capability bindings.
            const auto *caps = node->get("capability_bindings");
            if (caps != nullptr && caps->kind == json::Kind::Array) {
                for (const auto &cap : caps->array_items) {
                    if (cap) {
                        auto name = json_string(cap.get(), "capability_name");
                        if (!name.empty()) {
                            gn.capabilities.push_back(std::move(name));
                        }
                    }
                }
            }

            // Parse lifecycle (initial/final states).
            const auto *lifecycle = node->get("lifecycle");
            if (lifecycle != nullptr) {
                gn.initial_state = json_string(lifecycle, "target_initial_state");
                const auto *finals = lifecycle->get("target_final_states");
                if (finals != nullptr && finals->kind == json::Kind::Array) {
                    for (const auto &fs : finals->array_items) {
                        if (fs) {
                            auto s = fs->as_string();
                            if (s) {
                                gn.final_states.push_back(std::string(*s));
                            }
                        }
                    }
                }
            }

            if (!gn.name.empty()) {
                plan.nodes.push_back(std::move(gn));
            }
        }
    }

    return plan;
}

/// Parse a run-event JSONL trace and replay events to compute per-node state.
/// @param jsonl_text  The full JSONL file content.
/// @param nodes       Graph nodes from the execution plan (for lifecycle info).
[[nodiscard]] std::optional<ExecutionTrace>
parse_run_events(std::string_view jsonl_text,
                 const std::vector<GraphNode> &nodes) {
    ExecutionTrace trace;
    trace.node_states.resize(nodes.size());
    trace.has_trace = true;

    // Seed lifecycle info from the plan.
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        trace.node_states[i].initial_state = nodes[i].initial_state;
        trace.node_states[i].final_states = nodes[i].final_states;
    }

    // Track in-flight capability invocations: invocation_id → node index.
    struct PendingCap {
        std::size_t node_idx;
        std::uint64_t started_at_ns;
        std::uint64_t total_tokens{0};
        double total_cost_usd{0.0};
    };
    std::unordered_map<std::int64_t, PendingCap> pending_caps;

    std::istringstream stream{std::string(jsonl_text)};
    std::string line;
    while (std::getline(stream, line)) {
        // Skip empty lines.
        if (line.empty()) continue;

        auto parsed = json::parse_json(line);
        if (!parsed.has_value() || !*parsed) {
            continue;  // skip malformed lines
        }

        const auto &root = *parsed;
        const auto type = json_string(root.get(), "type");
        const auto offset_ns = static_cast<std::uint64_t>(
            json_int(root.get(), "monotonic_offset_ns"));
        const auto *payload = root->get("payload");

        if (type == "run_started" || type == "run_completed" ||
            type == "workflow_started" || type == "workflow_completed" ||
            type == "workflow_failed" || type == "workflow_suspended") {
            if (type == "workflow_completed") {
                trace.run_status = "completed";
            } else if (type == "workflow_failed") {
                trace.run_status = "failed";
            } else if (type == "workflow_suspended") {
                trace.run_status = "suspended";
            }
            if (type == "run_completed") {
                trace.total_duration_ns = offset_ns;
            }
            continue;
        }

        if (type == "node_scheduled") {
            const auto node_id = static_cast<std::size_t>(
                json_int(payload, "node_id", -1));
            if (node_id < nodes.size()) {
                auto &st = trace.node_states[node_id];
                if (st.status.empty() || st.status == "pending") {
                    st.status = "scheduled";
                }
            }
            continue;
        }

        if (type == "node_started") {
            const auto node_id = static_cast<std::size_t>(
                json_int(payload, "node_id", -1));
            if (node_id < nodes.size()) {
                auto &st = trace.node_states[node_id];
                st.status = "running";
                st.started_at_ns = offset_ns;
            }
            continue;
        }

        if (type == "agent_state_entered") {
            const auto node_id = static_cast<std::size_t>(
                json_int(payload, "node_id", -1));
            if (node_id < nodes.size()) {
                auto &st = trace.node_states[node_id];
                StateTransition tr;
                tr.state_id = json_int(payload, "state_id");
                tr.entered_at_ns = offset_ns;
                st.state_transitions.push_back(std::move(tr));
            }
            continue;
        }

        if (type == "node_completed") {
            const auto node_id = static_cast<std::size_t>(
                json_int(payload, "node_id", -1));
            if (node_id < nodes.size()) {
                auto &st = trace.node_states[node_id];
                st.status = "completed";
                st.completed_at_ns = offset_ns;
                if (st.started_at_ns > 0) {
                    st.duration_ns = offset_ns - st.started_at_ns;
                }
            }
            continue;
        }

        if (type == "node_failed") {
            const auto node_id = static_cast<std::size_t>(
                json_int(payload, "node_id", -1));
            if (node_id < nodes.size()) {
                auto &st = trace.node_states[node_id];
                st.status = "failed";
                st.completed_at_ns = offset_ns;
                if (st.started_at_ns > 0) {
                    st.duration_ns = offset_ns - st.started_at_ns;
                }
            }
            continue;
        }

        if (type == "node_skipped") {
            const auto node_id = static_cast<std::size_t>(
                json_int(payload, "node_id", -1));
            if (node_id < nodes.size()) {
                trace.node_states[node_id].status = "skipped";
            }
            continue;
        }

        if (type == "capability_started") {
            const auto inv_id = json_int(payload, "invocation_id");
            const auto node_id = static_cast<std::size_t>(
                json_int(payload, "node_id", -1));
            if (node_id < nodes.size()) {
                pending_caps[inv_id] = {node_id, offset_ns};
            }
            continue;
        }

        if (type == "capability_completed") {
            const auto inv_id = json_int(payload, "invocation_id");
            auto it = pending_caps.find(inv_id);
            if (it != pending_caps.end()) {
                const auto node_idx = it->second.node_idx;
                const auto started = it->second.started_at_ns;
                const auto cap_tokens = it->second.total_tokens;
                const auto cap_cost = it->second.total_cost_usd;
                pending_caps.erase(it);

                auto &st = trace.node_states[node_idx];
                CapabilityCall call;
                call.capability_name = node_idx < nodes.size()
                    ? nodes[node_idx].name : "";
                call.started_at_ns = started;
                call.completed_at_ns = offset_ns;
                call.duration_ns = offset_ns - started;
                call.attempts = static_cast<int>(json_int(payload, "attempts", 1));
                call.cache_hit = json_bool(payload, "cache_hit");
                call.total_tokens = cap_tokens;
                call.total_cost_usd = cap_cost;
                st.capability_calls.push_back(std::move(call));
                trace.total_capability_calls++;
            }
            continue;
        }

        if (type == "capability_usage_recorded") {
            const auto inv_id = json_int(payload, "invocation_id");
            auto it = pending_caps.find(inv_id);
            if (it != pending_caps.end()) {
                it->second.total_tokens = static_cast<std::uint64_t>(
                    json_int(payload, "total_tokens"));
                it->second.total_cost_usd = json_float(payload, "total_cost_usd");
            }
            trace.total_tokens += static_cast<std::uint64_t>(
                json_int(payload, "total_tokens"));
            trace.total_cost_usd += json_float(payload, "total_cost_usd");
            continue;
        }
    }

    // Mark any node that never got a terminal event as "pending" if it was
    // never scheduled, or "scheduled" if it was scheduled but never started.
    for (auto &st : trace.node_states) {
        if (st.status.empty()) {
            st.status = "pending";
        }
    }

    return trace;
}

/// Serialize the layout result to JSON for embedding in the HTML.
[[nodiscard]] std::string layout_to_json(const LayoutResult &layout,
                                         const ExecutionTrace *trace = nullptr) {
    std::ostringstream out;
    out << "{";
    out << "\"workflow_name\":\"" << layout.workflow_name << "\",";
    out << "\"input_type\":\"" << layout.input_type << "\",";
    out << "\"output_type\":\"" << layout.output_type << "\",";
    out << "\"canvas_width\":" << layout.canvas_width << ",";
    out << "\"canvas_height\":" << layout.canvas_height << ",";

    // Execution trace summary (if present).
    if (trace != nullptr && trace->has_trace) {
        out << "\"trace\":{";
        out << "\"run_status\":\"" << trace->run_status << "\",";
        out << "\"total_duration_ns\":" << trace->total_duration_ns << ",";
        out << "\"total_tokens\":" << trace->total_tokens << ",";
        out << "\"total_cost_usd\":" << trace->total_cost_usd << ",";
        out << "\"total_capability_calls\":" << trace->total_capability_calls;
        out << "},";
    }

    // Nodes.
    out << "\"nodes\":[";
    for (std::size_t i = 0; i < layout.nodes.size(); ++i) {
        const auto &n = layout.nodes[i];
        if (i > 0) out << ",";
        out << "{";
        out << "\"name\":\"" << n.name << "\",";
        out << "\"target\":\"" << n.target << "\",";
        out << "\"input_type\":\"" << n.input_type << "\",";
        out << "\"output_type\":\"" << n.output_type << "\",";
        out << "\"layer\":" << n.layer << ",";
        out << "\"index\":" << n.index;
        if (!n.capabilities.empty()) {
            out << ",\"capabilities\":[";
            for (std::size_t j = 0; j < n.capabilities.size(); ++j) {
                if (j > 0) out << ",";
                out << "\"" << n.capabilities[j] << "\"";
            }
            out << "]";
        }
        // Execution state for this node.
        if (trace != nullptr && trace->has_trace && i < trace->node_states.size()) {
            const auto &st = trace->node_states[i];
            out << ",\"exec\":{";
            out << "\"status\":\"" << st.status << "\",";
            out << "\"started_at_ns\":" << st.started_at_ns << ",";
            out << "\"completed_at_ns\":" << st.completed_at_ns << ",";
            out << "\"duration_ns\":" << st.duration_ns;
            if (!st.initial_state.empty()) {
                out << ",\"initial_state\":\"" << st.initial_state << "\"";
            }
            if (!st.final_states.empty()) {
                out << ",\"final_states\":[";
                for (std::size_t j = 0; j < st.final_states.size(); ++j) {
                    if (j > 0) out << ",";
                    out << "\"" << st.final_states[j] << "\"";
                }
                out << "]";
            }
            if (!st.state_transitions.empty()) {
                out << ",\"state_transitions\":[";
                for (std::size_t j = 0; j < st.state_transitions.size(); ++j) {
                    if (j > 0) out << ",";
                    const auto &tr = st.state_transitions[j];
                    out << "{\"state_id\":" << tr.state_id
                        << ",\"entered_at_ns\":" << tr.entered_at_ns << "}";
                }
                out << "]";
            }
            if (!st.capability_calls.empty()) {
                out << ",\"capability_calls\":[";
                for (std::size_t j = 0; j < st.capability_calls.size(); ++j) {
                    if (j > 0) out << ",";
                    const auto &call = st.capability_calls[j];
                    out << "{";
                    out << "\"name\":\"" << call.capability_name << "\",";
                    out << "\"started_at_ns\":" << call.started_at_ns << ",";
                    out << "\"completed_at_ns\":" << call.completed_at_ns << ",";
                    out << "\"duration_ns\":" << call.duration_ns << ",";
                    out << "\"attempts\":" << call.attempts << ",";
                    out << "\"cache_hit\":" << (call.cache_hit ? "true" : "false") << ",";
                    out << "\"total_tokens\":" << call.total_tokens << ",";
                    out << "\"total_cost_usd\":" << call.total_cost_usd;
                    out << "}";
                }
                out << "]";
            }
            out << "}";
        }
        out << "}";
    }
    out << "],";

    // Edges.
    out << "\"edges\":[";
    for (std::size_t i = 0; i < layout.edges.size(); ++i) {
        const auto &e = layout.edges[i];
        if (i > 0) out << ",";
        out << "{\"from\":\"" << e.from << "\",\"to\":\"" << e.to << "\"}";
    }
    out << "]";

    out << "}";
    return out.str();
}

} // namespace

/// Run the visualize command. Returns exit code (0 = success).
int run_visualize(std::string_view input_path,
                  std::string_view output_path,
                  std::string_view title,
                  std::string_view trace_path) {
    // Read input file.
    const std::string in_path(input_path);
    std::ifstream input{in_path};
    if (!input) {
        std::cerr << "error: cannot open input file: " << input_path << "\n";
        return 1;
    }
    std::ostringstream buf;
    buf << input.rdbuf();
    const std::string json_text = buf.str();

    // Parse execution plan.
    auto plan = parse_execution_plan(json_text);
    if (!plan.has_value()) {
        std::cerr << "error: failed to parse execution-plan JSON: " << input_path << "\n";
        return 1;
    }

    // Optionally parse run-event trace.
    std::optional<ExecutionTrace> trace;
    if (!trace_path.empty()) {
        const std::string trace_file(trace_path);
        std::ifstream trace_input{trace_file};
        if (!trace_input) {
            std::cerr << "error: cannot open trace file: " << trace_path << "\n";
            return 1;
        }
        std::ostringstream trace_buf;
        trace_buf << trace_input.rdbuf();
        trace = parse_run_events(trace_buf.str(), plan->nodes);
        if (!trace.has_value()) {
            std::cerr << "error: failed to parse run-event JSONL: " << trace_path << "\n";
            return 1;
        }
    }

    // Compute layout.
    auto layout = compute_layout(
        std::move(plan->nodes),
        std::move(plan->edges),
        std::move(plan->workflow_name),
        std::move(plan->input_type),
        std::move(plan->output_type)
    );

    // Generate HTML.
    const std::string json = layout_to_json(layout, trace.has_value() ? &*trace : nullptr);
    const std::string page_title = title.empty()
        ? std::string("AHFL — ") + layout.workflow_name
        : std::string(title);
    const std::string html = generate_canvas_html(json, page_title);

    // Write output.
    if (output_path.empty() || output_path == "-") {
        std::cout << html;
    } else {
        const std::string out_path(output_path);
        std::ofstream out_file{out_path};
        if (!out_file) {
            std::cerr << "error: cannot open output file: " << output_path << "\n";
            return 1;
        }
        out_file << html;
        std::cerr << "Generated: " << output_path << "\n";
    }

    return 0;
}

} // namespace ahfl::visualize
