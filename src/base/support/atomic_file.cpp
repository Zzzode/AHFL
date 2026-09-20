#include "base/support/atomic_file.hpp"

#include <cerrno>
#include <fstream>
#include <system_error>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace ahfl::support {

namespace {

#if defined(__unix__) || defined(__APPLE__)
// fsync retrying on EINTR; any other error is a genuine sync failure.
[[nodiscard]] bool fsync_checked(int fd) noexcept {
    while (::fsync(fd) != 0) {
        if (errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

// fsync a directory so a rename() into it is durable. Opening a directory
// O_RDONLY is legal on POSIX (Linux ext4/xfs/btrfs); on a platform/filesystem
// where the open or the fsync is rejected this reports failure rather than
// silently claiming durability. NOTE: because this runs AFTER the rename, its
// failure means "published but the directory metadata may not be durable", not
// "nothing was written" — see the AtomicReplaceOptions::durable contract.
[[nodiscard]] bool fsync_directory(const std::filesystem::path &directory) {
    const int fd = ::open(directory.c_str(), O_RDONLY);
    if (fd < 0) {
        return false;
    }
    const bool ok = fsync_checked(fd);
    ::close(fd);
    return ok;
}
#endif

// Flush the temporary file's bytes to stable storage before the rename. On a
// non-POSIX platform there is no fsync here; that is reported as a failure so a
// caller that asked for durability is never told it got it.
[[nodiscard]] bool sync_file(const std::filesystem::path &path) {
#if defined(__unix__) || defined(__APPLE__)
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return false;
    }
    const bool ok = fsync_checked(fd);
    ::close(fd);
    return ok;
#else
    (void)path;
    return false;
#endif
}

} // namespace

std::filesystem::path atomic_temporary_path(const std::filesystem::path &destination) {
    return destination.parent_path() / (destination.filename().string() + ".tmp");
}

std::expected<void, AtomicReplaceError>
atomic_replace_text(const std::filesystem::path &destination,
                    std::string_view content,
                    const AtomicReplaceOptions &options) {
    if (destination.has_parent_path()) {
        std::error_code error;
        std::filesystem::create_directories(destination.parent_path(), error);
        if (error) {
            return std::unexpected(AtomicReplaceError::CreateDirectoryFailed);
        }
    }

    const auto temporary = atomic_temporary_path(destination);
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            return std::unexpected(AtomicReplaceError::OpenTemporaryFailed);
        }
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        output.flush();
        if (!output.good()) {
            return std::unexpected(AtomicReplaceError::WriteTemporaryFailed);
        }
    }

    // Durability gate: the bytes must be on disk before the rename publishes them,
    // otherwise a power loss can leave the renamed name pointing at unwritten data.
    if (options.durable && !sync_file(temporary)) {
        return std::unexpected(AtomicReplaceError::SyncFailed);
    }

    if (options.before_commit && !options.before_commit(temporary, destination)) {
        return std::unexpected(AtomicReplaceError::CommitInterrupted);
    }

    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        return std::unexpected(AtomicReplaceError::RenameFailed);
    }

    // The rename itself lands in the parent directory's metadata, which needs its
    // own fsync to be durable. Only meaningful with a parent path.
    if (options.durable && destination.has_parent_path() &&
        !fsync_directory(destination.parent_path())) {
        return std::unexpected(AtomicReplaceError::SyncFailed);
    }
    return {};
}

} // namespace ahfl::support
