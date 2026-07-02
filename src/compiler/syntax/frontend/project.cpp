#include "ahfl/compiler/frontend/frontend.hpp"
#include "base/json/json_value.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ahfl {

namespace {

constexpr std::string_view kStdPreludeModule = "std::prelude";

[[nodiscard]] SourceRange
clamp_range(std::size_t begin_offset, std::size_t end_offset, const SourceFile &source) {
    const auto bounded_begin = std::min(begin_offset, source.content.size());
    const auto bounded_end = std::max(bounded_begin, std::min(end_offset, source.content.size()));

    return SourceRange{
        .begin_offset = bounded_begin,
        .end_offset = bounded_end,
    };
}

struct ProgramImports {
    std::vector<std::pair<std::string, SourceRange>> modules;
    std::vector<ImportRequest> imports;
};

[[nodiscard]] ProgramImports collect_program_imports(const ast::Program &program) {
    ProgramImports result;

    for (const auto &declaration : program.declarations) {
        switch (declaration->kind) {
        case ast::NodeKind::ModuleDecl: {
            const auto &module = static_cast<const ast::ModuleDecl &>(*declaration);
            result.modules.push_back({module.name ? module.name->spelling() : "", module.range});
            break;
        }
        case ast::NodeKind::ImportDecl: {
            const auto &import = static_cast<const ast::ImportDecl &>(*declaration);
            result.imports.push_back(ImportRequest{
                .module_name = import.path ? import.path->spelling() : "",
                .alias = import.alias,
                .range = import.range,
            });
            break;
        }
        default:
            break;
        }
    }

    return result;
}

[[nodiscard]] std::filesystem::path normalize_path(const std::filesystem::path &path) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    const auto candidate = error ? path.lexically_normal() : absolute.lexically_normal();
    if (!std::filesystem::exists(candidate, error)) {
        return candidate;
    }

    const auto canonical = std::filesystem::weakly_canonical(candidate, error);
    return error ? candidate : canonical.lexically_normal();
}

[[nodiscard]] SourceRange point_range(const SourceFile &source, std::size_t offset) {
    return clamp_range(offset, std::min(offset + 1, source.content.size()), source);
}

[[nodiscard]] bool path_has_prefix(const std::filesystem::path &prefix,
                                   const std::filesystem::path &candidate) {
    auto prefix_it = prefix.begin();
    auto candidate_it = candidate.begin();
    while (prefix_it != prefix.end() && candidate_it != candidate.end()) {
        if (*prefix_it != *candidate_it) {
            return false;
        }
        ++prefix_it;
        ++candidate_it;
    }

    return prefix_it == prefix.end();
}

[[nodiscard]] bool read_text_file(const std::filesystem::path &path,
                                  SourceFile &source,
                                  DiagnosticBag &diagnostics,
                                  std::string_view kind) {
    source.display_name = display_path(path);

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        diagnostics.error()
            .message("failed to open " + std::string(kind) + ": " + display_path(path))
            .emit();
        return false;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    source.content = buffer.str();
    source.invalidate_line_starts_cache();
    return true;
}

void append_unique_normalized_path(std::vector<std::filesystem::path> &paths,
                                   const std::filesystem::path &path) {
    const auto normalized = normalize_path(path);
    if (std::find(paths.begin(), paths.end(), normalized) == paths.end()) {
        paths.push_back(normalized);
    }
}

[[nodiscard]] SourceRange json_value_range(const SourceFile &source, const json::JsonValue *value) {
    if (value == nullptr) {
        return point_range(source, 0);
    }

    const auto end_offset =
        value->end_offset > value->begin_offset ? value->end_offset : value->begin_offset + 1;
    return clamp_range(value->begin_offset, end_offset, source);
}

void descriptor_error(DiagnosticBag &diagnostics,
                      const SourceFile &source,
                      std::string message,
                      const json::JsonValue *value = nullptr) {
    diagnostics.error()
        .message(std::move(message))
        .range(json_value_range(source, value))
        .source(source)
        .emit();
}

[[nodiscard]] bool starts_with_json_object(std::string_view content) {
    for (const auto ch : content) {
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            continue;
        }
        return ch == '{';
    }
    return false;
}

[[nodiscard]] std::optional<std::unique_ptr<json::JsonValue>>
parse_descriptor_json(const SourceFile &source,
                      DiagnosticBag &diagnostics,
                      std::string_view begin_message,
                      std::string_view valid_json_message) {
    auto parsed = json::parse_json(source.content);
    if (!parsed.has_value()) {
        descriptor_error(diagnostics,
                         source,
                         starts_with_json_object(source.content) ? std::string(valid_json_message)
                                                                 : std::string(begin_message));
        return std::nullopt;
    }

    if (!(**parsed).is_object()) {
        descriptor_error(diagnostics, source, std::string(begin_message), (*parsed).get());
        return std::nullopt;
    }

    return parsed;
}

[[nodiscard]] bool field_is_allowed(std::string_view field_name,
                                    std::initializer_list<std::string_view> allowed) {
    return std::find(allowed.begin(), allowed.end(), field_name) != allowed.end();
}

void reject_unknown_descriptor_fields(const json::JsonValue &object,
                                      DiagnosticBag &diagnostics,
                                      const SourceFile &source,
                                      std::string_view descriptor_name,
                                      std::initializer_list<std::string_view> allowed) {
    for (const auto &[field_name, value] : object.object_fields) {
        if (!field_is_allowed(field_name, allowed)) {
            descriptor_error(diagnostics,
                             source,
                             "unsupported " + std::string(descriptor_name) + " field '" +
                                 field_name + "'",
                             value.get());
        }
    }
}

[[nodiscard]] std::optional<std::string> read_json_string(const json::JsonValue &value,
                                                          DiagnosticBag &diagnostics,
                                                          const SourceFile &source,
                                                          std::string message) {
    const auto string_value = value.as_string();
    if (!string_value.has_value()) {
        descriptor_error(diagnostics, source, std::move(message), &value);
        return std::nullopt;
    }
    return std::string(*string_value);
}

