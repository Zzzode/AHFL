#pragma once

#include "runtime/providers/secret/secret_provider.hpp"

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ahfl::secret {

enum class RotationStatus {
    Idle,
    Scheduled,
    InProgress,
    Completed,
    Failed,
};

struct RotationPolicy {
    std::string key;
    std::chrono::seconds interval{3600};    // rotate every hour by default
    std::chrono::seconds grace_period{300}; // old key valid for 5min after rotation
    std::size_t max_retries = 3;
};

struct RotationEvent {
    std::string key;
    RotationStatus status{RotationStatus::Idle};
    std::chrono::system_clock::time_point timestamp;
    std::string old_version;
    std::string new_version;
    std::string error_message;
};

using RotationCallback = std::function<void(const RotationEvent &)>;

class KeyRotationManager {
  public:
    explicit KeyRotationManager(SecretProvider &provider);

    void add_policy(RotationPolicy policy);
    void remove_policy(const std::string &key);
    [[nodiscard]] std::optional<RotationPolicy> get_policy(const std::string &key) const;
    [[nodiscard]] std::vector<RotationPolicy> all_policies() const;

    /// Trigger rotation check for all keys.
    std::vector<RotationEvent> check_and_rotate();

    /// Trigger rotation for a specific key.
    RotationEvent rotate_key(const std::string &key);

    /// Register callback for rotation events.
    void on_rotation(RotationCallback callback);

    /// Query rotation history.
    [[nodiscard]] std::vector<RotationEvent> history() const;
    [[nodiscard]] std::vector<RotationEvent> history_for(const std::string &key) const;

    [[nodiscard]] std::size_t policy_count() const;

  private:
    SecretProvider &provider_;
    std::unordered_map<std::string, RotationPolicy> policies_;
    std::vector<RotationEvent> history_;
    std::vector<RotationCallback> callbacks_;
    std::unordered_map<std::string, std::chrono::system_clock::time_point> last_rotation_;
};

} // namespace ahfl::secret
