#pragma once

#include <expected>
#include <iosfwd>
#include <string>

namespace ahfl::runtime {

struct WorkflowResult;

enum class ExecutionOutputFormat {
    Human,
    Json,
    JsonLines,
    Quiet,
};

enum class ExecutionVerbosity {
    Normal,
    Verbose,
    Trace,
};

struct ExecutionOutputOptions {
    ExecutionOutputFormat format{ExecutionOutputFormat::Human};
    ExecutionVerbosity verbosity{ExecutionVerbosity::Normal};
    bool use_color{false};
    bool interactive_terminal{false};
};

using ExecutionRenderResult = std::expected<void, std::string>;

[[nodiscard]] ExecutionRenderResult render_execution_result(const WorkflowResult &result,
                                                            const ExecutionOutputOptions &options,
                                                            std::ostream &out);

} // namespace ahfl::runtime