[[nodiscard]] std::optional<std::string> required_string_field(std::string_view descriptor_name,
                                                               std::string_view field_name,
                                                               const json::JsonValue &object,
                                                               DiagnosticBag &diagnostics,
                                                               const SourceFile &source) {
    const auto key = std::string(field_name);
    const auto *field = object.get(field_name);
    if (field == nullptr) {
        descriptor_error(diagnostics,
                         source,
                         std::string(descriptor_name) + " is missing required field '" + key + "'");
        return std::nullopt;
    }

    return read_json_string(*field,
                            diagnostics,
                            source,
                            std::string(descriptor_name) + " field '" + key + "' must be a string");
}

[[nodiscard]] std::optional<std::vector<std::string>>
required_string_array_field(std::string_view descriptor_name,
                            std::string_view field_name,
                            const json::JsonValue &object,
                            DiagnosticBag &diagnostics,
                            const SourceFile &source) {
    const auto key = std::string(field_name);
    const auto *field = object.get(field_name);
    if (field == nullptr) {
        descriptor_error(diagnostics,
                         source,
                         std::string(descriptor_name) + " is missing required field '" + key + "'");
        return std::nullopt;
    }

    if (!field->is_array()) {
        descriptor_error(diagnostics,
                         source,
                         std::string(descriptor_name) + " field '" + key +
                             "' must be an array of strings",
                         field);
        return std::nullopt;
    }

    std::vector<std::string> values;
    values.reserve(field->array_items.size());
    for (const auto &item : field->array_items) {
        auto value = read_json_string(*item,
                                      diagnostics,
                                      source,
                                      std::string(descriptor_name) + " field '" + key +
                                          "' must be an array of strings");
        if (!value.has_value()) {
            return std::nullopt;
        }
        values.push_back(std::move(*value));
    }

    return values;
}

void reject_unknown_project_fields(const json::JsonValue &object,
                                   DiagnosticBag &diagnostics,
                                   const SourceFile &source) {
    reject_unknown_descriptor_fields(object,
                                     diagnostics,
                                     source,
                                     "project descriptor",
                                     {
                                         "format_version",
                                         "name",
                                         "search_roots",
                                         "entry_sources",
                                     });
}

void reject_unknown_workspace_fields(const json::JsonValue &object,
                                     DiagnosticBag &diagnostics,
                                     const SourceFile &source) {
    reject_unknown_descriptor_fields(object,
                                     diagnostics,
                                     source,
                                     "workspace descriptor",
                                     {
                                         "format_version",
                                         "name",
                                         "projects",
                                     });
}

class PackageAuthoringJsonReader {
  public:
    PackageAuthoringJsonReader(const json::JsonValue &root,
                               const SourceFile &source,
                               DiagnosticBag &diagnostics)
        : root_(root), source_(source), diagnostics_(diagnostics) {}

    [[nodiscard]] std::optional<PackageAuthoringDescriptor>
    parse_descriptor(const std::filesystem::path &descriptor_path) {
        if (!root_.is_object()) {
            error("package authoring descriptor must begin with '{'", &root_);
            return std::nullopt;
        }

        std::optional<std::string> format_version;
        std::optional<std::string> package_name;
        std::optional<std::string> package_version;
        std::optional<PackageAuthoringTarget> entry;
        std::vector<PackageAuthoringTarget> exports;
        std::vector<PackageAuthoringCapabilityBinding> capability_bindings;

        for (const auto &[key, value] : root_.object_fields) {
            if (key == "format_version") {
                format_version = read_string(
                    *value, "package authoring descriptor field 'format_version' must be a string");
            } else if (key == "package") {
                if (!read_package_object(*value, package_name, package_version)) {
                    return std::nullopt;
                }
            } else if (key == "entry") {
                auto target = read_target_object("entry", *value);
                if (!target.has_value()) {
                    return std::nullopt;
                }
                entry = std::move(*target);
            } else if (key == "exports") {
                if (!read_target_array(*value, exports)) {
                    return std::nullopt;
                }
            } else if (key == "capability_bindings") {
                if (!read_capability_binding_array(*value, capability_bindings)) {
                    return std::nullopt;
                }
            } else {
                error("unsupported package authoring descriptor field '" + key + "'", value.get());
                return std::nullopt;
            }

            if (diagnostics_.has_error()) {
                return std::nullopt;
            }
        }

        validate_required_top_level_fields(format_version, package_name, package_version, entry);
        if (diagnostics_.has_error()) {
            return std::nullopt;
        }

        if (*format_version != kPackageAuthoringFormatVersion) {
            error("unsupported package authoring descriptor format_version '" + *format_version +
                  "'");
            return std::nullopt;
        }

        validate_semantics(*package_name, *package_version, *entry, exports, capability_bindings);
        if (diagnostics_.has_error()) {
            return std::nullopt;
        }

        return PackageAuthoringDescriptor{
            .descriptor_path = descriptor_path,
            .format_version = std::move(*format_version),
            .package_name = std::move(*package_name),
            .package_version = std::move(*package_version),
            .entry = std::move(*entry),
            .exports = std::move(exports),
            .capability_bindings = std::move(capability_bindings),
        };
    }

  private:
    const json::JsonValue &root_;
    const SourceFile &source_;
    DiagnosticBag &diagnostics_;

    void error(std::string message, const json::JsonValue *value = nullptr) {
        descriptor_error(diagnostics_, source_, std::move(message), value);
    }

    [[nodiscard]] std::optional<std::string> read_string(const json::JsonValue &value,
                                                         std::string message) {
        return read_json_string(value, diagnostics_, source_, std::move(message));
    }

