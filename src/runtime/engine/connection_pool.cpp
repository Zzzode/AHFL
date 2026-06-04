#include "runtime/engine/connection_pool.hpp"

#include <algorithm>
#include <utility>

namespace ahfl::runtime {

ConnectionPool::Lease::Lease(ConnectionPool &pool, std::string host)
    : pool_(&pool), host_(std::move(host)) {}

ConnectionPool::Lease::~Lease() {
    release();
}

ConnectionPool::Lease::Lease(Lease &&other) noexcept
    : pool_(std::exchange(other.pool_, nullptr)), host_(std::move(other.host_)) {}

ConnectionPool::Lease &ConnectionPool::Lease::operator=(Lease &&other) noexcept {
    if (this != &other) {
        release();
        pool_ = std::exchange(other.pool_, nullptr);
        host_ = std::move(other.host_);
    }
    return *this;
}

bool ConnectionPool::Lease::acquired() const noexcept {
    return pool_ != nullptr;
}

void ConnectionPool::Lease::release() noexcept {
    if (pool_ == nullptr) {
        return;
    }
    pool_->record_end(host_);
    pool_ = nullptr;
}

ConnectionPool::ConnectionPool() : config_{} {}

ConnectionPool::ConnectionPool(Config config) : config_(config) {}

bool ConnectionPool::can_connect(std::string_view host) const {
    std::lock_guard lock(mutex_);

    if (total_active_ >= config_.max_connections) {
        return false;
    }

    auto it = hosts_.find(std::string(host));
    if (it != hosts_.end() && it->second.active_count >= config_.max_per_host) {
        return false;
    }

    return true;
}

ConnectionPool::Lease ConnectionPool::try_acquire(std::string_view host) {
    std::lock_guard lock(mutex_);

    if (total_active_ >= config_.max_connections) {
        return Lease{};
    }

    auto host_key = std::string(host);
    auto &entry = hosts_[host_key];
    if (entry.active_count >= config_.max_per_host) {
        return Lease{};
    }

    ++entry.active_count;
    entry.last_activity = std::chrono::steady_clock::now();
    ++total_active_;
    return Lease(*this, std::move(host_key));
}

void ConnectionPool::record_start(std::string_view host) {
    std::lock_guard lock(mutex_);

    auto &entry = hosts_[std::string(host)];
    ++entry.active_count;
    entry.last_activity = std::chrono::steady_clock::now();
    ++total_active_;
}

void ConnectionPool::record_end(std::string_view host) {
    std::lock_guard lock(mutex_);

    auto it = hosts_.find(std::string(host));
    if (it != hosts_.end() && it->second.active_count > 0) {
        --it->second.active_count;
        it->second.last_activity = std::chrono::steady_clock::now();
        --total_active_;
    }
}

void ConnectionPool::evict_idle() {
    std::lock_guard lock(mutex_);

    auto now = std::chrono::steady_clock::now();
    for (auto it = hosts_.begin(); it != hosts_.end();) {
        if (it->second.active_count == 0 &&
            (now - it->second.last_activity) > config_.idle_timeout) {
            it = hosts_.erase(it);
        } else {
            ++it;
        }
    }
}

std::size_t ConnectionPool::active_count() const {
    std::lock_guard lock(mutex_);
    return total_active_;
}

std::size_t ConnectionPool::active_for_host(std::string_view host) const {
    std::lock_guard lock(mutex_);

    auto it = hosts_.find(std::string(host));
    if (it != hosts_.end()) {
        return it->second.active_count;
    }
    return 0;
}

} // namespace ahfl::runtime
