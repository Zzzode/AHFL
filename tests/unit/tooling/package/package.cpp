#include "tooling/package/registry.hpp"
#include "tooling/package/resolver.hpp"

#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace ahfl::package;

int test_count = 0;
int pass_count = 0;

class FakeRegistryTransport final : public RegistryTransport {
  public:
    RegistryHttpResponse default_response{};
    std::map<std::string, RegistryHttpResponse> responses;
    std::vector<std::string> requested_urls;

    [[nodiscard]] RegistryHttpResponse get(std::string_view url, int /*timeout_seconds*/) override {
        requested_urls.push_back(std::string(url));
        const auto it = responses.find(std::string(url));
        if (it != responses.end()) {
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
    test_registry_search_percent_encodes_query();

    std::cerr << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? 0 : 1;
}
