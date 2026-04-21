#include "ahfl/frontend/frontend.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
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
        diagnostics.error("failed to open " + std::string(kind) + ": " + display_path(path));
        return false;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    source.content = buffer.str();
    source.invalidate_line_starts_cache();
    return true;
}

struct DescriptorJsonValue {
    enum class Kind {
        String,
        StringArray,
    };

    Kind kind{Kind::String};
    std::string string_value;
    std::vector<std::string> array_values;
};

class DescriptorJsonParser {
  public:
    DescriptorJsonParser(const SourceFile &source, DiagnosticBag &diagnostics)
        : source_(source), diagnostics_(diagnostics) {}

    [[nodiscard]] std::optional<std::unordered_map<std::string, DescriptorJsonValue>>
    parse_object() {
        skip_whitespace();
        if (!consume('{')) {
            error_here("descriptor must begin with '{'");
            return std::nullopt;
        }

        std::unordered_map<std::string, DescriptorJsonValue> fields;
        skip_whitespace();
        if (consume('}')) {
            skip_whitespace();
            if (!at_end()) {
                error_here("descriptor contains trailing content");
                return std::nullopt;
            }
            return fields;
        }

        while (true) {
            auto key = parse_string("expected descriptor field name");
            if (!key.has_value()) {
                return std::nullopt;
            }

            if (fields.contains(*key)) {
                error_here("duplicate descriptor field '" + *key + "'");
                return std::nullopt;
            }

            skip_whitespace();
            if (!consume(':')) {
                error_here("expected ':' after descriptor field name");
                return std::nullopt;
            }

            auto value = parse_value();
            if (!value.has_value()) {
                return std::nullopt;
            }

            fields.emplace(std::move(*key), std::move(*value));
            skip_whitespace();

            if (consume('}')) {
                break;
            }

            if (!consume(',')) {
                error_here("expected ',' or '}' after descriptor field");
                return std::nullopt;
            }

            skip_whitespace();
        }

        skip_whitespace();
        if (!at_end()) {
            error_here("descriptor contains trailing content");
            return std::nullopt;
        }

        return fields;
    }

  private:
    const SourceFile &source_;
    DiagnosticBag &diagnostics_;
    std::size_t index_{0};

    [[nodiscard]] bool at_end() const noexcept {
        return index_ >= source_.content.size();
    }

    [[nodiscard]] char current() const noexcept {
        return at_end() ? '\0' : source_.content[index_];
    }

    void skip_whitespace() {
        while (!at_end()) {
            const auto ch = current();
            if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') {
                break;
            }
            ++index_;
        }
    }

    [[nodiscard]] bool consume(char expected) {
        skip_whitespace();
        if (current() != expected) {
            return false;
        }
        ++index_;
        return true;
    }

    void error_here(std::string message) {
        diagnostics_.error_in_source(std::move(message), source_, point_range(source_, index_));
    }

    [[nodiscard]] std::optional<std::string> parse_string(std::string_view missing_message) {
        skip_whitespace();
        if (current() != '"') {
            error_here(std::string(missing_message));
            return std::nullopt;
        }

        ++index_;
        std::string value;
        while (!at_end()) {
            const auto ch = source_.content[index_++];
            if (ch == '"') {
                return value;
            }

            if (ch == '\\') {
                if (at_end()) {
                    error_here("unterminated escape sequence in descriptor string");
                    return std::nullopt;
                }

                const auto escaped = source_.content[index_++];
                switch (escaped) {
                case '"':
                case '\\':
                case '/':
                    value.push_back(escaped);
                    break;
                case 'b':
                    value.push_back('\b');
                    break;
                case 'f':
                    value.push_back('\f');
                    break;
                case 'n':
                    value.push_back('\n');
                    break;
                case 'r':
                    value.push_back('\r');
                    break;
                case 't':
                    value.push_back('\t');
                    break;
                default:
                    error_here("unsupported escape sequence in descriptor string");
                    return std::nullopt;
                }
                continue;
            }

            value.push_back(ch);
        }

        error_here("unterminated descriptor string");
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::vector<std::string>>
    parse_string_array(std::string_view missing_message) {
        skip_whitespace();
        if (current() != '[') {
            error_here(std::string(missing_message));
            return std::nullopt;
        }

        ++index_;
        std::vector<std::string> values;
        skip_whitespace();
        if (consume(']')) {
            return values;
        }

        while (true) {
            auto value = parse_string("expected descriptor array element string");
            if (!value.has_value()) {
                return std::nullopt;
            }
            values.push_back(std::move(*value));

            skip_whitespace();
            if (consume(']')) {
                return values;
            }

            if (!consume(',')) {
                error_here("expected ',' or ']' in descriptor array");
                return std::nullopt;
            }
        }
    }

    [[nodiscard]] std::optional<DescriptorJsonValue> parse_value() {
        skip_whitespace();
        if (current() == '"') {
            auto value = parse_string("expected descriptor string value");
            if (!value.has_value()) {
                return std::nullopt;
            }

            return DescriptorJsonValue{
                .kind = DescriptorJsonValue::Kind::String,
                .string_value = std::move(*value),
                .array_values = {},
            };
        }

        if (current() == '[') {
            auto values = parse_string_array("expected descriptor array value");
            if (!values.has_value()) {
                return std::nullopt;
            }

            return DescriptorJsonValue{
                .kind = DescriptorJsonValue::Kind::StringArray,
                .string_value = {},
                .array_values = std::move(*values),
            };
        }

        error_here("descriptor values must be strings or arrays of strings");
        return std::nullopt;
    }
};

