#include "tooling/package/registry.hpp"
#include "tooling/package/registry_package_input.hpp"
#include "tooling/package/resolver.hpp"
#include "tooling/package/source_archive.hpp"

#include "base/support/sha256.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

using namespace ahfl::package;

int test_count = 0;
int pass_count = 0;

class FakeRegistryTransport final : public RegistryTransport {
  public:
    struct Request {
        std::string method;
        std::string url;
        std::string body;
    };

    RegistryHttpResponse default_response{};
    std::map<std::string, RegistryHttpResponse> responses;
    std::map<std::string, RegistryHttpResponse> request_responses;
    std::vector<std::string> requested_urls;
    std::vector<Request> requests;

    [[nodiscard]] RegistryHttpResponse get(std::string_view url, int /*timeout_seconds*/) override {
        requested_urls.push_back(std::string(url));
        const auto it = responses.find(std::string(url));
        if (it != responses.end()) {
            return it->second;
        }
        return default_response;
    }

    [[nodiscard]] RegistryHttpResponse request(std::string_view method,
                                               std::string_view url,
                                               std::string_view body,
                                               int /*timeout_seconds*/) override {
        requests.push_back(Request{
            .method = std::string(method),
            .url = std::string(url),
            .body = std::string(body),
        });
        const auto key = std::string(method) + " " + std::string(url);
        const auto it = request_responses.find(key);
        if (it != request_responses.end()) {
            return it->second;
        }
        return default_response;
    }
};

class FakeRegistryCache final : public RegistryCache {
  public:
    std::map<std::string, std::string> entries;
    std::map<std::string, std::string> saved;

    void save(std::string_view package_name, std::string_view json_data) override {
        saved[std::string(package_name)] = std::string(json_data);
    }

    [[nodiscard]] std::optional<std::string> load(std::string_view package_name) override {
        const auto it = entries.find(std::string(package_name));
        if (it == entries.end()) {
            return std::nullopt;
        }
        return it->second;
    }
};

void check(bool condition, const char *name) {
    ++test_count;
    if (condition) {
        ++pass_count;
    } else {
        std::cerr << "FAIL: " << name << "\n";
    }
}

std::filesystem::path make_temp_root(std::string_view name) {
    std::random_device random;
    const auto suffix =
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
        std::to_string(random()) + "-" + std::to_string(random());
    auto root = std::filesystem::temp_directory_path() /
                (std::string{"ahfl-package-test-"} + std::string{name} + "-" + suffix);
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    return root;
}

void write_text_file(const std::filesystem::path &path, std::string_view content) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream output(path, std::ios::binary);
    output << content;
}

const SourceArchiveFile *find_archive_file(const SourceArchive &archive, std::string_view path) {
    const auto it = std::find_if(archive.files.begin(), archive.files.end(), [&](const auto &file) {
        return file.path == path;
    });
    return it == archive.files.end() ? nullptr : &*it;
}

std::string read_text_file(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

std::string registry_package_index_json(std::string_view package_name,
                                        const std::vector<RegistryIndexEntry> &entries) {
    std::string json = R"({"format_version":"ahfl.registry.package_index.v1","package":")";
    json += package_name;
    json += R"(","versions":[)";
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (i != 0) {
            json += ",";
        }
        json += serialize_registry_index_entry(entries[i]);
    }
    json += "]}";
    return json;
}

std::string public_api_snapshot_json(std::string_view package_name, std::string_view version) {
    std::string json = R"({"schema":"ahfl.public_api.v1","package":{"name":")";
    json += package_name;
    json += R"(","version":")";
    json += version;
    json += R"("},"entries":[]})";
    return json;
}

std::string sha256_digest(std::string_view payload) {
    return "sha256:" + ahfl::support::sha256_hex(payload);
}

RegistryIndexEntry make_registry_index_entry(std::string version, bool yanked);

void test_parse_version_basic() {
    auto v = Resolver::parse_version("1.2.3");
    check(v.major == 1, "parse_version major");
    check(v.minor == 2, "parse_version minor");
    check(v.patch == 3, "parse_version patch");
    check(v.prerelease.empty(), "parse_version no prerelease");

    auto pre = Resolver::parse_version("2.0.0-alpha");
    check(pre.major == 2, "parse_version prerelease major");
    check(pre.minor == 0, "parse_version prerelease minor");
    check(pre.patch == 0, "parse_version prerelease patch");
    check(pre.prerelease == "alpha", "parse_version prerelease tag");

    auto formatted = Resolver::format_version(v);
    check(formatted == "1.2.3", "format_version roundtrip");
}

void test_satisfies_caret_constraint() {
    auto v120 = Resolver::parse_version("1.2.0");
    auto v130 = Resolver::parse_version("1.3.0");
    auto v190 = Resolver::parse_version("1.9.9");
    auto v200 = Resolver::parse_version("2.0.0");
    auto v110 = Resolver::parse_version("1.1.0");

    // ^1.2.0 means >=1.2.0 <2.0.0
    check(Resolver::satisfies(v120, "^1.2.0"), "caret exact match");
    check(Resolver::satisfies(v130, "^1.2.0"), "caret higher minor");
    check(Resolver::satisfies(v190, "^1.2.0"), "caret high minor+patch");
    check(!Resolver::satisfies(v200, "^1.2.0"), "caret rejects next major");
    check(!Resolver::satisfies(v110, "^1.2.0"), "caret rejects lower minor");

    // Tilde: ~1.2.0 means >=1.2.0 <1.3.0
    auto v125 = Resolver::parse_version("1.2.5");
    check(Resolver::satisfies(v120, "~1.2.0"), "tilde exact match");
    check(Resolver::satisfies(v125, "~1.2.0"), "tilde higher patch");
    check(!Resolver::satisfies(v130, "~1.2.0"), "tilde rejects next minor");
}

void test_resolve_simple_dependency_graph() {
    Resolver resolver;

    resolver.add_available("ahfl-stdlib",
                           {
                               Resolver::parse_version("1.0.0"),
                               Resolver::parse_version("1.1.0"),
                               Resolver::parse_version("2.0.0"),
                           });

    resolver.add_available("ahfl-http",
                           {
                               Resolver::parse_version("0.5.0"),
                               Resolver::parse_version("0.6.0"),
                           });

    resolver.add_dependency(Dependency{"ahfl-stdlib", "^1.0.0"});
    resolver.add_dependency(Dependency{"ahfl-http", ">=0.5.0"});

    auto result = resolver.resolve();
    check(result.success, "resolve succeeds");
    check(result.resolved.size() == 2, "resolve returns 2 packages");

    // Should pick highest satisfying version
    if (result.resolved.size() >= 1) {
        check(result.resolved[0].name == "ahfl-stdlib", "resolve stdlib name");
        check(Resolver::format_version(result.resolved[0].version) == "1.1.0",
              "resolve picks highest stdlib");
    }
    if (result.resolved.size() >= 2) {
        check(result.resolved[1].name == "ahfl-http", "resolve http name");
        check(Resolver::format_version(result.resolved[1].version) == "0.6.0",
              "resolve picks highest http");
    }
}

void test_resolve_detects_conflict() {
    Resolver resolver;

    resolver.add_available("ahfl-stdlib",
                           {
                               Resolver::parse_version("1.0.0"),
                               Resolver::parse_version("1.1.0"),
                               Resolver::parse_version("2.0.0"),
                           });

    // Two constraints that resolve to different versions
    resolver.add_dependency(Dependency{"ahfl-stdlib", "^1.0.0"}); // -> 1.1.0
    resolver.add_dependency(Dependency{"ahfl-stdlib", "^2.0.0"}); // -> 2.0.0

    auto result = resolver.resolve();
    check(!result.success, "conflict detected");
    check(result.error == ResolveError::VersionConflict, "conflict error type");
    check(!result.error_message.empty(), "conflict has error message");
}