    [[nodiscard]] bool read_package_object(const json::JsonValue &object,
                                           std::optional<std::string> &package_name,
                                           std::optional<std::string> &package_version) {
        if (!object.is_object()) {
            error("package authoring descriptor field 'package' must be an object", &object);
            return false;
        }

        for (const auto &[key, value] : object.object_fields) {
            if (key == "name") {
                package_name = read_string(
                    *value, "package authoring descriptor field 'package.name' must be a string");
            } else if (key == "version") {
                package_version = read_string(
                    *value,
                    "package authoring descriptor field 'package.version' must be a string");
            } else {
                error("unsupported package authoring descriptor field 'package." + key + "'",
                      value.get());
                return false;
            }

            if (diagnostics_.has_error()) {
                return false;
            }
        }

        if (!package_name.has_value()) {
            error("package authoring descriptor field 'package' is missing required field 'name'");
        }
        if (!package_version.has_value()) {
            error("package authoring descriptor field 'package' is missing required field "
                  "'version'");
        }
        return !diagnostics_.has_error();
    }

    [[nodiscard]] std::optional<PackageAuthoringTargetKind>
    read_target_kind(const json::JsonValue &value, std::string_view field_path) {
        auto kind = read_string(value, "expected target kind string");
        if (!kind.has_value()) {
            return std::nullopt;
        }

        if (*kind == "agent") {
            return PackageAuthoringTargetKind::Agent;
        }
        if (*kind == "workflow") {
            return PackageAuthoringTargetKind::Workflow;
        }

        error("package authoring descriptor field '" + std::string(field_path) +
                  "' must be 'workflow' or 'agent'",
              &value);
        return std::nullopt;
    }

    [[nodiscard]] std::optional<PackageAuthoringTarget>
    read_target_object(std::string_view field_name, const json::JsonValue &object) {
        if (!object.is_object()) {
            error("package authoring descriptor field '" + std::string(field_name) +
                      "' must be an object",
                  &object);
            return std::nullopt;
        }

        std::optional<PackageAuthoringTargetKind> kind;
        std::optional<std::string> name;

        for (const auto &[key, value] : object.object_fields) {
            if (key == "kind") {
                kind = read_target_kind(*value, std::string(field_name) + ".kind");
            } else if (key == "name") {
                name = read_string(*value,
                                   "package authoring descriptor target field 'name' must be a "
                                   "string");
            } else {
                error("unsupported package authoring descriptor field '" + std::string(field_name) +
                          "." + key + "'",
                      value.get());
                return std::nullopt;
            }

            if (diagnostics_.has_error()) {
                return std::nullopt;
            }
        }

        if (!kind.has_value()) {
            error("package authoring descriptor field '" + std::string(field_name) +
                  "' is missing required field 'kind'");
        }
        if (!name.has_value()) {
            error("package authoring descriptor field '" + std::string(field_name) +
                  "' is missing required field 'name'");
        }
        if (diagnostics_.has_error()) {
            return std::nullopt;
        }

        return PackageAuthoringTarget{
            .kind = *kind,
            .name = std::move(*name),
        };
    }

    [[nodiscard]] bool read_target_array(const json::JsonValue &array,
                                         std::vector<PackageAuthoringTarget> &targets) {
        if (!array.is_array()) {
            error("package authoring descriptor field 'exports' must be an array", &array);
            return false;
        }

        for (const auto &item : array.array_items) {
            auto target = read_target_object("exports[]", *item);
            if (!target.has_value()) {
                return false;
            }
            targets.push_back(std::move(*target));
        }
        return true;
    }

    [[nodiscard]] std::optional<PackageAuthoringCapabilityBinding>
    read_capability_binding_object(const json::JsonValue &object) {
        if (!object.is_object()) {
            error("package authoring descriptor field 'capability_bindings[]' must be an object",
                  &object);
            return std::nullopt;
        }

        std::optional<std::string> capability;
        std::optional<std::string> binding_key;

        for (const auto &[key, value] : object.object_fields) {
            if (key == "capability") {
                capability = read_string(
                    *value,
                    "package authoring descriptor capability binding field 'capability' must be a "
                    "string");
            } else if (key == "binding_key") {
                binding_key = read_string(
                    *value,
                    "package authoring descriptor capability binding field 'binding_key' must be "
                    "a string");
            } else {
                error("unsupported package authoring descriptor field 'capability_bindings[]." +
                          key + "'",
                      value.get());
                return std::nullopt;
            }

            if (diagnostics_.has_error()) {
                return std::nullopt;
            }
        }

        if (!capability.has_value()) {
            error("package authoring descriptor field 'capability_bindings[]' is missing required "
                  "field 'capability'");
        }
        if (!binding_key.has_value()) {
            error("package authoring descriptor field 'capability_bindings[]' is missing required "
                  "field 'binding_key'");
        }
        if (diagnostics_.has_error()) {
            return std::nullopt;
        }

        return PackageAuthoringCapabilityBinding{
            .capability = std::move(*capability),
            .binding_key = std::move(*binding_key),
        };
    }

    [[nodiscard]] bool
    read_capability_binding_array(const json::JsonValue &array,
                                  std::vector<PackageAuthoringCapabilityBinding> &bindings) {
        if (!array.is_array()) {
            error("package authoring descriptor field 'capability_bindings' must be an array",
                  &array);
            return false;
        }

        for (const auto &item : array.array_items) {
            auto binding = read_capability_binding_object(*item);
            if (!binding.has_value()) {
                return false;
            }
            bindings.push_back(std::move(*binding));
        }
        return true;
    }

    void validate_required_top_level_fields(const std::optional<std::string> &format_version,
                                            const std::optional<std::string> &package_name,
                                            const std::optional<std::string> &package_version,
                                            const std::optional<PackageAuthoringTarget> &entry) {
        if (!format_version.has_value()) {
            error("package authoring descriptor is missing required field 'format_version'");
        }
        if (!package_name.has_value() || !package_version.has_value()) {
            error("package authoring descriptor is missing required field 'package'");
        }
        if (!entry.has_value()) {
            error("package authoring descriptor is missing required field 'entry'");
        }
    }

