#include "runtime/providers/secret/key_rotation.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace ahfl::secret {

namespace {

[[nodiscard]] std::uint64_t fnv1a_hash(std::string_view value) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (char ch : value) {
        hash ^= static_cast<unsigned char>(ch);
        hash *= 1099511628211ULL;
    }
    return hash;
}

[[nodiscard]] std::string hex_hash(std::uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << value;
    return out.str();
}

[[nodiscard]] std::string provider_prefix(std::string_view key) {
    const auto separator = key.find(':');
    if (separator == std::string_view::npos || separator == 0) {
        return "unqualified";
    }
    return std::string(key.substr(0, separator));
}

} // namespace

// ============================================================================
// KeyRotationManager
// ============================================================================

KeyRotationManager::KeyRotationManager(SecretProvider &provider) : provider_(provider) {}

void KeyRotationManager::add_policy(RotationPolicy policy) {
    std::string key = policy.key;
    policies_[key] = std::move(policy);
}

void KeyRotationManager::remove_policy(const std::string &key) {
    policies_.erase(key);
}

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

    auto old_value = provider_.resolve(key);
    std::optional<std::string> new_value;
    const auto policy = get_policy(key);
    const auto max_attempts =
        std::max<std::size_t>(std::size_t{1}, policy.has_value() ? policy->max_retries + 1 : 1);

    std::size_t attempts = 0;
    for (; attempts < max_attempts; ++attempts) {
        provider_.refresh(key);
        new_value = provider_.resolve(key);
        if (new_value.has_value()) {
            ++attempts;
            break;
        }
    }

    RotationEvent event;
    event.key = key;
    event.provider_prefix = provider_prefix(key);
    event.key_fingerprint = hex_hash(fnv1a_hash(key));
    event.status = new_value.has_value() ? RotationStatus::Completed : RotationStatus::Failed;
    event.timestamp = now;
    event.previous_value_present = old_value.has_value();
    event.rotated_value_present = new_value.has_value();
    event.attempts = attempts;
    event.secret_free = true;
    if (!new_value.has_value()) {
        event.error_message = "secret provider returned no value for rotated key";
    }

    if (event.status == RotationStatus::Completed) {
        last_rotation_[key] = now;
    }

    history_.push_back(event);

    for (const auto &cb : callbacks_) {
        cb(event);
    }

    return event;
}

void KeyRotationManager::on_rotation(RotationCallback callback) {
    callbacks_.push_back(std::move(callback));
}

std::vector<RotationEvent> KeyRotationManager::history() const {
    return history_;
}

std::vector<RotationEvent> KeyRotationManager::history_for(const std::string &key) const {
    std::vector<RotationEvent> result;
    std::copy_if(history_.begin(),
                 history_.end(),
                 std::back_inserter(result),
                 [&key](const RotationEvent &e) { return e.key == key; });
    return result;
}

std::size_t KeyRotationManager::policy_count() const {
    return policies_.size();
}

} // namespace ahfl::secret