void test_registry_fetch_metadata_from_transport() {
    auto transport = std::make_shared<FakeRegistryTransport>();
    auto cache = std::make_shared<FakeRegistryCache>();
    transport->responses["https://registry.test/packages/ahfl-http"] =
        RegistryHttpResponse{.status_code = 200,
                             .body = R"({
                                 "latest_version":"0.6.0",
                                 "versions":[
                                     {"version":"0.5.0","description":"HTTP capabilities"},
                                     {"version":"0.6.0","authors":["AHFL"],"sha256":"abc"}
                                 ]
                             })",
                             .success = true};

    Registry registry("https://registry.test", transport, cache);
    auto result = registry.fetch_metadata("ahfl-http");

    check(result.success, "registry metadata transport success");
    check(result.metadata.has_value(), "registry metadata present");
    if (result.metadata.has_value()) {
        check(result.metadata->name == "ahfl-http", "registry metadata package name");
        check(result.metadata->latest_version == "0.6.0", "registry metadata latest version");
        check(result.metadata->versions.size() == 2, "registry metadata versions");
        check(result.metadata->versions[1].checksum == "sha256:abc",
              "registry metadata sha256 checksum normalized");
    }
    check(cache->saved.find("ahfl-http") != cache->saved.end(), "registry metadata saved to cache");
}

void test_registry_fetch_metadata_from_cache_after_network_failure() {
    auto transport = std::make_shared<FakeRegistryTransport>();
    auto cache = std::make_shared<FakeRegistryCache>();
    cache->entries["ahfl-cache"] = R"({
        "latest_version":"1.1.0",
        "versions":[{"version":"1.0.0"},{"version":"1.1.0"}]
    })";

    Registry registry("https://registry.test", transport, cache);
    auto result = registry.fetch_metadata("ahfl-cache");

    check(result.success, "registry metadata cache success");
    check(result.metadata.has_value(), "registry cache metadata present");
    if (result.metadata.has_value()) {
        check(result.metadata->latest_version == "1.1.0", "registry cache latest version");
        check(result.metadata->versions.size() == 2, "registry cache version count");
    }
}

void test_registry_invalid_live_metadata_does_not_use_cache() {
    auto transport = std::make_shared<FakeRegistryTransport>();
    auto cache = std::make_shared<FakeRegistryCache>();
    transport->responses["https://registry.test/packages/ahfl-cache"] =
        RegistryHttpResponse{.status_code = 200, .body = R"({"versions": "bad"})", .success = true};
    cache->entries["ahfl-cache"] = R"({
        "latest_version":"1.1.0",
        "versions":[{"version":"1.1.0"}]
    })";

    Registry registry("https://registry.test", transport, cache);
    auto result = registry.fetch_metadata("ahfl-cache");

    check(!result.success, "registry invalid live metadata fails");
    check(!result.metadata.has_value(), "registry invalid live metadata no fallback metadata");
    check(result.error == RegistryError::InvalidResponse,
          "registry invalid live metadata error type");
}

void test_registry_does_not_fallback_to_stub_packages() {
    auto transport = std::make_shared<FakeRegistryTransport>();
    auto cache = std::make_shared<FakeRegistryCache>();
    Registry registry("https://registry.test", transport, cache);

    auto metadata = registry.fetch_metadata("ahfl-stdlib");
    check(!metadata.success, "registry missing package fails without stub");
    check(!metadata.metadata.has_value(), "registry missing package has no metadata");
    check(metadata.error == RegistryError::NetworkError, "registry missing package network error");

    auto version = registry.fetch_version("ahfl-stdlib", "1.0.0");
    check(!version.success, "registry missing version fails without stub");
    check(version.error == RegistryError::NetworkError, "registry missing version network error");

    auto results = registry.search("ahfl");
    check(results.empty(), "registry search has no stub fallback");
}

void test_registry_fetch_version_invalid_live_response_does_not_use_metadata_cache() {
    auto transport = std::make_shared<FakeRegistryTransport>();
    auto cache = std::make_shared<FakeRegistryCache>();
    transport->responses["https://registry.test/packages/ahfl-cache/versions/1.2.0"] =
        RegistryHttpResponse{.status_code = 200, .body = R"({"version": 12})", .success = true};
    cache->entries["ahfl-cache"] = R"({
        "latest_version":"1.2.0",
        "versions":[{"version":"1.2.0","description":"cached"}]
    })";

    Registry registry("https://registry.test", transport, cache);
    auto result = registry.fetch_version("ahfl-cache", "1.2.0");

    check(!result.success, "registry invalid live version fails");
    check(!result.metadata.has_value(), "registry invalid live version no fallback metadata");
    check(result.error == RegistryError::InvalidResponse,
          "registry invalid live version error type");
}

void test_registry_fetch_version_from_cached_metadata() {
    auto transport = std::make_shared<FakeRegistryTransport>();
    auto cache = std::make_shared<FakeRegistryCache>();
    cache->entries["ahfl-cache"] = R"({
        "latest_version":"1.2.0",
        "versions":[
            {"version":"1.1.0","description":"old"},
            {"version":"1.2.0","description":"new"}
        ]
    })";

    Registry registry("https://registry.test", transport, cache);
    auto result = registry.fetch_version("ahfl-cache", "1.2.0");

    check(result.success, "registry version cache success");
    check(result.metadata.has_value(), "registry version metadata present");
    if (result.metadata.has_value()) {
        check(result.metadata->latest_version == "1.2.0", "registry version latest");
        check(result.metadata->versions.size() == 1, "registry version single result");
        check(result.metadata->versions[0].description == "new", "registry version selected");
    }
}