    void
    validate_semantics(const std::string &package_name,
                       const std::string &package_version,
                       const PackageAuthoringTarget &entry,
                       const std::vector<PackageAuthoringTarget> &exports,
                       const std::vector<PackageAuthoringCapabilityBinding> &capability_bindings) {
        if (package_name.empty()) {
            error("package authoring descriptor field 'package.name' must not be empty");
        }
        if (package_version.empty()) {
            error("package authoring descriptor field 'package.version' must not be empty");
        }
        if (entry.name.empty()) {
            error("package authoring descriptor field 'entry.name' must not be empty");
        }

        std::unordered_set<std::string> seen_exports;
        for (const auto &target : exports) {
            if (target.name.empty()) {
                error("package authoring descriptor export target name must not be empty");
                continue;
            }

            const auto key = std::to_string(static_cast<int>(target.kind)) + ":" + target.name;
            if (!seen_exports.insert(key).second) {
                error("package authoring descriptor contains duplicate export target '" +
                      target.name + "'");
            }
        }

        std::unordered_set<std::string> seen_capabilities;
        std::unordered_set<std::string> seen_binding_keys;
        for (const auto &binding : capability_bindings) {
            if (binding.capability.empty()) {
                error("package authoring descriptor capability binding field 'capability' must "
                      "not be empty");
            }
            if (binding.binding_key.empty()) {
                error("package authoring descriptor capability binding field 'binding_key' must "
                      "not be empty");
            }
            if (!binding.capability.empty() &&
                !seen_capabilities.insert(binding.capability).second) {
                error("package authoring descriptor contains duplicate capability binding for '" +
                      binding.capability + "'");
            }
            if (!binding.binding_key.empty() &&
                !seen_binding_keys.insert(binding.binding_key).second) {
                error("package authoring descriptor contains duplicate binding_key '" +
                      binding.binding_key + "'");
            }
        }
    }
};

[[nodiscard]] std::optional<std::filesystem::path>
resolve_descriptor_path(const std::filesystem::path &descriptor_root,
                        const std::string &raw_path,
                        DiagnosticBag &diagnostics,
                        const SourceFile &source,
                        std::string_view field_name) {
    const std::filesystem::path input_path(raw_path);
    const auto resolved = input_path.is_absolute() ? normalize_path(input_path)
                                                   : normalize_path(descriptor_root / input_path);

    if (!input_path.is_absolute() && !path_has_prefix(descriptor_root, resolved)) {
        diagnostics.error()
            .message("descriptor field '" + std::string(field_name) +
                     "' must not escape the descriptor root")
            .range(point_range(source, 0))
            .source(source)
            .emit();
        return std::nullopt;
    }

    return resolved;
}

template <typename PathContainerT>
bool append_descriptor_paths(PathContainerT &out,
                             const std::vector<std::string> &raw_paths,
                             const std::filesystem::path &descriptor_root,
                             DiagnosticBag &diagnostics,
                             const SourceFile &source,
                             std::string_view field_name) {
    std::unordered_set<std::string> seen;
    for (const auto &raw_path : raw_paths) {
        const auto resolved =
            resolve_descriptor_path(descriptor_root, raw_path, diagnostics, source, field_name);
        if (!resolved.has_value()) {
            continue;
        }

        const auto key = resolved->string();
        if (!seen.insert(key).second) {
            diagnostics.error()
                .message("descriptor field '" + std::string(field_name) +
                         "' contains duplicate path '" + raw_path + "'")
                .range(point_range(source, 0))
                .source(source)
                .emit();
            continue;
        }

        out.push_back(*resolved);
    }

    return !diagnostics.has_error();
}

[[nodiscard]] std::filesystem::path module_relative_path(std::string_view module_name) {
    std::filesystem::path relative;
    std::size_t start = 0;

    while (start < module_name.size()) {
        const auto separator = module_name.find("::", start);
        if (separator == std::string_view::npos) {
            relative /= std::string(module_name.substr(start));
            break;
        }

        relative /= std::string(module_name.substr(start, separator - start));
        start = separator + 2;
    }

    relative += ".ahfl";
    return relative;
}

[[nodiscard]] bool module_has_prefix(std::string_view module_name, std::string_view prefix) {
    return module_name == prefix ||
           (module_name.size() > prefix.size() && module_name.starts_with(prefix) &&
            module_name.substr(prefix.size(), 2) == "::");
}

[[nodiscard]] std::filesystem::path
module_relative_path_after_prefix(std::string_view module_name, std::string_view prefix) {
    if (module_name == prefix) {
        return std::filesystem::path{"mod.ahfl"};
    }
    return module_relative_path(module_name.substr(prefix.size() + 2));
}

[[nodiscard]] bool has_std_manifest(const std::filesystem::path &search_root) {
    std::error_code error;
    const auto manifest_path = normalize_path(search_root / "std" / "ahfl.toml");
    return std::filesystem::exists(manifest_path, error) && !error;
}

[[nodiscard]] bool
contains_manifest_backed_stdlib_root(const std::vector<std::filesystem::path> &roots) {
    return std::any_of(
        roots.begin(), roots.end(), [](const auto &root) { return has_std_manifest(root); });
}

[[nodiscard]] std::vector<std::filesystem::path> builtin_stdlib_search_roots() {
    std::vector<std::filesystem::path> roots;

#ifdef AHFL_SOURCE_DIR
    append_unique_normalized_path(roots, std::filesystem::path(AHFL_SOURCE_DIR));
#endif

    std::error_code error;
    auto current = std::filesystem::current_path(error);
    if (!error) {
        current = normalize_path(current);
        while (!current.empty()) {
            if (has_std_manifest(current)) {
                append_unique_normalized_path(roots, current);
                break;
            }
            const auto parent = current.parent_path();
            if (parent == current) {
                break;
            }
            current = parent;
        }
    }

    return roots;
}