class PackageAuthoringJsonParser {
  public:
    PackageAuthoringJsonParser(const SourceFile &source, DiagnosticBag &diagnostics)
        : source_(source), diagnostics_(diagnostics) {}

    [[nodiscard]] std::optional<PackageAuthoringDescriptor>
    parse_descriptor(const std::filesystem::path &descriptor_path) {
        skip_whitespace();
        if (!consume('{')) {
            error_here("package authoring descriptor must begin with '{'");
            return std::nullopt;
        }

        std::optional<std::string> format_version;
        std::optional<std::string> package_name;
        std::optional<std::string> package_version;
        std::optional<PackageAuthoringTarget> entry;
        std::vector<PackageAuthoringTarget> exports;
        std::vector<PackageAuthoringCapabilityBinding> capability_bindings;
        std::unordered_set<std::string> seen_fields;

        skip_whitespace();
        if (!consume('}')) {
            while (true) {
                auto key = parse_string("expected package authoring descriptor field name");
                if (!key.has_value()) {
                    return std::nullopt;
                }

                if (!seen_fields.insert(*key).second) {
                    error_here("duplicate package authoring descriptor field '" + *key + "'");
                    return std::nullopt;
                }

                skip_whitespace();
                if (!consume(':')) {
                    error_here("expected ':' after package authoring descriptor field name");
                    return std::nullopt;
                }

                if (*key == "format_version") {
                    format_version =
                        parse_string("package authoring descriptor field 'format_version' must be a string");
                } else if (*key == "package") {
                    if (!parse_package_object(package_name, package_version)) {
                        return std::nullopt;
                    }
                } else if (*key == "entry") {
                    PackageAuthoringTarget target;
                    if (!parse_target_object("entry", target)) {
                        return std::nullopt;
                    }
                    entry = std::move(target);
                } else if (*key == "exports") {
                    if (!parse_target_array(exports)) {
                        return std::nullopt;
                    }
                } else if (*key == "capability_bindings") {
                    if (!parse_capability_binding_array(capability_bindings)) {
                        return std::nullopt;
                    }
                } else {
                    error_here("unsupported package authoring descriptor field '" + *key + "'");
                    return std::nullopt;
                }

                if (diagnostics_.has_error()) {
                    return std::nullopt;
                }

                skip_whitespace();
                if (consume('}')) {
                    break;
                }

                if (!consume(',')) {
                    error_here("expected ',' or '}' after package authoring descriptor field");
                    return std::nullopt;
                }

                skip_whitespace();
            }
        }

        skip_whitespace();
        if (!at_end()) {
            error_here("package authoring descriptor contains trailing content");
            return std::nullopt;
        }

        if (!format_version.has_value()) {
            diagnostics_.error_in_source(
                "package authoring descriptor is missing required field 'format_version'",
                source_,
                point_range(source_, 0));
        }
        if (!package_name.has_value() || !package_version.has_value()) {
            diagnostics_.error_in_source(
                "package authoring descriptor is missing required field 'package'",
                source_,
                point_range(source_, 0));
        }
        if (!entry.has_value()) {
            diagnostics_.error_in_source(
                "package authoring descriptor is missing required field 'entry'",
                source_,
                point_range(source_, 0));
        }
        if (diagnostics_.has_error()) {
            return std::nullopt;
        }

        if (*format_version != kPackageAuthoringFormatVersion) {
            diagnostics_.error_in_source("unsupported package authoring descriptor format_version '" +
                                             *format_version + "'",
                                         source_,
                                         point_range(source_, 0));
            return std::nullopt;
        }

        if (package_name->empty()) {
            diagnostics_.error_in_source("package authoring descriptor field 'package.name' must not be empty",
                                         source_,
                                         point_range(source_, 0));
        }
        if (package_version->empty()) {
            diagnostics_.error_in_source(
                "package authoring descriptor field 'package.version' must not be empty",
                source_,
                point_range(source_, 0));
        }
        if (entry->name.empty()) {
            diagnostics_.error_in_source("package authoring descriptor field 'entry.name' must not be empty",
                                         source_,
                                         point_range(source_, 0));
        }

        std::unordered_set<std::string> seen_exports;
        for (const auto &target : exports) {
            if (target.name.empty()) {
                diagnostics_.error_in_source(
                    "package authoring descriptor export target name must not be empty",
                    source_,
                    point_range(source_, 0));
                continue;
            }

            const auto key = std::to_string(static_cast<int>(target.kind)) + ":" + target.name;
            if (!seen_exports.insert(key).second) {
                diagnostics_.error_in_source(
                    "package authoring descriptor contains duplicate export target '" + target.name + "'",
                    source_,
                    point_range(source_, 0));
            }
        }

        std::unordered_set<std::string> seen_capabilities;
        std::unordered_set<std::string> seen_binding_keys;
        for (const auto &binding : capability_bindings) {
            if (binding.capability.empty()) {
                diagnostics_.error_in_source(
                    "package authoring descriptor capability binding field 'capability' must not be empty",
                    source_,
                    point_range(source_, 0));
            }
            if (binding.binding_key.empty()) {
                diagnostics_.error_in_source(
                    "package authoring descriptor capability binding field 'binding_key' must not be empty",
                    source_,
                    point_range(source_, 0));
            }
            if (!binding.capability.empty() && !seen_capabilities.insert(binding.capability).second) {
                diagnostics_.error_in_source(
                    "package authoring descriptor contains duplicate capability binding for '" +
                        binding.capability + "'",
                    source_,
                    point_range(source_, 0));
            }
            if (!binding.binding_key.empty() && !seen_binding_keys.insert(binding.binding_key).second) {
                diagnostics_.error_in_source(
                    "package authoring descriptor contains duplicate binding_key '" +
                        binding.binding_key + "'",
                    source_,
                    point_range(source_, 0));
            }
        }
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
    const SourceFile &source_;
    DiagnosticBag &diagnostics_;
    std::size_t index_{0};

    [[nodiscard]] bool at_end() const noexcept {
        return index_ >= source_.content.size();
    }

    [[nodiscard]] char current() const noexcept {
        return at_end() ? '\0' : source_.content[index_];
    }

    void skip_whitespace() {
        while (!at_end()) {
            const auto ch = current();
            if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') {
                break;
            }
            ++index_;
        }
    }