void test_registry_fetch_package_artifacts_from_transport() {
    const auto root = make_temp_root("registry-fetch-artifacts-source");
    write_text_file(root / "ahfl.toml",
                    R"(manifest_version = 2
[package]
name = "risk-model"
version = "2.1.3"
edition = "2026"
kind = "library"
[module]
prefix = "risk_model"
root = "src"
[exports]
modules = ["main"]
[targets.lib]
kind = "library"
entry = "src/main.ahfl"
[dependencies]
std = { source = "sysroot" }
)");
    write_text_file(root / "src" / "main.ahfl", "module risk_model::main;\n");

    auto built = build_source_archive(root, "risk-model");
    check(built.archive.has_value(), "registry artifacts source archive present");
    if (built.archive.has_value()) {
        const auto out = make_temp_root("registry-fetch-artifacts-output");
        auto transport = std::make_shared<FakeRegistryTransport>();
        auto cache = std::make_shared<FakeRegistryCache>();
        RegistryIndexEntry index{
            .registry_id = "default",
            .package = "risk-model",
            .version = "2.1.3",
            .yanked = false,
            .source_archive_sha256 = built.archive->archive_sha256,
            .manifest_sha256 = built.archive->manifest_sha256,
            .public_api_sha256 =
                "sha256:ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
            .dependencies = {RegistryDependencyMetadata{.name = "std", .source = "sysroot"}},
        };
        transport->responses["https://registry.test/packages/risk-model/versions/2.1.3/"
                             "registry-index"] = RegistryHttpResponse{
            .status_code = 200, .body = serialize_registry_index_entry(index), .success = true};
        transport->responses["https://registry.test/packages/risk-model/versions/2.1.3/"
                             "source-archive"] =
            RegistryHttpResponse{.status_code = 200,
                                 .body = serialize_source_archive_manifest(*built.archive),
                                 .success = true};
        transport->responses["https://registry.test/packages/risk-model/versions/2.1.3/"
                             "source-archive.payload"] = RegistryHttpResponse{
            .status_code = 200, .body = built.archive->payload, .success = true};

        Registry registry("https://registry.test", transport, cache);
        auto result = registry.fetch_package_artifacts("risk-model", "2.1.3");

        check(result.success(), "registry artifacts fetch succeeds");
        check(result.artifacts.has_value(), "registry artifacts present");
        if (result.artifacts.has_value()) {
            check(result.artifacts->registry.source_archive_sha256 == built.archive->archive_sha256,
                  "registry artifacts index digest");
            check(result.artifacts->archive.archive_sha256 == built.archive->archive_sha256,
                  "registry artifacts archive digest");
            check(result.artifacts->payload == built.archive->payload,
                  "registry artifacts payload");
            check(result.artifacts->archive.payload == built.archive->payload,
                  "registry artifacts archive carries payload");

            auto package_input =
                materialize_registry_package_input(RegistryPackageInputMaterialization{
                    .registry = result.artifacts->registry,
                    .archive = result.artifacts->archive,
                    .payload = result.artifacts->payload,
                    .output_root = out,
                });
            check(!package_input.has_errors(), "registry artifacts materialize package input");
            check(package_input.package.has_value(),
                  "registry artifacts materialized package input present");
            if (package_input.package.has_value()) {
                check(package_input.package->source ==
                          ahfl::package_graph::PackageSourceKind::Registry,
                      "registry artifacts materialized package input source");
            }
        }
        check(cache->saved.find("registry-index:risk-model:2.1.3") != cache->saved.end(),
              "registry artifacts index cached");
        check(cache->saved.find("source-archive-manifest:" + built.archive->archive_sha256) !=
                  cache->saved.end(),
              "registry artifacts manifest cached");
        check(cache->saved.find("source-archive-payload:" + built.archive->archive_sha256) !=
                  cache->saved.end(),
              "registry artifacts payload cached");

        std::error_code cleanup_error;
        std::filesystem::remove_all(out, cleanup_error);
    }

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void test_registry_fetch_package_artifacts_from_cache_after_network_failure() {
    const auto root = make_temp_root("registry-fetch-artifacts-cache-source");
    write_text_file(root / "ahfl.toml",
                    R"(manifest_version = 2
[package]
name = "risk-model"
version = "2.1.3"
edition = "2026"
kind = "library"
[module]
prefix = "risk_model"
root = "src"
[exports]
modules = ["main"]
[dependencies]
std = { source = "sysroot" }
)");
    write_text_file(root / "src" / "main.ahfl", "module risk_model::main;\n");

    auto built = build_source_archive(root, "risk-model");
    check(built.archive.has_value(), "registry artifacts cache archive present");
    if (built.archive.has_value()) {
        auto transport = std::make_shared<FakeRegistryTransport>();
        auto cache = std::make_shared<FakeRegistryCache>();
        RegistryIndexEntry index{
            .registry_id = "default",
            .package = "risk-model",
            .version = "2.1.3",
            .yanked = false,
            .source_archive_sha256 = built.archive->archive_sha256,
            .manifest_sha256 = built.archive->manifest_sha256,
            .public_api_sha256 =
                "sha256:ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
            .dependencies = {RegistryDependencyMetadata{.name = "std", .source = "sysroot"}},
        };
        cache->entries["registry-index:risk-model:2.1.3"] = serialize_registry_index_entry(index);
        cache->entries["source-archive-manifest:" + built.archive->archive_sha256] =
            serialize_source_archive_manifest(*built.archive);
        cache->entries["source-archive-payload:" + built.archive->archive_sha256] =
            built.archive->payload;

        Registry registry("https://registry.test", transport, cache);
        auto result = registry.fetch_package_artifacts("risk-model", "2.1.3");

        check(result.success(), "registry artifacts cache fallback succeeds");
        check(result.artifacts.has_value(), "registry artifacts cache fallback present");
        if (result.artifacts.has_value()) {
            check(result.artifacts->payload == built.archive->payload,
                  "registry artifacts cache payload");
        }
        check(transport->requested_urls.size() == 3, "registry artifacts cache attempts network");
    }

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void test_registry_fetch_package_artifacts_invalid_live_index_does_not_use_cache() {
    auto transport = std::make_shared<FakeRegistryTransport>();
    auto cache = std::make_shared<FakeRegistryCache>();
    transport->responses["https://registry.test/packages/risk-model/versions/2.1.3/"
                         "registry-index"] =
        RegistryHttpResponse{.status_code = 200, .body = R"({"bad":true})", .success = true};
    cache->entries["registry-index:risk-model:2.1.3"] = R"({
        "format_version":"ahfl.registry.index.v1",
        "registry_id":"default",
        "package":"risk-model",
        "version":"2.1.3",
        "yanked":false,
        "source_archive_sha256":"sha256:ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
        "manifest_sha256":"sha256:ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
        "public_api_sha256":"sha256:ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
        "dependencies":[{"name":"std","source":"sysroot"}]
    })";

    Registry registry("https://registry.test", transport, cache);
    auto result = registry.fetch_package_artifacts("risk-model", "2.1.3");

    check(!result.success(), "registry artifacts invalid live index fails");
    check(!result.artifacts.has_value(), "registry artifacts invalid live index no artifacts");
    check(result.error == RegistryError::InvalidResponse,
          "registry artifacts invalid live index error");
}