[[nodiscard]] std::vector<std::filesystem::path> effective_search_roots(const ProjectInput &input) {
    std::vector<std::filesystem::path> roots;
    roots.reserve(input.search_roots.size() + input.entry_files.size() +
                  input.stdlib_search_roots.size() + 2);

    for (const auto &root : input.search_roots) {
        append_unique_normalized_path(roots, root);
    }

    if (roots.empty()) {
        for (const auto &entry_file : input.entry_files) {
            append_unique_normalized_path(roots, normalize_path(entry_file).parent_path());
        }
    }

    if (input.include_stdlib && !contains_manifest_backed_stdlib_root(roots)) {
        for (const auto &root : input.stdlib_search_roots) {
            append_unique_normalized_path(roots, root);
        }
        for (const auto &root : builtin_stdlib_search_roots()) {
            append_unique_normalized_path(roots, root);
        }
    }

    return roots;
}

[[nodiscard]] std::vector<ProjectInput::ModuleRoot>
effective_module_roots(const ProjectInput &input) {
    std::vector<ProjectInput::ModuleRoot> roots;
    roots.reserve(input.module_roots.size());
    for (const auto &root : input.module_roots) {
        const auto normalized = normalize_path(root.root);
        const auto exists = std::find_if(roots.begin(), roots.end(), [&](const auto &existing) {
            return existing.prefix == root.prefix && existing.root == normalized;
        });
        if (exists == roots.end()) {
            roots.push_back(ProjectInput::ModuleRoot{
                .prefix = root.prefix,
                .root = normalized,
                .exported_modules = root.exported_modules,
            });
        }
    }
    return roots;
}

[[nodiscard]] const ProjectInput::ModuleRoot *
find_module_root(std::string_view module_name,
                 const std::vector<ProjectInput::ModuleRoot> &module_roots) {
    const ProjectInput::ModuleRoot *best = nullptr;
    for (const auto &root : module_roots) {
        if (!module_has_prefix(module_name, root.prefix)) {
            continue;
        }
        if (best == nullptr || root.prefix.size() > best->prefix.size()) {
            best = &root;
        }
    }
    return best;
}

[[nodiscard]] std::string exported_module_key(std::string_view module_name,
                                              std::string_view prefix) {
    const auto relative = module_relative_path_after_prefix(module_name, prefix);
    if (relative == "mod.ahfl") {
        return {};
    }
    auto without_extension = relative;
    without_extension.replace_extension();
    return without_extension.generic_string();
}

[[nodiscard]] bool module_is_exported(std::string_view module_name,
                                      const ProjectInput::ModuleRoot &root) {
    const auto key = exported_module_key(module_name, root.prefix);
    return std::find(root.exported_modules.begin(), root.exported_modules.end(), key) !=
           root.exported_modules.end();
}

[[nodiscard]] bool enforce_import_visibility(std::string_view importer_module,
                                             const ImportRequest &import_request,
                                             const std::vector<ProjectInput::ModuleRoot> &roots,
                                             DiagnosticBag &diagnostics,
                                             const SourceFile &source) {
    if (roots.empty()) {
        return true;
    }

    const auto *importer_root = find_module_root(importer_module, roots);
    const auto *imported_root = find_module_root(import_request.module_name, roots);
    if (importer_root == nullptr || imported_root == nullptr || importer_root == imported_root) {
        return true;
    }

    if (module_is_exported(import_request.module_name, *imported_root)) {
        return true;
    }

    diagnostics.error()
        .message("imported module '" + import_request.module_name +
                 "' is private to package prefix '" + imported_root->prefix + "'")
        .range(import_request.range)
        .source(source)
        .emit();
    return false;
}

[[nodiscard]] bool is_std_module(std::string_view module_name) {
    return module_name == "std" || module_name.starts_with("std::");
}

[[nodiscard]] bool should_inject_prelude(const ProjectInput &input, std::string_view module_name) {
    return input.include_stdlib && input.inject_prelude && !is_std_module(module_name);
}

[[nodiscard]] std::optional<std::filesystem::path>
resolve_import_path(std::string_view module_name,
                    const std::vector<std::filesystem::path> &search_roots,
                    const std::vector<ProjectInput::ModuleRoot> &module_roots,
                    DiagnosticBag &diagnostics,
                    MaybeCRef<SourceFile> source = std::nullopt,
                    std::optional<SourceRange> range = std::nullopt) {
    std::vector<std::filesystem::path> candidates;
    if (!module_roots.empty()) {
        for (const auto &root : module_roots) {
            if (!module_has_prefix(module_name, root.prefix)) {
                continue;
            }
            const auto relative = module_relative_path_after_prefix(module_name, root.prefix);
            std::error_code error;
            const auto candidate = normalize_path(root.root / relative);
            if (std::filesystem::exists(candidate, error) && !error) {
                if (std::find(candidates.begin(), candidates.end(), candidate) ==
                    candidates.end()) {
                    candidates.push_back(candidate);
                }
                continue;
            }
            const auto dir_candidate =
                normalize_path(root.root / relative.parent_path() / relative.stem() / "mod.ahfl");
            if (std::filesystem::exists(dir_candidate, error) && !error) {
                if (std::find(candidates.begin(), candidates.end(), dir_candidate) ==
                    candidates.end()) {
                    candidates.push_back(dir_candidate);
                }
            }
        }
    } else {
        const auto relative = module_relative_path(module_name);
        for (const auto &root : search_roots) {
            std::error_code error;
            // Try single-file layout: root/path/to/module.ahfl
            const auto candidate = normalize_path(root / relative);
            if (std::filesystem::exists(candidate, error) && !error) {
                if (std::find(candidates.begin(), candidates.end(), candidate) ==
                    candidates.end()) {
                    candidates.push_back(candidate);
                }
                continue;
            }
            // Try directory-module layout: root/path/to/module/mod.ahfl
            // (Rust-style: directory with mod.ahfl as the entry point)
            const auto dir_candidate =
                normalize_path(root / relative.parent_path() / relative.stem() / "mod.ahfl");
            if (std::filesystem::exists(dir_candidate, error) && !error) {
                if (std::find(candidates.begin(), candidates.end(), dir_candidate) ==
                    candidates.end()) {
                    candidates.push_back(dir_candidate);
                }
            }
        }
    }

    if (candidates.empty()) {
        const auto message = "failed to resolve imported module '" + std::string(module_name) + "'";
        if (source.has_value()) {
            diagnostics.error().message(message).range(range).source(source->get()).emit();
        } else {
            diagnostics.error().message(message).range(range).emit();
        }
        return std::nullopt;
    }

    if (candidates.size() > 1) {
        std::ostringstream builder;
        builder << "imported module '" << module_name << "' is ambiguous across search roots";
        if (source.has_value()) {
            diagnostics.error().message(builder.str()).range(range).source(source->get()).emit();
        } else {
            diagnostics.error().message(builder.str()).range(range).emit();
        }
        return std::nullopt;
    }

    return candidates.front();
}

