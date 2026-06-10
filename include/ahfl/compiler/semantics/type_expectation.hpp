#pragma once

#include "ahfl/compiler/semantics/types.hpp"
#include "ahfl/base/support/source.hpp"

#include <string>

namespace ahfl {

enum class TypeExpectationOriginKind {
    Annotation,
    StructField,
    FunctionParameter,
    ReturnType,
    AssignmentTarget,
    SchemaBoundary,
};

struct TypeExpectation {
    TypePtr expected{nullptr};
    TypeExpectationOriginKind origin_kind{TypeExpectationOriginKind::Annotation};
    SourceRange origin_range;
    std::string description;
};

[[nodiscard]] std::string describe_type_expectation_origin(TypeExpectationOriginKind kind);

} // namespace ahfl
