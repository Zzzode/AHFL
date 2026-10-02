#pragma once

// KR6.7 (RFC 0026 P7): the ONE compile seam for conformance test
// infrastructure.
//
// Every conformance consumer that needs a compiled AHFL program (the evaluator
// engine adapter, the wasm eligibility classifier) runs the identical
// parse -> resolve -> typecheck -> validate -> lower pipeline through this
// function. It replaced the per-driver copies the bespoke end-to-end binaries
// each carried, so the pipeline order and its diagnostic rendering exist
// exactly once. Engine adapters add their own execution on top; they never
// re-implement compilation.
//
// WH-5c.7: a fixture whose first line is `// @repo-std` compiles through a
// project parse with the REPO std module root. This is the only way a single
// fixture can declare a bounded `std::collections::Map<K,V>` frame: the
// standalone parse does not see the std package, and the builtin descriptor
// SSOT pins Map's (Invariant K, Covariant V) variance, which a single-file
// fieldless declaration cannot satisfy through the exact metadata-drift gate.
// Every other fixture keeps the plain file parse.

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/ir/program_view.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/validate.hpp"
#include "compiler/syntax/frontend/project.hpp"
#include "runtime/value/value_json.hpp"

#include <unordered_map>

namespace ahfl::conformance {

namespace detail {

/// The well-known std module exports (mirrors the repo std package's
/// `ahfl.toml` export list). Kept in sync with
/// `test_support::repo_std_exports()` in tests/common/project_input_support.hpp.
[[nodiscard]] inline std::vector<std::string> repo_std_exports() {
    return {"prelude",
            "bool",
            "int",
            "float",
            "option",
            "result",
            "string",
            "collections",
            "cmp",
            "fmt",
            "decimal",
            "json",
            "time",
            "uuid",
            "traits"};
}

/// The compiler-intrinsics allow-list for the repo std module root.
[[nodiscard]] inline std::vector<std::string> repo_std_intrinsics_allow() {
    return {"option_*",
            "result_*",
            "list_*",
            "set_*",
            "map_*",
            "string_*",
            "decimal_*",
            "json_*",
            "time_*",
            "uuid_*",
            "cmp_raw_compare",
            "int_to_string",
            "bool_to_string",
            "float_to_string",
            "float_trunc_to_int",
            "int_to_float"};
}

/// Walk up from `start` until a directory containing `std/ahfl.toml` (the repo
/// root) is found. Returns the repo root, or an empty path if not found.
[[nodiscard]] inline std::filesystem::path
find_repo_root(const std::filesystem::path &start) {
    std::error_code ec;
    auto current = std::filesystem::absolute(start, ec);
    if (ec) {
        current = start;
        ec.clear();
    }
    if (std::filesystem::is_regular_file(current, ec)) {
        current = current.parent_path();
    }
    ec.clear();
    for (auto candidate = current; !candidate.empty(); candidate = candidate.parent_path()) {
        if (std::filesystem::exists(candidate / "std" / "ahfl.toml", ec) && !ec) {
            return candidate;
        }
        ec.clear();
        if (candidate == candidate.parent_path()) {
            break;
        }
    }
    return {};
}

/// Check whether the first line of `path` is the `// @repo-std` marker.
[[nodiscard]] inline bool has_repo_std_marker(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.good()) {
        return false;
    }
    std::string first_line;
    std::getline(in, first_line);
    static constexpr std::string_view kMarker = "// @repo-std";
    return first_line.compare(0, kMarker.size(), kMarker) == 0;
}

/// Build a ProjectInput that compiles `entry` against the repo std module root.
[[nodiscard]] inline std::optional<ProjectInput>
build_repo_std_project_input(const std::filesystem::path &entry) {
    const auto repo_root = find_repo_root(entry);
    if (repo_root.empty()) {
        return std::nullopt;
    }
    ProjectInput input;
    input.entry_files.push_back(entry);
    input.inject_prelude = true;
    input.module_roots.push_back(ProjectInput::ModuleRoot{
        .prefix = "std",
        .root = repo_root / "std",
        .exported_modules = repo_std_exports(),
        .compiler_intrinsics_allow = repo_std_intrinsics_allow(),
    });
    return input;
}

/// Decode a JSON DOM subtree into a runtime Value according to an IR type
/// reference. This is the type-aware counterpart of the schema-free
/// `value_from_json`: it walks the declared type alongside the JSON so that
/// nominal collections (Map/Set/List), Option, Unit, Decimal and Duration
/// decode to their typed Value variants rather than the schema-free defaults
/// (StructValue / ListValue / NoneValue / StringValue). Type kinds without a
/// richer wire representation (bounded scalars, UUID, Timestamp, ...) fall
/// back to `value_from_json` for that subtree.
[[nodiscard]] inline std::optional<runtime::Value>
decode_typed_value(const ir::TypeRef &type,
                   const ahfl::json::JsonValue &jv,
                   const ir::ProgramIndex &index,
                   std::string &error) {
    using K = ahfl::json::Kind;
    using NP = ahfl::json::NumberProvenance;

    switch (type.kind) {
    case ir::TypeRefKind::Int:
        if (jv.kind != K::Int || jv.number_provenance != NP::SignedInteger) {
            error = "expected a signed integer for Int";
            return std::nullopt;
        }
        return runtime::Value{runtime::IntValue{jv.int_val}};
    case ir::TypeRefKind::Float:
        if (jv.kind != K::Float || jv.number_provenance != NP::FloatSyntax) {
            error = "expected a float for Float";
            return std::nullopt;
        }
        return runtime::Value{runtime::FloatValue{jv.float_val}};
    case ir::TypeRefKind::String:
        if (jv.kind != K::String) {
            error = "expected a string for String";
            return std::nullopt;
        }
        return runtime::Value{runtime::StringValue{jv.string_val}};
    case ir::TypeRefKind::Decimal:
        if (jv.kind != K::String) {
            error = "expected a string spelling for Decimal";
            return std::nullopt;
        }
        return runtime::make_decimal(jv.string_val);
    case ir::TypeRefKind::Duration:
        if (jv.kind != K::String) {
            error = "expected a string spelling for Duration";
            return std::nullopt;
        }
        return runtime::make_duration(jv.string_val);
    case ir::TypeRefKind::Unit:
        if (jv.kind != K::Null) {
            error = "expected null for Unit";
            return std::nullopt;
        }
        return runtime::Value{runtime::UnitValue{}};
    case ir::TypeRefKind::Bool:
        if (jv.kind != K::Bool) {
            error = "expected a boolean for Bool";
            return std::nullopt;
        }
        return runtime::Value{runtime::BoolValue{jv.bool_val}};
    case ir::TypeRefKind::Struct: {
        const auto &name = type.canonical_name;
        if (name == "std::collections::Map") {
            if (jv.kind != K::Object) {
                error = "expected a JSON object for Map";
                return std::nullopt;
            }
            if (type.params.size() < 2 || !type.params[0] || !type.params[1]) {
                error = "Map type has malformed type parameters";
                return std::nullopt;
            }
            // Only String keys are wire-supported (matches the wire-schema gate).
            if (type.params[0]->kind != ir::TypeRefKind::String) {
                error = "only String keys are supported for Map wire decoding";
                return std::nullopt;
            }
            std::vector<std::pair<runtime::Value, runtime::Value>> entries;
            entries.reserve(jv.object_fields.size());
            for (const auto &[key, jval] : jv.object_fields) {
                if (!jval) {
                    error = "null JSON entry in Map";
                    return std::nullopt;
                }
                auto decoded =
                    decode_typed_value(*type.params[1], *jval, index, error);
                if (!decoded.has_value()) {
                    return std::nullopt;
                }
                entries.emplace_back(runtime::Value{runtime::StringValue{key}},
                                     std::move(*decoded));
            }
            return runtime::make_map(std::move(entries));
        }
        if (name == "std::collections::Set") {
            if (jv.kind != K::Array) {
                error = "expected a JSON array for Set";
                return std::nullopt;
            }
            if (type.params.empty() || !type.params[0]) {
                error = "Set type has malformed type parameters";
                return std::nullopt;
            }
            std::vector<runtime::Value> items;
            items.reserve(jv.array_items.size());
            for (const auto &jitem : jv.array_items) {
                if (!jitem) {
                    error = "null JSON item in Set";
                    return std::nullopt;
                }
                auto decoded =
                    decode_typed_value(*type.params[0], *jitem, index, error);
                if (!decoded.has_value()) {
                    return std::nullopt;
                }
                items.push_back(std::move(*decoded));
            }
            return runtime::make_set(std::move(items));
        }
        if (name == "std::collections::List") {
            if (jv.kind != K::Array) {
                error = "expected a JSON array for List";
                return std::nullopt;
            }
            if (type.params.empty() || !type.params[0]) {
                error = "List type has malformed type parameters";
                return std::nullopt;
            }
            std::vector<runtime::Value> items;
            items.reserve(jv.array_items.size());
            for (const auto &jitem : jv.array_items) {
                if (!jitem) {
                    error = "null JSON item in List";
                    return std::nullopt;
                }
                auto decoded =
                    decode_typed_value(*type.params[0], *jitem, index, error);
                if (!decoded.has_value()) {
                    return std::nullopt;
                }
                items.push_back(std::move(*decoded));
            }
            return runtime::make_list(std::move(items));
        }
        // User struct: look up the declaration and walk its fields.
        const auto *decl = index.find_struct(name);
        if (decl == nullptr) {
            error = "struct declaration not found: " + name;
            return std::nullopt;
        }
        if (jv.kind != K::Object) {
            error = "expected a JSON object for struct " + name;
            return std::nullopt;
        }
        std::unordered_map<std::string, runtime::Value> fields;
        fields.reserve(decl->fields.size());
        for (const auto &field : decl->fields) {
            const auto *jfield = jv.get(field.name);
            if (jfield == nullptr) {
                error = "missing field '" + field.name + "' in struct " + name;
                return std::nullopt;
            }
            auto decoded =
                decode_typed_value(field.type_ref, *jfield, index, error);
            if (!decoded.has_value()) {
                error = "field '" + field.name + "': " + error;
                return std::nullopt;
            }
            fields.emplace(field.name, std::move(*decoded));
        }
        return runtime::make_struct(name, std::move(fields));
    }
    case ir::TypeRefKind::Enum: {
        const auto &name = type.canonical_name;
        if (name == "std::option::Option") {
            if (jv.kind == K::Null) {
                return runtime::make_option_none();
            }
            if (type.params.empty() || !type.params[0]) {
                error = "Option type has malformed type parameters";
                return std::nullopt;
            }
            auto inner = decode_typed_value(*type.params[0], jv, index, error);
            if (!inner.has_value()) {
                return std::nullopt;
            }
            return runtime::make_option_some(std::move(*inner));
        }
        // Other enums use the _enum/_variant wire form; schema-free handles it.
        return runtime::value_from_json(jv);
    }
    default:
        // BoundedInt, BoundedString, UUID, Timestamp, Fn, Any, Never, ...:
        // no richer wire representation than the schema-free default.
        return runtime::value_from_json(jv);
    }
}

} // namespace detail

