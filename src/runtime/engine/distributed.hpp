#pragma once
#include <chrono>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
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

/// Why a snapshot byte string was rejected. The snapshot codecs are fail-closed:
/// a snapshot that is not EXACTLY the documented shape is never partially
/// accepted (no silently dropped context member, no tolerated missing header, no
/// swallowed timestamp). Each enumerator names one rejected shape so a caller can
/// report an actionable reason instead of a bare "restore failed".
enum class SnapshotParseError {
    Empty,             // zero bytes
    MalformedJson,     // object/array-shaped but not syntactically valid JSON
    NotAnObject,       // valid JSON whose top-level value is not an object
    UnknownFormat,     // neither JSON-shaped nor carrying the V1 header line
    UnknownField,      // a field outside the closed schema
    MissingField,      // a required field is absent
    WrongFieldType,    // a field (or a context member) has the wrong JSON kind
    InvalidAgentId,    // agent_id empty or not usable as a path component
    InvalidTimestamp,  // timestamp absent / not a non-negative integer / malformed V1 line
    MalformedTextLine, // V1 body line is not `key=value` with a known key
};

/// Human-readable, actionable name for a parse rejection (diagnostics/tests).
[[nodiscard]] std::string_view describe(SnapshotParseError error) noexcept;

/// Why a local checkpoint could not be restored.
enum class CheckpointRestoreError {
    InvalidId,         // not a safe path component, or not shaped `<agent>_<16 hex>`
    NotFound,          // no exact `<id>.snapshot` in the checkpoint directory
    ReadFailed,        // the file exists but could not be read
    MalformedSnapshot, // the file bytes failed the strict parse
};

[[nodiscard]] std::string_view describe(CheckpointRestoreError error) noexcept;

/// True when `name` is safe to use as a single filesystem path component: ASCII
/// alphanumerics plus '_', '-', '.', 1..=128 bytes, and never "." or "..". This is
/// the only gate between a caller-supplied agent/checkpoint id and a path join, so
/// a traversal attempt ("../../etc/passwd") is rejected rather than resolved.
[[nodiscard]] bool is_safe_path_component(std::string_view name) noexcept;

/// A valid agent id is a safe path component (it is both a filename prefix and a
/// URL path segment for the remote checkpoint endpoints).
[[nodiscard]] bool is_valid_agent_id(std::string_view agent_id) noexcept;

/// The content-addressed checkpoint id for a serialized snapshot:
/// `<agent_id>_<first 16 lowercase hex of sha256(serialized)>`.
///
/// This is a pure function of (agent_id, serialized), so the id is deterministic
/// across processes and identical-content checkpoints are idempotent, while two
/// snapshots of DIFFERENT content can never collide (the old
/// `agent_id + "_" + wall-clock-ms` scheme silently overwrote a second checkpoint
/// written in the same millisecond). 64 bits of digest is ample for the expected
/// per-agent checkpoint cardinality; a same-id/different-bytes store conflict is
/// detected and failed closed rather than overwritten.
[[nodiscard]] std::string content_addressed_checkpoint_id(std::string_view agent_id,
                                                          std::string_view serialized);

/// True when `checkpoint_id` is exactly `<agent_id>_` + 16 lowercase hex. Used by
/// the agent-scoped listing so a prefix like "a" can never be mistaken for "ab_...".
[[nodiscard]] bool is_checkpoint_id_for(std::string_view checkpoint_id,
                                        std::string_view agent_id) noexcept;

class DistributedScheduler {
  public:
    void add_region(RegionConfig region);
    void set_failover_policy(FailoverPolicy policy);

    [[nodiscard]] StateSnapshot
    create_snapshot(const std::string &agent_id,
                    const std::string &state,
                    const std::unordered_map<std::string, std::string> &context) const;

    /// Deterministic wire spelling: keys are emitted in a fixed order and the
    /// context map is emitted sorted by key, so equal snapshots serialize to
    /// byte-identical strings (and therefore to one content-addressed id).
    [[nodiscard]] std::string serialize_snapshot(const StateSnapshot &snapshot) const;

    /// Strict, fail-closed decode of a snapshot byte string. Accepts exactly the
    /// `AHFL_SNAPSHOT_V2` JSON object or the `AHFL_SNAPSHOT_V1` line format; any
    /// deviation (unknown/missing field, wrong field type, non-string context
    /// member, unparseable timestamp, trailing garbage) is rejected wholesale.
    [[nodiscard]] std::expected<StateSnapshot, SnapshotParseError>
    deserialize_snapshot(std::string_view data) const;

    [[nodiscard]] CheckpointResult checkpoint(const StateSnapshot &snapshot) const;

    /// Restore by EXACT checkpoint id. There is no partial/prefix resolution: a
    /// partial id never selects a checkpoint, so "a" can never restore "ab_...".
    /// Use list_checkpoints() to discover ids for an agent, then restore exactly.
    [[nodiscard]] std::expected<StateSnapshot, CheckpointRestoreError>
    restore(const std::string &checkpoint_id) const;

    /// Local checkpoint ids stored for `agent_id`, sorted lexicographically (a
    /// deterministic order independent of filesystem iteration order). Only files
    /// whose name is exactly `<agent_id>_<16 hex>.snapshot` are listed.
    [[nodiscard]] std::vector<std::string> list_checkpoints(std::string_view agent_id) const;

    [[nodiscard]] size_t region_count() const;
    [[nodiscard]] size_t total_node_count() const;

  private:
    std::vector<RegionConfig> regions_;
    FailoverPolicy failover_policy_;
};

} // namespace ahfl::runtime
