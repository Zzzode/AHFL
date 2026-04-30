#pragma once

#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "ahfl/frontend/ast.hpp"
#include "ahfl/support/diagnostics.hpp"
#include "ahfl/support/ownership.hpp"
#include "ahfl/support/source.hpp"

namespace ahfl {

inline constexpr std::string_view kPackageAuthoringFormatVersion = "ahfl.package-authoring.v0.5";

struct FrontendOptions {
    bool emit_parse_note{false};
};

struct ParseResult {
    // Public result of the parse + AST lowering boundary. Generated parser
    // objects remain an implementation detail of src/frontend/frontend.cpp.
    SourceFile source;
    Owned<ast::Program> program;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct SourceUnit {
    SourceId id;
    std::filesystem::path path;
    std::string module_name;
    SourceRange module_range;
    SourceFile source;
    Owned<ast::Program> program;
    std::vector<ImportRequest> imports;
};

struct ImportEdge {
    SourceId importer;
    SourceId imported;
    ImportRequest request;
};

struct SourceGraph {
    std::vector<SourceId> entry_sources;
    std::vector<SourceUnit> sources;
    std::unordered_map<std::string, SourceId> module_to_source;
    std::vector<ImportEdge> import_edges;
};

struct ProjectInput {
    std::vector<std::filesystem::path> entry_files;
    std::vector<std::filesystem::path> search_roots;
};

struct ProjectDescriptor {
    std::filesystem::path descriptor_path;
    std::string format_version;
    std::string name;
    std::vector<std::filesystem::path> entry_files;
    std::vector<std::filesystem::path> search_roots;
};

struct WorkspaceDescriptor {
    std::filesystem::path descriptor_path;
    std::string format_version;
    std::string name;
    std::vector<std::filesystem::path> projects;
};

enum class PackageAuthoringTargetKind {
    Agent,
    Workflow,
};

struct PackageAuthoringTarget {
    PackageAuthoringTargetKind kind{PackageAuthoringTargetKind::Workflow};
    std::string name;
};

struct PackageAuthoringCapabilityBinding {
    std::string capability;
    std::string binding_key;
};

struct PackageAuthoringDescriptor {
    std::filesystem::path descriptor_path;
    std::string format_version;
    std::string package_name;
    std::string package_version;
    PackageAuthoringTarget entry;
    std::vector<PackageAuthoringTarget> exports;
    std::vector<PackageAuthoringCapabilityBinding> capability_bindings;
};

struct WorkspaceDescriptorParseResult {
    std::optional<WorkspaceDescriptor> descriptor;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProjectDescriptorParseResult {
    std::optional<ProjectDescriptor> descriptor;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct PackageAuthoringDescriptorParseResult {
    std::optional<PackageAuthoringDescriptor> descriptor;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProjectParseResult {
    SourceGraph graph;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

class Frontend {
  public:
    explicit Frontend(FrontendOptions options = {});

    // These entry points stop at the hand-written AST boundary. Later
    // resolve/typecheck/validate/emission stages must consume ast::Program
    // rather than ANTLR parse-tree nodes.
    [[nodiscard]] ParseResult parse_file(const std::filesystem::path &path) const;
    [[nodiscard]] ParseResult parse_text(std::string display_name, std::string text) const;
    [[nodiscard]] ProjectDescriptorParseResult
    load_project_descriptor(const std::filesystem::path &path) const;
    [[nodiscard]] PackageAuthoringDescriptorParseResult
    load_package_authoring_descriptor(const std::filesystem::path &path) const;
    [[nodiscard]] WorkspaceDescriptorParseResult
    load_workspace_descriptor(const std::filesystem::path &path) const;
    [[nodiscard]] ProjectDescriptorParseResult
    load_project_descriptor_from_workspace(const std::filesystem::path &workspace_path,
                                           std::string_view project_name) const;
    [[nodiscard]] ProjectParseResult parse_project(const ProjectInput &input) const;

  private:
    FrontendOptions options_;
};

void dump_program_outline(const ast::Program &program, std::ostream &out);
void dump_project_outline(const SourceGraph &graph, std::ostream &out);
void dump_project_ast_outline(const SourceGraph &graph, std::ostream &out);

} // namespace ahfl
