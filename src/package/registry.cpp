#include "ahfl/package/registry.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace ahfl::package {

namespace {

namespace fs = std::filesystem;

fs::path cache_directory() {
    const char *env = std::getenv("AHFL_PACKAGE_CACHE");
    if (env != nullptr && *env != '\0')
        return fs::path(env);
    const char *home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        return fs::path(home) / ".ahfl" / "packages";
    }
    return fs::path("/tmp") / ".ahfl" / "packages";
}

struct HttpResult {
    int status_code = 0;
    std::string body;
    bool success = false;
};

HttpResult http_get(const std::string &url, int timeout_seconds = 10) {
    HttpResult result;
    // Shell-escape URL (basic protection)
    std::string safe_url;
    safe_url.reserve(url.size());
    for (char c : url) {
        if (c == '\'') {
            safe_url += "'\\''";
        } else {
            safe_url += c;
        }
    }

    std::string cmd = "curl -s -w '\\n%{http_code}' --max-time " + std::to_string(timeout_seconds) +
                      " '" + safe_url + "' 2>/dev/null";

    FILE *pipe = popen(cmd.c_str(), "r");
    if (pipe == nullptr)
        return result;

    std::string output;
    std::array<char, 4096> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }
    pclose(pipe);

    if (output.empty())
        return result;

    // Extract status code from last line
    auto last_newline = output.rfind('\n', output.size() - 2);
    if (last_newline == std::string::npos) {
        last_newline = 0;
    } else {
        ++last_newline;
    }

    std::string status_str = output.substr(last_newline);
    // Trim trailing whitespace
    while (!status_str.empty() && (status_str.back() == '\n' || status_str.back() == '\r')) {
        status_str.pop_back();
    }

    try {
        result.status_code = std::stoi(status_str);
    } catch (...) {
        return result;
    }

    result.body = output.substr(0, last_newline > 0 ? last_newline - 1 : 0);
    result.success = (result.status_code >= 200 && result.status_code < 300);
    return result;
}

// Minimal JSON field extraction (handles simple {"key":"value"} patterns)
std::string extract_json_string(const std::string &json, const std::string &key) {
    auto search = "\"" + key + "\":\"";
    auto pos = json.find(search);
    if (pos == std::string::npos)
        return "";
    pos += search.size();
    auto end = json.find('"', pos);
    if (end == std::string::npos)
        return "";
    return json.substr(pos, end - pos);
}

fs::path cache_file_path(const std::string &package_name) {
    auto dir = cache_directory();
    return dir / (package_name + ".json");
}

void save_to_cache(const std::string &package_name, const std::string &json_data) {
    auto dir = cache_directory();
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec)
        return;

    std::ofstream ofs(cache_file_path(package_name));
    if (ofs) {
        ofs << json_data;
    }
}

