#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ahfl/compiler/ir/analysis.hpp"

namespace ahfl::ir {
struct Program;
}

namespace ahfl::passes {

/// Result base class for analysis passes.
struct AnalysisResult {
    virtual ~AnalysisResult() = default;
};

/// A transformation pass that may modify the IR.
class Pass {
  public:
    virtual ~Pass() = default;
    [[nodiscard]] virtual std::string_view name() const = 0;
    /// Returns true if the IR was modified.
    [[nodiscard]] virtual bool run(ir::Program &program) = 0;

    /// Declare which analyses this pass requires (run before if missing).
    [[nodiscard]] virtual std::vector<std::string_view> required_analyses() const {
        return {};
    }
    /// Declare which IR derived analyses must be fresh before this pass runs.
    [[nodiscard]] virtual std::vector<ir::DerivedAnalysisKind> required_derived_analyses() const {
        return {};
    }
    /// Declare which analyses this pass invalidates (cleared after run).
    [[nodiscard]] virtual std::vector<std::string_view> invalidated_analyses() const {
        return {};
    }
    /// Declare which IR derived analyses this pass invalidates if it modifies the program.
    [[nodiscard]] virtual std::vector<ir::DerivedAnalysisKind>
    invalidated_derived_analyses() const {
        return ir::all_derived_analysis_kinds_vector();
    }
};

/// An analysis pass that inspects but does not modify the IR.
class AnalysisPass {
  public:
    virtual ~AnalysisPass() = default;
    [[nodiscard]] virtual std::string_view name() const = 0;
    [[nodiscard]] virtual std::unique_ptr<AnalysisResult> run(const ir::Program &program) = 0;
};

/// Manages and runs a pipeline of transformation and analysis passes.
class PassManager {
  public:
    void add_pass(std::unique_ptr<Pass> pass);
    void add_analysis(std::unique_ptr<AnalysisPass> analysis);

    struct RunResult {
        bool any_modified{false};
        std::vector<std::string> executed;
        std::vector<std::pair<std::string_view, double>> timings_ms;
    };

    [[nodiscard]] RunResult run(ir::Program &program);

    /// Run passes repeatedly until no modifications occur or max_iterations reached.
    [[nodiscard]] RunResult run_to_fixpoint(ir::Program &program, std::size_t max_iterations = 10);

    /// Access analysis results after run().
    [[nodiscard]] const std::vector<std::unique_ptr<AnalysisResult>> &analysis_results() const {
        return analysis_results_;
    }

    /// Set a filter: only passes whose name() is in the set will execute.
    /// An empty filter means all passes run (default behavior).
    void set_pass_filter(std::vector<std::string_view> pass_names);

    /// Query registered pass/analysis names.
    [[nodiscard]] std::vector<std::string_view> registered_pass_names() const;
    [[nodiscard]] std::vector<std::string_view> registered_analysis_names() const;

  private:
    std::vector<std::unique_ptr<Pass>> passes_;
    std::vector<std::unique_ptr<AnalysisPass>> analyses_;
    std::vector<std::unique_ptr<AnalysisResult>> analysis_results_;
    std::unordered_set<std::string_view> pass_filter_;
    std::unordered_map<std::string_view, std::size_t> analysis_index_;

    void ensure_analysis(std::string_view name, const ir::Program &program);
    void invalidate_analysis(std::string_view name);
};

/// Convenience: create a PassManager with all default optimization passes registered.
[[nodiscard]] std::unique_ptr<PassManager> create_default_pipeline();

} // namespace ahfl::passes