void test_registry_fetch_package_artifacts_rejects_payload_digest_drift() {
    const auto root = make_temp_root("registry-fetch-artifacts-drift-source");
    write_text_file(root / "ahfl.toml",
                    R"(manifest_version = 2
[package]
name = "risk-model"
version = "2.1.3"
edition = "2026"
kind = "library"
[module]
prefix = "risk_model"
root = "src"
[exports]
modules = ["main"]
[dependencies]
std = { source = "sysroot" }
)");
    write_text_file(root / "src" / "main.ahfl", "module risk_model::main;\n");

    auto built = build_source_archive(root, "risk-model");
    check(built.archive.has_value(), "registry artifacts drift archive present");
    if (built.archive.has_value()) {
        auto transport = std::make_shared<FakeRegistryTransport>();
        auto cache = std::make_shared<FakeRegistryCache>();
        RegistryIndexEntry index{
            .registry_id = "default",
            .package = "risk-model",
            .version = "2.1.3",
            .yanked = false,
            .source_archive_sha256 = built.archive->archive_sha256,
            .manifest_sha256 = built.archive->manifest_sha256,
            .public_api_sha256 =
                "sha256:ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
            .dependencies = {RegistryDependencyMetadata{.name = "std", .source = "sysroot"}},
        };
        transport->responses["https://registry.test/packages/risk-model/versions/2.1.3/"
                             "registry-index"] = RegistryHttpResponse{
            .status_code = 200, .body = serialize_registry_index_entry(index), .success = true};
        transport->responses["https://registry.test/packages/risk-model/versions/2.1.3/"
                             "source-archive"] =
            RegistryHttpResponse{.status_code = 200,
                                 .body = serialize_source_archive_manifest(*built.archive),
                                 .success = true};
        transport->responses["https://registry.test/packages/risk-model/versions/2.1.3/"
                             "source-archive.payload"] = RegistryHttpResponse{
            .status_code = 200, .body = built.archive->payload + "x", .success = true};

        Registry registry("https://registry.test", transport, cache);
        auto result = registry.fetch_package_artifacts("risk-model", "2.1.3");

        check(!result.success(), "registry artifacts payload drift fails");
        check(!result.artifacts.has_value(), "registry artifacts payload drift no artifacts");
        check(result.error == RegistryError::InvalidResponse,
              "registry artifacts payload drift error");
    }

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void test_registry_fetch_public_api_snapshot_from_transport() {
    const auto snapshot = public_api_snapshot_json("risk-model", "2.1.3");
    const auto digest = sha256_digest(snapshot);
    auto transport = std::make_shared<FakeRegistryTransport>();
    auto cache = std::make_shared<FakeRegistryCache>();
    transport->responses["https://registry.test/packages/risk-model/versions/2.1.3/"
                         "public-api"] =
        RegistryHttpResponse{.status_code = 200, .body = snapshot, .success = true};

    Registry registry("https://registry.test", transport, cache);
    auto result = registry.fetch_public_api_snapshot("risk-model", "2.1.3", digest);

    check(result.success(), "registry public API snapshot fetch succeeds");
    check(result.snapshot.has_value(), "registry public API snapshot present");
    if (result.snapshot.has_value()) {
        check(result.snapshot->snapshot == snapshot, "registry public API snapshot body");
        check(result.snapshot->public_api_sha256 == digest,
              "registry public API snapshot digest field");
    }
    check(cache->saved.find("public-api-snapshot:" + digest) != cache->saved.end(),
          "registry public API snapshot cached");
}

void test_registry_fetch_public_api_snapshot_from_cache_after_network_failure() {
    const auto snapshot = public_api_snapshot_json("risk-model", "2.1.3");
    const auto digest = sha256_digest(snapshot);
    auto transport = std::make_shared<FakeRegistryTransport>();
    auto cache = std::make_shared<FakeRegistryCache>();
    cache->entries["public-api-snapshot:" + digest] = snapshot;

    Registry registry("https://registry.test", transport, cache);
    auto result = registry.fetch_public_api_snapshot("risk-model", "2.1.3", digest);

    check(result.success(), "registry public API snapshot cache fallback succeeds");
    check(result.snapshot.has_value(), "registry public API snapshot cache fallback present");
    if (result.snapshot.has_value()) {
        check(result.snapshot->snapshot == snapshot, "registry public API snapshot cache body");
    }
    check(transport->requested_urls.size() == 1,
          "registry public API snapshot cache attempts network");
}

void test_registry_fetch_public_api_snapshot_rejects_digest_drift() {
    const auto snapshot = public_api_snapshot_json("risk-model", "2.1.3");
    const auto digest = sha256_digest(snapshot);
    auto transport = std::make_shared<FakeRegistryTransport>();
    auto cache = std::make_shared<FakeRegistryCache>();
    transport->responses["https://registry.test/packages/risk-model/versions/2.1.3/"
                         "public-api"] =
        RegistryHttpResponse{.status_code = 200, .body = snapshot + " ", .success = true};
    cache->entries["public-api-snapshot:" + digest] = snapshot;

    Registry registry("https://registry.test", transport, cache);
    auto result = registry.fetch_public_api_snapshot("risk-model", "2.1.3", digest);

    check(!result.success(), "registry public API snapshot digest drift fails");
    check(!result.snapshot.has_value(), "registry public API snapshot digest drift no snapshot");
    check(result.error == RegistryError::InvalidResponse,
          "registry public API snapshot digest drift error");
}

void test_registry_fetch_public_api_snapshot_rejects_schema_drift() {
    const std::string snapshot = R"({"schema":"wrong","entries":[]})";
    const auto digest = sha256_digest(snapshot);
    auto transport = std::make_shared<FakeRegistryTransport>();
    auto cache = std::make_shared<FakeRegistryCache>();
    transport->responses["https://registry.test/packages/risk-model/versions/2.1.3/"
                         "public-api"] =
        RegistryHttpResponse{.status_code = 200, .body = snapshot, .success = true};

    Registry registry("https://registry.test", transport, cache);
    auto result = registry.fetch_public_api_snapshot("risk-model", "2.1.3", digest);

    check(!result.success(), "registry public API snapshot schema drift fails");
    check(!result.snapshot.has_value(), "registry public API snapshot schema drift no snapshot");
    check(result.error == RegistryError::InvalidResponse,
          "registry public API snapshot schema drift error");
}

void test_registry_publish_package_sends_validated_envelope() {
    const auto root = make_temp_root("registry-publish-source");
    write_text_file(root / "ahfl.toml",
                    R"(manifest_version = 2
[package]
name = "risk-model"
version = "2.1.3"
edition = "2026"
kind = "library"
[module]
prefix = "risk_model"
root = "src"
[exports]
modules = ["main"]
[dependencies]
std = { source = "sysroot" }
)");
    write_text_file(root / "src" / "main.ahfl", "module risk_model::main;\n");

    auto archive = build_source_archive(root, "risk-model");
    check(archive.archive.has_value(), "registry publish archive present");
    if (archive.archive.has_value()) {
        const auto public_api = public_api_snapshot_json("risk-model", "2.1.3");
        RegistryIndexEntry index{
            .registry_id = "default",
            .package = "risk-model",
            .version = "2.1.3",
            .yanked = false,
            .source_archive_sha256 = archive.archive->archive_sha256,
            .manifest_sha256 = archive.archive->manifest_sha256,
            .public_api_sha256 = sha256_digest(public_api),
            .dependencies = {RegistryDependencyMetadata{.name = "std", .source = "sysroot"}},
        };

        auto transport = std::make_shared<FakeRegistryTransport>();
        auto cache = std::make_shared<FakeRegistryCache>();
        transport->request_responses["PUT https://registry.test/packages/risk-model/versions/"
                                     "2.1.3/publish"] =
            RegistryHttpResponse{.status_code = 200,
                                 .body = serialize_registry_index_entry(index),
                                 .success = true};

        Registry registry("https://registry.test", transport, cache);
        auto result = registry.publish_package(RegistryPublishPackageRequest{
            .registry = index,
            .archive = *archive.archive,
            .public_api_snapshot = public_api,
        });

        check(result.success(), "registry publish succeeds");
        check(result.entry.has_value(), "registry publish returns entry");
        check(transport->requests.size() == 1, "registry publish sends one request");
        if (!transport->requests.empty()) {
            check(transport->requests.front().method == "PUT", "registry publish uses PUT");
            check(transport->requests.front().body.find("ahfl.registry.publish_request.v1") !=
                      std::string::npos,
                  "registry publish body schema");
            check(transport->requests.front().body.find("source_archive_payload") !=
                      std::string::npos,
                  "registry publish body includes payload");
            check(transport->requests.front().body.find("public_api_snapshot") !=
                      std::string::npos,
                  "registry publish body includes public API snapshot");
        }
    }

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void test_registry_publish_package_rejects_public_api_digest_drift_before_request() {
    const auto root = make_temp_root("registry-publish-digest-drift");
    write_text_file(root / "ahfl.toml",
                    R"(manifest_version = 2
[package]
name = "risk-model"
version = "2.1.3"
edition = "2026"
kind = "library"
[module]
prefix = "risk_model"
root = "src"
[exports]
modules = ["main"]
[dependencies]
std = { source = "sysroot" }
)");
    write_text_file(root / "src" / "main.ahfl", "module risk_model::main;\n");

    auto archive = build_source_archive(root, "risk-model");
    check(archive.archive.has_value(), "registry publish drift archive present");
    if (archive.archive.has_value()) {
        const auto public_api = public_api_snapshot_json("risk-model", "2.1.3");
        RegistryIndexEntry index{
            .registry_id = "default",
            .package = "risk-model",
            .version = "2.1.3",
            .yanked = false,
            .source_archive_sha256 = archive.archive->archive_sha256,
            .manifest_sha256 = archive.archive->manifest_sha256,
            .public_api_sha256 =
                "sha256:ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
            .dependencies = {RegistryDependencyMetadata{.name = "std", .source = "sysroot"}},
        };

        auto transport = std::make_shared<FakeRegistryTransport>();
        auto cache = std::make_shared<FakeRegistryCache>();
        Registry registry("https://registry.test", transport, cache);
        auto result = registry.publish_package(RegistryPublishPackageRequest{
            .registry = index,
            .archive = *archive.archive,
            .public_api_snapshot = public_api,
        });

        check(!result.success(), "registry publish digest drift fails");
        check(result.error == RegistryError::InvalidResponse,
              "registry publish digest drift error");
        check(transport->requests.empty(), "registry publish digest drift sends no request");
    }

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void test_registry_yank_package_requires_yanked_response() {
    auto transport = std::make_shared<FakeRegistryTransport>();
    auto cache = std::make_shared<FakeRegistryCache>();
    auto yanked = make_registry_index_entry("2.1.3", true);
    transport->request_responses["POST https://registry.test/packages/risk-model/versions/"
                                 "2.1.3/yank"] =
        RegistryHttpResponse{.status_code = 200,
                             .body = serialize_registry_index_entry(yanked),
                             .success = true};

    Registry registry("https://registry.test", transport, cache);
    auto result = registry.yank_package("risk-model", "2.1.3", "default", "bad release");

    check(result.success(), "registry yank succeeds");
    check(result.entry.has_value() && result.entry->yanked, "registry yank returns yanked entry");
    check(transport->requests.size() == 1, "registry yank sends one request");
    if (!transport->requests.empty()) {
        check(transport->requests.front().method == "POST", "registry yank uses POST");
        check(transport->requests.front().body.find("ahfl.registry.yank_request.v1") !=
                  std::string::npos,
              "registry yank body schema");
        check(transport->requests.front().body.find("bad release") != std::string::npos,
              "registry yank body includes reason");
    }

    auto bad_transport = std::make_shared<FakeRegistryTransport>();
    auto bad_cache = std::make_shared<FakeRegistryCache>();
    auto not_yanked = make_registry_index_entry("2.1.3", false);
    bad_transport->request_responses["POST https://registry.test/packages/risk-model/versions/"
                                     "2.1.3/yank"] =
        RegistryHttpResponse{.status_code = 200,
                             .body = serialize_registry_index_entry(not_yanked),
                             .success = true};
    Registry bad_registry("https://registry.test", bad_transport, bad_cache);
    auto bad_result =
        bad_registry.yank_package("risk-model", "2.1.3", "default", "bad release");

    check(!bad_result.success(), "registry yank false response fails");
    check(bad_result.error == RegistryError::InvalidResponse,
          "registry yank false response error");
}

void test_registry_package_input_resolution_fetches_transitive_registry_packages() {
    constexpr std::string_view public_api_digest =
        "sha256:ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";
    const auto root = make_temp_root("registry-input-resolution-root");
    const auto risk_root = make_temp_root("registry-input-resolution-risk");
    const auto audit_root = make_temp_root("registry-input-resolution-audit");
    const auto out = make_temp_root("registry-input-resolution-output");

    write_text_file(root / "ahfl.toml",
                    R"(manifest_version = 2
[package]
name = "app"
version = "1.0.0"
edition = "2026"
kind = "application"
[module]
prefix = "app"
root = "src"
[exports]
modules = ["main"]
[targets.lib]
kind = "library"
entry = "src/main.ahfl"
[dependencies]
std = { source = "sysroot" }
risk-model = { source = "registry", registry = "default", version = "^2.0.0" }
)");
    write_text_file(root / "src" / "main.ahfl", "module app::main;\n");

    write_text_file(risk_root / "ahfl.toml",
                    R"(manifest_version = 2
[package]
name = "risk-model"
version = "2.1.3"
edition = "2026"
kind = "library"
[module]
prefix = "risk_model"
root = "src"
[exports]
modules = ["main"]
[targets.lib]
kind = "library"
entry = "src/main.ahfl"
[dependencies]
std = { source = "sysroot" }
audit-core = { source = "registry", registry = "default", version = "^1.0.0" }
)");
    write_text_file(risk_root / "src" / "main.ahfl", "module risk_model::main;\n");

    write_text_file(audit_root / "ahfl.toml",
                    R"(manifest_version = 2
[package]
name = "audit-core"
version = "1.4.0"
edition = "2026"
kind = "library"
[module]
prefix = "audit_core"
root = "src"
[exports]
modules = ["main"]
[targets.lib]
kind = "library"
entry = "src/main.ahfl"
[dependencies]
std = { source = "sysroot" }
)");
    write_text_file(audit_root / "src" / "main.ahfl", "module audit_core::main;\n");

    auto root_manifest = ahfl::manifest::parse_package_manifest(read_text_file(root / "ahfl.toml"));
    auto risk_archive = build_source_archive(risk_root, "risk-model");
    auto audit_archive = build_source_archive(audit_root, "audit-core");
    check(root_manifest.manifest.has_value(), "registry input resolution root manifest");
    check(risk_archive.archive.has_value(), "registry input resolution risk archive");
    check(audit_archive.archive.has_value(), "registry input resolution audit archive");
    if (root_manifest.manifest.has_value() && risk_archive.archive.has_value() &&
        audit_archive.archive.has_value()) {
        RegistryIndexEntry risk_index{
            .registry_id = "default",
            .package = "risk-model",
            .version = "2.1.3",
            .yanked = false,
            .source_archive_sha256 = risk_archive.archive->archive_sha256,
            .manifest_sha256 = risk_archive.archive->manifest_sha256,
            .public_api_sha256 = std::string{public_api_digest},
            .dependencies = {RegistryDependencyMetadata{.name = "std", .source = "sysroot"},
                             RegistryDependencyMetadata{.name = "audit-core",
                                                        .source = "registry",
                                                        .registry = std::string{"default"},
                                                        .version_requirement =
                                                            std::string{"^1.0.0"}}},
        };
        RegistryIndexEntry audit_index{
            .registry_id = "default",
            .package = "audit-core",
            .version = "1.4.0",
            .yanked = false,
            .source_archive_sha256 = audit_archive.archive->archive_sha256,
            .manifest_sha256 = audit_archive.archive->manifest_sha256,
            .public_api_sha256 = std::string{public_api_digest},
            .dependencies = {RegistryDependencyMetadata{.name = "std", .source = "sysroot"}},
        };

        auto transport = std::make_shared<FakeRegistryTransport>();
        auto cache = std::make_shared<FakeRegistryCache>();
        transport->responses["https://registry.test/packages/risk-model/registry-index"] =
            RegistryHttpResponse{.status_code = 200,
                                 .body = registry_package_index_json("risk-model", {risk_index}),
                                 .success = true};
        transport->responses["https://registry.test/packages/audit-core/registry-index"] =
            RegistryHttpResponse{.status_code = 200,
                                 .body = registry_package_index_json("audit-core", {audit_index}),
                                 .success = true};
        transport->responses["https://registry.test/packages/risk-model/versions/2.1.3/"
                             "registry-index"] =
            RegistryHttpResponse{.status_code = 200,
                                 .body = serialize_registry_index_entry(risk_index),
                                 .success = true};
        transport->responses["https://registry.test/packages/risk-model/versions/2.1.3/"
                             "source-archive"] =
            RegistryHttpResponse{.status_code = 200,
                                 .body = serialize_source_archive_manifest(*risk_archive.archive),
                                 .success = true};
        transport->responses["https://registry.test/packages/risk-model/versions/2.1.3/"
                             "source-archive.payload"] = RegistryHttpResponse{
            .status_code = 200, .body = risk_archive.archive->payload, .success = true};
        transport->responses["https://registry.test/packages/audit-core/versions/1.4.0/"
                             "registry-index"] =
            RegistryHttpResponse{.status_code = 200,
                                 .body = serialize_registry_index_entry(audit_index),
                                 .success = true};
        transport->responses["https://registry.test/packages/audit-core/versions/1.4.0/"
                             "source-archive"] =
            RegistryHttpResponse{.status_code = 200,
                                 .body = serialize_source_archive_manifest(*audit_archive.archive),
                                 .success = true};
        transport->responses["https://registry.test/packages/audit-core/versions/1.4.0/"
                             "source-archive.payload"] = RegistryHttpResponse{
            .status_code = 200, .body = audit_archive.archive->payload, .success = true};

        Registry registry("https://registry.test", transport, cache);
        auto resolved = resolve_registry_package_inputs(RegistryPackageInputResolution{
            .root_manifest = &*root_manifest.manifest,
            .registry = &registry,
            .materialization_root = out,
        });

        check(!resolved.has_errors(), "registry input resolution succeeds");
        check(resolved.packages.size() == 2, "registry input resolution package count");
        if (resolved.packages.size() == 2) {
            check(resolved.packages[0].manifest.package_name == "risk-model",
                  "registry input resolution direct package");
            check(resolved.packages[1].manifest.package_name == "audit-core",
                  "registry input resolution transitive package");
            check(resolved.packages[0].source == ahfl::package_graph::PackageSourceKind::Registry,
                  "registry input resolution package source");
        }
    }

    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::remove_all(risk_root, error);
    std::filesystem::remove_all(audit_root, error);
    std::filesystem::remove_all(out, error);
}

void test_registry_search_percent_encodes_query() {
    auto transport = std::make_shared<FakeRegistryTransport>();
    auto cache = std::make_shared<FakeRegistryCache>();
    transport->default_response =
        RegistryHttpResponse{.status_code = 200, .body = R"(["ahfl-http"])", .success = true};

    Registry registry("https://registry.test", transport, cache);
    auto result = registry.search("name:ahfl http+grpc");

    check(result.size() == 1 && result[0] == "ahfl-http", "registry search result parsed");
    check(!transport->requested_urls.empty(), "registry search records request");
    if (!transport->requested_urls.empty()) {
        check(transport->requested_urls.front() ==
                  "https://registry.test/search?q=name%3Aahfl%20http%2Bgrpc",
              "registry search query encoded");
    }
}

void test_registry_index_entry_parse_and_serialize() {
    constexpr std::string_view digest =
        "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    const std::string input = std::string{R"({
            "format_version":"ahfl.registry.index.v1",
            "registry_id":"default",
            "package":"risk-model",
            "version":"2.1.3",
            "yanked":false,
            "source_archive_sha256":")"} +
                              std::string{digest} + R"(",
            "manifest_sha256":")" +
                              std::string{digest} + R"(",
            "public_api_sha256":")" +
                              std::string{digest} + R"(",
            "dependencies":[
                {"name":"std","source":"sysroot"},
                {"name":"audit-core","source":"registry","registry":"default","version":"^1.2.0"}
            ]
        })";

    auto parsed = parse_registry_index_entry(input);
    check(!parsed.has_errors(), "registry index parse success");
    check(parsed.entry.has_value(), "registry index parse entry present");
    if (parsed.entry.has_value()) {
        check(parsed.entry->registry_id == "default", "registry index registry id");
        check(parsed.entry->package == "risk-model", "registry index package");
        check(parsed.entry->version == "2.1.3", "registry index version");
        check(!parsed.entry->yanked, "registry index yanked false");
        check(parsed.entry->dependencies.size() == 2, "registry index dependency count");
        if (parsed.entry->dependencies.size() == 2) {
            check(parsed.entry->dependencies[1].source == "registry",
                  "registry index dependency source");
            check(parsed.entry->dependencies[1].version_requirement == "^1.2.0",
                  "registry index dependency requirement");
        }

        const auto serialized = serialize_registry_index_entry(*parsed.entry);
        auto roundtrip = parse_registry_index_entry(serialized);
        check(!roundtrip.has_errors(), "registry index serialize roundtrip parses");
        check(roundtrip.entry.has_value(), "registry index serialize roundtrip entry");
        if (roundtrip.entry.has_value()) {
            check(roundtrip.entry->dependencies.size() == 2,
                  "registry index serialize roundtrip dependencies");
        }
    }
}

void test_registry_index_entry_rejects_unknown_fields() {
    constexpr std::string_view digest =
        "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    const std::string input = std::string{R"({
            "format_version":"ahfl.registry.index.v1",
            "registry_id":"default",
            "package":"risk-model",
            "version":"2.1.3",
            "yanked":false,
            "source_archive_sha256":")"} +
                              std::string{digest} + R"(",
            "manifest_sha256":")" +
                              std::string{digest} + R"(",
            "public_api_sha256":")" +
                              std::string{digest} + R"(",
            "source_text":"forbidden",
            "dependencies":[{"name":"std","source":"sysroot","path":"forbidden"}]
        })";

    auto parsed = parse_registry_index_entry(input);
    check(parsed.has_errors(), "registry index unknown fields rejected");
    check(!parsed.entry.has_value(), "registry index unknown fields no entry");
}

void test_registry_index_entry_rejects_invalid_digests() {
    constexpr std::string_view digest =
        "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    const std::string input = std::string{R"({
            "format_version":"ahfl.registry.index.v1",
            "registry_id":"default",
            "package":"risk-model",
            "version":"2.1.3",
            "yanked":false,
            "source_archive_sha256":"sha256:ABC",
            "manifest_sha256":")"} +
                              std::string{digest} + R"(",
            "public_api_sha256":")" +
                              std::string{digest} + R"(",
            "dependencies":[{"name":"std","source":"sysroot"}]
        })";

    auto parsed = parse_registry_index_entry(input);
    check(parsed.has_errors(), "registry index invalid digest rejected");
    check(!parsed.entry.has_value(), "registry index invalid digest no entry");
}

