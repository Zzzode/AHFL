#include "compiler/syntax/frontend/project.hpp"

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

[[nodiscard]] std::optional<std::string> use_target_module(const ast::UseDecl &use_decl) {
    if (use_decl.path == nullptr || use_decl.path->segments.size() < 2) {
        return std::nullopt;
    }

    std::string module_name;
    for (std::size_t index = 0; index + 1 < use_decl.path->segments.size(); ++index) {
        if (!module_name.empty()) {
            module_name += "::";
        }
        module_name += use_decl.path->segments[index];
    }
    return module_name;
}

[[nodiscard]] ProgramImports collect_program_imports(const ast::Program &program) {
    ProgramImports result;

    for (const auto &declaration : program.declarations) {
        if (const auto *module = std::get_if<ast::ModuleDecl>(&declaration)) {
            result.modules.push_back(
                {module->name ? module->name->spelling() : "", module->range});
            continue;
        }
        if (const auto *import = std::get_if<ast::ImportDecl>(&declaration)) {
            result.imports.push_back(ImportRequest{
                .module_name = import->path ? import->path->spelling() : "",
                .alias = import->alias,
                .range = import->range,
            });
            continue;
        }
        if (const auto *use_decl = std::get_if<ast::UseDecl>(&declaration)) {
            const auto module_name = use_target_module(*use_decl);
            result.imports.push_back(ImportRequest{
                .module_name = module_name.value_or(std::string{}),
                .alias = "",
                .range = use_decl->range,
            });
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

[[nodiscard]] std::filesystem::path module_relative_path_after_prefix(std::string_view module_name,
                                                                      std::string_view prefix) {
    if (module_name == prefix) {
        return std::filesystem::path{"mod.ahfl"};
    }
    return module_relative_path(module_name.substr(prefix.size() + 2));
}

[[nodiscard]] std::vector<std::filesystem::path> effective_search_roots(const ProjectInput &input) {
    std::vector<std::filesystem::path> roots;
    roots.reserve(input.search_roots.size() + input.entry_files.size());

    for (const auto &root : input.search_roots) {
        append_unique_normalized_path(roots, root);
    }

    if (roots.empty()) {
        for (const auto &entry_file : input.entry_files) {
            append_unique_normalized_path(roots, normalize_path(entry_file).parent_path());
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
                .artifact_exports = root.artifact_exports,
                .dependency_prefixes = root.dependency_prefixes,
                .compiler_intrinsics_allow = root.compiler_intrinsics_allow,
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

[[nodiscard]] bool module_dependency_allowed(const ProjectInput::ModuleRoot &importer,
                                             const ProjectInput::ModuleRoot &imported) {
    return std::find(importer.dependency_prefixes.begin(),
                     importer.dependency_prefixes.end(),
                     imported.prefix) != importer.dependency_prefixes.end();
}

[[nodiscard]] bool enforce_import_visibility(std::string_view importer_module,
                                             const ImportRequest &import_request,
                                             const std::vector<ProjectInput::ModuleRoot> &roots,
                                             bool enforce_package_dependencies,
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

    if (enforce_package_dependencies &&
        !module_dependency_allowed(*importer_root, *imported_root)) {
        diagnostics.error()
            .code("E::package_dependency_missing")
            .message("package prefix '" + importer_root->prefix +
                     "' does not depend on package prefix '" + imported_root->prefix +
                     "' required by import '" + import_request.module_name + "'")
            .range(import_request.range)
            .source(source)
            .emit();
        return false;
    }

    if (module_is_exported(import_request.module_name, *imported_root)) {
        return true;
    }

    diagnostics.error()
        .code(error_codes::visibility::PrivateModule)
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

[[nodiscard]] bool should_inject_prelude(bool inject_prelude, std::string_view module_name) {
    return inject_prelude && !is_std_module(module_name);
}

[[nodiscard]] std::optional<std::filesystem::path>
resolve_import_path(std::string_view module_name,
                    const std::vector<std::filesystem::path> &search_roots,
                    const std::vector<ProjectInput::ModuleRoot> &module_roots,
                    const std::unordered_map<std::string, const ProjectSource *> &sources,
                    DiagnosticBag &diagnostics,
                    MaybeCRef<SourceFile> source = std::nullopt,
                    std::optional<SourceRange> range = std::nullopt) {
    // The parser's own existence gate, replayed against the frozen snapshot: a
    // path is a candidate exactly when `std::filesystem::exists` would have said
    // so at resolve time. An overlay / cached text whose path is not on disk
    // never satisfies it, matching the direct pipeline.
    const auto exists_on_disk = [&](const std::filesystem::path &path) {
        const auto found = sources.find(path.string());
        return found != sources.end() && found->second->exists_on_disk;
    };

    std::vector<std::filesystem::path> candidates;
    if (!module_roots.empty()) {
        for (const auto &root : module_roots) {
            if (!module_has_prefix(module_name, root.prefix)) {
                continue;
            }
            const auto relative = module_relative_path_after_prefix(module_name, root.prefix);
            const auto candidate = normalize_path(root.root / relative);
            if (exists_on_disk(candidate)) {
                if (std::find(candidates.begin(), candidates.end(), candidate) ==
                    candidates.end()) {
                    candidates.push_back(candidate);
                }
                continue;
            }
            const auto dir_candidate =
                normalize_path(root.root / relative.parent_path() / relative.stem() / "mod.ahfl");
            if (exists_on_disk(dir_candidate)) {
                if (std::find(candidates.begin(), candidates.end(), dir_candidate) ==
                    candidates.end()) {
                    candidates.push_back(dir_candidate);
                }
            }
        }
    } else {
        const auto relative = module_relative_path(module_name);
        for (const auto &root : search_roots) {
            // Try single-file layout: root/path/to/module.ahfl
            const auto candidate = normalize_path(root / relative);
            if (exists_on_disk(candidate)) {
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
            if (exists_on_disk(dir_candidate)) {
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

// Read a whole file as text. Read-only, no diagnostics: the parser renders its
// own "failed to open source file" diagnostic when a requested path is absent
// from the model, so a read failure here simply leaves the path out of the
// model — the parser's error path is the single one.
[[nodiscard]] std::optional<std::string> read_source_text(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

void append_model_source(std::vector<ProjectSource> &sources,
                         std::filesystem::path path,
                         std::optional<std::string> text,
                         bool exists_on_disk) {
    sources.push_back(
        ProjectSource{.path = std::move(path), .text = std::move(text), .exists_on_disk = exists_on_disk});
}

// Collect every `.ahfl` path under a root and freeze what the parser's
// filesystem probes would have seen: regular files contribute their text, and
// directories whose name ends in `.ahfl` contribute existence only (the direct
// pipeline's `std::filesystem::exists` candidate gate accepts them; loading one
// then reads zero bytes and produces a parse error, which the model reproduces
// by leaving the text absent). Root does not exist / is not a directory:
// nothing to add.
void append_ahfl_sources_under(std::vector<ProjectSource> &sources,
                               const std::filesystem::path &root) {
    std::error_code error;
    if (!std::filesystem::is_directory(root, error) || error) {
        return;
    }

    std::vector<std::filesystem::path> files;
    std::vector<std::filesystem::path> directories;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(
             root, std::filesystem::directory_options::skip_permission_denied, error)) {
        if (error) {
            break;
        }
        if (entry.path().extension() != ".ahfl") {
            continue;
        }
        if (entry.is_regular_file(error) && !error) {
            files.push_back(normalize_path(entry.path()));
        } else if (entry.is_directory(error) && !error) {
            directories.push_back(normalize_path(entry.path()));
            // A directory named `*.ahfl` is still descended into (its own
            // `.ahfl` children are real sources); only its own existence flag
            // is recorded.
            error.clear();
        }
        error.clear();
    }

    const auto append_sorted = [&sources](std::vector<std::filesystem::path> &paths, bool is_file) {
        std::sort(paths.begin(), paths.end());
        paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
        for (const auto &path : paths) {
            append_model_source(sources,
                                path,
                                is_file ? read_source_text(path) : std::nullopt,
                                /*exists_on_disk=*/true);
        }
    };
    append_sorted(directories, /*is_file=*/false);
    append_sorted(files, /*is_file=*/true);
}

} // namespace

ProjectInputModel resolve_project_input(const ProjectInput &input) {
    ProjectInputModel model;
    // The model's roots are the *effective* ones: the parser normalizes them
    // again, and the normalization is idempotent, so freezing the resolved form
    // keeps the model byte-equal to what the parser computes.
    model.entry_files = input.entry_files;
    model.search_roots = effective_search_roots(input);
    model.module_roots = effective_module_roots(input);
    model.inject_prelude = input.inject_prelude;
    model.enforce_package_dependencies = input.enforce_package_dependencies;

    // Explicit texts (overlays and cached sources) come first: the parser
    // prefers them over disk. Their key is stored verbatim (not re-normalized):
    // the parser looks them up by the *already normalized* path string of the
    // source it is loading, so preserving the caller's exact key string keeps
    // the original hit/miss behavior — re-normalizing could make a key that
    // never matched start matching.
    //
    // `exists_on_disk` is left false for these: the direct pipeline only ever
    // substitutes an overlay / cached text *after* the parser has decided the
    // path is a candidate via `std::filesystem::exists`, and for a path that is
    // on disk the disk entry below still supplies the flag. A cached text for a
    // path not on disk therefore behaves exactly as it did before — substituted
    // on load, but never able to satisfy import resolution on its own.
    for (const auto &[path_key, text] : input.source_overlays) {
        append_model_source(
            model.sources, std::filesystem::path{path_key}, text, /*exists_on_disk=*/false);
    }
    for (const auto &[path_key, text] : input.source_cache) {
        append_model_source(
            model.sources, std::filesystem::path{path_key}, text, /*exists_on_disk=*/false);
    }

    // Disk snapshot: entry files plus every `.ahfl` file under the effective
    // roots. Over-inclusive on purpose — the parser resolves imports lazily, so
    // a later import may load a file that no entry reaches syntactically.
    for (const auto &entry_file : model.entry_files) {
        const auto entry = normalize_path(entry_file);
        append_ahfl_sources_under(model.sources, entry.parent_path());
        append_model_source(model.sources, entry, read_source_text(entry), /*exists_on_disk=*/true);
    }
    for (const auto &root : model.search_roots) {
        append_ahfl_sources_under(model.sources, root);
    }
    for (const auto &root : model.module_roots) {
        append_ahfl_sources_under(model.sources, root.root);
    }

    // Deduplicate by path, merging the two facts each occurrence contributes:
    // the text (an explicit overlay / cache wins over disk, so the first
    // non-null text is kept) and `exists_on_disk` (true if ANY occurrence saw
    // the path on disk — a disk file with no overlay is still a real path, and
    // an overlay for a path that IS on disk must not erase that fact). The final
    // order is a pure function of the path set, which is what lets two equal
    // models compare equal regardless of discovery order.
    std::stable_sort(model.sources.begin(),
                     model.sources.end(),
                     [](const ProjectSource &lhs, const ProjectSource &rhs) {
                         return lhs.path.native() < rhs.path.native();
                     });
    std::vector<ProjectSource> deduped;
    deduped.reserve(model.sources.size());
    for (auto &source : model.sources) {
        if (!deduped.empty() && deduped.back().path == source.path) {
            auto &merged = deduped.back();
            if (!merged.text.has_value()) {
                merged.text = std::move(source.text);
            }
            merged.exists_on_disk = merged.exists_on_disk || source.exists_on_disk;
            continue;
        }
        deduped.push_back(std::move(source));
    }
    model.sources = std::move(deduped);
    return model;
}

ProjectParseResult parse_project(const Frontend &frontend, const ProjectInputModel &model) {
    ProjectParseResult result;

    if (model.entry_files.empty()) {
        result.diagnostics.error()
            .message("project input must contain at least one entry file")
            .emit();
        return result;
    }

    if (model.search_roots.empty() && model.module_roots.empty()) {
        result.diagnostics.error()
            .message("project input did not yield any module or search roots")
            .emit();
        return result;
    }

    // The model is the complete text universe: every path the parser can reach
    // is either here (parse the frozen text) or absent (a source-load failure,
    // rendered exactly as `parse_file` would render a missing file). No
    // filesystem probe happens below this line.
    std::unordered_map<std::string, const ProjectSource *> sources;
    sources.reserve(model.sources.size());
    for (const auto &source : model.sources) {
        sources.emplace(source.path.string(), &source);
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
            const auto found = sources.find(path_key);
            if (found == sources.end()) {
                // Not in the frozen snapshot at all: treat as a missing file so
                // the Frontend's own file reader renders the diagnostic. (The
                // direct pipeline reaches this only if the path was deleted
                // between resolution and load.)
                return frontend.parse_file(path);
            }
            if (!found->second->text.has_value()) {
                // The path was on disk but its text could not be read: the same
                // "failed to open source file" diagnostic `parse_file` renders.
                return frontend.parse_file(path);
            }
            return frontend.parse_text(display_path(path), *found->second->text);
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
                    const auto *module_root = find_module_root(module_name, model.module_roots);
                    path_to_source.emplace(path_key, source_id);
                    result.graph.module_to_source.emplace(module_name, source_id);
                    result.graph.sources.push_back(SourceUnit{
                        .id = source_id,
                        .path = path,
                        .module_name = module_name,
                        .package_prefix = module_root == nullptr ? std::string{} : module_root->prefix,
                        .module_exported =
                            module_root == nullptr ? false : module_is_exported(module_name, *module_root),
                        .artifact_exports =
                            module_root == nullptr ? std::vector<std::string>{}
                                                   : module_root->artifact_exports,
                        .dependency_prefixes =
                            module_root == nullptr ? std::vector<std::string>{}
                                                   : module_root->dependency_prefixes,
                        .module_range = imports.modules.front().second,
                        .compiler_intrinsics_allow = module_root == nullptr
                                                         ? std::nullopt
                                                         : module_root->compiler_intrinsics_allow,
                        .source = std::move(parse_result.source),
                        .program = std::move(parse_result.program),
                        .imports = imports.imports,
                    });

                    loaded_id = source_id;

                    auto &source_unit = result.graph.sources.back();
                    // RFC 0013 P6: a leading `#![no_prelude]` inner attribute on
                    // the source unit opts it out of implicit prelude injection.
                    const bool file_opts_out =
                        source_unit.program != nullptr && source_unit.program->suppress_prelude;
                    if (!file_opts_out &&
                        should_inject_prelude(model.inject_prelude, source_unit.module_name)) {
                        source_unit.imports.push_back(ImportRequest{
                            .module_name = std::string(kStdPreludeModule),
                            .alias = "",
                            .range = source_unit.module_range,
                        });
                    }
                    // Snapshot importer identity and the import list before
                    // resolving imports. Resolving an import recursively calls
                    // load_source, which push_backs into result.graph.sources
                    // and may reallocate that vector, invalidating references
                    // into it (including `source_unit` above). Addressable
                    // members are re-fetched through the stable SourceId index.
                    const auto importer_module = source_unit.module_name;
                    const auto imports_to_load = source_unit.imports;
                    for (const auto &import_request : imports_to_load) {
                        if (import_request.module_name == importer_module) {
                            result.graph.import_edges.push_back(ImportEdge{
                                .importer = source_id,
                                .imported = source_id,
                                .request = import_request,
                            });
                            continue;
                        }
                        if (!enforce_import_visibility(
                                importer_module,
                                import_request,
                                model.module_roots,
                                model.enforce_package_dependencies,
                                result.diagnostics,
                                result.graph.sources[source_id.value].source)) {
                            continue;
                        }
                        const auto imported_path =
                            resolve_import_path(import_request.module_name,
                                                model.search_roots,
                                                model.module_roots,
                                                sources,
                                                result.diagnostics,
                                                std::cref(result.graph.sources[source_id.value].source),
                                                import_request.range);
                        if (!imported_path.has_value()) {
                            continue;
                        }

                        const auto imported_id =
                            load_source(*imported_path,
                                        import_request.module_name,
                                        std::cref(result.graph.sources[source_id.value].source),
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

    for (const auto &entry_file : model.entry_files) {
        const auto entry_id = load_source(entry_file, std::nullopt, std::nullopt, std::nullopt);
        if (entry_id.has_value()) {
            result.graph.entry_sources.push_back(*entry_id);
        }
    }

    return result;
}

ProjectParseResult parse_project(const Frontend &frontend, const ProjectInput &input) {
    // The direct route: freeze the input into a model (the one filesystem read
    // boundary) and parse it. Both routes share this single body, so they cannot
    // drift; `resolve_project_input` is pure with respect to the input value, so
    // an already-built model would parse identically.
    return parse_project(frontend, resolve_project_input(input));
}

} // namespace ahfl
