#include "runtime/evaluator/runtime_fn_table.hpp"

#include "ahfl/base/support/overloaded.hpp"

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
/// error/diagnostic bridge).
EvalResult invoke_body(const RuntimeFnEntry &entry,
                       std::vector<EvalResult> &&evaluated_args,
                       const EvalContext &parent_ctx) {
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
    const RuntimeFnEntry *entry = table.find_function(mangled_name);
    if (entry == nullptr) {
        return make_call_error("find_function('" + std::string(mangled_name) +
                               "'): no runtime entry registered");
    }
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
        arg_values.push_back(eval_expr(*arg_ref, ctx));
    }

    return invoke_body(*entry, std::move(arg_values), ctx);
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