RegistryIndexEntry make_registry_index_entry(std::string version, bool yanked) {
    constexpr std::string_view digest =
        "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    return RegistryIndexEntry{
        .registry_id = "default",
        .package = "risk-model",
        .version = std::move(version),
        .yanked = yanked,
        .source_archive_sha256 = std::string{digest},
        .manifest_sha256 = std::string{digest},
        .public_api_sha256 = std::string{digest},
        .dependencies = {RegistryDependencyMetadata{.name = "std", .source = "sysroot"}},
    };
}

void test_registry_candidate_selection_picks_highest_non_yanked() {
    const std::vector<RegistryIndexEntry> candidates = {
        make_registry_index_entry("2.1.0", false),
        make_registry_index_entry("2.2.0", true),
        make_registry_index_entry("2.1.3", false),
        make_registry_index_entry("3.0.0", false),
    };

    auto selection = select_registry_candidate("risk-model", candidates, "^2.1.0");
    check(selection.entry.has_value(), "registry candidate selected");
    if (selection.entry.has_value()) {
        check(selection.entry->version == "2.1.3",
              "registry candidate skips yanked and next major");
    }
    check(selection.error_message.empty(), "registry candidate success has no error");
}

void test_registry_candidate_selection_allows_locked_yanked_version() {
    const std::vector<RegistryIndexEntry> candidates = {
        make_registry_index_entry("2.1.0", false),
        make_registry_index_entry("2.2.0", true),
    };

    auto selection =
        select_registry_candidate("risk-model", candidates, "^2.1.0", std::string_view{"2.2.0"});
    check(selection.entry.has_value(), "registry candidate locked yanked selected");
    if (selection.entry.has_value()) {
        check(selection.entry->version == "2.2.0", "registry candidate locked yanked version");
        check(selection.entry->yanked, "registry candidate locked yanked flag");
    }
}