[[nodiscard]] MaybeRef<SourceUnit> find_source_unit(SourceGraph &graph, SourceId id) {
    for (auto &source : graph.sources) {
        if (source.id == id) {
            return std::ref(source);
        }
    }

    return std::nullopt;
}

} // namespace

ProjectDescriptorParseResult
Frontend::load_project_descriptor(const std::filesystem::path &path) const {
    ProjectDescriptorParseResult result;
    SourceFile source;
    if (!read_text_file(path, source, result.diagnostics, "project descriptor")) {
        return result;
    }

    const auto descriptor_path = normalize_path(path);
    const auto descriptor_root = descriptor_path.parent_path();

    auto root = parse_descriptor_json(source,
                                      result.diagnostics,
                                      "descriptor must begin with '{'",
                                      "project descriptor must be valid JSON");
    if (!root.has_value()) {
        return result;
    }
    const auto &fields = **root;

    reject_unknown_project_fields(fields, result.diagnostics, source);
    if (result.has_errors()) {
        return result;
    }

    auto format_version = required_string_field(
        "project descriptor", "format_version", fields, result.diagnostics, source);
    auto name =
        required_string_field("project descriptor", "name", fields, result.diagnostics, source);
    auto search_roots = required_string_array_field(
        "project descriptor", "search_roots", fields, result.diagnostics, source);
    auto entry_sources = required_string_array_field(
        "project descriptor", "entry_sources", fields, result.diagnostics, source);

    if (!format_version.has_value() || !name.has_value() || !search_roots.has_value() ||
        !entry_sources.has_value()) {
        return result;
    }

    if (*format_version != "ahfl.project.v0.3") {
        result.diagnostics.error()
            .message("unsupported project descriptor format_version '" + *format_version + "'")
            .range(point_range(source, 0))
            .source(source)
            .emit();
        return result;
    }

    if (search_roots->empty()) {
        result.diagnostics.error()
            .message("project descriptor requires non-empty 'search_roots'")
            .range(point_range(source, 0))
            .source(source)
            .emit();
    }
    if (entry_sources->empty()) {
        result.diagnostics.error()
            .message("project descriptor requires non-empty 'entry_sources'")
            .range(point_range(source, 0))
            .source(source)
            .emit();
    }
    if (result.has_errors()) {
        return result;
    }

    ProjectDescriptor descriptor{
        .descriptor_path = descriptor_path,
        .format_version = std::move(*format_version),
        .name = std::move(*name),
        .entry_files = {},
        .search_roots = {},
    };

    append_descriptor_paths(descriptor.search_roots,
                            *search_roots,
                            descriptor_root,
                            result.diagnostics,
                            source,
                            "search_roots");
    append_descriptor_paths(descriptor.entry_files,
                            *entry_sources,
                            descriptor_root,
                            result.diagnostics,
                            source,
                            "entry_sources");
    if (result.has_errors()) {
        return result;
    }

    result.descriptor = std::move(descriptor);
    return result;
}

PackageAuthoringDescriptorParseResult
Frontend::load_package_authoring_descriptor(const std::filesystem::path &path) const {
    PackageAuthoringDescriptorParseResult result;
    SourceFile source;
    if (!read_text_file(path, source, result.diagnostics, "package authoring descriptor")) {
        return result;
    }

    const auto descriptor_path = normalize_path(path);
    auto root = parse_descriptor_json(source,
                                      result.diagnostics,
                                      "package authoring descriptor must begin with '{'",
                                      "package authoring descriptor must be valid JSON");
    if (!root.has_value()) {
        return result;
    }

    PackageAuthoringJsonReader reader(**root, source, result.diagnostics);
    auto descriptor = reader.parse_descriptor(descriptor_path);
    if (!descriptor.has_value()) {
        return result;
    }

    result.descriptor = std::move(*descriptor);
    return result;
}

