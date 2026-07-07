#pragma once

#include "tooling/package/source_archive.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ahfl::package {

struct PackageVersion {
    std::string name;
    std::string version;
    std::string description;
    std::vector<std::string> authors;
    std::string checksum;
};

struct PackageMetadata {
    std::string name;
    std::vector<PackageVersion> versions;
    std::string latest_version;
};

struct RegistryDependencyMetadata {
    std::string name;
    std::string source;
    std::optional<std::string> registry;
    std::optional<std::string> version_requirement;
};

struct RegistryIndexEntry {
    std::string format_version{"ahfl.registry.index.v1"};
    std::string registry_id;
    std::string package;
    std::string version;
    bool yanked{false};
    std::string source_archive_sha256;
    std::string manifest_sha256;
    std::string public_api_sha256;
    std::vector<RegistryDependencyMetadata> dependencies;
};

struct RegistryIndexParseResult {
    std::optional<RegistryIndexEntry> entry;
    std::vector<std::string> diagnostics;

    [[nodiscard]] bool has_errors() const {
        return !diagnostics.empty();
    }
};

struct RegistryCandidateSelection {
    std::optional<RegistryIndexEntry> entry;
    std::string error_message;
};

struct RegistryPackageArtifacts {
    RegistryIndexEntry registry;
    SourceArchive archive;
    std::string payload;
};

struct RegistryPublicApiSnapshot {
    std::string snapshot;
    std::string public_api_sha256;
};

struct RegistryPublishPackageRequest {
    RegistryIndexEntry registry;
    SourceArchive archive;
    std::string public_api_snapshot;
};

struct RegistryPackageIndex {
    std::string format_version{"ahfl.registry.package_index.v1"};
    std::string package;
    std::vector<RegistryIndexEntry> versions;
};

enum class RegistryError {
    NotFound,
    NetworkError,
    InvalidResponse,
    Unauthorized,
    Conflict
};

struct RegistryResult {
    bool success;
    std::optional<PackageMetadata> metadata;
    std::optional<RegistryError> error;
    std::string error_message;
};

struct RegistryPackageArtifactResult {
    std::optional<RegistryPackageArtifacts> artifacts;
    std::optional<RegistryError> error;
    std::vector<std::string> diagnostics;

    [[nodiscard]] bool success() const {
        return artifacts.has_value() && !error.has_value() && diagnostics.empty();
    }
};

struct RegistryPackageIndexResult {
    std::optional<RegistryPackageIndex> index;
    std::optional<RegistryError> error;
    std::vector<std::string> diagnostics;

    [[nodiscard]] bool success() const {
        return index.has_value() && !error.has_value() && diagnostics.empty();
    }
};

struct RegistryPublicApiSnapshotResult {
    std::optional<RegistryPublicApiSnapshot> snapshot;
    std::optional<RegistryError> error;
    std::vector<std::string> diagnostics;

    [[nodiscard]] bool success() const {
        return snapshot.has_value() && !error.has_value() && diagnostics.empty();
    }
};

struct RegistryMutationResult {
    std::optional<RegistryIndexEntry> entry;
    std::optional<RegistryError> error;
    std::vector<std::string> diagnostics;

    [[nodiscard]] bool success() const {
        return entry.has_value() && !error.has_value() && diagnostics.empty();
    }
};

struct RegistryHttpResponse {
    int status_code{0};
    std::string body;
    bool success{false};
};

class RegistryTransport {
  public:
    virtual ~RegistryTransport() = default;

    [[nodiscard]] virtual RegistryHttpResponse get(std::string_view url, int timeout_seconds) = 0;
    [[nodiscard]] virtual RegistryHttpResponse request(std::string_view method,
                                                       std::string_view url,
                                                       std::string_view body,
                                                       int timeout_seconds) {
        if (method == "GET" && body.empty()) {
            return get(url, timeout_seconds);
        }
        return RegistryHttpResponse{};
    }
};

class RegistryCache {
  public:
    virtual ~RegistryCache() = default;

    virtual void save(std::string_view package_name, std::string_view json_data) = 0;
    [[nodiscard]] virtual std::optional<std::string> load(std::string_view package_name) = 0;
};

class Registry {
  public:
    explicit Registry(std::string base_url = "https://registry.ahfl.io/v1");
    Registry(std::string base_url,
             std::shared_ptr<RegistryTransport> transport,
             std::shared_ptr<RegistryCache> cache);

    [[nodiscard]] RegistryResult fetch_metadata(const std::string &package_name) const;
    [[nodiscard]] RegistryResult fetch_version(const std::string &package_name,
                                               const std::string &version) const;
    [[nodiscard]] RegistryPackageIndexResult
    fetch_package_index(const std::string &package_name) const;
    [[nodiscard]] RegistryPackageArtifactResult
    fetch_package_artifacts(const std::string &package_name, const std::string &version) const;
    [[nodiscard]] RegistryPublicApiSnapshotResult
    fetch_public_api_snapshot(const std::string &package_name,
                              const std::string &version,
                              const std::string &public_api_sha256) const;
    [[nodiscard]] RegistryMutationResult
    publish_package(const RegistryPublishPackageRequest &request) const;
    [[nodiscard]] RegistryMutationResult yank_package(const std::string &package_name,
                                                      const std::string &version,
                                                      const std::string &registry_id,
                                                      const std::string &reason) const;
    [[nodiscard]] std::vector<std::string> search(const std::string &query) const;
    [[nodiscard]] const std::string &base_url() const;

  private:
    std::string base_url_;
    std::shared_ptr<RegistryTransport> transport_;
    std::shared_ptr<RegistryCache> cache_;
};

[[nodiscard]] RegistryIndexParseResult parse_registry_index_entry(std::string_view input);
[[nodiscard]] std::string serialize_registry_index_entry(const RegistryIndexEntry &entry);
[[nodiscard]] RegistryCandidateSelection
select_registry_candidate(std::string_view package_name,
                          const std::vector<RegistryIndexEntry> &candidates,
                          std::string_view version_requirement,
                          std::optional<std::string_view> locked_version = std::nullopt);

} // namespace ahfl::package
