#pragma once

#include <concepts>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ahfl/base/support/ownership.hpp"
#include "ahfl/base/support/source.hpp"

namespace ahfl {

// ============================================================================
// Diagnostic Severity
// ============================================================================

enum class DiagnosticSeverity {
    Note,
    Warning,
    Error,
};

[[nodiscard]] inline std::string_view to_string(DiagnosticSeverity severity) noexcept {
    switch (severity) {
    case DiagnosticSeverity::Note:
        return "note";
    case DiagnosticSeverity::Warning:
        return "warning";
    case DiagnosticSeverity::Error:
        return "error";
    }

    return "unknown";
}

// ============================================================================
// Diagnostic Category
// ============================================================================

enum class DiagnosticCategory {
    Parse,
    Resolve,
    TypeCheck,
    Validation,
    Runtime,
    Backend,
    IO,
    Internal,
};

[[nodiscard]] inline std::string_view to_string(DiagnosticCategory category) noexcept {
    switch (category) {
    case DiagnosticCategory::Parse:
        return "parse";
    case DiagnosticCategory::Resolve:
        return "resolve";
    case DiagnosticCategory::TypeCheck:
        return "typecheck";
    case DiagnosticCategory::Validation:
        return "validation";
    case DiagnosticCategory::Runtime:
        return "runtime";
    case DiagnosticCategory::Backend:
        return "backend";
    case DiagnosticCategory::IO:
        return "io";
    case DiagnosticCategory::Internal:
        return "internal";
    }

    return "unknown";
}

// ============================================================================
// Legacy Diagnostic Code (for backward compatibility)
// ============================================================================

struct DiagnosticCode {
    DiagnosticCategory category{DiagnosticCategory::Validation};
    std::string value;
};

// ============================================================================
// Structured Error Code (new API)
// ============================================================================

template <DiagnosticCategory Cat> struct ErrorCode {
    std::string_view id;

    static constexpr DiagnosticCategory category = Cat;

    constexpr explicit ErrorCode(std::string_view code_id) noexcept : id(code_id) {}

    [[nodiscard]] std::string full_code() const {
        return std::string(to_string(category)) + "." + std::string(id);
    }
};

// ============================================================================
// Message Template (for structured messages and future i18n)
// ============================================================================

struct MessageTemplate {
    std::string_view format;

    constexpr explicit MessageTemplate(std::string_view fmt) noexcept : format(fmt) {}

    [[nodiscard]] std::string format_with() const {
        return std::string(format);
    }

