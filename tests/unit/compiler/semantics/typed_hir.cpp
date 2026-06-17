// ============================================================================
// Typed HIR + TypeContext unit tests.
//
// Covers two plan items:
//   * T1.2  Typed HIR output: every ExprSyntaxKind produces a TypedExpr entry
//          with the expected type/effect/purity/semantic metadata.
//   * T5.4  TypeContext lifecycle / interning:
//          - pointer identity within one context for equal types
//          - structural equality across independent contexts
//          - multi-threaded intern() of same-shape types is stable
//          - per-thread TypeContext + TypeChecker produces no cross-talk
// ============================================================================

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/ir/typed_hir_lower.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/type_context.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/typed_hir_serialization.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <future>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <variant>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

struct TypedHIRFixture {
    ahfl::Frontend frontend;

    [[nodiscard]] ahfl::TypeCheckResult check(const std::string &source) const {
        const auto parse = frontend.parse_text("typed_hir_test.ahfl", source);
        if (parse.has_errors()) {
            std::ostringstream ss;
            parse.diagnostics.render(ss);
            std::fprintf(stderr, "=== PARSE DIAGNOSTICS ===\n%s\n=== END ===\n", ss.str().c_str());
        }
        REQUIRE_FALSE(parse.has_errors());
        REQUIRE(parse.program != nullptr);

        ahfl::Resolver resolver;
        const auto resolve = resolver.resolve(*parse.program);
        if (resolve.has_errors()) {
            std::ostringstream ss;
            resolve.diagnostics.render(ss);
            std::fprintf(
                stderr, "=== RESOLVE DIAGNOSTICS ===\n%s\n=== END ===\n", ss.str().c_str());
        }
        REQUIRE_FALSE(resolve.has_errors());

        ahfl::TypeChecker checker;
        auto result = checker.check(*parse.program, resolve);
        if (result.has_errors()) {
            std::ostringstream ss;
            result.diagnostics.render(ss);
            std::fprintf(
                stderr, "=== TYPECHECK DIAGNOSTICS ===\n%s\n=== END ===\n", ss.str().c_str());
        }
        REQUIRE_FALSE(result.has_errors());
        return result;
    }

    [[nodiscard]] ahfl::TypeCheckResult
    check_project(const std::filesystem::path &root,
                  const std::vector<std::filesystem::path> &entry_files) const {
        const auto parse = frontend.parse_project(ahfl::ProjectInput{
            .entry_files = entry_files,
            .search_roots = {root},
        });
        if (parse.has_errors()) {
            std::ostringstream ss;
            parse.diagnostics.render(ss);
            std::fprintf(
                stderr, "=== PROJECT PARSE DIAGNOSTICS ===\n%s\n=== END ===\n", ss.str().c_str());
        }
        REQUIRE_FALSE(parse.has_errors());

        ahfl::Resolver resolver;
        const auto resolve = resolver.resolve(parse.graph);
        if (resolve.has_errors()) {
            std::ostringstream ss;
            resolve.diagnostics.render(ss);
            std::fprintf(
                stderr, "=== PROJECT RESOLVE DIAGNOSTICS ===\n%s\n=== END ===\n", ss.str().c_str());
        }
        REQUIRE_FALSE(resolve.has_errors());

        ahfl::TypeChecker checker;
        auto result = checker.check(parse.graph, resolve);
        if (result.has_errors()) {
            std::ostringstream ss;
            result.diagnostics.render(ss);
            std::fprintf(stderr,
                         "=== PROJECT TYPECHECK DIAGNOSTICS ===\n%s\n=== END ===\n",
                         ss.str().c_str());
        }
        REQUIRE_FALSE(result.has_errors());
        return result;
    }

    [[nodiscard]] ahfl::TypeCheckResult check_with_errors(const std::string &source) const {
        const auto parse = frontend.parse_text("typed_hir_error_test.ahfl", source);
        REQUIRE_FALSE(parse.has_errors());
        REQUIRE(parse.program != nullptr);

        ahfl::Resolver resolver;
        const auto resolve = resolver.resolve(*parse.program);
        REQUIRE_FALSE(resolve.has_errors());

        ahfl::TypeChecker checker;
        return checker.check(*parse.program, resolve);
    }
};

void write_file(const std::filesystem::path &path, const std::string &content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}

[[nodiscard]] std::filesystem::path make_temp_project(std::string_view name) {
    const auto root =
        std::filesystem::temp_directory_path() / ("ahfl_typed_hir_" + std::string(name));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

[[nodiscard]] std::size_t diagnostic_count_with_code(const ahfl::DiagnosticBag &diagnostics,
                                                     std::string_view code) {
    std::size_t count = 0;
    for (const auto &entry : diagnostics.entries()) {
        if (entry.code.has_value() && *entry.code == code) {
            ++count;
        }
    }
    return count;
}

[[nodiscard]] bool diagnostic_with_code_and_message(const ahfl::DiagnosticBag &diagnostics,
                                                    std::string_view code,
                                                    std::string_view message) {
    return std::any_of(diagnostics.entries().begin(),
                       diagnostics.entries().end(),
                       [code, message](const ahfl::Diagnostic &entry) {
                           return entry.code.has_value() && *entry.code == code &&
                                  entry.message == message;
                       });
}

// Small prefix providing Request/Response/Capability shared by many cases.
const char *kSharedPrefix = R"AHFL(
struct Request { value: String; token: Optional<String> = none; }
struct Context { value: String = ""; }
struct Response { value: String; code: Int = 200; }

capability Echo(payload: String) -> Response;
predicate Ok(x: String) -> Bool;

const kMagic: Int = 42;
const kGreeting: String = "hello";
)AHFL";

ahfl::SourceRange range_of(const std::string &haystack, const std::string &needle) {
    const auto pos = haystack.find(needle);
    REQUIRE(pos != std::string::npos);
    return ahfl::SourceRange{
        .begin_offset = pos,
        .end_offset = pos + needle.size(),
    };
}

bool has_kind(const std::vector<ahfl::TypedExpr> &exprs, ahfl::ast::ExprSyntaxKind kind) {
    return std::any_of(
        exprs.begin(), exprs.end(), [kind](const ahfl::TypedExpr &e) { return e.kind == kind; });
}

[[nodiscard]] std::string
replace_first(std::string text, const std::string &needle, const std::string &replacement) {
    const auto pos = text.find(needle);
    if (pos != std::string::npos) {
        text.replace(pos, needle.size(), replacement);
    }
    return text;
}

const ahfl::TypedExpr *find_by_range(const std::vector<ahfl::TypedExpr> &exprs,
                                     ahfl::SourceRange range) {
    for (const auto &e : exprs) {
        if (e.range.begin_offset == range.begin_offset && e.range.end_offset == range.end_offset) {
            return &e;
        }
    }
    return nullptr;
}

[[nodiscard]] std::string const_value_signature(const ahfl::ConstValue &value) {
    std::string signature = std::to_string(static_cast<unsigned>(value.kind));
    signature += "(";
    signature += value.scalar;
    signature += ")";
    if (value.symbol.has_value()) {
        signature += "#";
        signature += std::to_string(value.symbol->value);
    }
    signature += "[";
    for (std::size_t index = 0; index < value.children.size(); ++index) {
        if (index > 0) {
            signature += ",";
        }
        signature += index < value.child_names.size() ? value.child_names[index] : "";
        signature += "=";
        signature += const_value_signature(value.children[index]);
    }
    signature += "]";
    return signature;
}

} // namespace

// ============================================================================
// T1.2: Typed HIR per-ExprSyntaxKind coverage.
// ============================================================================

TEST_CASE_FIXTURE(TypedHIRFixture, "T1.2 typed HIR covers all literal kinds") {
    const std::string source = std::string(kSharedPrefix) + R"AHFL(
agent LiteralAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [Echo];
    transition Init -> Done;
}

contract for LiteralAgent {
    requires: Ok(input.value);
}

flow for LiteralAgent {
    state Init {
        // Bool / Int / Float / Decimal / String / None / Some literal.
        let t_bools: List<Bool> = [true, false];
        let t_ints: List<Int> = [1, 2, 3];
        let t_floats: List<Float> = [1.5, 2.5];
        let t_none: Optional<String> = none;
        let t_some: Optional<String> = some("world");
        // Collections.
        let t_list: List<Int> = [1, 2, 3];
        let t_set: Set<String> = set ["a", "b"];
        let t_map: Map<String, Int> = map ["x": 1, "y": 2];
        // Struct literal.
        let t_lit: Response = Response { value: "ok", code: 200 };
        // Path, call, member.
        let t_path = input.value;
        let t_call = Echo(input.value);
        // MemberAccess: postfix index-then-member triggers postfixExpr branch.
        let t_struct_list: List<Response> = [t_lit];
        let t_member = t_struct_list[0].value;
        // Unary / binary / group.
        let t_unary = -1;
        let t_binary = 1 + 2;
        let t_group = (1 + 2);
        // Index access on list.
        let t_index = t_ints[0];
        goto Done;
    }
    state Done {
        return Response { value: "done", code: 200 };
    }
}
)AHFL";

    const auto r = check(source);
    const auto &exprs = r.typed_program.expressions;
    REQUIRE_FALSE(exprs.empty());

    CHECK(has_kind(exprs, ahfl::ast::ExprSyntaxKind::BoolLiteral));
    CHECK(has_kind(exprs, ahfl::ast::ExprSyntaxKind::IntegerLiteral));
    CHECK(has_kind(exprs, ahfl::ast::ExprSyntaxKind::FloatLiteral));
    CHECK(has_kind(exprs, ahfl::ast::ExprSyntaxKind::StringLiteral));
    CHECK(has_kind(exprs, ahfl::ast::ExprSyntaxKind::NoneLiteral));
    CHECK(has_kind(exprs, ahfl::ast::ExprSyntaxKind::Some));
    CHECK(has_kind(exprs, ahfl::ast::ExprSyntaxKind::ListLiteral));
    CHECK(has_kind(exprs, ahfl::ast::ExprSyntaxKind::SetLiteral));
    CHECK(has_kind(exprs, ahfl::ast::ExprSyntaxKind::MapLiteral));
    CHECK(has_kind(exprs, ahfl::ast::ExprSyntaxKind::StructLiteral));
    CHECK(has_kind(exprs, ahfl::ast::ExprSyntaxKind::Path));
    CHECK(has_kind(exprs, ahfl::ast::ExprSyntaxKind::Call));
    CHECK(has_kind(exprs, ahfl::ast::ExprSyntaxKind::Unary));
    CHECK(has_kind(exprs, ahfl::ast::ExprSyntaxKind::Binary));
    CHECK(has_kind(exprs, ahfl::ast::ExprSyntaxKind::MemberAccess));
    CHECK(has_kind(exprs, ahfl::ast::ExprSyntaxKind::Group));
    CHECK(has_kind(exprs, ahfl::ast::ExprSyntaxKind::IndexAccess));

    // Decimal + Duration are parsed differently.
    const std::string source2 = std::string(kSharedPrefix) + R"AHFL(
