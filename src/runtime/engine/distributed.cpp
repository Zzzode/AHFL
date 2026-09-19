#include "runtime/engine/distributed.hpp"

#include "base/json/json_value.hpp"
#include "base/support/atomic_file.hpp"
#include "base/support/sha256.hpp"
#include "runtime/engine/http_transport.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

namespace ahfl::runtime {

namespace {

namespace fs = std::filesystem;

using ahfl::json::JsonValue;

// The one header line and the two `format` values this codec understands.
constexpr std::string_view kSnapshotHeaderV1 = "AHFL_SNAPSHOT_V1";
constexpr std::string_view kSnapshotFormatV2 = "AHFL_SNAPSHOT_V2";

// The closed top-level field set of the V2 object. Anything else is an unknown
// field and is rejected (no forward-compatible tolerance: a snapshot is an
// internal artifact, and a silently ignored field is a silently lost fact).
constexpr std::array<std::string_view, 5> kSnapshotV2Fields = {
    "format", "agent_id", "current_state", "timestamp_ms", "context"};

// Digest prefix length in the content-addressed checkpoint id. 16 lowercase hex
// chars = 64 bits, ample for the per-agent checkpoint cardinality here.
constexpr std::size_t kCheckpointDigestChars = 16;

[[nodiscard]] bool is_lower_hex_digit(char c) noexcept {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

[[nodiscard]] fs::path checkpoint_directory() {
    const char *env = std::getenv("AHFL_CHECKPOINT_DIR");
    if (env != nullptr && *env != '\0')
        return fs::path(env);
    return fs::temp_directory_path() / "ahfl_checkpoints";
}

[[nodiscard]] fs::path snapshot_path(const fs::path &dir, std::string_view checkpoint_id) {
    return dir / (std::string(checkpoint_id) + ".snapshot");
}

[[nodiscard]] std::optional<std::string> read_file_bytes(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return std::nullopt;
    }
    std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (!input.good() && !input.eof()) {
        return std::nullopt;
    }
    return content;
}

// Strip one trailing '\r' so a CRLF-checked-out snapshot still reads, then reject
// anything else left on the line. No other whitespace is trimmed: a byte the
// serializer would not have written is a corrupt line.
[[nodiscard]] std::string_view chomp_cr(std::string_view line) {
    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }
    return line;
}

// ---------------------------------------------------------------------------
// Strict V2 (JSON) codec
// ---------------------------------------------------------------------------

[[nodiscard]] std::expected<StateSnapshot, SnapshotParseError>
parse_snapshot_v2(const JsonValue &root) {
    if (!root.is_object()) {
        return std::unexpected(SnapshotParseError::NotAnObject);
    }
    for (const auto &[key, value] : root.object_fields) {
        (void)value;
        const bool known = std::find(kSnapshotV2Fields.begin(), kSnapshotV2Fields.end(), key) !=
                           kSnapshotV2Fields.end();
        if (!known) {
            return std::unexpected(SnapshotParseError::UnknownField);
        }
    }

    const auto *format = root.get("format");
    if (format == nullptr) {
        return std::unexpected(SnapshotParseError::MissingField);
    }
    const auto format_value = format->as_string();
    if (!format_value.has_value() || *format_value != kSnapshotFormatV2) {
        return std::unexpected(SnapshotParseError::UnknownFormat);
    }

    const auto *agent_id = root.get("agent_id");
    if (agent_id == nullptr) {
        return std::unexpected(SnapshotParseError::MissingField);
    }
    const auto agent_id_value = agent_id->as_string();
    if (!agent_id_value.has_value()) {
        return std::unexpected(SnapshotParseError::WrongFieldType);
    }
    if (!is_valid_agent_id(*agent_id_value)) {
        return std::unexpected(SnapshotParseError::InvalidAgentId);
    }

    const auto *state = root.get("current_state");
    if (state == nullptr) {
        return std::unexpected(SnapshotParseError::MissingField);
    }
    const auto state_value = state->as_string();
    if (!state_value.has_value()) {
        return std::unexpected(SnapshotParseError::WrongFieldType);
    }

    const auto *timestamp = root.get("timestamp_ms");
    if (timestamp == nullptr) {
        return std::unexpected(SnapshotParseError::MissingField);
    }
    const auto timestamp_value = timestamp->as_int();
    if (!timestamp_value.has_value() || *timestamp_value < 0) {
        return std::unexpected(SnapshotParseError::InvalidTimestamp);
    }

    StateSnapshot snapshot;
    snapshot.agent_id = std::string(*agent_id_value);
    snapshot.current_state = std::string(*state_value);
    snapshot.timestamp =
        std::chrono::system_clock::time_point(std::chrono::milliseconds(*timestamp_value));

    const auto *context = root.get("context");
    if (context != nullptr) {
        if (!context->is_object()) {
            return std::unexpected(SnapshotParseError::WrongFieldType);
        }
        // Every member must be a string. The old reader silently dropped a
        // non-string member, which is data loss disguised as a successful load.
        for (const auto &[key, value] : context->object_fields) {
            const auto string_value = value->as_string();
            if (!string_value.has_value()) {
                return std::unexpected(SnapshotParseError::WrongFieldType);
            }
            snapshot.context_values[key] = std::string(*string_value);
        }
    }
    return snapshot;
}

// ---------------------------------------------------------------------------
// Strict V1 (line) codec
// ---------------------------------------------------------------------------

[[nodiscard]] std::expected<StateSnapshot, SnapshotParseError>
parse_snapshot_v1(std::string_view data) {
    std::istringstream input{std::string(data)};
    std::string line;

    if (!std::getline(input, line) || chomp_cr(line) != kSnapshotHeaderV1) {
        return std::unexpected(SnapshotParseError::UnknownFormat);
    }

    StateSnapshot snapshot;
    bool have_state = false;
    bool have_timestamp = false;
    // Reject duplicates rather than letting a later line win: two spellings of one
    // fact is a corrupt snapshot, not a merge.
    std::vector<std::string> seen_keys;

    while (std::getline(input, line)) {
        const std::string_view text = chomp_cr(line);
        if (text.empty()) {
            continue; // a blank line only ever spells the file's trailing newline
        }
        const auto eq_pos = text.find('=');
        if (eq_pos == std::string_view::npos) {
            return std::unexpected(SnapshotParseError::MalformedTextLine);
        }
        const std::string key(text.substr(0, eq_pos));
        const std::string value(text.substr(eq_pos + 1));

        const bool allowed = key == "agent_id" || key == "current_state" || key == "timestamp" ||
                             key.starts_with("ctx.");
        if (!allowed) {
            return std::unexpected(SnapshotParseError::MalformedTextLine);
        }
        if (std::find(seen_keys.begin(), seen_keys.end(), key) != seen_keys.end()) {
            return std::unexpected(SnapshotParseError::MalformedTextLine);
        }
        seen_keys.push_back(key);

        if (key == "agent_id") {
            if (!is_valid_agent_id(value)) {
                return std::unexpected(SnapshotParseError::InvalidAgentId);
            }
            snapshot.agent_id = value;
        } else if (key == "current_state") {
            snapshot.current_state = value;
            have_state = true;
        } else if (key == "timestamp") {
            std::int64_t millis = 0;
            const auto *begin = value.data();
            const auto *end = value.data() + value.size();
            const auto parsed = std::from_chars(begin, end, millis);
            // Full consumption: a trailing byte means the value is not a number
            // (the old `std::stoll` swallowed exactly this).
            if (parsed.ec != std::errc{} || parsed.ptr != end || millis < 0) {
                return std::unexpected(SnapshotParseError::InvalidTimestamp);
            }
            snapshot.timestamp =
                std::chrono::system_clock::time_point(std::chrono::milliseconds(millis));
            have_timestamp = true;
        } else { // ctx.<name>
            const auto name = key.substr(4);
            if (name.empty()) {
                return std::unexpected(SnapshotParseError::MalformedTextLine);
            }
            snapshot.context_values[name] = value;
        }
    }

    if (snapshot.agent_id.empty()) {
        return std::unexpected(SnapshotParseError::MissingField);
    }
    if (!have_state) {
        return std::unexpected(SnapshotParseError::MissingField);
    }
    if (!have_timestamp) {
        return std::unexpected(SnapshotParseError::MissingField);
    }
    return snapshot;
}

// ---------------------------------------------------------------------------
// Remote transport
// ---------------------------------------------------------------------------

struct HttpResult {
    int status_code = 0;
    std::string body;
    bool success = false;
};

HttpResult http_request(const std::string &method,
                        const std::string &url,
                        const std::string &body = "",
                        int timeout_seconds = 10) {
    HttpRequest request;
    request.method = method;
    request.url = url;
    request.timeout_seconds = timeout_seconds;
    if (method == "PUT" || method == "POST") {
        request.headers.emplace("Content-Type", "application/octet-stream");
        request.body = body;
    }

    HttpTransport transport;
    const auto response = transport.execute(request);
    return HttpResult{
        .status_code = response.status_code,
        .body = response.body,
        .success = response.is_success(),
    };
}

// Get best endpoint sorted by priority (highest first). The sort key is
// (priority desc, endpoint asc) so equal priorities produce one deterministic
// order instead of whatever the region list happened to hold.
std::vector<std::string> get_sorted_endpoints(const std::vector<RegionConfig> &regions) {
    std::vector<std::pair<int, std::string>> endpoints;
    for (const auto &region : regions) {
        for (const auto &node : region.nodes) {
            if (!node.endpoint.empty()) {
                endpoints.emplace_back(node.priority, node.endpoint);
            }
        }
    }
    std::sort(endpoints.begin(), endpoints.end(), [](const auto &a, const auto &b) {
        if (a.first != b.first) {
            return a.first > b.first;
        }
        return a.second < b.second;
    });

    std::vector<std::string> result;
    result.reserve(endpoints.size());
    for (auto &[_, ep] : endpoints) {
        result.push_back(std::move(ep));
    }
    return result;
}

// Internal: a well-formed content-addressed id is `<valid agent id>_<16 hex>`
// (heavy char validation happens inside is_checkpoint_id_for).
[[nodiscard]] bool is_wellformed_checkpoint_id(std::string_view checkpoint_id) noexcept {
    const std::size_t underscore = checkpoint_id.rfind('_');
    if (underscore == std::string_view::npos) {
        return false;
    }
    const auto digest = checkpoint_id.substr(underscore + 1);
    if (digest.size() != kCheckpointDigestChars) {
        return false;
    }
    return is_checkpoint_id_for(checkpoint_id, checkpoint_id.substr(0, underscore));
}

} // namespace

