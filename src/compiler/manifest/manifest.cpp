#include "compiler/manifest/manifest.hpp"

#include "base/toml/toml.hpp"

#include <algorithm>
#include <filesystem>
#include <regex>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace ahfl::manifest {
namespace {

using toml::Entry;
using toml::Value;
using toml::ValueKind;

constexpr std::string_view kSyntax = "E::manifest_syntax";
constexpr std::string_view kUnknownField = "E::manifest_unknown_field";
constexpr std::string_view kType = "E::manifest_type";
constexpr std::string_view kRequired = "E::manifest_required";
constexpr std::string_view kInvalidValue = "E::manifest_invalid_value";
constexpr std::string_view kPathEscape = "E::manifest_path_escape";
constexpr std::string_view kPackageGraph = "E::package_graph";

[[nodiscard]] bool contains(std::initializer_list<std::string_view> values,
                            std::string_view candidate) {
    return std::find(values.begin(), values.end(), candidate) != values.end();
}

void add_diag(std::vector<ManifestDiagnostic> &diagnostics,
              std::string_view code,
              std::string message,
              SourceRange range) {
    diagnostics.push_back(ManifestDiagnostic{
        .code = std::string{code},
        .message = std::move(message),
        .range = range,
    });
}

void reject_empty_string(std::string_view display,
                         SourceRange range,
                         std::vector<ManifestDiagnostic> &diagnostics) {
    add_diag(diagnostics,
             kInvalidValue,
             "manifest field '" + std::string(display) + "' must not be empty",
             range);
}

void reject_empty_string_array_item(std::string_view display,
                                    SourceRange range,
                                    std::vector<ManifestDiagnostic> &diagnostics) {
    add_diag(diagnostics,
             kInvalidValue,
             "manifest field '" + std::string(display) + "' must not contain empty strings",
             range);
}

void copy_toml_diagnostics(const toml::Document &document,
                           std::vector<ManifestDiagnostic> &diagnostics) {
    for (const auto &diag : document.diagnostics) {
        add_diag(diagnostics, kSyntax, diag.message, diag.range);
    }
}

[[nodiscard]] const Entry *find_entry(const Value &table, std::string_view key) {
    for (const auto &entry : table.table_fields) {
        if (entry.key == key) {
            return &entry;
        }
    }
    return nullptr;
}

[[nodiscard]] const Value *find_value(const Value &table, std::string_view key) {
    const auto *entry = find_entry(table, key);
    return entry == nullptr ? nullptr : entry->value.get();
}

void reject_unknown_fields(const Value &table,
                           std::initializer_list<std::string_view> allowed,
                           std::string_view context,
                           std::vector<ManifestDiagnostic> &diagnostics) {
    for (const auto &entry : table.table_fields) {
        if (!contains(allowed, entry.key)) {
            add_diag(diagnostics,
                     kUnknownField,
                     "unsupported manifest field '" + std::string(context) + entry.key + "'",
                     entry.key_range);
        }
    }
}

[[nodiscard]] std::optional<std::string>
read_required_string(const Value &table,
                     std::string_view key,
                     std::string_view display,
                     std::vector<ManifestDiagnostic> &diagnostics) {
    const auto *entry = find_entry(table, key);
    if (entry == nullptr) {
        add_diag(diagnostics,
                 kRequired,
                 "manifest is missing required field '" + std::string(display) + "'",
                 table.range);
        return std::nullopt;
    }
    if (entry->value->kind != ValueKind::String) {
        add_diag(diagnostics,
                 kType,
                 "manifest field '" + std::string(display) + "' must be a string",
                 entry->value_range);
        return std::nullopt;
    }
    if (entry->value->string_value.empty()) {
        reject_empty_string(display, entry->value_range, diagnostics);
        return std::nullopt;
    }
    return entry->value->string_value;
}

[[nodiscard]] std::optional<int> read_required_int(const Value &table,
                                                   std::string_view key,
                                                   std::string_view display,
                                                   std::vector<ManifestDiagnostic> &diagnostics) {
    const auto *entry = find_entry(table, key);
    if (entry == nullptr) {
        add_diag(diagnostics,
                 kRequired,
                 "manifest is missing required field '" + std::string(display) + "'",
                 table.range);
        return std::nullopt;
    }
    if (entry->value->kind != ValueKind::Integer) {
        add_diag(diagnostics,
                 kType,
                 "manifest field '" + std::string(display) + "' must be an integer",
                 entry->value_range);
        return std::nullopt;
    }
    return static_cast<int>(entry->value->integer_value);
}

[[nodiscard]] std::vector<std::string>
read_string_array(const Value &table,
                  std::string_view key,
                  std::string_view display,
                  bool required,
                  std::vector<ManifestDiagnostic> &diagnostics) {
    const auto *entry = find_entry(table, key);
    if (entry == nullptr) {
        if (required) {
            add_diag(diagnostics,
                     kRequired,
                     "manifest is missing required field '" + std::string(display) + "'",
                     table.range);
        }
        return {};
    }
    if (entry->value->kind != ValueKind::Array) {
        add_diag(diagnostics,
                 kType,
                 "manifest field '" + std::string(display) + "' must be an array",
                 entry->value_range);
        return {};
    }

    std::vector<std::string> values;
    values.reserve(entry->value->array_items.size());
    for (const auto &item : entry->value->array_items) {
        if (item->kind != ValueKind::String) {
            add_diag(diagnostics,
                     kType,
                     "manifest field '" + std::string(display) + "' must contain only strings",
                     item->range);
            continue;
        }
        if (item->string_value.empty()) {
            reject_empty_string_array_item(display, item->range, diagnostics);
            continue;
        }
        values.push_back(item->string_value);
    }
    return values;
}

[[nodiscard]] std::vector<HandoffExportManifest>
read_handoff_exports(const Value &table,
                     std::string_view key,
                     std::string_view display,
                     bool required,
                     std::vector<ManifestDiagnostic> &diagnostics) {
    const auto *entry = find_entry(table, key);
    if (entry == nullptr) {
        if (required) {
            add_diag(diagnostics,
                     kRequired,
                     "manifest is missing required field '" + std::string(display) + "'",
                     table.range);
        }
        return {};
    }
    if (entry->value->kind != ValueKind::Array) {
        add_diag(diagnostics,
                 kType,
                 "manifest field '" + std::string(display) + "' must be an array",
                 entry->value_range);
        return {};
    }

    std::vector<HandoffExportManifest> exports;
    exports.reserve(entry->value->array_items.size());
    for (const auto &item : entry->value->array_items) {
        if (item->kind != ValueKind::InlineTable && item->kind != ValueKind::Table) {
            add_diag(diagnostics,
                     kType,
                     "manifest field '" + std::string(display) + "' items must be tables",
                     item->range);
            continue;
        }

        reject_unknown_fields(*item, {"kind", "name"}, "targets.exports.", diagnostics);
        HandoffExportManifest export_item;
        export_item.range = item->range;
        if (auto kind = read_required_string(*item, "kind", "targets.exports.kind", diagnostics);
            kind.has_value()) {
            export_item.kind = *kind;
        }
        if (auto name = read_required_string(*item, "name", "targets.exports.name", diagnostics);
            name.has_value()) {
            export_item.name = *name;
        }
        if (!contains({"workflow", "agent"}, export_item.kind) && !export_item.kind.empty()) {
            add_diag(diagnostics,
                     kInvalidValue,
                     "target export kind must be workflow or agent",
                     item->range);
        }
        exports.push_back(std::move(export_item));
    }
    return exports;
}

[[nodiscard]] bool matches_regex(std::string_view value, const char *pattern) {
    const std::regex regex{pattern};
    return std::regex_match(value.begin(), value.end(), regex);
}

void validate_kebab(std::string_view value,
                    std::string_view display,
                    SourceRange range,
                    std::vector<ManifestDiagnostic> &diagnostics) {
    if (!matches_regex(value, "^[a-z][a-z0-9]*(?:-[a-z0-9]+)*$")) {
        add_diag(diagnostics, kInvalidValue, std::string(display) + " must be kebab-case", range);
    }
}

void validate_target_name(std::string_view value,
                          SourceRange range,
                          std::vector<ManifestDiagnostic> &diagnostics) {
    if (value.empty()) {
        add_diag(diagnostics, kInvalidValue, "target name must not be empty", range);
        return;
    }
    const bool is_kebab = matches_regex(value, "^[a-z][a-z0-9]*(?:-[a-z0-9]+)*$");
    const bool is_snake = matches_regex(value, "^[a-z][a-z0-9]*(?:_[a-z0-9]+)*$");
    if (!is_kebab && !is_snake) {
        add_diag(diagnostics, kInvalidValue, "target name must be kebab-case or snake_case", range);
    }
}

void validate_module_prefix(std::string_view value,
                            SourceRange range,
                            std::vector<ManifestDiagnostic> &diagnostics) {
    if (!matches_regex(value, "^[A-Za-z_][A-Za-z0-9_]*$")) {
        add_diag(diagnostics,
                 kInvalidValue,
                 "module.prefix must be an AHFL module identifier without '::'",
                 range);
    }
}

void validate_canonical_symbol_name(std::string_view value,
                                    std::string_view display,
                                    SourceRange range,
                                    std::vector<ManifestDiagnostic> &diagnostics) {
    if (!matches_regex(value, "^[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)+$")) {
        add_diag(diagnostics,
                 kInvalidValue,
                 std::string(display) + " must be a canonical AHFL symbol name for handoff targets",
                 range);
    }
}

void validate_semver(std::string_view value,
                     std::string_view display,
                     SourceRange range,
                     std::vector<ManifestDiagnostic> &diagnostics) {
    if (!matches_regex(value, "^[0-9]+\\.[0-9]+\\.[0-9]+$")) {
        add_diag(diagnostics,
                 kInvalidValue,
                 std::string(display) + " must be an exact semantic version",
                 range);
    }
}

void validate_relative_path(std::string_view value,
                            std::string_view display,
                            SourceRange range,
                            std::vector<ManifestDiagnostic> &diagnostics) {
    const std::filesystem::path path{std::string{value}};
    const bool has_parent_escape =
        std::any_of(path.begin(), path.end(), [](const auto &part) { return part == ".."; });
    if (value.empty() || path.is_absolute() || has_parent_escape) {
        add_diag(diagnostics,
                 kPathEscape,
                 "manifest field '" + std::string(display) + "' must not escape package root",
                 range);
    }
}

[[nodiscard]] DependencySpec read_dependency(std::string_view key,
                                             const Value &value,
                                             SourceRange key_range,
                                             SourceRange range,
                                             std::vector<ManifestDiagnostic> &diagnostics) {
    DependencySpec spec;
    spec.key = std::string{key};
    spec.range = range;

    if (key.empty()) {
        add_diag(diagnostics, kInvalidValue, "dependency key must not be empty", key_range);
    } else {
        validate_kebab(key, "dependency key", key_range, diagnostics);
    }

    if (value.kind != ValueKind::InlineTable && value.kind != ValueKind::Table) {
        add_diag(diagnostics,
                 kType,
                 "dependency '" + std::string(key) + "' must be an inline table",
                 range);
        return spec;
    }

    reject_unknown_fields(
        value, {"source", "path", "version"}, "dependencies." + spec.key + ".", diagnostics);

    if (auto source = read_required_string(value, "source", "dependencies.source", diagnostics);
        source.has_value()) {
        spec.source = *source;
    }
    if (const auto *path = find_entry(value, "path"); path != nullptr) {
        if (path->value->kind == ValueKind::String) {
            spec.path = path->value->string_value;
            if (spec.path->empty()) {
                reject_empty_string("dependencies.path", path->value_range, diagnostics);
            }
        } else {
            add_diag(diagnostics, kType, "dependency path must be a string", path->value->range);
        }
    }
    if (const auto *version = find_entry(value, "version"); version != nullptr) {
        if (version->value->kind == ValueKind::String) {
            spec.version = version->value->string_value;
            if (spec.version->empty()) {
                reject_empty_string("dependencies.version", version->value_range, diagnostics);
            }
        } else {
            add_diag(
                diagnostics, kType, "dependency version must be a string", version->value->range);
        }
    }

    if (spec.source == "sysroot") {
        if (spec.key != "std") {
            add_diag(diagnostics, kPackageGraph, "sysroot dependency key must be 'std'", range);
        }
        if (spec.path.has_value() || spec.version.has_value()) {
            add_diag(diagnostics,
                     kInvalidValue,
                     "sysroot dependency must not declare path or version",
                     range);
        }
    } else if (spec.source == "path") {
        if (!spec.path.has_value()) {
            add_diag(
                diagnostics, kRequired, "path dependency is missing required field 'path'", range);
        }
    } else if (spec.source == "workspace") {
        if (spec.path.has_value() || spec.version.has_value()) {
            add_diag(diagnostics,
                     kInvalidValue,
                     "workspace dependency must not declare path or version",
                     range);
        }
    } else if (!spec.source.empty()) {
        add_diag(diagnostics,
                 kInvalidValue,
                 "unsupported dependency source '" + spec.source + "'",
                 range);
    }

    if (spec.version.has_value() && !spec.version->empty()) {
        validate_semver(*spec.version, "dependency.version", range, diagnostics);
    }

    return spec;
}

[[nodiscard]] std::vector<DependencySpec>
read_dependencies(const Value &root, std::vector<ManifestDiagnostic> &diagnostics) {
    std::vector<DependencySpec> dependencies;
    const auto *deps = find_value(root, "dependencies");
    if (deps == nullptr) {
        return dependencies;
    }
    if (deps->kind != ValueKind::Table) {
        add_diag(diagnostics, kType, "manifest field 'dependencies' must be a table", deps->range);
        return dependencies;
    }
    for (const auto &entry : deps->table_fields) {
        dependencies.push_back(read_dependency(
            entry.key, *entry.value, entry.key_range, entry.value_range, diagnostics));
    }
    return dependencies;
}

[[nodiscard]] std::vector<TargetManifest>
read_targets(const Value &root, std::vector<ManifestDiagnostic> &diagnostics) {
    std::vector<TargetManifest> targets;
    const auto *targets_table = find_value(root, "targets");
    if (targets_table == nullptr) {
        return targets;
    }
    if (targets_table->kind != ValueKind::Table) {
        add_diag(
            diagnostics, kType, "manifest field 'targets' must be a table", targets_table->range);
        return targets;
    }

    std::unordered_set<std::string> normalized_names;
    for (const auto &entry : targets_table->table_fields) {
        if (entry.value->kind != ValueKind::Table) {
            add_diag(
                diagnostics, kType, "target manifest entry must be a table", entry.value_range);
            continue;
        }
        reject_unknown_fields(*entry.value,
                              {"kind", "entry", "exports", "capability_bindings"},
                              "targets." + entry.key + ".",
                              diagnostics);

        TargetManifest target;
        target.name = entry.key;
        target.range = entry.value_range;
        validate_target_name(target.name, entry.key_range, diagnostics);
        std::string normalized = target.name;
        std::replace(normalized.begin(), normalized.end(), '-', '_');
        if (!normalized_names.insert(normalized).second) {
            add_diag(diagnostics,
                     kInvalidValue,
                     "target name normalizes to a duplicate identifier",
                     entry.key_range);
        }

        if (auto kind = read_required_string(*entry.value, "kind", "targets.kind", diagnostics);
            kind.has_value()) {
            target.kind = *kind;
        }
        if (auto target_entry =
                read_required_string(*entry.value, "entry", "targets.entry", diagnostics);
            target_entry.has_value()) {
            target.entry = *target_entry;
            const auto *entry_field = find_entry(*entry.value, "entry");
            const auto entry_range =
                entry_field == nullptr ? entry.value_range : entry_field->value_range;
            if (contains({"library", "test"}, target.kind)) {
                validate_relative_path(target.entry, "targets.entry", entry_range, diagnostics);
            } else if (target.kind == "handoff") {
                validate_canonical_symbol_name(
                    target.entry, "targets.entry", entry_range, diagnostics);
            }
        }
        const auto *target_exports = find_value(*entry.value, "exports");
        if (target_exports != nullptr && contains({"library", "test"}, target.kind)) {
            add_diag(diagnostics,
                     kInvalidValue,
                     "targets.exports are only allowed on handoff targets",
                     target_exports->range);
        }
        target.exports = read_handoff_exports(
            *entry.value, "exports", "targets.exports", target.kind == "handoff", diagnostics);

        const auto *bindings = find_value(*entry.value, "capability_bindings");
        if (bindings != nullptr) {
            if (target.kind != "handoff") {
                add_diag(diagnostics,
                         kInvalidValue,
                         "capability_bindings are only allowed on handoff targets",
                         bindings->range);
            }
            if (bindings->kind != ValueKind::Array) {
                add_diag(diagnostics,
                         kType,
                         "capability_bindings must be an array of tables",
                         bindings->range);
            } else {
                for (const auto &item : bindings->array_items) {
                    if (item->kind != ValueKind::Table) {
                        add_diag(diagnostics,
                                 kType,
                                 "capability_bindings item must be a table",
                                 item->range);
                        continue;
                    }
                    reject_unknown_fields(*item,
                                          {"capability", "binding_key"},
                                          "targets.capability_bindings.",
                                          diagnostics);
                    CapabilityBindingManifest binding;
                    binding.range = item->range;
                    if (auto capability = read_required_string(
                            *item, "capability", "capability_bindings.capability", diagnostics);
                        capability.has_value()) {
                        binding.capability = *capability;
                    }
                    if (auto binding_key = read_required_string(
                            *item, "binding_key", "capability_bindings.binding_key", diagnostics);
                        binding_key.has_value()) {
                        binding.binding_key = *binding_key;
                    }
                    target.capability_bindings.push_back(std::move(binding));
                }
            }
        }

        if (!contains({"library", "handoff", "test"}, target.kind) && !target.kind.empty()) {
            add_diag(diagnostics,
                     kInvalidValue,
                     "target kind must be library, handoff, or test",
                     entry.value_range);
        }

        targets.push_back(std::move(target));
    }
    return targets;
}

} // namespace

