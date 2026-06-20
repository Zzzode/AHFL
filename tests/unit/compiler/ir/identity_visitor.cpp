#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/base/support/ownership.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/handoff/package.hpp"
#include "ahfl/compiler/ir/identity.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/ir/program_view.hpp"
#include "ahfl/compiler/ir/typed_hir_lower.hpp"
#include "ahfl/compiler/ir/visitor.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/type_context.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"

#include <string>

namespace {

[[nodiscard]] ahfl::Owned<ahfl::ir::Expr> make_call_expr(std::string callee) {
    return ahfl::make_owned<ahfl::ir::Expr>(ahfl::ir::Expr{
        .node =
            ahfl::ir::CallExpr{
                .callee = std::move(callee),
                .arguments = {},
            },
        .source_range = {},
    });
}

[[nodiscard]] ahfl::ir::TypeRef type_ref(ahfl::ir::TypeRefKind kind,
                                         std::string display_name,
                                         std::string canonical_name = {}) {
    return ahfl::ir::TypeRef{
        .kind = kind,
        .display_name = std::move(display_name),
        .canonical_name = std::move(canonical_name),
        .string_bounds = {},
        .decimal_scale = {},
        .first = nullptr,
        .second = nullptr,
    };
}

class CountingVisitor final : public ahfl::ir::ProgramVisitor {
  public:
    int expression_count{0};
    int workflow_node_count{0};

  private:
    void on_expr(const ahfl::ir::Expr & /*expr*/) override {
        expression_count += 1;
    }
    void on_workflow_node(const ahfl::ir::WorkflowNode & /*node*/) override {
        workflow_node_count += 1;
    }
};

class RenameCallRewriter final : public ahfl::ir::ProgramRewriter {
  private:
    bool on_expr(ahfl::ir::Expr &expr) override {
        auto *call = std::get_if<ahfl::ir::CallExpr>(&expr.node);
        if (call == nullptr || call->callee == "pkg::CanonicalCall") {
            return false;
        }

        call->callee = "pkg::CanonicalCall";
        return true;
    }
};

} // namespace

TEST_CASE("IR identity helpers prefer structured canonical references") {
    const ahfl::ir::SymbolRef symbol{
        .kind = ahfl::ir::SymbolRefKind::Agent,
        .canonical_name = "pkg::Agent",
        .local_name = "Agent",
        .module_name = "pkg",
    };
    CHECK(ahfl::ir::has_resolved_symbol(symbol));
    CHECK(ahfl::ir::symbol_canonical_name(symbol, "Fallback") == "pkg::Agent");
    CHECK(ahfl::ir::symbol_display_name(symbol, "Fallback") == "Agent");

    const auto type = type_ref(ahfl::ir::TypeRefKind::Struct, "Request", "pkg::Request");
    CHECK(ahfl::ir::has_resolved_type(type));
    CHECK(ahfl::ir::type_canonical_name(type, "Fallback") == "pkg::Request");
    CHECK(ahfl::ir::type_display_name(type, "Fallback") == "Request");
}

TEST_CASE("ProgramIndex and handoff lowering use SymbolRef canonical names") {
    ahfl::ir::Program program;
    program.declarations.push_back(ahfl::ir::AgentDecl{
        .provenance = {},
        .name = "LocalAgent",
        .states = {"Init", "Done"},
        .initial_state = "Init",
        .final_states = {"Done"},
        .quota = {},
        .transitions = {},
        .input_type_ref =
            type_ref(ahfl::ir::TypeRefKind::Struct, "LocalRequest", "pkg::Request"),
        .context_type_ref = type_ref(ahfl::ir::TypeRefKind::Unit, "Unit"),
        .output_type_ref =
            type_ref(ahfl::ir::TypeRefKind::Struct, "LocalResponse", "pkg::Response"),
        .capability_refs =
            {
                ahfl::ir::SymbolRef{
                    .kind = ahfl::ir::SymbolRefKind::Capability,
                    .canonical_name = "pkg::CanonicalCall",
                    .local_name = "LocalCall",
                    .module_name = "pkg",
                },
            },
        .symbol_ref =
            ahfl::ir::SymbolRef{
                .kind = ahfl::ir::SymbolRefKind::Agent,
                .canonical_name = "pkg::Agent",
                .local_name = "LocalAgent",
                .module_name = "pkg",
            },
    });

    const ahfl::ir::ProgramIndex index(program);
    REQUIRE(index.find_agent("pkg::Agent") != nullptr);
    CHECK(index.find_agent("LocalAgent") == nullptr);

    const auto package = ahfl::handoff::lower_package(program);
    const auto *agent = ahfl::handoff::find_agent_executable(package, "pkg::Agent");
    REQUIRE(agent != nullptr);
    CHECK(agent->input_type == "pkg::Request");
    CHECK(agent->output_type == "pkg::Response");
    REQUIRE(agent->capabilities.size() == 1);
    CHECK(agent->capabilities[0] == "pkg::CanonicalCall");
}