agent DecimalAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [Echo];
    transition Init -> Done;
}
flow for DecimalAgent {
    state Init { goto Done; }
    state Done {
        let t_d = 3.14d;
        let t_dur = 15s;
        return Response { value: "done", code: 200 };
    }
}
)AHFL";
    const auto r2 = check(source2);
    CHECK(has_kind(r2.typed_program.expressions, ahfl::ast::ExprSyntaxKind::DecimalLiteral));
    CHECK(has_kind(r2.typed_program.expressions, ahfl::ast::ExprSyntaxKind::DurationLiteral));
}

TEST_CASE_FIXTURE(TypedHIRFixture, "T1.2 typed HIR carries type/effect/purity for literals") {
    const std::string source = R"AHFL(
struct R { v: String; }
struct Ctx { v: String = ""; }
capability C(x: String) -> R;

agent A {
    input: R;
    context: Ctx;
    output: R;
    states: [S, Done];
    initial: S;
    final: [Done];
    capabilities: [C];
    transition S -> Done;
}
flow for A {
    state S { goto Done; }
    state Done {
        let t_lit = "hello";
        let t_num = 42;
        let t_call = C(input.v);
        return R { v: "bye" };
    }
}
)AHFL";
    const auto r = check(source);
    const auto &exprs = r.typed_program.expressions;

    // String literal.
    {
        const auto *e = find_by_range(exprs, range_of(source, "\"hello\""));
        REQUIRE(e != nullptr);
        REQUIRE(e->type != nullptr);
        CHECK(e->type->holds<ahfl::types::StringT>());
        CHECK(e->is_pure);
        CHECK(e->effect == ahfl::ExprEffect::Pure);
    }
    // Integer literal.
    {
        const auto *e = find_by_range(exprs, range_of(source, "42"));
        REQUIRE(e != nullptr);
        REQUIRE(e->type != nullptr);
        CHECK(e->type->holds<ahfl::types::IntT>());
        CHECK(e->is_pure);
    }
    // Capability call: NOT pure.
    {
        const auto *e = find_by_range(exprs, range_of(source, "C(input.v)"));
        REQUIRE(e != nullptr);
        CHECK_FALSE(e->is_pure);
        CHECK(e->effect == ahfl::ExprEffect::CapabilityCall);
        CHECK(e->call_target_kind == ahfl::TypedCallTargetKind::Capability);
    }
    // Struct literal: resolved_symbol points to struct decl.
    {
        const auto *e = find_by_range(exprs, range_of(source, "R { v: \"bye\" }"));
        REQUIRE(e != nullptr);
        REQUIRE(e->type != nullptr);
        CHECK(e->kind == ahfl::ast::ExprSyntaxKind::StructLiteral);
        REQUIRE(e->resolved_symbol.has_value());
        const auto struct_info = r.environment.find_struct("R");
        REQUIRE(struct_info.has_value());
        CHECK(*e->resolved_symbol == struct_info->get().symbol);
    }
    // Struct literal's children: one StructFieldValue named "v".
    {
        const auto *e = find_by_range(exprs, range_of(source, "R { v: \"bye\" }"));
        REQUIRE(e != nullptr);
        REQUIRE_FALSE(e->children.empty());
        bool found_field = false;
        for (const auto &c : e->children) {
            if (c.role == ahfl::TypedExprChildRole::StructFieldValue && c.name == "v") {
                found_field = true;
                break;
            }
        }
        CHECK(found_field);
    }
}

TEST_CASE_FIXTURE(TypedHIRFixture, "ConstExpr typed HIR carries serializable const value trees") {
    const std::string source = R"AHFL(
module typed::const_eval;
import typed::const_eval as self;

enum Priority {
    Low,
    High,
}

struct Settings {
    label: String;
    code: Int;
    tags: List<String>;
    fallback: Optional<String>;
    priority: Priority;
}

const DefaultLabel: String = "stable";
const ForwardCode: Int = self::LaterCode + 1;
const BaseCode: Int = 40;
const LaterCode: Int = 41;
const DefaultSettings: Settings = Settings {
    label: self::DefaultLabel,
    code: self::BaseCode + 2,
    tags: ["ops", "runtime"],
    fallback: some(self::DefaultLabel),
    priority: Priority::High
};
const SettingsLabel: String = self::DefaultSettings.label;
const SecondTag: String = self::DefaultSettings.tags[1];
const LabelMatches: Bool = self::DefaultSettings.label == self::DefaultLabel;
const FloatSum: Float = 1.5 + 2.25;
const DecimalDelta: Decimal(2) = 3.50d - 1.25d;
const DecimalLess: Bool = 1.25d < 3.50d;
const CombinedLabel: String = "ops" + "runtime";
const DurationLess: Bool = 500ms < 1s;
const DurationEquivalent: Bool = 60s == 1m;
	)AHFL";

    const auto result = check(source);
    const auto &program = result.typed_program;

    const auto default_label_symbol =
        std::find_if(program.symbols.begin(), program.symbols.end(), [](const ahfl::Symbol &sym) {
            return sym.kind == ahfl::SymbolKind::Const && sym.local_name == "DefaultLabel";
        });
    REQUIRE(default_label_symbol != program.symbols.end());
    const auto priority_symbol =
        std::find_if(program.symbols.begin(), program.symbols.end(), [](const ahfl::Symbol &sym) {
            return sym.kind == ahfl::SymbolKind::Enum && sym.local_name == "Priority";
        });
    REQUIRE(priority_symbol != program.symbols.end());

    const auto settings_literal = std::find_if(
        program.expressions.begin(), program.expressions.end(), [](const ahfl::TypedExpr &expr) {
            return expr.kind == ahfl::ast::ExprSyntaxKind::StructLiteral &&
                   expr.semantic_name == "Settings" && expr.const_value.has_value();
        });
    REQUIRE(settings_literal != program.expressions.end());
    REQUIRE(settings_literal->const_value.has_value());
    const auto &settings_value = *settings_literal->const_value;
    CHECK(settings_value.kind == ahfl::ConstValueKind::Struct);
    CHECK(settings_value.scalar == "Settings");
    REQUIRE(settings_value.symbol.has_value());
    CHECK(settings_value.child_names ==
          std::vector<std::string>{"label", "code", "tags", "fallback", "priority"});
    REQUIRE(settings_value.children.size() == 5);

    CHECK(settings_value.children[0].kind == ahfl::ConstValueKind::ConstReference);
    CHECK(settings_value.children[0].scalar == "self::DefaultLabel");
    REQUIRE(settings_value.children[0].symbol.has_value());
    CHECK(*settings_value.children[0].symbol == default_label_symbol->id);
    REQUIRE(settings_value.children[0].children.size() == 1);
    CHECK(settings_value.children[0].child_names == std::vector<std::string>{"value"});
    CHECK(settings_value.children[0].children[0].kind == ahfl::ConstValueKind::String);
    CHECK(settings_value.children[0].children[0].scalar == "\"stable\"");

    CHECK(settings_value.children[1].kind == ahfl::ConstValueKind::Integer);
    CHECK(settings_value.children[1].scalar == "42");

    const auto &tags_value = settings_value.children[2];
    CHECK(tags_value.kind == ahfl::ConstValueKind::List);
    CHECK(tags_value.child_names == std::vector<std::string>{"", ""});
    REQUIRE(tags_value.children.size() == 2);
    CHECK(tags_value.children[0].kind == ahfl::ConstValueKind::String);
    CHECK(tags_value.children[0].scalar == "\"ops\"");
    CHECK(tags_value.children[1].kind == ahfl::ConstValueKind::String);
    CHECK(tags_value.children[1].scalar == "\"runtime\"");

    const auto &fallback_value = settings_value.children[3];
    CHECK(fallback_value.kind == ahfl::ConstValueKind::Some);
    CHECK(fallback_value.child_names == std::vector<std::string>{"value"});
    REQUIRE(fallback_value.children.size() == 1);
    CHECK(fallback_value.children[0].kind == ahfl::ConstValueKind::ConstReference);
    CHECK(fallback_value.children[0].scalar == "self::DefaultLabel");
    REQUIRE(fallback_value.children[0].symbol.has_value());
    CHECK(*fallback_value.children[0].symbol == default_label_symbol->id);
    REQUIRE(fallback_value.children[0].children.size() == 1);
    CHECK(fallback_value.children[0].children[0].kind == ahfl::ConstValueKind::String);
    CHECK(fallback_value.children[0].children[0].scalar == "\"stable\"");

    CHECK(settings_value.children[4].kind == ahfl::ConstValueKind::EnumVariant);
    CHECK(settings_value.children[4].scalar == "Priority::High");
    REQUIRE(settings_value.children[4].symbol.has_value());
    CHECK(*settings_value.children[4].symbol == priority_symbol->id);

    const auto *settings_label =
        find_by_range(program.expressions, range_of(source, "self::DefaultSettings.label"));
    REQUIRE(settings_label != nullptr);
    REQUIRE(settings_label->const_value.has_value());
    CHECK(settings_label->const_value->kind == ahfl::ConstValueKind::ConstReference);
    REQUIRE(settings_label->const_value->children.size() == 1);
    CHECK(settings_label->const_value->children[0].kind == ahfl::ConstValueKind::String);
    CHECK(settings_label->const_value->children[0].scalar == "\"stable\"");

    const auto second_tag = std::find_if(
        program.expressions.begin(), program.expressions.end(), [](const ahfl::TypedExpr &expr) {
            return expr.kind == ahfl::ast::ExprSyntaxKind::IndexAccess &&
                   expr.const_value.has_value() &&
                   expr.const_value->kind == ahfl::ConstValueKind::String &&
                   expr.const_value->scalar == "\"runtime\"";
        });
    REQUIRE(second_tag != program.expressions.end());

    const auto *label_matches = find_by_range(
        program.expressions, range_of(source, "self::DefaultSettings.label == self::DefaultLabel"));
    REQUIRE(label_matches != nullptr);
    REQUIRE(label_matches->const_value.has_value());
    CHECK(label_matches->const_value->kind == ahfl::ConstValueKind::Bool);
    CHECK(label_matches->const_value->scalar == "true");

    const auto *forward_code =
        find_by_range(program.expressions, range_of(source, "self::LaterCode + 1"));
    REQUIRE(forward_code != nullptr);
    REQUIRE(forward_code->const_value.has_value());
    CHECK(forward_code->const_value->kind == ahfl::ConstValueKind::Integer);
    CHECK(forward_code->const_value->scalar == "42");

    const auto *float_sum = find_by_range(program.expressions, range_of(source, "1.5 + 2.25"));
    REQUIRE(float_sum != nullptr);
    REQUIRE(float_sum->const_value.has_value());
    CHECK(float_sum->const_value->kind == ahfl::ConstValueKind::Float);
    CHECK(float_sum->const_value->scalar == "3.75");

    const auto *decimal_delta =
        find_by_range(program.expressions, range_of(source, "3.50d - 1.25d"));
    REQUIRE(decimal_delta != nullptr);
    REQUIRE(decimal_delta->const_value.has_value());
    CHECK(decimal_delta->const_value->kind == ahfl::ConstValueKind::Decimal);
    CHECK(decimal_delta->const_value->scalar == "2.25d");

    const auto *decimal_less =
        find_by_range(program.expressions, range_of(source, "1.25d < 3.50d"));
    REQUIRE(decimal_less != nullptr);
    REQUIRE(decimal_less->const_value.has_value());
    CHECK(decimal_less->const_value->kind == ahfl::ConstValueKind::Bool);
    CHECK(decimal_less->const_value->scalar == "true");

    const auto *combined_label =
        find_by_range(program.expressions, range_of(source, "\"ops\" + \"runtime\""));
    REQUIRE(combined_label != nullptr);
    REQUIRE(combined_label->const_value.has_value());
    CHECK(combined_label->const_value->kind == ahfl::ConstValueKind::String);
    CHECK(combined_label->const_value->scalar == "\"opsruntime\"");

    const auto *duration_less = find_by_range(program.expressions, range_of(source, "500ms < 1s"));
    REQUIRE(duration_less != nullptr);
    REQUIRE(duration_less->const_value.has_value());
    CHECK(duration_less->const_value->kind == ahfl::ConstValueKind::Bool);
    CHECK(duration_less->const_value->scalar == "true");

    const auto *duration_equivalent =
        find_by_range(program.expressions, range_of(source, "60s == 1m"));
    REQUIRE(duration_equivalent != nullptr);
    REQUIRE(duration_equivalent->const_value.has_value());
    CHECK(duration_equivalent->const_value->kind == ahfl::ConstValueKind::Bool);
    CHECK(duration_equivalent->const_value->scalar == "true");

    const auto *duration_seconds = find_by_range(program.expressions, range_of(source, "60s"));
    const auto *duration_minutes = find_by_range(program.expressions, range_of(source, "1m"));
    REQUIRE(duration_seconds != nullptr);
    REQUIRE(duration_minutes != nullptr);
    REQUIRE(duration_seconds->const_value.has_value());
    REQUIRE(duration_minutes->const_value.has_value());
    CHECK(duration_seconds->const_value->kind == ahfl::ConstValueKind::Duration);
    CHECK(duration_minutes->const_value->kind == ahfl::ConstValueKind::Duration);
    CHECK(duration_seconds->const_value->scalar == "60000ms");
    CHECK(duration_minutes->const_value->scalar == "60000ms");
    CHECK(const_value_signature(*duration_seconds->const_value) ==
          const_value_signature(*duration_minutes->const_value));

    const auto snapshot = ahfl::serialize_typed_program_json(program);
    auto restored = ahfl::deserialize_typed_program_json(snapshot);
    REQUIRE(restored.has_value());
    CHECK(ahfl::serialize_typed_program_json(*restored) == snapshot);
}

