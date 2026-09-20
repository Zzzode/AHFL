// The one translation unit that owns the private JSON-writer dependency for the
// shared bench harness. `base/support/json.hpp` lives under `src/`, so this file
// is the only place the src-private include root is required; every bench TU
// includes the dependency-free `bench_harness.hpp` instead (see its header
// comment). Keeping the writer out of the header means a new bench cannot fail
// to compile because it forgot to add `${PROJECT_SOURCE_DIR}/src`.

#include "bench_harness.hpp"

#include <fstream>
#include <ostream>

#include "base/support/json.hpp"

namespace ahfl::bench {
namespace {

// Writes the `ahfl.bench-report.v1` document with the repository's canonical
// `PrettyJsonWriter` (2-space indent), the same spelling the IR / assurance /
// counterexample JSON artifacts use.
class BenchReportWriter final : private PrettyJsonWriter {
  public:
    explicit BenchReportWriter(std::ostream &out) : PrettyJsonWriter(out) {}

    void write(std::string_view kind, const std::vector<BenchMetric> &metrics) {
        print_object(0, [&](const auto &field) {
            field("schema", [&]() { write_string(kBenchReportSchema); });
            field("kind", [&]() { write_string(kind); });
            field("budgets", [&]() {
                print_array(1, [&](const auto &item) {
                    for (const auto &metric : metrics) {
                        item([&]() { write_metric(metric, 2); });
                    }
                });
            });
        });
        out_ << '\n';
    }

  private:
    void write_metric(const BenchMetric &metric, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("name", [&]() { write_string(metric.name); });
            field("source_bytes", [&]() { out_ << metric.source_bytes; });
            field("typed_exprs", [&]() { out_ << metric.typed_exprs; });
            field("typed_statements", [&]() { out_ << metric.typed_statements; });
            field("ir_decls", [&]() { out_ << metric.ir_decls; });
            field("ir_exprs", [&]() { out_ << metric.ir_exprs; });
            field("proxy_bytes", [&]() { out_ << metric.proxy_bytes; });
            field("duration_us", [&]() { out_ << metric.duration.count(); });
        });
    }
};

} // namespace

bool write_bench_report(const std::string &path, std::string_view kind,
                        const std::vector<BenchMetric> &metrics) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    BenchReportWriter writer(out);
    writer.write(kind, metrics);
    out.flush();
    return static_cast<bool>(out);
}

} // namespace ahfl::bench
