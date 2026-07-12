#include "runtime/engine/capability_eval.hpp"

#include <string_view>
#include <variant>
#include <vector>

namespace ahfl::runtime {

namespace {

[[nodiscard]] std::string_view capability_call_status_name(CapabilityCallStatus status) {
    switch (status) {
    case CapabilityCallStatus::Success:
        return "success";
    case CapabilityCallStatus::Error:
        return "error";
    case CapabilityCallStatus::Timeout:
        return "timeout";
    case CapabilityCallStatus::RetryExhausted:
        return "retry_exhausted";
    case CapabilityCallStatus::CircuitOpen:
        return "circuit_open";
    }

    return "unknown";
}

[[nodiscard]] evaluator::EvalResult make_capability_error(std::string message,
                                                          std::string diagnostic_code = {}) {
    evaluator::EvalResult result;
    result.value = evaluator::make_none();
    if (diagnostic_code.empty()) {
        result.diagnostics.error()
            .message(std::move(message))
            .code(error_codes::backend::ExecutionError)
            .emit();
    } else {
        result.diagnostics.error()
            .message(std::move(message))
            .code(std::move(diagnostic_code))
            .emit();
    }
    return result;
}

[[nodiscard]] evaluator::EvalResult make_unknown_capability_error(std::string message) {
    evaluator::EvalResult result;
    result.value = evaluator::make_none();
    std::move(result.diagnostics)
        .error()
        .message(std::move(message))
        .code(error_codes::typecheck::UnknownCapability)
        .emit();
    return result;
}

[[nodiscard]] evaluator::EvalResult
capability_call_result_to_eval_result(const std::string &callee, CapabilityCallResult call_result) {
    if (call_result.status == CapabilityCallStatus::Success) {
        evaluator::EvalResult result{
            call_result.value.has_value() ? std::move(*call_result.value) : evaluator::make_none(),
            {},
        };
        if (call_result.usage.has_value()) {
            for (const auto &notice : call_result.usage->notices) {
                result.diagnostics.warning()
                    .message(notice.message)
                    .code(notice.diagnostic_code)
                    .emit();
            }
        }
        return result;
    }

    std::string message = "capability '" + callee + "' failed with status " +
                          std::string(capability_call_status_name(call_result.status));
    if (!call_result.error_message.empty()) {
        message += ": ";
        message += call_result.error_message;
    }
    if (call_result.attempts > 0) {
        message += " (attempts=" + std::to_string(call_result.attempts) + ")";
    }
    return make_capability_error(std::move(message), std::move(call_result.diagnostic_code));
}

template <typename InvokeCall>
[[nodiscard]] evaluator::EvalResult eval_expr_with_capability_call_handler(
    const ir::Expr &expr, const evaluator::EvalContext &eval_ctx, InvokeCall invoke_call) {
    evaluator::CallEvalFn call_eval;
    call_eval = [&call_eval,
                 invoke_call](const ir::CallExpr &call,
                              const evaluator::EvalContext &current_ctx) -> evaluator::EvalResult {
        // Step 1: evaluate arguments using the full call dispatcher so that
        // nested capability calls inside stdlib constructor arguments still go
        // through the runtime registry.
        std::vector<evaluator::Value> arg_values;
        for (const auto &arg_ptr : call.arguments) {
            if (!arg_ptr) {
                return make_capability_error("call '" + call.callee +
                                             "' has null argument expression");
            }
            auto arg_result = evaluator::eval_expr(*arg_ptr, current_ctx, call_eval);
            if (arg_result.has_errors()) {
                return arg_result;
            }
            arg_values.push_back(std::move(arg_result.value));
        }

        // Step 2: dispatch the call itself.
        //
        // Stdlib-namespaced callees (e.g. std::option::Option::Some,
        // std::collections::list_from_array) are serviced by the evaluator's
        // intrinsic path / builtin table, not by the runtime capability
        // registry.  We invoke the intrinsic helper with the
        // ALREADY-EVALUATED arg_values computed above so that nested
        // capability calls inside constructor arguments are resolved exactly
        // once via the full dispatcher and never re-evaluated through a
        // different (empty) call_eval.
        if (call.callee.starts_with("std::")) {
            return evaluator::eval_intrinsic_with_args(call.callee, std::move(arg_values),
                                                       current_ctx);
        }
        return invoke_call(call, arg_values);
    };

    return evaluator::eval_expr(expr, eval_ctx, call_eval);
}

} // namespace

evaluator::EvalResult eval_expr_with_capabilities(const ir::Expr &expr,
                                                  const evaluator::EvalContext &eval_ctx,
                                                  CapabilityRegistry *registry) {
    return eval_expr_with_capability_call_handler(
        expr,
        eval_ctx,
        [registry](const ir::CallExpr &call,
                   const std::vector<evaluator::Value> &arg_values) -> evaluator::EvalResult {
            if (registry == nullptr) {
                return make_unknown_capability_error("capability registry is null when invoking '" +
                                                     call.callee + "'");
            }
            if (!registry->has(call.callee)) {
                return make_unknown_capability_error("unknown capability '" + call.callee + "'");
            }
            return capability_call_result_to_eval_result(
                call.callee, registry->invoke(call.callee, arg_values));
        });
}

evaluator::EvalResult eval_expr_with_capabilities(const ir::Expr &expr,
                                                  const evaluator::EvalContext &eval_ctx,
                                                  const CapabilityInvoker &invoker) {
    return eval_expr_with_capability_call_handler(
        expr,
        eval_ctx,
        [&invoker](const ir::CallExpr &call,
                   const std::vector<evaluator::Value> &arg_values) -> evaluator::EvalResult {
            if (!invoker) {
                return make_unknown_capability_error("capability invoker is empty when invoking '" +
                                                     call.callee + "'");
            }
            return capability_call_result_to_eval_result(call.callee,
                                                         invoker(call.callee, arg_values));
        });
}

evaluator::EvalResult eval_expr_with_capabilities(const ir::Expr &expr,
                                                  const evaluator::EvalContext &eval_ctx,
                                                  const ContextualCapabilityInvoker &invoker,
                                                  const CapabilityInvocationContext &context) {
    return eval_expr_with_capability_call_handler(
        expr,
        eval_ctx,
        [&invoker,
         &context](const ir::CallExpr &call,
                   const std::vector<evaluator::Value> &arg_values) -> evaluator::EvalResult {
            if (!invoker) {
                return make_unknown_capability_error("capability invoker is empty when invoking '" +
                                                     call.callee + "'");
            }
            auto call_context = context;
            call_context.source_capability_symbol_id = call.callee_ref.id;
            return capability_call_result_to_eval_result(
                call.callee, invoker(call_context, call.callee, arg_values));
        });
}

} // namespace ahfl::runtime
