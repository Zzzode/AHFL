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
    /// the cheaper path.
    ///
    /// A SyncFailed error is returned when either sync fails. The two syncs are NOT
    /// symmetric and a caller must not read SyncFailed as "nothing was written":
    /// a temp-file sync failure leaves the destination untouched (rename has not
    /// run), but a PARENT-DIRECTORY sync failure happens AFTER the rename has
    /// already published the new bytes — the destination holds the new content and
    /// the reported error is only about the directory metadata's durability
    /// (checkpoint() therefore fails closed with "failed to durably write", and an
    /// immediate retry observes the file and reports idempotent Created).
    ///
    /// Supported platforms for `durable == true`: POSIX (Linux, Darwin). The
    /// parent fsync uses `fsync(dirfd)`, which Linux ext4/xfs/btrfs honour as the
    /// rename barrier. On a filesystem where a directory fsync is rejected the
    /// call reports SyncFailed (it never silently claims durability); Darwin callers
    /// that need the hardware barrier for the FILE should note fsync is a weaker
    /// guarantee than fcntl(F_FULLFSYNC), which this helper does not issue.
    bool durable = false;
};

[[nodiscard]] std::filesystem::path atomic_temporary_path(const std::filesystem::path &destination);

[[nodiscard]] std::expected<void, AtomicReplaceError>
atomic_replace_text(const std::filesystem::path &destination,
                    std::string_view content,
                    const AtomicReplaceOptions &options = {});

} // namespace ahfl::support
