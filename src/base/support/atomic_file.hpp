#pragma once

#include <expected>
#include <filesystem>
#include <functional>
#include <string_view>

namespace ahfl::support {

enum class AtomicReplaceError {
    CreateDirectoryFailed,
    OpenTemporaryFailed,
    WriteTemporaryFailed,
    SyncFailed,
    CommitInterrupted,
    RenameFailed,
};

struct AtomicReplaceOptions {
    std::function<bool(const std::filesystem::path &, const std::filesystem::path &)> before_commit;
    /// When true, the temporary file is fsync'd before the rename and the parent
    /// directory is fsync'd after it, so the replacement survives a power loss
    /// (the durability gate missing from a plain write() + rename()). Off by
    /// default: callers that only need atomic-visibility (a crash-safe swap) keep
    /// the cheaper path; a durability-critical caller opts in. A SyncFailed error
    /// is returned when either sync fails, leaving the destination untouched
    /// (rename has not run) or already committed (parent fsync after rename).
    bool durable = false;
};

[[nodiscard]] std::filesystem::path atomic_temporary_path(const std::filesystem::path &destination);

[[nodiscard]] std::expected<void, AtomicReplaceError>
atomic_replace_text(const std::filesystem::path &destination,
                    std::string_view content,
                    const AtomicReplaceOptions &options = {});

} // namespace ahfl::support
