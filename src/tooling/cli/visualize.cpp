#include "visualize.hpp"
#include "visualize_html.hpp"

#include "base/json/json_value.hpp"

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

            if (!gn.name.empty()) {
                plan.nodes.push_back(std::move(gn));
            }
        }
    }

    return plan;
}

/// Serialize the layout result to JSON for embedding in the HTML.
[[nodiscard]] std::string layout_to_json(const LayoutResult &layout) {
    std::ostringstream out;
    out << "{";
    out << "\"workflow_name\":\"" << layout.workflow_name << "\",";
    out << "\"input_type\":\"" << layout.input_type << "\",";
    out << "\"output_type\":\"" << layout.output_type << "\",";
    out << "\"canvas_width\":" << layout.canvas_width << ",";
    out << "\"canvas_height\":" << layout.canvas_height << ",";

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
                  std::string_view title) {
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

    // Compute layout.
    auto layout = compute_layout(
        std::move(plan->nodes),
        std::move(plan->edges),
        std::move(plan->workflow_name),
        std::move(plan->input_type),
        std::move(plan->output_type)
    );

    // Generate HTML.
    const std::string json = layout_to_json(layout);
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
