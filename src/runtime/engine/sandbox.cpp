#include "runtime/engine/sandbox.hpp"

#include <cerrno>
#include <cstring>
#include <filesystem>

#ifdef __unix__
#include <sys/resource.h>
#endif

namespace ahfl::runtime {

namespace {

std::string canonicalize_path(const std::string &path) {
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(path, ec);
    return ec ? path : canonical.string();
}

} // namespace

Sandbox::Sandbox(SandboxConfig config) : config_(std::move(config)) {}

bool Sandbox::check_network_access(const std::string &host, uint16_t port) const {
    if (!config_.network.allow_network) {
        return false;
    }
    // Check host allowlist
    if (!config_.network.allowed_hosts.empty()) {
        bool host_found = false;
        for (const auto &allowed : config_.network.allowed_hosts) {
            if (allowed == host || allowed == "*") {
                host_found = true;
                break;
            }
        }
        if (!host_found)
            return false;
    }
    // Check port allowlist
    if (!config_.network.allowed_ports.empty()) {
        bool port_found = false;
        for (auto allowed_port : config_.network.allowed_ports) {
            if (allowed_port == port) {
                port_found = true;
                break;
            }
        }
        if (!port_found)
            return false;
    }
    return true;
}

bool Sandbox::check_fs_read(const std::string &path) const {
    if (!config_.filesystem.allow_filesystem)
        return false;
    auto canonical = canonicalize_path(path);
    return path_matches(canonical, config_.filesystem.read_paths);
}

bool Sandbox::check_fs_write(const std::string &path) const {
    if (!config_.filesystem.allow_filesystem)
        return false;
    auto canonical = canonicalize_path(path);
    return path_matches(canonical, config_.filesystem.write_paths);
}

bool Sandbox::check_memory(size_t bytes) const {
    return bytes <= config_.resources.max_memory_bytes;
}

bool Sandbox::check_thread_count(size_t count) const {
    return count <= config_.resources.max_threads;
}

const SandboxConfig &Sandbox::config() const {
    return config_;
}

Sandbox::EnforcementResult Sandbox::enforce() const {
    EnforcementResult result;

#ifdef __unix__
    // Enforce memory limit via RLIMIT_AS (address space)
    struct rlimit mem_limit {};
    mem_limit.rlim_cur = static_cast<rlim_t>(config_.resources.max_memory_bytes);
    mem_limit.rlim_max = static_cast<rlim_t>(config_.resources.max_memory_bytes);
    if (setrlimit(RLIMIT_AS, &mem_limit) != 0) {
        result.success = false;
        result.error = std::string("failed to set RLIMIT_AS: ") + std::strerror(errno);
        return result;
    }

    // Enforce CPU time via RLIMIT_CPU
    auto cpu_seconds = static_cast<rlim_t>(config_.resources.max_cpu_time.count());
    if (cpu_seconds > 0) {
        struct rlimit cpu_limit {};
        cpu_limit.rlim_cur = cpu_seconds;
        cpu_limit.rlim_max = cpu_seconds;
        if (setrlimit(RLIMIT_CPU, &cpu_limit) != 0) {
            result.success = false;
            result.error = std::string("failed to set RLIMIT_CPU: ") + std::strerror(errno);
            return result;
        }
    }

#ifdef __linux__
    // Enforce thread limit via RLIMIT_NPROC (Linux only)
    struct rlimit nproc_limit {};
    nproc_limit.rlim_cur = static_cast<rlim_t>(config_.resources.max_threads);
    nproc_limit.rlim_max = static_cast<rlim_t>(config_.resources.max_threads);
    if (setrlimit(RLIMIT_NPROC, &nproc_limit) != 0) {
        result.success = false;
        result.error = std::string("failed to set RLIMIT_NPROC: ") + std::strerror(errno);
        return result;
    }
#endif // __linux__
#endif // __unix__

    return result;
}

bool Sandbox::path_matches(const std::string &path, const std::vector<std::string> &allowed) const {
    for (const auto &prefix : allowed) {
        auto canonical_prefix = canonicalize_path(prefix);
        if (path.starts_with(canonical_prefix)) {
            return true;
        }
    }
    return false;
}

} // namespace ahfl::runtime
