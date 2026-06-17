#pragma once

#include "ahfl/base/support/source.hpp"
#include "ahfl/compiler/semantics/types.hpp"

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
[[nodiscard]] std::string expected_type_note(const Type &target,
                                             const TypeExpectation &expectation);
[[nodiscard]] std::string expected_schema_note(const Type &target,
                                               const TypeExpectation &expectation);
[[nodiscard]] std::string actual_type_note(const Type &source);

} // namespace ahfl