/// Compiles one AHFL source file through the standard pipeline. Returns the
/// lowered AHFL IR, or `nullopt` with the rendered diagnostics in `error_out`.
///
/// A fixture whose first line is `// @repo-std` compiles through a project
/// parse with the repo std module root (so `import std::collections` resolves
/// and the sysroot's Map declaration — with its SSOT variance — is used).
/// Every other fixture compiles through the plain single-file parse.
[[nodiscard]] inline std::optional<ir::Program>
compile_conformance_source(const std::filesystem::path &file_path, std::string &error_out) {
    const Frontend frontend;

    if (detail::has_repo_std_marker(file_path)) {
        auto project_input = detail::build_repo_std_project_input(file_path);
        if (!project_input.has_value()) {
            error_out = "repo-std marker present but repo root not found from: " +
                        file_path.string();
            return std::nullopt;
        }
        const auto parse_result = parse_project(frontend, *project_input);
        if (parse_result.has_errors()) {
            std::ostringstream out;
            parse_result.diagnostics.render(out);
            error_out = "parse failed:\n" + out.str();
            return std::nullopt;
        }

        const Resolver resolver;
        const auto resolve_result = resolver.resolve(parse_result.graph);
        if (resolve_result.has_errors()) {
            std::ostringstream out;
            resolve_result.diagnostics.render(out);
            error_out = "resolve failed:\n" + out.str();
            return std::nullopt;
        }

        const TypeChecker type_checker;
        const auto type_check_result =
            type_checker.check(parse_result.graph, resolve_result);
        if (type_check_result.has_errors()) {
            std::ostringstream out;
            type_check_result.diagnostics.render(out);
            error_out = "typecheck failed:\n" + out.str();
            return std::nullopt;
        }

        const Validator validator;
        const auto validation_result =
            validator.validate(parse_result.graph, resolve_result, type_check_result);
        if (validation_result.has_errors()) {
            std::ostringstream out;
            validation_result.diagnostics.render(out);
            error_out = "validate failed:\n" + out.str();
            return std::nullopt;
        }

        return lower_program_ir(parse_result.graph, resolve_result, type_check_result);
    }

    const auto parse_result = frontend.parse_file(file_path);
    if (parse_result.has_errors() || !parse_result.program) {
        std::ostringstream out;
        parse_result.diagnostics.render(out);
        error_out = "parse failed:\n" + out.str();
        return std::nullopt;
    }

    const Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    if (resolve_result.has_errors()) {
        std::ostringstream out;
        resolve_result.diagnostics.render(out);
        error_out = "resolve failed:\n" + out.str();
        return std::nullopt;
    }

    const TypeChecker type_checker;
    const auto type_check_result = type_checker.check(*parse_result.program, resolve_result);
    if (type_check_result.has_errors()) {
        std::ostringstream out;
        type_check_result.diagnostics.render(out);
        error_out = "typecheck failed:\n" + out.str();
        return std::nullopt;
    }

    const Validator validator;
    const auto validation_result =
        validator.validate(*parse_result.program, resolve_result, type_check_result);
    if (validation_result.has_errors()) {
        std::ostringstream out;
        validation_result.diagnostics.render(out);
        error_out = "validate failed:\n" + out.str();
        return std::nullopt;
    }

    return lower_program_ir(*parse_result.program, resolve_result, type_check_result);
}

