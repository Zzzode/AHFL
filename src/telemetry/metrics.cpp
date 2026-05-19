#include "ahfl/telemetry/metrics.hpp"

#include <utility>

namespace ahfl::telemetry {

Counter::Counter(std::string name) : name_(std::move(name)) {}

void Counter::increment(double amount) {
    value_ += amount;
}

double Counter::value() const {
    return value_;
}

const std::string& Counter::name() const {
    return name_;
}

Histogram::Histogram(std::string name, std::vector<double> boundaries)
    : name_(std::move(name)), boundaries_(std::move(boundaries)) {
    bucket_counts_.resize(boundaries_.size() + 1, 0);
}

void Histogram::record(double value) {
    ++count_;
    sum_ += value;
    // Find the bucket
    bool placed = false;
    for (std::size_t i = 0; i < boundaries_.size(); ++i) {
        if (value <= boundaries_[i]) {
            ++bucket_counts_[i];
            placed = true;
            break;
        }
    }
    if (!placed) {
        ++bucket_counts_[boundaries_.size()];
    }
}

std::size_t Histogram::count() const {
    return count_;
}

double Histogram::sum() const {
    return sum_;
}

const std::string& Histogram::name() const {
    return name_;
}

void InMemoryMetricsExporter::export_metric(const MetricPoint& point) {
    metrics_.push_back(point);
}

std::size_t InMemoryMetricsExporter::metric_count() const {
    return metrics_.size();
}

const std::vector<MetricPoint>& InMemoryMetricsExporter::metrics() const {
    return metrics_;
}

void InMemoryMetricsExporter::clear() {
    metrics_.clear();
}

} // namespace ahfl::telemetry
