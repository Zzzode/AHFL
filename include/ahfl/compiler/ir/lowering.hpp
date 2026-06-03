#pragma once

#include <iosfwd>
#include <vector>

#include "ahfl/compiler/frontend/ast.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"

namespace ahfl {

struct SourceGraph;

/// 从 AST 生成 IR（单文件）
[[nodiscard]] ir::Program lower_program_ir(const ast::Program &program,
                                           const ResolveResult &resolve_result,
                                           const TypeCheckResult &type_check_result);

/// 从 SourceGraph 生成 IR（多文件）
[[nodiscard]] ir::Program lower_program_ir(const SourceGraph &graph,
                                           const ResolveResult &resolve_result,
                                           const TypeCheckResult &type_check_result);

/// 收集形式化观察
[[nodiscard]] std::vector<ir::FormalObservation>
collect_formal_observations(const ir::Program &program);

/// 打印 IR（可读格式）
void print_program_ir(const ir::Program &program, std::ostream &out);

/// 打印 IR（JSON 格式）
void print_program_ir_json(const ir::Program &program, std::ostream &out);

/// 发射 IR 到输出流（可读格式）
void emit_program_ir(const ast::Program &program,
                     const ResolveResult &resolve_result,
                     const TypeCheckResult &type_check_result,
                     std::ostream &out);

void emit_program_ir(const SourceGraph &graph,
                     const ResolveResult &resolve_result,
                     const TypeCheckResult &type_check_result,
                     std::ostream &out);

/// 发射 IR 到输出流（JSON 格式）
void emit_program_ir_json(const ast::Program &program,
                          const ResolveResult &resolve_result,
                          const TypeCheckResult &type_check_result,
                          std::ostream &out);

void emit_program_ir_json(const SourceGraph &graph,
                          const ResolveResult &resolve_result,
                          const TypeCheckResult &type_check_result,
                          std::ostream &out);

} // namespace ahfl
