#pragma once
#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ahfl::runtime {

struct RemoteNodeSpec {
    std::string node_id;
    std::string endpoint; // e.g. "grpc://host:port"
    std::string region;
    int priority = 0;
};

struct StateSnapshot {
    std::string agent_id;
    std::string current_state;
    std::unordered_map<std::string, std::string> context_values;
    std::chrono::system_clock::time_point timestamp;
};

enum class CheckpointStatus {
    Created,
    Stored,
    Restored,
    Failed
};

struct CheckpointResult {
    std::string checkpoint_id;
    CheckpointStatus status = CheckpointStatus::Created;
    std::string error;
};

enum class FailoverStrategy {
    Retry,      // Retry on same node
    Reschedule, // Schedule on different node
    Abort       // Give up
};

struct FailoverPolicy {
    FailoverStrategy strategy = FailoverStrategy::Reschedule;
    int max_retries = 3;
    std::chrono::seconds timeout{30};
};

struct RegionConfig {
    std::string region_id;
    std::vector<RemoteNodeSpec> nodes;
    int max_concurrent = 4;
};

class DistributedScheduler {
  public:
    void add_region(RegionConfig region);
    void set_failover_policy(FailoverPolicy policy);

    [[nodiscard]] StateSnapshot
    create_snapshot(const std::string &agent_id,
                    const std::string &state,
                    const std::unordered_map<std::string, std::string> &context) const;

    [[nodiscard]] std::string serialize_snapshot(const StateSnapshot &snapshot) const;
    [[nodiscard]] std::optional<StateSnapshot> deserialize_snapshot(const std::string &data) const;

    [[nodiscard]] CheckpointResult checkpoint(const StateSnapshot &snapshot) const;
    [[nodiscard]] std::optional<StateSnapshot> restore(const std::string &checkpoint_id) const;

    [[nodiscard]] size_t region_count() const;
    [[nodiscard]] size_t total_node_count() const;

  private:
    std::vector<RegionConfig> regions_;
    FailoverPolicy failover_policy_;
};

} // namespace ahfl::runtime