    [[nodiscard]] bool consume(char expected) {
        skip_whitespace();
        if (current() != expected) {
            return false;
        }
        ++index_;
        return true;
    }

    void error_here(std::string message) {
        diagnostics_.error_in_source(std::move(message), source_, point_range(source_, index_));
    }

    [[nodiscard]] std::optional<std::string> parse_string(std::string_view missing_message) {
        skip_whitespace();
        if (current() != '"') {
            error_here(std::string(missing_message));
            return std::nullopt;
        }

        ++index_;
        std::string value;
        while (!at_end()) {
            const auto ch = source_.content[index_++];
            if (ch == '"') {
                return value;
            }

            if (ch == '\\') {
                if (at_end()) {
                    error_here("unterminated escape sequence in package authoring descriptor string");
                    return std::nullopt;
                }

                const auto escaped = source_.content[index_++];
                switch (escaped) {
                case '"':
                case '\\':
                case '/':
                    value.push_back(escaped);
                    break;
                case 'b':
                    value.push_back('\b');
                    break;
                case 'f':
                    value.push_back('\f');
                    break;
                case 'n':
                    value.push_back('\n');
                    break;
                case 'r':
                    value.push_back('\r');
                    break;
                case 't':
                    value.push_back('\t');
                    break;
                default:
                    error_here("unsupported escape sequence in package authoring descriptor string");
                    return std::nullopt;
                }
                continue;
            }

            value.push_back(ch);
        }

        error_here("unterminated package authoring descriptor string");
        return std::nullopt;
    }

