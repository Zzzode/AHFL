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
    CommitInterrupted,
    RenameFailed,
};

struct AtomicReplaceOptions {
    std::function<bool(const std::filesystem::path &, const std::filesystem::path &)>
        before_commit;
};

[[nodiscard]] std::filesystem::path
atomic_temporary_path(const std::filesystem::path &destination);

[[nodiscard]] std::expected<void, AtomicReplaceError>
atomic_replace_text(const std::filesystem::path &destination,
                    std::string_view content,
                    const AtomicReplaceOptions &options = {});

} // namespace ahfl::support
