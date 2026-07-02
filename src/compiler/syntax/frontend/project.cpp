#include "ahfl/compiler/frontend/frontend.hpp"

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

constexpr std::string_view kStdPreludeModule = "std::prelude";

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

void append_unique_normalized_path(std::vector<std::filesystem::path> &paths,
                                   const std::filesystem::path &path) {
    const auto normalized = normalize_path(path);
    if (std::find(paths.begin(), paths.end(), normalized) == paths.end()) {
        paths.push_back(normalized);
    }
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
