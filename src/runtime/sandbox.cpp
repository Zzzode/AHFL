#include "ahfl/runtime/sandbox.hpp"
#include <algorithm>

namespace ahfl::runtime {

Sandbox::Sandbox(SandboxConfig config) : config_(std::move(config)) {}

bool Sandbox::check_network_access(const std::string& host, uint16_t port) const {
    if (!config_.network.allow_network) {
        return false;
    }

    bool host_ok = config_.network.allowed_hosts.empty();
    for (const auto& h : config_.network.allowed_hosts) {
        if (h == host || h == "*") {
            host_ok = true;
            break;
        }
    }

    bool port_ok = config_.network.allowed_ports.empty();
    for (auto p : config_.network.allowed_ports) {
        if (p == port) {
            port_ok = true;
            break;
        }
    }

    return host_ok && port_ok;
}

bool Sandbox::check_fs_read(const std::string& path) const {
    if (!config_.filesystem.allow_filesystem) {
        return false;
    }
    return path_matches(path, config_.filesystem.read_paths);
}

bool Sandbox::check_fs_write(const std::string& path) const {
    if (!config_.filesystem.allow_filesystem) {
        return false;
    }
    return path_matches(path, config_.filesystem.write_paths);
}

bool Sandbox::check_memory(size_t bytes) const {
    return bytes <= config_.resources.max_memory_bytes;
}

bool Sandbox::check_thread_count(size_t count) const {
    return count <= config_.resources.max_threads;
}

const SandboxConfig& Sandbox::config() const {
    return config_;
}

bool Sandbox::path_matches(const std::string& path, const std::vector<std::string>& allowed) const {
    for (const auto& prefix : allowed) {
        if (path.starts_with(prefix)) {
            return true;
        }
    }
    return false;
}

} // namespace ahfl::runtime
