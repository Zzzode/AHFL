#include "compiler/package_graph/package_graph.hpp"

#include "base/json/json_value.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

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

[[nodiscard]] std::unique_ptr<json::JsonValue>
make_handoff_export_array(const std::vector<manifest::HandoffExportManifest> &values) {
    auto array = json::JsonValue::make_array();
    for (const auto &export_item : values) {
        auto item = json::JsonValue::make_object();
        item->set("kind", json::JsonValue::make_string(export_item.kind));
        item->set("name", json::JsonValue::make_string(export_item.name));
        array->push(std::move(item));
    }
    return array;
}

[[nodiscard]] std::unique_ptr<json::JsonValue> make_target_json(const TargetNode &target) {
    auto value = json::JsonValue::make_object();
    value->set("id", json::JsonValue::make_int(static_cast<std::int64_t>(target.id.value)));
    value->set("name", json::JsonValue::make_string(target.name));
    value->set("kind", json::JsonValue::make_string(target.kind));
    value->set("entry", json::JsonValue::make_string(target.entry));
    value->set("exports", make_handoff_export_array(target.exports));
    auto capability_bindings = json::JsonValue::make_array();
    for (const auto &binding : target.capability_bindings) {
        auto item = json::JsonValue::make_object();
        item->set("capability", json::JsonValue::make_string(binding.capability));
        item->set("binding_key", json::JsonValue::make_string(binding.binding_key));
        capability_bindings->push(std::move(item));
    }
    value->set("capability_bindings", std::move(capability_bindings));
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
    if (package.registry.has_value()) {
        auto registry = json::JsonValue::make_object();
        registry->set("registry_id", json::JsonValue::make_string(package.registry->registry_id));
        registry->set("source_archive_sha256",
                      json::JsonValue::make_string(package.registry->source_archive_sha256));
        registry->set("manifest_sha256",
                      json::JsonValue::make_string(package.registry->manifest_sha256));
        registry->set("public_api_sha256",
                      json::JsonValue::make_string(package.registry->public_api_sha256));
        value->set("registry", std::move(registry));
    }
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
    if (edge.registry_id.has_value()) {
        value->set("registry_id", json::JsonValue::make_string(*edge.registry_id));
    }
    if (edge.version_requirement.has_value()) {
        value->set("version_requirement", json::JsonValue::make_string(*edge.version_requirement));
    }
    if (edge.selected_version.has_value()) {
        value->set("selected_version", json::JsonValue::make_string(*edge.selected_version));
    }
    if (edge.source_archive_sha256.has_value()) {
        value->set("source_archive_sha256",
                   json::JsonValue::make_string(*edge.source_archive_sha256));
    }
    if (edge.manifest_sha256.has_value()) {
        value->set("manifest_sha256", json::JsonValue::make_string(*edge.manifest_sha256));
    }
    if (edge.public_api_sha256.has_value()) {
        value->set("public_api_sha256", json::JsonValue::make_string(*edge.public_api_sha256));
    }
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

[[nodiscard]] std::unique_ptr<json::JsonValue> make_source_unit_json(const SourceUnitNode &unit) {
    auto value = json::JsonValue::make_object();
    value->set("id", json::JsonValue::make_int(static_cast<std::int64_t>(unit.id.value)));
    value->set("package", json::JsonValue::make_int(static_cast<std::int64_t>(unit.package.value)));
    value->set("path", json::JsonValue::make_string(unit.path.generic_string()));
    value->set("module_path", json::JsonValue::make_string(unit.module_path));
    auto roles = json::JsonValue::make_array();
    for (const auto role : unit.roles) {
        roles->push(json::JsonValue::make_string(std::string{source_unit_role_name(role)}));
    }
    value->set("roles", std::move(roles));
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

    auto source_units = json::JsonValue::make_array();
    for (const auto &source_unit : graph.source_units) {
        source_units->push(make_source_unit_json(source_unit));
    }
    root->set("source_units", std::move(source_units));

    return json::serialize_json(*root);
}

} // namespace ahfl::package_graph
