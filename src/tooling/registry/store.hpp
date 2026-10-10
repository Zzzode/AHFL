#pragma once

#include "tooling/package/registry.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ahfl::registry {

/// Why a stored version could not be loaded.
enum class LoadError {
    /// The (package, version) was never published.
    NotFound,
    /// Files exist but fail SHA-256 verification (storage corruption).
    Corrupt,
};

/// The four immutable blobs of a published version, loaded and verified.
struct StoredVersion {
    ahfl::package::RegistryIndexEntry entry;
    std::string source_archive_manifest;
    std::string source_archive_payload;
    std::string public_api_snapshot;
};

/// Why a publish was rejected.
enum class PublishError {
    /// The (package, version) already exists with different immutable content.
    Conflict,
    /// Filesystem write failed.
    StorageFailure,
};

/// Filesystem-backed content-addressed registry store.
///
/// On-disk layout:
///   {root}/{package}/{version}/registry-index.json
///   {root}/{package}/{version}/source-archive.json
///   {root}/{package}/{version}/source-archive.payload
///   {root}/{package}/{version}/public-api.json
///
/// Every write is atomic (temp-file + rename). A version is immutable once
/// published: re-publishing identical content is an idempotent success,
/// different content is a Conflict. Only the yanked flag mutates (via yank()).
class RegistryStore {
  public:
    explicit RegistryStore(std::filesystem::path root) : root_(std::move(root)) {}

    /// Publishes a version. On success returns the stored index entry.
    /// Identical re-publish is idempotent (the stored entry, including its
    /// yanked flag, is returned unchanged); a re-publish with different
    /// immutable content returns Conflict.
    [[nodiscard]] std::expected<ahfl::package::RegistryIndexEntry, PublishError> publish(
        const ahfl::package::RegistryIndexEntry &entry,
        std::string source_archive_manifest,
        std::string source_archive_payload,
        std::string public_api_snapshot);

    /// Loads a version and verifies every stored blob against the digests in
    /// its registry index entry.
    [[nodiscard]] std::expected<StoredVersion, LoadError> load(std::string_view package_name,
                                                               std::string_view version) const;

    /// All published versions of a package, sorted by semantic version
    /// (oldest first). Returns nullopt if the package has no versions.
    [[nodiscard]] std::optional<std::vector<ahfl::package::RegistryIndexEntry>> list_versions(
        std::string_view package_name) const;

    /// Sets yanked=true on the stored entry and persists it. Returns the
    /// updated entry, or nullopt if the version is not published.
    [[nodiscard]] std::optional<ahfl::package::RegistryIndexEntry> yank(
        std::string_view package_name, std::string_view version);

  private:
    std::filesystem::path root_;
};

} // namespace ahfl::registry
