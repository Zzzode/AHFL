#pragma once

#include <concepts>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ahfl/support/ownership.hpp"
#include "ahfl/support/source.hpp"

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
              ? (result.replace(pos, placeholder.length(),
                                std::string_view(std::forward<Args>(args))),
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
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> UnknownType{"UNKNOWN_TYPE"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> InvalidOperation{"INVALID_OPERATION"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> NonPureExpression{"NON_PURE_EXPRESSION"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> InvalidMemberAccess{
    "INVALID_MEMBER_ACCESS"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> UnknownField{"UNKNOWN_FIELD"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> MissingField{"MISSING_FIELD"};
inline constexpr ErrorCode<DiagnosticCategory::TypeCheck> DuplicateField{"DUPLICATE_FIELD"};
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
} // namespace typecheck

namespace validation {
inline constexpr ErrorCode<DiagnosticCategory::Validation> VersionMismatch{"VERSION_MISMATCH"};
inline constexpr ErrorCode<DiagnosticCategory::Validation> RequiredFieldEmpty{
    "REQUIRED_FIELD_EMPTY"};
inline constexpr ErrorCode<DiagnosticCategory::Validation> OptionalFieldEmpty{
    "OPTIONAL_FIELD_EMPTY"};
inline constexpr ErrorCode<DiagnosticCategory::Validation> InvalidState{"INVALID_STATE"};
inline constexpr ErrorCode<DiagnosticCategory::Validation> FailureSummaryEmptyMessage{
    "FAILURE_SUMMARY_EMPTY_MESSAGE"};
inline constexpr ErrorCode<DiagnosticCategory::Validation> FailureSummaryEmptyNodeName{
    "FAILURE_SUMMARY_EMPTY_NODE_NAME"};
} // namespace validation

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
inline constexpr MessageTemplate InvalidMemberAccess{
    "member access requires a struct value, got {}"};
inline constexpr MessageTemplate UnknownField{"unknown field '{}' on struct '{}'"};
inline constexpr MessageTemplate MissingField{"missing field '{}' in struct literal"};
inline constexpr MessageTemplate DuplicateField{"duplicate field '{}' in struct literal"};
inline constexpr MessageTemplate InvalidOperator{"operator '{}' is not defined for {} and {}"};
inline constexpr MessageTemplate NoneWithoutContext{
    "cannot infer type of 'none' without an expected Optional<T> context"};
inline constexpr MessageTemplate EmptyListWithoutContext{"cannot infer type of empty list literal"};
inline constexpr MessageTemplate EmptySetWithoutContext{"cannot infer type of empty set literal"};
inline constexpr MessageTemplate EmptyMapWithoutContext{"cannot infer type of empty map literal"};
inline constexpr MessageTemplate InvalidAgentType{"agent {} type must resolve to a struct type"};
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
} // namespace typecheck

namespace validation {
inline constexpr MessageTemplate VersionMismatch{"{} must be '{}'"};
inline constexpr MessageTemplate RequiredFieldEmpty{"{} must not be empty"};
inline constexpr MessageTemplate OptionalFieldEmpty{"{} {}"};
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

    [[nodiscard]] bool has_error() const noexcept { return error_count_ > 0; }
    [[nodiscard]] bool has_warning() const noexcept { return warning_count_ > 0; }
    [[nodiscard]] std::size_t error_count() const noexcept { return error_count_; }
    [[nodiscard]] std::size_t warning_count() const noexcept { return warning_count_; }

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
            if (source.has_value() && diagnostic.range.has_value() &&
                !diagnostic.range->empty()) {
                auto &src = source->get();
                src.refresh_line_starts_cache();
                const auto &starts = src.line_starts_cache;
                auto begin_pos = src.locate(diagnostic.range->begin_offset);

                std::size_t line_idx = begin_pos.line - 1;
                std::size_t line_start = starts[line_idx];
                std::size_t line_end = (line_idx + 1 < starts.size())
                                           ? starts[line_idx + 1] - 1
                                           : src.content.size();
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
    });
}

} // namespace ahfl
