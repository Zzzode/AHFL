#pragma once

#include <optional>
#include <string>
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

class Registry {
public:
    explicit Registry(std::string base_url = "https://registry.ahfl.io/v1");

    [[nodiscard]] RegistryResult fetch_metadata(const std::string& package_name) const;
    [[nodiscard]] RegistryResult fetch_version(const std::string& package_name,
                                               const std::string& version) const;
    [[nodiscard]] std::vector<std::string> search(const std::string& query) const;
    [[nodiscard]] const std::string& base_url() const;

private:
    std::string base_url_;
};

} // namespace ahfl::package
