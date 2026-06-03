#pragma once

#include "ahfl/compiler/ir/ir.hpp"
#include "runtime/engine/capability_bridge.hpp"
#include "runtime/evaluator/eval_context.hpp"
#include "runtime/evaluator/evaluator.hpp"

namespace ahfl::runtime {

// 带 Capability 支持的表达式求值
[[nodiscard]] evaluator::EvalResult eval_expr_with_capabilities(
    const ir::Expr &expr, const evaluator::EvalContext &eval_ctx, CapabilityRegistry *registry);

[[nodiscard]] evaluator::EvalResult eval_expr_with_capabilities(
    const ir::Expr &expr, const evaluator::EvalContext &eval_ctx, const CapabilityInvoker &invoker);

} // namespace ahfl::runtime
