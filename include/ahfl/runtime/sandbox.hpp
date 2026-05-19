#pragma once
#include <string>
#include <vector>
#include <chrono>
#include <cstdint>

namespace ahfl::runtime {

struct ResourceQuota {
    size_t max_memory_bytes = 256 * 1024 * 1024;  // 256 MB
    size_t max_threads = 8;
    std::chrono::seconds max_cpu_time{60};
};

struct NetworkPolicy {
    bool allow_network = false;
    std::vector<std::string> allowed_hosts;
    std::vector<uint16_t> allowed_ports;
};

struct FsIsolation {
    bool allow_filesystem = false;
    std::vector<std::string> read_paths;
    std::vector<std::string> write_paths;
};

struct SandboxConfig {
    ResourceQuota resources;
    NetworkPolicy network;
    FsIsolation filesystem;
};

enum class ViolationKind {
    MemoryExceeded,
    ThreadLimitExceeded,
    CpuTimeExceeded,
    NetworkDenied,
    HostNotAllowed,
    PortNotAllowed,
    FsDenied,
    PathNotAllowed
};

struct SandboxViolation {
    ViolationKind kind;
    std::string detail;
};

class Sandbox {
public:
    explicit Sandbox(SandboxConfig config);

    [[nodiscard]] bool check_network_access(const std::string& host, uint16_t port) const;
    [[nodiscard]] bool check_fs_read(const std::string& path) const;
    [[nodiscard]] bool check_fs_write(const std::string& path) const;
    [[nodiscard]] bool check_memory(size_t bytes) const;
    [[nodiscard]] bool check_thread_count(size_t count) const;

    [[nodiscard]] const SandboxConfig& config() const;

private:
    SandboxConfig config_;
    [[nodiscard]] bool path_matches(const std::string& path, const std::vector<std::string>& allowed) const;
};

} // namespace ahfl::runtime