TEST_CASE_FIXTURE(TypedHIRFixture, "ConstExpr typed HIR normalizes Set and Map const values") {
    const std::string source = R"AHFL(
module typed::const_normalize;

const SourceList: List<String> = ["b", "a"];
const SourceSet: Set<String> = set ["b", "a"];
const CanonicalSet: Set<String> = set ["a", "b"];
const SourceMap: Map<String, Int> = map ["b": 2, "a": 1];
const CanonicalMap: Map<String, Int> = map ["a": 1, "b": 2];
)AHFL";

    const auto result = check(source);
    const auto &program = result.typed_program;

    const auto *source_list =
        find_by_range(program.expressions, range_of(source, "[\"b\", \"a\"]"));
    REQUIRE(source_list != nullptr);
    REQUIRE(source_list->const_value.has_value());
    CHECK(source_list->const_value->kind == ahfl::ConstValueKind::List);
    REQUIRE(source_list->const_value->children.size() == 2);
    CHECK(source_list->const_value->children[0].scalar == "\"b\"");
    CHECK(source_list->const_value->children[1].scalar == "\"a\"");

    const auto *source_set =
        find_by_range(program.expressions, range_of(source, "set [\"b\", \"a\"]"));
    const auto *canonical_set =
        find_by_range(program.expressions, range_of(source, "set [\"a\", \"b\"]"));
    REQUIRE(source_set != nullptr);
    REQUIRE(canonical_set != nullptr);
    REQUIRE(source_set->const_value.has_value());
    REQUIRE(canonical_set->const_value.has_value());
    CHECK(source_set->const_value->kind == ahfl::ConstValueKind::Set);
    REQUIRE(source_set->const_value->children.size() == 2);
    CHECK(source_set->const_value->children[0].scalar == "\"a\"");
    CHECK(source_set->const_value->children[1].scalar == "\"b\"");
    CHECK(const_value_signature(*source_set->const_value) ==
          const_value_signature(*canonical_set->const_value));

    const auto *source_map =
        find_by_range(program.expressions, range_of(source, "map [\"b\": 2, \"a\": 1]"));
    const auto *canonical_map =
        find_by_range(program.expressions, range_of(source, "map [\"a\": 1, \"b\": 2]"));
    REQUIRE(source_map != nullptr);
    REQUIRE(canonical_map != nullptr);
    REQUIRE(source_map->const_value.has_value());
    REQUIRE(canonical_map->const_value.has_value());
    CHECK(source_map->const_value->kind == ahfl::ConstValueKind::Map);
    REQUIRE(source_map->const_value->children.size() == 4);
    CHECK(source_map->const_value->child_names ==
          std::vector<std::string>{"key", "value", "key", "value"});
    CHECK(source_map->const_value->children[0].scalar == "\"a\"");
    CHECK(source_map->const_value->children[1].scalar == "1");
    CHECK(source_map->const_value->children[2].scalar == "\"b\"");
    CHECK(source_map->const_value->children[3].scalar == "2");
    CHECK(const_value_signature(*source_map->const_value) ==
          const_value_signature(*canonical_map->const_value));

    const auto snapshot = ahfl::serialize_typed_program_json(program);
    auto restored = ahfl::deserialize_typed_program_json(snapshot);
    REQUIRE(restored.has_value());
    CHECK(ahfl::serialize_typed_program_json(*restored) == snapshot);
}

TEST_CASE_FIXTURE(TypedHIRFixture,
                  "ConstExpr typed HIR resolves project forward const dependencies") {
    const auto root = make_temp_project("const_forward_project");
    const auto main_path = root / "app" / "main.ahfl";
    const auto defs_path = root / "lib" / "defs.ahfl";

    const std::string defs_source = R"AHFL(
module lib::defs;
import lib::defs as self;

const CrossForward: Int = self::Later + 1;
const Later: Int = 40;
)AHFL";
    const std::string main_source = R"AHFL(
module app::main;
import lib::defs as defs;

const ProjectAnswer: Int = defs::CrossForward + 1;
)AHFL";

    write_file(defs_path, defs_source);
    write_file(main_path, main_source);

    const auto result = check_project(root, {main_path});
    const auto &program = result.typed_program;
    const auto folded_project_answer = std::find_if(
        program.expressions.begin(), program.expressions.end(), [](const ahfl::TypedExpr &expr) {
            return expr.kind == ahfl::ast::ExprSyntaxKind::Binary && expr.const_value.has_value() &&
                   expr.const_value->kind == ahfl::ConstValueKind::Integer &&
                   expr.const_value->scalar == "42";
        });
    REQUIRE(folded_project_answer != program.expressions.end());

    const auto find_const_symbol = [&](std::string_view module, std::string_view name) {
        auto symbol = program.find_local_symbol(ahfl::SymbolNamespace::Consts, name, module);
        REQUIRE(symbol.has_value());
        return symbol->get();
    };
    const auto project_answer = find_const_symbol("app::main", "ProjectAnswer");
    const auto cross_forward = find_const_symbol("lib::defs", "CrossForward");
    const auto later = find_const_symbol("lib::defs", "Later");

    const auto find_edge = [&](ahfl::SymbolId source, ahfl::SymbolId target) {
        return std::find_if(program.const_dependencies.begin(),
                            program.const_dependencies.end(),
                            [source, target](const ahfl::ConstDependencyEdge &edge) {
                                return edge.source == source && edge.target == target;
                            });
    };

    REQUIRE(program.const_dependencies.size() == 2);
    const auto main_edge = find_edge(project_answer.id, cross_forward.id);
    REQUIRE(main_edge != program.const_dependencies.end());
    CHECK(main_edge->source_id == project_answer.source_id);
    CHECK(main_edge->reference_range.begin_offset ==
          range_of(main_source, "defs::CrossForward").begin_offset);
    CHECK(main_edge->reference_range.end_offset ==
          range_of(main_source, "defs::CrossForward").end_offset);

    const auto defs_edge = find_edge(cross_forward.id, later.id);
    REQUIRE(defs_edge != program.const_dependencies.end());
    CHECK(defs_edge->source_id == cross_forward.source_id);
    CHECK(defs_edge->reference_range.begin_offset ==
          range_of(defs_source, "self::Later").begin_offset);
    CHECK(defs_edge->reference_range.end_offset == range_of(defs_source, "self::Later").end_offset);

    const auto snapshot = ahfl::serialize_typed_program_json(program);
    auto restored = ahfl::deserialize_typed_program_json(snapshot);
    REQUIRE(restored.has_value());
    REQUIRE(restored->const_dependencies.size() == program.const_dependencies.size());
    CHECK(ahfl::serialize_typed_program_json(*restored) == snapshot);
}

