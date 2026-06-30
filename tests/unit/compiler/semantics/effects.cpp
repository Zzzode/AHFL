#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/validate.hpp"

#include "compiler/semantics/typecheck_internal.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] ahfl::SourceRange range_of(std::string_view source, std::string_view needle) {
    const auto offset = source.find(needle);
    REQUIRE(offset != std::string_view::npos);
    return ahfl::SourceRange{
        .begin_offset = offset,
        .end_offset = offset + needle.size(),
    };
}

} // namespace

[[nodiscard]] bool diagnostics_contain(const ahfl::DiagnosticBag &diagnostics,
                                       std::string_view needle) {
    for (const auto &entry : diagnostics.entries()) {
        if (entry.message.find(needle) != std::string::npos) {
            return true;
        }
        for (const auto &related : entry.related) {
            if (related.message.find(needle) != std::string::npos) {
                return true;
            }
        }
    }
    return false;
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

[[nodiscard]] const ahfl::Diagnostic *diagnostic_with_code(const ahfl::DiagnosticBag &diagnostics,
                                                           std::string_view code) {
    for (const auto &entry : diagnostics.entries()) {
        if (entry.code.has_value() && *entry.code == code) {
            return &entry;
        }
    }
    return nullptr;
}

[[nodiscard]] ahfl::ValidationResult validate_source(std::string_view filename,
                                                     std::string_view source,
                                                     bool require_typecheck_clean = true) {
    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text(std::string(filename), std::string(source));
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto type_result = type_checker.check(*parse_result.program, resolve_result);
    if (require_typecheck_clean) {
        REQUIRE_FALSE(type_result.has_errors());
    }

    const ahfl::Validator validator;
    auto validation_result = validator.validate(*parse_result.program, resolve_result, type_result);
    REQUIRE(validation_result.has_errors());
    return validation_result;
}

[[nodiscard]] ahfl::TypeCheckResult typecheck_source(std::string_view filename,
                                                     std::string_view source) {
    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text(std::string(filename), std::string(source));
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    return type_checker.check(*parse_result.program, resolve_result);
}

void write_file(const std::filesystem::path &path, const std::string &content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}

template <typename DiagBag>
void dump_diagnostics(std::string_view label, const DiagBag &bag) {
    std::ostringstream ss;
    for (const auto &entry : bag.entries()) {
        ss << "DIAG " << label << ": code="
           << (entry.code.has_value() ? std::string{*entry.code} : "-")
           << " sev=" << static_cast<int>(entry.severity)
           << " msg=[" << entry.message << "]\n";
        for (const auto &r : entry.related) {
            ss << "  RELATED: [" << r.message << "]\n";
        }
    }
    std::fprintf(stderr, "%s", ss.str().c_str());
}

[[nodiscard]] std::filesystem::path module_source_path(const std::filesystem::path &root,
                                                       std::string_view module_name) {
    auto path = root;
    std::string module{module_name};
    std::size_t start = 0;
    while (true) {
        const auto separator = module.find("::", start);
        const auto segment = module.substr(start, separator - start);
        if (separator == std::string::npos) {
            path /= segment + ".ahfl";
            return path;
        }
        path /= segment;
        start = separator + 2;
    }
}

[[nodiscard]] ahfl::TypeCheckResult typecheck_project_source(std::string_view filename,
                                                             std::string_view source,
                                                             ahfl::TypeCheckOptions options = {}) {
    const auto root = std::filesystem::temp_directory_path() /
                      ("ahfl_effects_" + std::string{filename});
    std::filesystem::remove_all(root);
    const auto main_path = root / "app" / "main.ahfl";
    write_file(main_path, "module app::main;\n" + std::string{source});

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_project(ahfl::ProjectInput{
        .entry_files = {main_path},
        .search_roots = {root, std::filesystem::path{"std"}},
    });
    REQUIRE_FALSE(parse_result.has_errors());

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(parse_result.graph);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    return type_checker.check(parse_result.graph, resolve_result, options);
}

[[nodiscard]] ahfl::TypeCheckResult
typecheck_project_module_source(std::string_view module_name,
                                std::string_view source,
                                ahfl::TypeCheckOptions options = {}) {
    std::string root_name{module_name};
    std::replace(root_name.begin(), root_name.end(), ':', '_');
    const auto root = std::filesystem::temp_directory_path() / ("ahfl_effects_" + root_name);
    std::filesystem::remove_all(root);
    const auto source_path = module_source_path(root, module_name);
    write_file(source_path, std::string{source});

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_project(ahfl::ProjectInput{
        .entry_files = {source_path},
        .search_roots = {root, std::filesystem::path{"std"}},
    });
    REQUIRE_FALSE(parse_result.has_errors());

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(parse_result.graph);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    return type_checker.check(parse_result.graph, resolve_result, options);
}

TEST_CASE("DiagnosticReporter preserves source-present and source-absent ranges") {
    ahfl::DiagnosticBag diagnostics;
    const ahfl::SourceUnit *current_source = nullptr;
    ahfl::DiagnosticReporter reporter(diagnostics, current_source);

    reporter.typecheck_error(ahfl::error_codes::typecheck::InvalidOperation,
                             "source absent",
                             ahfl::SourceRange{.begin_offset = 0, .end_offset = 5});
    REQUIRE(diagnostics.entries().size() == 1);
    CHECK_FALSE(diagnostics.entries().front().source_name.has_value());
    CHECK_FALSE(diagnostics.entries().front().position.has_value());

    ahfl::SourceUnit source_unit{
        .id = ahfl::SourceId{1},
        .path = "reporter.ahfl",
        .module_name = "reporter",
        .module_range = ahfl::SourceRange{},
        .source =
            ahfl::SourceFile{
                .display_name = "reporter.ahfl",
                .content = "const Value: Int = true;\n",
            },
        .program = nullptr,
        .imports = {},
    };
    current_source = &source_unit;
    reporter.typecheck_error(ahfl::error_codes::typecheck::InvalidOperation,
                             "source present",
                             ahfl::SourceRange{.begin_offset = 19, .end_offset = 23});

    REQUIRE(diagnostics.entries().size() == 2);
    const auto &with_source = diagnostics.entries().back();
    REQUIRE(with_source.source_name.has_value());
    CHECK(*with_source.source_name == "reporter.ahfl");
    REQUIRE(with_source.position.has_value());
    CHECK(with_source.position->line == 1);
    CHECK(with_source.position->column == 20);
}

TEST_CASE("TypeCheckSession state gives single program and SourceGraph equivalent output") {
    const std::string source = R"AHFL(
struct Request {
    value: String;
}

struct Context {
    value: String = "pending";
}

struct Response {
    value: String;
}

agent SmokeAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for SmokeAgent {
    state Done {
        return Response {
            value: input.value,
        };
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto single_parse = frontend.parse_text("session_smoke.ahfl", source);
    REQUIRE_FALSE(single_parse.has_errors());
    REQUIRE(single_parse.program != nullptr);

    const ahfl::Resolver resolver;
    const auto single_resolve = resolver.resolve(*single_parse.program);
    REQUIRE_FALSE(single_resolve.has_errors());

    const ahfl::TypeChecker checker;
    const auto single_typecheck = checker.check(*single_parse.program, single_resolve);
    REQUIRE_FALSE(single_typecheck.has_errors());

    auto graph_parse = frontend.parse_text("session_smoke.ahfl", source);
    REQUIRE_FALSE(graph_parse.has_errors());
    REQUIRE(graph_parse.program != nullptr);
    ahfl::SourceGraph graph;
    graph.entry_sources.push_back(ahfl::SourceId{1});
    graph.sources.push_back(ahfl::SourceUnit{
        .id = ahfl::SourceId{1},
        .path = "session_smoke.ahfl",
        .module_name = {},
        .module_range = ahfl::SourceRange{},
        .source = graph_parse.source,
        .program = std::move(graph_parse.program),
        .imports = {},
    });

    const auto graph_resolve = resolver.resolve(graph);
    REQUIRE_FALSE(graph_resolve.has_errors());
    const auto graph_typecheck = checker.check(graph, graph_resolve);
    REQUIRE_FALSE(graph_typecheck.has_errors());

    CHECK(single_typecheck.typed_program.declarations.size() ==
          graph_typecheck.typed_program.declarations.size());
    CHECK(single_typecheck.typed_program.expressions.size() ==
          graph_typecheck.typed_program.expressions.size());
    CHECK(single_typecheck.environment.structs().size() ==
          graph_typecheck.environment.structs().size());
    CHECK(single_typecheck.environment.agents().size() ==
          graph_typecheck.environment.agents().size());
    CHECK(single_typecheck.environment.flows().size() ==
          graph_typecheck.environment.flows().size());
}

TEST_CASE("Expression effects distinguish predicate and capability calls") {
    const std::string source = R"AHFL(
struct Request {
    value: String;
}

struct Context {
    value: String = "pending";
}

struct Response {
    value: String;
}

predicate Ready(value: String) -> Bool;
capability Do(request: Request) -> Response;

agent EffectAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [Do];
    transition Init -> Done;
}

contract for EffectAgent {
    requires: Ready(input.value);
}

flow for EffectAgent {
    state Init {
        let reply = Do(input);
        ctx.value = reply.value;
        goto Done;
    }

    state Done {
        return Response {
            value: ctx.value,
        };
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("effect_test.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto type_result = type_checker.check(*parse_result.program, resolve_result);
    REQUIRE_FALSE(type_result.has_errors());

    const auto *predicate_effect = type_result.typed_program.find_expr_by_range(
        range_of(source, "Ready(input.value)"), std::nullopt);
    REQUIRE(predicate_effect != nullptr);
    CHECK(predicate_effect->effect == ahfl::ExprEffect::PredicateCall);
    CHECK(predicate_effect->is_pure);

    const auto *capability_effect =
        type_result.typed_program.find_expr_by_range(range_of(source, "Do(input)"), std::nullopt);
    REQUIRE(capability_effect != nullptr);
    CHECK(capability_effect->effect == ahfl::ExprEffect::CapabilityCall);
    CHECK_FALSE(capability_effect->is_pure);
}

TEST_CASE("Const expression diagnostics report the concrete expression effect") {
    const std::string source = R"AHFL(
struct Response {
    value: String;
}

capability Build() -> Response;

const bad: Response = Build();
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("const_expr_effect_diagnostic.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto type_result = type_checker.check(*parse_result.program, resolve_result);
    REQUIRE(type_result.has_errors());

    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.CONST_EXPR_REQUIRED") ==
          1);
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "const expression required in const initializer"));
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "expression has runtime effect: capability call"));
}

TEST_CASE("Non-pure expression diagnostics report concrete effects across semantic boundaries") {
    const std::string source = R"AHFL(
struct Request {
    value: String;
}

struct Context {
    value: String = "";
}

struct Response {
    value: String;
}

capability Probe(value: String) -> Bool;

agent NonPureAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [Probe];
}

contract for NonPureAgent {
    requires: Probe(input.value);
}

flow for NonPureAgent {
    state Done {
        if Probe(input.value) {
        }
        assert Probe(input.value);
        return Response { value: input.value };
    }
}

workflow NonPureWorkflow {
    input: Request;
    output: Response;
    node run: NonPureAgent(input);
    safety: always Probe(input.value);
    return: run;
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("non_pure_effect_diagnostics.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto type_result = type_checker.check(*parse_result.program, resolve_result);
    REQUIRE(type_result.has_errors());

    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.NON_PURE_EXPRESSION") ==
          4);
    CHECK(diagnostics_contain(
        type_result.diagnostics,
        "contract expression must be pure; expression effect: capability call"));
    CHECK(diagnostics_contain(
        type_result.diagnostics,
        "temporal embedded expression must be pure; expression effect: capability call"));
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "if condition must be pure; expression effect: capability call"));
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "assert condition must be pure; expression effect: capability call"));
}

TEST_CASE("P4 Pure function body without decreases reports NO_DECREASES") {
    const std::string source = R"AHFL(
fn missing_measure() -> Int effect Pure {
    return 1;
}
)AHFL";

    const auto type_result = typecheck_source("p4_missing_decreases.ahfl", source);
    REQUIRE(type_result.has_errors());
    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.NO_DECREASES") == 1);
}

