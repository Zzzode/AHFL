#include "visualize.hpp"

#include <algorithm>
#include <map>
#include <queue>
#include <unordered_set>

namespace ahfl::visualize {

namespace {

/// Build a name → index map for quick lookup.
[[nodiscard]] std::unordered_map<std::string, std::size_t>
build_name_map(const std::vector<GraphNode> &nodes) {
    std::unordered_map<std::string, std::size_t> map;
    map.reserve(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        map[nodes[i].name] = i;
    }
    return map;
}

/// Kahn's topological sort. Returns node indices in topological order.
/// Nodes not in the edge set (isolated) are appended at the end.
[[nodiscard]] std::vector<std::size_t>
topological_sort(const std::vector<GraphNode> &nodes,
                 const std::vector<GraphEdge> &edges) {
    const auto name_map = build_name_map(nodes);
    const std::size_t n = nodes.size();

    // Build adjacency list and in-degree count.
    std::vector<std::vector<std::size_t>> adj(n);
    std::vector<int> in_degree(n, 0);
    std::vector<bool> in_edge_set(n, false);

    for (const auto &edge : edges) {
        auto from_it = name_map.find(edge.from);
        auto to_it = name_map.find(edge.to);
        if (from_it == name_map.end() || to_it == name_map.end()) {
            continue;
        }
        const std::size_t from = from_it->second;
        const std::size_t to = to_it->second;
        adj[from].push_back(to);
        ++in_degree[to];
        in_edge_set[from] = true;
        in_edge_set[to] = true;
    }

    // Also consider "after" dependencies as edges.
    for (std::size_t i = 0; i < n; ++i) {
        for (const auto &dep : nodes[i].after) {
            auto it = name_map.find(dep);
            if (it == name_map.end()) {
                continue;
            }
            const std::size_t from = it->second;
            // Check if this edge already exists.
            bool exists = false;
            for (const auto &e : edges) {
                if (e.from == dep && e.to == nodes[i].name) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                adj[from].push_back(i);
                ++in_degree[i];
                in_edge_set[from] = true;
                in_edge_set[i] = true;
            }
        }
    }

    // Kahn's algorithm.
    std::queue<std::size_t> queue;
    for (std::size_t i = 0; i < n; ++i) {
        if (in_degree[i] == 0) {
            queue.push(i);
        }
    }

    std::vector<std::size_t> order;
    order.reserve(n);
    while (!queue.empty()) {
        const std::size_t u = queue.front();
        queue.pop();
        order.push_back(u);
        for (const std::size_t v : adj[u]) {
            if (--in_degree[v] == 0) {
                queue.push(v);
            }
        }
    }

    // If there's a cycle (shouldn't happen for a DAG), append remaining nodes.
    if (order.size() < n) {
        std::unordered_set<std::size_t> in_order(order.begin(), order.end());
        for (std::size_t i = 0; i < n; ++i) {
            if (!in_order.contains(i)) {
                order.push_back(i);
            }
        }
    }

    return order;
}

/// Assign layers via longest-path from entry nodes.
/// Entry nodes (no dependencies) get layer 0.
/// Each node's layer = 1 + max(layer of predecessors).
[[nodiscard]] std::vector<int> assign_layers(const std::vector<GraphNode> &nodes,
                                             const std::vector<GraphEdge> &edges,
                                             const std::vector<std::size_t> &topo_order) {
    const auto name_map = build_name_map(nodes);
    const std::size_t n = nodes.size();
    std::vector<int> layer(n, 0);

    // Build predecessor list.
    std::vector<std::vector<std::size_t>> predecessors(n);
    for (const auto &edge : edges) {
        auto from_it = name_map.find(edge.from);
        auto to_it = name_map.find(edge.to);
        if (from_it != name_map.end() && to_it != name_map.end()) {
            predecessors[to_it->second].push_back(from_it->second);
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        for (const auto &dep : nodes[i].after) {
            auto it = name_map.find(dep);
            if (it != name_map.end()) {
                predecessors[i].push_back(it->second);
            }
        }
    }

    // Assign layers in topological order.
    for (const std::size_t idx : topo_order) {
        int max_pred_layer = -1;
        for (const std::size_t pred : predecessors[idx]) {
            max_pred_layer = std::max(max_pred_layer, layer[pred]);
        }
        layer[idx] = max_pred_layer + 1;
    }

    return layer;
}

/// Order nodes within each layer using the barycenter heuristic.
/// For each layer, sort nodes by the average layer-position of their predecessors.
/// This reduces edge crossings.
[[nodiscard]] std::vector<int>
order_within_layers(const std::vector<GraphNode> &nodes,
                    const std::vector<GraphEdge> &edges,
                    const std::vector<int> &layer) {
    const auto name_map = build_name_map(nodes);
    const std::size_t n = nodes.size();

    // Group nodes by layer.
    std::map<int, std::vector<std::size_t>> layers;
    for (std::size_t i = 0; i < n; ++i) {
        layers[layer[i]].push_back(i);
    }

    // Build predecessor list.
    std::vector<std::vector<std::size_t>> predecessors(n);
    for (const auto &edge : edges) {
        auto from_it = name_map.find(edge.from);
        auto to_it = name_map.find(edge.to);
        if (from_it != name_map.end() && to_it != name_map.end()) {
            predecessors[to_it->second].push_back(from_it->second);
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        for (const auto &dep : nodes[i].after) {
            auto it = name_map.find(dep);
            if (it != name_map.end()) {
                predecessors[i].push_back(it->second);
            }
        }
    }

    // Assign initial index within each layer (preserve topological order).
    std::vector<int> index(n, 0);
    for (auto &[layer_id, members] : layers) {
        for (std::size_t j = 0; j < members.size(); ++j) {
            index[members[j]] = static_cast<int>(j);
        }
    }

    // Barycenter passes: for each layer (left to right), sort by average
    // predecessor position. Do 2 passes for better results.
    for (int pass = 0; pass < 2; ++pass) {
        for (auto &[layer_id, members] : layers) {
            if (members.size() <= 1) {
                continue;
            }
            // Compute barycenter for each node.
            std::vector<std::pair<double, std::size_t>> barycenters;
            barycenters.reserve(members.size());
            for (const std::size_t node : members) {
                if (predecessors[node].empty()) {
                    barycenters.emplace_back(static_cast<double>(index[node]), node);
                } else {
                    double sum = 0.0;
                    for (const std::size_t pred : predecessors[node]) {
                        sum += static_cast<double>(index[pred]);
                    }
                    barycenters.emplace_back(sum / static_cast<double>(predecessors[node].size()),
                                             node);
                }
            }
            std::stable_sort(barycenters.begin(), barycenters.end());
            for (std::size_t j = 0; j < barycenters.size(); ++j) {
                index[barycenters[j].second] = static_cast<int>(j);
            }
        }
    }

    return index;
}

} // namespace

LayoutResult compute_layout(std::vector<GraphNode> nodes,
                            std::vector<GraphEdge> edges,
                            std::string workflow_name,
                            std::string input_type,
                            std::string output_type) {
    LayoutResult result;
    result.workflow_name = std::move(workflow_name);
    result.input_type = std::move(input_type);
    result.output_type = std::move(output_type);

    if (nodes.empty()) {
        return result;
    }

    // Step 1: Topological sort.
    const auto topo_order = topological_sort(nodes, edges);

    // Step 2: Assign layers.
    const auto layer = assign_layers(nodes, edges, topo_order);

    // Step 3: Order within layers (barycenter).
    const auto index = order_within_layers(nodes, edges, layer);

    // Step 4: Build positioned nodes.
    result.nodes.reserve(nodes.size());
    int max_layer = 0;
    int max_index = 0;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        PositionedNode pn;
        pn.name = std::move(nodes[i].name);
        pn.target = std::move(nodes[i].target);
        pn.input_type = nodes[i].input_type;
        pn.output_type = nodes[i].output_type;
        pn.capabilities = std::move(nodes[i].capabilities);
        pn.layer = layer[i];
        pn.index = index[i];
        max_layer = std::max(max_layer, pn.layer);
        max_index = std::max(max_index, pn.index);
        result.nodes.push_back(std::move(pn));
    }

    // Sort nodes by layer then index for deterministic output.
    std::sort(result.nodes.begin(), result.nodes.end(),
              [](const PositionedNode &a, const PositionedNode &b) {
                  if (a.layer != b.layer) {
                      return a.layer < b.layer;
                  }
                  return a.index < b.index;
              });

    result.edges = std::move(edges);

    // Compute canvas size.
    result.canvas_width = kMargin * 2 + (max_layer + 1) * kCardWidth +
                          max_layer * kHGap;
    result.canvas_height = kMargin * 2 + (max_index + 1) * kCardHeight +
                           max_index * kVGap;

    return result;
}

} // namespace ahfl::visualize
