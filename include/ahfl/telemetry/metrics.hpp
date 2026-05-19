#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

namespace ahfl::telemetry {

struct MetricPoint {
    std::string name;
    double value;
    std::chrono::steady_clock::time_point timestamp;
    std::vector<std::pair<std::string, std::string>> labels;
};

class Counter {
  public:
    explicit Counter(std::string name);
    void increment(double amount = 1.0);
    [[nodiscard]] double value() const;
    [[nodiscard]] const std::string &name() const;

  private:
    std::string name_;
    std::atomic<double> value_{0.0};
};

class Gauge {
  public:
    explicit Gauge(std::string name);
    void set(double value);
    void increment(double amount = 1.0);
    void decrement(double amount = 1.0);
    [[nodiscard]] double value() const;
    [[nodiscard]] const std::string &name() const;

  private:
    std::string name_;
    std::atomic<double> value_{0.0};
};

class Histogram {
  public:
    explicit Histogram(std::string name, std::vector<double> boundaries = {});
    void record(double value);
    [[nodiscard]] std::size_t count() const;
    [[nodiscard]] double sum() const;
    [[nodiscard]] const std::string &name() const;

  private:
    std::string name_;
    std::vector<double> boundaries_;
    std::vector<std::size_t> bucket_counts_;
    std::size_t count_ = 0;
    double sum_ = 0.0;
};

class MetricsExporter {
  public:
    virtual ~MetricsExporter() = default;
    virtual void export_metric(const MetricPoint &point) = 0;
    [[nodiscard]] virtual std::size_t metric_count() const = 0;
};

class InMemoryMetricsExporter final : public MetricsExporter {
  public:
    void export_metric(const MetricPoint &point) override;
    [[nodiscard]] std::size_t metric_count() const override;
    [[nodiscard]] const std::vector<MetricPoint> &metrics() const;
    void clear();

  private:
    std::vector<MetricPoint> metrics_;
};

class FileMetricsExporter final : public MetricsExporter {
  public:
    explicit FileMetricsExporter(std::string path);
    void export_metric(const MetricPoint &point) override;
    [[nodiscard]] std::size_t metric_count() const override;

  private:
    std::string path_;
    std::size_t count_ = 0;
    mutable std::mutex mutex_;
};

} // namespace ahfl::telemetry