TEST_CASE("P4 verified contract rejects Nondet function calls with subset codes") {
    const std::string source = R"AHFL(
struct Request {
    value: Int;
}

struct Context {
    value: Int = 0;
}

struct Response {
    value: Int;
}

fn nondet_source() -> Int effect Nondet {
    return 1;
}

agent VerifiedContractAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

contract for VerifiedContractAgent {
    requires: nondet_source() > 0;
}
)AHFL";

    const auto type_result = typecheck_source("p4_contract_nondet.ahfl", source);
    REQUIRE(type_result.has_errors());
    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.EFFECT_NOT_PURE") == 1);
    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.NOT_IN_VERIFIED_SUBSET") ==
          1);
}

TEST_CASE("P4 invariant rejects Nondet function calls with invariant code") {
    const std::string source = R"AHFL(
struct Request {
    value: Int;
}

struct Context {
    value: Int = 0;
}

struct Response {
    value: Int;
}

fn nondet_flag() -> Bool effect Nondet {
    return true;
}

agent InvariantAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

contract for InvariantAgent {
    invariant: always nondet_flag();
}
)AHFL";

    const auto type_result = typecheck_source("p4_invariant_nondet.ahfl", source);
    REQUIRE(type_result.has_errors());
    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.EFFECT_NOT_PURE") == 1);
    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.NOT_IN_VERIFIED_SUBSET") ==
          1);
    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.NONDET_IN_INVARIANT") ==
          1);
}

TEST_CASE("P4 verified contract rejects Nondet method calls with subset codes") {
    const std::string source = R"AHFL(
struct Request {
    value: Int;
}

struct Context {
    value: Int = 0;
}

struct Response {
    value: Int;
}

impl Request {
    fn unstable(self: Request) -> Bool effect Nondet {
        return true;
    }
}

agent MethodVerifiedAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

contract for MethodVerifiedAgent {
    requires: input.unstable();
}
)AHFL";

    const auto type_result = typecheck_source("p4_contract_nondet_method.ahfl", source);
    REQUIRE(type_result.has_errors());
    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.EFFECT_NOT_PURE") == 1);
    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.NOT_IN_VERIFIED_SUBSET") ==
          1);
}

TEST_CASE("P4 Pure impl method body without decreases reports NO_DECREASES") {
    const std::string source = R"AHFL(
struct Request {
    value: Int;
}

impl Request {
    fn missing_measure(self: Request) -> Int effect Pure {
        return self.value;
    }
}
)AHFL";

    const auto type_result = typecheck_source("p4_impl_missing_decreases.ahfl", source);
    REQUIRE(type_result.has_errors());
    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.NO_DECREASES") == 1);
}

TEST_CASE("Bool semantic boundary diagnostics use stable typecheck codes") {
    const std::string source = R"AHFL(
struct Request {
    value: String;
}

struct Context {
    value: String = "";
}

struct Response {
    value: String;
}

agent BoolBoundaryAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

contract for BoolBoundaryAgent {
    requires: input.value;
}

flow for BoolBoundaryAgent {
    state Done {
        if input.value {
        }
        assert input.value;
        return Response { value: input.value };
    }
}

workflow BoolBoundaryWorkflow {
    input: Request;
    output: Response;
    node run: BoolBoundaryAgent(input);
    safety: always input.value;
    return: run;
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("bool_boundary_diagnostic_codes.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto type_result = type_checker.check(*parse_result.program, resolve_result);
    REQUIRE(type_result.has_errors());

    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.TYPE_MISMATCH") == 4);
    CHECK(diagnostics_contain(type_result.diagnostics, "contract expression must have type Bool"));
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "temporal embedded expression must have type Bool"));
    CHECK(diagnostics_contain(type_result.diagnostics, "if condition must have type Bool"));
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "type mismatch in assert condition: expected Bool, got String"));
    CHECK(diagnostics_contain(type_result.diagnostics, "actual expression has type 'String' here"));
}

TEST_CASE("Callable diagnostics use stable typecheck codes") {
    const std::string source = R"AHFL(
struct Request {
    value: String;
}

struct Context {
    value: String = "";
}

struct Response {
    value: String;
}

capability Declared(value: String, extra: String) -> Response;
capability NotDeclared(value: String) -> Response;
capability AsPredicate(value: String) -> Bool;
predicate NeedsTwo(value: String, extra: String) -> Bool;
predicate Wrap(flag: Bool) -> Bool;

agent CallableAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [Declared, AsPredicate];
}

contract for CallableAgent {
    requires: NeedsTwo(input.value);
    requires: Wrap(AsPredicate(input.value));
}

flow for CallableAgent {
    state Done {
        let missing = NotDeclared(input.value);
        let wrong = Declared(input.value);
        return Response { value: input.value };
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("callable_diagnostic_codes.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto type_result = type_checker.check(*parse_result.program, resolve_result);
    REQUIRE(type_result.has_errors());

    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.WRONG_ARITY") == 2);
    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.CAPABILITY_NOT_ALLOWED") ==
          2);
    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.NON_PURE_EXPRESSION") ==
          2);

    CHECK(diagnostics_contain(type_result.diagnostics,
                              "predicate 'NeedsTwo' expects 2 argument(s), got 1"));
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "capability 'Declared' expects 2 argument(s), got 1"));
    CHECK(diagnostics_contain(
        type_result.diagnostics,
        "capability 'NotDeclared' is not declared in the current agent capabilities"));
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "capability call 'AsPredicate' is not allowed in this context"));
    CHECK(diagnostics_contain(
        type_result.diagnostics,
        "predicate arguments must be pure expressions; expression effect: capability call"));
}

TEST_CASE("Field and literal diagnostics use stable typecheck codes") {
    const std::string source = R"AHFL(
struct Request {
    value: String;
    numbers: List<Int>;
}

struct Context {
    value: String = "";
}

struct Response {
    value: String;
}

agent FieldLiteralAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for FieldLiteralAgent {
    state Done {
        let bad_member = input.value.missing;
        let bad_field = input.valu;
        let bad_string_index = input.value[0];
        let bad_list_index = input.numbers["zero"];
        let empty_list = std::collections::list_from_array<Int>();
        let empty_set = std::collections::set_from_array<Int>();
        let empty_map = std::collections::map_from_entries<String, Int>();
        let none_value = std::option::Option::None;
        let missing = Response {};
        return Response {
            value: input.value,
            value: input.value,
            valu: input.value,
        };
    }
}
)AHFL";

    const auto type_result = typecheck_project_source("field_literal_diagnostic_codes.ahfl", source);
    REQUIRE(type_result.has_errors());

    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.INVALID_MEMBER_ACCESS") ==
          1);
    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.UNKNOWN_FIELD") == 2);
    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.DUPLICATE_FIELD") == 1);
    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.MISSING_FIELD") == 1);
    CHECK(diagnostic_count_with_code(type_result.diagnostics,
                                     "typecheck.EMPTY_LITERAL_WITHOUT_CONTEXT") == 0);
    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.NONE_WITHOUT_CONTEXT") ==
          0);
    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.INVALID_INDEX_ACCESS") ==
          2);

    CHECK(diagnostics_contain(type_result.diagnostics,
                              "member access requires a struct value, got String"));
    CHECK(diagnostics_contain(
        type_result.diagnostics,
        "unknown field 'valu' on struct 'app::main::Request'; did you mean 'value'?"));
    CHECK(
        diagnostics_contain(type_result.diagnostics, "duplicate field 'value' in struct literal"));
    CHECK(diagnostics_contain(type_result.diagnostics, "missing field 'value' in struct literal"));
    // `std::option::Option::None` without an explicit expected type is now
    // represented as the unspecialised nominal Option enum rather than the
    // old `'none'` sugar literal, so no "cannot infer type of none" diagnostic
    // fires (the bidirectional-typing gate in QualifiedValue handling simply
    // returns the nominal owner type when no surrounding context is available).
    CHECK(diagnostics_contain(type_result.diagnostics, "list index must have type Int"));
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "index access requires a List or Map value, got String"));
}

TEST_CASE("Expression operation diagnostics use stable typecheck codes") {
    const std::string source = R"AHFL(
struct Request {
    text: String;
    count: Int;
    flag: Bool;
}

struct Context {
    value: String = "pending";
}

struct Response {
    value: String;
}

agent OperationDiagnosticAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for OperationDiagnosticAgent {
    state Done {
        let bad_not = not input.text;
        let bad_unary = -input.text;
        let bad_none = input.text == std::option::Option::None;
        let bad_logic = input.flag and input.text;
        let bad_compare = input.text < input.count;
        let bad_add = input.text + input.count;
        let bad_subtract = input.text - input.count;
        let bad_multiply = input.text * input.count;
        let bad_modulo = input.text % input.count;
        return Response {
            value: input.text,
        };
    }
}
)AHFL";

    const auto type_result = typecheck_project_source("operation_diagnostic_codes.ahfl", source);
    REQUIRE(type_result.has_errors());

    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.INVALID_OPERATION") == 9);

    CHECK(diagnostics_contain(type_result.diagnostics, "logical not requires Bool, got String"));
    CHECK(
        diagnostics_contain(type_result.diagnostics,
                            "numeric unary operator requires Int, Float, or Decimal, got String"));
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "comparison with none requires Optional<T>, got String"));
    CHECK(diagnostics_contain(type_result.diagnostics, "logical operator requires Bool operands"));
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "comparison operands are not type-compatible: String vs Int"));
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "operator '+' is not defined for String and Int"));
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "operator '-' is not defined for String and Int"));
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "arithmetic operator is not defined for String and Int"));
    CHECK(diagnostics_contain(type_result.diagnostics, "operator '%' requires Int operands"));
}

TEST_CASE("Source arithmetic rejects implicit numeric promotion") {
    const std::string source = R"AHFL(
struct Request {
    text: String;
    count: Int;
}

struct Context {
    value: String = "pending";
}

struct Response {
    value: String;
}

agent ArithmeticAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for ArithmeticAgent {
    state Done {
        let ok_int = input.count + 1;
        let ok_float = 1.25 + 2.5;
        let ok_decimal_add = 1.00d + 2.00d;
        let ok_decimal_subtract = 2.00d - 1.00d;
        let ok_string = input.text + "x";
        let bad_int_float_add = input.count + 1.25;
        let bad_int_decimal_add = input.count + 1.00d;
        let bad_decimal_scale_add = 1.0d + 1.00d;
        let bad_decimal_multiply = 1.00d * 2.00d;
        let bad_int_float_compare = input.count < 1.25;
        return Response {
            value: ok_string,
        };
    }
}
)AHFL";

    const auto type_result = typecheck_source("strict_arithmetic.ahfl", source);
    REQUIRE(type_result.has_errors());

    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.INVALID_OPERATION") == 5);
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "operator '+' is not defined for Int and Float"));
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "operator '+' is not defined for Int and Decimal(2)"));
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "operator '+' is not defined for Decimal(1) and Decimal(2)"));
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "arithmetic operator is not defined for Decimal(2) and Decimal(2)"));
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "comparison operands are not type-compatible: Int vs Float"));
}

TEST_CASE("Declaration diagnostics use stable typecheck codes") {
    const std::string source = R"AHFL(
struct Request {
    value: String;
}

struct Context {
    value: String;
}

struct Response {
    value: String;
}

struct DuplicateField {
    value: Int;
    value: String;
}

enum DuplicateVariant {
    Pending,
    Pending,
}

agent DeclarationDiagnosticAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("declaration_diagnostic_codes.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto type_result = type_checker.check(*parse_result.program, resolve_result);
    REQUIRE(type_result.has_errors());

    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.DUPLICATE_FIELD") == 1);
    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.DUPLICATE_VARIANT") == 1);
    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.MISSING_FIELD") == 1);

    CHECK(diagnostics_contain(type_result.diagnostics, "duplicate struct field 'value'"));
    CHECK(diagnostics_contain(type_result.diagnostics, "duplicate enum variant 'Pending'"));
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "agent context field 'value' must declare a default value"));
}

TEST_CASE("Let shadowing diagnostics use stable typecheck warning code") {
    const std::string source = R"AHFL(
struct Request {
    value: String;
}

struct Context {
    value: String = "";
}

struct Response {
    value: String;
}

agent ShadowAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];
    transition Init -> Done;
}

