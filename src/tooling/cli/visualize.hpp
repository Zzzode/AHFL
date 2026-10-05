#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ahfl::visualize {

/// A node in the visualization graph (extracted from execution-plan JSON).
struct GraphNode {
    std::string name;
    std::string target;       // Agent type (canonical name)
    std::string input_type;   // Workflow-level input type
    std::string output_type;  // Workflow-level output type
    std::vector<std::string> capabilities;  // Capability binding names
    std::vector<std::string> after;         // DAG dependencies
};

/// An edge in the visualization graph.
struct GraphEdge {
    std::string from;
    std::string to;
};

/// A positioned node (output of the layout algorithm).
struct PositionedNode {
    std::string name;
    std::string target;
    std::string input_type;
    std::string output_type;
    std::vector<std::string> capabilities;
    int layer{0};    // x = layer * (card_width + h_gap)
    int index{0};    // y = index * (card_height + v_gap)
};

/// The result of the layout algorithm.
struct LayoutResult {
    std::vector<PositionedNode> nodes;
    std::vector<GraphEdge> edges;
    std::string workflow_name;
    std::string input_type;
    std::string output_type;
    int canvas_width{0};
    int canvas_height{0};
};

// ── Execution overlay (Phase 2) ────────────────────────────────────────────

/// A single capability invocation recorded in the trace.
struct CapabilityCall {
    std::string capability_name;
    std::uint64_t started_at_ns{0};
    std::uint64_t completed_at_ns{0};
    std::uint64_t duration_ns{0};
    int attempts{1};
    bool cache_hit{false};
    std::uint64_t total_tokens{0};
    double total_cost_usd{0.0};
};

/// Per-node execution state, replayed from run-event JSONL.
struct NodeExecutionState {
    std::string status;  // "pending" | "scheduled" | "running" | "completed" | "failed" | "skipped"
    std::uint64_t started_at_ns{0};
    std::uint64_t completed_at_ns{0};
    std::uint64_t duration_ns{0};
    std::vector<CapabilityCall> capability_calls;
};

/// Aggregated execution trace (all nodes + run-level summary).
struct ExecutionTrace {
    std::vector<NodeExecutionState> node_states;  // indexed by node order in plan
    std::string run_status;   // "completed" | "failed" | "suspended" | ...
    std::uint64_t total_duration_ns{0};
    std::uint64_t total_tokens{0};
    double total_cost_usd{0.0};
    std::uint64_t total_capability_calls{0};
    bool has_trace{false};    // true if a trace was loaded
};

/// Layout constants (pixels).
inline constexpr int kCardWidth = 260;
inline constexpr int kCardHeight = 120;
inline constexpr int kHGap = 80;
inline constexpr int kVGap = 40;
inline constexpr int kMargin = 60;

/// Compute a layered DAG layout (simplified Sugiyama).
///
/// 1. Topological sort (Kahn's algorithm).
/// 2. Assign layers via longest-path from entry nodes.
/// 3. Order within layers via barycenter heuristic (reduce edge crossings).
/// 4. Compute pixel positions.
[[nodiscard]] LayoutResult compute_layout(std::vector<GraphNode> nodes,
                                          std::vector<GraphEdge> edges,
                                          std::string workflow_name,
                                          std::string input_type,
                                          std::string output_type);

} // namespace ahfl::visualize

// Standalone visualize command (called from CLI driver).
namespace ahfl::visualize {
/// Run the visualize command. Returns exit code (0 = success).
/// @param input_path   Path to the execution-plan JSON file.
/// @param output_path  Path to the output HTML file ("-" for stdout).
/// @param title        Optional page title.
/// @param trace_path   Optional path to a run-event JSONL trace file.
int run_visualize(std::string_view input_path,
                  std::string_view output_path,
                  std::string_view title,
                  std::string_view trace_path = "");
} // namespace ahfl::visualize
