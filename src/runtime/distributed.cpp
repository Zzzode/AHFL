#include "runtime/distributed.hpp"

#include "json/json_value.hpp"
#include "support/curl.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace ahfl::runtime {

namespace {

namespace fs = std::filesystem;

std::string generate_checkpoint_id(const std::string &agent_id) {
    auto now = std::chrono::system_clock::now();
    auto epoch =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return agent_id + "_" + std::to_string(epoch);
}

fs::path checkpoint_directory() {
    const char *env = std::getenv("AHFL_CHECKPOINT_DIR");
    if (env != nullptr && *env != '\0')
        return fs::path(env);
    return fs::temp_directory_path() / "ahfl_checkpoints";
}

struct HttpResult {
    int status_code = 0;
    std::string body;
    bool success = false;
};

HttpResult http_request(const std::string &method,
                        const std::string &url,
                        const std::string &body = "",
                        int timeout_seconds = 10) {
    ahfl::support::CurlRequest request;
    request.method = method;
    request.url = url;
    request.timeout_seconds = timeout_seconds;
    if (method == "PUT" || method == "POST") {
        request.headers.emplace_back("Content-Type", "application/octet-stream");
        request.body = body;
    }

    const auto response = ahfl::support::execute_curl(request);
    return HttpResult{
        .status_code = response.status_code,
        .body = response.body,
        .success = response.is_success(),
    };
}

// Get best endpoint sorted by priority (highest first)
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
        return a.first > b.first;
    });

    std::vector<std::string> result;
    result.reserve(endpoints.size());
    for (auto &[_, ep] : endpoints) {
        result.push_back(std::move(ep));
    }
    return result;
}

} // namespace

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
    auto root = ahfl::json::JsonValue::make_object();
    root->set("format", ahfl::json::JsonValue::make_string("AHFL_SNAPSHOT_V2"));
    root->set("agent_id", ahfl::json::JsonValue::make_string(snapshot.agent_id));
    root->set("current_state", ahfl::json::JsonValue::make_string(snapshot.current_state));
    auto epoch =
        std::chrono::duration_cast<std::chrono::milliseconds>(snapshot.timestamp.time_since_epoch())
            .count();
    root->set("timestamp_ms", ahfl::json::JsonValue::make_int(epoch));

    auto context = ahfl::json::JsonValue::make_object();
    std::vector<std::pair<std::string, std::string>> sorted_context(snapshot.context_values.begin(),
                                                                    snapshot.context_values.end());
    std::sort(sorted_context.begin(), sorted_context.end());
    for (const auto &[key, value] : sorted_context) {
        context->set(key, ahfl::json::JsonValue::make_string(value));
    }
    root->set("context", std::move(context));
    return ahfl::json::serialize_json(*root);
}