TEST_CASE("IR visitor and rewriter traverse workflow node inputs") {
    ahfl::ir::WorkflowDecl workflow{
        .provenance = {},
        .name = "pkg::Workflow",
        .nodes = {},
        .safety = {},
        .liveness = {},
        .return_value = make_call_expr("ReturnCall"),
        .input_type_ref = type_ref(ahfl::ir::TypeRefKind::Unit, "Unit"),
        .output_type_ref = type_ref(ahfl::ir::TypeRefKind::Unit, "Unit"),
        .symbol_ref = {},
    };
    workflow.nodes.push_back(ahfl::ir::WorkflowNode{
        .name = "run",
        .input = make_call_expr("LocalCall"),
        .after = {},
        .target_ref =
            ahfl::ir::SymbolRef{
                .kind = ahfl::ir::SymbolRefKind::Agent,
                .canonical_name = "pkg::Agent",
                .local_name = "Agent",
                .module_name = "pkg",
            },
        .source_range = {},
    });

    ahfl::ir::Program program;
    program.declarations.push_back(std::move(workflow));

    CountingVisitor visitor;
    visitor.visit(program);
    CHECK(visitor.workflow_node_count == 1);
    CHECK(visitor.expression_count == 2);

    RenameCallRewriter rewriter;
    CHECK(rewriter.rewrite(program));

    const auto *rewritten = std::get_if<ahfl::ir::WorkflowDecl>(&program.declarations.front());
    REQUIRE(rewritten != nullptr);
    const auto *node_call = std::get_if<ahfl::ir::CallExpr>(&rewritten->nodes.front().input->node);
    REQUIRE(node_call != nullptr);
    CHECK(node_call->callee == "pkg::CanonicalCall");
}

