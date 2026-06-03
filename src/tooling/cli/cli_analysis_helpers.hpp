#pragma once

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/handoff/package.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "tooling/cli/command_catalog.hpp"
#include "tooling/cli/diagnostic_consumer.hpp"

#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>

namespace ahfl::cli {

// ---------------------------------------------------------------------------
// Compile pipeline helpers used by ahflc commands.
// ---------------------------------------------------------------------------

/// Full compile pipeline: parse → resolve → typecheck → validate → lower to IR.
/// Diagnostics are rendered to \p diagnostics on failure.
[[nodiscard]] std::optional<ir::Program> compile_to_ir(const std::filesystem::path &file_path,
                                                       std::ostream &diagnostics);

// ---------------------------------------------------------------------------
// Package metadata lowering
// ---------------------------------------------------------------------------

[[nodiscard]] ahfl::handoff::PackageMetadata
lower_package_metadata(const ahfl::PackageAuthoringDescriptor &descriptor);

// ---------------------------------------------------------------------------
// Core backend emission
// ---------------------------------------------------------------------------

[[nodiscard]] std::optional<int>
emit_core_backend(std::optional<CommandKind> effective_command,
                  const ahfl::ir::Program &program,
                  const ahfl::ResolveResult &resolve_result,
                  const ahfl::TypeCheckResult &type_check_result,
                  const ahfl::handoff::PackageMetadata *package_metadata,
                  std::ostream &out);

/// Template overload: lowers InputT to IR, then delegates to the ir::Program overload.
template <typename InputT>
[[nodiscard]] std::optional<int>
emit_core_backend(std::optional<CommandKind> effective_command,
                  const InputT &input,
                  const ahfl::ResolveResult &resolve_result,
                  const ahfl::TypeCheckResult &type_check_result,
                  const ahfl::handoff::PackageMetadata *package_metadata,
                  std::ostream &out) {
    const auto backend = core_backend_for_command(effective_command);
    if (!backend.has_value()) {
        return std::nullopt;
    }

    const auto ir_program = ahfl::lower_program_ir(input, resolve_result, type_check_result);
    return emit_core_backend(
        effective_command, ir_program, resolve_result, type_check_result, package_metadata, out);
}

// ---------------------------------------------------------------------------
// Analysis helpers
// ---------------------------------------------------------------------------

[[nodiscard]] int validate_assurance_program(const ahfl::ir::Program &program);

[[nodiscard]] int verify_formal_program(const ahfl::ir::Program &program,
                                        const CommandLineOptions &options);

[[nodiscard]] bool
read_text_file(const std::filesystem::path &path, std::string &content, std::ostream &diagnostics);

void dump_ast_outline(const ahfl::ast::Program &program, std::ostream &out);
void dump_ast_outline(const ahfl::SourceGraph &graph, std::ostream &out);

void print_success_summary(const ahfl::ast::Program &program,
                           const ahfl::ResolveResult &resolve_result,
                           const ahfl::TypeCheckResult &type_check_result,
                           std::ostream &out);

void print_success_summary(const ahfl::SourceGraph &graph,
                           const ahfl::ResolveResult &resolve_result,
                           const ahfl::TypeCheckResult &type_check_result,
                           std::ostream &out);

[[nodiscard]] int load_project_input(const CommandLineOptions &options,
                                     const ahfl::Frontend &frontend,
                                     ahfl::ProjectInput &input,
                                     DiagnosticConsumer &consumer);

// ---------------------------------------------------------------------------
// Render diagnostics (template — inline)
// ---------------------------------------------------------------------------

using MaybeSourceFile = std::optional<std::reference_wrapper<const ahfl::SourceFile>>;

template <typename ResultT>
void render_diagnostics(DiagnosticConsumer &consumer,
                        const ResultT &result,
                        MaybeSourceFile source_file) {
    consumer.consume(result.diagnostics, source_file);
}

} // namespace ahfl::cli
