#include "telemetry/metrics.hpp"

#include <fstream>
#include <sstream>
#include <utility>

namespace ahfl::telemetry {

Counter::Counter(std::string name) : name_(std::move(name)) {}

void Counter::increment(double amount) {
    auto current = value_.load(std::memory_order_relaxed);
    while (!value_.compare_exchange_weak(current, current + amount, std::memory_order_relaxed)) {
    }
}

double Counter::value() const {
    return value_.load(std::memory_order_relaxed);
}

const std::string &Counter::name() const {
    return name_;
}

Gauge::Gauge(std::string name) : name_(std::move(name)) {}

void Gauge::set(double value) {
    value_.store(value, std::memory_order_relaxed);
}

void Gauge::increment(double amount) {
    auto current = value_.load(std::memory_order_relaxed);
    while (!value_.compare_exchange_weak(current, current + amount, std::memory_order_relaxed)) {
    }
}

void Gauge::decrement(double amount) {
    increment(-amount);
}

double Gauge::value() const {
    return value_.load(std::memory_order_relaxed);
}

const std::string &Gauge::name() const {
    return name_;
}

Histogram::Histogram(std::string name, std::vector<double> boundaries)
    : name_(std::move(name)), boundaries_(std::move(boundaries)) {
    bucket_counts_.resize(boundaries_.size() + 1, 0);
}

void Histogram::record(double value) {
    ++count_;
    sum_ += value;
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

const std::string &Histogram::name() const {
    return name_;
}

void InMemoryMetricsExporter::export_metric(const MetricPoint &point) {
    metrics_.push_back(point);
}

std::size_t InMemoryMetricsExporter::metric_count() const {
    return metrics_.size();
}

const std::vector<MetricPoint> &InMemoryMetricsExporter::metrics() const {
    return metrics_;
}

void InMemoryMetricsExporter::clear() {
    metrics_.clear();
}

FileMetricsExporter::FileMetricsExporter(std::string path) : path_(std::move(path)) {}

void FileMetricsExporter::export_metric(const MetricPoint &point) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ofstream ofs(path_, std::ios::app);
    if (ofs) {
        ofs << "{\"name\":\"" << point.name << "\",\"value\":" << point.value;
        auto ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(point.timestamp.time_since_epoch())
                .count();
        ofs << ",\"timestampNano\":" << ns;
        if (!point.labels.empty()) {
            ofs << ",\"labels\":{";
            bool first = true;
            for (const auto &[key, value] : point.labels) {
                if (!first)
                    ofs << ",";
                ofs << "\"" << key << "\":\"" << value << "\"";
                first = false;
            }
            ofs << "}";
        }
        ofs << "}\n";
    }
    ++count_;
}

std::size_t FileMetricsExporter::metric_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return count_;
}

} // namespace ahfl::telemetry
