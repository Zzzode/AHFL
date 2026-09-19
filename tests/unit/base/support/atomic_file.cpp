#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "base/support/atomic_file.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

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