std::string_view describe(SnapshotParseError error) noexcept {
    switch (error) {
    case SnapshotParseError::Empty:
        return "snapshot is empty";
    case SnapshotParseError::MalformedJson:
        return "snapshot is object-shaped but not syntactically valid JSON";
    case SnapshotParseError::NotAnObject:
        return "snapshot JSON is not an object";
    case SnapshotParseError::UnknownFormat:
        return "snapshot has no recognized `format` / header";
    case SnapshotParseError::UnknownField:
        return "snapshot carries a field outside the closed schema";
    case SnapshotParseError::MissingField:
        return "snapshot is missing a required field";
    case SnapshotParseError::WrongFieldType:
        return "snapshot field or context member has the wrong JSON type";
    case SnapshotParseError::InvalidAgentId:
        return "snapshot agent_id is empty or unsafe as a path component";
    case SnapshotParseError::InvalidTimestamp:
        return "snapshot timestamp is absent, negative, or not an integer";
    case SnapshotParseError::MalformedTextLine:
        return "snapshot V1 body line is not a known `key=value` pair";
    }
    return "unknown snapshot parse error";
}

std::string_view describe(CheckpointRestoreError error) noexcept {
    switch (error) {
    case CheckpointRestoreError::InvalidId:
        return "checkpoint id is unsafe or not shaped <agent>_<16 hex>";
    case CheckpointRestoreError::NotFound:
        return "no checkpoint with exactly that id";
    case CheckpointRestoreError::ReadFailed:
        return "checkpoint file could not be read";
    case CheckpointRestoreError::MalformedSnapshot:
        return "checkpoint bytes failed the strict snapshot parse";
    }
    return "unknown checkpoint restore error";
}

