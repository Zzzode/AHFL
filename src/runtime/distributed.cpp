#include "ahfl/runtime/distributed.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
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
    HttpResult result;

    std::string safe_url;
    for (char c : url) {
        if (c == '\'')
            safe_url += "'\\''";
        else
            safe_url += c;
    }

    std::string cmd;
    if (method == "PUT" || method == "POST") {
        // Write body to a temp file to avoid shell escaping issues
        auto tmp_path = fs::temp_directory_path() / "ahfl_http_body.tmp";
        {
            std::ofstream tmp(tmp_path, std::ios::binary);
            if (tmp)
                tmp << body;
        }
        cmd = "curl -s -w '\\n%{http_code}' -X " + method + " --max-time " +
              std::to_string(timeout_seconds) + " -H 'Content-Type: application/octet-stream'" +
              " --data-binary @'" + tmp_path.string() + "'" + " '" + safe_url + "' 2>/dev/null";
    } else {
        cmd = "curl -s -w '\\n%{http_code}' -X " + method + " --max-time " +
              std::to_string(timeout_seconds) + " '" + safe_url + "' 2>/dev/null";
    }

    FILE *pipe = popen(cmd.c_str(), "r");
    if (pipe == nullptr)
        return result;

    std::string output;
    std::array<char, 4096> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }
    pclose(pipe);

    if (output.empty())
        return result;

    // Extract status code from last line
    auto last_newline = output.rfind('\n', output.size() - 2);
    std::string status_str;
    if (last_newline == std::string::npos) {
        status_str = output;
        result.body = "";
    } else {
        status_str = output.substr(last_newline + 1);
        result.body = output.substr(0, last_newline);
    }

    while (!status_str.empty() && (status_str.back() == '\n' || status_str.back() == '\r')) {
        status_str.pop_back();
    }

    try {
        result.status_code = std::stoi(status_str);
    } catch (...) {
        return result;
    }

    result.success = (result.status_code >= 200 && result.status_code < 300);
    return result;
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
    std::ostringstream oss;
    oss << "AHFL_SNAPSHOT_V1\n";
    oss << "agent_id=" << snapshot.agent_id << "\n";
    oss << "current_state=" << snapshot.current_state << "\n";
    auto epoch =
        std::chrono::duration_cast<std::chrono::milliseconds>(snapshot.timestamp.time_since_epoch())
            .count();
    oss << "timestamp=" << epoch << "\n";
    // Sort context keys for deterministic output
    std::vector<std::pair<std::string, std::string>> sorted_ctx(snapshot.context_values.begin(),
                                                                snapshot.context_values.end());
    std::sort(sorted_ctx.begin(), sorted_ctx.end());
    for (const auto &[key, value] : sorted_ctx) {
        oss << "ctx." << key << "=" << value << "\n";
    }
    return oss.str();
}

std::optional<StateSnapshot>
DistributedScheduler::deserialize_snapshot(const std::string &data) const {
    if (data.empty())
        return std::nullopt;

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
