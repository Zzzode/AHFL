#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "base/support/atomic_file.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

[[nodiscard]] std::string read_text(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream content;
    content << input.rdbuf();
    return content.str();
}

} // namespace

TEST_CASE("atomic text replacement preserves committed data across pre-commit failure") {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory =
        std::filesystem::temp_directory_path() / ("ahfl-atomic-file-" + std::to_string(suffix));
    const auto target = directory / "snapshot.json";
    std::filesystem::create_directories(directory);

    REQUIRE(ahfl::support::atomic_replace_text(target, "old-snapshot").has_value());
    CHECK(read_text(target) == "old-snapshot");

    const auto failed = ahfl::support::atomic_replace_text(
        target,
        "partial-new-snapshot",
        ahfl::support::AtomicReplaceOptions{
            .before_commit =
                [](const std::filesystem::path &temporary,
                   const std::filesystem::path &destination) {
                    CHECK(std::filesystem::exists(temporary));
                    CHECK(std::filesystem::exists(destination));
                    CHECK(read_text(temporary) == "partial-new-snapshot");
                    return false;
                },
        });
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error() == ahfl::support::AtomicReplaceError::CommitInterrupted);
    CHECK(read_text(target) == "old-snapshot");
    CHECK(std::filesystem::exists(ahfl::support::atomic_temporary_path(target)));

    REQUIRE(ahfl::support::atomic_replace_text(target, "committed-new-snapshot").has_value());
    CHECK(read_text(target) == "committed-new-snapshot");
    CHECK_FALSE(std::filesystem::exists(ahfl::support::atomic_temporary_path(target)));

    std::filesystem::remove_all(directory);
}

TEST_CASE("durable atomic replace fsyncs before commit and keeps the destination whole") {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
                           ("ahfl-atomic-file-durable-" + std::to_string(suffix));
    const auto target = directory / "snapshot.json";
    std::filesystem::create_directories(directory);

    ahfl::support::AtomicReplaceOptions durable_options;
    durable_options.durable = true;

    REQUIRE(ahfl::support::atomic_replace_text(target, "first", durable_options).has_value());
    CHECK(read_text(target) == "first");
    CHECK_FALSE(std::filesystem::exists(ahfl::support::atomic_temporary_path(target)));

    REQUIRE(ahfl::support::atomic_replace_text(target, "second", durable_options).has_value());
    CHECK(read_text(target) == "second");

    // The durability flag must not weaken the commit gate: an aborted before_commit
    // still leaves the previous bytes in place.
    const auto interrupted = ahfl::support::atomic_replace_text(
        target,
        "never",
        ahfl::support::AtomicReplaceOptions{
            .before_commit = [](const std::filesystem::path &,
                                const std::filesystem::path &) { return false; },
            .durable = true,
        });
    REQUIRE_FALSE(interrupted.has_value());
    CHECK(interrupted.error() == ahfl::support::AtomicReplaceError::CommitInterrupted);
    CHECK(read_text(target) == "second");

    std::filesystem::remove_all(directory);
}

// Pin the parent-directory-sync failure path: its failure comes AFTER the rename,
// so the destination holds the new bytes while the call reports SyncFailed. This
// is the documented asymmetry a caller must not misread as "nothing was written"
// (see AtomicReplaceOptions::durable). The trigger is a parent directory that
// denies open() but still allows the rename — mode 0300 (write+execute, no read)
// on Linux. If the platform lets the directory open, the case cannot be driven and
// it is skipped rather than asserting a platform-specific error.
TEST_CASE("durable atomic replace reports a post-rename directory sync failure") {
#if defined(__unix__) || defined(__APPLE__)
    if (::geteuid() == 0) {
        return; // root bypasses the mode bits
    }
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
                           ("ahfl-atomic-file-dirsync-" + std::to_string(suffix));
    const auto target = directory / "snapshot.json";
    std::filesystem::create_directories(directory);
    // A prior committed file, then lock the directory against open().
    REQUIRE(ahfl::support::atomic_replace_text(target, "old").has_value());
    std::filesystem::permissions(directory,
                                 std::filesystem::perms::owner_write |
                                     std::filesystem::perms::owner_exec,
                                 std::filesystem::perm_options::replace);

    ahfl::support::AtomicReplaceOptions durable_options;
    durable_options.durable = true;
    const auto result = ahfl::support::atomic_replace_text(target, "new", durable_options);

    // Restore access so cleanup can run regardless of the assertion outcome.
    std::filesystem::permissions(directory, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace);

    if (::open(directory.c_str(), O_RDONLY) >= 3) {
        // The platform opened the locked directory, so the failure path was not
        // driven here; do not assert a platform-specific status.
        std::filesystem::remove_all(directory);
        return;
    }
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == ahfl::support::AtomicReplaceError::SyncFailed);
    // The property that makes SyncFailed ambiguous: the rename already published
    // the new bytes, so a retry observes the new content, not the old.
    CHECK(read_text(target) == "new");

    std::filesystem::remove_all(directory);
#endif
}
