#include "ahfl/compiler/semantics/type_expectation.hpp"

namespace ahfl {

std::string describe_type_expectation_origin(TypeExpectationOriginKind kind) {
    switch (kind) {
    case TypeExpectationOriginKind::Annotation:
        return "annotation";
    case TypeExpectationOriginKind::StructField:
        return "field";
    case TypeExpectationOriginKind::FunctionParameter:
        return "parameter";
    case TypeExpectationOriginKind::ReturnType:
        return "return type";
    case TypeExpectationOriginKind::AssignmentTarget:
        return "assignment target";
    case TypeExpectationOriginKind::SchemaBoundary:
        return "schema boundary";
    }
    return "declaration";
}

std::string expected_type_note(const Type &target, const TypeExpectation &expectation) {
    std::string source = expectation.description.empty()
                             ? describe_type_expectation_origin(expectation.origin_kind)
                             : expectation.description;
    return "expected type '" + target.describe() + "' from " + source + " declared here";
}

std::string expected_schema_note(const Type &target, const TypeExpectation &expectation) {
    std::string source = expectation.description.empty()
                             ? describe_type_expectation_origin(expectation.origin_kind)
                             : expectation.description;
    return "expected schema '" + target.describe() + "' from " + source + " declared here";
}

std::string actual_type_note(const Type &source) {
    return "actual expression has type '" + source.describe() + "' here";
}

} // namespace ahfl