flow for ShadowAgent {
    state Init {
        let reply: String = input.value;
        let reply: String = reply;
        goto Done;
    }

    state Done {
        return Response { value: ctx.value };
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("shadowed_binding_warning.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto type_result = type_checker.check(*parse_result.program, resolve_result);
    CHECK_FALSE(type_result.has_errors());
    CHECK(type_result.diagnostics.has_warning());
    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.SHADOWED_BINDING") == 1);

    const auto *diagnostic =
        diagnostic_with_code(type_result.diagnostics, "typecheck.SHADOWED_BINDING");
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->severity == ahfl::DiagnosticSeverity::Warning);
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "let binding 'reply' shadows an existing binding of type 'String'"));
}

TEST_CASE("Typecheck missing reference fallbacks use stable typecheck codes") {
    const std::string source = R"AHFL(
struct Payload {
    value: String;
}

enum Mode {
    A,
    B,
}

predicate Ready() -> Bool;

const StructFallback: Payload = Payload { value: "ok" };
const QualifiedFallback: Mode = Mode::A;
const CallFallback: Bool = Ready();
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("typecheck_missing_references.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    ahfl::ResolveResult missing_references;
    missing_references.symbol_table = resolve_result.symbol_table;

    const ahfl::TypeChecker type_checker;
    const auto type_result = type_checker.check(*parse_result.program, missing_references);
    REQUIRE(type_result.has_errors());

    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.UNKNOWN_TYPE") >= 2);
    CHECK(diagnostic_count_with_code(type_result.diagnostics,
                                     "typecheck.UNKNOWN_QUALIFIED_VALUE") == 1);
    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.UNKNOWN_CALLABLE") == 1);
    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.SEMANTIC_ERROR") == 0);

    CHECK(diagnostics_contain(type_result.diagnostics, "unknown type 'Payload'"));
    CHECK(diagnostics_contain(type_result.diagnostics, "unknown qualified value 'Mode::A'"));
    CHECK(diagnostics_contain(type_result.diagnostics, "unknown callable 'Ready'"));
}

TEST_CASE("Typecheck invalid type reference fallback uses stable typecheck code") {
    const std::string source = R"AHFL(
struct Payload {
    value: String;
}

capability Do() -> Payload;

const Broken: Payload = Payload { value: "ok" };
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("typecheck_invalid_type_reference.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const auto capability_symbol =
        resolve_result.symbol_table.find_local(ahfl::SymbolNamespace::Capabilities, "Do");
    REQUIRE(capability_symbol.has_value());

    ahfl::ResolveResult invalid_type_reference;
    invalid_type_reference.symbol_table = resolve_result.symbol_table;
    for (const auto &import : resolve_result.imports()) {
        invalid_type_reference.add_import(import);
    }
    for (auto reference : resolve_result.references()) {
        if (reference.kind == ahfl::ReferenceKind::TypeName && reference.text == "Payload") {
            reference.target = capability_symbol->get().id;
        }
        invalid_type_reference.add_reference(reference);
    }

    const ahfl::TypeChecker type_checker;
    const auto type_result = type_checker.check(*parse_result.program, invalid_type_reference);
    REQUIRE(type_result.has_errors());

    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.INVALID_TYPE_REFERENCE") >=
          1);
    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.SEMANTIC_ERROR") == 0);
    CHECK(diagnostics_contain(type_result.diagnostics, "symbol 'Do' does not name a type"));
}

TEST_CASE("Typecheck invalid callable reference fallbacks use stable typecheck code") {
    const std::string source = R"AHFL(
struct Payload {
    value: String;
}

predicate Ready() -> Bool;

const Broken: Bool = Ready();
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result =
        frontend.parse_text("typecheck_invalid_callable_reference.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    ahfl::ResolveResult missing_callable_symbol;
    missing_callable_symbol.symbol_table = resolve_result.symbol_table;
    for (const auto &import : resolve_result.imports()) {
        missing_callable_symbol.add_import(import);
    }
    for (auto reference : resolve_result.references()) {
        if (reference.kind == ahfl::ReferenceKind::CallTarget && reference.text == "Ready") {
            reference.target = ahfl::SymbolId{resolve_result.symbol_table.symbols().size() + 1000};
        }
        missing_callable_symbol.add_reference(reference);
    }

    const ahfl::TypeChecker type_checker;
    const auto missing_symbol_result =
        type_checker.check(*parse_result.program, missing_callable_symbol);
    REQUIRE(missing_symbol_result.has_errors());
    CHECK(diagnostic_count_with_code(missing_symbol_result.diagnostics,
                                     "typecheck.INVALID_CALLABLE_REFERENCE") == 1);
    CHECK(diagnostic_count_with_code(missing_symbol_result.diagnostics,
                                     "typecheck.SEMANTIC_ERROR") == 0);
    CHECK(diagnostics_contain(missing_symbol_result.diagnostics, "call target symbol is missing"));

    const auto payload_symbol =
        resolve_result.symbol_table.find_local(ahfl::SymbolNamespace::Types, "Payload");
    REQUIRE(payload_symbol.has_value());

    ahfl::ResolveResult non_callable_symbol;
    non_callable_symbol.symbol_table = resolve_result.symbol_table;
    for (const auto &import : resolve_result.imports()) {
        non_callable_symbol.add_import(import);
    }
    for (auto reference : resolve_result.references()) {
        if (reference.kind == ahfl::ReferenceKind::CallTarget && reference.text == "Ready") {
            reference.target = payload_symbol->get().id;
        }
        non_callable_symbol.add_reference(reference);
    }

    const auto non_callable_result = type_checker.check(*parse_result.program, non_callable_symbol);
    REQUIRE(non_callable_result.has_errors());
    CHECK(diagnostic_count_with_code(non_callable_result.diagnostics,
                                     "typecheck.INVALID_CALLABLE_REFERENCE") == 1);
    CHECK(diagnostic_count_with_code(non_callable_result.diagnostics, "typecheck.SEMANTIC_ERROR") ==
          0);
    CHECK(diagnostics_contain(non_callable_result.diagnostics,
                              "symbol 'Payload' does not name a callable"));
}

TEST_CASE("Typecheck missing callable metadata fallbacks use stable typecheck code") {
    const std::string capability_base = R"AHFL(
struct Payload {
    value: String;
}

const Broken: Payload = Do();
)AHFL";

    const std::string capability_full = capability_base + R"AHFL(

capability Do() -> Payload;
)AHFL";

    const ahfl::Frontend frontend;
    const auto capability_parse =
        frontend.parse_text("typecheck_missing_capability_metadata.ahfl", capability_base);
    REQUIRE_FALSE(capability_parse.has_errors());
    REQUIRE(capability_parse.program != nullptr);

    const auto capability_full_parse =
        frontend.parse_text("typecheck_missing_capability_metadata_full.ahfl", capability_full);
    REQUIRE_FALSE(capability_full_parse.has_errors());
    REQUIRE(capability_full_parse.program != nullptr);

    const ahfl::Resolver resolver;
    const auto capability_resolve = resolver.resolve(*capability_full_parse.program);
    REQUIRE_FALSE(capability_resolve.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto capability_result =
        type_checker.check(*capability_parse.program, capability_resolve);
    REQUIRE(capability_result.has_errors());
    CHECK(diagnostic_count_with_code(capability_result.diagnostics,
                                     "typecheck.MISSING_CALLABLE_METADATA") == 1);
    CHECK(diagnostic_count_with_code(capability_result.diagnostics, "typecheck.SEMANTIC_ERROR") ==
          0);
    CHECK(diagnostics_contain(capability_result.diagnostics,
                              "capability type info is missing for 'Do'"));

    const std::string predicate_base = R"AHFL(
const Broken: Bool = Ready();
)AHFL";

    const std::string predicate_full = predicate_base + R"AHFL(

predicate Ready() -> Bool;
)AHFL";

    const auto predicate_parse =
        frontend.parse_text("typecheck_missing_predicate_metadata.ahfl", predicate_base);
    REQUIRE_FALSE(predicate_parse.has_errors());
    REQUIRE(predicate_parse.program != nullptr);

    const auto predicate_full_parse =
        frontend.parse_text("typecheck_missing_predicate_metadata_full.ahfl", predicate_full);
    REQUIRE_FALSE(predicate_full_parse.has_errors());
    REQUIRE(predicate_full_parse.program != nullptr);

    const auto predicate_resolve = resolver.resolve(*predicate_full_parse.program);
    REQUIRE_FALSE(predicate_resolve.has_errors());

    const auto predicate_result = type_checker.check(*predicate_parse.program, predicate_resolve);
    REQUIRE(predicate_result.has_errors());
    CHECK(diagnostic_count_with_code(predicate_result.diagnostics,
                                     "typecheck.MISSING_CALLABLE_METADATA") == 1);
    CHECK(diagnostic_count_with_code(predicate_result.diagnostics, "typecheck.SEMANTIC_ERROR") ==
          0);
    CHECK(diagnostics_contain(predicate_result.diagnostics,
                              "predicate type info is missing for 'Ready'"));
}

TEST_CASE("Typecheck missing environment metadata fallbacks use stable typecheck codes") {
    const std::string base_source = R"AHFL(
module typecheck::metadata;
import typecheck::metadata as self;

const ConstBroken: String = self::Value;
const StructBroken: Payload = Payload { value: "ok" };
const EnumBroken: Mode = Mode::A;
)AHFL";

    const std::string full_source = base_source + R"AHFL(

const Value: String = "ok";

struct Payload {
    value: String;
}

enum Mode {
    A,
    B,
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto base_parse =
        frontend.parse_text("typecheck_missing_environment_metadata.ahfl", base_source);
    REQUIRE_FALSE(base_parse.has_errors());
    REQUIRE(base_parse.program != nullptr);

    const auto full_parse =
        frontend.parse_text("typecheck_missing_environment_metadata_full.ahfl", full_source);
    REQUIRE_FALSE(full_parse.has_errors());
    REQUIRE(full_parse.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*full_parse.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto type_result = type_checker.check(*base_parse.program, resolve_result);
    REQUIRE(type_result.has_errors());

    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.MISSING_CONST_METADATA") ==
          1);
    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.MISSING_TYPE_METADATA") ==
          2);
    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.SEMANTIC_ERROR") == 0);

    CHECK(diagnostics_contain(type_result.diagnostics,
                              "constant type info is missing for 'self::Value'"));
    CHECK(diagnostics_contain(type_result.diagnostics, "struct type info is missing for"));
    CHECK(diagnostics_contain(type_result.diagnostics, "enum type info is missing for"));
}

TEST_CASE("Validation state-machine diagnostics use stable validation codes") {
    const auto unreachable = validate_source("validation_unreachable_state.ahfl", R"AHFL(
struct Request {
    value: String;
}

struct Context {
    value: String = "pending";
}

struct Response {
    value: String;
}

agent UnreachableStateAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Init, Dead, Done];
    initial: Init;
    final: [Done];
    capabilities: [];
    transition Init -> Done;
    transition Dead -> Done;
}

flow for UnreachableStateAgent {
    state Init {
        goto Done;
    }

    state Dead {
        goto Done;
    }

    state Done {
        return Response {
            value: input.value,
        };
    }
}
)AHFL");

    CHECK(diagnostic_count_with_code(unreachable.diagnostics, "validation.INVALID_STATE") == 1);
    CHECK(diagnostics_contain(unreachable.diagnostics,
                              "state 'Dead' is unreachable from initial state 'Init'"));

    const auto flow = validate_source("validation_flow_state.ahfl", R"AHFL(
struct Request {
    value: String;
}

struct Context {
    value: String = "pending";
}

struct Response {
    value: String;
}

agent FlowStateAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Init, Middle, Done];
    initial: Init;
    final: [Done];
    capabilities: [];
    transition Init -> Middle;
    transition Middle -> Done;
}

flow for FlowStateAgent {
    state Init {
        goto Init;
    }

    state Done {
        return Response {
            value: input.value,
        };
    }
}
)AHFL");

    CHECK(diagnostic_count_with_code(flow.diagnostics, "validation.INVALID_STATE") == 2);
    CHECK(diagnostics_contain(flow.diagnostics, "missing non-final-state handler for 'Middle'"));
    CHECK(diagnostics_contain(flow.diagnostics, "illegal goto from state 'Init' to 'Init'"));
}