    [[nodiscard]] std::optional<PackageAuthoringTargetKind>
    parse_target_kind(std::string_view field_path) {
        auto kind = parse_string("expected target kind string");
        if (!kind.has_value()) {
            return std::nullopt;
        }

        if (*kind == "agent") {
            return PackageAuthoringTargetKind::Agent;
        }
        if (*kind == "workflow") {
            return PackageAuthoringTargetKind::Workflow;
        }

        diagnostics_.error_in_source("package authoring descriptor field '" + std::string(field_path) +
                                         "' must be 'workflow' or 'agent'",
                                     source_,
                                     point_range(source_, 0));
        return std::nullopt;
    }

    [[nodiscard]] bool parse_package_object(std::optional<std::string> &package_name,
                                            std::optional<std::string> &package_version) {
        skip_whitespace();
        if (!consume('{')) {
            error_here("package authoring descriptor field 'package' must be an object");
            return false;
        }

        std::unordered_set<std::string> seen_fields;
        skip_whitespace();
        if (consume('}')) {
            diagnostics_.error_in_source(
                "package authoring descriptor field 'package' is missing required field 'name'",
                source_,
                point_range(source_, 0));
            diagnostics_.error_in_source(
                "package authoring descriptor field 'package' is missing required field 'version'",
                source_,
                point_range(source_, 0));
            return false;
        }

        while (true) {
            auto key = parse_string("expected package field name");
            if (!key.has_value()) {
                return false;
            }
            if (!seen_fields.insert(*key).second) {
                error_here("duplicate package authoring descriptor field 'package." + *key + "'");
                return false;
            }

            skip_whitespace();
            if (!consume(':')) {
                error_here("expected ':' after package field name");
                return false;
            }

            if (*key == "name") {
                package_name = parse_string("package authoring descriptor field 'package.name' must be a string");
            } else if (*key == "version") {
                package_version =
                    parse_string("package authoring descriptor field 'package.version' must be a string");
            } else {
                error_here("unsupported package authoring descriptor field 'package." + *key + "'");
                return false;
            }

            if (diagnostics_.has_error()) {
                return false;
            }

            skip_whitespace();
            if (consume('}')) {
                break;
            }
            if (!consume(',')) {
                error_here("expected ',' or '}' after package field");
                return false;
            }
        }

        if (!package_name.has_value()) {
            diagnostics_.error_in_source(
                "package authoring descriptor field 'package' is missing required field 'name'",
                source_,
                point_range(source_, 0));
        }
        if (!package_version.has_value()) {
            diagnostics_.error_in_source(
                "package authoring descriptor field 'package' is missing required field 'version'",
                source_,
                point_range(source_, 0));
        }
        return !diagnostics_.has_error();
    }

