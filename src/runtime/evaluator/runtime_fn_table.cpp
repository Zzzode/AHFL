#include "runtime/evaluator/runtime_fn_table.hpp"

#include "ahfl/base/support/overloaded.hpp"
#include "runtime/evaluator/builtins.hpp"
#include "runtime/evaluator/evaluator.hpp"

#include <cassert>
#include <iostream>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace ahfl::evaluator {

namespace {

EvalResult make_call_error(std::string message) {
    EvalResult r;
    r.value = make_none();
    std::move(r.diagnostics.error()).message(std::move(message)).emit();
    return r;
}

const ir::FnDecl *find_fn_decl_by_symbol_ref(const ir::Program &program,
                                             const ir::SymbolRef &ref) {
    if (!ref.id.has_value()) return nullptr;
    const std::size_t want = *ref.id;
    for (const auto &decl : program.declarations) {
        const auto *fn = std::get_if<ir::FnDecl>(&decl);
        if (fn == nullptr) continue;
        if (fn->symbol_ref.id.has_value() && *fn->symbol_ref.id == want) {
            return fn;
        }
    }
    // Fallback: match on canonical name for symbols whose numeric id was not
    // propagated (e.g. early test programs with a stub provenance pipeline).
    if (!ref.canonical_name.empty()) {
        for (const auto &decl : program.declarations) {
            const auto *fn = std::get_if<ir::FnDecl>(&decl);
            if (fn == nullptr) continue;
            if (fn->symbol_ref.canonical_name == ref.canonical_name ||
                fn->name == ref.canonical_name) {
                return fn;
            }
        }
    }
    return nullptr;
}

} // namespace

RuntimeFunctionTable::RuntimeFunctionTable(const ir::Program &program) {
    // (1) Index nominal FnDecl bodies by canonical name so monomorphic calls
    // keep the simple "canonical_name" callee string produced by lowering.
    for (const auto &decl : program.declarations) {
        const auto *fn = std::get_if<ir::FnDecl>(&decl);
        if (fn == nullptr) continue;
        if (!fn->has_body || fn->body == nullptr) continue;

        RuntimeFnEntry entry;
        entry.decl = fn;
        entry.instance = nullptr;
        // Prefer the symbol's canonical name (namespaced). Fall back to the
        // raw name field (which is already canonical after typed_hir_lower).
        entry.mangled_name =
            fn->symbol_ref.canonical_name.empty() ? fn->name : fn->symbol_ref.canonical_name;
        if (entry.mangled_name.empty()) continue;
        entries_.emplace(entry.mangled_name, std::move(entry));
        owned_index_.push_back(fn);
    }

    // (2) Index generic instantiations by their mangled instance name. Each
    // InstanceDecl of kind Fn points at its originating symbol; we pair it
    // with the nominal FnDecl that carries the lowered body.
    for (const auto &decl : program.declarations) {
        const auto *instance = std::get_if<ir::InstanceDecl>(&decl);
        if (instance == nullptr) continue;
        if (instance->kind != ir::InstanceKind::Fn) continue;
        if (instance->name.empty()) continue;

        const ir::FnDecl *fn = find_fn_decl_by_symbol_ref(program, instance->symbol_ref);
        if (fn == nullptr) continue;

        RuntimeFnEntry entry;
        entry.mangled_name = instance->name;
        entry.decl = fn;
        entry.instance = instance;
        entries_.emplace(entry.mangled_name, std::move(entry));
    }
}

const RuntimeFnEntry *RuntimeFunctionTable::find_function(std::string_view mangled) const noexcept {
    const auto it = entries_.find(std::string(mangled));
    return it == entries_.end() ? nullptr : &it->second;
}

namespace {

/// Evaluate the function body with `params[i] = args[i]` bound in a child
/// context and return the ExecReturn value (or the appropriate EvalResult
/// error/diagnostic bridge). The optional `call_eval` closure is installed on
/// the nested ExecContext so nested fn-call / builtin-call expressions reuse
/// the same RuntimeFunctionTable + BuiltinTable dispatch pipeline instead of
/// falling back to the body-less main evaluator path.
EvalResult invoke_body(const RuntimeFnEntry &entry,
                       std::vector<EvalResult> &&evaluated_args,
                       const EvalContext &parent_ctx,
                       const CallEvalFn *call_eval = nullptr) {
    if (entry.decl == nullptr) return make_call_error("null function declaration");
    if (!entry.decl->has_body || entry.decl->body == nullptr) {
        return make_call_error("function '" + entry.mangled_name + "' has no body");
    }
    if (evaluated_args.size() != entry.decl->params.size()) {
        std::ostringstream oss;
        oss << "argument count mismatch calling '" << entry.mangled_name << "': expected "
            << entry.decl->params.size() << " got " << evaluated_args.size();
        return make_call_error(oss.str());
    }

    // Aggregate diagnostics from the evaluated arguments. Stop if any failed.
    DiagnosticBag arg_diags;
    for (auto &r : evaluated_args) {
        if (r.has_errors()) {
            EvalResult fail;
            fail.value = make_none();
            fail.diagnostics = std::move(r.diagnostics);
            return fail;
        }
        arg_diags.append(r.diagnostics);
    }

    ExecContext exec_ctx;
    exec_ctx.eval_ctx = parent_ctx; // inherit input/ctx/node_output scopes
    if (call_eval != nullptr) {
        exec_ctx.expr_eval = [call_eval](const ir::Expr &expr,
                                         const EvalContext &ec) -> EvalResult {
            return eval_expr(expr, ec, *call_eval);
        };
    }
    for (std::size_t i = 0; i < entry.decl->params.size(); ++i) {
        exec_ctx.bind_local(entry.decl->params[i].name, std::move(evaluated_args[i].value));
    }

    const ExecResult body_result = exec_block(*entry.decl->body, exec_ctx);

    EvalResult result;
    result.value = make_none();
    result.diagnostics = std::move(arg_diags);
    result.diagnostics.append(body_result.diagnostics);
    if (body_result.has_errors()) {
        return result;
    }

    return std::visit(
        Overloaded{
            [&](const ExecContinue &) -> EvalResult { return std::move(result); },
            [&](const ExecReturn &r) -> EvalResult {
                result.value = clone_value(r.value);
                return std::move(result);
            },
            [&](const ExecGoto &g) -> EvalResult {
                return make_call_error("unexpected goto '" + g.target_state +
                                       "' while evaluating fn '" + entry.mangled_name + "'");
            },
            [&](const ExecAssertFailed &a) -> EvalResult {
                return make_call_error("assertion failed inside fn '" + entry.mangled_name + "': " +
                                       a.message);
            },
        },
        body_result.outcome);
}

} // namespace