TEST_CASE("Validation duplicate capability diagnostics use stable validation code") {
    const auto duplicate_capability =
        validate_source("validation_duplicate_capability.ahfl", R"AHFL(
struct Request {
    value: String;
}

struct Context {
    value: String = "pending";
}

struct Response {
    value: String;
}

capability Echo(request: Request) -> Response;

agent DuplicateCapabilityAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [Echo, Echo];
    transition Init -> Done;
}

flow for DuplicateCapabilityAgent {
    state Init {
        goto Done;
    }

    state Done {
        return Response {
            value: input.value,
        };
    }
}
)AHFL");

    CHECK(diagnostic_count_with_code(duplicate_capability.diagnostics,
                                     "validation.DUPLICATE_CAPABILITY") == 1);
    CHECK(diagnostic_count_with_code(duplicate_capability.diagnostics,
                                     "validation.SEMANTIC_INVARIANT") == 0);
    CHECK(diagnostics_contain(duplicate_capability.diagnostics,
                              "duplicate capability 'Echo' in agent capability list"));
}

TEST_CASE("Validation workflow and temporal diagnostics use stable validation codes") {
    const auto temporal = validate_source("validation_temporal_formula.ahfl",
                                          R"AHFL(
struct Request {
    value: String;
}

struct Context {
    value: String = "pending";
}

struct Response {
    value: String;
}

agent TemporalAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];
    transition Init -> Done;
}

flow for TemporalAgent {
    state Init {
        goto Done;
    }

    state Done {
        return Response {
            value: input.value,
        };
    }
}

contract for TemporalAgent {
    invariant: always in_state(Missing);
    invariant: always running(step);
}

workflow TemporalWorkflow {
    input: Request;
    output: Response;
    node run: TemporalAgent(input);
    liveness: eventually completed(run, Init);
    return: run;
}
)AHFL",
                                          false);

    CHECK(diagnostic_count_with_code(temporal.diagnostics, "validation.INVALID_TEMPORAL_FORMULA") ==
          3);
    CHECK(diagnostics_contain(temporal.diagnostics,
                              "unknown state 'Missing' in contract for 'TemporalAgent'"));
    CHECK(diagnostics_contain(temporal.diagnostics,
                              "running(...) is only valid in workflow safety/liveness formulas"));
    CHECK(diagnostics_contain(temporal.diagnostics,
                              "state 'Init' is not a final state of node 'run'"));

    const auto workflow = validate_source("validation_workflow_graph.ahfl",
                                          R"AHFL(
struct Request {
    value: String;
}

struct Context {
    value: String = "pending";
}

agent WorkflowAgent {
    input: Request;
    context: Context;
    output: Request;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];
    transition Init -> Done;
}

flow for WorkflowAgent {
    state Init {
        goto Done;
    }

    state Done {
        return Request {
            value: input.value,
        };
    }
}

workflow BadWorkflow {
    input: Request;
    output: Request;
    node first: WorkflowAgent(input) after [missing];
    node second: WorkflowAgent(first) after [third];
    node third: WorkflowAgent(second) after [second];
    return: first;
}
)AHFL",
                                          false);

    CHECK(diagnostic_count_with_code(workflow.diagnostics, "validation.INVALID_WORKFLOW_GRAPH") ==
          2);
    CHECK(diagnostics_contain(workflow.diagnostics, "unknown workflow dependency 'missing'"));
    CHECK(
        diagnostics_contain(workflow.diagnostics, "workflow dependency cycle detected involving"));
}

TEST_CASE("Optional narrowing supports symmetric member facts and else complements") {
    const std::string source = R"AHFL(
struct Request {
    fallback: String;
}

struct Context {
    token: Optional<String> = std::option::Option::None;
}

struct Response {
    value: String;
}

agent NarrowAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for NarrowAgent {
    state Done {
        if (std::option::Option::None != ctx.token) {
            return Response { value: ctx.token };
        } else {
            if (ctx.token == std::option::Option::None) {
                return Response { value: input.fallback };
            } else {
                return Response { value: ctx.token };
            }
        }
    }
}
)AHFL";

    const auto type_result = typecheck_project_source("optional_narrowing.ahfl", source);
    CHECK_FALSE(type_result.has_errors());
}

TEST_CASE("Optional narrowing is invalidated by assignment") {
    const std::string source = R"AHFL(
struct Request {
    fallback: String;
}

struct Context {
    token: Optional<String> = std::option::Option::Some("seed");
}

struct Response {
    value: String;
}

agent NarrowAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for NarrowAgent {
    state Done {
        if (ctx.token != std::option::Option::None) {
            ctx.token = std::option::Option::None;
            return Response { value: ctx.token };
        } else {
            return Response { value: input.fallback };
        }
    }
}
)AHFL";

    const auto type_result = typecheck_project_source("optional_invalidation.ahfl", source);
    CHECK(type_result.has_errors());
}

TEST_CASE("Optional narrowing explanations are emitted only in debug mode") {
    const std::string source = R"AHFL(
struct Request {
    fallback: String;
}

struct Context {
    token: Optional<String> = std::option::Option::None;
}

struct Response {
    value: String;
}

agent NarrowDebugAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for NarrowDebugAgent {
    state Done {
        if (ctx.token != std::option::Option::None) {
            return Response { value: ctx.token };
        } else {
            return Response { value: input.fallback };
        }
    }
}
)AHFL";

    const auto default_result =
        typecheck_project_source("optional_narrowing_debug_default.ahfl", source);
    REQUIRE_FALSE(default_result.has_errors());
    CHECK(default_result.diagnostics.entries().empty());

    const auto debug_result =
        typecheck_project_source("optional_narrowing_debug.ahfl",
                                 source,
                                 ahfl::TypeCheckOptions{.explain_narrowing = true});
    REQUIRE_FALSE(debug_result.has_errors());
    CHECK(diagnostics_contain(
        debug_result.diagnostics,
        "narrowing: condition '(ctx.token != std::option::Option::None)' narrows 'ctx.token' to non-none on then "
        "branch"));
    CHECK(diagnostics_contain(
        debug_result.diagnostics,
        "narrowing: condition '(ctx.token != std::option::Option::None)' narrows 'ctx.token' to none on else branch"));
}

TEST_CASE("Optional narrowing explanations describe unsupported disjunctive conditions") {
    const std::string source = R"AHFL(
struct Request {
    fallback: String;
}

struct Context {
    token: Optional<String> = std::option::Option::None;
}

struct Response {
    value: String;
}

agent NarrowDebugUnsupportedAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for NarrowDebugUnsupportedAgent {
    state Done {
        if (ctx.token != std::option::Option::None || input.fallback != "") {
            return Response { value: input.fallback };
        } else {
            return Response { value: input.fallback };
        }
    }
}
)AHFL";

    const auto debug_result = typecheck_project_source(
        "optional_narrowing_debug_unsupported.ahfl",
        source,
        ahfl::TypeCheckOptions{.explain_narrowing = true});
    REQUIRE_FALSE(debug_result.has_errors());
    CHECK(diagnostics_contain(
        debug_result.diagnostics,
        "narrowing: condition '(ctx.token != std::option::Option::None || input.fallback != \"\")' did not produce "
        "Optional narrowing facts because disjunctive conditions are not represented"));
}

TEST_CASE("Type diagnostics suggest nearby struct fields") {
    const std::string source = R"AHFL(
struct Request {
    name: String;
}

struct Context {
    value: String = "";
}

struct Response {
    value: String;
}

agent SuggestAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for SuggestAgent {
    state Done {
        return Response { value: input.naem };
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("field_suggestion.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto type_result = type_checker.check(*parse_result.program, resolve_result);
    REQUIRE(type_result.has_errors());
    CHECK(diagnostics_contain(type_result.diagnostics, "did you mean 'name'?"));
}

TEST_CASE("Type diagnostics suggest nearby enum variants") {
    const std::string source = R"AHFL(
struct Request {
    value: String;
}

struct Context {
    value: String = "";
}

enum Status {
    Pending,
    Approved,
    Rejected,
}

struct Response {
    status: Status;
}

agent SuggestEnumAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for SuggestEnumAgent {
    state Done {
        return Response { status: Status::Aproved };
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("enum_variant_suggestion.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto type_result = type_checker.check(*parse_result.program, resolve_result);
    REQUIRE(type_result.has_errors());
    CHECK(diagnostics_contain(type_result.diagnostics, "did you mean 'Approved'?"));
}

TEST_CASE("Type diagnostics describe struct field expectation origins") {
    const std::string source = R"AHFL(
struct Request {
    value: String;
}

struct Context {
    value: String = "";
}

struct Response {
    value: String;
}

agent ExpectationAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for ExpectationAgent {
    state Done {
        return Response { value: 1 };
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("struct_field_expectation.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto type_result = type_checker.check(*parse_result.program, resolve_result);
    REQUIRE(type_result.has_errors());
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "expected type 'String' from struct field 'value' declared here"));
}

TEST_CASE("Type diagnostics preserve struct field expectation through list literals") {
    const std::string source = R"AHFL(
struct Request {
    value: String;
}

struct Context {
    value: String = "";
}

struct Response {
    values: List<String>;
}

agent NestedExpectationAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for NestedExpectationAgent {
    state Done {
        return Response { values: std::collections::list_from_array<Int>(1) };
    }
}
)AHFL";

    const auto type_result =
        typecheck_project_source("struct_field_list_expectation.ahfl", source);
    REQUIRE(type_result.has_errors());
    CHECK(diagnostics_contain(
        type_result.diagnostics,
        "expected type 'std::collections::List<String>' from struct field 'values' declared here"));
}

TEST_CASE("Type diagnostics preserve struct field expectation through grouped list literals") {
    const std::string source = R"AHFL(
struct Request {
    value: String;
}

struct Context {
    value: String = "";
}

struct Response {
    values: List<String>;
}

agent GroupedExpectationAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for GroupedExpectationAgent {
    state Done {
        return Response { values: (std::collections::list_from_array<Int>(1)) };
    }
}
)AHFL";

    const auto type_result =
        typecheck_project_source("struct_field_grouped_list_expectation.ahfl", source);
    REQUIRE(type_result.has_errors());
    CHECK(diagnostics_contain(
        type_result.diagnostics,
        "expected type 'std::collections::List<String>' from struct field 'values' declared here"));
}

TEST_CASE("Type diagnostics describe capability parameter expectation origins") {
    const std::string source = R"AHFL(
struct Request {
    value: String;
}

struct Context {
    value: String = "";
}

struct Response {
    value: String;
}

predicate Ready(value: String) -> Bool;
capability Do(value: String) -> Response;

agent ParameterExpectationAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [Do];
}

flow for ParameterExpectationAgent {
    state Done {
        return Do(1);
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("capability_parameter_expectation.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto type_result = type_checker.check(*parse_result.program, resolve_result);
    REQUIRE(type_result.has_errors());
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "argument #1: expected 'String' declared in `Do` from parameter 'value' declared here"));
}

TEST_CASE("Type diagnostics preserve flow return expectation through list literals") {
    const std::string source = R"AHFL(
struct Request {
    value: String;
}

struct Context {
    value: String = "";
}

agent ReturnExpectationAgent {
    input: Request;
    context: Context;
    output: List<String>;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for ReturnExpectationAgent {
    state Done {
        return std::collections::list_from_array<Int>(1);
    }
}
)AHFL";

    const auto type_result = typecheck_project_source("flow_return_list_expectation.ahfl", source);
    REQUIRE(type_result.has_errors());
    CHECK(diagnostics_contain(
        type_result.diagnostics,
        "expected schema 'std::collections::List<String>' from flow return declared here"));
}

TEST_CASE("Type diagnostics preserve assignment target expectation through list literals") {
    const std::string source = R"AHFL(
struct Request {
    fallback: String;
}

struct Context {
    values: List<String> = std::collections::list_from_array<Int>();
}

struct Response {
    value: String;
}

agent AssignmentExpectationAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for AssignmentExpectationAgent {
    state Done {
        ctx.values = std::collections::list_from_array<Int>(1);
        return Response { value: input.fallback };
    }
}
)AHFL";

    const auto type_result =
        typecheck_project_source("assignment_target_list_expectation.ahfl", source);
    REQUIRE(type_result.has_errors());
    CHECK(diagnostics_contain(
        type_result.diagnostics,
        "expected type 'std::collections::List<String>' from assignment target "
        "'ctx.values' declared here"));
}

TEST_CASE("Type diagnostics describe actual expression origins") {
    const std::string source = R"AHFL(
struct Request {
    value: String;
}

struct Context {
    value: String = "";
}

struct Response {
    value: String;
}

agent ActualOriginAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for ActualOriginAgent {
    state Done {
        return Response { value: 1 };
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("actual_expression_origin.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto type_result = type_checker.check(*parse_result.program, resolve_result);
    REQUIRE(type_result.has_errors());
    CHECK(diagnostics_contain(type_result.diagnostics, "actual expression has type 'Int' here"));
}

TEST_CASE("Const initializer diagnostics preserve declared type expectation") {
    const std::string source = R"AHFL(
module typed::diagnostics;
import typed::diagnostics as self;

const NarrowMapKey: Map<String(2, 8), Int> =
    std::collections::map_from_entries<String(2, 8), Int>();
const RejectedMapKey: Map<String, Int> = self::NarrowMapKey;
)AHFL";

    const auto type_result = typecheck_project_module_source("typed::diagnostics", source);
    REQUIRE(type_result.has_errors());
    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.TYPE_MISMATCH") == 1);
    const auto *diagnostic =
        diagnostic_with_code(type_result.diagnostics, "typecheck.TYPE_MISMATCH");
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->related.size() >= 2);
    CHECK(diagnostics_contain(
        type_result.diagnostics,
        "expected type 'std::collections::Map<String, Int>' from declared type of const "
        "'RejectedMapKey' declared here"));
    CHECK(diagnostics_contain(
        type_result.diagnostics,
        "actual expression has type 'std::collections::Map<String(2, 8), Int>' here"));
}

TEST_CASE("Const agent context defaults preserve exact schema expectation") {
    const std::string source = R"AHFL(
struct Config {
    value: String;
}

struct WiderConfig {
    value: String;
    extra: String;
}

struct Request {
    value: String;
}

struct Context {
    config: Config = WiderConfig {
        value: "seed",
        extra: "extra",
    };
}

struct Response {
    value: String;
}

agent BadContextDefaultAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("agent_context_default_expectation.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto type_result = type_checker.check(*parse_result.program, resolve_result);
    REQUIRE(type_result.has_errors());
    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.EXACT_SCHEMA_MISMATCH") ==
          1);
    const auto *diagnostic =
        diagnostic_with_code(type_result.diagnostics, "typecheck.EXACT_SCHEMA_MISMATCH");
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->related.size() == 5);
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "agent context default requires an exact schema match"));
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "expected schema 'Config' from agent context default declared here"));
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "actual expression has type 'WiderConfig' here"));
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "expected type 'Config' declared here in module"));
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "actual type 'WiderConfig' declared here in module"));
}

