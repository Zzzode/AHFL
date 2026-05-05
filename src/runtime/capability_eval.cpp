#include "ahfl/runtime/capability_eval.hpp"

#include <variant>
#include <vector>

namespace ahfl::runtime {

evaluator::EvalResult eval_expr_with_capabilities(const ir::Expr &expr,
                                                   const evaluator::EvalContext &eval_ctx,
                                                   CapabilityRegistry *registry) {
    // 如果不是 CallExpr，委托给标准求值器
    if (!std::holds_alternative<ir::CallExpr>(expr.node)) {
        return evaluator::eval_expr(expr, eval_ctx);
    }

    // 提取 CallExpr
    const auto &call = std::get<ir::CallExpr>(expr.node);

    // 递归求值每个参数
    std::vector<evaluator::Value> arg_values;

    for (const auto &arg_ptr : call.arguments) {
        auto arg_result = eval_expr_with_capabilities(*arg_ptr, eval_ctx, registry);
        if (arg_result.has_errors()) {
            return arg_result;
        }
        arg_values.push_back(std::move(arg_result.value));
    }

    // 检查 registry 是否为空
    if (registry == nullptr) {
        evaluator::EvalResult result;
        result.value = evaluator::make_none();
        std::move(result.diagnostics)
            .error()
            .message("capability registry is null when invoking '" + call.callee + "'")
            .code(error_codes::typecheck::UnknownCapability)
            .emit();
        return result;
    }

    // 检查 capability 是否注册
    if (!registry->has(call.callee)) {
        evaluator::EvalResult result;
        result.value = evaluator::make_none();
        std::move(result.diagnostics)
            .error()
            .message("unknown capability '" + call.callee + "'")
            .code(error_codes::typecheck::UnknownCapability)
            .emit();
        return result;
    }

    // 调用 capability
    auto call_result = registry->invoke(call.callee, arg_values);

    if (call_result.status == CapabilityCallStatus::Success) {
        evaluator::EvalResult result;
        if (call_result.value.has_value()) {
            result.value = std::move(*call_result.value);
        } else {
            result.value = evaluator::make_none();
        }
        return result;
    }

    // 错误情况
    evaluator::EvalResult result;
    result.value = evaluator::make_none();
    std::string msg = "capability '" + call.callee + "' failed: " + call_result.error_message;
    std::move(result.diagnostics)
        .error()
        .message(std::move(msg))
        .code(error_codes::backend::ExecutionError)
        .emit();
    return result;
}

} // namespace ahfl::runtime
