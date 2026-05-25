#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

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
    };

    [[nodiscard]] RunResult run(ir::Program &program);

    /// Access analysis results after run().
    [[nodiscard]] const std::vector<std::unique_ptr<AnalysisResult>> &analysis_results() const {
        return analysis_results_;
    }

  private:
    std::vector<std::unique_ptr<Pass>> passes_;
    std::vector<std::unique_ptr<AnalysisPass>> analyses_;
    std::vector<std::unique_ptr<AnalysisResult>> analysis_results_;
};

/// Convenience: create a PassManager with all default optimization passes registered.
[[nodiscard]] std::unique_ptr<PassManager> create_default_pipeline();

} // namespace ahfl::passes