std::string load_from_cache(const std::string &package_name) {
    auto path = cache_file_path(package_name);
    std::ifstream ifs(path);
    if (!ifs)
        return "";
    return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

PackageMetadata parse_metadata_json(const std::string &json, const std::string &package_name) {
    PackageMetadata metadata;
    metadata.name = package_name;
    metadata.latest_version = extract_json_string(json, "latest_version");

    // Parse versions array (simplified: look for version patterns)
    std::string::size_type pos = 0;
    while ((pos = json.find("\"version\":\"", pos)) != std::string::npos) {
        pos += 11; // skip "version":"
        auto end = json.find('"', pos);
        if (end == std::string::npos)
            break;
        std::string version = json.substr(pos, end - pos);

        PackageVersion pv;
        pv.name = package_name;
        pv.version = version;
        pv.description = extract_json_string(json, "description");
        pv.checksum = extract_json_string(json, "checksum");
        metadata.versions.push_back(std::move(pv));
        pos = end + 1;
    }

    if (metadata.latest_version.empty() && !metadata.versions.empty()) {
        metadata.latest_version = metadata.versions.back().version;
    }

    return metadata;
}

// Hardcoded fallback packages for offline mode
struct StubPackage {
    const char *name;
    const char *version;
    const char *description;
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

RegistryResult fallback_fetch(const std::string &package_name) {
    PackageMetadata metadata;
    metadata.name = package_name;
    for (const auto &pkg : stub_packages) {
        if (pkg.name == package_name) {
            PackageVersion ver;
            ver.name = pkg.name;
            ver.version = pkg.version;
            ver.description = pkg.description;
            ver.checksum = "sha256:offline";
            metadata.versions.push_back(std::move(ver));
        }
    }
    if (metadata.versions.empty()) {
        return RegistryResult{
            false, std::nullopt, RegistryError::NotFound, "package not found: " + package_name};
    }
    metadata.latest_version = metadata.versions.back().version;
    return RegistryResult{true, std::move(metadata), std::nullopt, ""};
}

} // namespace

Registry::Registry(std::string base_url) : base_url_(std::move(base_url)) {}

RegistryResult Registry::fetch_metadata(const std::string &package_name) const {
    // 1. Try HTTP fetch from registry
    auto url = base_url_ + "/packages/" + package_name;
    auto http_result = http_get(url);

    if (http_result.success && !http_result.body.empty()) {
        save_to_cache(package_name, http_result.body);
        auto metadata = parse_metadata_json(http_result.body, package_name);
        if (!metadata.versions.empty()) {
            return RegistryResult{true, std::move(metadata), std::nullopt, ""};
        }
    }

    // 2. Fallback to local cache
    auto cached_json = load_from_cache(package_name);
    if (!cached_json.empty()) {
        auto metadata = parse_metadata_json(cached_json, package_name);
        if (!metadata.versions.empty()) {
            return RegistryResult{true, std::move(metadata), std::nullopt, ""};
        }
    }

    // 3. Fallback to hardcoded stub (offline compatibility)
    return fallback_fetch(package_name);
}

RegistryResult Registry::fetch_version(const std::string &package_name,
                                       const std::string &version) const {
    // 1. Try HTTP fetch
    auto url = base_url_ + "/packages/" + package_name + "/versions/" + version;
    auto http_result = http_get(url);

    if (http_result.success && !http_result.body.empty()) {
        PackageMetadata metadata;
        metadata.name = package_name;
        metadata.latest_version = version;

        PackageVersion ver;
        ver.name = package_name;
        ver.version = version;
        ver.description = extract_json_string(http_result.body, "description");
        ver.checksum = extract_json_string(http_result.body, "checksum");
        if (ver.checksum.empty())
            ver.checksum = "sha256:" + extract_json_string(http_result.body, "sha256");
        metadata.versions.push_back(std::move(ver));
        return RegistryResult{true, std::move(metadata), std::nullopt, ""};
    }

    // 2. Fallback: try full metadata fetch and filter
    auto full = fetch_metadata(package_name);
    if (full.success && full.metadata.has_value()) {
        for (auto &v : full.metadata->versions) {
            if (v.version == version) {
                PackageMetadata single;
                single.name = package_name;
                single.latest_version = version;
                single.versions.push_back(std::move(v));
                return RegistryResult{true, std::move(single), std::nullopt, ""};
            }
        }
    }

    return RegistryResult{false,
                          std::nullopt,
                          RegistryError::NotFound,
                          "version " + version + " not found for " + package_name};
}

std::vector<std::string> Registry::search(const std::string &query) const {
    std::vector<std::string> results;

    // 1. Try HTTP search
    auto url = base_url_ + "/search?q=" + query;
    auto http_result = http_get(url);

    if (http_result.success && !http_result.body.empty()) {
        // Parse simple JSON array of names: ["pkg1","pkg2"]
        std::string::size_type pos = 0;
        while ((pos = http_result.body.find('"', pos)) != std::string::npos) {
            ++pos;
            auto end = http_result.body.find('"', pos);
            if (end == std::string::npos)
                break;
            auto name = http_result.body.substr(pos, end - pos);
            if (!name.empty() && name.find(':') == std::string::npos) {
                results.push_back(std::move(name));
            }
            pos = end + 1;
        }
        if (!results.empty())
            return results;
    }

    // 2. Fallback to stub data
    for (const auto &pkg : stub_packages) {
        std::string name = pkg.name;
        if (name.find(query) != std::string::npos ||
            std::string(pkg.description).find(query) != std::string::npos) {
            if (std::find(results.begin(), results.end(), name) == results.end()) {
                results.push_back(std::move(name));
            }
        }
    }
    return results;
}

const std::string &Registry::base_url() const {
    return base_url_;
}

} // namespace ahfl::package