    template <typename... Args>
        requires(sizeof...(Args) > 0 && (std::convertible_to<Args, std::string_view> && ...))
    [[nodiscard]] std::string format_with(Args &&...args) const {
        std::string result(format);
        constexpr std::string_view placeholder = "{}";
        std::size_t pos = 0;
        ((pos = result.find(placeholder, pos),
          pos != std::string::npos
              ? (result.replace(
                     pos, placeholder.length(), std::string_view(std::forward<Args>(args))),
                 pos += std::string_view(std::forward<Args>(args)).length())
              : pos),
         ...);
        return result;
    }
};

// ============================================================================
// Error Code Definitions
// ============================================================================

namespace error_codes {
namespace parse {
inline constexpr ErrorCode<DiagnosticCategory::Parse> UnexpectedToken{"UNEXPECTED_TOKEN"};
inline constexpr ErrorCode<DiagnosticCategory::Parse> InvalidSyntax{"INVALID_SYNTAX"};
inline constexpr ErrorCode<DiagnosticCategory::Parse> UnterminatedString{"UNTERMINATED_STRING"};
} // namespace parse

namespace resolve {
inline constexpr ErrorCode<DiagnosticCategory::Resolve> DuplicateSymbol{"DUPLICATE_SYMBOL"};
inline constexpr ErrorCode<DiagnosticCategory::Resolve> UnknownSymbol{"UNKNOWN_SYMBOL"};
inline constexpr ErrorCode<DiagnosticCategory::Resolve> CyclicTypeAlias{"CYCLIC_TYPE_ALIAS"};
inline constexpr ErrorCode<DiagnosticCategory::Resolve> AmbiguousReference{"AMBIGUOUS_REFERENCE"};
inline constexpr ErrorCode<DiagnosticCategory::Resolve> UnknownCallable{"UNKNOWN_CALLABLE"};
inline constexpr ErrorCode<DiagnosticCategory::Resolve> AmbiguousCallable{"AMBIGUOUS_CALLABLE"};
inline constexpr ErrorCode<DiagnosticCategory::Resolve> DuplicateImport{"DUPLICATE_IMPORT"};
inline constexpr ErrorCode<DiagnosticCategory::Resolve> ModuleBoundaryMismatch{
    "MODULE_BOUNDARY_MISMATCH"};
inline constexpr ErrorCode<DiagnosticCategory::Resolve> MultipleModuleDeclarations{
    "MULTIPLE_MODULE_DECLARATIONS"};
} // namespace resolve

namespace typecheck {
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> TypeMismatch{"TYPE_MISMATCH"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> SemanticError{"SEMANTIC_ERROR"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> ExactSchemaMismatch{
    "EXACT_SCHEMA_MISMATCH"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> UnknownType{"UNKNOWN_TYPE"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> UnknownValue{"UNKNOWN_VALUE"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> UnknownQualifiedValue{
    "UNKNOWN_QUALIFIED_VALUE"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> UnknownCallable{"UNKNOWN_CALLABLE"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> InvalidTypeReference{
    "INVALID_TYPE_REFERENCE"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> InvalidCallableReference{
    "INVALID_CALLABLE_REFERENCE"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> MissingCallableMetadata{
    "MISSING_CALLABLE_METADATA"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> MissingConstMetadata{
    "MISSING_CONST_METADATA"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> MissingTypeMetadata{
    "MISSING_TYPE_METADATA"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> InvalidStructLiteralTarget{
    "INVALID_STRUCT_LITERAL_TARGET"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> InvalidQualifiedValue{
    "INVALID_QUALIFIED_VALUE"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> UnknownEnumVariant{
    "UNKNOWN_ENUM_VARIANT"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> InvalidOperation{"INVALID_OPERATION"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> NonPureExpression{"NON_PURE_EXPRESSION"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> ConstExprRequired{"CONST_EXPR_REQUIRED"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> ConstDependencyCycle{
    "CONST_DEPENDENCY_CYCLE"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> InvalidMemberAccess{
    "INVALID_MEMBER_ACCESS"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> UnknownField{"UNKNOWN_FIELD"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> MissingField{"MISSING_FIELD"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> DuplicateField{"DUPLICATE_FIELD"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> DuplicateVariant{"DUPLICATE_VARIANT"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> InvalidIndexAccess{
    "INVALID_INDEX_ACCESS"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> NoneWithoutContext{
    "NONE_WITHOUT_CONTEXT"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> EmptyLiteralWithoutContext{
    "EMPTY_LITERAL_WITHOUT_CONTEXT"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> InvalidAgentType{"INVALID_AGENT_TYPE"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> UnknownCapability{"UNKNOWN_CAPABILITY"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> CapabilityNotAllowed{
    "CAPABILITY_NOT_ALLOWED"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> WrongArity{"WRONG_ARITY"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> ShadowedBinding{"SHADOWED_BINDING"};
} // namespace typecheck

namespace validation {
inline constexpr ErrorCode<DiagnosticCategory::Validation> SemanticInvariant{"SEMANTIC_INVARIANT"};
inline constexpr ErrorCode<DiagnosticCategory::Validation> VersionMismatch{"VERSION_MISMATCH"};
inline constexpr ErrorCode<DiagnosticCategory::Validation> RequiredFieldEmpty{
    "REQUIRED_FIELD_EMPTY"};
inline constexpr ErrorCode<DiagnosticCategory::Validation> OptionalFieldEmpty{
    "OPTIONAL_FIELD_EMPTY"};
inline constexpr ErrorCode<DiagnosticCategory::Validation> InvalidState{"INVALID_STATE"};
inline constexpr ErrorCode<DiagnosticCategory::Validation> DuplicateCapability{
    "DUPLICATE_CAPABILITY"};
inline constexpr ErrorCode<DiagnosticCategory::Validation> InvalidTemporalFormula{
    "INVALID_TEMPORAL_FORMULA"};
inline constexpr ErrorCode<DiagnosticCategory::Validation> InvalidWorkflowGraph{
    "INVALID_WORKFLOW_GRAPH"};
inline constexpr ErrorCode<DiagnosticCategory::Validation> FailureSummaryEmptyMessage{
    "FAILURE_SUMMARY_EMPTY_MESSAGE"};
inline constexpr ErrorCode<DiagnosticCategory::Validation> FailureSummaryEmptyNodeName{
    "FAILURE_SUMMARY_EMPTY_NODE_NAME"};
} // namespace validation

namespace runtime {
inline constexpr ErrorCode<DiagnosticCategory::Runtime> LLMPromptBudgetRejected{
    "LLM_PROMPT_BUDGET_REJECTED"};
inline constexpr ErrorCode<DiagnosticCategory::Runtime> LLMTokenBudgetExceeded{
    "LLM_TOKEN_BUDGET_EXCEEDED"};
inline constexpr ErrorCode<DiagnosticCategory::Runtime> LLMCostBudgetExceeded{
    "LLM_COST_BUDGET_EXCEEDED"};
} // namespace runtime

namespace backend {
inline constexpr ErrorCode<DiagnosticCategory::Backend> BootstrapError{"BOOTSTRAP_ERROR"};
inline constexpr ErrorCode<DiagnosticCategory::Backend> ExecutionError{"EXECUTION_ERROR"};
inline constexpr ErrorCode<DiagnosticCategory::Backend> UnknownTarget{"UNKNOWN_TARGET"};
inline constexpr ErrorCode<DiagnosticCategory::Backend> DuplicateWorkflow{"DUPLICATE_WORKFLOW"};
inline constexpr ErrorCode<DiagnosticCategory::Backend> NoWorkflows{"NO_WORKFLOWS"};
inline constexpr ErrorCode<DiagnosticCategory::Backend> UnknownWorkflow{"UNKNOWN_WORKFLOW"};
inline constexpr ErrorCode<DiagnosticCategory::Backend> MissingBootstrap{"MISSING_BOOTSTRAP"};
inline constexpr ErrorCode<DiagnosticCategory::Backend> InvalidDependency{"INVALID_DEPENDENCY"};
} // namespace backend
} // namespace error_codes

// ============================================================================
// Message Template Definitions
// ============================================================================

namespace messages {
namespace resolve {
inline constexpr MessageTemplate DuplicateSymbol{"duplicate {} '{}'"};
inline constexpr MessageTemplate UnknownSymbol{"unknown {} '{}'"};
inline constexpr MessageTemplate AmbiguousCallable{
    "ambiguous callable '{}' matches both a capability and a predicate"};
inline constexpr MessageTemplate CyclicTypeAlias{"type alias cycle detected: {}"};
inline constexpr MessageTemplate DuplicateImport{"duplicate import alias '{}'"};
inline constexpr MessageTemplate ModuleBoundaryMismatch{
    "source unit module boundary does not match graph owner"};
inline constexpr MessageTemplate MultipleModuleDeclarations{
    "multiple module declarations are not supported in one source file"};
} // namespace resolve

namespace typecheck {
inline constexpr MessageTemplate TypeMismatch{"type mismatch in {}: expected {}, got {}"};
inline constexpr MessageTemplate BoolExpressionRequired{"{} must have type Bool"};
inline constexpr MessageTemplate ExactSchemaMismatch{
    "exact schema mismatch in {}: expected {}, got {}"};
inline constexpr MessageTemplate InvalidMemberAccess{
    "member access requires a struct value, got {}"};
inline constexpr MessageTemplate UnknownType{"unknown type '{}'"};
inline constexpr MessageTemplate UnknownValue{"unknown value '{}'"};
inline constexpr MessageTemplate UnknownQualifiedValue{"unknown qualified value '{}'"};
inline constexpr MessageTemplate UnknownCallable{"unknown callable '{}'"};
inline constexpr MessageTemplate ResolvedTypeSymbolMissing{"resolved type symbol is missing"};
inline constexpr MessageTemplate SymbolDoesNotNameType{"symbol '{}' does not name a type"};
inline constexpr MessageTemplate TypeAliasCycleDuringResolution{
    "type alias cycle reached during type resolution"};
inline constexpr MessageTemplate TypeAliasDeclarationMissing{"type alias declaration is missing"};
inline constexpr MessageTemplate CallTargetSymbolMissing{"call target symbol is missing"};
inline constexpr MessageTemplate SymbolDoesNotNameCallable{"symbol '{}' does not name a callable"};
inline constexpr MessageTemplate CapabilityTypeInfoMissing{
    "capability type info is missing for '{}'"};
inline constexpr MessageTemplate PredicateTypeInfoMissing{
    "predicate type info is missing for '{}'"};
inline constexpr MessageTemplate ConstTypeInfoMissing{"constant type info is missing for '{}'"};
inline constexpr MessageTemplate StructTypeInfoMissing{"struct type info is missing for '{}'"};
inline constexpr MessageTemplate EnumTypeInfoMissing{"enum type info is missing for '{}'"};
inline constexpr MessageTemplate StructLiteralTargetRequiresStruct{
    "struct literal target '{}' does not resolve to a struct type"};
inline constexpr MessageTemplate QualifiedValueRequiresConstOrEnumVariant{
    "qualified value '{}' must refer to a constant or enum variant"};
inline constexpr MessageTemplate UnknownEnumVariant{"unknown enum variant '{}'"};
inline constexpr MessageTemplate UnknownField{"unknown field '{}' on struct '{}'"};
inline constexpr MessageTemplate MissingField{"missing field '{}' in struct literal"};
inline constexpr MessageTemplate DuplicateField{"duplicate field '{}' in struct literal"};
inline constexpr MessageTemplate DuplicateStructField{"duplicate struct field '{}'"};
inline constexpr MessageTemplate DuplicateEnumVariant{"duplicate enum variant '{}'"};
inline constexpr MessageTemplate MissingAgentContextDefault{
    "agent context field '{}' must declare a default value"};
inline constexpr MessageTemplate InvalidOperator{"operator '{}' is not defined for {} and {}"};
inline constexpr MessageTemplate LogicalNotRequiresBool{"logical not requires Bool, got {}"};
inline constexpr MessageTemplate NumericUnaryRequiresNumeric{
    "numeric unary operator requires Int, Float, or Decimal, got {}"};
inline constexpr MessageTemplate NoneComparisonRequiresOptional{
    "comparison with none requires Optional<T>, got {}"};
inline constexpr MessageTemplate LogicalOperatorRequiresBool{
    "logical operator requires Bool operands"};
inline constexpr MessageTemplate ComparisonOperandsIncompatible{
    "comparison operands are not type-compatible: {} vs {}"};
inline constexpr MessageTemplate ArithmeticOperatorInvalid{
    "arithmetic operator is not defined for {} and {}"};
inline constexpr MessageTemplate ModuloRequiresInt{"operator '%' requires Int operands"};
inline constexpr MessageTemplate NoneWithoutContext{
    "cannot infer type of 'none' without an expected Optional<T> context"};
inline constexpr MessageTemplate EmptyListWithoutContext{"cannot infer type of empty list literal"};
inline constexpr MessageTemplate EmptySetWithoutContext{"cannot infer type of empty set literal"};
inline constexpr MessageTemplate EmptyMapWithoutContext{"cannot infer type of empty map literal"};
inline constexpr MessageTemplate ListIndexRequiresInt{"list index must have type Int"};
inline constexpr MessageTemplate IndexTargetRequiresCollection{
    "index access requires a List or Map value, got {}"};
inline constexpr MessageTemplate AssignmentTargetRequiresContext{
    "assignment target must be rooted at writable 'ctx'"};
inline constexpr MessageTemplate SchemaBoundaryTypeRequiresStruct{
    "{} type must resolve to a struct type"};
inline constexpr MessageTemplate ShadowedBinding{
    "let binding '{}' shadows an existing binding of type '{}'"};
inline constexpr MessageTemplate UnknownCapabilityInAgent{
    "unknown capability '{}' in agent capability list"};
inline constexpr MessageTemplate CapabilityNotAllowed{
    "capability call '{}' is not allowed in this context"};
inline constexpr MessageTemplate CapabilityNotDeclared{
    "capability '{}' is not declared in the current agent capabilities"};
inline constexpr MessageTemplate WrongArity{"{} '{}' expects {} argument(s), got {}"};
inline constexpr MessageTemplate PredicateArgsNotPure{
    "predicate arguments must be pure expressions"};
inline constexpr MessageTemplate NonPureContext{"{} must be a pure expression"};
inline constexpr MessageTemplate ConstExprRequired{"const expression required in {}: {}"};
inline constexpr MessageTemplate ConstDependencyCycle{
    "const dependency cycle detected involving '{}'"};
inline constexpr MessageTemplate ConstDependencyCycleParticipant{
    "const declaration participates in the dependency cycle"};
} // namespace typecheck

namespace validation {
inline constexpr MessageTemplate VersionMismatch{"{} must be '{}'"};
inline constexpr MessageTemplate RequiredFieldEmpty{"{} must not be empty"};
inline constexpr MessageTemplate OptionalFieldEmpty{"{} {}"};
inline constexpr MessageTemplate DuplicateAgentState{"duplicate agent state '{}'"};
inline constexpr MessageTemplate InitialStateNotDeclared{
    "initial state '{}' is not declared in agent states"};
inline constexpr MessageTemplate DuplicateFinalState{"duplicate final state '{}'"};
inline constexpr MessageTemplate FinalStateNotDeclared{
    "final state '{}' is not declared in agent states"};
inline constexpr MessageTemplate DuplicateCapability{
    "duplicate capability '{}' in agent capability list"};
inline constexpr MessageTemplate TransitionSourceNotDeclared{
    "transition source state '{}' is not declared in agent states"};
inline constexpr MessageTemplate TransitionTargetNotDeclared{
    "transition target state '{}' is not declared in agent states"};
inline constexpr MessageTemplate FinalStateOutgoingTransition{
    "final state '{}' must not have outgoing transitions"};
inline constexpr MessageTemplate UnreachableAgentState{
    "state '{}' is unreachable from initial state '{}'"};
inline constexpr MessageTemplate IllegalGoto{"illegal goto from state '{}' to '{}'"};
inline constexpr MessageTemplate ReturnOnlyAllowedInFinalHandler{
    "return is only allowed in final state handlers"};
inline constexpr MessageTemplate FlowHandlerStateNotDeclared{
    "flow handler state '{}' is not declared in the target agent"};
inline constexpr MessageTemplate DuplicateFlowHandler{"duplicate flow handler for state '{}'"};
inline constexpr MessageTemplate MissingFinalStateHandler{"missing final-state handler for '{}'"};
inline constexpr MessageTemplate MissingNonFinalStateHandler{
    "missing non-final-state handler for '{}'"};
inline constexpr MessageTemplate FinalStateHandlerMustReturn{
    "final-state handler '{}' must end with return on all control paths"};
inline constexpr MessageTemplate NonFinalStateHandlerMustGoto{
    "non-final-state handler '{}' must end with goto on all control paths"};
inline constexpr MessageTemplate TemporalEmbeddedExprMustBeBool{
    "temporal embedded expression must have type Bool"};
inline constexpr MessageTemplate TemporalEmbeddedExprMustBePure{
    "temporal embedded expression must be pure"};
inline constexpr MessageTemplate UnknownContractState{"unknown state '{}' in contract for '{}'"};
inline constexpr MessageTemplate RunningOnlyWorkflow{
    "running(...) is only valid in workflow safety/liveness formulas"};
inline constexpr MessageTemplate CompletedOnlyWorkflow{
    "completed(...) is only valid in workflow safety/liveness formulas"};
inline constexpr MessageTemplate CalledOnlyAgentContracts{
    "called(...) is only valid in agent contracts"};
inline constexpr MessageTemplate InStateOnlyAgentContracts{
    "in_state(...) is only valid in agent contracts"};
inline constexpr MessageTemplate UnknownWorkflowNode{"unknown workflow node '{}'"};
inline constexpr MessageTemplate WorkflowCompletedStateNotFinal{
    "state '{}' is not a final state of node '{}'"};
inline constexpr MessageTemplate DuplicateWorkflowNode{"duplicate workflow node '{}'"};
inline constexpr MessageTemplate UnknownWorkflowDependency{"unknown workflow dependency '{}'"};
inline constexpr MessageTemplate WorkflowDependencyCycle{
    "workflow dependency cycle detected involving '{}'"};
inline constexpr MessageTemplate FailureSummaryEmptyMessage{
    "{} contains failure summary with empty message"};
inline constexpr MessageTemplate FailureSummaryEmptyNodeName{
    "{} contains failure summary with empty node_name"};
} // namespace validation
} // namespace messages

// ============================================================================
// Diagnostic Struct
// ============================================================================

struct Diagnostic {
    DiagnosticSeverity severity{DiagnosticSeverity::Error};
    std::string message;
    std::optional<std::string> code;
    std::optional<SourceRange> range;
    std::optional<std::string> source_name;
    std::optional<SourcePosition> position;

    // Secondary "related" notes attached to this diagnostic. They share the
    // diagnostic's owning bag (i.e. they do not contribute to error/warning
    // counts), and are rendered immediately after the primary message.
    struct Related {
        std::string message;
        std::optional<SourceRange> range;
    };
    std::vector<Related> related;
};

// ============================================================================
// Forward Declarations
// ============================================================================

class DiagnosticBag;

// ============================================================================
// Diagnostic Builder - Fluent API
// ============================================================================

class DiagnosticBuilder {
  public:
    DiagnosticBuilder(DiagnosticBag &bag, DiagnosticSeverity severity)
        : bag_(&bag), severity_(severity) {}

    // Set message directly
    DiagnosticBuilder &&message(std::string msg) && {
        message_ = std::move(msg);
        return std::move(*this);
    }

    // Set message from template with arguments
    DiagnosticBuilder &&message(MessageTemplate tmpl) && {
        message_ = tmpl.format_with();
        return std::move(*this);
    }

    template <typename... Args>
        requires(sizeof...(Args) > 0 && (std::convertible_to<Args, std::string_view> && ...))
    DiagnosticBuilder &&message(MessageTemplate tmpl, Args &&...args) && {
        message_ = tmpl.format_with(std::forward<Args>(args)...);
        return std::move(*this);
    }

    // Set structured error code
    template <DiagnosticCategory Cat> DiagnosticBuilder &&code(ErrorCode<Cat> err_code) && {
        code_ = err_code.full_code();
        return std::move(*this);
    }

    // Set source range
    DiagnosticBuilder &&range(SourceRange r) && {
        range_ = r;
        return std::move(*this);
    }

    DiagnosticBuilder &&range(std::optional<SourceRange> r) && {
        range_ = r;
        return std::move(*this);
    }

    // Set source from SourceFile directly
    DiagnosticBuilder &&source(const SourceFile &src) && {
        source_name_ = src.display_name;
        if (range_.has_value()) {
            position_ = src.locate(range_->begin_offset);
        }
        return std::move(*this);
    }

    // Set source explicitly
    DiagnosticBuilder &&source_name(std::string name,
                                    std::optional<SourcePosition> pos = std::nullopt) && {
        source_name_ = std::move(name);
        position_ = pos;
        return std::move(*this);
    }

    // Set position directly
    DiagnosticBuilder &&position(SourcePosition pos) && {
        position_ = pos;
        return std::move(*this);
    }

    // Attach a secondary "related" note (e.g. "expected here", "declared here").
    // The note inherits the primary diagnostic's source_name; its `range` is
    // optional so callers can omit it when only contextual prose is needed.
    DiagnosticBuilder &&with_note(std::string note_message,
                                  std::optional<SourceRange> note_range = std::nullopt) && {
        related_.push_back(Diagnostic::Related{
            .message = std::move(note_message),
            .range = note_range,
        });
        return std::move(*this);
    }

    // Emit the diagnostic
    void emit() &&;

  private:
    DiagnosticBag *bag_;
    DiagnosticSeverity severity_;
    std::string message_;
    std::optional<std::string> code_;
    std::optional<SourceRange> range_;
    std::optional<std::string> source_name_;
    std::optional<SourcePosition> position_;
    std::vector<Diagnostic::Related> related_;
};

// ============================================================================
// Diagnostic Bag
// ============================================================================

class DiagnosticBag {
  public:
    // === New Builder-Based API ===

    [[nodiscard]] DiagnosticBuilder note() {
        return DiagnosticBuilder(*this, DiagnosticSeverity::Note);
    }
    [[nodiscard]] DiagnosticBuilder warning() {
        return DiagnosticBuilder(*this, DiagnosticSeverity::Warning);
    }
    [[nodiscard]] DiagnosticBuilder error() {
        return DiagnosticBuilder(*this, DiagnosticSeverity::Error);
    }

    // Convenience: Pre-ranged builders
    [[nodiscard]] DiagnosticBuilder note(SourceRange r) {
        return DiagnosticBuilder(*this, DiagnosticSeverity::Note).range(r);
    }
    [[nodiscard]] DiagnosticBuilder warning(SourceRange r) {
        return DiagnosticBuilder(*this, DiagnosticSeverity::Warning).range(r);
    }
    [[nodiscard]] DiagnosticBuilder error(SourceRange r) {
        return DiagnosticBuilder(*this, DiagnosticSeverity::Error).range(r);
    }

    // === Core Operations ===

    [[nodiscard]] bool has_error() const noexcept {
        return error_count_ > 0;
    }
    [[nodiscard]] bool has_warning() const noexcept {
        return warning_count_ > 0;
    }
    [[nodiscard]] std::size_t error_count() const noexcept {
        return error_count_;
    }
    [[nodiscard]] std::size_t warning_count() const noexcept {
        return warning_count_;
    }

    [[nodiscard]] const std::vector<Diagnostic> &entries() const noexcept {
        return diagnostics_;
    }

    void append(const DiagnosticBag &other) {
        error_count_ += other.error_count_;
        warning_count_ += other.warning_count_;
        diagnostics_.insert(
            diagnostics_.end(), other.diagnostics_.begin(), other.diagnostics_.end());
    }

    void append_from_source(const DiagnosticBag &other, const SourceFile &source) {
        error_count_ += other.error_count_;
        warning_count_ += other.warning_count_;
        for (const auto &entry : other.diagnostics_) {
            auto diagnostic = entry;
            if (!diagnostic.source_name.has_value()) {
                diagnostic.source_name = source.display_name;
            }
            if (!diagnostic.position.has_value() && diagnostic.range.has_value()) {
                diagnostic.position = source.locate(diagnostic.range->begin_offset);
            }
            diagnostics_.push_back(std::move(diagnostic));
        }
    }

    void render(std::ostream &out,
                MaybeCRef<SourceFile> source = std::nullopt,
                bool include_code = false) const {
        for (const auto &diagnostic : diagnostics_) {
            out << to_string(diagnostic.severity);
            if (include_code && diagnostic.code.has_value()) {
                out << " [" << *diagnostic.code << "]";
            }
            out << ": " << diagnostic.message;

            if (diagnostic.source_name.has_value()) {
                out << " (" << *diagnostic.source_name;
                if (diagnostic.position.has_value()) {
                    out << ":" << diagnostic.position->line << ":" << diagnostic.position->column;
                }
                out << ")";
            } else if (source.has_value() && diagnostic.range.has_value()) {
                const auto &source_file = source->get();
                const auto position = source_file.locate(diagnostic.range->begin_offset);
                out << " (" << source_file.display_name << ":" << position.line << ":"
                    << position.column << ")";
            }

            out << '\n';

            // Source snippet + caret rendering
            if (source.has_value() && diagnostic.range.has_value() && !diagnostic.range->empty()) {
                auto &src = source->get();
                src.refresh_line_starts_cache();
                const auto &starts = src.line_starts_cache;
                auto begin_pos = src.locate(diagnostic.range->begin_offset);

                std::size_t line_idx = begin_pos.line - 1;
                std::size_t line_start = starts[line_idx];
                std::size_t line_end =
                    (line_idx + 1 < starts.size()) ? starts[line_idx + 1] - 1 : src.content.size();
                if (line_end > line_start && line_end <= src.content.size() &&
                    src.content[line_end - 1] == '\r') {
                    --line_end;
                }

                std::string_view line_text =
                    std::string_view(src.content).substr(line_start, line_end - line_start);

                std::string line_num = std::to_string(begin_pos.line);
                std::string gutter(line_num.size(), ' ');

                out << "  " << line_num << " | " << line_text << '\n';

                std::size_t col_start = begin_pos.column - 1;
                std::size_t span_end_in_line = diagnostic.range->end_offset - line_start;
                if (span_end_in_line > line_text.size()) {
                    span_end_in_line = line_text.size();
                }
                if (span_end_in_line <= col_start) {
                    span_end_in_line = col_start + 1;
                }

                out << "  " << gutter << " | " << std::string(col_start, ' ')
                    << std::string(span_end_in_line - col_start, '~') << '\n';
            }

            // Render attached "related" notes (e.g. "note: expected here").
            for (const auto &related : diagnostic.related) {
                out << "  note: " << related.message;
                if (related.range.has_value()) {
                    if (diagnostic.source_name.has_value()) {
                        out << " (" << *diagnostic.source_name;
                        if (source.has_value()) {
                            const auto pos = source->get().locate(related.range->begin_offset);
                            out << ":" << pos.line << ":" << pos.column;
                        }
                        out << ")";
                    } else if (source.has_value()) {
                        const auto &src_file = source->get();
                        const auto pos = src_file.locate(related.range->begin_offset);
                        out << " (" << src_file.display_name << ":" << pos.line << ":" << pos.column
                            << ")";
                    }
                }
                out << '\n';
            }
        }
    }

    // Internal method for DiagnosticBuilder
    void add_diagnostic(Diagnostic diagnostic) {
        if (diagnostic.severity == DiagnosticSeverity::Error) {
            ++error_count_;
        } else if (diagnostic.severity == DiagnosticSeverity::Warning) {
            ++warning_count_;
        }
        diagnostics_.push_back(std::move(diagnostic));
    }

  private:
    std::vector<Diagnostic> diagnostics_;
    std::size_t error_count_{0};
    std::size_t warning_count_{0};
};

// Implementation of DiagnosticBuilder::emit()
inline void DiagnosticBuilder::emit() && {
    bag_->add_diagnostic(Diagnostic{
        .severity = severity_,
        .message = std::move(message_),
        .code = std::move(code_),
        .range = range_,
        .source_name = std::move(source_name_),
        .position = position_,
        .related = std::move(related_),
    });
}

} // namespace ahfl