bool is_safe_path_component(std::string_view name) noexcept {
    if (name.empty() || name.size() > 128) {
        return false;
    }
    if (name == "." || name == "..") {
        return false;
    }
    for (const char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool is_valid_agent_id(std::string_view agent_id) noexcept {
    return is_safe_path_component(agent_id);
}

std::string content_addressed_checkpoint_id(std::string_view agent_id,
                                            std::string_view serialized) {
    const std::string digest = ahfl::support::sha256_hex(serialized);
    return std::string(agent_id) + "_" + digest.substr(0, kCheckpointDigestChars);
}

bool is_checkpoint_id_for(std::string_view checkpoint_id, std::string_view agent_id) noexcept {
    if (!is_valid_agent_id(agent_id)) {
        return false;
    }
    const std::size_t expected = agent_id.size() + 1 + kCheckpointDigestChars;
    if (checkpoint_id.size() != expected || !checkpoint_id.starts_with(agent_id) ||
        checkpoint_id[agent_id.size()] != '_') {
        return false;
    }
    for (std::size_t i = agent_id.size() + 1; i < checkpoint_id.size(); ++i) {
        if (!is_lower_hex_digit(checkpoint_id[i])) {
            return false;
        }
    }
    return true;
}

void DistributedScheduler::add_region(RegionConfig region) {
    regions_.push_back(std::move(region));
}

void DistributedScheduler::set_failover_policy(FailoverPolicy policy) {
    failover_policy_ = policy;
}

StateSnapshot DistributedScheduler::create_snapshot(
    const std::string &agent_id,
    const std::string &state,
    const std::unordered_map<std::string, std::string> &context) const {
    StateSnapshot snapshot;
    snapshot.agent_id = agent_id;
    snapshot.current_state = state;
    snapshot.context_values = context;
    snapshot.timestamp = std::chrono::system_clock::now();
    return snapshot;
}

std::string DistributedScheduler::serialize_snapshot(const StateSnapshot &snapshot) const {
    auto root = JsonValue::make_object();
    root->set("format", JsonValue::make_string(std::string(kSnapshotFormatV2)));
    root->set("agent_id", JsonValue::make_string(snapshot.agent_id));
    root->set("current_state", JsonValue::make_string(snapshot.current_state));
    auto epoch =
        std::chrono::duration_cast<std::chrono::milliseconds>(snapshot.timestamp.time_since_epoch())
            .count();
    root->set("timestamp_ms", JsonValue::make_int(epoch));

    // Sorted by key: the map has no inherent order, so the byte spelling (and
    // therefore the content-addressed id) must not depend on insertion order.
    auto context = JsonValue::make_object();
    std::vector<std::pair<std::string, std::string>> sorted_context(snapshot.context_values.begin(),
                                                                    snapshot.context_values.end());
    std::sort(sorted_context.begin(), sorted_context.end());
    for (const auto &[key, value] : sorted_context) {
        context->set(key, JsonValue::make_string(value));
    }
    root->set("context", std::move(context));
    return ahfl::json::serialize_json(*root);
}

std::expected<StateSnapshot, SnapshotParseError>
DistributedScheduler::deserialize_snapshot(std::string_view data) const {
    if (data.empty()) {
        return std::unexpected(SnapshotParseError::Empty);
    }
    // Shape-first dispatch: a JSON object/array is the V2 path; anything else is
    // the V1 line format, which REQUIRES the header line. There is no third,
    // tolerant path — a body of `key=value` lines with no header is rejected
    // rather than parsed as V1-with-a-missing-header.
    const bool json_shaped = data.starts_with('{') || data.starts_with('[');
    if (!json_shaped) {
        return parse_snapshot_v1(data);
    }

    auto parsed = ahfl::json::parse_json(data);
    if (!parsed.has_value() || !*parsed) {
        return std::unexpected(SnapshotParseError::MalformedJson);
    }
    return parse_snapshot_v2(**parsed);
}

CheckpointResult DistributedScheduler::checkpoint(const StateSnapshot &snapshot) const {
    CheckpointResult result;
    if (!is_valid_agent_id(snapshot.agent_id)) {
        result.status = CheckpointStatus::Failed;
        result.error = "invalid agent id: not usable as a path component";
        return result;
    }
    const std::string serialized = serialize_snapshot(snapshot);
    if (serialized.empty()) {
        result.status = CheckpointStatus::Failed;
        result.error = "failed to serialize snapshot";
        return result;
    }
    // Content-addressed: a pure function of (agent_id, bytes), so replay after a
    // restart re-derives the same id and a second checkpoint in the same
    // millisecond no longer overwrites the first.
    result.checkpoint_id = content_addressed_checkpoint_id(snapshot.agent_id, serialized);

    // 1. Try remote checkpoint via HTTP
    auto endpoints = get_sorted_endpoints(regions_);
    int timeout = static_cast<int>(failover_policy_.timeout.count());
    int retries_left = failover_policy_.max_retries;

    for (const auto &endpoint : endpoints) {
        auto url = endpoint + "/checkpoints/" + result.checkpoint_id;
        auto http_result = http_request("PUT", url, serialized, timeout);
        if (http_result.success) {
            result.status = CheckpointStatus::Stored;
            return result;
        }

        // Failover logic
        if (failover_policy_.strategy == FailoverStrategy::Retry) {
            while (retries_left > 0) {
                --retries_left;
                http_result = http_request("PUT", url, serialized, timeout);
                if (http_result.success) {
                    result.status = CheckpointStatus::Stored;
                    return result;
                }
            }
        } else if (failover_policy_.strategy == FailoverStrategy::Abort) {
            break;
        }
        // FailoverStrategy::Reschedule → try next endpoint (loop continues)
    }

    // 2. Fallback to local filesystem
    auto dir = checkpoint_directory();
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        result.status = CheckpointStatus::Failed;
        result.error = "failed to create checkpoint directory: " + ec.message();
        return result;
    }

    auto filepath = snapshot_path(dir, result.checkpoint_id);

    // Same id ⇒ same digest of the same content, so an existing file with this
    // name must already hold these exact bytes. A differing byte string under one
    // content-addressed id is a real integrity error: fail closed, never
    // overwrite (and never report a checkpoint that is not the one requested).
    if (fs::exists(filepath, ec) && !ec) {
        const auto existing = read_file_bytes(filepath);
        if (!existing.has_value()) {
            result.status = CheckpointStatus::Failed;
            result.error = "existing checkpoint file could not be read";
            return result;
        }
        if (*existing != serialized) {
            result.status = CheckpointStatus::Failed;
            result.error = "checkpoint id collision: existing bytes differ from the new snapshot";
            return result;
        }
        result.status = CheckpointStatus::Created; // idempotent re-write
        return result;
    }

    // Durable atomic replace: fsync the temp file, rename, then fsync the parent
    // directory, so a checkpoint reported Created survives a power loss rather
    // than lingering in the page cache.
    ahfl::support::AtomicReplaceOptions durable_options;
    durable_options.durable = true;
    const auto written = ahfl::support::atomic_replace_text(filepath, serialized, durable_options);
    if (!written.has_value()) {
        result.status = CheckpointStatus::Failed;
        result.error = "failed to durably write checkpoint file";
        return result;
    }

    // Local-only: status remains Created (not Stored to remote)
    result.status = CheckpointStatus::Created;
    return result;
}

std::expected<StateSnapshot, CheckpointRestoreError>
DistributedScheduler::restore(const std::string &checkpoint_id) const {
    // Exact-id only: a partial/prefix id is rejected outright, so "a" can never
    // restore "ab_...". Validate before it ever touches a path or a URL.
    if (!is_wellformed_checkpoint_id(checkpoint_id)) {
        return std::unexpected(CheckpointRestoreError::InvalidId);
    }

    std::optional<SnapshotParseError> remote_parse_error;
    // 1. Try remote restore via HTTP
    auto endpoints = get_sorted_endpoints(regions_);
    int timeout = static_cast<int>(failover_policy_.timeout.count());

    for (const auto &endpoint : endpoints) {
        auto url = endpoint + "/checkpoints/" + checkpoint_id;
        auto http_result = http_request("GET", url, "", timeout);
        if (http_result.success && !http_result.body.empty()) {
            auto snapshot = deserialize_snapshot(http_result.body);
            if (snapshot.has_value()) {
                return *snapshot;
            }
            remote_parse_error = snapshot.error();
        }
    }

    // 2. Fallback to local filesystem (exact name, no directory scan)
    auto dir = checkpoint_directory();
    auto filepath = snapshot_path(dir, checkpoint_id);

    std::error_code ec;
    if (!fs::exists(filepath, ec) || ec) {
        if (remote_parse_error.has_value()) {
            return std::unexpected(CheckpointRestoreError::MalformedSnapshot);
        }
        return std::unexpected(CheckpointRestoreError::NotFound);
    }

    const auto content = read_file_bytes(filepath);
    if (!content.has_value()) {
        return std::unexpected(CheckpointRestoreError::ReadFailed);
    }
    auto snapshot = deserialize_snapshot(*content);
    if (!snapshot.has_value()) {
        return std::unexpected(CheckpointRestoreError::MalformedSnapshot);
    }
    return *snapshot;
}

std::vector<std::string> DistributedScheduler::list_checkpoints(std::string_view agent_id) const {
    std::vector<std::string> ids;
    if (!is_valid_agent_id(agent_id)) {
        return ids;
    }
    const auto dir = checkpoint_directory();
    std::error_code ec;
    if (!fs::exists(dir, ec) || ec) {
        return ids;
    }
    for (const auto &entry : fs::directory_iterator(dir, ec)) {
        if (ec) {
            break;
        }
        const std::string filename = entry.path().filename().string();
        if (!filename.ends_with(".snapshot")) {
            continue;
        }
        const std::string id =
            filename.substr(0, filename.size() - std::string(".snapshot").size());
        if (is_checkpoint_id_for(id, agent_id)) {
            ids.push_back(id);
        }
    }
    // Deterministic order, independent of filesystem iteration order.
    std::sort(ids.begin(), ids.end());
    return ids;
}

size_t DistributedScheduler::region_count() const {
    return regions_.size();
}

size_t DistributedScheduler::total_node_count() const {
    size_t total = 0;
    for (const auto &region : regions_) {
        total += region.nodes.size();
    }
    return total;
}

} // namespace ahfl::runtime
