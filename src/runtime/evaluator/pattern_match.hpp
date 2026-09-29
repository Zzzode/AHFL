#pragma once

#include "ahfl/compiler/ir/expr.hpp"
#include "runtime/value/value.hpp"

#include <string>
#include <unordered_map>

namespace ahfl::evaluator {

using ahfl::runtime::Value;

using PatternBindings = std::unordered_map<std::string, Value>;

[[nodiscard]] bool
match_pattern(const ir::MatchPattern &pattern, const Value &value, PatternBindings &bindings);

} // namespace ahfl::evaluator
