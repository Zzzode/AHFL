#include "compiler/package_graph/package_graph.hpp"

#include "base/json/json_value.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace ahfl::package_graph {
namespace {

[[nodiscard]] std::unique_ptr<json::JsonValue>
make_string_array(const std::vector<std::string> &values) {
    auto array = json::JsonValue::make_array();
    for (const auto &value : values) {
        array->push(json::JsonValue::make_string(value));
    }
    return array;
}

[[nodiscard]] std::unique_ptr<json::JsonValue> make_target_json(const TargetNode &target) {
    auto value = json::JsonValue::make_object();
    value->set("id", json::JsonValue::make_int(static_cast<std::int64_t>(target.id.value)));
    value->set("name", json::JsonValue::make_string(target.name));
    value->set("kind", json::JsonValue::make_string(target.kind));
    value->set("entry", json::JsonValue::make_string(target.entry));
    value->set("exports", make_string_array(target.exports));
    return value;
}

[[nodiscard]] std::unique_ptr<json::JsonValue> make_package_json(const PackageNode &package) {
    auto value = json::JsonValue::make_object();
    value->set("id", json::JsonValue::make_int(static_cast<std::int64_t>(package.id.value)));
    value->set("name", json::JsonValue::make_string(package.name));
    value->set("version", json::JsonValue::make_string(package.version));
    value->set("kind", json::JsonValue::make_string(package.kind));
    value->set("source",
               json::JsonValue::make_string(std::string{source_kind_name(package.source)}));
    value->set("module_prefix", json::JsonValue::make_string(package.module_prefix));
    value->set("package_root", json::JsonValue::make_string(package.package_root.generic_string()));
    value->set("module_root", json::JsonValue::make_string(package.module_root.generic_string()));
    value->set("manifest", json::JsonValue::make_string(package.manifest_path.generic_string()));
    value->set("checksum", json::JsonValue::make_string(package.checksum));
    value->set("exports", make_string_array(package.exported_modules));

    auto targets = json::JsonValue::make_array();
    for (const auto &target : package.targets) {
        targets->push(make_target_json(target));
    }
    value->set("targets", std::move(targets));
    return value;
}

[[nodiscard]] std::unique_ptr<json::JsonValue> make_dependency_json(const DependencyEdge &edge) {
    auto value = json::JsonValue::make_object();
    value->set("from", json::JsonValue::make_int(static_cast<std::int64_t>(edge.from.value)));
    value->set("dependency", json::JsonValue::make_string(edge.dependency_key));
    value->set("to", json::JsonValue::make_int(static_cast<std::int64_t>(edge.to.value)));
    value->set("source", json::JsonValue::make_string(edge.source));
    return value;
}

[[nodiscard]] std::unique_ptr<json::JsonValue> make_module_root_json(const ModuleRootEntry &entry) {
    auto value = json::JsonValue::make_object();
    value->set("prefix", json::JsonValue::make_string(entry.prefix));
    value->set("package",
               json::JsonValue::make_int(static_cast<std::int64_t>(entry.package.value)));
    value->set("root", json::JsonValue::make_string(entry.root.generic_string()));
    return value;
}

} // namespace

std::string serialize_package_graph_json(const PackageGraph &graph) {
    auto root = json::JsonValue::make_object();

    auto packages = json::JsonValue::make_array();
    for (const auto &package : graph.packages) {
        packages->push(make_package_json(package));
    }
    root->set("packages", std::move(packages));

    auto dependencies = json::JsonValue::make_array();
    for (const auto &dependency : graph.dependencies) {
        dependencies->push(make_dependency_json(dependency));
    }
    root->set("dependencies", std::move(dependencies));

    auto module_roots = json::JsonValue::make_array();
    for (const auto &entry : graph.module_roots) {
        module_roots->push(make_module_root_json(entry));
    }
    root->set("module_roots", std::move(module_roots));

    return json::serialize_json(*root);
}

} // namespace ahfl::package_graph