/// Decode a conformance scenario's wire-JSON input according to the compiled
/// program's declared entry input type. Unlike the schema-free
/// `value_from_json`, this walks the IR type alongside the JSON so that Map
/// fields decode as MapValue (not nameless StructValue), Set fields as
/// SetValue (not ListValue), Option as Some/None (not bare values), Unit as
/// UnitValue (not NoneValue), and Decimal/Duration as their typed spelling
/// variants (which the P6 frame packer requires).
///
/// Both the evaluator and wasm conformance engines MUST use this (not
/// `value_from_json`) so that the two lanes start from identical typed values
/// and their observations agree byte-for-byte.
[[nodiscard]] inline std::optional<runtime::Value>
decode_conformance_input(const ir::Program &program,
                         std::string_view entry_name,
                         std::string_view input_json,
                         std::string &error_out) {
    auto parsed = ahfl::json::parse_json(input_json);
    if (!parsed.has_value() || !*parsed) {
        error_out = "failed to parse scenario input JSON";
        return std::nullopt;
    }

    const ir::ProgramIndex index(program);
    const ir::TypeRef *input_type = nullptr;
    if (const auto *wf = index.find_workflow(entry_name)) {
        input_type = &wf->input_type_ref;
    } else if (const auto *ag = index.find_agent(entry_name)) {
        input_type = &ag->input_type_ref;
    } else {
        error_out = "entry '" + std::string{entry_name} +
                     "' not found as workflow or agent in compiled program";
        return std::nullopt;
    }

    std::string decode_error;
    auto result =
        detail::decode_typed_value(*input_type, **parsed, index, decode_error);
    if (!result.has_value()) {
        error_out = "input type mismatch for entry '" + std::string{entry_name} +
                     "': " + decode_error;
        return std::nullopt;
    }
    return result;
}

} // namespace ahfl::conformance