WorkspaceDescriptorParseResult
Frontend::load_workspace_descriptor(const std::filesystem::path &path) const {
    WorkspaceDescriptorParseResult result;
    SourceFile source;
    if (!read_text_file(path, source, result.diagnostics, "workspace descriptor")) {
        return result;
    }

    const auto descriptor_path = normalize_path(path);
    const auto descriptor_root = descriptor_path.parent_path();

    auto root = parse_descriptor_json(source,
                                      result.diagnostics,
                                      "descriptor must begin with '{'",
                                      "workspace descriptor must be valid JSON");
    if (!root.has_value()) {
        return result;
    }
    const auto &fields = **root;

    reject_unknown_workspace_fields(fields, result.diagnostics, source);
    if (result.has_errors()) {
        return result;
    }

    auto format_version = required_string_field(
        "workspace descriptor", "format_version", fields, result.diagnostics, source);
    auto name =
        required_string_field("workspace descriptor", "name", fields, result.diagnostics, source);
    auto projects = required_string_array_field(
        "workspace descriptor", "projects", fields, result.diagnostics, source);
    if (!format_version.has_value() || !name.has_value() || !projects.has_value()) {
        return result;
    }

    if (*format_version != "ahfl.workspace.v0.3") {
        result.diagnostics.error()
            .message("unsupported workspace descriptor format_version '" + *format_version + "'")
            .range(point_range(source, 0))
            .source(source)
            .emit();
        return result;
    }

    if (projects->empty()) {
        result.diagnostics.error()
            .message("workspace descriptor requires non-empty 'projects'")
            .range(point_range(source, 0))
            .source(source)
            .emit();
        return result;
    }

    WorkspaceDescriptor descriptor{
        .descriptor_path = descriptor_path,
        .format_version = std::move(*format_version),
        .name = std::move(*name),
        .projects = {},
    };

    append_descriptor_paths(
        descriptor.projects, *projects, descriptor_root, result.diagnostics, source, "projects");
    if (result.has_errors()) {
        return result;
    }

    result.descriptor = std::move(descriptor);
    return result;
}

ProjectDescriptorParseResult
Frontend::load_project_descriptor_from_workspace(const std::filesystem::path &workspace_path,
                                                 std::string_view project_name) const {
    ProjectDescriptorParseResult result;
    auto workspace_result = load_workspace_descriptor(workspace_path);
    result.diagnostics.append(workspace_result.diagnostics);
    if (workspace_result.has_errors() || !workspace_result.descriptor.has_value()) {
        return result;
    }

    if (project_name.empty()) {
        result.diagnostics.error().message("workspace project selection must not be empty").emit();
        return result;
    }

    std::optional<ProjectDescriptor> selected_descriptor;
    std::optional<std::filesystem::path> first_match_path;

    for (const auto &project_path : workspace_result.descriptor->projects) {
        auto project_result = load_project_descriptor(project_path);
        result.diagnostics.append(project_result.diagnostics);
        if (project_result.has_errors() || !project_result.descriptor.has_value()) {
            continue;
        }

        if (project_result.descriptor->name != project_name) {
            continue;
        }

        if (selected_descriptor.has_value()) {
            result.diagnostics.error()
                .message("workspace contains duplicate project name '" + std::string(project_name) +
                         "'")
                .emit();
            result.diagnostics.note()
                .message("first matching project descriptor is '" +
                         display_path(*first_match_path) + "'")
                .emit();
            result.diagnostics.note()
                .message("second matching project descriptor is '" +
                         display_path(project_result.descriptor->descriptor_path) + "'")
                .emit();
            return result;
        }

        first_match_path = project_result.descriptor->descriptor_path;
        selected_descriptor = std::move(*project_result.descriptor);
    }

    if (!selected_descriptor.has_value()) {
        result.diagnostics.error()
            .message("workspace does not contain project named '" + std::string(project_name) + "'")
            .emit();
        return result;
    }

    result.descriptor = std::move(selected_descriptor);
    return result;
}

