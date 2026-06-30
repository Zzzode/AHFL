#include "ahfl/compiler/semantics/type_expectation.hpp"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

namespace ahfl {

namespace {

[[nodiscard]] std::string_view local_name_of(std::string_view canonical_name) {
    const auto sep = canonical_name.find_last_of(':');
    if (sep == std::string_view::npos) {
        return canonical_name;
    }
    return canonical_name.substr(sep + 1);
}

} // namespace

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
    std::string result;
    if (expectation.argument_index.has_value()) {
        // g-1 Phase 2: per-argument diff note — surfaces the 1-based argument
        // index and (when known) the callable name so users can correlate a
        // call-site mismatch with the exact parameter slot.
        result = "argument #" + std::to_string(*expectation.argument_index) +
                 ": expected '" + target.describe() + "'";
        if (!expectation.callable_name.empty()) {
            result += " declared in `" + expectation.callable_name + "`";
        }
        result += " from " + source + " declared here";
    } else {
        result = "expected type '" + target.describe() + "' from " + source + " declared here";
    }
    return result;
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

std::vector<std::reference_wrapper<const Symbol>>
collect_nominal_declarations(const Type &type, const SymbolTable &symbols) {
    std::vector<std::reference_wrapper<const Symbol>> result;

    type.visit(types::Overloads{
        [&](const types::StructT &s) {
            const auto name = local_name_of(s.canonical_name);
            for (const auto id : symbols.find_all_local(SymbolNamespace::Types, name)) {
                if (auto sym = symbols.get(id); sym.has_value()) {
                    result.push_back(*sym);
                }
            }
        },
        [&](const types::EnumT &e) {
            const auto name = local_name_of(e.canonical_name);
            for (const auto id : symbols.find_all_local(SymbolNamespace::Types, name)) {
                if (auto sym = symbols.get(id); sym.has_value()) {
                    result.push_back(*sym);
                }
            }
        },
        [&](const types::EnumVariantT &v) {
            const auto name = local_name_of(v.canonical_name);
            for (const auto id : symbols.find_all_local(SymbolNamespace::Types, name)) {
                if (auto sym = symbols.get(id); sym.has_value()) {
                    result.push_back(*sym);
                }
            }
        },
        // TraitT / TypeAliasT are not yet first-class type variants in the
        // semantic type system; traits are collected below via a fallback
        // branch on the SymbolKind when the caller later narrows by kind.
        [](const auto &) { /* primitive / fn type / typevar -> no nominal match */ },
    });

    // De-duplicate by SymbolId.
    std::unordered_set<std::size_t> seen;
    std::vector<std::reference_wrapper<const Symbol>> unique;
    unique.reserve(result.size());
    for (const auto &ref : result) {
        if (const auto [_, inserted] = seen.insert(ref.get().id.value); inserted) {
            unique.push_back(ref);
        }
    }

    std::sort(unique.begin(), unique.end(), [](const Symbol &lhs, const Symbol &rhs) {
        if (lhs.module_name != rhs.module_name) {
            return lhs.module_name < rhs.module_name;
        }
        return lhs.declaration_range.begin_offset < rhs.declaration_range.begin_offset;
    });
    return unique;
}

std::vector<std::reference_wrapper<const Symbol>>
collect_function_declarations(std::string_view name, const SymbolTable &symbols) {
    std::vector<std::reference_wrapper<const Symbol>> result;
    if (name.empty()) {
        return result;
    }
    for (const auto id : symbols.find_all_local(SymbolNamespace::Functions, name)) {
        if (auto sym = symbols.get(id); sym.has_value()) {
            result.push_back(*sym);
        }
    }
    std::sort(result.begin(), result.end(), [](const Symbol &lhs, const Symbol &rhs) {
        if (lhs.module_name != rhs.module_name) {
            return lhs.module_name < rhs.module_name;
        }
        return lhs.declaration_range.begin_offset < rhs.declaration_range.begin_offset;
    });
    return result;
}

void append_multi_declaration_notes(std::vector<Diagnostic::Related> &out,
                                    std::vector<std::reference_wrapper<const Symbol>> declarations,
                                    std::string_view type_descriptor,
                                    std::string_view role_label) {
    if (declarations.size() <= 1) {
        return;
    }
    // Best-effort human-readable label for each declaration site. When the
    // resolver left `source_id` empty (e.g. synthetic stdlib symbols or
    // inline test inputs) we fall back to the "module 'X'" label used by
    // the diagnostic message so CLI rendering can still anchor cross-module
    // notes to a sensible name even without a registered SourceFile.
    auto label_for = [](const Symbol &sym) -> std::optional<std::string> {
        return std::make_optional(std::string("module '") + sym.module_name + "'");
    };

    const auto N = declarations.size();
    const auto &first = declarations.front().get();
    out.push_back(Diagnostic::Related{
        .message = std::string(role_label) + " '" + std::string(type_descriptor) +
                   "' declared in " + std::to_string(N) + " locations (and " +
                   std::to_string(N - 1) + " other location" + (N - 1 == 1 ? "" : "s") + ")",
        .range = first.declaration_range,
        .source_id = first.source_id,
        .source_name = label_for(first),
    });
    for (std::size_t i = 1; i < N; ++i) {
        const auto &sym = declarations[i].get();
        out.push_back(Diagnostic::Related{
            .message = "other declaration in module '" + sym.module_name + "'",
            .range = sym.declaration_range,
            .source_id = sym.source_id,
            .source_name = label_for(sym),
        });
    }
}

void append_nominal_declared_here_note(
    std::vector<Diagnostic::Related> &out,
    std::vector<std::reference_wrapper<const Symbol>> declarations,
    std::string_view type_descriptor,
    std::string_view role_label) {
    if (declarations.size() != 1) {
        return;
    }
    const auto &sym = declarations.front().get();
    out.push_back(Diagnostic::Related{
        .message = std::string(role_label) + " '" + std::string(type_descriptor) +
                   "' declared here in module '" + sym.module_name + "'",
        .range = sym.declaration_range,
        .source_id = sym.source_id,
        .source_name = std::make_optional(std::string("module '") + sym.module_name + "'"),
    });
}

} // namespace ahfl