void test_registry_candidate_selection_reports_no_satisfying_version() {
    const std::vector<RegistryIndexEntry> candidates = {
        make_registry_index_entry("1.9.0", false),
        make_registry_index_entry("3.0.0", false),
    };

    auto selection = select_registry_candidate("risk-model", candidates, "^2.1.0");
    check(!selection.entry.has_value(), "registry candidate no satisfying version");
    check(!selection.error_message.empty(), "registry candidate failure has error");
}

void test_source_archive_builds_normalized_payload() {
    const auto root = make_temp_root("source-archive-normalized");
    write_text_file(root / "ahfl.toml",
                    R"(manifest_version = 2

[package]
name = "risk-model"
version = "1.0.0"
edition = "2026"
kind = "library"

[module]
prefix = "risk_model"
root = "src"

[exports]
modules = ["main"]

[dependencies]
std = { source = "sysroot" }
)");
    write_text_file(root / "src" / "main.ahfl",
                    "module risk_model::main;\r\nfn score() -> Int effect Pure;\r\n");
    write_text_file(root / "build" / "generated.ahfl",
                    "module risk_model::generated;\nfn leaked() -> Int effect Pure;\n");
    write_text_file(root / ".git" / "config", "[core]\nrepositoryformatversion = 0\n");
    write_text_file(root / "ahfl.lock", "{}\n");

    auto built = build_source_archive(root, "risk-model");
    check(!built.has_errors(), "source archive build succeeds");
    check(built.archive.has_value(), "source archive build returns archive");
    if (built.archive.has_value()) {
        const auto &archive = *built.archive;
        check(archive.files.size() == 2, "source archive includes only manifest and source");
        const auto *manifest = find_archive_file(archive, "ahfl.toml");
        const auto *source = find_archive_file(archive, "src/main.ahfl");
        check(manifest != nullptr, "source archive includes root manifest");
        check(source != nullptr, "source archive includes source file");
        check(find_archive_file(archive, "build/generated.ahfl") == nullptr,
              "source archive excludes build output");
        check(archive.payload.find('\r') == std::string::npos,
              "source archive normalizes CRLF newlines");
        check(archive.payload.find("build/generated.ahfl") == std::string::npos,
              "source archive payload omits excluded file");
        if (manifest != nullptr) {
            check(archive.manifest_sha256 == manifest->sha256,
                  "source archive manifest digest matches manifest file");
        }

        const auto serialized = serialize_source_archive_manifest(archive);
        auto parsed = parse_source_archive_manifest(serialized);
        check(!parsed.has_errors(), "source archive manifest serialize roundtrip parses");
        check(parsed.archive.has_value(), "source archive manifest roundtrip returns archive");
        if (parsed.archive.has_value()) {
            check(parsed.archive->archive_sha256 == archive.archive_sha256,
                  "source archive manifest roundtrip archive digest");
            check(parsed.archive->files.size() == archive.files.size(),
                  "source archive manifest roundtrip file count");
        }
    }

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void test_source_archive_excluded_files_do_not_change_digest() {
    const auto root = make_temp_root("source-archive-excluded");
    write_text_file(root / "ahfl.toml",
                    R"(manifest_version = 2
[package]
name = "risk-model"
version = "1.0.0"
edition = "2026"
kind = "library"
[module]
prefix = "risk_model"
root = "src"
[exports]
modules = ["main"]
[dependencies]
std = { source = "sysroot" }
audit_core = { source = "registry", registry = "default", version = "^1.2.0" }
)");
    write_text_file(root / "src" / "main.ahfl", "module risk_model::main;\n");
    write_text_file(root / "dist" / "artifact.ahfl", "module risk_model::artifact;\n");

    auto first = build_source_archive(root, "risk-model");
    write_text_file(root / "dist" / "artifact.ahfl", "module risk_model::changed;\n");
    auto second = build_source_archive(root, "risk-model");

    check(first.archive.has_value(), "source archive excluded digest first archive");
    check(second.archive.has_value(), "source archive excluded digest second archive");
    if (first.archive.has_value() && second.archive.has_value()) {
        check(first.archive->archive_sha256 == second.archive->archive_sha256,
              "source archive digest ignores excluded files");
    }

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void test_source_archive_requires_root_manifest() {
    const auto root = make_temp_root("source-archive-missing-manifest");
    write_text_file(root / "src" / "main.ahfl", "module risk_model::main;\n");

    auto built = build_source_archive(root, "risk-model");
    check(built.has_errors(), "source archive missing manifest fails");
    check(!built.archive.has_value(), "source archive missing manifest no archive");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void test_source_archive_manifest_rejects_unknown_fields_and_invalid_digests() {
    constexpr std::string_view digest =
        "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    const std::string input = std::string{R"json({
            "format_version":"ahfl.source_archive.v1",
            "package":"risk-model",
            "manifest_sha256":")json"} +
                              std::string{digest} + R"json(",
            "archive_sha256":"sha256:ABC",
            "payload":"forbidden",
            "files":[
                {"path":"ahfl.toml","size_bytes":12,"sha256":")json" +
                              std::string{digest} + R"json(","extra":true},
                {"path":"../escape.ahfl","size_bytes":1,"sha256":")json" +
                              std::string{digest} + R"json("}
            ]
        })json";

    auto parsed = parse_source_archive_manifest(input);
    check(parsed.has_errors(), "source archive manifest rejects invalid metadata");
    check(!parsed.archive.has_value(), "source archive manifest invalid metadata no archive");
}

void test_source_archive_materializes_verified_payload() {
    const auto root = make_temp_root("source-archive-materialize-source");
    const auto out = make_temp_root("source-archive-materialize-output");
    write_text_file(root / "ahfl.toml",
                    R"(manifest_version = 2
[package]
name = "risk-model"
version = "1.0.0"
edition = "2026"
kind = "library"
[module]
prefix = "risk_model"
root = "src"
[exports]
modules = ["main"]
[dependencies]
std = { source = "sysroot" }
)");
    write_text_file(root / "src" / "main.ahfl", "module risk_model::main;\n");

    auto built = build_source_archive(root, "risk-model");
    check(built.archive.has_value(), "source archive materialize source archive present");
    if (built.archive.has_value()) {
        auto materialized = materialize_source_archive(*built.archive, built.archive->payload, out);
        check(!materialized.has_errors(), "source archive materialize succeeds");
        check(materialized.package_root.has_value(), "source archive materialize package root");
        check(materialized.files == std::vector<std::string>({"ahfl.toml", "src/main.ahfl"}),
              "source archive materialize file list");
        check(read_text_file(out / "src" / "main.ahfl") == "module risk_model::main;\n",
              "source archive materialize source content");

        auto rebuilt = build_source_archive(out, "risk-model");
        check(rebuilt.archive.has_value(), "source archive materialize rebuild present");
        if (rebuilt.archive.has_value()) {
            check(rebuilt.archive->archive_sha256 == built.archive->archive_sha256,
                  "source archive materialize rebuild digest stable");
        }
    }

    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::remove_all(out, error);
}

void test_source_archive_materialize_rejects_tampering_and_non_empty_output() {
    const auto root = make_temp_root("source-archive-materialize-tamper-source");
    const auto out = make_temp_root("source-archive-materialize-tamper-output");
    write_text_file(root / "ahfl.toml",
                    R"(manifest_version = 2
[package]
name = "risk-model"
version = "1.0.0"
edition = "2026"
kind = "library"
[module]
prefix = "risk_model"
root = "src"
[exports]
modules = ["main"]
[dependencies]
std = { source = "sysroot" }
)");
    write_text_file(root / "src" / "main.ahfl", "module risk_model::main;\n");

    auto built = build_source_archive(root, "risk-model");
    check(built.archive.has_value(), "source archive tamper source archive present");
    if (built.archive.has_value()) {
        auto bad_payload =
            materialize_source_archive(*built.archive, built.archive->payload + "x", out);
        check(bad_payload.has_errors(), "source archive materialize rejects payload digest drift");
        check(!bad_payload.package_root.has_value(),
              "source archive materialize bad payload no root");

        auto bad_manifest = *built.archive;
        if (bad_manifest.files.size() >= 2) {
            bad_manifest.files[0].sha256 = bad_manifest.files[1].sha256;
        }
        auto bad_file_digest =
            materialize_source_archive(bad_manifest, built.archive->payload, out);
        check(bad_file_digest.has_errors(),
              "source archive materialize rejects manifest file digest drift");
        check(!bad_file_digest.package_root.has_value(),
              "source archive materialize bad manifest no root");

        write_text_file(out / "existing.txt", "occupied\n");
        auto non_empty = materialize_source_archive(*built.archive, built.archive->payload, out);
        check(non_empty.has_errors(), "source archive materialize rejects non-empty output");
        check(!non_empty.package_root.has_value(),
              "source archive materialize non-empty output no root");
    }

    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::remove_all(out, error);
}

void test_registry_package_input_materializes_verified_archive() {
    constexpr std::string_view public_api_digest =
        "sha256:ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";
    const auto root = make_temp_root("registry-package-input-source");
    const auto out = make_temp_root("registry-package-input-output");
    write_text_file(root / "ahfl.toml",
                    R"(manifest_version = 2
[package]
name = "risk-model"
version = "2.1.3"
edition = "2026"
kind = "library"
[module]
prefix = "risk_model"
root = "src"
[exports]
modules = ["main"]
[targets.lib]
kind = "library"
entry = "src/main.ahfl"
[dependencies]
std = { source = "sysroot" }
)");
    write_text_file(root / "src" / "main.ahfl", "module risk_model::main;\n");

    auto built = build_source_archive(root, "risk-model");
    check(built.archive.has_value(), "registry package input source archive present");
    if (built.archive.has_value()) {
        RegistryIndexEntry registry{
            .registry_id = "default",
            .package = "risk-model",
            .version = "2.1.3",
            .yanked = false,
            .source_archive_sha256 = built.archive->archive_sha256,
            .manifest_sha256 = built.archive->manifest_sha256,
            .public_api_sha256 = std::string{public_api_digest},
            .dependencies = {RegistryDependencyMetadata{.name = "std", .source = "sysroot"}},
        };
        auto result = materialize_registry_package_input(RegistryPackageInputMaterialization{
            .registry = registry,
            .archive = *built.archive,
            .payload = built.archive->payload,
            .output_root = out,
        });
        check(!result.has_errors(), "registry package input materialize succeeds");
        check(result.package.has_value(), "registry package input package present");
        check(result.materialized_files == std::vector<std::string>({"ahfl.toml", "src/main.ahfl"}),
              "registry package input materialized files");
        if (result.package.has_value()) {
            check(result.package->manifest.package_name == "risk-model",
                  "registry package input manifest name");
            check(result.package->manifest.package_version == "2.1.3",
                  "registry package input manifest version");
            check(result.package->source == ahfl::package_graph::PackageSourceKind::Registry,
                  "registry package input source kind");
            check(result.package->checksum == built.archive->archive_sha256,
                  "registry package input checksum");
            check(result.package->registry.has_value(), "registry package input identity");
            if (result.package->registry.has_value()) {
                check(result.package->registry->registry_id == "default",
                      "registry package input registry id");
                check(result.package->registry->public_api_sha256 == public_api_digest,
                      "registry package input public api digest");
            }
        }
    }

    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::remove_all(out, error);
}

void test_registry_package_input_rejects_digest_drift_and_manifest_mismatch() {
    constexpr std::string_view digest_a =
        "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    constexpr std::string_view public_api_digest =
        "sha256:ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";
    const auto root = make_temp_root("registry-package-input-reject-source");
    write_text_file(root / "ahfl.toml",
                    R"(manifest_version = 2
[package]
name = "risk-model"
version = "2.1.3"
edition = "2026"
kind = "library"
[module]
prefix = "risk_model"
root = "src"
[exports]
modules = ["main"]
[targets.lib]
kind = "library"
entry = "src/main.ahfl"
[dependencies]
std = { source = "sysroot" }
)");
    write_text_file(root / "src" / "main.ahfl", "module risk_model::main;\n");

    auto built = build_source_archive(root, "risk-model");
    check(built.archive.has_value(), "registry package input reject archive present");
    if (built.archive.has_value()) {
        auto registry = RegistryIndexEntry{
            .registry_id = "default",
            .package = "risk-model",
            .version = "2.1.3",
            .yanked = false,
            .source_archive_sha256 = std::string{digest_a},
            .manifest_sha256 = built.archive->manifest_sha256,
            .public_api_sha256 = std::string{public_api_digest},
            .dependencies = {RegistryDependencyMetadata{.name = "std", .source = "sysroot"},
                             RegistryDependencyMetadata{.name = "audit_core",
                                                        .source = "registry",
                                                        .registry = std::string{"default"},
                                                        .version_requirement =
                                                            std::string{"^1.2.0"}}},
        };
        auto digest_out = make_temp_root("registry-package-input-reject-digest-output");
        auto digest_result = materialize_registry_package_input(RegistryPackageInputMaterialization{
            .registry = registry,
            .archive = *built.archive,
            .payload = built.archive->payload,
            .output_root = digest_out,
        });
        check(digest_result.has_errors(), "registry package input rejects archive digest drift");
        check(!digest_result.package.has_value(), "registry package input digest drift no package");

        registry.source_archive_sha256 = built.archive->archive_sha256;
        registry.version = "2.1.4";
        auto version_out = make_temp_root("registry-package-input-reject-version-output");
        auto version_result =
            materialize_registry_package_input(RegistryPackageInputMaterialization{
                .registry = registry,
                .archive = *built.archive,
                .payload = built.archive->payload,
                .output_root = version_out,
            });
        check(version_result.has_errors(), "registry package input rejects manifest version drift");
        check(!version_result.package.has_value(),
              "registry package input manifest version drift no package");

        registry.version = "2.1.3";
        registry.dependencies[1].version_requirement = std::string{"~1.2.0"};
        auto dependency_out = make_temp_root("registry-package-input-reject-dependency-output");
        auto dependency_result =
            materialize_registry_package_input(RegistryPackageInputMaterialization{
                .registry = registry,
                .archive = *built.archive,
                .payload = built.archive->payload,
                .output_root = dependency_out,
            });
        check(dependency_result.has_errors(),
              "registry package input rejects dependency metadata drift");
        check(!dependency_result.package.has_value(),
              "registry package input dependency metadata drift no package");

        std::error_code cleanup_error;
        std::filesystem::remove_all(digest_out, cleanup_error);
        std::filesystem::remove_all(version_out, cleanup_error);
        std::filesystem::remove_all(dependency_out, cleanup_error);
    }

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

} // namespace

int main() {
    test_parse_version_basic();
    test_satisfies_caret_constraint();
    test_resolve_simple_dependency_graph();
    test_resolve_detects_conflict();
    test_registry_fetch_metadata_from_transport();
    test_registry_fetch_metadata_from_cache_after_network_failure();
    test_registry_invalid_live_metadata_does_not_use_cache();
    test_registry_does_not_fallback_to_stub_packages();
    test_registry_fetch_version_invalid_live_response_does_not_use_metadata_cache();
    test_registry_fetch_version_from_cached_metadata();
    test_registry_fetch_package_artifacts_from_transport();
    test_registry_fetch_package_artifacts_from_cache_after_network_failure();
    test_registry_fetch_package_artifacts_invalid_live_index_does_not_use_cache();
    test_registry_fetch_package_artifacts_rejects_payload_digest_drift();
    test_registry_fetch_public_api_snapshot_from_transport();
    test_registry_fetch_public_api_snapshot_from_cache_after_network_failure();
    test_registry_fetch_public_api_snapshot_rejects_digest_drift();
    test_registry_fetch_public_api_snapshot_rejects_schema_drift();
    test_registry_publish_package_sends_validated_envelope();
    test_registry_publish_package_rejects_public_api_digest_drift_before_request();
    test_registry_yank_package_requires_yanked_response();
    test_registry_package_input_resolution_fetches_transitive_registry_packages();
    test_registry_search_percent_encodes_query();
    test_registry_index_entry_parse_and_serialize();
    test_registry_index_entry_rejects_unknown_fields();
    test_registry_index_entry_rejects_invalid_digests();
    test_registry_candidate_selection_picks_highest_non_yanked();
    test_registry_candidate_selection_allows_locked_yanked_version();
    test_registry_candidate_selection_reports_no_satisfying_version();
    test_source_archive_builds_normalized_payload();
    test_source_archive_excluded_files_do_not_change_digest();
    test_source_archive_requires_root_manifest();
    test_source_archive_manifest_rejects_unknown_fields_and_invalid_digests();
    test_source_archive_materializes_verified_payload();
    test_source_archive_materialize_rejects_tampering_and_non_empty_output();
    test_registry_package_input_materializes_verified_archive();
    test_registry_package_input_rejects_digest_drift_and_manifest_mismatch();

    std::cerr << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? 0 : 1;
}
