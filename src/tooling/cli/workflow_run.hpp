#pragma once

#include "ahfl/base/support/source.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "runtime/wasm_runner/wasm_workflow_runtime.hpp"
#include "tooling/cli/command_catalog.hpp"

#include <functional>
#include <iosfwd>
#include <optional>

namespace ahfl::cli {

// kr68 §12.16: the host-supplied context that lets the CLI render compile-
// pipeline diagnostics with source locations. The locator resolves a
// diagnostic's owning module + SourceRange to a displayable source location;
// primary_source supplies the single-file SourceFile for caret rendering
// (nullopt for a SourceGraph, which renders (file:line:col) without caret).
struct WorkflowRunSourceContext {
    std::function<std::optional<ahfl::runtime::wasm_runner::LocatedDiagnosticSource>(
        std::string_view, const ahfl::SourceRange &)>
        locate_compile_diagnostic;
    std::optional<std::reference_wrapper<const ahfl::SourceFile>> primary_source;
};

[[nodiscard]] int run_workflow_with_llm(const ahfl::ir::Program &program,
                                        const CommandLineOptions &options,
                                        std::ostream &out,
                                        std::ostream &err,
                                        const WorkflowRunSourceContext &source_context = {});

} // namespace ahfl::cli
