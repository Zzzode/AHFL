#pragma once

namespace ahfl::cli {

enum class ExitCode : int {
    Success = 0,
    CompileError = 1,
    UsageError = 2,
    InternalError = 3,
};

} // namespace ahfl::cli