std::optional<StateSnapshot>
DistributedScheduler::deserialize_snapshot(const std::string &data) const {
    if (data.empty())
        return std::nullopt;

    if (auto parsed = ahfl::json::parse_json(data); parsed.has_value() && *parsed &&
                                                (*parsed)->is_object()) {
        const auto *format = (*parsed)->get("format");
        if (format != nullptr) {
            auto format_value = format->as_string();
            if (!format_value.has_value() || *format_value != "AHFL_SNAPSHOT_V2") {
                return std::nullopt;
            }
        }

        const auto *agent_id = (*parsed)->get("agent_id");
        const auto *state = (*parsed)->get("current_state");
        const auto *timestamp = (*parsed)->get("timestamp_ms");
        if (agent_id == nullptr || state == nullptr || timestamp == nullptr) {
            return std::nullopt;
        }
        auto agent_id_value = agent_id->as_string();
        auto state_value = state->as_string();
        auto timestamp_value = timestamp->as_int();
        if (!agent_id_value.has_value() || !state_value.has_value() ||
            !timestamp_value.has_value() || agent_id_value->empty()) {
            return std::nullopt;
        }

        StateSnapshot snapshot;
        snapshot.agent_id = std::string(*agent_id_value);
        snapshot.current_state = std::string(*state_value);
        snapshot.timestamp =
            std::chrono::system_clock::time_point(std::chrono::milliseconds(*timestamp_value));

        if (const auto *context = (*parsed)->get("context");
            context != nullptr && context->is_object()) {
            for (const auto &[key, value] : context->object_fields) {
                if (auto string_value = value->as_string(); string_value.has_value()) {
                    snapshot.context_values[key] = std::string(*string_value);
                }
            }
        }
        return snapshot;
    }

    std::istringstream iss(data);
    std::string line;

    // Verify header
    if (!std::getline(iss, line) || line != "AHFL_SNAPSHOT_V1") {
        return std::nullopt;
    }

    StateSnapshot snapshot;
    while (std::getline(iss, line)) {
        auto eq_pos = line.find('=');
        if (eq_pos == std::string::npos)
            continue;
        auto key = line.substr(0, eq_pos);
        auto value = line.substr(eq_pos + 1);

        if (key == "agent_id") {
            snapshot.agent_id = value;
        } else if (key == "current_state") {
            snapshot.current_state = value;
        } else if (key == "timestamp") {
            try {
                auto ms = std::stoll(value);
                snapshot.timestamp =
                    std::chrono::system_clock::time_point(std::chrono::milliseconds(ms));
            } catch (...) {
            }
        } else if (key.starts_with("ctx.")) {
            snapshot.context_values[key.substr(4)] = value;
        }
    }

    if (snapshot.agent_id.empty())
        return std::nullopt;
    return snapshot;
}

CheckpointResult DistributedScheduler::checkpoint(const StateSnapshot &snapshot) const {
    CheckpointResult result;
    result.checkpoint_id = generate_checkpoint_id(snapshot.agent_id);
    std::string serialized = serialize_snapshot(snapshot);

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

    auto filepath = dir / (result.checkpoint_id + ".snapshot");
    auto tmp_path = dir / (result.checkpoint_id + ".snapshot.tmp");

    // Atomic write: write to temp then rename
    {
        std::ofstream ofs(tmp_path, std::ios::binary);
        if (!ofs) {
            result.status = CheckpointStatus::Failed;
            result.error = "failed to write checkpoint file";
            return result;
        }
        ofs << serialized;
    }
    fs::rename(tmp_path, filepath, ec);
    if (ec) {
        result.status = CheckpointStatus::Failed;
        result.error = "failed to rename checkpoint file: " + ec.message();
        return result;
    }

    // Local-only: status remains Created (not Stored to remote)
    result.status = CheckpointStatus::Created;
    return result;
}

std::optional<StateSnapshot> DistributedScheduler::restore(const std::string &checkpoint_id) const {

    // 1. Try remote restore via HTTP
    auto endpoints = get_sorted_endpoints(regions_);
    int timeout = static_cast<int>(failover_policy_.timeout.count());

    for (const auto &endpoint : endpoints) {
        auto url = endpoint + "/checkpoints/" + checkpoint_id;
        auto http_result = http_request("GET", url, "", timeout);
        if (http_result.success && !http_result.body.empty()) {
            auto snapshot = deserialize_snapshot(http_result.body);
            if (snapshot.has_value())
                return snapshot;
        }
    }

    // 2. Fallback to local filesystem
    auto dir = checkpoint_directory();
    auto filepath = dir / (checkpoint_id + ".snapshot");

    if (fs::exists(filepath)) {
        std::ifstream ifs(filepath, std::ios::binary);
        if (ifs) {
            std::string content((std::istreambuf_iterator<char>(ifs)),
                                std::istreambuf_iterator<char>());
            return deserialize_snapshot(content);
        }
    }

    // Try prefix match (for partial checkpoint IDs)
    if (fs::exists(dir)) {
        for (const auto &entry : fs::directory_iterator(dir)) {
            auto filename = entry.path().filename().string();
            if (filename.starts_with(checkpoint_id) && filename.ends_with(".snapshot")) {
                std::ifstream ifs(entry.path(), std::ios::binary);
                if (ifs) {
                    std::string content((std::istreambuf_iterator<char>(ifs)),
                                        std::istreambuf_iterator<char>());
                    return deserialize_snapshot(content);
                }
            }
        }
    }

    return std::nullopt;
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