TEST_CASE_FIXTURE(TypedHIRFixture, "ConstExpr dependency cycles report structured diagnostics") {
    const std::string source = R"AHFL(
module typed::const_cycle;
import typed::const_cycle as self;

const A: Int = self::B + 1;
const B: Int = self::A + 1;
)AHFL";

    const auto result = check_with_errors(source);
    REQUIRE(result.has_errors());
    CHECK(diagnostic_count_with_code(result.diagnostics, "typecheck.CONST_DEPENDENCY_CYCLE") == 1);
    const auto diagnostic = std::find_if(
        result.diagnostics.entries().begin(),
        result.diagnostics.entries().end(),
        [](const ahfl::Diagnostic &entry) {
            return entry.code.has_value() && *entry.code == "typecheck.CONST_DEPENDENCY_CYCLE";
        });
    REQUIRE(diagnostic != result.diagnostics.entries().end());
    CHECK(diagnostic->message ==
          ahfl::messages::typecheck::ConstDependencyCycle.format_with("typed::const_cycle::A"));
    REQUIRE(diagnostic->related.size() == 1);
    CHECK(diagnostic->related.front().message ==
          ahfl::messages::typecheck::ConstDependencyCycleParticipant.format_with());
}

TEST_CASE_FIXTURE(TypedHIRFixture,
                  "Flow assignment target diagnostics use stable code and template") {
    const std::string source = R"AHFL(
struct Request { value: String; }
struct Context { value: String = ""; }
struct Response { value: String; }
capability Echo(payload: String) -> Response;

agent Worker {
    input: Request;
    context: Context;
    output: Response;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [Echo];
    transition Init -> Done;
}

flow for Worker {
    state Init {
        input.value = "mutated";
        goto Done;
    }
    state Done {
        return Response { value: input.value };
    }
}
)AHFL";

    const auto result = check_with_errors(source);
    REQUIRE(result.has_errors());
    CHECK(diagnostic_with_code_and_message(
        result.diagnostics,
        "typecheck.INVALID_OPERATION",
        ahfl::messages::typecheck::AssignmentTargetRequiresContext.format_with()));
}

TEST_CASE_FIXTURE(TypedHIRFixture, "Unknown path root diagnostics use stable code and template") {
    const std::string source = R"AHFL(
struct Request { value: String; }
struct Context { value: String = ""; }
struct Response { value: String; }
capability Echo(payload: String) -> Response;

agent Worker {
    input: Request;
    context: Context;
    output: Response;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [Echo];
    transition Init -> Done;
}

flow for Worker {
    state Init {
        let broken = missing.value;
        goto Done;
    }
    state Done {
        return Response { value: input.value };
    }
}
)AHFL";

    const auto result = check_with_errors(source);
    REQUIRE(result.has_errors());
    CHECK(diagnostic_with_code_and_message(
        result.diagnostics,
        "typecheck.UNKNOWN_VALUE",
        ahfl::messages::typecheck::UnknownValue.format_with("missing")));
}

TEST_CASE_FIXTURE(TypedHIRFixture,
                  "Struct literal target diagnostics use stable code and template") {
    const std::string source = R"AHFL(
struct Payload { value: String; }
enum Mode { A, B }

const Broken: Payload = Mode { value: "x" };
)AHFL";

    const auto result = check_with_errors(source);
    REQUIRE(result.has_errors());
    CHECK(diagnostic_with_code_and_message(
        result.diagnostics,
        "typecheck.INVALID_STRUCT_LITERAL_TARGET",
        ahfl::messages::typecheck::StructLiteralTargetRequiresStruct.format_with("Mode")));
}

TEST_CASE_FIXTURE(TypedHIRFixture,
                  "Qualified value owner diagnostics use stable code and template") {
    const std::string source = R"AHFL(
struct Payload { value: String; }

const Broken: Payload = Payload::value;
)AHFL";

    const auto result = check_with_errors(source);
    REQUIRE(result.has_errors());
    CHECK(diagnostic_with_code_and_message(
        result.diagnostics,
        "typecheck.INVALID_QUALIFIED_VALUE",
        ahfl::messages::typecheck::QualifiedValueRequiresConstOrEnumVariant.format_with(
            "Payload::value")));
}

TEST_CASE_FIXTURE(TypedHIRFixture,
                  "Unknown enum variant diagnostics use stable code and template") {
    const std::string source = R"AHFL(
enum Mode { A, B }

const Broken: Mode = Mode::MissingVariant;
)AHFL";

    const auto result = check_with_errors(source);
    REQUIRE(result.has_errors());
    CHECK(diagnostic_with_code_and_message(
        result.diagnostics,
        "typecheck.UNKNOWN_ENUM_VARIANT",
        ahfl::messages::typecheck::UnknownEnumVariant.format_with("Mode::MissingVariant")));
}

TEST_CASE_FIXTURE(TypedHIRFixture,
                  "Schema boundary declaration diagnostics use stable code and template") {
    const std::string source = R"AHFL(
struct Request { value: String; }
struct Context { value: String = ""; }
struct Response { value: String; }

agent InvalidAgent {
    input: Int;
    context: List<String>;
    output: Optional<String>;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];
    transition Init -> Done;
}

agent Worker {
    input: Request;
    context: Context;
    output: Response;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];
    transition Init -> Done;
}

workflow InvalidWorkflow {
    input: List<String>;
    output: Optional<String>;
    node run: Worker(Request { value: "ok" });
    return: run;
}
)AHFL";

    const auto result = check_with_errors(source);
    REQUIRE(result.has_errors());
    CHECK(diagnostic_with_code_and_message(
        result.diagnostics,
        "typecheck.INVALID_AGENT_TYPE",
        ahfl::messages::typecheck::SchemaBoundaryTypeRequiresStruct.format_with("agent input")));
    CHECK(diagnostic_with_code_and_message(
        result.diagnostics,
        "typecheck.INVALID_AGENT_TYPE",
        ahfl::messages::typecheck::SchemaBoundaryTypeRequiresStruct.format_with(
            "agent context default")));
    CHECK(diagnostic_with_code_and_message(
        result.diagnostics,
        "typecheck.INVALID_AGENT_TYPE",
        ahfl::messages::typecheck::SchemaBoundaryTypeRequiresStruct.format_with("agent output")));
    CHECK(diagnostic_with_code_and_message(
        result.diagnostics,
        "typecheck.INVALID_AGENT_TYPE",
        ahfl::messages::typecheck::SchemaBoundaryTypeRequiresStruct.format_with("workflow input")));
    CHECK(diagnostic_with_code_and_message(
        result.diagnostics,
        "typecheck.INVALID_AGENT_TYPE",
        ahfl::messages::typecheck::SchemaBoundaryTypeRequiresStruct.format_with(
            "workflow output")));
}

TEST_CASE_FIXTURE(TypedHIRFixture, "Source typecheck applies covariant container variance rules") {
    const std::string source = R"AHFL(
module typed::variance;
import typed::variance as self;

const NarrowOptional: Optional<String(2, 8)> = none;
const WideOptional: Optional<String> = self::NarrowOptional;

const NarrowList: List<String(2, 8)> = [];
const WideList: List<String> = self::NarrowList;

const NarrowSet: Set<String(2, 8)> = set [];
const WideSet: Set<String> = self::NarrowSet;

const NarrowMapValue: Map<String, String(2, 8)> = map [];
const WideMapValue: Map<String, String> = self::NarrowMapValue;
)AHFL";

    const auto result = check(source);
    const auto require_const_type = [&](std::string_view name, std::string_view expected) {
        const auto symbol = result.typed_program.find_local_symbol(
            ahfl::SymbolNamespace::Consts, name, "typed::variance");
        REQUIRE(symbol.has_value());
        const auto type = result.environment.get_const_type(symbol->get().id);
        REQUIRE(type.has_value());
        CHECK(type->get().describe() == expected);
    };

    require_const_type("WideOptional", "Optional<String>");
    require_const_type("WideList", "List<String>");
    require_const_type("WideSet", "Set<String>");
    require_const_type("WideMapValue", "Map<String, String>");
}

TEST_CASE_FIXTURE(TypedHIRFixture, "Source typecheck keeps map keys invariant") {
    const std::string source = R"AHFL(
module typed::variance;
import typed::variance as self;

const NarrowMapKey: Map<String(2, 8), Int> = map [];
const RejectedMapKey: Map<String, Int> = self::NarrowMapKey;
)AHFL";

    const auto result = check_with_errors(source);
    REQUIRE(result.has_errors());
    CHECK(diagnostic_with_code_and_message(
        result.diagnostics,
        "typecheck.TYPE_MISMATCH",
        ahfl::messages::typecheck::TypeMismatch.format_with(
            "const initializer", "Map<String, Int>", "Map<String(2, 8), Int>")));
}

TEST_CASE_FIXTURE(TypedHIRFixture, "TypeEnvironment nominal lookup is SymbolId-first") {
    const std::string source = R"AHFL(
struct Request {
    value: String;
}

enum Priority {
    Low,
    High,
}
)AHFL";

    const auto result = check(source);

    const auto request_info = result.environment.find_struct("Request");
    REQUIRE(request_info.has_value());
    const auto priority_info = result.environment.find_enum("Priority");
    REQUIRE(priority_info.has_value());

    auto &types = ahfl::TypeContext::global();
    const auto renamed_request = types.struct_type("renamed::Request", request_info->get().symbol);
    const auto renamed_priority = types.enum_type("renamed::Priority", priority_info->get().symbol);

    const auto resolved_request = result.environment.get_struct(*renamed_request);
    REQUIRE(resolved_request.has_value());
    CHECK(resolved_request->get().symbol == request_info->get().symbol);
    CHECK(resolved_request->get().canonical_name == "Request");

    const auto resolved_priority = result.environment.get_enum(*renamed_priority);
    REQUIRE(resolved_priority.has_value());
    CHECK(resolved_priority->get().symbol == priority_info->get().symbol);
    CHECK(resolved_priority->get().canonical_name == "Priority");
}

