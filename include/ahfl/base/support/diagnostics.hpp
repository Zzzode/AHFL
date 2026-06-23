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
// P1 (ADT, RFC §1.6): match typecheck diagnostics. Surfaced by the P1b
// match typecheck pass (scrutinee narrowing + arm unification + exhaustiveness).
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> MatchNotYetSupported{
    "MATCH_NOT_YET_SUPPORTED"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> MatchScrutineeRequiresEnum{
    "MATCH_SCRUTINEE_REQUIRES_ENUM"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> MatchUnknownVariant{
    "MATCH_UNKNOWN_VARIANT"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> MatchVariantPayloadArity{
    "MATCH_VARIANT_PAYLOAD_ARITY"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> MatchNotExhaustive{
    "MATCH_NOT_EXHAUSTIVE"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> MatchArmTypeMismatch{
    "MATCH_ARM_TYPE_MISMATCH"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> MatchDuplicateBinding{
    "MATCH_DUPLICATE_BINDING"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> MatchPatternBindingTypeMismatch{
    "MATCH_PATTERN_BINDING_TYPE_MISMATCH"};
// P2 (RFC §6): closure typecheck lands in P2b; surfaced by P2a parsers so a
// lambda never silently type-checks to the wrong shape before its pass exists.
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> LambdaNotYetSupported{
    "LAMBDA_NOT_YET_SUPPORTED"};
// P2 (RFC §3.2.2): fn-declaration typecheck lands in P2b; surfaced by P2a so
// a parsed fn body is not silently skipped before the fn pass exists.
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> FnDeclNotYetSupported{
    "FN_DECL_NOT_YET_SUPPORTED"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> InvalidBuiltinAttribute{
    "INVALID_BUILTIN_ATTRIBUTE"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> UnknownBuiltinHook{
    "UNKNOWN_BUILTIN_HOOK"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> MissingBuiltinEffect{
    "MISSING_BUILTIN_EFFECT"};
// P3 (RFC §3.2.2 / type-system §2): trait resolution + coherence diagnostics.
// Surfaced by the P3b trait/impl typecheck pass: orphan rule, impl-trait
// signature matching, super-trait coverage, and (later) method-call resolution.
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> OrphanImpl{"ORPHAN_IMPL"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> DuplicateTraitImpl{
    "DUPLICATE_TRAIT_IMPL"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> ImplTraitUnknown{"IMPL_TRAIT_UNKNOWN"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> ImplTargetUnknown{"IMPL_TARGET_UNKNOWN"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> TraitMethodNotFound{
    "TRAIT_METHOD_NOT_FOUND"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> TraitMethodSignatureMismatch{
    "TRAIT_METHOD_SIGNATURE_MISMATCH"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> TraitAssocTypeNotFound{
    "TRAIT_ASSOC_TYPE_NOT_FOUND"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> MissingSuperTrait{"MISSING_SUPER_TRAIT"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> NoTraitImpl{"NO_TRAIT_IMPL"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> AmbiguousTraitImpl{
    "AMBIGUOUS_TRAIT_IMPL"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> TraitBoundNotSatisfied{
    "TRAIT_BOUND_NOT_SATISFIED"};
// P3c.S6 Trait/Impl additional codes: method-lookup, inherent-vs-trait conflict
// and signature-mismatch diagnostics used by the Trait/Impl resolver smoke suite.
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> MethodNotFound{"METHOD_NOT_FOUND"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> MethodSignatureMismatch{
    "METHOD_SIGNATURE_MISMATCH"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> AssocTypeNotFound{"ASSOC_TYPE_NOT_FOUND"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> InherentTraitConflict{
    "INHERENT_TRAIT_CONFLICT"};
// P4a (RFC corelib-effect-system.zh.md §2.6.4 / §3.4 / §4.5): effect-system
// diagnostics. Surfaced by the P4a effect-judgement + verified-subset checks.
//   effect_not_pure          — pure-context call resolved to a non-Pure effect
//   no_decreases             — Pure fn missing a decreases measure (RFC §3.4)
//   not_in_verified_subset   — umbrella code: call entering a verified-subset
//                              context fails one of the subset conditions
//                              (§4.5), always accompanied by a specific code.
//   effect_underdeclared     — declared effect does not cover the inferred
//                              body effect (§2.6.4)
//   effect_incompatible      — Nondet and capability effect coexist in the
//                              same judgement (§2.6.4)
//   effect_on_predicate      — predicate declaration carries an explicit
//                              effect clause (§2.4; predicates are implicitly
//                              Pure, users may not override)
//   nondet_in_invariant      — Nondet fn or now appears in an invariant /
//                              safety / liveness formula (§4.2)
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> EffectNotPure{"EFFECT_NOT_PURE"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> NoDecreases{"NO_DECREASES"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> NotInVerifiedSubset{
    "NOT_IN_VERIFIED_SUBSET"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> EffectUnderdeclared{
    "EFFECT_UNDERDECLARED"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> EffectIncompatible{"EFFECT_INCOMPATIBLE"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> EffectOnPredicate{"EFFECT_ON_PREDICATE"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> NondetInInvariant{"NONDET_IN_INVARIANT"};
} // namespace typecheck

namespace resolve {
// Deduplicated, single-source-of-truth orphan-rule error code. Trait/Impl
// orphan-reject diagnostics are surfaced during resolution and re-used by
// downstream passes via the same identifier.
inline constexpr ErrorCode<DiagnosticCategory::Resolve> TraitOrphanImpl{
    "TRAIT_ORPHAN_IMPL"};
} // namespace resolve

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
inline constexpr ErrorCode<DiagnosticCategory::Validation> DecreasesNotProven{
    "DECREASES_NOT_PROVEN"};
inline constexpr ErrorCode<DiagnosticCategory::Validation> DecreasesNonLex{
    "DECREASES_NON_LEX"};
inline constexpr ErrorCode<DiagnosticCategory::Validation> DecreasesWildcardInvalid{
    "DECREASES_WILDCARD_INVALID"};
inline constexpr ErrorCode<DiagnosticCategory::Validation> DecreasesEmpty{"DECREASES_EMPTY"};
inline constexpr ErrorCode<DiagnosticCategory::Validation> DecreasesShadowedReceiver{
    "DECREASES_SHADOWED_RECEIVER"};
inline constexpr ErrorCode<DiagnosticCategory::Validation> DecreasesInNonPure{
    "DECREASES_IN_NON_PURE"};
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
// ---- Trait / Impl messages ----
inline constexpr MessageTemplate TraitOrphanImpl{
    "impl for trait '{}' on type '{}' violates the orphan rule: neither the trait nor the type is local to this module"};
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
inline constexpr MessageTemplate StdContainerTypeUnavailable{
    "stdlib container type '{}' is unavailable"};
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
// P1 (ADT): match typecheck lands in P1b; surfaced by P1a parsers.
inline constexpr MessageTemplate MatchNotYetSupported{
    "'match' expressions are not yet type-checked (ADT support is in progress)"};
// P1b match typecheck messages.
inline constexpr MessageTemplate MatchScrutineeRequiresEnum{
    "'match' scrutinee must have an enum type, got {}"};
inline constexpr MessageTemplate MatchUnknownVariant{
    "match arm references unknown variant '{}' of enum '{}'"};
inline constexpr MessageTemplate MatchVariantPayloadArity{
    "variant '{}' of enum '{}' expects {} payload slot(s), got {} in pattern"};
inline constexpr MessageTemplate MatchNotExhaustive{
    "match is not exhaustive: variant(s) not covered: {}"};
inline constexpr MessageTemplate MatchArmTypeMismatch{
    "match arm body type mismatch: expected {}, got {}"};
inline constexpr MessageTemplate MatchDuplicateBinding{"duplicate binding '{}' in match pattern"};
inline constexpr MessageTemplate MatchPatternBindingTypeMismatch{
    "match binding '{}' expects type {}, got payload slot type {}"};
// P2 (RFC §6): closure typecheck lands in P2b; surfaced by P2a parsers.
inline constexpr MessageTemplate LambdaNotYetSupported{
    "'lambda' expressions are not yet type-checked (closure support is in progress)"};
// P2 (RFC §3.2.2): fn-declaration typecheck lands in P2b; surfaced by P2a.
inline constexpr MessageTemplate FnDeclNotYetSupported{
    "'fn' declarations are not yet type-checked (function support is in progress)"};
inline constexpr MessageTemplate InvalidBuiltinAttribute{"@builtin is only allowed in std modules"};
inline constexpr MessageTemplate UnknownBuiltinHook{"unknown @builtin hook '{}'"};
inline constexpr MessageTemplate MissingBuiltinEffect{
    "@builtin functions must declare an explicit effect clause"};
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
// P3 (RFC §3.2.2 / type-system §2) trait / impl diagnostics. Mirrors the
// diagnostic catalogue in type-system.zh.md §2.1 / §2.2 (orphan rule).
inline constexpr MessageTemplate OrphanImpl{
    "impl '{}' for '{}' is an orphan: neither the type nor the trait is defined in module '{}'"};
inline constexpr MessageTemplate OrphanImplHint{
    "move this impl to the module that defines '{}' or '{}'"};
inline constexpr MessageTemplate DuplicateTraitImpl{
    "impl '{}' for '{}' duplicates an earlier impl of the same trait and type"};
inline constexpr MessageTemplate ImplTraitUnknown{"impl references unknown trait '{}'"};
inline constexpr MessageTemplate ImplTargetUnknown{"impl targets unknown type '{}'"};
inline constexpr MessageTemplate ImplTargetMustBeNominal{
    "impl target must be a nominal type (struct/enum), got {}"};
inline constexpr MessageTemplate TraitMethodNotFound{
    "trait '{}' declares method '{}' but impl does not provide it"};
inline constexpr MessageTemplate TraitMethodNotInTrait{
    "impl provides method '{}' which is not declared by trait '{}'"};
inline constexpr MessageTemplate TraitMethodSignatureMismatch{
    "method '{}' signature mismatch: trait expects ({}), impl provides ({})"};
inline constexpr MessageTemplate TraitAssocTypeNotFound{
    "trait '{}' declares associated type '{}' but impl does not provide it"};
inline constexpr MessageTemplate MissingSuperTrait{
    "trait '{}' requires super-trait '{}' but no impl is found for '{}'"};
inline constexpr MessageTemplate NoTraitImpl{"type '{}' does not implement trait '{}'"};
// P3c.S6 (trait/impl smoke) re-words AmbiguousTraitImpl and
// TraitBoundNotSatisfied to match the smoke-test assertion strings and
// the extended TraitBoundNotSatisfied placeholder arity (bound_type,
// bound_trait, impl_type). No src pass uses TraitBoundNotSatisfied yet;
// AmbiguousTraitImpl call sites in typecheck_expr.cpp pass 2 args which
// still render with the new 2-placeholder wording.
inline constexpr MessageTemplate AmbiguousTraitImpl{
    "multiple trait implementations match for type '{}' and trait '{}'"};
inline constexpr MessageTemplate TraitBoundNotSatisfied{
    "trait bound '{}: {}' is not satisfied by type '{}'"};
inline constexpr MessageTemplate TraitSelfNotYetSupported{
    "'Self' type in trait bounds is not yet supported (P3b only resolves named trait/type "
    "references)"};
// P3c.S6 additional Trait/Impl diagnostic messages: extended signature
// mismatch (named impl/trait context), inherent-vs-trait conflict and the
// method/assoc lookup variants used by the Trait/Impl smoke suite.
inline constexpr MessageTemplate MethodNotFound{
    "method '{}' not found on type '{}'"};
inline constexpr MessageTemplate MethodSignatureMismatch{
    "method '{}' signature mismatch on impl '{}' of trait '{}': expected '{}', got '{}'"};
inline constexpr MessageTemplate AssocTypeNotFound{
    "associated type '{}' not found on trait '{}'"};
inline constexpr MessageTemplate InherentTraitConflict{
    "member '{}' on '{}' conflicts between inherent impl and trait impl of '{}'"};
// P4a (RFC corelib-effect-system.zh.md §2.6.4 / §3.4 / §4.5): effect-system
// messages. Mirror the §4.5 diagnostic catalogue.
inline constexpr MessageTemplate EffectNotPure{
    "function call '{}' in a verified-subset context must be effect Pure, but its declared effect "
    "is {}"};
inline constexpr MessageTemplate NoDecreases{
    "effect Pure function '{}' must declare a decreases termination measure"};
inline constexpr MessageTemplate NotInVerifiedSubset{
    "call to '{}' is not in the verifiable subset: {}"};
inline constexpr MessageTemplate EffectUnderdeclared{
    "function '{}' declares effect {} but its body infers effect {}; declared effect must be an "
    "upper bound of the body effect"};
inline constexpr MessageTemplate EffectIncompatible{
    "incompatible effects: Nondet and capability effects cannot be combined in the same judgement"};
inline constexpr MessageTemplate EffectOnPredicate{
    "predicate declarations may not carry an explicit effect clause; predicates are implicitly "
    "effect Pure"};
inline constexpr MessageTemplate NondetInInvariant{
    "non-deterministic expression in invariant/safety/liveness formula: {}"};
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
inline constexpr MessageTemplate DecreasesNotProven{
    "cannot prove termination: measure '{}' is not strictly decreasing along all paths"};
inline constexpr MessageTemplate DecreasesNonLex{
    "termination measure '{}' is not a well-founded lexicographic tuple: component {}"};
inline constexpr MessageTemplate DecreasesWildcardInvalid{
    "wildcard '_' is not allowed as a termination measure component (position {})"};
inline constexpr MessageTemplate DecreasesEmpty{
    "decreases clause is empty: expected at least one measure expression"};
inline constexpr MessageTemplate DecreasesShadowedReceiver{
    "termination receiver '{}' shadows an outer binding of type '{}' (receiver is still safe)"};
inline constexpr MessageTemplate DecreasesInNonPure{
    "decreases clause is only allowed in pure predicates; '{}' is marked impure"};
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

// ============================================================================
// Diagnostic Report
// ============================================================================

/// A structured diagnostic report that wraps a collection of diagnostics
/// with metadata for identification and categorization.
///
/// Use cases:
/// - Pipeline stage diagnostic outputs with a stable result_id for correlation
/// - Tooling / IDE integration where report identity matters
/// - Serialized diagnostic bundles for caching or remote transport
struct DiagnosticReport {
    /// Unique identifier for this report.
    /// Used to correlate diagnostic results across invocations or stages.
    std::string result_id;

    /// Report kind — e.g. "full", "incremental", "snapshot".
    /// Lets consumers distinguish complete reports from partial updates.
    std::string kind{"full"};

    /// The diagnostic items contained in this report.
    std::vector<Diagnostic> diagnostics;

    // ---- Convenience accessors ----

    [[nodiscard]] bool has_error() const noexcept {
        for (const auto &d : diagnostics) {
            if (d.severity == DiagnosticSeverity::Error) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::size_t error_count() const noexcept {
        std::size_t count = 0;
        for (const auto &d : diagnostics) {
            if (d.severity == DiagnosticSeverity::Error) {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] std::size_t warning_count() const noexcept {
        std::size_t count = 0;
        for (const auto &d : diagnostics) {
            if (d.severity == DiagnosticSeverity::Warning) {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] std::size_t note_count() const noexcept {
        std::size_t count = 0;
        for (const auto &d : diagnostics) {
            if (d.severity == DiagnosticSeverity::Note) {
                ++count;
            }
        }
        return count;
    }

    /// Build a DiagnosticReport from a DiagnosticBag, copying all entries.
    [[nodiscard]] static DiagnosticReport
    from_bag(const DiagnosticBag &bag, std::string result_id = {}, std::string kind = "full") {
        DiagnosticReport report;
        report.result_id = std::move(result_id);
        report.kind = std::move(kind);
        report.diagnostics = bag.entries();
        return report;
    }

    /// Append another report's diagnostics into this one.
    /// Does not change result_id or kind of the receiving report.
    void append(const DiagnosticReport &other) {
        diagnostics.insert(diagnostics.end(), other.diagnostics.begin(), other.diagnostics.end());
    }
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