    [[nodiscard]] bool parse_target_object(std::string_view field_name, PackageAuthoringTarget &target) {
        skip_whitespace();
        if (!consume('{')) {
            error_here("package authoring descriptor field '" + std::string(field_name) +
                       "' must be an object");
            return false;
        }

        std::optional<PackageAuthoringTargetKind> kind;
        std::optional<std::string> name;
        std::unordered_set<std::string> seen_fields;

        skip_whitespace();
        if (consume('}')) {
            diagnostics_.error_in_source("package authoring descriptor field '" + std::string(field_name) +
                                             "' is missing required field 'kind'",
                                         source_,
                                         point_range(source_, 0));
            diagnostics_.error_in_source("package authoring descriptor field '" + std::string(field_name) +
                                             "' is missing required field 'name'",
                                         source_,
                                         point_range(source_, 0));
            return false;
        }

        while (true) {
            auto key = parse_string("expected target field name");
            if (!key.has_value()) {
                return false;
            }
            if (!seen_fields.insert(*key).second) {
                error_here("duplicate package authoring descriptor field '" + std::string(field_name) +
                           "." + *key + "'");
                return false;
            }

            skip_whitespace();
            if (!consume(':')) {
                error_here("expected ':' after target field name");
                return false;
            }

            if (*key == "kind") {
                kind = parse_target_kind(std::string(field_name) + ".kind");
            } else if (*key == "name") {
                name = parse_string("package authoring descriptor target field 'name' must be a string");
            } else {
                error_here("unsupported package authoring descriptor field '" +
                           std::string(field_name) + "." + *key + "'");
                return false;
            }

            if (diagnostics_.has_error()) {
                return false;
            }

            skip_whitespace();
            if (consume('}')) {
                break;
            }
            if (!consume(',')) {
                error_here("expected ',' or '}' after target field");
                return false;
            }
        }

        if (!kind.has_value()) {
            diagnostics_.error_in_source("package authoring descriptor field '" + std::string(field_name) +
                                             "' is missing required field 'kind'",
                                         source_,
                                         point_range(source_, 0));
        }
        if (!name.has_value()) {
            diagnostics_.error_in_source("package authoring descriptor field '" + std::string(field_name) +
                                             "' is missing required field 'name'",
                                         source_,
                                         point_range(source_, 0));
        }
        if (diagnostics_.has_error()) {
            return false;
        }

        target.kind = *kind;
        target.name = std::move(*name);
        return true;
    }

    [[nodiscard]] bool parse_target_array(std::vector<PackageAuthoringTarget> &targets) {
        skip_whitespace();
        if (!consume('[')) {
            error_here("package authoring descriptor field 'exports' must be an array");
            return false;
        }

        skip_whitespace();
        if (consume(']')) {
            return true;
        }

        while (true) {
            PackageAuthoringTarget target;
            if (!parse_target_object("exports[]", target)) {
                return false;
            }
            targets.push_back(std::move(target));

            skip_whitespace();
            if (consume(']')) {
                return true;
            }
            if (!consume(',')) {
                error_here("expected ',' or ']' in exports array");
                return false;
            }
        }
    }

    [[nodiscard]] bool
    parse_capability_binding_object(PackageAuthoringCapabilityBinding &binding) {
        skip_whitespace();
        if (!consume('{')) {
            error_here("package authoring descriptor field 'capability_bindings[]' must be an object");
            return false;
        }

        std::optional<std::string> capability;
        std::optional<std::string> binding_key;
        std::unordered_set<std::string> seen_fields;

        skip_whitespace();
        if (consume('}')) {
            diagnostics_.error_in_source(
                "package authoring descriptor field 'capability_bindings[]' is missing required field 'capability'",
                source_,
                point_range(source_, 0));
            diagnostics_.error_in_source(
                "package authoring descriptor field 'capability_bindings[]' is missing required field 'binding_key'",
                source_,
                point_range(source_, 0));
            return false;
        }

        while (true) {
            auto key = parse_string("expected capability binding field name");
            if (!key.has_value()) {
                return false;
            }
            if (!seen_fields.insert(*key).second) {
                error_here("duplicate package authoring descriptor field 'capability_bindings[]." +
                           *key + "'");
                return false;
            }

            skip_whitespace();
            if (!consume(':')) {
                error_here("expected ':' after capability binding field name");
                return false;
            }

            if (*key == "capability") {
                capability = parse_string(
                    "package authoring descriptor capability binding field 'capability' must be a string");
            } else if (*key == "binding_key") {
                binding_key = parse_string(
                    "package authoring descriptor capability binding field 'binding_key' must be a string");
            } else {
                error_here("unsupported package authoring descriptor field 'capability_bindings[]." +
                           *key + "'");
                return false;
            }

            if (diagnostics_.has_error()) {
                return false;
            }

            skip_whitespace();
            if (consume('}')) {
                break;
            }
            if (!consume(',')) {
                error_here("expected ',' or '}' after capability binding field");
                return false;
            }
        }

        if (!capability.has_value()) {
            diagnostics_.error_in_source(
                "package authoring descriptor field 'capability_bindings[]' is missing required field 'capability'",
                source_,
                point_range(source_, 0));
        }
        if (!binding_key.has_value()) {
            diagnostics_.error_in_source(
                "package authoring descriptor field 'capability_bindings[]' is missing required field 'binding_key'",
                source_,
                point_range(source_, 0));
        }
        if (diagnostics_.has_error()) {
            return false;
        }

        binding.capability = std::move(*capability);
        binding.binding_key = std::move(*binding_key);
        return true;
    }

