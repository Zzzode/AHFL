#include "ahfl/runtime/distributed.hpp"
#include <sstream>

namespace ahfl::runtime {

void DistributedScheduler::add_region(RegionConfig region) {
    regions_.push_back(std::move(region));
}

void DistributedScheduler::set_failover_policy(FailoverPolicy policy) {
    failover_policy_ = policy;
}

StateSnapshot DistributedScheduler::create_snapshot(
    const std::string& agent_id,
    const std::string& state,
    const std::unordered_map<std::string, std::string>& context) const {
    StateSnapshot snapshot;
    snapshot.agent_id = agent_id;
    snapshot.current_state = state;
    snapshot.context_values = context;
    snapshot.timestamp = std::chrono::system_clock::now();
    return snapshot;
}

std::string DistributedScheduler::serialize_snapshot(const StateSnapshot& snapshot) const {
    // Simple key=value serialization
    std::ostringstream oss;
    oss << "agent_id=" << snapshot.agent_id << "\n";
    oss << "state=" << snapshot.current_state << "\n";
    for (const auto& [k, v] : snapshot.context_values) {
        oss << "ctx." << k << "=" << v << "\n";
    }
    return oss.str();
}

std::optional<StateSnapshot> DistributedScheduler::deserialize_snapshot(const std::string& data) const {
    if (data.empty()) {
        return std::nullopt;
    }

    StateSnapshot snapshot;
    std::istringstream iss(data);
    std::string line;

    while (std::getline(iss, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        auto key = line.substr(0, eq);
        auto value = line.substr(eq + 1);

        if (key == "agent_id") {
            snapshot.agent_id = value;
        } else if (key == "state") {
            snapshot.current_state = value;
        } else if (key.starts_with("ctx.")) {
            snapshot.context_values[key.substr(4)] = value;
        }
    }

    snapshot.timestamp = std::chrono::system_clock::now();
    return snapshot;
}

CheckpointResult DistributedScheduler::checkpoint(const StateSnapshot& snapshot) const {
    // Stub: in production this would persist to durable store
    CheckpointResult result;
    result.checkpoint_id = snapshot.agent_id + "_checkpoint";
    result.status = CheckpointStatus::Created;
    return result;
}

std::optional<StateSnapshot> DistributedScheduler::restore(const std::string& checkpoint_id) const {
    // Stub: in production this would restore from durable store
    (void)checkpoint_id;
    return std::nullopt;
}

size_t DistributedScheduler::region_count() const {
    return regions_.size();
}

size_t DistributedScheduler::total_node_count() const {
    size_t count = 0;
    for (const auto& r : regions_) {
        count += r.nodes.size();
    }
    return count;
}

} // namespace ahfl::runtime
