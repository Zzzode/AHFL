#include "ahfl/compiler/semantics/type_relations.hpp"

namespace ahfl {

std::string_view to_string(SchemaBoundaryKind kind) noexcept {
    switch (kind) {
    case SchemaBoundaryKind::AgentInput:
        return "agent input";
    case SchemaBoundaryKind::AgentOutput:
        return "agent output";
    case SchemaBoundaryKind::AgentContextDefault:
        return "agent context default";
    case SchemaBoundaryKind::WorkflowInput:
        return "workflow input";
    case SchemaBoundaryKind::WorkflowOutput:
        return "workflow output";
    case SchemaBoundaryKind::WorkflowNodeInput:
        return "workflow node input";
    }

    return "schema boundary";
}

bool are_types_equivalent(const Type &lhs, const Type &rhs) {
    if (lhs.kind != rhs.kind) {
        return false;
    }

    switch (lhs.kind) {
    case TypeKind::Any:
    case TypeKind::Never:
    case TypeKind::Unit:
    case TypeKind::Bool:
    case TypeKind::Int:
    case TypeKind::Float:
    case TypeKind::String:
    case TypeKind::UUID:
    case TypeKind::Timestamp:
    case TypeKind::Duration:
        return true;
    case TypeKind::BoundedString:
        return lhs.string_bounds == rhs.string_bounds;
    case TypeKind::Decimal:
        return lhs.decimal_scale == rhs.decimal_scale;
    case TypeKind::Struct:
    case TypeKind::Enum:
        if (lhs.nominal_symbol.has_value() && rhs.nominal_symbol.has_value()) {
            return *lhs.nominal_symbol == *rhs.nominal_symbol;
        }
        return lhs.name == rhs.name;
    case TypeKind::Optional:
    case TypeKind::List:
    case TypeKind::Set:
        return lhs.first && rhs.first && are_types_equivalent(*lhs.first, *rhs.first);
    case TypeKind::Map:
        return lhs.first && rhs.first && lhs.second && rhs.second &&
               are_types_equivalent(*lhs.first, *rhs.first) &&
               are_types_equivalent(*lhs.second, *rhs.second);
    }

    return false;
}

bool is_subtype_of(const Type &source, const Type &target) {
    if (are_types_equivalent(source, target)) {
        return true;
    }

    if (source.kind == TypeKind::BoundedString && target.kind == TypeKind::String) {
        return true;
    }

    if (source.kind == TypeKind::BoundedString && target.kind == TypeKind::BoundedString) {
        return source.string_bounds && target.string_bounds &&
               source.string_bounds->first >= target.string_bounds->first &&
               source.string_bounds->second <= target.string_bounds->second;
    }

    return false;
}

bool is_assignable_to(const Type &source, const Type &target) {
    return is_subtype_of(source, target);
}

bool is_exact_schema_match(const Type &source, const Type &target) {
    return are_types_equivalent(source, target);
}

} // namespace ahfl
