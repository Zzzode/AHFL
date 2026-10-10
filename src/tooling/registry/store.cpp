#include "tooling/registry/store.hpp"

#include "base/support/atomic_file.hpp"
#include "base/support/sha256.hpp"
#include "tooling/package/resolver.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace ahfl::registry {

namespace {

namespace fs = std::filesystem;

/// A path segment that can never escape the storage root: no separators, no
/// "."/".." traversal, no NUL. Package names and versions are additionally
/// grammar-validated by the registry codecs before reaching the store.
[[nodiscard]] bool is_safe_path_segment(std::string_view segment) {
    if (segment.empty() || segment == "." || segment == "..") {
        return false;
    }
    for (const char value : segment) {
        if (value == '/' || value == '\\' || value == '\0') {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<std::string> read_file(const fs::path &path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

[[nodiscard]] bool write_atomic(const fs::path &path, std::string_view content) {
    return ahfl::support::atomic_replace_text(path, content).has_value();
}

[[nodiscard]] std::string digest_text(std::string_view content) {
    return "sha256:" + ahfl::support::sha256_hex(content);
}

[[nodiscard]] bool same_dependencies(
    const std::vector<ahfl::package::RegistryDependencyMetadata> &lhs,
    const std::vector<ahfl::package::RegistryDependencyMetadata> &rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (lhs[i].name != rhs[i].name || lhs[i].source != rhs[i].source ||
            lhs[i].registry != rhs[i].registry ||
            lhs[i].version_requirement != rhs[i].version_requirement) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] fs::path version_directory(const fs::path &root,
                                         std::string_view package_name,
                                         std::string_view version) {
    return root / std::string(package_name) / std::string(version);
}

} // namespace

std::expected<ahfl::package::RegistryIndexEntry, PublishError> RegistryStore::publish(
    const ahfl::package::RegistryIndexEntry &entry,
    std::string source_archive_manifest,
    std::string source_archive_payload,
    std::string public_api_snapshot) {
    if (!is_safe_path_segment(entry.package) || !is_safe_path_segment(entry.version)) {
        return std::unexpected(PublishError::StorageFailure);
    }

    const auto dir = version_directory(root_, entry.package, entry.version);
    const auto index_path = dir / "registry-index.json";

    std::error_code ec;
    if (fs::exists(index_path, ec)) {
        // Immutability: an existing version is either an identical re-publish
        // (idempotent success, yanked flag preserved) or a conflict.
        auto existing = load(entry.package, entry.version);
        if (!existing.has_value()) {
            return std::unexpected(PublishError::Conflict);
        }
        const auto &stored = existing->entry;
        const bool identical =
            stored.source_archive_sha256 == entry.source_archive_sha256 &&
            stored.manifest_sha256 == entry.manifest_sha256 &&
            stored.public_api_sha256 == entry.public_api_sha256 &&
            same_dependencies(stored.dependencies, entry.dependencies) &&
            existing->source_archive_payload == source_archive_payload &&
            existing->public_api_snapshot == public_api_snapshot;
        if (!identical) {
            return std::unexpected(PublishError::Conflict);
        }
        return stored;
    }

    fs::create_directories(dir, ec);
    if (ec) {
        return std::unexpected(PublishError::StorageFailure);
    }

    // Write the blobs first; registry-index.json is the completeness marker,
    // so it is written last. A crash before that leaves a partial directory
    // that a later publish simply overwrites.
    if (!write_atomic(dir / "source-archive.json", source_archive_manifest) ||
        !write_atomic(dir / "source-archive.payload", source_archive_payload) ||
        !write_atomic(dir / "public-api.json", public_api_snapshot) ||
        !write_atomic(index_path,
                      ahfl::package::serialize_registry_index_entry(entry))) {
        return std::unexpected(PublishError::StorageFailure);
    }
    return entry;
}

std::expected<StoredVersion, LoadError> RegistryStore::load(std::string_view package_name,
                                                            std::string_view version) const {
    if (!is_safe_path_segment(package_name) || !is_safe_path_segment(version)) {
        return std::unexpected(LoadError::NotFound);
    }

    const auto dir = version_directory(root_, package_name, version);

    const auto index_text = read_file(dir / "registry-index.json");
    if (!index_text.has_value()) {
        return std::unexpected(LoadError::NotFound);
    }
    auto parsed_index = ahfl::package::parse_registry_index_entry(*index_text);
    if (parsed_index.has_errors() || !parsed_index.entry.has_value()) {
        return std::unexpected(LoadError::Corrupt);
    }

    const auto manifest_text = read_file(dir / "source-archive.json");
    const auto payload_text = read_file(dir / "source-archive.payload");
    const auto snapshot_text = read_file(dir / "public-api.json");
    if (!manifest_text.has_value() || !payload_text.has_value() || !snapshot_text.has_value()) {
        return std::unexpected(LoadError::Corrupt);
    }

    auto parsed_manifest = ahfl::package::parse_source_archive_manifest(*manifest_text);
    if (parsed_manifest.has_errors() || !parsed_manifest.archive.has_value()) {
        return std::unexpected(LoadError::Corrupt);
    }
    const auto &archive = *parsed_manifest.archive;

    StoredVersion stored;
    stored.entry = std::move(*parsed_index.entry);
    if (archive.archive_sha256 != stored.entry.source_archive_sha256 ||
        archive.manifest_sha256 != stored.entry.manifest_sha256 ||
        digest_text(*payload_text) != stored.entry.source_archive_sha256 ||
        digest_text(*snapshot_text) != stored.entry.public_api_sha256) {
        return std::unexpected(LoadError::Corrupt);
    }
    stored.source_archive_manifest = std::move(*manifest_text);
    stored.source_archive_payload = std::move(*payload_text);
    stored.public_api_snapshot = std::move(*snapshot_text);
    return stored;
}

std::optional<std::vector<ahfl::package::RegistryIndexEntry>> RegistryStore::list_versions(
    std::string_view package_name) const {
    if (!is_safe_path_segment(package_name)) {
        return std::nullopt;
    }

    const auto package_dir = root_ / std::string(package_name);
    std::error_code ec;
    if (!fs::is_directory(package_dir, ec)) {
        return std::nullopt;
    }

    std::vector<ahfl::package::RegistryIndexEntry> entries;
    for (const auto &version_dir : fs::directory_iterator(package_dir, ec)) {
        if (!version_dir.is_directory()) {
            continue;
        }
        const auto index_text = read_file(version_dir.path() / "registry-index.json");
        if (!index_text.has_value()) {
            continue; // incomplete publish (no completeness marker yet)
        }
        auto parsed = ahfl::package::parse_registry_index_entry(*index_text);
        if (parsed.has_errors() || !parsed.entry.has_value()) {
            continue;
        }
        entries.push_back(std::move(*parsed.entry));
    }

    std::sort(entries.begin(), entries.end(), [](const auto &lhs, const auto &rhs) {
        return ahfl::package::Resolver::compare(
                   ahfl::package::Resolver::parse_version(lhs.version),
                   ahfl::package::Resolver::parse_version(rhs.version)) < 0;
    });
    return entries;
}

std::optional<ahfl::package::RegistryIndexEntry> RegistryStore::yank(
    std::string_view package_name, std::string_view version) {
    if (!is_safe_path_segment(package_name) || !is_safe_path_segment(version)) {
        return std::nullopt;
    }

    const auto index_path =
        version_directory(root_, package_name, version) / "registry-index.json";
    const auto index_text = read_file(index_path);
    if (!index_text.has_value()) {
        return std::nullopt;
    }
    auto parsed = ahfl::package::parse_registry_index_entry(*index_text);
    if (parsed.has_errors() || !parsed.entry.has_value()) {
        return std::nullopt;
    }

    parsed.entry->yanked = true;
    if (!write_atomic(index_path,
                      ahfl::package::serialize_registry_index_entry(*parsed.entry))) {
        return std::nullopt;
    }
    return std::move(*parsed.entry);
}

} // namespace ahfl::registry
