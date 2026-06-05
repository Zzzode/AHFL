#pragma once

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

enum class RegistryError {
    NotFound,
    NetworkError,
    InvalidResponse,
    Unauthorized
};

struct RegistryResult {
    bool success;
    std::optional<PackageMetadata> metadata;
    std::optional<RegistryError> error;
    std::string error_message;
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
    [[nodiscard]] std::vector<std::string> search(const std::string &query) const;
    [[nodiscard]] const std::string &base_url() const;

  private:
    std::string base_url_;
    std::shared_ptr<RegistryTransport> transport_;
    std::shared_ptr<RegistryCache> cache_;
};

} // namespace ahfl::package
