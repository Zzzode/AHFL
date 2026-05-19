#include "ahfl/secret/key_rotation.hpp"

#include <algorithm>
#include <chrono>

namespace ahfl::secret {

// ============================================================================
// KeyRotationManager
// ============================================================================

KeyRotationManager::KeyRotationManager(SecretProvider &provider) : provider_(provider) {}

void KeyRotationManager::add_policy(RotationPolicy policy) {
    std::string key = policy.key;
    policies_[key] = std::move(policy);
}

void KeyRotationManager::remove_policy(const std::string &key) { policies_.erase(key); }

std::optional<RotationPolicy> KeyRotationManager::get_policy(const std::string &key) const {
    auto it = policies_.find(key);
    if (it == policies_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<RotationPolicy> KeyRotationManager::all_policies() const {
    std::vector<RotationPolicy> result;
    result.reserve(policies_.size());
    for (const auto &[k, v] : policies_) {
        static_cast<void>(k);
        result.push_back(v);
    }
    return result;
}

std::vector<RotationEvent> KeyRotationManager::check_and_rotate() {
    std::vector<RotationEvent> events;
    auto now = std::chrono::system_clock::now();

    for (const auto &[key, policy] : policies_) {
        auto last_it = last_rotation_.find(key);
        bool should_rotate = false;

        if (last_it == last_rotation_.end()) {
            // Never rotated — rotate now
            should_rotate = true;
        } else {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_it->second);
            if (elapsed >= policy.interval) {
                should_rotate = true;
            }
        }

        if (should_rotate) {
            events.push_back(rotate_key(key));
        }
    }

    return events;
}

RotationEvent KeyRotationManager::rotate_key(const std::string &key) {
    auto now = std::chrono::system_clock::now();

    // Get old value
    auto old_value = provider_.resolve(key);
    std::string old_version = old_value.value_or("");

    // Refresh the key in the provider
    provider_.refresh(key);

    // Get new value
    auto new_value = provider_.resolve(key);
    std::string new_version = new_value.value_or("");

    // Create event
    RotationEvent event;
    event.key = key;
    event.status = RotationStatus::Completed;
    event.timestamp = now;
    event.old_version = std::move(old_version);
    event.new_version = std::move(new_version);

    // Record last rotation time
    last_rotation_[key] = now;

    // Store in history
    history_.push_back(event);

    // Notify callbacks
    for (const auto &cb : callbacks_) {
        cb(event);
    }

    return event;
}

void KeyRotationManager::on_rotation(RotationCallback callback) {
    callbacks_.push_back(std::move(callback));
}

std::vector<RotationEvent> KeyRotationManager::history() const { return history_; }

std::vector<RotationEvent> KeyRotationManager::history_for(const std::string &key) const {
    std::vector<RotationEvent> result;
    std::copy_if(history_.begin(), history_.end(), std::back_inserter(result),
                 [&key](const RotationEvent &e) { return e.key == key; });
    return result;
}

std::size_t KeyRotationManager::policy_count() const { return policies_.size(); }

} // namespace ahfl::secret
