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

} // namespace ahfl
