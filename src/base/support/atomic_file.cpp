#include "base/support/atomic_file.hpp"

#include <fstream>
#include <system_error>

namespace ahfl::support {

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

    if (options.before_commit && !options.before_commit(temporary, destination)) {
        return std::unexpected(AtomicReplaceError::CommitInterrupted);
    }

    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        return std::unexpected(AtomicReplaceError::RenameFailed);
    }
    return {};
}

} // namespace ahfl::support
