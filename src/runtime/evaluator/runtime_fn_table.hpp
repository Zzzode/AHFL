#pragma once

// ---------------------------------------------------------------------------
// Runtime function dispatch table for the evaluator.
//
// P2d.S5: the evaluator resolves an instantiated `fn` call by its mangled
// instance name. The table is built from an `ir::Program` that carries both
// nominal `FnDecl`s (with their lowered bodies) and the `InstanceDecl`s
// produced by typed-tree monomorphization lowering. The mangled name used on
// the call site (see typed_hir_lower's render_call_target) matches the name
// stored on the corresponding InstanceDecl, so dispatch is a single hash
// lookup -- no type information is re-consulted at runtime.
//
// Usage:
//   RuntimeFunctionTable table{program};
//   EvalResult r = call_mangled(table, "_inst_...", {a, b}, ctx);
// or register the table's call evaluator with an ExecContext to make every
// `eval_expression` resolve calls through it automatically.
// ---------------------------------------------------------------------------

#include "ahfl/compiler/ir/ir.hpp"
#include "runtime/evaluator/eval_context.hpp"
#include "runtime/evaluator/evaluator.hpp"
#include "runtime/evaluator/executor.hpp"

#include <cstddef>
#include <functional>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ahfl::evaluator {

/// Logging verbosity: the evaluator writes instance dispatches to the
/// supplied stream when this is enabled. The "-v 日志能打出实例名" acceptance
/// criterion is exercised by passing `std::cerr` or a stringstream-backed
/// sink.
struct RuntimeFnTrace {
    std::ostream *out{nullptr};
};

struct RuntimeFnEntry {
    /// Mangled instance name. For monomorphic (non-generic) calls this is the
    /// nominal fn's canonical name; for generic instantiations it is the
    /// mangle_instance() output.
    std::string mangled_name;
    /// Nominal FnDecl carrying the lowered body + params. The body is shared
    /// across instances; argument binding + the evaluator's structural value
    /// representation mean we don't need per-instance codegen.
    const ir::FnDecl *decl{nullptr};
    /// InstanceDecl provenance when this entry was produced by a generic
    /// instantiation; nullptr for monomorphic fallbacks.
    const ir::InstanceDecl *instance{nullptr};
};

class RuntimeFunctionTable {
  public:
    RuntimeFunctionTable() = default;
    /// Build the table from a lowered `ir::Program`. Generic instances are
    /// indexed by their InstanceDecl::name (the mangled key); every nominal
    /// FnDecl with a body is additionally indexed by its canonical name so
    /// non-generic call sites keep the canonical callee string.
    explicit RuntimeFunctionTable(const ir::Program &program);

    [[nodiscard]] const RuntimeFnEntry *find_function(std::string_view mangled) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    /// Produce a CallEvalFn that looks `callee` up through this table and
    /// executes the body with positional parameters bound to the evaluated
    /// arguments. Captures `this` by pointer -- the table must outlive the
    /// returned function.
    [[nodiscard]] CallEvalFn make_call_eval(const RuntimeFnTrace &trace = {}) const;

    /// Install the call evaluator on an existing ExecContext so that
    /// ExecContext::eval_expression dispatches calls through this table.
    void install(ExecContext &ctx, const RuntimeFnTrace &trace = {}) const;

  private:
    std::unordered_map<std::string, RuntimeFnEntry> entries_;
    std::vector<const ir::FnDecl *> owned_index_; // not owned, debugging aid
};

/// Direct helper: evaluate arguments, look up `mangled_name`, bind params,
/// execute the body, return the body's return value (or none + diagnostic if
/// the lookup/body failed). Used by the `CallEvalFn` adapter and exposed for
/// unit tests that want to bypass the full CallExpr plumbing.
[[nodiscard]] EvalResult call_mangled(const RuntimeFunctionTable &table,
                                      std::string_view mangled_name,
                                      const std::vector<ir::ExprRef> &arguments,
                                      const EvalContext &ctx,
                                      const RuntimeFnTrace &trace = {});

} // namespace ahfl::evaluator