ProjectParseResult Frontend::parse_project(const ProjectInput &input) const {
    ProjectParseResult result;

    if (input.entry_files.empty()) {
        result.diagnostics.error()
            .message("project input must contain at least one entry file")
            .emit();
        return result;
    }

    const auto search_roots = effective_search_roots(input);
    const auto module_roots = effective_module_roots(input);
    if (search_roots.empty() && module_roots.empty()) {
        result.diagnostics.error()
            .message("project input did not yield any module or search roots")
            .emit();
        return result;
    }

    std::unordered_map<std::string, SourceId> path_to_source;
    std::unordered_set<std::string> in_progress_paths;
    std::size_t next_source_id = 0;

    std::function<std::optional<SourceId>(const std::filesystem::path &,
                                          std::optional<std::string>,
                                          MaybeCRef<SourceFile>,
                                          std::optional<SourceRange>)>
        load_source;

    load_source = [&](const std::filesystem::path &raw_path,
                      std::optional<std::string> expected_module,
                      MaybeCRef<SourceFile> request_source,
                      std::optional<SourceRange> import_range) -> std::optional<SourceId> {
        const auto path = normalize_path(raw_path);
        const auto path_key = path.string();

        if (const auto existing = path_to_source.find(path_key); existing != path_to_source.end()) {
            return existing->second;
        }

        if (in_progress_paths.contains(path_key)) {
            if (request_source.has_value()) {
                result.diagnostics.error()
                    .message("import refers to a source while it is still being loaded: " +
                             display_path(path))
                    .range(import_range)
                    .source(request_source->get())
                    .emit();
            } else {
                result.diagnostics.error()
                    .message("import refers to a source while it is still being loaded: " +
                             display_path(path))
                    .range(import_range)
                    .emit();
            }
            return std::nullopt;
        }

        in_progress_paths.insert(path_key);
        auto parse_result = [&]() {
            const auto overlay = input.source_overlays.find(path_key);
            if (overlay != input.source_overlays.end()) {
                return parse_text(display_path(path), overlay->second);
            }
            return parse_file(path);
        }();
        result.diagnostics.append_from_source(parse_result.diagnostics, parse_result.source);

        std::optional<SourceId> loaded_id;

        if (parse_result.program && !parse_result.has_errors()) {
            const auto imports = collect_program_imports(*parse_result.program);
            if (imports.modules.empty()) {
                result.diagnostics.error()
                    .message("project-aware source file must declare exactly one module")
                    .range(parse_result.program->range)
                    .source(parse_result.source)
                    .emit();
            } else if (imports.modules.size() > 1) {
                result.diagnostics.error()
                    .message("project-aware source file must declare exactly one module")
                    .range(imports.modules[1].second)
                    .source(parse_result.source)
                    .emit();
                result.diagnostics.note()
                    .message("first module declaration is here")
                    .range(imports.modules.front().second)
                    .source(parse_result.source)
                    .emit();
            } else if (imports.modules.front().first.empty()) {
                result.diagnostics.error()
                    .message("module declaration must not be empty")
                    .range(imports.modules.front().second)
                    .source(parse_result.source)
                    .emit();
            } else {
                const auto &module_name = imports.modules.front().first;
                if (expected_module.has_value() && module_name != *expected_module) {
                    result.diagnostics.error()
                        .message("source file declares module '" + module_name +
                                 "' but import requested '" + *expected_module + "'")
                        .range(imports.modules.front().second)
                        .source(parse_result.source)
                        .emit();
                } else if (const auto existing = result.graph.module_to_source.find(module_name);
                           existing != result.graph.module_to_source.end()) {
                    result.diagnostics.error()
                        .message("duplicate module owner for '" + module_name + "'")
                        .range(imports.modules.front().second)
                        .source(parse_result.source)
                        .emit();
                    if (const auto previous = find_source_unit(result.graph, existing->second);
                        previous.has_value()) {
                        result.diagnostics.note()
                            .message("previous module owner is '" +
                                     previous->get().source.display_name + "'")
                            .range(previous->get().module_range)
                            .source(previous->get().source)
                            .emit();
                    }
                } else {
                    const auto source_id = SourceId{next_source_id++};
                    path_to_source.emplace(path_key, source_id);
                    result.graph.module_to_source.emplace(module_name, source_id);
                    result.graph.sources.push_back(SourceUnit{
                        .id = source_id,
                        .path = path,
                        .module_name = module_name,
                        .module_range = imports.modules.front().second,
                        .source = std::move(parse_result.source),
                        .program = std::move(parse_result.program),
                        .imports = imports.imports,
                    });

                    loaded_id = source_id;

                    auto &source_unit = result.graph.sources.back();
                    if (should_inject_prelude(input, source_unit.module_name)) {
                        source_unit.imports.push_back(ImportRequest{
                            .module_name = std::string(kStdPreludeModule),
                            .alias = "",
                            .range = source_unit.module_range,
                        });
                    }
                    for (const auto &import_request : source_unit.imports) {
                        if (import_request.module_name == source_unit.module_name) {
                            result.graph.import_edges.push_back(ImportEdge{
                                .importer = source_id,
                                .imported = source_id,
                                .request = import_request,
                            });
                            continue;
                        }
                        if (!enforce_import_visibility(source_unit.module_name,
                                                       import_request,
                                                       module_roots,
                                                       result.diagnostics,
                                                       source_unit.source)) {
                            continue;
                        }
                        const auto imported_path =
                            resolve_import_path(import_request.module_name,
                                                search_roots,
                                                module_roots,
                                                result.diagnostics,
                                                std::cref(source_unit.source),
                                                import_request.range);
                        if (!imported_path.has_value()) {
                            continue;
                        }

                        const auto imported_id = load_source(*imported_path,
                                                             import_request.module_name,
                                                             std::cref(source_unit.source),
                                                             import_request.range);
                        if (!imported_id.has_value()) {
                            continue;
                        }

                        result.graph.import_edges.push_back(ImportEdge{
                            .importer = source_id,
                            .imported = *imported_id,
                            .request = import_request,
                        });
                    }
                }
            }
        }

        if (!loaded_id.has_value() && expected_module.has_value()) {
            if (request_source.has_value()) {
                result.diagnostics.note()
                    .message("import requested module '" + *expected_module + "' here")
                    .range(import_range)
                    .source(request_source->get())
                    .emit();
            } else {
                result.diagnostics.note()
                    .message("import requested module '" + *expected_module + "' here")
                    .range(import_range)
                    .emit();
            }
        }

        in_progress_paths.erase(path_key);
        return loaded_id;
    };

    for (const auto &entry_file : input.entry_files) {
        const auto entry_id = load_source(entry_file, std::nullopt, std::nullopt, std::nullopt);
        if (entry_id.has_value()) {
            result.graph.entry_sources.push_back(*entry_id);
        }
    }

    return result;
}

void dump_project_outline(const SourceGraph &graph, std::ostream &out) {
    const auto visible_source = [](const SourceUnit &source) {
        return !is_std_module(source.module_name);
    };
    const auto visible_import = [&](const ImportEdge &edge) {
        const auto source_by_id = [&](SourceId id) -> const SourceUnit * {
            for (const auto &source : graph.sources) {
                if (source.id == id) {
                    return &source;
                }
            }
            return nullptr;
        };
        const auto *importer = source_by_id(edge.importer);
        const auto *imported = source_by_id(edge.imported);
        return importer != nullptr && imported != nullptr && visible_source(*importer) &&
               visible_source(*imported);
    };

    const auto source_count =
        std::count_if(graph.sources.begin(), graph.sources.end(), visible_source);
    const auto import_count =
        std::count_if(graph.import_edges.begin(), graph.import_edges.end(), visible_import);

    out << "source_graph (" << graph.entry_sources.size() << " entry, " << source_count
        << " sources, " << import_count << " import" << (import_count == 1 ? "" : "s") << ")\n";

    for (const auto &source : graph.sources) {
        if (!visible_source(source)) {
            continue;
        }
        out << "source " << source.source.display_name << '\n';
        out << "  module " << source.module_name << '\n';
        const auto visible_imports =
            std::count_if(source.imports.begin(), source.imports.end(), [](const auto &request) {
                return !is_std_module(request.module_name);
            });
        if (visible_imports > 0) {
            out << "  imports\n";
            for (const auto &import_request : source.imports) {
                if (is_std_module(import_request.module_name)) {
                    continue;
                }
                out << "    " << import_request.module_name;
                if (!import_request.alias.empty()) {
                    out << " as " << import_request.alias;
                }
                out << '\n';
            }
        }
    }
}

} // namespace ahfl
