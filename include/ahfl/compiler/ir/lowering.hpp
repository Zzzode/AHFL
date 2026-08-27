#pragma once

#include <iosfwd>
#include <optional>
#include <string_view>
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
                                           const TypeCheckResult &type_check_result,
                                           bool include_stdlib = false);

/// Collect formal observations
[[nodiscard]] std::vector<ir::FormalObservation>
collect_formal_observations(const ir::Program &program);

/// Print IR (human-readable format)
void print_program_ir(const ir::Program &program, std::ostream &out);

/// Print IR (JSON format)
void print_program_ir_json(const ir::Program &program, std::ostream &out);

/// KR5.9: parse IR JSON (as produced by print_program_ir_json) back into an
/// ir::Program. Round-trips byte-identically: re-emitting the returned program
/// with print_program_ir_json reproduces the input verbatim. Returns nullopt
/// on malformed input or a missing required field. Derived analyses
/// (formal observations, flow/workflow summaries) are recomputed from the
/// parsed declarations rather than read from JSON, matching what the emitter
/// itself recomputes on the forward path.
[[nodiscard]] std::optional<ir::Program> parse_program_ir_json(std::string_view json);

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