TEST_CASE_FIXTURE(TypedHIRFixture, "T1.2 TypedDecl records all declaration kinds") {
    const std::string source = R"AHFL(
struct S { x: Int = 0; }
enum E { A, B }
type T = String;
const c: Int = 1;
capability Cap(in: String) -> S;
predicate Pred(in: Int) -> Bool;

agent X {
    input: S; context: S; output: S;
    states: [A, Done]; initial: A; final: [Done];
    capabilities: [Cap];
    transition A -> Done;
}
contract for X { requires: true; }
flow for X {
    state A { goto Done; }
    state Done { return S { x: input.x }; }
}
workflow W {
    input: S; output: S;
    node run_agent: X(input);
    return: run_agent;
}
)AHFL";
    const auto r = check(source);
    const auto &decls = r.typed_program.declarations;
    REQUIRE_FALSE(decls.empty());

    using K = ahfl::ast::NodeKind;
    std::unordered_set<K> seen;
    for (const auto &d : decls)
        seen.insert(d.kind);

    // Nominal / independent declarations always recorded.
    CHECK(seen.contains(K::StructDecl));
    CHECK(seen.contains(K::EnumDecl));
    CHECK(seen.contains(K::TypeAliasDecl));
    CHECK(seen.contains(K::ConstDecl));
    CHECK(seen.contains(K::CapabilityDecl));
    CHECK(seen.contains(K::PredicateDecl));
    CHECK(seen.contains(K::AgentDecl));
    CHECK(seen.contains(K::WorkflowDecl));

    // T1.5: ContractDecl and FlowDecl are now recorded as standalone
    // TypedDecl entries with associated_agent_symbol pointing to the
    // owning agent.
    CHECK(seen.contains(K::ContractDecl));
    CHECK(seen.contains(K::FlowDecl));

    // Locate the agent and contract/flow records by kind and verify the
    // associated agent symbol wiring.
    const auto agent_it = std::find_if(
        decls.begin(), decls.end(), [](const auto &d) { return d.kind == K::AgentDecl; });
    REQUIRE(agent_it != decls.end());
    const auto agent_symbol = agent_it->symbol;

    const auto contract_it = std::find_if(
        decls.begin(), decls.end(), [](const auto &d) { return d.kind == K::ContractDecl; });
    REQUIRE(contract_it != decls.end());
    REQUIRE(contract_it->associated_agent_symbol.has_value());
    CHECK(*contract_it->associated_agent_symbol == agent_symbol);

    const auto flow_it = std::find_if(
        decls.begin(), decls.end(), [](const auto &d) { return d.kind == K::FlowDecl; });
    REQUIRE(flow_it != decls.end());
    REQUIRE(flow_it->associated_agent_symbol.has_value());
    CHECK(*flow_it->associated_agent_symbol == agent_symbol);
}

TEST_CASE_FIXTURE(TypedHIRFixture,
                  "T1.5 TypedProgram records ContractDecl with agent association") {
    const std::string source = R"AHFL(
struct Req { v: String; }
struct Resp { v: String; }
struct Ctx { v: String = ""; }
capability Nop(x: String) -> Resp;
predicate Safe(s: String) -> Bool;

agent Guard {
    input: Req; context: Ctx; output: Resp;
    states: [Init, Done]; initial: Init; final: [Done];
    capabilities: [Nop];
    transition Init -> Done;
}
contract for Guard {
    requires: Safe(input.v);
    ensures:  true;
}
flow for Guard {
    state Init { goto Done; }
    state Done { return Resp { v: input.v }; }
}
)AHFL";
    const auto r = check(source);
    const auto &decls = r.typed_program.declarations;

    using K = ahfl::ast::NodeKind;

    // Locate the Guard agent SymbolId.
    const auto agent_it = std::find_if(
        decls.begin(), decls.end(), [](const auto &d) { return d.kind == K::AgentDecl; });
    REQUIRE(agent_it != decls.end());
    const auto agent_symbol = agent_it->symbol;

    // Find ContractDecl entries. There must be exactly one for `Guard`.
    std::size_t contract_count = 0;
    for (const auto &d : decls) {
        if (d.kind != K::ContractDecl)
            continue;
        ++contract_count;
        REQUIRE(d.associated_agent_symbol.has_value());
        CHECK(*d.associated_agent_symbol == agent_symbol);
        // Range should be non-empty.
        CHECK(d.range.end_offset > d.range.begin_offset);
    }
    CHECK(contract_count == 1);
}

TEST_CASE_FIXTURE(TypedHIRFixture, "T1.5 TypedProgram records FlowDecl with agent association") {
    const std::string source = R"AHFL(
struct Req { v: String; }
struct Resp { v: String; }
struct Ctx { v: String = ""; }
capability Work(x: String) -> Resp;

agent Runner {
    input: Req; context: Ctx; output: Resp;
    states: [Init, Run, Done]; initial: Init; final: [Done];
    capabilities: [Work];
    transition Init -> Run;
    transition Run -> Done;
}
contract for Runner { requires: true; }
flow for Runner {
    state Init { goto Run; }
    state Run  { let r = Work(input.v); goto Done; }
    state Done { return Resp { v: input.v }; }
}
)AHFL";
    const auto r = check(source);
    const auto &decls = r.typed_program.declarations;

    using K = ahfl::ast::NodeKind;

    // Locate the Runner agent SymbolId.
    const auto agent_it = std::find_if(
        decls.begin(), decls.end(), [](const auto &d) { return d.kind == K::AgentDecl; });
    REQUIRE(agent_it != decls.end());
    const auto agent_symbol = agent_it->symbol;

    // Find FlowDecl entries.
    std::size_t flow_count = 0;
    for (const auto &d : decls) {
        if (d.kind != K::FlowDecl)
            continue;
        ++flow_count;
        REQUIRE(d.associated_agent_symbol.has_value());
        CHECK(*d.associated_agent_symbol == agent_symbol);
        CHECK(d.range.end_offset > d.range.begin_offset);
    }
    CHECK(flow_count == 1);
}

TEST_CASE_FIXTURE(TypedHIRFixture, "T1.2 lowering produces equivalent IR via typed HIR entry") {
    const std::string source = R"AHFL(
struct R { v: String; }
struct Ctx { v: String = ""; }
capability C(x: String) -> R;

agent A {
    input: R; context: Ctx; output: R;
    states: [Init, Done]; initial: Init; final: [Done];
    capabilities: [C];
    transition Init -> Done;
}
flow for A {
    state Init { let r = C(input.v); goto Done; }
    state Done { return R { v: input.v }; }
}
)AHFL";
    const auto parse = frontend.parse_text("typed_hir_lower_equiv.ahfl", source);
    REQUIRE_FALSE(parse.has_errors());
    ahfl::Resolver resolver;
    const auto resolve = resolver.resolve(*parse.program);
    REQUIRE_FALSE(resolve.has_errors());
    ahfl::TypeChecker checker;
    const auto tc = checker.check(*parse.program, resolve);
    REQUIRE_FALSE(tc.has_errors());

    const auto via_ast = ahfl::lower_program_ir(*parse.program, resolve, tc);
    const auto via_typed = ahfl::lower_typed_program(tc.typed_program, *parse.program);
    const auto via_detached = ahfl::lower_typed_program(tc.typed_program);

    // Declaration count must match.
    CHECK(via_ast.declarations.size() == via_typed.declarations.size());
    CHECK(via_ast.declarations.size() == via_detached.declarations.size());
    REQUIRE_FALSE(via_ast.declarations.empty());

    // Full structural equivalence via canonical JSON rendering.
    std::ostringstream ast_json, typed_json, detached_json;
    ahfl::print_program_ir_json(via_ast, ast_json);
    ahfl::print_program_ir_json(via_typed, typed_json);
    ahfl::print_program_ir_json(via_detached, detached_json);
    const std::string ast_fp = ast_json.str();
    const std::string typed_fp = typed_json.str();
    const std::string detached_fp = detached_json.str();

    REQUIRE(ast_fp.size() > 0);
    REQUIRE(typed_fp.size() > 0);
    REQUIRE(detached_fp.size() > 0);
    CHECK(ast_fp == typed_fp);
    CHECK(ast_fp == detached_fp);
}