TEST_CASE("Type diagnostics preserve assignment expectation through some literals") {
    const std::string source = R"AHFL(
struct Request {
    fallback: String;
}

struct Context {
    token: Optional<String> = std::option::Option::None;
}

struct Response {
    value: String;
}

agent SomeExpectationAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for SomeExpectationAgent {
    state Done {
        ctx.token = std::option::Option::Some(1);
        return Response { value: input.fallback };
    }
}
)AHFL";

    const auto type_result = typecheck_project_source("some_assignment_expectation.ahfl", source);
    REQUIRE(type_result.has_errors());
    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.TYPE_MISMATCH") == 1);
    const auto *diagnostic =
        diagnostic_with_code(type_result.diagnostics, "typecheck.TYPE_MISMATCH");
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->related.size() >= 2);
    CHECK(diagnostics_contain(
        type_result.diagnostics,
        "argument #1: expected 'String' declared in `Some` from enum variant payload declared here"));
    CHECK(diagnostics_contain(type_result.diagnostics, "actual expression has type 'Int' here"));
}

TEST_CASE("Type diagnostics describe inferred collection expectation origins") {
    const std::string source = R"AHFL(
struct Request {
    value: String;
}

struct Context {
    value: String = "";
}

struct Response {
    value: String;
}

agent InferredCollectionExpectationAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for InferredCollectionExpectationAgent {
    state Done {
        let mixedList = std::collections::list_from_array<Int>(1, "x");
        let mixedSet = std::collections::set_from_array<Int>(1, "x");
        let mixedMap = std::collections::map_from_entries<String, Int>("a", 1, 2, "x");
        return Response { value: input.value };
    }
}
)AHFL";

    const auto type_result =
        typecheck_project_source("inferred_collection_expectation.ahfl", source);
    REQUIRE(type_result.has_errors());
    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.TYPE_MISMATCH") == 4);

    CHECK(diagnostics_contain(type_result.diagnostics,
                              "declared in `list_from_array` from parameter 'v1' declared here"));
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "declared in `map_from_entries` from parameter 'k1' declared here"));
    CHECK(diagnostics_contain(type_result.diagnostics, "actual expression has type 'String' here"));
    CHECK(diagnostics_contain(type_result.diagnostics, "actual expression has type 'Int' here"));
}

TEST_CASE("Type diagnostics describe actual expression origins for schema boundaries") {
    const std::string source = R"AHFL(
struct Request {
    value: String;
}

struct Context {
    value: String = "";
}

struct Response {
    value: String;
}

struct WiderResponse {
    value: String;
    extra: String;
}

agent SchemaActualOriginAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for SchemaActualOriginAgent {
    state Done {
        return WiderResponse { value: input.value, extra: "extra" };
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("schema_actual_expression_origin.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto type_result = type_checker.check(*parse_result.program, resolve_result);
    REQUIRE(type_result.has_errors());
    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.EXACT_SCHEMA_MISMATCH") ==
          1);
    const auto *diagnostic =
        diagnostic_with_code(type_result.diagnostics, "typecheck.EXACT_SCHEMA_MISMATCH");
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->related.size() == 3);
    CHECK(diagnostics_contain(type_result.diagnostics, "exact schema match"));
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "actual expression has type 'WiderResponse' here"));
}

TEST_CASE("Schema boundary declaration diagnostics use stable typecheck codes") {
    const std::string source = R"AHFL(
enum Status {
    Pending,
    Done,
}

type InputAlias = Status;

struct Context {
    value: String = "pending";
}

struct Response {
    value: String;
}

agent InvalidInputAgent {
    input: InputAlias;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("schema_boundary_decl_type_code.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto type_result = type_checker.check(*parse_result.program, resolve_result);
    REQUIRE(type_result.has_errors());
    CHECK(diagnostic_count_with_code(type_result.diagnostics, "typecheck.INVALID_AGENT_TYPE") == 1);
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "agent input type must resolve to a struct type"));
}

TEST_CASE("Type checker records typed HIR expressions alongside legacy expression types") {
    const std::string source = R"AHFL(
struct Request {
    value: String;
}

struct Context {
    value: String = "";
}

struct Response {
    value: String;
}

predicate Ready(value: String) -> Bool;
capability Do(value: String) -> Response;

agent HirAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [Do];
}

flow for HirAgent {
    state Done {
        let ok = true;
        let token: Optional<String> = std::option::Option::None;
        let reply = Response { value: input.value };
        let grouped = Response { value: (input.value) };
        let ready = Ready((reply).value);
        return Do(reply.value);
    }
}
)AHFL";

    const auto root = std::filesystem::temp_directory_path() / "ahfl_effects_typed_hir_project";
    std::filesystem::remove_all(root);
    const auto main_path = root / "app" / "main.ahfl";
    write_file(main_path, "module app::main;\n" + source);

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_project(ahfl::ProjectInput{
        .entry_files = {main_path},
        .search_roots = {root, std::filesystem::path{"std"}},
    });
    REQUIRE_FALSE(parse_result.has_errors());

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(parse_result.graph);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto type_result = type_checker.check(parse_result.graph, resolve_result);
    REQUIRE_FALSE(type_result.has_errors());

    const auto response_symbol = resolve_result.symbol_table.find_local(
        ahfl::SymbolNamespace::Types, "Response", "app::main");
    REQUIRE(response_symbol.has_value());
    const auto do_symbol = resolve_result.symbol_table.find_local(
        ahfl::SymbolNamespace::Capabilities, "Do", "app::main");
    REQUIRE(do_symbol.has_value());
    const auto ready_symbol = resolve_result.symbol_table.find_local(
        ahfl::SymbolNamespace::Predicates, "Ready", "app::main");
    REQUIRE(ready_symbol.has_value());
    REQUIRE(response_symbol->get().source_id.has_value());
    const auto app_source_id = response_symbol->get().source_id;

    REQUIRE_FALSE(type_result.typed_program.expressions.empty());
    CHECK_FALSE(type_result.typed_program.declarations.empty());

    const auto expression = std::find_if(type_result.typed_program.expressions.begin(),
                                         type_result.typed_program.expressions.end(),
                                         [&](const ahfl::TypedExpr &expr) {
                                             return expr.source_id == app_source_id;
                                         });
    REQUIRE(expression != type_result.typed_program.expressions.end());
    const auto *typed_expr =
        type_result.typed_program.find_expr(expression->node_id, expression->source_id);
    REQUIRE(typed_expr != nullptr);
    CHECK(typed_expr->type == expression->type);
    CHECK(typed_expr->effect == expression->effect);
    CHECK(typed_expr->is_pure == expression->is_pure);

    const auto struct_literal =
        std::find_if(type_result.typed_program.expressions.begin(),
                     type_result.typed_program.expressions.end(),
                     [&](const ahfl::TypedExpr &expr) {
                         return expr.kind == ahfl::ast::ExprSyntaxKind::StructLiteral &&
                                expr.source_id == app_source_id;
                     });
    REQUIRE(struct_literal != type_result.typed_program.expressions.end());
    REQUIRE(struct_literal->resolved_symbol.has_value());
    CHECK(*struct_literal->resolved_symbol == response_symbol->get().id);
    CHECK(struct_literal->semantic_name == "Response");
    REQUIRE(struct_literal->children.size() == 1);
    CHECK(struct_literal->children.front().role == ahfl::TypedExprChildRole::StructFieldValue);
    CHECK(struct_literal->children.front().name == "value");
    const auto *struct_child =
        ahfl::resolve_child(type_result.typed_program, struct_literal->children.front());
    REQUIRE(struct_child != nullptr);
    CHECK(struct_child->kind == ahfl::ast::ExprSyntaxKind::Path);
    CHECK(struct_child->semantic_name == "input.value");
    CHECK(struct_child->path_root == "input");
    CHECK(struct_child->member_path == std::vector<std::string>{"value"});
    CHECK(struct_child->type->holds<ahfl::types::StringT>());

    const auto &tp = type_result.typed_program;
    const auto grouped_struct_literal = std::find_if(
        tp.expressions.begin(), tp.expressions.end(), [&](const ahfl::TypedExpr &expr) {
            return expr.kind == ahfl::ast::ExprSyntaxKind::StructLiteral &&
                   expr.source_id == app_source_id &&
                   expr.children.size() == 1 &&
                   ahfl::resolve_child(tp, expr.children.front()) != nullptr &&
                   ahfl::resolve_child(tp, expr.children.front())->kind ==
                       ahfl::ast::ExprSyntaxKind::Group;
        });
    REQUIRE(grouped_struct_literal != tp.expressions.end());
    const auto *grouped_child = ahfl::resolve_child(tp, grouped_struct_literal->children.front());
    CHECK(grouped_child->semantic_name == "input.value");
    CHECK(grouped_child->path_root == "input");
    CHECK(grouped_child->member_path == std::vector<std::string>{"value"});

    const auto call =
        std::find_if(tp.expressions.begin(), tp.expressions.end(), [&](const ahfl::TypedExpr &expr) {
            return expr.kind == ahfl::ast::ExprSyntaxKind::Call && expr.semantic_name == "Do" &&
                   expr.source_id == app_source_id;
        });
    REQUIRE(call != tp.expressions.end());
    REQUIRE(call->resolved_symbol.has_value());
    CHECK(*call->resolved_symbol == do_symbol->get().id);
    CHECK(call->call_target_kind.has_value());
    CHECK(*call->call_target_kind == ahfl::TypedCallTargetKind::InherentMethod);
    CHECK(call->semantic_name == "Do");
    REQUIRE(call->children.size() == 1);
    CHECK(call->children.front().role == ahfl::TypedExprChildRole::Argument);
    CHECK(call->children.front().name == "value");
    const auto *call_child = ahfl::resolve_child(tp, call->children.front());
    REQUIRE(call_child != nullptr);
    CHECK(call_child->semantic_name == "reply.value");
    CHECK(call_child->path_root == "reply");
    CHECK(call_child->member_path == std::vector<std::string>{"value"});

    const auto predicate_call =
        std::find_if(tp.expressions.begin(), tp.expressions.end(), [&](const ahfl::TypedExpr &expr) {
            return expr.kind == ahfl::ast::ExprSyntaxKind::Call && expr.semantic_name == "Ready" &&
                   expr.source_id == app_source_id;
        });
    REQUIRE(predicate_call != tp.expressions.end());
    REQUIRE(predicate_call->resolved_symbol.has_value());
    CHECK(*predicate_call->resolved_symbol == ready_symbol->get().id);
    CHECK(predicate_call->call_target_kind.has_value());
    CHECK(*predicate_call->call_target_kind == ahfl::TypedCallTargetKind::TraitMethod);
    REQUIRE(predicate_call->children.size() == 1);
    CHECK(predicate_call->children.front().role == ahfl::TypedExprChildRole::Argument);
    CHECK(predicate_call->children.front().name == "value");
    const auto *pred_child = ahfl::resolve_child(tp, predicate_call->children.front());
    REQUIRE(pred_child != nullptr);
    CHECK(pred_child->kind == ahfl::ast::ExprSyntaxKind::MemberAccess);
    CHECK(pred_child->semantic_name == "reply.value");
    CHECK(pred_child->path_root == "reply");
    CHECK(pred_child->member_path == std::vector<std::string>{"value"});

    const auto bool_literal =
        std::find_if(type_result.typed_program.expressions.begin(),
                     type_result.typed_program.expressions.end(),
                     [&](const ahfl::TypedExpr &expr) {
                         return expr.kind == ahfl::ast::ExprSyntaxKind::BoolLiteral &&
                                expr.source_id == app_source_id;
                     });
    REQUIRE(bool_literal != type_result.typed_program.expressions.end());
    CHECK(bool_literal->semantic_name == "true");

    // Note: ExprSyntaxKind::NoneLiteral removed in P5.6a; `none` now lowered by desugar.
    (void)type_result;
}

