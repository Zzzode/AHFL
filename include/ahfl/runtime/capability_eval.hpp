#pragma once

#include "ahfl/evaluator/eval_context.hpp"
#include "ahfl/evaluator/evaluator.hpp"
#include "ahfl/ir/ir.hpp"
#include "ahfl/runtime/capability_bridge.hpp"

namespace ahfl::runtime {

// 带 Capability 支持的表达式求值
[[nodiscard]] evaluator::EvalResult eval_expr_with_capabilities(
    const ir::Expr &expr, const evaluator::EvalContext &eval_ctx, CapabilityRegistry *registry);

} // namespace ahfl::runtime
