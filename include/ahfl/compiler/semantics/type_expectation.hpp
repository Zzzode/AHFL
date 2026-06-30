#pragma once

#include "ahfl/base/support/source.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/types.hpp"

#include <functional>
#include <string>
#include <vector>

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
    // g-1 Phase 2: per-argument TypeMismatch enhancement. When non-empty,
    // the expected-side note embeds the callable name (e.g. "declared in
    // `foo`"). Only meaningful when origin_kind == FunctionParameter and the
    // mismatch arose from a call-site argument assignability check.
    std::string callable_name;
    // g-1 Phase 2: 1-based argument position index. std::nullopt means the
    // mismatch did not arise from a positional argument slot.
    std::optional<std::size_t> argument_index;
};

[[nodiscard]] std::string describe_type_expectation_origin(TypeExpectationOriginKind kind);
[[nodiscard]] std::string expected_type_note(const Type &target,
                                             const TypeExpectation &expectation);
[[nodiscard]] std::string expected_schema_note(const Type &target,
                                               const TypeExpectation &expectation);
[[nodiscard]] std::string actual_type_note(const Type &source);

// g-4 MultipleModuleDeclarations compromise: helpers used by TypeMismatch
// emission points to surface additional declaration sites when a nominal
// type (struct / enum / enum variant / named top-level fn) is declared in
// more than one module with the same local name.

// Returns every symbol in the resolver's symbol table that is a nominal
// declaration for `type`, grouped by shared local_name across modules.
// Output is deterministically ordered by (module_name, declaration_range).
[[nodiscard]] std::vector<std::reference_wrapper<const Symbol>>
collect_nominal_declarations(const Type &type, const SymbolTable &symbols);

// Same as collect_nominal_declarations but for the Functions namespace:
// returns all top-level `fn` symbols whose local name equals `fn_name`.
[[nodiscard]] std::vector<std::reference_wrapper<const Symbol>>
collect_function_declarations(std::string_view fn_name, const SymbolTable &symbols);

// If `declarations` has size >1, appends to `out`:
//   - one header note "role 'type_desc' declared in N locations (and N-1
//     other location(s))" anchored at the primary declaration range
//   - N-1 "other declaration in module 'M'" notes pointing at each additional
//     declaration site
//
// Uses the structured Diagnostic.related / ConstDiagnosticRelated note
// vector rather than a single-string compromise, because the IDE-facing
// rendering and JSON serialization already understand an arbitrary number
// of related notes — zero forward-compatibility risk.
void append_multi_declaration_notes(std::vector<Diagnostic::Related> &out,
                                    std::vector<std::reference_wrapper<const Symbol>> declarations,
                                    std::string_view type_descriptor,
                                    std::string_view role_label);

// g-1 Phase 2: complementary helper. When `declarations` has size == 1,
// appends a single "role 'type_desc' declared here in module 'M'" note
// anchored at the declaration site. This covers the common single-declaration
// nominal case (struct / enum / enum variant / trait / type alias) so users
// can navigate from either side of a TypeMismatch diagnostic back to the type
// definition. For declarations.size() > 1 this helper is a no-op — the
// multi-declaration case is already handled by append_multi_declaration_notes.
void append_nominal_declared_here_note(
    std::vector<Diagnostic::Related> &out,
    std::vector<std::reference_wrapper<const Symbol>> declarations,
    std::string_view type_descriptor,
    std::string_view role_label);

} // namespace ahfl
