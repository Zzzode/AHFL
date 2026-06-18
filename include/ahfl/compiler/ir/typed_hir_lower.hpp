#pragma once

#include "ahfl/compiler/ir/ir.hpp"
#include "ahfl/compiler/semantics/typed_hir.hpp"

namespace ahfl {

struct SourceGraph;

// Lower a TypedProgram. ast::Program / SourceGraph overloads are compatibility
// shims; all Semantic IR facts are read from the TypedProgram snapshot.
[[nodiscard]] ir::Program lower_typed_program(const TypedProgram &program,
                                              const ast::Program &ast_program);
[[nodiscard]] ir::Program lower_typed_program(const TypedProgram &program,
                                              const SourceGraph &source_graph);
[[nodiscard]] ir::Program lower_typed_program(const TypedProgram &program);

} // namespace ahfl
