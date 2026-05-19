#include "ahfl/package/registry.hpp"

#include <algorithm>

namespace ahfl::package {

namespace {

// Hardcoded package database for offline/stub mode
struct StubPackage {
    const char* name;
    const char* version;
    const char* description;
};

constexpr StubPackage stub_packages[] = {
    {"ahfl-stdlib", "1.0.0", "AHFL standard library"},
    {"ahfl-stdlib", "1.1.0", "AHFL standard library"},
    {"ahfl-stdlib", "2.0.0", "AHFL standard library v2"},
    {"ahfl-http", "0.5.0", "HTTP capabilities for AHFL agents"},
    {"ahfl-http", "0.6.0", "HTTP capabilities for AHFL agents"},
    {"ahfl-testing", "1.0.0", "Testing utilities for AHFL workflows"},
    {"ahfl-llm", "0.1.0", "LLM provider bindings"},
    {"ahfl-llm", "0.2.0", "LLM provider bindings"},
};

} // namespace

Registry::Registry(std::string base_url) : base_url_(std::move(base_url)) {}

RegistryResult Registry::fetch_metadata(const std::string& package_name) const {
    PackageMetadata metadata;
    metadata.name = package_name;

    bool found = false;
    for (const auto& pkg : stub_packages) {
        if (pkg.name == package_name) {
            found = true;
            PackageVersion pv;
            pv.name = pkg.name;
            pv.version = pkg.version;
            pv.description = pkg.description;
            pv.authors = {"AHFL Team"};
            pv.checksum = "sha256:stub";
            metadata.versions.push_back(std::move(pv));
        }
    }

    if (!found) {
        return RegistryResult{
            .success = false,
            .metadata = std::nullopt,
            .error = RegistryError::NotFound,
            .error_message = "Package not found: " + package_name,
        };
    }

    if (!metadata.versions.empty()) {
        metadata.latest_version = metadata.versions.back().version;
    }

    return RegistryResult{
        .success = true,
        .metadata = std::move(metadata),
        .error = std::nullopt,
        .error_message = {},
    };
}

RegistryResult Registry::fetch_version(const std::string& package_name,
                                       const std::string& version) const {
    for (const auto& pkg : stub_packages) {
        if (pkg.name == package_name && pkg.version == version) {
            PackageMetadata metadata;
            metadata.name = package_name;
            metadata.latest_version = version;

            PackageVersion pv;
            pv.name = pkg.name;
            pv.version = pkg.version;
            pv.description = pkg.description;
            pv.authors = {"AHFL Team"};
            pv.checksum = "sha256:stub";
            metadata.versions.push_back(std::move(pv));

            return RegistryResult{
                .success = true,
                .metadata = std::move(metadata),
                .error = std::nullopt,
                .error_message = {},
            };
        }
    }

    return RegistryResult{
        .success = false,
        .metadata = std::nullopt,
        .error = RegistryError::NotFound,
        .error_message = "Version not found: " + package_name + "@" + version,
    };
}

std::vector<std::string> Registry::search(const std::string& query) const {
    std::vector<std::string> results;

    for (const auto& pkg : stub_packages) {
        std::string name(pkg.name);
        if (name.find(query) != std::string::npos) {
            if (std::find(results.begin(), results.end(), name) == results.end()) {
                results.push_back(name);
            }
        }
    }

    return results;
}

const std::string& Registry::base_url() const { return base_url_; }

} // namespace ahfl::package