ManifestResult<PackageManifest> parse_package_manifest(std::string_view input) {
    auto parsed = toml::parse(input);
    ManifestResult<PackageManifest> result;
    copy_toml_diagnostics(parsed.document, result.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    const Value &root = parsed.document.root;
    reject_unknown_fields(root,
                          {"manifest_version",
                           "package",
                           "module",
                           "exports",
                           "targets",
                           "dependencies",
                           "prelude",
                           "compiler_intrinsics"},
                          "",
                          result.diagnostics);

    PackageManifest manifest;
    if (auto version =
            read_required_int(root, "manifest_version", "manifest_version", result.diagnostics);
        version.has_value()) {
        manifest.manifest_version = *version;
        if (*version != 1) {
            const auto *entry = find_entry(root, "manifest_version");
            add_diag(result.diagnostics,
                     kInvalidValue,
                     "manifest_version must be 1",
                     entry == nullptr ? root.range : entry->value_range);
        }
    }

    const auto *package = find_value(root, "package");
    if (package == nullptr || package->kind != ValueKind::Table) {
        add_diag(result.diagnostics,
                 kRequired,
                 "manifest is missing required table 'package'",
                 root.range);
    } else {
        reject_unknown_fields(
            *package, {"name", "version", "edition", "kind"}, "package.", result.diagnostics);
        if (auto name = read_required_string(*package, "name", "package.name", result.diagnostics);
            name.has_value()) {
            manifest.package_name = *name;
            const auto *entry = find_entry(*package, "name");
            validate_kebab(*name, "package.name", entry->value_range, result.diagnostics);
        }
        if (auto version =
                read_required_string(*package, "version", "package.version", result.diagnostics);
            version.has_value()) {
            manifest.package_version = *version;
            const auto *entry = find_entry(*package, "version");
            validate_semver(*version, "package.version", entry->value_range, result.diagnostics);
        }
        if (auto edition =
                read_required_string(*package, "edition", "package.edition", result.diagnostics);
            edition.has_value()) {
            manifest.edition = *edition;
            if (*edition != "2026") {
                const auto *entry = find_entry(*package, "edition");
                add_diag(result.diagnostics,
                         kInvalidValue,
                         "package.edition must be \"2026\"",
                         entry->value_range);
            }
        }
        if (auto kind = read_required_string(*package, "kind", "package.kind", result.diagnostics);
            kind.has_value()) {
            manifest.package_kind = *kind;
            if (!contains({"library", "application", "standard-library"}, *kind)) {
                const auto *entry = find_entry(*package, "kind");
                add_diag(result.diagnostics,
                         kInvalidValue,
                         "package.kind must be library, application, or standard-library",
                         entry->value_range);
            }
        }
    }

    const auto *module = find_value(root, "module");
    if (module == nullptr || module->kind != ValueKind::Table) {
        add_diag(result.diagnostics,
                 kRequired,
                 "manifest is missing required table 'module'",
                 root.range);
    } else {
        reject_unknown_fields(*module, {"prefix", "root"}, "module.", result.diagnostics);
        if (auto prefix =
                read_required_string(*module, "prefix", "module.prefix", result.diagnostics);
            prefix.has_value()) {
            manifest.module_prefix = *prefix;
            const auto *entry = find_entry(*module, "prefix");
            validate_module_prefix(*prefix, entry->value_range, result.diagnostics);
        }
        if (auto root_path =
                read_required_string(*module, "root", "module.root", result.diagnostics);
            root_path.has_value()) {
            manifest.module_root = *root_path;
            const auto *entry = find_entry(*module, "root");
            validate_relative_path(
                *root_path, "module.root", entry->value_range, result.diagnostics);
        }
    }

    const auto *exports = find_value(root, "exports");
    if (exports == nullptr) {
        if (manifest.package_kind == "library" || manifest.package_kind == "standard-library") {
            add_diag(result.diagnostics,
                     kRequired,
                     "library and standard-library packages require 'exports.modules'",
                     root.range);
        }
    } else if (exports->kind != ValueKind::Table) {
        add_diag(
            result.diagnostics, kType, "manifest field 'exports' must be a table", exports->range);
    } else {
        reject_unknown_fields(*exports, {"modules"}, "exports.", result.diagnostics);
        manifest.exported_modules = read_string_array(
            *exports,
            "modules",
            "exports.modules",
            manifest.package_kind == "library" || manifest.package_kind == "standard-library",
            result.diagnostics);
    }

    if (const auto *prelude = find_value(root, "prelude"); prelude != nullptr) {
        if (prelude->kind != ValueKind::Table) {
            add_diag(result.diagnostics,
                     kType,
                     "manifest field 'prelude' must be a table",
                     prelude->range);
        } else {
            reject_unknown_fields(
                *prelude, {"module", "injection"}, "prelude.", result.diagnostics);
            manifest.prelude_module =
                read_required_string(*prelude, "module", "prelude.module", result.diagnostics);
            manifest.prelude_injection = read_required_string(
                *prelude, "injection", "prelude.injection", result.diagnostics);
            if (manifest.prelude_injection.has_value() &&
                *manifest.prelude_injection != "explicit") {
                const auto *entry = find_entry(*prelude, "injection");
                add_diag(result.diagnostics,
                         kInvalidValue,
                         "prelude.injection must be explicit",
                         entry->value_range);
            }
            if (manifest.package_kind != "standard-library") {
                add_diag(result.diagnostics,
                         kInvalidValue,
                         "prelude is only allowed for standard-library packages",
                         prelude->range);
            }
        }
    }

    if (const auto *intrinsics = find_value(root, "compiler_intrinsics"); intrinsics != nullptr) {
        if (intrinsics->kind != ValueKind::Table) {
            add_diag(result.diagnostics,
                     kType,
                     "manifest field 'compiler_intrinsics' must be a table",
                     intrinsics->range);
        } else {
            reject_unknown_fields(
                *intrinsics, {"allow"}, "compiler_intrinsics.", result.diagnostics);
            manifest.compiler_intrinsics_allow = read_string_array(
                *intrinsics, "allow", "compiler_intrinsics.allow", false, result.diagnostics);
            if (manifest.package_kind != "standard-library") {
                add_diag(result.diagnostics,
                         kInvalidValue,
                         "compiler_intrinsics is only allowed for standard-library packages",
                         intrinsics->range);
            }
        }
    }

    manifest.targets = read_targets(root, result.diagnostics);
    if (manifest.package_kind != "standard-library" && manifest.targets.empty()) {
        add_diag(result.diagnostics,
                 kRequired,
                 "non standard-library package must declare at least one target",
                 root.range);
    }
    manifest.dependencies = read_dependencies(root, result.diagnostics);

    if (!result.has_errors()) {
        result.manifest = std::move(manifest);
    }
    return result;
}

ManifestResult<WorkspaceManifest> parse_workspace_manifest(std::string_view input) {
    auto parsed = toml::parse(input);
    ManifestResult<WorkspaceManifest> result;
    copy_toml_diagnostics(parsed.document, result.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    const Value &root = parsed.document.root;
    reject_unknown_fields(root,
                          {"manifest_version", "workspace", "resolver", "dependencies"},
                          "",
                          result.diagnostics);

    WorkspaceManifest manifest;
    if (auto version =
            read_required_int(root, "manifest_version", "manifest_version", result.diagnostics);
        version.has_value()) {
        manifest.manifest_version = *version;
        if (*version != 1) {
            const auto *entry = find_entry(root, "manifest_version");
            add_diag(result.diagnostics,
                     kInvalidValue,
                     "manifest_version must be 1",
                     entry == nullptr ? root.range : entry->value_range);
        }
    }

    const auto *workspace = find_value(root, "workspace");
    if (workspace == nullptr || workspace->kind != ValueKind::Table) {
        add_diag(result.diagnostics,
                 kRequired,
                 "workspace manifest is missing required table 'workspace'",
                 root.range);
    } else {
        reject_unknown_fields(*workspace, {"name", "members"}, "workspace.", result.diagnostics);
        if (auto name =
                read_required_string(*workspace, "name", "workspace.name", result.diagnostics);
            name.has_value()) {
            manifest.workspace_name = *name;
            const auto *entry = find_entry(*workspace, "name");
            validate_kebab(*name, "workspace.name", entry->value_range, result.diagnostics);
        }
        manifest.members =
            read_string_array(*workspace, "members", "workspace.members", true, result.diagnostics);
        for (const auto &member : manifest.members) {
            validate_relative_path(
                member, "workspace.members", workspace->range, result.diagnostics);
        }
        if (manifest.members.empty()) {
            add_diag(result.diagnostics,
                     kInvalidValue,
                     "workspace.members must not be empty",
                     workspace->range);
        }
    }

    const auto *resolver = find_value(root, "resolver");
    if (resolver == nullptr || resolver->kind != ValueKind::Table) {
        add_diag(result.diagnostics,
                 kRequired,
                 "workspace manifest is missing required table 'resolver'",
                 root.range);
    } else {
        reject_unknown_fields(*resolver, {"version"}, "resolver.", result.diagnostics);
        if (auto version =
                read_required_int(*resolver, "version", "resolver.version", result.diagnostics);
            version.has_value()) {
            manifest.resolver_version = *version;
            if (*version != 1) {
                const auto *entry = find_entry(*resolver, "version");
                add_diag(result.diagnostics,
                         kInvalidValue,
                         "resolver.version must be 1",
                         entry->value_range);
            }
        }
    }

    manifest.dependencies = read_dependencies(root, result.diagnostics);
    if (!result.has_errors()) {
        result.manifest = std::move(manifest);
    }
    return result;
}

} // namespace ahfl::manifest
