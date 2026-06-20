#pragma once

#include <iosfwd>
#include <vector>

#include "ahfl/compiler/frontend/ast.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"

namespace ahfl {

struct SourceGraph;

/// Generate IR from AST (single file)
[[nodiscard]] ir::Program lower_program_ir(const ast::Program &program,
                                           const ResolveResult &resolve_result,
                                           const TypeCheckResult &type_check_result);

/// Generate IR from SourceGraph (multiple files)
[[nodiscard]] ir::Program lower_program_ir(const SourceGraph &graph,
                                           const ResolveResult &resolve_result,
                                           const TypeCheckResult &type_check_result);

/// Collect formal observations
[[nodiscard]] std::vector<ir::FormalObservation>
collect_formal_observations(const ir::Program &program);

/// Print IR (human-readable format)
void print_program_ir(const ir::Program &program, std::ostream &out);

/// Print IR (JSON format)
void print_program_ir_json(const ir::Program &program, std::ostream &out);

/// Emit IR to output stream (human-readable format)
void emit_program_ir(const ast::Program &program,
                     const ResolveResult &resolve_result,
                     const TypeCheckResult &type_check_result,
                     std::ostream &out);

void emit_program_ir(const SourceGraph &graph,
                     const ResolveResult &resolve_result,
                     const TypeCheckResult &type_check_result,
                     std::ostream &out);

/// Emit IR to output stream (JSON format)
void emit_program_ir_json(const ast::Program &program,
                          const ResolveResult &resolve_result,
                          const TypeCheckResult &type_check_result,
                          std::ostream &out);

void emit_program_ir_json(const SourceGraph &graph,
                          const ResolveResult &resolve_result,
                          const TypeCheckResult &type_check_result,
                          std::ostream &out);

} // namespace ahfl
