#include "compiler/package_graph/lockfile.hpp"

#include "base/json/json_value.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <utility>

namespace ahfl::package_graph {
namespace {

constexpr std::string_view kPackageGraph = "E::package_graph";
constexpr std::string_view kLockfileSyntax = "E::lockfile_syntax";

void add_error(std::vector<Diagnostic> &diagnostics,
               std::string code,
               std::string message,
               SourceRange range = {}) {
    diagnostics.push_back(Diagnostic{
        .code = std::move(code),
        .message = std::move(message),
        .range = range,
    });
}

[[nodiscard]] SourceRange range_of(const json::JsonValue &value) {
    return SourceRange{.begin_offset = value.begin_offset, .end_offset = value.end_offset};
}

[[nodiscard]] std::string package_id_text(PackageId id) {
    return std::to_string(id.value);
}

[[nodiscard]] std::string edge_text(PackageId from, std::string_view dependency, PackageId to) {
    return package_id_text(from) + " --" + std::string{dependency} + "--> " + package_id_text(to);
}

[[nodiscard]] std::string edge_key(PackageId from, std::string_view dependency, PackageId to) {
    return package_id_text(from) + "\n" + std::string{dependency} + "\n" + package_id_text(to);
}

[[nodiscard]] std::string_view lockfile_source_name(PackageSourceKind source) noexcept {
    switch (source) {
    case PackageSourceKind::Sysroot:
        return "sysroot";
    case PackageSourceKind::Root:
    case PackageSourceKind::Path:
        return "path";
    case PackageSourceKind::Workspace:
        return "workspace";
    }
    return "unknown";
}

[[nodiscard]] std::filesystem::path normalize_path(const std::filesystem::path &path) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    const auto candidate = error ? path.lexically_normal() : absolute.lexically_normal();
    const auto canonical = std::filesystem::weakly_canonical(candidate, error);
    return (error ? candidate : canonical).lexically_normal();
}

[[nodiscard]] bool relative_path_stays_inside(const std::filesystem::path &path) {
    if (path.empty() || path.is_absolute()) {
        return false;
    }
    for (const auto &part : path.lexically_normal()) {
        if (part == "..") {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string
lockfile_manifest_path(const PackageNode &package,
                       const std::optional<std::filesystem::path> &lockfile_directory) {
    if (!lockfile_directory.has_value()) {
        return package.manifest_path.generic_string();
    }

    std::error_code error;
    auto relative = std::filesystem::relative(package.manifest_path, *lockfile_directory, error);
    if (!error && relative_path_stays_inside(relative)) {
        return relative.lexically_normal().generic_string();
    }
    relative = package.manifest_path.lexically_relative(*lockfile_directory);
    if (relative_path_stays_inside(relative)) {
        return relative.lexically_normal().generic_string();
    }
    return package.manifest_path.generic_string();
}

[[nodiscard]] std::string
resolve_lockfile_manifest_path(std::string_view manifest,
                               const std::optional<std::filesystem::path> &lockfile_directory) {
    const std::filesystem::path path{std::string{manifest}};
    if (!lockfile_directory.has_value()) {
        return std::string{manifest};
    }
    if (path.is_absolute()) {
        return normalize_path(path).generic_string();
    }
    return normalize_path(*lockfile_directory / path).generic_string();
}

[[nodiscard]] bool is_lockfile_source(std::string_view source) noexcept {
    return source == "sysroot" || source == "path" || source == "workspace";
}

[[nodiscard]] bool is_lower_hex(char value) noexcept {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
}

[[nodiscard]] bool is_sha256_checksum(std::string_view checksum) noexcept {
    constexpr std::string_view prefix = "sha256:";
    constexpr std::size_t hex_size = 64;
    if (!checksum.starts_with(prefix) || checksum.size() != prefix.size() + hex_size) {
        return false;
    }
    const auto hex = checksum.substr(prefix.size());
    return std::all_of(hex.begin(), hex.end(), is_lower_hex);
}

[[nodiscard]] const PackageNode *root_package(const PackageGraph &graph) {
    const auto found =
        std::find_if(graph.packages.begin(), graph.packages.end(), [](const auto &p) {
            return p.source == PackageSourceKind::Root;
        });
    if (found != graph.packages.end()) {
        return &*found;
    }
    return graph.find_package(PackageId{1});
}

[[nodiscard]] std::unique_ptr<json::JsonValue> make_package_json(const LockfilePackage &package) {
    auto value = json::JsonValue::make_object();
    value->set("id", json::JsonValue::make_int(static_cast<std::int64_t>(package.id.value)));
    value->set("name", json::JsonValue::make_string(package.name));
    value->set("version", json::JsonValue::make_string(package.version));
    value->set("source", json::JsonValue::make_string(package.source));
    value->set("manifest", json::JsonValue::make_string(package.manifest));
    value->set("checksum", json::JsonValue::make_string(package.checksum));
    return value;
}

[[nodiscard]] std::unique_ptr<json::JsonValue> make_edge_json(const LockfileEdge &edge) {
    auto value = json::JsonValue::make_object();
    value->set("from", json::JsonValue::make_int(static_cast<std::int64_t>(edge.from.value)));
    value->set("dependency", json::JsonValue::make_string(edge.dependency));
    value->set("to", json::JsonValue::make_int(static_cast<std::int64_t>(edge.to.value)));
    return value;
}

[[nodiscard]] const json::JsonValue *required_field(const json::JsonValue &object,
                                                    std::string_view field,
                                                    std::vector<Diagnostic> &diagnostics,
                                                    SourceRange fallback_range) {
    if (const auto *value = object.get(field); value != nullptr) {
        return value;
    }
    add_error(diagnostics,
              std::string{kLockfileSyntax},
              "lockfile is missing required field '" + std::string{field} + "'",
              fallback_range);
    return nullptr;
}

bool require_object(const json::JsonValue &value,
                    std::string_view field,
                    std::vector<Diagnostic> &diagnostics) {
    if (value.is_object()) {
        return true;
    }
    add_error(diagnostics,
              std::string{kLockfileSyntax},
              "lockfile field '" + std::string{field} + "' must be an object",
              range_of(value));
    return false;
}

[[nodiscard]] std::optional<std::string> read_string(const json::JsonValue &object,
                                                     std::string_view field,
                                                     std::vector<Diagnostic> &diagnostics) {
    const auto *value = required_field(object, field, diagnostics, range_of(object));
    if (value == nullptr) {
        return std::nullopt;
    }
    const auto text = value->as_string();
    if (!text.has_value()) {
        add_error(diagnostics,
                  std::string{kLockfileSyntax},
                  "lockfile field '" + std::string{field} + "' must be a string",
                  range_of(*value));
        return std::nullopt;
    }
    return std::string{*text};
}

[[nodiscard]] std::optional<int> read_int(const json::JsonValue &object,
                                          std::string_view field,
                                          std::vector<Diagnostic> &diagnostics) {
    const auto *value = required_field(object, field, diagnostics, range_of(object));
    if (value == nullptr) {
        return std::nullopt;
    }
    const auto number = value->as_int();
    if (!number.has_value() || *number < std::numeric_limits<int>::min() ||
        *number > std::numeric_limits<int>::max()) {
        add_error(diagnostics,
                  std::string{kLockfileSyntax},
                  "lockfile field '" + std::string{field} + "' must be an integer",
                  range_of(*value));
        return std::nullopt;
    }
    return static_cast<int>(*number);
}

[[nodiscard]] std::optional<PackageId> read_package_id(const json::JsonValue &object,
                                                       std::string_view field,
                                                       std::vector<Diagnostic> &diagnostics) {
    const auto *value = required_field(object, field, diagnostics, range_of(object));
    if (value == nullptr) {
        return std::nullopt;
    }
    const auto number = value->as_int();
    if (!number.has_value() || *number < 0 ||
        static_cast<std::uint64_t>(*number) >
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        add_error(diagnostics,
                  std::string{kLockfileSyntax},
                  "lockfile field '" + std::string{field} + "' must be a non-negative package id",
                  range_of(*value));
        return std::nullopt;
    }
    return PackageId{static_cast<std::size_t>(*number)};
}

void reject_unknown_fields(const json::JsonValue &object,
                           std::initializer_list<std::string_view> allowed,
                           std::string_view context,
                           std::vector<Diagnostic> &diagnostics) {
    for (const auto &[key, value] : object.object_fields) {
        const auto is_allowed = std::any_of(
            allowed.begin(), allowed.end(), [&](auto candidate) { return candidate == key; });
        if (!is_allowed) {
            add_error(diagnostics,
                      std::string{kLockfileSyntax},
                      "unsupported lockfile field '" + std::string{context} + "." + key + "'",
                      range_of(*value));
        }
    }
}

[[nodiscard]] std::optional<LockfilePackage> parse_package(const json::JsonValue &value,
                                                           std::vector<Diagnostic> &diagnostics) {
    if (!require_object(value, "packages[]", diagnostics)) {
        return std::nullopt;
    }
    reject_unknown_fields(value,
                          {"id", "name", "version", "source", "manifest", "checksum"},
                          "packages[]",
                          diagnostics);

    const auto id = read_package_id(value, "id", diagnostics);
    const auto name = read_string(value, "name", diagnostics);
    const auto version = read_string(value, "version", diagnostics);
    const auto source = read_string(value, "source", diagnostics);
    const auto manifest = read_string(value, "manifest", diagnostics);
    const auto checksum = read_string(value, "checksum", diagnostics);
    if (!id.has_value() || !name.has_value() || !version.has_value() || !source.has_value() ||
        !manifest.has_value() || !checksum.has_value()) {
        return std::nullopt;
    }
    if (!is_lockfile_source(*source)) {
        add_error(diagnostics,
                  std::string{kLockfileSyntax},
                  "lockfile field 'source' must be 'sysroot', 'path', or 'workspace'",
                  range_of(*value.get("source")));
    }
    if (!is_sha256_checksum(*checksum)) {
        add_error(diagnostics,
                  std::string{kLockfileSyntax},
                  "lockfile field 'checksum' must be sha256:<64 lowercase hex digits>",
                  range_of(*value.get("checksum")));
    }

    return LockfilePackage{
        .id = *id,
        .name = *name,
        .version = *version,
        .source = *source,
        .manifest = *manifest,
        .checksum = *checksum,
    };
}

[[nodiscard]] std::optional<LockfileEdge> parse_edge(const json::JsonValue &value,
                                                     std::vector<Diagnostic> &diagnostics) {
    if (!require_object(value, "edges[]", diagnostics)) {
        return std::nullopt;
    }
    reject_unknown_fields(value, {"from", "dependency", "to"}, "edges[]", diagnostics);

    const auto from = read_package_id(value, "from", diagnostics);
    const auto dependency = read_string(value, "dependency", diagnostics);
    const auto to = read_package_id(value, "to", diagnostics);
    if (!from.has_value() || !dependency.has_value() || !to.has_value()) {
        return std::nullopt;
    }

    return LockfileEdge{
        .from = *from,
        .dependency = *dependency,
        .to = *to,
    };
}

[[nodiscard]] const LockfilePackage *find_lockfile_package(const Lockfile &lockfile, PackageId id) {
    const auto found = std::find_if(lockfile.packages.begin(),
                                    lockfile.packages.end(),
                                    [id](const auto &p) { return p.id == id; });
    if (found == lockfile.packages.end()) {
        return nullptr;
    }
    return &*found;
}

void compare_package_field(std::vector<Diagnostic> &diagnostics,
                           PackageId id,
                           std::string_view field,
                           std::string_view actual,
                           std::string_view expected) {
    if (actual == expected) {
        return;
    }
    add_error(diagnostics,
              std::string{kPackageGraph},
              "lockfile package id " + package_id_text(id) + " field '" + std::string{field} +
                  "' is '" + std::string{actual} + "' but resolver produced '" +
                  std::string{expected} + "'");
}

[[nodiscard]] std::set<std::string> edge_keys_for_graph(const PackageGraph &graph) {
    std::set<std::string> keys;
    for (const auto &edge : graph.dependencies) {
        keys.insert(edge_key(edge.from, edge.dependency_key, edge.to));
    }
    return keys;
}

[[nodiscard]] std::set<std::string> edge_keys_for_lockfile(const Lockfile &lockfile) {
    std::set<std::string> keys;
    for (const auto &edge : lockfile.edges) {
        keys.insert(edge_key(edge.from, edge.dependency, edge.to));
    }
    return keys;
}

} // namespace

Lockfile make_lockfile(const PackageGraph &graph,
                       const std::optional<std::filesystem::path> &lockfile_directory) {
    Lockfile lockfile;
    if (const auto *root = root_package(graph); root != nullptr) {
        lockfile.root_package = root->name;
    }

    lockfile.packages.reserve(graph.packages.size());
    for (const auto &package : graph.packages) {
        lockfile.packages.push_back(LockfilePackage{
            .id = package.id,
            .name = package.name,
            .version = package.version,
            .source = std::string{lockfile_source_name(package.source)},
            .manifest = lockfile_manifest_path(package, lockfile_directory),
            .checksum = package.checksum,
        });
    }

    lockfile.edges.reserve(graph.dependencies.size());
    for (const auto &edge : graph.dependencies) {
        lockfile.edges.push_back(LockfileEdge{
            .from = edge.from,
            .dependency = edge.dependency_key,
            .to = edge.to,
        });
    }

    return lockfile;
}

Lockfile make_lockfile(const PackageGraph &graph) {
    return make_lockfile(graph, std::nullopt);
}

Lockfile make_lockfile(const PackageGraph &graph, const std::filesystem::path &lockfile_directory) {
    return make_lockfile(graph,
                         std::optional<std::filesystem::path>{normalize_path(lockfile_directory)});
}

std::string serialize_lockfile(const Lockfile &lockfile) {
    auto root = json::JsonValue::make_object();
    root->set("format_version", json::JsonValue::make_string(lockfile.format_version));
    root->set("resolver_version", json::JsonValue::make_int(lockfile.resolver_version));
    root->set("root_package", json::JsonValue::make_string(lockfile.root_package));

    auto packages = json::JsonValue::make_array();
    for (const auto &package : lockfile.packages) {
        packages->push(make_package_json(package));
    }
    root->set("packages", std::move(packages));

    auto edges = json::JsonValue::make_array();
    for (const auto &edge : lockfile.edges) {
        edges->push(make_edge_json(edge));
    }
    root->set("edges", std::move(edges));

    return json::serialize_json(*root);
}

LockfileResult parse_lockfile_json(std::string_view input) {
    LockfileResult result;
    auto parsed = json::parse_json(input);
    if (!parsed.has_value()) {
        add_error(result.diagnostics,
                  std::string{kLockfileSyntax},
                  "lockfile must be valid JSON",
                  SourceRange{.begin_offset = 0, .end_offset = input.size()});
        return result;
    }

    const auto &root = **parsed;
    if (!root.is_object()) {
        add_error(result.diagnostics,
                  std::string{kLockfileSyntax},
                  "lockfile root must be an object",
                  range_of(root));
        return result;
    }

    reject_unknown_fields(
        root,
        {"format_version", "resolver_version", "root_package", "packages", "edges"},
        "root",
        result.diagnostics);

    Lockfile lockfile;
    if (auto format = read_string(root, "format_version", result.diagnostics); format.has_value()) {
        lockfile.format_version = *format;
    }
    if (auto resolver = read_int(root, "resolver_version", result.diagnostics);
        resolver.has_value()) {
        lockfile.resolver_version = *resolver;
    }
    if (auto root_name = read_string(root, "root_package", result.diagnostics);
        root_name.has_value()) {
        lockfile.root_package = *root_name;
    }

    if (const auto *packages = required_field(root, "packages", result.diagnostics, range_of(root));
        packages != nullptr) {
        if (!packages->is_array()) {
            add_error(result.diagnostics,
                      std::string{kLockfileSyntax},
                      "lockfile field 'packages' must be an array",
                      range_of(*packages));
        } else {
            std::set<std::size_t> seen_ids;
            for (const auto &item : packages->array_items) {
                auto package = parse_package(*item, result.diagnostics);
                if (!package.has_value()) {
                    continue;
                }
                if (!seen_ids.insert(package->id.value).second) {
                    add_error(result.diagnostics,
                              std::string{kLockfileSyntax},
                              "lockfile contains duplicate package id " +
                                  package_id_text(package->id),
                              range_of(*item));
                    continue;
                }
                lockfile.packages.push_back(std::move(*package));
            }
        }
    }

    if (const auto *edges = required_field(root, "edges", result.diagnostics, range_of(root));
        edges != nullptr) {
        if (!edges->is_array()) {
            add_error(result.diagnostics,
                      std::string{kLockfileSyntax},
                      "lockfile field 'edges' must be an array",
                      range_of(*edges));
        } else {
            std::set<std::string> seen_edges;
            for (const auto &item : edges->array_items) {
                auto edge = parse_edge(*item, result.diagnostics);
                if (!edge.has_value()) {
                    continue;
                }
                const auto key = edge_key(edge->from, edge->dependency, edge->to);
                if (!seen_edges.insert(key).second) {
                    add_error(result.diagnostics,
                              std::string{kLockfileSyntax},
                              "lockfile contains duplicate dependency edge " +
                                  edge_text(edge->from, edge->dependency, edge->to),
                              range_of(*item));
                    continue;
                }
                lockfile.edges.push_back(std::move(*edge));
            }
        }
    }

    if (lockfile.format_version != "ahfl.lock.v1") {
        add_error(result.diagnostics,
                  std::string{kLockfileSyntax},
                  "lockfile format_version must be 'ahfl.lock.v1'");
    }
    if (lockfile.resolver_version != 1) {
        add_error(result.diagnostics,
                  std::string{kLockfileSyntax},
                  "lockfile resolver_version must be 1");
    }

    if (!result.has_errors()) {
        result.lockfile = std::move(lockfile);
    }
    return result;
}

std::vector<Diagnostic>
check_lockfile_drift(const PackageGraph &graph,
                     const Lockfile &lockfile,
                     const std::optional<std::filesystem::path> &lockfile_directory) {
    std::vector<Diagnostic> diagnostics;

    if (lockfile.format_version != "ahfl.lock.v1") {
        add_error(diagnostics,
                  std::string{kPackageGraph},
                  "lockfile format_version must be 'ahfl.lock.v1'");
    }
    if (lockfile.resolver_version != 1) {
        add_error(diagnostics, std::string{kPackageGraph}, "lockfile resolver_version must be 1");
    }

    if (const auto *root = root_package(graph);
        root != nullptr && lockfile.root_package != root->name) {
        add_error(diagnostics,
                  std::string{kPackageGraph},
                  "lockfile root_package is '" + lockfile.root_package +
                      "' but resolver produced '" + root->name + "'");
    }

    for (const auto &package : lockfile.packages) {
        const auto *graph_package = graph.find_package(package.id);
        if (graph_package == nullptr) {
            add_error(diagnostics,
                      std::string{kPackageGraph},
                      "lockfile contains unused package id " + package_id_text(package.id) + " ('" +
                          package.name + "')");
            continue;
        }

        if (package.name != graph_package->name) {
            add_error(diagnostics,
                      std::string{kPackageGraph},
                      "lockfile package id " + package_id_text(package.id) + " is '" +
                          package.name + "' but resolver assigned '" + graph_package->name + "'");
            continue;
        }

        compare_package_field(
            diagnostics, package.id, "version", package.version, graph_package->version);
        compare_package_field(diagnostics,
                              package.id,
                              "source",
                              package.source,
                              lockfile_source_name(graph_package->source));
        const auto actual_manifest =
            resolve_lockfile_manifest_path(package.manifest, lockfile_directory);
        compare_package_field(diagnostics,
                              package.id,
                              "manifest",
                              actual_manifest,
                              graph_package->manifest_path.generic_string());
        compare_package_field(
            diagnostics, package.id, "checksum", package.checksum, graph_package->checksum);
    }

    for (const auto &package : graph.packages) {
        if (find_lockfile_package(lockfile, package.id) == nullptr) {
            add_error(diagnostics,
                      std::string{kPackageGraph},
                      "lockfile is missing package id " + package_id_text(package.id) + " ('" +
                          package.name + "')");
        }
    }

    const auto graph_edges = edge_keys_for_graph(graph);
    const auto lockfile_edges = edge_keys_for_lockfile(lockfile);
    for (const auto &edge : graph.dependencies) {
        if (!lockfile_edges.contains(edge_key(edge.from, edge.dependency_key, edge.to))) {
            add_error(diagnostics,
                      std::string{kPackageGraph},
                      "lockfile is missing dependency edge " +
                          edge_text(edge.from, edge.dependency_key, edge.to));
        }
    }
    for (const auto &edge : lockfile.edges) {
        if (!graph_edges.contains(edge_key(edge.from, edge.dependency, edge.to))) {
            add_error(diagnostics,
                      std::string{kPackageGraph},
                      "lockfile contains unused dependency edge " +
                          edge_text(edge.from, edge.dependency, edge.to));
        }
    }

    return diagnostics;
}

std::vector<Diagnostic> check_lockfile_drift(const PackageGraph &graph, const Lockfile &lockfile) {
    return check_lockfile_drift(graph, lockfile, std::nullopt);
}

std::vector<Diagnostic> check_lockfile_drift(const PackageGraph &graph,
                                             const Lockfile &lockfile,
                                             const std::filesystem::path &lockfile_directory) {
    return check_lockfile_drift(
        graph, lockfile, std::optional<std::filesystem::path>{normalize_path(lockfile_directory)});
}

} // namespace ahfl::package_graph
