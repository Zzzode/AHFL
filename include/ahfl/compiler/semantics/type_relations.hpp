#pragma once

#include "ahfl/compiler/semantics/types.hpp"

#include <string_view>

namespace ahfl {

enum class SchemaBoundaryKind {
    AgentInput,
    AgentOutput,
    AgentContextDefault,
    WorkflowInput,
    WorkflowOutput,
    WorkflowNodeInput,
};

[[nodiscard]] std::string_view to_string(SchemaBoundaryKind kind) noexcept;

[[nodiscard]] bool are_types_equivalent(const Type &lhs, const Type &rhs);
[[nodiscard]] bool is_subtype_of(const Type &source, const Type &target);
[[nodiscard]] bool is_assignable_to(const Type &source, const Type &target);
[[nodiscard]] bool is_exact_schema_match(const Type &source, const Type &target);

} // namespace ahfl