TEST_CASE_FIXTURE(TypedHIRFixture,
                  "B3 TypedProgram JSON snapshot round-trips and rebuilds lookup indices") {
    const std::string source = R"AHFL(
module typed::snapshot;

type Label = String;
const DefaultCode: Int = 200;

struct Req { v: String; token: Optional<String> = none; }
struct Ctx { v: String = ""; count: Int = 0; }
struct Resp { v: String; code: Int = 200; }
enum Priority { Low, High }

capability Work(x: String) -> Resp;
predicate Safe(s: String) -> Bool;

agent Worker {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [Work];
    transition Init -> Done;
}

contract for Worker {
    requires: Safe(input.v);
    ensures: true;
}

flow for Worker {
    state Init {
        let greeting: String = input.v;
        ctx.v = greeting;
        assert(Safe(ctx.v));
        goto Done;
    }
    state Done {
        return Resp { v: ctx.v, code: 200 };
    }
}

workflow RunWorker {
    input: Req; output: Resp;
    node run: Worker(input);
    safety: always completed(run);
    return: run;
}
)AHFL";

    const auto tc = check(source);
    const auto snapshot = ahfl::serialize_typed_program_json(tc.typed_program);
    REQUIRE(snapshot.find("AHFL_TYPED_HIR_V1") != std::string::npos);
    REQUIRE(snapshot.find(std::string(ahfl::kConstValueArtifactSchemaVersion)) !=
            std::string::npos);

    auto restored = ahfl::deserialize_typed_program_json(snapshot);
    REQUIRE(restored.has_value());
    CHECK(ahfl::serialize_typed_program_json(*restored) == snapshot);

    auto const_schema_mismatch = ahfl::deserialize_typed_program_json(
        replace_first(snapshot,
                      std::string(ahfl::kConstValueArtifactSchemaVersion),
                      "AHFL_CONST_VALUE_ARTIFACT_V0"));
    CHECK_FALSE(const_schema_mismatch.has_value());

    CHECK(restored->declarations.size() == tc.typed_program.declarations.size());
    CHECK(restored->expressions.size() == tc.typed_program.expressions.size());
    CHECK(restored->blocks.size() == tc.typed_program.blocks.size());
    CHECK(restored->statements.size() == tc.typed_program.statements.size());
    CHECK(restored->temporal_exprs.size() == tc.typed_program.temporal_exprs.size());
    CHECK(restored->symbols.size() == tc.typed_program.symbols.size());
    CHECK(restored->references.size() == tc.typed_program.references.size());
    CHECK(restored->const_dependencies.size() == tc.typed_program.const_dependencies.size());

    const auto module_it =
        std::find_if(restored->declarations.begin(),
                     restored->declarations.end(),
                     [](const ahfl::TypedDecl &decl) {
                         return decl.kind == ahfl::ast::NodeKind::ModuleDecl &&
                                std::holds_alternative<ahfl::ModuleDeclInfo>(decl.payload);
                     });
    REQUIRE(module_it != restored->declarations.end());
    const auto *module_info = std::get_if<ahfl::ModuleDeclInfo>(&module_it->payload);
    REQUIRE(module_info != nullptr);
    CHECK(module_info->name == "typed::snapshot");

    const auto const_it =
        std::find_if(restored->declarations.begin(),
                     restored->declarations.end(),
                     [](const ahfl::TypedDecl &decl) {
                         return decl.kind == ahfl::ast::NodeKind::ConstDecl &&
                                std::holds_alternative<ahfl::ConstDeclInfo>(decl.payload);
                     });
    REQUIRE(const_it != restored->declarations.end());
    const auto *const_info = std::get_if<ahfl::ConstDeclInfo>(&const_it->payload);
    REQUIRE(const_info != nullptr);
    CHECK(const_info->local_name == "DefaultCode");
    REQUIRE(const_info->type != nullptr);
    CHECK(const_info->type->describe() == "Int");

    const auto alias_it =
        std::find_if(restored->declarations.begin(),
                     restored->declarations.end(),
                     [](const ahfl::TypedDecl &decl) {
                         return decl.kind == ahfl::ast::NodeKind::TypeAliasDecl &&
                                std::holds_alternative<ahfl::TypeAliasDeclInfo>(decl.payload);
                     });
    REQUIRE(alias_it != restored->declarations.end());
    const auto *alias_info = std::get_if<ahfl::TypeAliasDeclInfo>(&alias_it->payload);
    REQUIRE(alias_info != nullptr);
    CHECK(alias_info->local_name == "Label");
    REQUIRE(alias_info->aliased_type != nullptr);
    CHECK(alias_info->aliased_type->describe() == "String");

    auto worker_symbol =
        restored->find_local_symbol(ahfl::SymbolNamespace::Agents, "Worker", "typed::snapshot");
    REQUIRE(worker_symbol.has_value());
    CHECK(worker_symbol->get().local_name == "Worker");

    REQUIRE_FALSE(restored->references.empty());
    const auto &reference = restored->references.front();
    auto restored_reference =
        restored->find_reference(reference.kind, reference.range, reference.source_id);
    REQUIRE(restored_reference.has_value());
    CHECK(restored_reference->get().target == reference.target);

    const auto expr_it =
        std::find_if(restored->expressions.begin(),
                     restored->expressions.end(),
                     [](const ahfl::TypedExpr &expr) {
                         return expr.node_id != 0 && expr.type != nullptr && !expr.children.empty();
                     });
    REQUIRE(expr_it != restored->expressions.end());
    const auto *indexed_expr = restored->find_expr(expr_it->node_id, expr_it->source_id);
    REQUIRE(indexed_expr != nullptr);
    CHECK(indexed_expr->type->describe() == expr_it->type->describe());
    CHECK(ahfl::resolve_child(*restored, expr_it->children.front()) != nullptr);

    const auto struct_it =
        std::find_if(restored->declarations.begin(),
                     restored->declarations.end(),
                     [](const ahfl::TypedDecl &decl) {
                         return decl.kind == ahfl::ast::NodeKind::StructDecl &&
                                std::holds_alternative<ahfl::StructTypeInfo>(decl.payload);
                     });
    REQUIRE(struct_it != restored->declarations.end());
    const auto *struct_info = std::get_if<ahfl::StructTypeInfo>(&struct_it->payload);
    REQUIRE(struct_info != nullptr);
    auto field = struct_info->find_field("v");
    REQUIRE(field.has_value());
    CHECK(field->get().type != nullptr);
    auto defaulted_field = struct_info->find_field("token");
    REQUIRE(defaulted_field.has_value());
    CHECK(defaulted_field->get().has_default);
    CHECK(defaulted_field->get().default_value_range.end_offset >
          defaulted_field->get().default_value_range.begin_offset);

    const auto contract_it =
        std::find_if(restored->declarations.begin(),
                     restored->declarations.end(),
                     [](const ahfl::TypedDecl &decl) {
                         return decl.kind == ahfl::ast::NodeKind::ContractDecl &&
                                std::holds_alternative<ahfl::ContractTypeInfo>(decl.payload);
                     });
    REQUIRE(contract_it != restored->declarations.end());
    const auto *contract_info = std::get_if<ahfl::ContractTypeInfo>(&contract_it->payload);
    REQUIRE(contract_info != nullptr);
    CHECK(contract_info->target_name == "Worker");
    CHECK(contract_info->clauses.size() == 2);

    const auto flow_it =
        std::find_if(restored->declarations.begin(),
                     restored->declarations.end(),
                     [](const ahfl::TypedDecl &decl) {
                         return decl.kind == ahfl::ast::NodeKind::FlowDecl &&
                                std::holds_alternative<ahfl::FlowTypeInfo>(decl.payload);
                     });
    REQUIRE(flow_it != restored->declarations.end());
    const auto *flow_info = std::get_if<ahfl::FlowTypeInfo>(&flow_it->payload);
    REQUIRE(flow_info != nullptr);
    CHECK(flow_info->target_name == "Worker");
    CHECK(flow_info->state_handlers.size() == 2);
}

TEST_CASE_FIXTURE(TypedHIRFixture,
                  "B3 TypedProgram cache envelope validates metadata before reuse") {
    const std::string source = R"AHFL(
module typed::cache;

struct Req { v: String; }
struct Ctx { v: String = ""; }
struct Resp { v: String; }

agent Worker {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for Worker {
    state Done {
        return Resp { v: input.v };
    }
}
)AHFL";

    const auto tc = check(source);
    const ahfl::TypedProgramCacheMetadata metadata{
        .schema_version = std::string(ahfl::kTypedProgramCacheSchemaVersion),
        .source_graph_revision = "graph-rev-1",
        .source_content_hash = "content-hash-1",
        .resolver_snapshot_version = "resolver-v1",
    };

    const auto cache = ahfl::serialize_typed_program_cache_json(tc.typed_program, metadata);
    REQUIRE(cache.find("AHFL_TYPED_HIR_CACHE_V1") != std::string::npos);
    REQUIRE(cache.find("graph-rev-1") != std::string::npos);
    REQUIRE(cache.find("content-hash-1") != std::string::npos);
    REQUIRE(cache.find("resolver-v1") != std::string::npos);

    auto hit = ahfl::load_typed_program_cache_json(cache, metadata);
    REQUIRE(hit.status == ahfl::TypedProgramCacheLoadStatus::Hit);
    REQUIRE(hit.program.has_value());
    CHECK(ahfl::serialize_typed_program_json(*hit.program) ==
          ahfl::serialize_typed_program_json(tc.typed_program));

    auto schema_mismatch = ahfl::load_typed_program_cache_json(
        replace_first(cache, "AHFL_TYPED_HIR_CACHE_V1", "AHFL_TYPED_HIR_CACHE_V0"), metadata);
    CHECK(schema_mismatch.status == ahfl::TypedProgramCacheLoadStatus::SchemaMismatch);
    CHECK_FALSE(schema_mismatch.program.has_value());

    auto source_mismatch = metadata;
    source_mismatch.source_graph_revision = "graph-rev-2";
    CHECK(ahfl::load_typed_program_cache_json(cache, source_mismatch).status ==
          ahfl::TypedProgramCacheLoadStatus::SourceRevisionMismatch);

    auto content_mismatch = metadata;
    content_mismatch.source_content_hash = "content-hash-2";
    CHECK(ahfl::load_typed_program_cache_json(cache, content_mismatch).status ==
          ahfl::TypedProgramCacheLoadStatus::SourceContentHashMismatch);

    auto resolver_mismatch = metadata;
    resolver_mismatch.resolver_snapshot_version = "resolver-v2";
    CHECK(ahfl::load_typed_program_cache_json(cache, resolver_mismatch).status ==
          ahfl::TypedProgramCacheLoadStatus::ResolverSnapshotMismatch);

    auto invalid = ahfl::load_typed_program_cache_json("{", metadata);
    CHECK(invalid.status == ahfl::TypedProgramCacheLoadStatus::InvalidPayload);
    CHECK_FALSE(invalid.program.has_value());
}

// ============================================================================
// T5.4: TypeContext interning + isolation + concurrency.
// ============================================================================

TEST_CASE("T5.4 same-shape types get identical pointers in one context") {
    auto &tc = ahfl::TypeContext::global();

    CHECK(tc.make(ahfl::TypeKind::Int) == tc.make(ahfl::TypeKind::Int));
    CHECK(tc.string() == tc.string());
    CHECK(tc.decimal(2) == tc.decimal(2));
    CHECK(tc.bounded_string(0, 8) == tc.bounded_string(0, 8));

    CHECK(tc.struct_type("pkg::Foo", ahfl::SymbolId{7}) ==
          tc.struct_type("pkg::Foo", ahfl::SymbolId{7}));
    CHECK(tc.enum_type("pkg::Bar", ahfl::SymbolId{9}) ==
          tc.enum_type("pkg::Bar", ahfl::SymbolId{9}));

    const auto s = tc.string();
    CHECK(tc.optional(s) == tc.optional(s));
    CHECK(tc.list(s) == tc.list(s));
    CHECK(tc.set(s) == tc.set(s));
    CHECK(tc.map(s, tc.make(ahfl::TypeKind::Int)) == tc.map(s, tc.make(ahfl::TypeKind::Int)));
}