    [[nodiscard]] bool parse_capability_binding_array(
        std::vector<PackageAuthoringCapabilityBinding> &bindings) {
        skip_whitespace();
        if (!consume('[')) {
            error_here("package authoring descriptor field 'capability_bindings' must be an array");
            return false;
        }

        skip_whitespace();
        if (consume(']')) {
            return true;
        }

        while (true) {
            PackageAuthoringCapabilityBinding binding;
            if (!parse_capability_binding_object(binding)) {
                return false;
            }
            bindings.push_back(std::move(binding));

            skip_whitespace();
            if (consume(']')) {
                return true;
            }
            if (!consume(',')) {
                error_here("expected ',' or ']' in capability_bindings array");
                return false;
            }
        }
    }
};

[[nodiscard]] std::optional<std::string>
required_string_field(std::string_view field_name,
                      const std::unordered_map<std::string, DescriptorJsonValue> &fields,
                      DiagnosticBag &diagnostics,
                      const SourceFile &source) {
    const auto key = std::string(field_name);
    const auto it = fields.find(key);
    if (it == fields.end()) {
        diagnostics.error_in_source("project descriptor is missing required field '" + key + "'",
                                    source,
                                    point_range(source, 0));
        return std::nullopt;
    }

    if (it->second.kind != DescriptorJsonValue::Kind::String) {
        diagnostics.error_in_source("project descriptor field '" + key + "' must be a string",
                                    source,
                                    point_range(source, 0));
        return std::nullopt;
    }

    return it->second.string_value;
}

[[nodiscard]] std::optional<std::vector<std::string>>
required_string_array_field(std::string_view field_name,
                            const std::unordered_map<std::string, DescriptorJsonValue> &fields,
                            DiagnosticBag &diagnostics,
                            const SourceFile &source) {
    const auto key = std::string(field_name);
    const auto it = fields.find(key);
    if (it == fields.end()) {
        diagnostics.error_in_source("project descriptor is missing required field '" + key + "'",
                                    source,
                                    point_range(source, 0));
        return std::nullopt;
    }

    if (it->second.kind != DescriptorJsonValue::Kind::StringArray) {
        diagnostics.error_in_source("project descriptor field '" + key +
                                        "' must be an array of strings",
                                    source,
                                    point_range(source, 0));
        return std::nullopt;
    }

    return it->second.array_values;
}

void reject_unknown_project_fields(
    const std::unordered_map<std::string, DescriptorJsonValue> &fields,
    DiagnosticBag &diagnostics,
    const SourceFile &source) {
    static const std::unordered_set<std::string> allowed = {
        "format_version",
        "name",
        "search_roots",
        "entry_sources",
    };

    for (const auto &[field_name, _] : fields) {
        if (!allowed.contains(field_name)) {
            diagnostics.error_in_source("unsupported project descriptor field '" + field_name + "'",
                                        source,
                                        point_range(source, 0));
        }
    }
}

void reject_unknown_workspace_fields(
    const std::unordered_map<std::string, DescriptorJsonValue> &fields,
    DiagnosticBag &diagnostics,
    const SourceFile &source) {
    static const std::unordered_set<std::string> allowed = {
        "format_version",
        "name",
        "projects",
    };

    for (const auto &[field_name, _] : fields) {
        if (!allowed.contains(field_name)) {
            diagnostics.error_in_source("unsupported workspace descriptor field '" + field_name +
                                            "'",
                                        source,
                                        point_range(source, 0));
        }
    }
}

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
        diagnostics.error_in_source("descriptor field '" + std::string(field_name) +
                                        "' must not escape the descriptor root",
                                    source,
                                    point_range(source, 0));
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
            diagnostics.error_in_source("descriptor field '" + std::string(field_name) +
                                            "' contains duplicate path '" + raw_path + "'",
                                        source,
                                        point_range(source, 0));
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