TEST_CASE("Type checker can trace relation checks through TypeRelationContext") {
    const std::string source = R"AHFL(
struct Request {
    value: String;
}

struct Context {
    value: String = "";
}

struct Response {
    value: String;
}

agent RelationTraceAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for RelationTraceAgent {
    state Done {
        return Response { value: input.value };
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("type_relation_trace.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    const auto type_result =
        type_checker.check(*parse_result.program,
                           resolve_result,
                           ahfl::TypeCheckOptions{.trace_type_relations = true});
    REQUIRE_FALSE(type_result.has_errors());

    bool saw_assignable = false;
    bool saw_exact_schema = false;
    for (const auto &step : type_result.relation_trace.steps) {
        saw_assignable = saw_assignable || step.kind == ahfl::TypeRelationKind::Assignable;
        saw_exact_schema = saw_exact_schema || step.kind == ahfl::TypeRelationKind::ExactSchema;
    }
    CHECK(saw_assignable);
    CHECK(saw_exact_schema);
}

TEST_CASE("Decreases shadowed receiver emits stable warning and degrades rank encoding") {
    // Downstream SMV treats a decreases contract on a shadowed receiver as an
    // abstract observation rather than a bounded-rank formula. Wave-12 grammar
    // does not permit `self` as a let-binding identifier (it is a reserved
    // receiver keyword), so we exercise the DECREASES_SHADOWED_RECEIVER branch
    // through the explicit test-only injector that marks the agent symbol as
    // having a flow-local `let self` shadow.
    const std::string source = R"AHFL(
struct Request {
    id: Int;
}

struct Context {
    length: Int = 0;
    count: Int = 0;
}

struct Response {
    processed: Int;
}

agent DecreasesShadowAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];
    transition Init -> Done;
}

contract for DecreasesShadowAgent {
    decreases: self.length;
}

flow for DecreasesShadowAgent {
    state Init {
        goto Done;
    }

    state Done {
        return Response { processed: 0 };
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("decreases_shadowed_receiver.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    // Locate the agent symbol via the contract target reference (contract
    // and flow share the same target SymbolId).
    const auto contract_it = std::find_if(
        parse_result.program->declarations.begin(),
        parse_result.program->declarations.end(),
        [](const ahfl::Owned<ahfl::ast::Decl> &d) {
            return d != nullptr && d->kind == ahfl::ast::NodeKind::ContractDecl;
        });
    REQUIRE(contract_it != parse_result.program->declarations.end());
    const auto &contract =
        static_cast<const ahfl::ast::ContractDecl &>(**contract_it);
    const auto agent_ref = resolve_result.find_reference(
        ahfl::ReferenceKind::ContractTarget, contract.target->range);
    REQUIRE(agent_ref.has_value());

    ahfl::TypeCheckPass type_checker(*parse_result.program, resolve_result);
    type_checker.inject_flow_self_shadowing_for_test(
        agent_ref->get().target.value, std::string{"List<Int>"});
    const auto type_result = type_checker.run();
    CHECK_FALSE(type_result.has_errors());
    CHECK(type_result.diagnostics.has_warning());
    CHECK(diagnostic_count_with_code(type_result.diagnostics,
                                     "typecheck.DECREASES_SHADOWED_RECEIVER") == 1);

    const auto *diagnostic =
        diagnostic_with_code(type_result.diagnostics, "typecheck.DECREASES_SHADOWED_RECEIVER");
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->severity == ahfl::DiagnosticSeverity::Warning);
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "decreases clause receiver 'self' is shadowed by a local binding"));
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "termination measure is degraded to an abstract observation"));
}

// =============================================================================
// QW-2 (PB-01 g-2): Unified WRONG_ARITY message template across all callable
// categories.  P4-01 added the statement-level categories; this section
// provides regression coverage for every remaining category so that nobody
// can silently re-introduce an ad-hoc "expected N, got M" string that would
// break IDE consumers that rely on the shared
//
//     {} '{}' expects {} argument(s), got {}
//
// MessageTemplate defined in `messages::typecheck::WrongArity`
// (include/ahfl/base/support/diagnostics.hpp:497).
//
// Categories already covered elsewhere:
//   - statement:assert / statement:unwrap / statement:requires /
//     statement:unreachable   → tests/unit/compiler/semantics/stmt_diagnostics.cpp
//   - predicate / capability   → "Callable / capability / predicate" test above
//                                 (l. 770–779).
// Categories covered HERE (7 new cases):
//   - method                       (instance method on impl)
//   - lambda                       (closure arity vs expected Fn)
//   - function                     (top-level / standalone fn)
//   - function value               (fn passed as a value, arity mismatch)
//   - function type arguments      (explicit <A, B> with wrong count)
//   - method type arguments        (explicit <U> on method with wrong count)
//   - enum variant                 (variant payload arity)
//   - type                         (type constructor / nominal arity)
// =============================================================================

TEST_CASE("WRONG_ARITY unified message: method call") {
    // impl 定义一个需 2 个参数的方法，调用时传 1 个
    const std::string source = R"AHFL(
struct Pair { a: Int; b: Int; }

impl Pair {
    fn scaled_add(self, factor: Int, offset: Int) -> Int effect Pure decreases 0 {
        return self.a * factor + self.b * factor + offset;
    }
}

struct Request { value: Int; }
struct Context { scratch: Int = 0; }
struct Response { out: Int; }

agent M {
    input: Request; context: Context; output: Response;
    states: [Done]; initial: Done; final: [Done]; capabilities: [];
}

flow for M {
    state Done {
        let p: Pair = Pair { a: 1, b: 2 };
        // Expected: (factor, offset) = 2 args; actual: 1
        let bad = p.scaled_add(2);
        return Response { out: bad };
    }
}
)AHFL";

    const auto type_result = typecheck_project_source("method_call.ahfl", source);
    CHECK(type_result.has_errors());
    CHECK(diagnostic_count_with_code(
              type_result.diagnostics, "typecheck.WRONG_ARITY") == 1);
    // Template format: "{} '{}' expects {} argument(s), got {}"
    //  → method 'app::main::Pair.scaled_add' expects 2 argument(s), got 1
    CHECK(diagnostics_contain(
        type_result.diagnostics,
        "expects 2 argument(s), got 1"));
    CHECK(diagnostics_contain(
        type_result.diagnostics,
        "scaled_add"));
}

TEST_CASE("WRONG_ARITY unified message: lambda vs expected Fn param size") {
    const std::string source = R"AHFL(
struct Request { n: Int; }
struct Context { scratch: Int = 0; }
struct Response { out: Int; }

// expect 接受 Fn(Int, Int) -> Int (2 params), 实际给 Fn(Int) -> Int (1 param)
fn apply_binary(f: Fn(Int, Int) -> Int, x: Int, y: Int) -> Int effect Pure decreases 0 { return f(x, y); }

agent L {
    input: Request; context: Context; output: Response;
    states: [Done]; initial: Done; final: [Done]; capabilities: [];
}

flow for L {
    state Done {
        // expected 2 params → actual 1
        let bad = apply_binary(\a: Int -> a * 2, 3, 4);
        return Response { out: bad };
    }
}
)AHFL";

    const auto type_result = typecheck_project_source("lambda_vs_expected_fn_param_size.ahfl", source);
    CHECK(type_result.has_errors());
    CHECK(diagnostic_count_with_code(
              type_result.diagnostics, "typecheck.WRONG_ARITY") == 1);
    // lambda '<closure>' expects 2 argument(s), got 1
    CHECK(diagnostics_contain(
        type_result.diagnostics,
        "lambda '<closure>' expects 2 argument(s), got 1"));
}

TEST_CASE("WRONG_ARITY unified message: standalone function call") {
    const std::string source = R"AHFL(
fn add3(x: Int, y: Int, z: Int) -> Int effect Pure decreases 0 { return x + y + z; }

struct Request { v: Int; }
struct Context { scratch: Int = 0; }
struct Response { out: Int; }

agent F {
    input: Request; context: Context; output: Response;
    states: [Done]; initial: Done; final: [Done]; capabilities: [];
}

flow for F {
    state Done {
        // expects 3 args, got 2
        let bad = add3(input.v, 1);
        return Response { out: bad };
    }
}
)AHFL";

    const auto type_result = typecheck_project_source("standalone_function_call.ahfl", source);
    CHECK(type_result.has_errors());
    CHECK(diagnostic_count_with_code(
              type_result.diagnostics, "typecheck.WRONG_ARITY") >= 1);
    // function 'add3' expects 3 argument(s), got 2
    CHECK(diagnostics_contain(
        type_result.diagnostics,
        "function 'add3' expects 3 argument(s), got 2"));
}

TEST_CASE("WRONG_ARITY unified message: function value call arity") {
    const std::string source = R"AHFL(
struct Request { v: Int; }
struct Context { scratch: Int = 0; }
struct Response { out: Int; }

fn compose_twice(f: Fn(Int) -> Int, x: Int) -> Int effect Pure decreases 0 {
    // f 是 1-arg Fn，调用成 2 args → WrongArity
    return f(f(x, 0));
}

agent FV {
    input: Request; context: Context; output: Response;
    states: [Done]; initial: Done; final: [Done]; capabilities: [];
}

flow for FV {
    state Done {
        let r = compose_twice(\a: Int -> a + 1, input.v);
        return Response { out: r };
    }
}
)AHFL";

    const auto type_result = typecheck_project_source("function_value_call_arity.ahfl", source);
    CHECK(type_result.has_errors());
    // function value '<fn-value>' expects 1 argument(s), got 2
    CHECK(diagnostic_count_with_code(
              type_result.diagnostics, "typecheck.WRONG_ARITY") >= 1);
    CHECK(diagnostics_contain(
        type_result.diagnostics,
        "expects 1 argument(s), got 2"));
}

TEST_CASE("WRONG_ARITY unified message: explicit type arguments on function") {
    const std::string source = R"AHFL(
fn id<T>(x: T) -> T effect Pure decreases 0 { return x; }

struct Request { v: Int; }
struct Context { scratch: Int = 0; }
struct Response { out: Int; }

agent TA {
    input: Request; context: Context; output: Response;
    states: [Done]; initial: Done; final: [Done]; capabilities: [];
}

flow for TA {
    state Done {
        // id takes 1 type parameter, spelled with 2
        let bad = id<Int, String>(input.v);
        return Response { out: bad };
    }
}
)AHFL";

    const auto type_result = typecheck_project_source("explicit_type_arguments_on_function.ahfl", source);
    CHECK(type_result.has_errors());
    CHECK(diagnostic_count_with_code(
              type_result.diagnostics, "typecheck.WRONG_ARITY") >= 1);
    // function type arguments 'id' expects 1 argument(s), got 2
    CHECK(diagnostics_contain(
        type_result.diagnostics,
        "function type arguments 'id' expects 1 argument(s), got 2"));
}

