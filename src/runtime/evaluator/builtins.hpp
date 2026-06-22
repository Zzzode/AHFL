#pragma once

#include "runtime/evaluator/eval_context.hpp"
#include "runtime/evaluator/evaluator.hpp"
#include "runtime/evaluator/value.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ahfl::evaluator {

// ============================================================================
// P5 @builtin runtime dispatch
// ============================================================================
//
// The builtin table maps @builtin hook names (e.g. "list_raw_length") to C++
// implementations.  Stdlib functions declared with @builtin("name") are wired
// by the evaluator through this table — when a CallExpr resolves to a fn with
// a non-empty builtin_name, the evaluator dispatches to the corresponding
// BuiltinFn instead of interpreting a fn body.
//
// Builtin implementations receive their already-evaluated arguments and the
// evaluation context, and return an EvalResult.  Argument count / type
// validation is the caller's responsibility (the typechecker guarantees the
// stdlib declaration matches its signature).

using BuiltinFn =
    std::function<EvalResult(const std::vector<Value> &args, const EvalContext &ctx)>;

/// Immutable lookup table of all registered builtins.
class BuiltinTable {
  public:
    /// Returns the global builtin table (populated with all known builtins).
    [[nodiscard]] static const BuiltinTable &instance();

    /// Look up a builtin by name.  Returns nullptr if not found.
    [[nodiscard]] const BuiltinFn *find(std::string_view name) const;

    /// Register a builtin.  Used internally by populate(); exposed for tests.
    void insert(std::string name, BuiltinFn fn);

  private:
    BuiltinTable() = default;
    void populate();

    std::unordered_map<std::string, BuiltinFn> table_;
};

/// Helper: create a CallEvalFn that dispatches through the global builtin
/// table.  Suitable for passing to eval_expr() when stdlib @builtin calls
/// should be evaluated.
///
/// The returned closure captures the table by reference; callers must ensure
/// the table outlives the closure.
[[nodiscard]] CallEvalFn make_builtin_call_eval(const BuiltinTable &table);

} // namespace ahfl::evaluator