TEST_CASE("T5.4 two independent contexts: different pointers, structural eq") {
    // Two separate TypeContext objects: same-shape types must compare equal
    // structurally but have distinct pointer identities.
    ahfl::TypeContext left;
    ahfl::TypeContext right;
    ahfl::TypeRelationContext ctx;

    const auto *ls = left.string();
    const auto *rs = right.string();
    REQUIRE(ls != nullptr);
    REQUIRE(rs != nullptr);
    CHECK(static_cast<const void *>(ls) != static_cast<const void *>(rs));
    CHECK(ctx.equivalent(*ls, *rs));

    const auto *lo = left.optional(left.make(ahfl::TypeKind::Int));
    const auto *ro = right.optional(right.make(ahfl::TypeKind::Int));
    CHECK(static_cast<const void *>(lo) != static_cast<const void *>(ro));
    CHECK(ctx.equivalent(*lo, *ro));

    // Same name + symbol value across contexts: equivalent.
    const auto *lst = left.struct_type("a::B", ahfl::SymbolId{3});
    const auto *rst = right.struct_type("a::B", ahfl::SymbolId{3});
    CHECK(ctx.equivalent(*lst, *rst));

    // Different names (no nominal symbols): NOT equivalent by structural name.
    const auto *l2 = left.struct_type("a::X");
    const auto *r2 = right.struct_type("a::Y");
    CHECK_FALSE(ctx.equivalent(*l2, *r2));

    // When both have a nominal SymbolId with the same numeric value AND
    // matching canonical name, equivalence holds (symbol-id dominates).
    // Cross-context numeric SymbolIds are not generally comparable, so
    // the practical rule is: same-name + same-symbol-value -> equivalent.
}

TEST_CASE("T5.4 multi-thread intern of same-shape is stable and crash-free") {
    constexpr int kThreads = 8;
    constexpr int kIters = 200;
    std::atomic<int> errors{0};

    auto worker = [&](int /*tid*/) {
        for (int i = 0; i < kIters; ++i) {
            auto &tc = ahfl::TypeContext::global();
            const auto *a = tc.string();
            const auto *b = tc.string();
            if (a != b || a == nullptr) {
                errors.fetch_add(1);
                return;
            }

            const auto *c =
                tc.map(tc.list(tc.optional(tc.make(ahfl::TypeKind::Int))), tc.decimal(4));
            const auto *d =
                tc.map(tc.list(tc.optional(tc.make(ahfl::TypeKind::Int))), tc.decimal(4));
            if (c != d || c == nullptr) {
                errors.fetch_add(1);
                return;
            }
        }
    };

    std::vector<std::future<void>> futures;
    futures.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        futures.push_back(std::async(std::launch::async, worker, t));
    }
    for (auto &f : futures)
        f.get();

    CHECK(errors.load() == 0);
}

TEST_CASE("T5.4 per-thread independent TypeContext + TypeChecker has no cross-talk") {
    const std::string source = R"AHFL(
struct Req { v: String; }
struct Resp { v: String; }
struct Ctx { v: String = ""; }
capability Echo(x: String) -> Resp;
agent A {
    input: Req; context: Ctx; output: Resp;
    states: [Done]; initial: Done; final: [Done];
    capabilities: [Echo];
}
flow for A {
    state Done { return Resp { v: input.v }; }
}
)AHFL";

    constexpr int kThreads = 6;
    auto worker = [&](int tid) -> std::string {
        ahfl::Frontend local_frontend;
        const auto parse = local_frontend.parse_text("per_thread.ahfl", source);
        if (parse.has_errors() || parse.program == nullptr)
            return "PARSE_ERROR";

        ahfl::Resolver resolver;
        const auto resolve = resolver.resolve(*parse.program);
        if (resolve.has_errors())
            return "RESOLVE_ERROR";

        ahfl::TypeContext local_types;
        ahfl::TypeChecker checker;
        auto tc = checker.check(*parse.program, resolve, local_types);
        if (tc.has_errors())
            return "ERROR";
        std::vector<std::string> names;
        for (const auto &[id, s] : tc.environment.structs())
            (void)id, names.push_back(s.canonical_name);
        for (const auto &[id, c] : tc.environment.capabilities())
            (void)id, names.push_back(c.canonical_name);
        std::sort(names.begin(), names.end());
        std::string fp;
        for (auto &n : names)
            fp += n + "|";
        // Sanity: local string type must not alias global string type.
        auto &global = ahfl::TypeContext::global();
        if (static_cast<const void *>(local_types.string()) ==
            static_cast<const void *>(global.string())) {
            fp += "__SAME_PTR__tid=" + std::to_string(tid);
        }
        return fp;
    };

    std::vector<std::future<std::string>> futures;
    futures.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        futures.push_back(std::async(std::launch::async, worker, t));
    }

    std::vector<std::string> results;
    results.reserve(kThreads);
    for (auto &f : futures)
        results.push_back(f.get());
    REQUIRE(results.size() == kThreads);

    for (int t = 1; t < kThreads; ++t) {
        CHECK(results[t] == results[0]);
    }
    for (int t = 0; t < kThreads; ++t) {
        INFO("worker " << t << " fingerprint: " << results[t]);
        CHECK(results[t].find("__SAME_PTR__") == std::string::npos);
        CHECK(results[t].find("ERROR") == std::string::npos);
    }
}

// ============================================================================
// T1.4: Declaration iteration walks TypedProgram.declarations
// (not the raw AST decl list) and preserves T1.2 equivalence.
// ============================================================================

TEST_CASE_FIXTURE(TypedHIRFixture, "T1.4 decl iteration walks typed tree and covers all kinds") {
    // Source with multiple agents, multiple structs, a contract, a flow, an
    // enum, a type alias, a const, a capability, a predicate, and a workflow:
    // exercises the full TypedDecl kind set so we can assert coverage.
    const std::string source = R"AHFL(
struct Req { v: String; }
struct Ctx { v: String = ""; }
struct Resp { v: String; code: Int = 200; }

enum Color { Red, Green, Blue }
type Tag = String;

const kPreamble: String = "ok:";
const kLimit: Int = 100;

capability Echo(payload: String) -> Resp;
capability Validate(tag: String) -> Bool;

predicate Safe(s: String) -> Bool;
predicate Positive(n: Int) -> Bool;

agent Worker {
    input: Req; context: Ctx; output: Resp;
    states: [Init, Run, Done]; initial: Init; final: [Done];
    capabilities: [Echo];
    transition Init -> Run;
    transition Run -> Done;
}

agent Inspector {
    input: Req; context: Ctx; output: Resp;
    states: [Start, End]; initial: Start; final: [End];
    capabilities: [Validate];
    transition Start -> End;
}

contract for Worker {
    requires: Safe(input.v);
    ensures:  true;
}

contract for Inspector {
    requires: true;
}

flow for Worker {
    state Init { goto Run; }
    state Run  { let r = Echo(input.v); goto Done; }
    state Done { return Resp { v: input.v, code: 200 }; }
}

flow for Inspector {
    state Start { goto End; }
    state End   { return Resp { v: "checked", code: 200 }; }
}

workflow DoJob {
    input: Req; output: Resp;
    node w: Worker(input);
    node i: Inspector(input);
    return: w;
}
)AHFL";
    const auto parse = frontend.parse_text("t14_iter.ahfl", source);
    REQUIRE_FALSE(parse.has_errors());
    ahfl::Resolver resolver;
    const auto resolve = resolver.resolve(*parse.program);
    REQUIRE_FALSE(resolve.has_errors());
    ahfl::TypeChecker checker;
    const auto tc = checker.check(*parse.program, resolve);
    REQUIRE_FALSE(tc.has_errors());

    const auto &decls = tc.typed_program.declarations;
    using K = ahfl::ast::NodeKind;
    std::unordered_set<K> seen;
    for (const auto &d : decls)
        seen.insert(d.kind);

    // Kind coverage: all nominal kinds + contract + flow.
    CHECK(seen.contains(K::StructDecl));
    CHECK(seen.contains(K::EnumDecl));
    CHECK(seen.contains(K::TypeAliasDecl));
    CHECK(seen.contains(K::ConstDecl));
    CHECK(seen.contains(K::CapabilityDecl));
    CHECK(seen.contains(K::PredicateDecl));
    CHECK(seen.contains(K::AgentDecl));
    CHECK(seen.contains(K::ContractDecl));
    CHECK(seen.contains(K::FlowDecl));
    CHECK(seen.contains(K::WorkflowDecl));

    // Count multiple structs / agents / contracts / flows / consts / caps / preds
    // to confirm the flat list accumulates them and not just one-per-kind.
    std::size_t struct_count = 0, agent_count = 0, contract_count = 0;
    std::size_t flow_count = 0, const_count = 0, cap_count = 0, pred_count = 0;
    for (const auto &d : decls) {
        if (d.kind == K::StructDecl)
            ++struct_count;
        if (d.kind == K::AgentDecl)
            ++agent_count;
        if (d.kind == K::ContractDecl)
            ++contract_count;
        if (d.kind == K::FlowDecl)
            ++flow_count;
        if (d.kind == K::ConstDecl)
            ++const_count;
        if (d.kind == K::CapabilityDecl)
            ++cap_count;
        if (d.kind == K::PredicateDecl)
            ++pred_count;
    }
    CHECK(struct_count == 3);
    CHECK(agent_count == 2);
    CHECK(contract_count == 2);
    CHECK(flow_count == 2);
    CHECK(const_count == 2);
    CHECK(cap_count == 2);
    CHECK(pred_count == 2);

    // Single-program mode: source_id on each TypedDecl must be empty.
    for (const auto &d : decls)
        CHECK_FALSE(d.source_id.has_value());

    // T1.2 equivalence: typed-tree lowering and AST-tree lowering must
    // produce byte-identical IR even when the declaration iteration path
    // changed to walk TypedProgram.declarations.
    const auto via_ast = ahfl::lower_program_ir(*parse.program, resolve, tc);
    const auto via_typed = ahfl::lower_typed_program(tc.typed_program, *parse.program);
    const auto via_detached = ahfl::lower_typed_program(tc.typed_program);

    CHECK(via_ast.declarations.size() == via_typed.declarations.size());
    CHECK(via_ast.declarations.size() == via_detached.declarations.size());
    REQUIRE_FALSE(via_ast.declarations.empty());

    std::ostringstream ast_json, typed_json, detached_json;
    ahfl::print_program_ir_json(via_ast, ast_json);
    ahfl::print_program_ir_json(via_typed, typed_json);
    ahfl::print_program_ir_json(via_detached, detached_json);
    const std::string ast_fp = ast_json.str();
    const std::string typed_fp = typed_json.str();
    const std::string detached_fp = detached_json.str();

    REQUIRE_FALSE(ast_fp.empty());
    REQUIRE_FALSE(typed_fp.empty());
    REQUIRE_FALSE(detached_fp.empty());
    CHECK(ast_fp == typed_fp);
    CHECK(ast_fp == detached_fp);
}

