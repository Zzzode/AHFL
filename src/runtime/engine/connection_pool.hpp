#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ahfl::runtime {

/// Connection pool that manages concurrency limits per host.
/// Since the transport uses popen+curl, this acts as a concurrency limiter
/// rather than a true TCP connection pool.
class ConnectionPool {
  public:
    class Lease {
      public:
        Lease() = default;
        ~Lease();

        Lease(const Lease &) = delete;
        Lease &operator=(const Lease &) = delete;

        Lease(Lease &&other) noexcept;
        Lease &operator=(Lease &&other) noexcept;

        [[nodiscard]] bool acquired() const noexcept;
        void release() noexcept;

      private:
        friend class ConnectionPool;

        Lease(ConnectionPool &pool, std::string host);

        ConnectionPool *pool_{nullptr};
        std::string host_;
    };

    struct Config {
        std::size_t max_connections{8};
        std::size_t max_per_host{4};
        std::chrono::seconds idle_timeout{60};
    };

    ConnectionPool();
    explicit ConnectionPool(Config config);

    /// Check if a new connection to the given host is allowed.
    [[nodiscard]] bool can_connect(std::string_view host) const;

    /// Atomically acquire a connection slot for the given host.
    [[nodiscard]] Lease try_acquire(std::string_view host);

    /// Record that a connection to the given host has started.
    void record_start(std::string_view host);

    /// Record that a connection to the given host has ended.
    void record_end(std::string_view host);

    /// Remove idle host entries (those with 0 active connections past idle_timeout).
    void evict_idle();

    /// Total number of active connections across all hosts.
    [[nodiscard]] std::size_t active_count() const;

    /// Number of active connections to a specific host.
    [[nodiscard]] std::size_t active_for_host(std::string_view host) const;

  private:
    struct HostEntry {
        std::size_t active_count{0};
        std::chrono::steady_clock::time_point last_activity;
    };

    Config config_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, HostEntry> hosts_;
    std::size_t total_active_{0};
};

} // namespace ahfl::runtime