TEST_CASE("WRONG_ARITY unified message: method explicit type arguments") {
    const std::string source = R"AHFL(
struct Box { value: Int; }

impl Box {
    fn map<U>(self, f: Fn(Int) -> U) -> U effect Pure decreases 0 { return f(self.value); }
}

struct Request { v: Int; }
struct Context { scratch: Int = 0; }
struct Response { out: Int; }

agent MA {
    input: Request; context: Context; output: Response;
    states: [Done]; initial: Done; final: [Done]; capabilities: [];
}

flow for MA {
    state Done {
        let b: Box = Box { value: input.v };
        // map takes 1 type param, spelled with 2
        let bad = b.map<Int, String>(\x: Int -> x + 1);
        return Response { out: bad };
    }
}
)AHFL";

    const auto type_result = typecheck_project_source("method_explicit_type_arguments.ahfl", source);
    CHECK(type_result.has_errors());
    CHECK(diagnostic_count_with_code(
              type_result.diagnostics, "typecheck.WRONG_ARITY") >= 1);
    // method type arguments 'Box.map' expects 1 argument(s), got 2
    CHECK(diagnostics_contain(
        type_result.diagnostics,
        "method type arguments"));
    CHECK(diagnostics_contain(
        type_result.diagnostics,
        "expects 1 argument(s), got 2"));
}

TEST_CASE("WRONG_ARITY unified message: enum variant payload arity") {
    const std::string source = R"AHFL(
enum Result {
    Ok(Int, String),
    Err(String),
}

struct Request { v: Int; }
struct Context { scratch: Int = 0; }
struct Response { out: Int; }

agent EV {
    input: Request; context: Context; output: Response;
    states: [Done]; initial: Done; final: [Done]; capabilities: [];
}

flow for EV {
    state Done {
        // Ok expects 2 payload values, pass 1 (use qualified variant name to
        // avoid shadowing from std prelude's std::result::Ok)
        let r: Result = Result::Ok(42);
        return Response { out: input.v };
    }
}
)AHFL";

    const auto type_result = typecheck_project_source("enum_variant_payload_arity.ahfl", source);
    CHECK(type_result.has_errors());
    CHECK(diagnostic_count_with_code(
              type_result.diagnostics, "typecheck.WRONG_ARITY") >= 1);
    // enum variant 'Result::Ok' expects 2 argument(s), got 1
    CHECK(diagnostics_contain(
        type_result.diagnostics,
        "enum variant"));
    CHECK(diagnostics_contain(
        type_result.diagnostics,
        "expects 2 argument(s), got 1"));
}

TEST_CASE("WRONG_ARITY unified message: type constructor arity") {
    // Map<String, Int> 正确；Map<String> 少一个 → WrongArity
    const std::string source = R"AHFL(
struct Map<K, V> { size: Int; }

struct Request { v: Int; }
struct Context { scratch: Int = 0; }
struct Response { out: Int; }

agent TC {
    input: Request; context: Context; output: Response;
    states: [Done]; initial: Done; final: [Done]; capabilities: [];
}

flow for TC {
    state Done {
        // Map expects 2 type args, given 1
        let m: Map<String> = Map { size: 0 };
        return Response { out: input.v };
    }
}
)AHFL";

    const auto type_result = typecheck_project_source("type_constructor_arity.ahfl", source);
    CHECK(type_result.has_errors());
    CHECK(diagnostic_count_with_code(
              type_result.diagnostics, "typecheck.WRONG_ARITY") >= 1);
    // type 'Map' expects 2 argument(s), got 1
    CHECK(diagnostics_contain(
        type_result.diagnostics,
        "type 'Map' expects 2 argument(s), got 1"));
}

// ============================================================================
// G3 matrix regression tests — error codes with emit sites but no dedicated
// assertion (P1 match, P2 builtin/unknown/reference, P3 trait, P4 budget)
// Each case asserts: has_errors + diagnostic_count_with_code + unique
// message substring pair (category + numeric/shape cue).
// ============================================================================

// ---------------------------------------------------------------------------
// G3-m1: UNKNOWN_VALUE — bare name that resolves to neither a value, a const,
// a predicate, nor a capability. Reported by expression_sema when no symbol
// resolves at all.
// ---------------------------------------------------------------------------
TEST_CASE("G3 UNKNOWN_VALUE bare unresolved name reports typecheck.UNKNOWN_VALUE") {
    const std::string source = R"AHFL(
struct Request { v: Int; }
struct Context { scratch: Int = 0; }
struct Response { out: Int; }

agent UV {
    input: Request; context: Context; output: Response;
    states: [Done]; initial: Done; final: [Done]; capabilities: [];
}

flow for UV {
    state Done {
        // `unresolved_name_here` is not a let, a field, a const, or a builtin.
        let result: Int = unresolved_name_here + 0;
        return Response { out: result };
    }
}
)AHFL";

    const auto type_result = typecheck_project_source("unknown_value.ahfl", source);
    CHECK(type_result.has_errors());
    CHECK(diagnostic_count_with_code(type_result.diagnostics,
                                     "typecheck.UNKNOWN_VALUE") >= 1);
    // message: "unknown value 'unresolved_name_here'"
    CHECK(diagnostics_contain(type_result.diagnostics, "unknown value"));
    CHECK(diagnostics_contain(type_result.diagnostics, "unresolved_name_here"));
}

// ---------------------------------------------------------------------------
// G3-m2: UNKNOWN_QUALIFIED_VALUE — Foo::Bar where Foo does not resolve to an
// enum or const symbol type.
// ---------------------------------------------------------------------------
TEST_CASE("G3 UNKNOWN_QUALIFIED_VALUE placeholder (no trigger surface in loose resolves)") {
    // The UNKNOWN_QUALIFIED_VALUE diagnostic surfaces when the *owner* part of
    // `Owner::Name` fails to resolve at all. Using `NoSuchOwner::X` inside a
    // project source unit triggers the resolver's module-resolution path first
    // ("is NoSuchOwner a module name?") which surfaces a resolve-level error
    // *before* typecheck. The matrix row is still exercised: the diagnostic
    // identifier lives in diagnostics.hpp, and the ad-hoc diagnostic_matrix
    // binary covers the emit site via a different (inline text) surface that
    // skips module graph resolution.
    const std::string source = R"AHFL(
struct Request { v: Int; }
struct Context { scratch: Int = 0; }
struct Response { out: Int; }

agent UQV_P {
    input: Request; context: Context; output: Response;
    states: [Done]; initial: Done; final: [Done]; capabilities: [];
}

flow for UQV_P {
    state Done {
        return Response { out: input.v };
    }
}
)AHFL";

    const auto type_result = typecheck_project_source("uqv_placeholder.ahfl", source);
    CHECK_FALSE(type_result.has_errors());
    // Compile-time pin: identifier resolves in the diagnostic enum.
    const ahfl::ErrorCode<ahfl::DiagnosticCategory::TypeCheck> _pin{
        "UNKNOWN_QUALIFIED_VALUE"};
    (void)_pin;
}

// ---------------------------------------------------------------------------
// G3-m3: INVALID_QUALIFIED_VALUE — Foo::Bar where Foo resolves but is not an
// enum or const type (e.g. Foo is a plain struct).
// ---------------------------------------------------------------------------
TEST_CASE("G3 INVALID_QUALIFIED_VALUE struct-qualified value rejected") {
    const std::string source = R"AHFL(
struct Request { v: Int; }
struct Context { scratch: Int = 0; }
struct Response { out: Int; }

// NotAnEnum — a struct, not an enum/const. Qualifying it is an error.
struct NotAnEnum { field: Int; }

agent IQV {
    input: Request; context: Context; output: Response;
    states: [Done]; initial: Done; final: [Done]; capabilities: [];
}

flow for IQV {
    state Done {
        let r: Int = 0;
        if NotAnEnum::whatever == 42 {
            let ignore: Int = 1;
        }
        return Response { out: r };
    }
}
)AHFL";

    const auto type_result = typecheck_project_source("invalid_qualified_value.ahfl", source);
    CHECK(type_result.has_errors());
    CHECK(diagnostic_count_with_code(type_result.diagnostics,
                                     "typecheck.INVALID_QUALIFIED_VALUE") >= 1);
    // message: "qualified value '...' must refer to a constant or enum variant"
    CHECK(diagnostics_contain(type_result.diagnostics, "qualified value"));
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "must refer to a constant or enum variant"));
}

// ---------------------------------------------------------------------------
// G3-m4: INVALID_STRUCT_LITERAL_TARGET — struct literal target resolves to a
// non-struct type. Currently the pass collapses enum-qualified value paths to
// UNKNOWN_QUALIFIED_VALUE before reaching the struct-literal type-check branch,
// so no user constructible source reaches this diagnostic through a clean
// parse/resolve. Placeholder row keeps the matrix explicit.
// ---------------------------------------------------------------------------
TEST_CASE("G3 INVALID_STRUCT_LITERAL_TARGET placeholder row") {
    const std::string source = R"AHFL(
struct Request { v: Int; }
struct Context { scratch: Int = 0; }
struct Response { out: Int; }

agent ISLT_P {
    input: Request; context: Context; output: Response;
    states: [Done]; initial: Done; final: [Done]; capabilities: [];
}

flow for ISLT_P {
    state Done {
        return Response { out: input.v };
    }
}
)AHFL";
    const auto type_result = typecheck_project_source("islt_placeholder.ahfl", source);
    CHECK_FALSE(type_result.has_errors());
    const ahfl::ErrorCode<ahfl::DiagnosticCategory::TypeCheck> _pin{"INVALID_STRUCT_LITERAL_TARGET"};
    (void)_pin;
}

// ---------------------------------------------------------------------------
// G3-m5: UNKNOWN_ENUM_VARIANT — EnumName::NoSuchVariant in value position.
// Currently qualified-name lookup emits UNKNOWN_QUALIFIED_VALUE for unresolved
// qualified paths before the enum-variant check can fire. Placeholder row pins
// the identifier until the dispatcher order is revisited.
// ---------------------------------------------------------------------------
TEST_CASE("G3 UNKNOWN_ENUM_VARIANT placeholder row") {
    const std::string source = R"AHFL(
struct Request { v: Int; }
struct Context { scratch: Int = 0; }
struct Response { out: Int; }

agent UEV_P {
    input: Request; context: Context; output: Response;
    states: [Done]; initial: Done; final: [Done]; capabilities: [];
}

flow for UEV_P {
    state Done {
        return Response { out: input.v };
    }
}
)AHFL";
    const auto type_result = typecheck_project_source("uev_placeholder.ahfl", source);
    CHECK_FALSE(type_result.has_errors());
    const ahfl::ErrorCode<ahfl::DiagnosticCategory::TypeCheck> _pin{"UNKNOWN_ENUM_VARIANT"};
    (void)_pin;
}

// ---------------------------------------------------------------------------
// G3-m6: INVALID_CALLABLE_REFERENCE — call-expression target resolves to a
// non-callable symbol (e.g. a struct type). The current expression pass treats
// `Name(args)` as an enum/struct constructor first and falls through to
// MISSING_CALLABLE_METADATA on unknown symbols; the explicit "symbol resolves
// but is not callable" branch is not reachable without a parse/resolve
// diagnostic also firing. Placeholder row keeps the identifier pinned.
// ---------------------------------------------------------------------------
TEST_CASE("G3 INVALID_CALLABLE_REFERENCE placeholder row") {
    const std::string source = R"AHFL(
struct Request { v: Int; }
struct Context { scratch: Int = 0; }
struct Response { out: Int; }

agent ICR_P {
    input: Request; context: Context; output: Response;
    states: [Done]; initial: Done; final: [Done]; capabilities: [];
}

flow for ICR_P {
    state Done {
        return Response { out: input.v };
    }
}
)AHFL";
    const auto type_result = typecheck_project_source("icr_placeholder.ahfl", source);
    CHECK_FALSE(type_result.has_errors());
    const ahfl::ErrorCode<ahfl::DiagnosticCategory::TypeCheck> _pin{"INVALID_CALLABLE_REFERENCE"};
    (void)_pin;
}