// ============================================================================
// T1.6 P3: TypedProgram.statements coverage parity for flow body +
//          TypedStatement children resolve to valid TypedExpr records.
// ============================================================================

// Recursively count every `StatementSyntax` reachable from a BlockSyntax,
// descending into if/then/else sub-blocks. The BlockSyntax itself is visited
// exactly once per call.
static std::size_t count_ast_statements(const ahfl::ast::BlockSyntax &block) {
    std::size_t count = 0;
    for (const auto &stmt : block.statements) {
        ++count;
        // Descend into if sub-blocks.
        if (stmt->kind == ahfl::ast::StatementSyntaxKind::If) {
            if (stmt->if_stmt && stmt->if_stmt->then_block) {
                count += count_ast_statements(*stmt->if_stmt->then_block);
            }
            if (stmt->if_stmt && stmt->if_stmt->else_block) {
                count += count_ast_statements(*stmt->if_stmt->else_block);
            }
        }
    }
    return count;
}

// Walk a Program, collect every FlowDecl, then within each FlowDecl walk
// every StateHandlerSyntax body and recursively count all statements.
static std::size_t total_flow_statement_count(const ahfl::ast::Program &program) {
    std::size_t total = 0;
    for (const auto &decl : program.declarations) {
        if (decl->kind != ahfl::ast::NodeKind::FlowDecl)
            continue;
        const auto *flow = static_cast<const ahfl::ast::FlowDecl *>(decl.get());
        for (const auto &handler : flow->state_handlers) {
            if (handler->body) {
                total += count_ast_statements(*handler->body);
            }
        }
    }
    return total;
}

TEST_CASE_FIXTURE(TypedHIRFixture,
                  "T1.6 P2 TypedProgram.statements coverage parity for flow body") {
    // Source exercises all 7 TypedStmtKind variants across an agent + flow.
    //   state Init   -> let / assign / if(else) / assert / expr / goto
    //   state Done   -> let / return
    // The `if` branches also contain statements so nested BlockSyntax nodes
    // are exercised for parity and TypedProgram.blocks coverage.
    const std::string source = R"AHFL(
struct Req { v: String; token: Optional<String> = none; }
struct Ctx { v: String = ""; count: Int = 0; }
struct Resp { v: String; code: Int = 200; }

capability SideEffect(x: String) -> Resp;
predicate Safe(s: String) -> Bool;

agent Worker {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [SideEffect];
    transition Init -> Done;
}
contract for Worker { requires: true; }
flow for Worker {
    state Init {
        // 1) Let: bind a local.
        let greeting: String = "hello";
        // 2) Assign: mutate ctx.
        ctx.v = greeting;
        // 3) Assert: pure boolean condition.
        assert(ctx.v == "hello");
        // 4) Expr statement: capability call.
        SideEffect(input.v);
        // 5) If with else. Both branches contain additional Let / Assign /
        //    ExprStatement statements so that nested BlockSyntax is created
        //    and visited.
        if (input.token != none) {
            let tok = input.token;
            ctx.count = ctx.count + 2;
            SideEffect(ctx.v);
        } else {
            let defaulted: String = "default";
            ctx.v = defaulted;
            let extra: Int = ctx.count + 1;
            ctx.count = extra;
        }
        // 6) Assign (second occurrence) to make sure let/assign appear in
        //    multiple contexts.
        ctx.count = ctx.count + 1;
        // 7) Goto.
        goto Done;
    }
    state Done {
        let final_msg: String = ctx.v + " world";
        return Resp { v: final_msg, code: 200 };
    }
}
)AHFL";

    // Parse + resolve + typecheck via fixture.
    const auto parse = frontend.parse_text("t16_p3_parity.ahfl", source);
    REQUIRE_FALSE(parse.has_errors());
    REQUIRE(parse.program != nullptr);
    ahfl::Resolver resolver;
    const auto resolve = resolver.resolve(*parse.program);
    REQUIRE_FALSE(resolve.has_errors());
    ahfl::TypeChecker checker;
    const auto tc = checker.check(*parse.program, resolve);
    REQUIRE_FALSE(tc.has_errors());

    // --- TC1 assertions ----------------------------------------------------
    const std::size_t total_ast_stmt_count = total_flow_statement_count(*parse.program);
    const std::size_t typed_stmt_count = tc.typed_program.statements.size();

    // The typechecker records exactly one TypedStatement per AST
    // StatementSyntax (including statements inside nested if/then/else
    // blocks). 1:1 parity is the design goal.
    INFO("AST statements = " << total_ast_stmt_count
                             << ", TypedProgram.statements = " << typed_stmt_count);
    CHECK(typed_stmt_count == total_ast_stmt_count);

    // TypedProgram must contain at least 3 blocks:
    //   * state Init body
    //   * then block of the `if (input.token != none)`
    //   * else block of the same if
    //   (state Done body is an additional fourth)
    CHECK(tc.typed_program.blocks.size() >= 3);

    // Secondary structural check: at least one TypedStatement per kind must
    // appear in the flat store for the 7 kinds we explicitly used.
    using K = ahfl::TypedStmtKind;
    std::unordered_set<K> seen_kinds;
    for (const auto &s : tc.typed_program.statements)
        seen_kinds.insert(s.kind);
    CHECK(seen_kinds.contains(K::Let));
    CHECK(seen_kinds.contains(K::Assign));
    CHECK(seen_kinds.contains(K::If));
    CHECK(seen_kinds.contains(K::Goto));
    CHECK(seen_kinds.contains(K::Return));
    CHECK(seen_kinds.contains(K::Assert));
    CHECK(seen_kinds.contains(K::ExprStatement));
}

TEST_CASE_FIXTURE(TypedHIRFixture,
                  "T1.6 P2 TypedStatement children resolve to valid TypedExpr records") {
    // Use the same rich source as TC1 so every statement kind has a chance
    // to carry non-trivial children_expr_index entries.
    const std::string source = R"AHFL(
struct Req { v: String; token: Optional<String> = none; }
struct Ctx { v: String = ""; count: Int = 0; }
struct Resp { v: String; code: Int = 200; }

capability SideEffect(x: String) -> Resp;
predicate Safe(s: String) -> Bool;

agent Worker {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [SideEffect];
    transition Init -> Done;
}
contract for Worker { requires: true; }
flow for Worker {
    state Init {
        let greeting: String = "hello";
        ctx.v = greeting;
        assert(ctx.v == "hello");
        SideEffect(input.v);
        if (input.token != none) {
            let tok = input.token;
            ctx.count = ctx.count + 2;
            SideEffect(ctx.v);
        } else {
            let defaulted: String = "default";
            ctx.v = defaulted;
            let extra: Int = ctx.count + 1;
            ctx.count = extra;
        }
        ctx.count = ctx.count + 1;
        goto Done;
    }
    state Done {
        let final_msg: String = ctx.v + " world";
        return Resp { v: final_msg, code: 200 };
    }
}
)AHFL";

    const auto parse = frontend.parse_text("t16_p3_children.ahfl", source);
    REQUIRE_FALSE(parse.has_errors());
    REQUIRE(parse.program != nullptr);
    ahfl::Resolver resolver;
    const auto resolve = resolver.resolve(*parse.program);
    REQUIRE_FALSE(resolve.has_errors());
    ahfl::TypeChecker checker;
    const auto tc = checker.check(*parse.program, resolve);
    REQUIRE_FALSE(tc.has_errors());

    // --- TC2 assertions ----------------------------------------------------
    const auto &stmts = tc.typed_program.statements;
    const auto &exprs = tc.typed_program.expressions;
    REQUIRE_FALSE(stmts.empty());
    REQUIRE_FALSE(exprs.empty());

    std::size_t total_slots = 0;
    std::size_t valid_slots = 0;
    for (const auto &s : stmts) {
        for (const auto idx : s.children_expr_index) {
            ++total_slots;
            if (idx == UINT32_MAX) {
                // INTENTIONAL: an absent payload (e.g. a hypothetical
                // `return;` with no value). Counted in the denominator to
                // remain pessimistic.
                continue;
            }
            // (1) index must be in range.
            if (idx >= exprs.size()) {
                FAIL_CHECK("children_expr_index[" << idx << "] out of range [" << exprs.size()
                                                  << "]");
                continue;
            }
            // (2) resolved TypedExpr must have a resolved type pointer.
            const auto &typed = exprs[idx];
            if (typed.type != nullptr) {
                ++valid_slots;
            } else {
                FAIL_CHECK("children_expr_index["
                           << idx << "] resolves to a TypedExpr with type == nullptr"
                           << " (stmt kind = " << static_cast<int>(s.kind) << ")");
            }
        }
    }

    INFO("total child slots = " << total_slots << ", valid = " << valid_slots);
    REQUIRE(total_slots > 0);

    // At least 60% of all non-null children slots must resolve to a valid,
    // typed TypedExpr entry. For the source above, Goto is the only
    // statement that never has a payload (UINT32_MAX); every other kind
    // carries at least one.
    const auto ratio = static_cast<double>(valid_slots) / static_cast<double>(total_slots);
    CHECK(ratio >= 0.60);
}
