#include "compiler/manifest/manifest.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace ahfl::manifest {
namespace {

void append_basic_string(std::ostream &out, std::string_view value) {
    out << '"';
    for (const unsigned char ch : value) {
        switch (ch) {
        case '\b':
            out << "\\b";
            break;
        case '\t':
            out << "\\t";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\f':
            out << "\\f";
            break;
        case '\r':
            out << "\\r";
            break;
        case '"':
            out << "\\\"";
            break;
        case '\\':
            out << "\\\\";
            break;
        default:
            if (ch < 0x20U) {
                out << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
                    << static_cast<int>(ch) << std::dec << std::setfill(' ');
            } else {
                out << static_cast<char>(ch);
            }
            break;
        }
    }
    out << '"';
}

void append_key_value(std::ostream &out, std::string_view key, std::string_view value) {
    out << key << " = ";
    append_basic_string(out, value);
    out << '\n';
}

void append_string_array(std::ostream &out,
                         std::string_view key,
                         const std::vector<std::string> &values) {
    out << key << " = [";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            out << ", ";
        }
        append_basic_string(out, values[index]);
    }
    out << "]\n";
}

void append_exported_modules_array(std::ostream &out,
                                   std::string_view key,
                                   const std::vector<ExportedModuleManifest> &values) {
    out << key << " = [";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            out << ", ";
        }
        append_basic_string(out, values[index].module_path);
    }
    out << "]\n";
}

void append_handoff_export_array(std::ostream &out,
                                 std::string_view key,
                                 const std::vector<HandoffExportManifest> &values) {
    out << key << " = [";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            out << ", ";
        }
        out << "{ kind = ";
        append_basic_string(out, values[index].kind);
        out << ", name = ";
        append_basic_string(out, values[index].name);
        out << " }";
    }
    out << "]\n";
}

[[nodiscard]] std::vector<const DependencySpec *>
sorted_dependencies(const std::vector<DependencySpec> &dependencies) {
    std::vector<const DependencySpec *> sorted;
    sorted.reserve(dependencies.size());
    for (const auto &dependency : dependencies) {
        sorted.push_back(&dependency);
    }
    std::sort(sorted.begin(), sorted.end(), [](const auto *lhs, const auto *rhs) {
        return lhs->key < rhs->key;
    });
    return sorted;
}

[[nodiscard]] std::vector<const TargetManifest *>
sorted_targets(const std::vector<TargetManifest> &targets) {
    std::vector<const TargetManifest *> sorted;
    sorted.reserve(targets.size());
    for (const auto &target : targets) {
        sorted.push_back(&target);
    }
    std::sort(sorted.begin(), sorted.end(), [](const auto *lhs, const auto *rhs) {
        return lhs->name < rhs->name;
    });
    return sorted;
}

void append_dependency(std::ostream &out, const DependencySpec &dependency) {
    out << dependency.key << " = { source = ";
    append_basic_string(out, dependency.source);
    if (dependency.path.has_value()) {
        out << ", path = ";
        append_basic_string(out, *dependency.path);
    }
    if (dependency.version.has_value()) {
        out << ", version = ";
        append_basic_string(out, *dependency.version);
    }
    out << " }\n";
}

void append_dependencies(std::ostream &out, const std::vector<DependencySpec> &dependencies) {
    if (dependencies.empty()) {
        return;
    }

    out << "\n[dependencies]\n";
    for (const auto *dependency : sorted_dependencies(dependencies)) {
        append_dependency(out, *dependency);
    }
}

} // namespace

std::string canonicalize_package_manifest(const PackageManifest &manifest) {
    std::ostringstream out;
    out << "manifest_version = " << manifest.manifest_version << "\n\n";

    out << "[package]\n";
    append_key_value(out, "name", manifest.package_name);
    append_key_value(out, "version", manifest.package_version);
    append_key_value(out, "edition", manifest.edition);
    append_key_value(out, "kind", manifest.package_kind);

    out << "\n[module]\n";
    append_key_value(out, "prefix", manifest.module_prefix);
    append_key_value(out, "root", manifest.module_root);

    if (!manifest.exported_modules.empty()) {
        out << "\n[exports]\n";
        append_exported_modules_array(out, "modules", manifest.exported_modules);
    }

    if (manifest.prelude_module.has_value() || manifest.prelude_injection.has_value()) {
        out << "\n[prelude]\n";
        if (manifest.prelude_module.has_value()) {
            append_key_value(out, "module", *manifest.prelude_module);
        }
        if (manifest.prelude_injection.has_value()) {
            append_key_value(out, "injection", *manifest.prelude_injection);
        }
    }

    if (!manifest.compiler_intrinsics_allow.empty()) {
        out << "\n[compiler_intrinsics]\n";
        append_string_array(out, "allow", manifest.compiler_intrinsics_allow);
    }

    for (const auto *target : sorted_targets(manifest.targets)) {
        out << "\n[targets." << target->name << "]\n";
        append_key_value(out, "kind", target->kind);
        append_key_value(out, "entry", target->entry);
        if (!target->exports.empty()) {
            append_handoff_export_array(out, "exports", target->exports);
        }
        for (const auto &binding : target->capability_bindings) {
            out << "\n[[targets." << target->name << ".capability_bindings]]\n";
            append_key_value(out, "capability", binding.capability);
            append_key_value(out, "binding_key", binding.binding_key);
        }
    }

    append_dependencies(out, manifest.dependencies);
    return out.str();
}

std::string canonicalize_workspace_manifest(const WorkspaceManifest &manifest) {
    std::ostringstream out;
    out << "manifest_version = " << manifest.manifest_version << "\n\n";

    out << "[workspace]\n";
    append_key_value(out, "name", manifest.workspace_name);
    append_string_array(out, "members", manifest.members);

    out << "\n[resolver]\n";
    out << "version = " << manifest.resolver_version << '\n';

    append_dependencies(out, manifest.dependencies);
    return out.str();
}

} // namespace ahfl::manifest