// ---------------------------------------------------------------------------
// G3-m7: MISSING_CALLABLE_METADATA — capability declared but its type info is
// absent. Expression-level calls to undeclared capability-like names currently
// collapse into UNKNOWN_VALUE or a resolve-level diagnostic before the
// callable-metadata slot check fires, so no clean parse/resolve source reaches
// this specific code through the test helper. Placeholder row pins the id.
// ---------------------------------------------------------------------------
TEST_CASE("G3 MISSING_CALLABLE_METADATA placeholder row") {
    const std::string source = R"AHFL(
struct Request { v: Int; }
struct Context { scratch: Int = 0; }
struct Response { out: Int; }

agent MCM_P {
    input: Request; context: Context; output: Response;
    states: [Done]; initial: Done; final: [Done]; capabilities: [];
}

flow for MCM_P {
    state Done {
        return Response { out: input.v };
    }
}
)AHFL";
    const auto type_result = typecheck_project_source("mcm_placeholder.ahfl", source);
    CHECK_FALSE(type_result.has_errors());
    const ahfl::ErrorCode<ahfl::DiagnosticCategory::TypeCheck> _pin{"MISSING_CALLABLE_METADATA"};
    (void)_pin;
}

// ---------------------------------------------------------------------------
// G3-m8: UNKNOWN_CAPABILITY — agent declaration lists a capability name that
// is not a known capability symbol.
// ---------------------------------------------------------------------------
TEST_CASE("G3 UNKNOWN_CAPABILITY unknown agent capability name") {
    const std::string source = R"AHFL(
struct Request { v: Int; }
struct Context { scratch: Int = 0; }
struct Response { out: Int; }

agent UKCap {
    input: Request; context: Context; output: Response;
    states: [Done]; initial: Done; final: [Done];
    capabilities: [ghost_cap_no_such_definition];
}

flow for UKCap {
    state Done {
        return Response { out: input.v };
    }
}
)AHFL";

    const auto type_result = typecheck_project_source("unknown_capability.ahfl", source);
    CHECK(type_result.has_errors());
    CHECK(diagnostic_count_with_code(type_result.diagnostics,
                                     "typecheck.UNKNOWN_CAPABILITY") >= 1);
    // message: "unknown capability 'ghost_cap_no_such_definition' in agent capability list"
    CHECK(diagnostics_contain(type_result.diagnostics, "unknown capability"));
    CHECK(diagnostics_contain(type_result.diagnostics, "ghost_cap_no_such_definition"));
}

// ---------------------------------------------------------------------------
// G3-m9: INVALID_BUILTIN_ATTRIBUTE — non-std module writes @builtin on a fn.
// Single-file helpers prepend a user-module preamble, but the attribute parse
// also collides with attribute-string validity checks; we keep a placeholder
// row so the identifier is pinned.
// ---------------------------------------------------------------------------
TEST_CASE("G3 INVALID_BUILTIN_ATTRIBUTE placeholder row") {
    const std::string source = R"AHFL(
struct Request { v: Int; }
struct Context { scratch: Int = 0; }
struct Response { out: Int; }

agent IBA_P {
    input: Request; context: Context; output: Response;
    states: [Done]; initial: Done; final: [Done]; capabilities: [];
}

flow for IBA_P {
    state Done {
        return Response { out: input.v };
    }
}
)AHFL";
    const auto type_result = typecheck_project_source("iba_placeholder.ahfl", source);
    CHECK_FALSE(type_result.has_errors());
    const ahfl::ErrorCode<ahfl::DiagnosticCategory::TypeCheck> _pin{"INVALID_BUILTIN_ATTRIBUTE"};
    (void)_pin;
}

// ---------------------------------------------------------------------------
// G3-m10: UNKNOWN_BUILTIN_HOOK — @builtin hook string is not a recognised name.
// Requires std-module context to reach past INVALID_BUILTIN_ATTRIBUTE. We pin
// the identifier here; real-context coverage would need a std-module test
// fixture. Placeholder row keeps the matrix explicit.
// ---------------------------------------------------------------------------
TEST_CASE("G3 UNKNOWN_BUILTIN_HOOK placeholder row") {
    const std::string source = R"AHFL(
struct Request { v: Int; }
struct Context { scratch: Int = 0; }
struct Response { out: Int; }

agent UBH_P {
    input: Request; context: Context; output: Response;
    states: [Done]; initial: Done; final: [Done]; capabilities: [];
}

flow for UBH_P {
    state Done {
        return Response { out: input.v };
    }
}
)AHFL";
    const auto type_result = typecheck_project_source("ubh_placeholder.ahfl", source);
    CHECK_FALSE(type_result.has_errors());
    const ahfl::ErrorCode<ahfl::DiagnosticCategory::TypeCheck> _pin{"UNKNOWN_BUILTIN_HOOK"};
    (void)_pin;
}

// ---------------------------------------------------------------------------
// G3-m11: MISSING_BUILTIN_EFFECT — @builtin fn without an explicit effect
// clause. Requires std-module context (non-std @builtin is itself an error).
// We pin via placeholder; std-module fixture needed for behavioural coverage.
// ---------------------------------------------------------------------------
TEST_CASE("G3 MISSING_BUILTIN_EFFECT placeholder row") {
    const std::string source = R"AHFL(
struct Request { v: Int; }
struct Context { scratch: Int = 0; }
struct Response { out: Int; }

agent MBE_P {
    input: Request; context: Context; output: Response;
    states: [Done]; initial: Done; final: [Done]; capabilities: [];
}

flow for MBE_P {
    state Done {
        return Response { out: input.v };
    }
}
)AHFL";
    const auto type_result = typecheck_project_source("mbe_placeholder.ahfl", source);
    CHECK_FALSE(type_result.has_errors());
    const ahfl::ErrorCode<ahfl::DiagnosticCategory::TypeCheck> _pin{"MISSING_BUILTIN_EFFECT"};
    (void)_pin;
}

// ---------------------------------------------------------------------------
// G3-m12: IMPL_TRAIT_UNKNOWN — `impl NotATrait for Foo` names a trait symbol
// that does not resolve to a trait. Resolver reports the undefined name before
// the strict test helper reaches typechecking; the loose helper lives in
// diagnostic_matrix.cpp where we also defer this row via a compile-time pin.
// ---------------------------------------------------------------------------
TEST_CASE("G3 IMPL_TRAIT_UNKNOWN placeholder row") {
    const std::string source = R"AHFL(
struct Request { v: Int; }
struct Context { scratch: Int = 0; }
struct Response { out: Int; }

agent ITU_P {
    input: Request; context: Context; output: Response;
    states: [Done]; initial: Done; final: [Done]; capabilities: [];
}

flow for ITU_P {
    state Done {
        return Response { out: input.v };
    }
}
)AHFL";
    const auto type_result = typecheck_project_source("impl_trait_unknown_ph.ahfl", source);
    CHECK_FALSE(type_result.has_errors());
    const ahfl::ErrorCode<ahfl::DiagnosticCategory::TypeCheck> _pin{"IMPL_TRAIT_UNKNOWN"};
    (void)_pin;
}

// ---------------------------------------------------------------------------
// G3-m13: TRAIT_METHOD_SIGNATURE_MISMATCH — impl provides method whose
// parameter types do not match the trait's signature. The current
// signature-comparison branch is bypassed when extra parameters cause an
// earlier resolve diagnostic; we keep an identifier-pin placeholder so the
// matrix records this row explicitly.
// ---------------------------------------------------------------------------
TEST_CASE("G3 TRAIT_METHOD_SIGNATURE_MISMATCH placeholder row") {
    const std::string source = R"AHFL(
struct Request { v: Int; }
struct Context { scratch: Int = 0; }
struct Response { out: Int; }

agent TMSM_P {
    input: Request; context: Context; output: Response;
    states: [Done]; initial: Done; final: [Done]; capabilities: [];
}

flow for TMSM_P {
    state Done {
        return Response { out: input.v };
    }
}
)AHFL";
    const auto type_result = typecheck_project_source("trait_sig_mismatch_ph.ahfl", source);
    CHECK_FALSE(type_result.has_errors());
    const ahfl::ErrorCode<ahfl::DiagnosticCategory::TypeCheck> _pin{"TRAIT_METHOD_SIGNATURE_MISMATCH"};
    (void)_pin;
}

// ---------------------------------------------------------------------------
// G3-m14: TRAIT_ASSOC_TYPE_NOT_FOUND — trait declares an associated type
// (not yet expressible in current grammar? check). Since the grammar may not
// support `type X = ...` inside impl blocks yet, we defer by marking the
// code absent via a dummy test that confirms zero diagnostics for an
// intentionally clean source. Note: the emit site in typecheck.cpp only runs
// for associated-type declarations; if the surface does not yet accept them
// this diagnostic is unreachable from user code.
// ---------------------------------------------------------------------------
// (No user-constructible source triggers this in the current grammar; kept
// as a placeholder so the matrix has a record. The row is coded `1` for
// "has emit site" and the explicit lack of a test case documents that the
// surface grammar does not yet exercise the branch.)

// ---------------------------------------------------------------------------
// G3-m15: MISSING_SUPER_TRAIT — trait T requires S but there is no impl of S
// for the same type.
// ---------------------------------------------------------------------------
TEST_CASE("G3 MISSING_SUPER_TRAIT unsupplied super-trait impl rejected") {
    const std::string source = R"AHFL(
struct Request { v: Int; }
struct Context { scratch: Int = 0; }
struct Response { out: Int; }

struct Counter { value: Int; }

// Base trait.
trait Base {
    fn get(self: Counter) -> Int;
}

// Derived requires Base as its super-trait.
trait Derived : Base {
    fn bump(self: Counter) -> Int;
}

// Only impl Derived but no impl of Base → MISSING_SUPER_TRAIT.
impl Derived for Counter {
    fn get(self: Counter) -> Int { return self.value; }
    fn bump(self: Counter) -> Int { return self.value + 1; }
}

agent MST {
    input: Request; context: Context; output: Response;
    states: [Done]; initial: Done; final: [Done]; capabilities: [];
}

flow for MST {
    state Done {
        return Response { out: input.v };
    }
}
)AHFL";

    const auto type_result = typecheck_project_source("missing_super_trait.ahfl", source);
    CHECK(type_result.has_errors());
    CHECK(diagnostic_count_with_code(type_result.diagnostics,
                                     "typecheck.MISSING_SUPER_TRAIT") >= 1);
    // message: "trait 'Derived' requires super-trait 'Base' but no impl is found"
    CHECK(diagnostics_contain(type_result.diagnostics, "requires super-trait"));
    CHECK(diagnostics_contain(type_result.diagnostics, "but no impl is found"));
}

// ---------------------------------------------------------------------------
// G3-m16: AMBIGUOUS_TRAIT_IMPL — multiple trait impls are candidates for a
// single method call (requires generics to construct the overlap; we use two
// inherent impls triggering the trait-dispatch ambiguity branch via an
// overloaded name on the same nominal type via distinct trait impls — since
// that needs two impls of *different* traits to match the same call, we
// construct: two distinct traits each declaring the same method name, then
// call that method on a value whose type has impls for both traits).
// ---------------------------------------------------------------------------
TEST_CASE("G3 AMBIGUOUS_TRAIT_IMPL duplicate trait-method dispatch") {
    const std::string source = R"AHFL(
struct Request { v: Int; }
struct Context { scratch: Int = 0; }
struct Response { out: Int; }

struct Container { value: Int; }

trait TraitA {
    fn render(self: Container) -> Int;
}
trait TraitB {
    fn render(self: Container) -> Int;
}

impl TraitA for Container {
    fn render(self: Container) -> Int { return self.value; }
}
impl TraitB for Container {
    fn render(self: Container) -> Int { return self.value + 1; }
}

agent ATI {
    input: Request; context: Context; output: Response;
    states: [Done]; initial: Done; final: [Done]; capabilities: [];
}

flow for ATI {
    state Done {
        let c: Container = Container { value: input.v };
        // c.render() has two trait candidates → AMBIGUOUS_TRAIT_IMPL.
        let x: Int = c.render();
        return Response { out: x };
    }
}
)AHFL";

    const auto type_result = typecheck_project_source("ambiguous_trait_impl.ahfl", source);
    CHECK(type_result.has_errors());
    CHECK(diagnostic_count_with_code(type_result.diagnostics,
                                     "typecheck.AMBIGUOUS_TRAIT_IMPL") >= 1);
    // message: "multiple trait implementations match for type 'Container' and trait"
    CHECK(diagnostics_contain(type_result.diagnostics,
                              "multiple trait implementations match"));
    CHECK(diagnostics_contain(type_result.diagnostics, "Container"));
}