EvalResult call_mangled(const RuntimeFunctionTable &table,
                        std::string_view mangled_name,
                        const std::vector<ir::ExprRef> &arguments,
                        const EvalContext &ctx,
                        const RuntimeFnTrace &trace) {
    auto self_call_eval = table.make_call_eval(trace);
    const CallEvalFn *call_eval_ptr = &self_call_eval;
    const RuntimeFnEntry *entry = table.find_function(mangled_name);
    if (entry != nullptr) {
        if (trace.out != nullptr) {
            *trace.out << "[evaluator] dispatch fn instance: " << entry->mangled_name << '\n';
        }

        std::vector<EvalResult> arg_values;
        arg_values.reserve(arguments.size());
        for (const auto &arg_ref : arguments) {
            if (!arg_ref) {
                arg_values.push_back(make_call_error("null argument expression in call to '" +
                                                     std::string(mangled_name) + "'"));
                continue;
            }
            arg_values.push_back(eval_expr(*arg_ref, ctx, *call_eval_ptr));
        }

        return invoke_body(*entry, std::move(arg_values), ctx, call_eval_ptr);
    }

    // P6a fallback: when a @builtin function's callee name does not appear in
    // the runtime fn-instance table (either because lowering forwarded the
    // canonical qualified name instead of the builtin hook, or because the
    // prototype has no body to register), dispatch directly through the C++
    // builtin table. This mirrors the eval_intrinsic_call() path in the main
    // evaluator so both call resolutions stay in sync.
    //
    // Two-tier lookup: try the exact callee first, then try stripping a
    // std-prefixed module qualifier (e.g. "std::fmt_smoke::int_to_string" →
    // "int_to_string") so ad-hoc test modules whose lowering emits qualified
    // names still resolve to the correct C++ hook.
    const BuiltinTable &builtins = BuiltinTable::instance();
    auto try_builtin = [&](std::string_view name) -> const BuiltinFn * {
        return builtins.find(name);
    };
    const BuiltinFn *builtin_fn = try_builtin(mangled_name);
    if (builtin_fn == nullptr) {
        const auto sep = mangled_name.rfind("::");
        if (sep != std::string_view::npos && sep + 2 < mangled_name.size()) {
            std::string_view unqualified = mangled_name.substr(sep + 2);
            builtin_fn = try_builtin(unqualified);
        }
    }
    if (builtin_fn != nullptr) {
        if (trace.out != nullptr) {
            *trace.out << "[evaluator] dispatch builtin hook: " << mangled_name << '\n';
        }
        std::vector<Value> args;
        args.reserve(arguments.size());
        DiagnosticBag diags;
        for (const auto &arg_ref : arguments) {
            if (!arg_ref) {
                return make_call_error("builtin call: null argument expression");
            }
            EvalResult r = eval_expr(*arg_ref, ctx, *call_eval_ptr);
            if (r.has_errors()) {
                diags.append(std::move(r.diagnostics));
                return EvalResult{make_none(), std::move(diags)};
            }
            diags.append(r.diagnostics);
            args.push_back(std::move(r.value));
        }
        EvalResult result = (*builtin_fn)(args, ctx);
        result.diagnostics.append(std::move(diags));
        return result;
    }

    return make_call_error("find_function('" + std::string(mangled_name) +
                           "'): no runtime entry registered");
}

CallEvalFn RuntimeFunctionTable::make_call_eval(const RuntimeFnTrace &trace) const {
    const RuntimeFunctionTable *self = this;
    return [self, trace](const ir::CallExpr &call, const EvalContext &ctx) -> EvalResult {
        return call_mangled(*self, call.callee, call.arguments, ctx, trace);
    };
}

void RuntimeFunctionTable::install(ExecContext &ctx, const RuntimeFnTrace &trace) const {
    const RuntimeFunctionTable *self = this;
    ctx.expr_eval = [self, trace](const ir::Expr &expr, const EvalContext &eval_ctx) -> EvalResult {
        return eval_expr(expr, eval_ctx, self->make_call_eval(trace));
    };
}

} // namespace ahfl::evaluator