TEST_CASE("Typed HIR lowering matches AST lowering for checked programs") {
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

agent HirAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for HirAgent {
    state Done {
        let reply = Response { value: input.value };
        return reply;
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("typed_hir_lowering.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker checker;
    const auto type_result = checker.check(*parse_result.program, resolve_result);
    REQUIRE_FALSE(type_result.has_errors());

    const auto ast_ir = ahfl::lower_program_ir(*parse_result.program, resolve_result, type_result);
    const auto typed_ir = ahfl::lower_typed_program(type_result.typed_program);

    CHECK(typed_ir.declarations.size() == ast_ir.declarations.size());
    REQUIRE_FALSE(typed_ir.declarations.empty());
}

TEST_CASE("IR lowering prefers Typed HIR expression types for inferred let bindings") {
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

agent HirAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for HirAgent {
    state Done {
        let reply = Response { value: input.value };
        return reply;
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("typed_hir_preferred.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker checker;
    auto type_result = checker.check(*parse_result.program, resolve_result);
    REQUIRE_FALSE(type_result.has_errors());

    ahfl::TypedExpr *struct_literal = nullptr;
    for (auto &expr : type_result.typed_program.expressions) {
        if (expr.kind == ahfl::ast::ExprSyntaxKind::StructLiteral && expr.type != nullptr &&
            expr.type->describe() == "Response") {
            struct_literal = &expr;
            break;
        }
    }
    REQUIRE(struct_literal != nullptr);
    struct_literal->type = ahfl::TypeContext::global().make(ahfl::TypeKind::Int);

    const auto lowered = ahfl::lower_program_ir(*parse_result.program, resolve_result, type_result);

    const auto *flow = std::get_if<ahfl::ir::FlowDecl>(&lowered.declarations.back());
    REQUIRE(flow != nullptr);
    REQUIRE(flow->state_handlers.size() == 1);
    REQUIRE(flow->state_handlers.front().body.statements.size() >= 1);

    const auto *let_statement = std::get_if<ahfl::ir::LetStatement>(
        &flow->state_handlers.front().body.statements.front()->node);
    REQUIRE(let_statement != nullptr);
    CHECK(let_statement->name == "reply");
    CHECK(let_statement->type_ref.kind == ahfl::ir::TypeRefKind::Int);
    CHECK(let_statement->type_ref.display_name == "Int");
}

TEST_CASE("Typed HIR lowering can consume detached typed expressions without legacy result") {
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

agent DetachedHirAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for DetachedHirAgent {
    state Done {
        let reply = Response { value: input.value };
        return reply;
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("typed_hir_detached_lowering.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker checker;
    const auto type_result = checker.check(*parse_result.program, resolve_result);
    REQUIRE_FALSE(type_result.has_errors());

    auto detached = type_result.typed_program;
    detached.type_check_result = nullptr;
    ahfl::TypedExpr *struct_literal = nullptr;
    for (auto &expr : detached.expressions) {
        if (expr.kind == ahfl::ast::ExprSyntaxKind::StructLiteral && expr.type != nullptr &&
            expr.type->describe() == "Response") {
            struct_literal = &expr;
            break;
        }
    }
    REQUIRE(struct_literal != nullptr);
    // Force lowering off the TypedProgram::find_expr(node_id) fast path so this
    // regression specifically proves lower_typed_program rebuilds the legacy
    // range-indexed expression type side table from detached TypedExpr records.
    struct_literal->node_id = 0;
    struct_literal->type = ahfl::TypeContext::global().make(ahfl::TypeKind::Int);

    const auto lowered = ahfl::lower_typed_program(detached);

    const auto *flow = std::get_if<ahfl::ir::FlowDecl>(&lowered.declarations.back());
    REQUIRE(flow != nullptr);
    REQUIRE(flow->state_handlers.size() == 1);
    REQUIRE_FALSE(flow->state_handlers.front().body.statements.empty());

    const auto *let_statement = std::get_if<ahfl::ir::LetStatement>(
        &flow->state_handlers.front().body.statements.front()->node);
    REQUIRE(let_statement != nullptr);
    CHECK(let_statement->name == "reply");
    CHECK(let_statement->type_ref.kind == ahfl::ir::TypeRefKind::Int);
    CHECK(let_statement->type_ref.display_name == "Int");
}

TEST_CASE("Typed HIR lowering prefers resolved call target metadata over AST references") {
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

capability Original(value: String) -> Response;
capability Redirected(value: String) -> Response;

agent MetadataCallAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [Original, Redirected];
}

flow for MetadataCallAgent {
    state Done {
        return Original(input.value);
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("typed_hir_call_target_lowering.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker checker;
    const auto type_result = checker.check(*parse_result.program, resolve_result);
    REQUIRE_FALSE(type_result.has_errors());

    const auto redirected_symbol = resolve_result.symbol_table.find_local(
        ahfl::SymbolNamespace::Capabilities, "Redirected");
    REQUIRE(redirected_symbol.has_value());

    auto detached = type_result.typed_program;
    detached.type_check_result = nullptr;
    ahfl::TypedExpr *call = nullptr;
    for (auto &expr : detached.expressions) {
        if (expr.kind == ahfl::ast::ExprSyntaxKind::Call && expr.semantic_name == "Original") {
            call = &expr;
            break;
        }
    }
    REQUIRE(call != nullptr);
    call->resolved_symbol = redirected_symbol->get().id;
    call->semantic_name = "Redirected";
    call->call_target_kind = ahfl::TypedCallTargetKind::Capability;

    const auto lowered = ahfl::lower_typed_program(detached);

    const auto *flow = std::get_if<ahfl::ir::FlowDecl>(&lowered.declarations.back());
    REQUIRE(flow != nullptr);
    REQUIRE(flow->state_handlers.size() == 1);
    REQUIRE_FALSE(flow->state_handlers.front().body.statements.empty());

    const auto *return_statement = std::get_if<ahfl::ir::ReturnStatement>(
        &flow->state_handlers.front().body.statements.front()->node);
    REQUIRE(return_statement != nullptr);
    REQUIRE(return_statement->value != nullptr);
    const auto *return_call = std::get_if<ahfl::ir::CallExpr>(&return_statement->value->node);
    REQUIRE(return_call != nullptr);
    CHECK(return_call->callee == "Redirected");
}

TEST_CASE("Typed HIR lowering prefers resolved struct target metadata over AST references") {
    const std::string source = R"AHFL(
struct Request {
    value: String;
}

struct Context {
    value: String = "";
}

struct OriginalResponse {
    value: String;
}

struct RedirectedResponse {
    value: String;
}

agent MetadataStructAgent {
    input: Request;
    context: Context;
    output: OriginalResponse;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for MetadataStructAgent {
    state Done {
        return OriginalResponse { value: input.value };
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("typed_hir_struct_target_lowering.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker checker;
    const auto type_result = checker.check(*parse_result.program, resolve_result);
    REQUIRE_FALSE(type_result.has_errors());

    const auto redirected_symbol = resolve_result.symbol_table.find_local(
        ahfl::SymbolNamespace::Types, "RedirectedResponse");
    REQUIRE(redirected_symbol.has_value());

    auto detached = type_result.typed_program;
    detached.type_check_result = nullptr;
    ahfl::TypedExpr *struct_literal = nullptr;
    for (auto &expr : detached.expressions) {
        if (expr.kind == ahfl::ast::ExprSyntaxKind::StructLiteral &&
            expr.semantic_name == "OriginalResponse") {
            struct_literal = &expr;
            break;
        }
    }
    REQUIRE(struct_literal != nullptr);
    struct_literal->resolved_symbol = redirected_symbol->get().id;
    struct_literal->semantic_name = "RedirectedResponse";

    const auto lowered = ahfl::lower_typed_program(detached);

    const auto *flow = std::get_if<ahfl::ir::FlowDecl>(&lowered.declarations.back());
    REQUIRE(flow != nullptr);
    REQUIRE(flow->state_handlers.size() == 1);
    REQUIRE_FALSE(flow->state_handlers.front().body.statements.empty());

    const auto *return_statement = std::get_if<ahfl::ir::ReturnStatement>(
        &flow->state_handlers.front().body.statements.front()->node);
    REQUIRE(return_statement != nullptr);
    REQUIRE(return_statement->value != nullptr);
    const auto *return_literal = std::get_if<ahfl::ir::StructLiteralExpr>(
        &return_statement->value->node);
    REQUIRE(return_literal != nullptr);
    CHECK(return_literal->type_name == "RedirectedResponse");
}

TEST_CASE("Typed HIR lowering prefers resolved qualified value metadata over AST references") {
    const std::string source = R"AHFL(
struct Request {
    value: String;
}

struct Context {
    value: String = "";
}

enum OriginalStatus {
    Ready,
    Blocked,
}

enum RedirectedStatus {
    Forwarded,
    Blocked,
}

struct Response {
    status: OriginalStatus;
}

agent MetadataQualifiedValueAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for MetadataQualifiedValueAgent {
    state Done {
        return Response { status: OriginalStatus::Ready };
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("typed_hir_qualified_value_lowering.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker checker;
    const auto type_result = checker.check(*parse_result.program, resolve_result);
    REQUIRE_FALSE(type_result.has_errors());

    const auto redirected_symbol = resolve_result.symbol_table.find_local(
        ahfl::SymbolNamespace::Types, "RedirectedStatus");
    REQUIRE(redirected_symbol.has_value());

    auto detached = type_result.typed_program;
    detached.type_check_result = nullptr;
    ahfl::TypedExpr *qualified_value = nullptr;
    for (auto &expr : detached.expressions) {
        if (expr.kind == ahfl::ast::ExprSyntaxKind::QualifiedValue &&
            expr.semantic_name == "OriginalStatus::Ready") {
            qualified_value = &expr;
            break;
        }
    }
    REQUIRE(qualified_value != nullptr);
    qualified_value->resolved_symbol = redirected_symbol->get().id;
    qualified_value->semantic_name = "RedirectedStatus::Forwarded";

    const auto lowered = ahfl::lower_typed_program(detached);

    const auto *flow = std::get_if<ahfl::ir::FlowDecl>(&lowered.declarations.back());
    REQUIRE(flow != nullptr);
    REQUIRE(flow->state_handlers.size() == 1);
    REQUIRE_FALSE(flow->state_handlers.front().body.statements.empty());

    const auto *return_statement = std::get_if<ahfl::ir::ReturnStatement>(
        &flow->state_handlers.front().body.statements.front()->node);
    REQUIRE(return_statement != nullptr);
    REQUIRE(return_statement->value != nullptr);
    const auto *return_literal = std::get_if<ahfl::ir::StructLiteralExpr>(
        &return_statement->value->node);
    REQUIRE(return_literal != nullptr);
    REQUIRE(return_literal->fields.size() == 1);
    REQUIRE(return_literal->fields.front().value != nullptr);
    const auto *return_value = std::get_if<ahfl::ir::QualifiedValueExpr>(
        &return_literal->fields.front().value->node);
    REQUIRE(return_value != nullptr);
    CHECK(return_value->value == "RedirectedStatus::Forwarded");
}
