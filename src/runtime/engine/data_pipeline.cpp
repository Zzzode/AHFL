#include "runtime/engine/data_pipeline.hpp"

namespace ahfl::runtime {

void DataPipeline::publish(const std::string& node_id, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    values_[node_id] = value;
}

std::optional<std::string> DataPipeline::consume(const std::string& node_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = values_.find(node_id);
    if (it != values_.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool DataPipeline::has_value(const std::string& node_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return values_.count(node_id) > 0;
}

void DataPipeline::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    values_.clear();
}

size_t DataPipeline::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return values_.size();
}

} // namespace ahfl::runtime
