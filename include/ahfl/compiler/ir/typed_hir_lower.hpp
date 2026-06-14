#pragma once

#include "ahfl/compiler/ir/ir.hpp"
#include "ahfl/compiler/semantics/typed_hir.hpp"

namespace ahfl {

struct SourceGraph;

// Lower a TypedProgram. The ast::Program / SourceGraph overloads preserve
// caller-supplied declaration ordering; the single-argument overload uses the
// TypedProgram resolver/source snapshot only.
[[nodiscard]] ir::Program lower_typed_program(const TypedProgram &program,
                                              const ast::Program &ast_program);
[[nodiscard]] ir::Program lower_typed_program(const TypedProgram &program,
                                              const SourceGraph &source_graph);
[[nodiscard]] ir::Program lower_typed_program(const TypedProgram &program);

} // namespace ahfl