[[nodiscard]] std::vector<std::filesystem::path> effective_search_roots(const ProjectInput &input) {
    std::vector<std::filesystem::path> roots;
    roots.reserve(input.search_roots.size() + input.entry_files.size());

    for (const auto &root : input.search_roots) {
        const auto normalized = normalize_path(root);
        if (std::find(roots.begin(), roots.end(), normalized) == roots.end()) {
            roots.push_back(normalized);
        }
    }

    if (!roots.empty()) {
        return roots;
    }

    for (const auto &entry_file : input.entry_files) {
        const auto normalized = normalize_path(entry_file).parent_path();
        if (std::find(roots.begin(), roots.end(), normalized) == roots.end()) {
            roots.push_back(normalized);
        }
    }

    return roots;
}

[[nodiscard]] std::optional<std::filesystem::path>
resolve_import_path(std::string_view module_name,
                    const std::vector<std::filesystem::path> &search_roots,
                    DiagnosticBag &diagnostics,
                    MaybeCRef<SourceFile> source = std::nullopt,
                    std::optional<SourceRange> range = std::nullopt) {
    const auto relative = module_relative_path(module_name);

    std::vector<std::filesystem::path> candidates;
    for (const auto &root : search_roots) {
        std::error_code error;
        const auto candidate = normalize_path(root / relative);
        if (!std::filesystem::exists(candidate, error) || error) {
            continue;
        }

        if (std::find(candidates.begin(), candidates.end(), candidate) == candidates.end()) {
            candidates.push_back(candidate);
        }
    }

    if (candidates.empty()) {
        const auto message = "failed to resolve imported module '" + std::string(module_name) + "'";
        if (source.has_value()) {
            diagnostics.error_in_source(message, source->get(), range);
        } else {
            diagnostics.error(message, range);
        }
        return std::nullopt;
    }

    if (candidates.size() > 1) {
        std::ostringstream builder;
        builder << "imported module '" << module_name << "' is ambiguous across search roots";
        if (source.has_value()) {
            diagnostics.error_in_source(builder.str(), source->get(), range);
        } else {
            diagnostics.error(builder.str(), range);
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

    DescriptorJsonParser parser(source, result.diagnostics);
    auto fields = parser.parse_object();
    if (!fields.has_value()) {
        return result;
    }

    reject_unknown_project_fields(*fields, result.diagnostics, source);
    if (result.has_errors()) {
        return result;
    }

    auto format_version =
        required_string_field("format_version", *fields, result.diagnostics, source);
    auto name = required_string_field("name", *fields, result.diagnostics, source);
    auto search_roots =
        required_string_array_field("search_roots", *fields, result.diagnostics, source);
    auto entry_sources =
        required_string_array_field("entry_sources", *fields, result.diagnostics, source);

    if (!format_version.has_value() || !name.has_value() || !search_roots.has_value() ||
        !entry_sources.has_value()) {
        return result;
    }

    if (*format_version != "ahfl.project.v0.3") {
        result.diagnostics.error_in_source("unsupported project descriptor format_version '" +
                                               *format_version + "'",
                                           source,
                                           point_range(source, 0));
        return result;
    }

    if (search_roots->empty()) {
        result.diagnostics.error_in_source(
            "project descriptor requires non-empty 'search_roots'", source, point_range(source, 0));
    }
    if (entry_sources->empty()) {
        result.diagnostics.error_in_source("project descriptor requires non-empty 'entry_sources'",
                                           source,
                                           point_range(source, 0));
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
    PackageAuthoringJsonParser parser(source, result.diagnostics);
    auto descriptor = parser.parse_descriptor(descriptor_path);
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

    DescriptorJsonParser parser(source, result.diagnostics);
    auto fields = parser.parse_object();
    if (!fields.has_value()) {
        return result;
    }

    reject_unknown_workspace_fields(*fields, result.diagnostics, source);
    if (result.has_errors()) {
        return result;
    }

    auto format_version =
        required_string_field("format_version", *fields, result.diagnostics, source);
    auto name = required_string_field("name", *fields, result.diagnostics, source);
    auto projects = required_string_array_field("projects", *fields, result.diagnostics, source);
    if (!format_version.has_value() || !name.has_value() || !projects.has_value()) {
        return result;
    }

    if (*format_version != "ahfl.workspace.v0.3") {
        result.diagnostics.error_in_source("unsupported workspace descriptor format_version '" +
                                               *format_version + "'",
                                           source,
                                           point_range(source, 0));
        return result;
    }

    if (projects->empty()) {
        result.diagnostics.error_in_source(
            "workspace descriptor requires non-empty 'projects'", source, point_range(source, 0));
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
        result.diagnostics.error("workspace project selection must not be empty");
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
            result.diagnostics.error("workspace contains duplicate project name '" +
                                     std::string(project_name) + "'");
            result.diagnostics.note("first matching project descriptor is '" +
                                    display_path(*first_match_path) + "'");
            result.diagnostics.note("second matching project descriptor is '" +
                                    display_path(project_result.descriptor->descriptor_path) + "'");
            return result;
        }

        first_match_path = project_result.descriptor->descriptor_path;
        selected_descriptor = std::move(*project_result.descriptor);
    }

    if (!selected_descriptor.has_value()) {
        result.diagnostics.error("workspace does not contain project named '" +
                                 std::string(project_name) + "'");
        return result;
    }

    result.descriptor = std::move(selected_descriptor);
    return result;
}

ProjectParseResult Frontend::parse_project(const ProjectInput &input) const {
    ProjectParseResult result;

    if (input.entry_files.empty()) {
        result.diagnostics.error("project input must contain at least one entry file");
        return result;
    }

    const auto search_roots = effective_search_roots(input);
    if (search_roots.empty()) {
        result.diagnostics.error("project input did not yield any search roots");
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
            return std::nullopt;
        }

        in_progress_paths.insert(path_key);
        auto parse_result = parse_file(path);
        result.diagnostics.append_from_source(parse_result.diagnostics, parse_result.source);

        std::optional<SourceId> loaded_id;

        if (parse_result.program && !parse_result.has_errors()) {
            const auto imports = collect_program_imports(*parse_result.program);
            if (imports.modules.empty()) {
                result.diagnostics.error_in_source(
                    "project-aware source file must declare exactly one module",
                    parse_result.source,
                    parse_result.program->range);
            } else if (imports.modules.size() > 1) {
                result.diagnostics.error_in_source(
                    "project-aware source file must declare exactly one module",
                    parse_result.source,
                    imports.modules[1].second);
                result.diagnostics.note_in_source("first module declaration is here",
                                                  parse_result.source,
                                                  imports.modules.front().second);
            } else if (imports.modules.front().first.empty()) {
                result.diagnostics.error_in_source("module declaration must not be empty",
                                                   parse_result.source,
                                                   imports.modules.front().second);
            } else {
                const auto &module_name = imports.modules.front().first;
                if (expected_module.has_value() && module_name != *expected_module) {
                    result.diagnostics.error_in_source(
                        "source file declares module '" + module_name + "' but import requested '" +
                            *expected_module + "'",
                        parse_result.source,
                        imports.modules.front().second);
                } else if (const auto existing = result.graph.module_to_source.find(module_name);
                           existing != result.graph.module_to_source.end()) {
                    result.diagnostics.error_in_source("duplicate module owner for '" +
                                                           module_name + "'",
                                                       parse_result.source,
                                                       imports.modules.front().second);
                    if (const auto previous = find_source_unit(result.graph, existing->second);
                        previous.has_value()) {
                        result.diagnostics.note_in_source("previous module owner is '" +
                                                              previous->get().source.display_name +
                                                              "'",
                                                          previous->get().source,
                                                          previous->get().module_range);
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
                    for (const auto &import_request : source_unit.imports) {
                        const auto imported_path =
                            resolve_import_path(import_request.module_name,
                                                search_roots,
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
                result.diagnostics.note_in_source("import requested module '" + *expected_module +
                                                      "' here",
                                                  request_source->get(),
                                                  import_range);
            } else {
                result.diagnostics.note("import requested module '" + *expected_module + "' here",
                                        import_range);
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
    out << "source_graph (" << graph.entry_sources.size() << " entry, " << graph.sources.size()
        << " sources, " << graph.import_edges.size() << " import"
        << (graph.import_edges.size() == 1 ? "" : "s") << ")\n";

    for (const auto &source : graph.sources) {
        out << "source " << source.source.display_name << '\n';
        out << "  module " << source.module_name << '\n';
        if (!source.imports.empty()) {
            out << "  imports\n";
            for (const auto &import_request : source.imports) {
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
