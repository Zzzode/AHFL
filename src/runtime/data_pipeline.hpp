#pragma once
#include <string>
#include <unordered_map>
#include <mutex>
#include <optional>

namespace ahfl::runtime {

class DataPipeline {
public:
    void publish(const std::string& node_id, const std::string& value);
    [[nodiscard]] std::optional<std::string> consume(const std::string& node_id) const;
    [[nodiscard]] bool has_value(const std::string& node_id) const;
    void clear();
    [[nodiscard]] size_t size() const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::string> values_;
};

} // namespace ahfl::runtime
