#pragma once

#include "ahfl/compiler/frontend/ast.hpp"
#include "ahfl/compiler/semantics/flow_facts.hpp"

namespace ahfl {

struct ConditionFacts {
    FlowFacts when_true;
    FlowFacts when_false;
};

[[nodiscard]] ConditionFacts extract_condition_facts(const ast::ExprSyntax &condition);

} // namespace ahfl
