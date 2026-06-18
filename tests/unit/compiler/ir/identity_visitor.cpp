#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/base/support/ownership.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/handoff/package.hpp"
#include "ahfl/compiler/ir/analysis.hpp"
#include "ahfl/compiler/ir/identity.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/ir/program_view.hpp"
#include "ahfl/compiler/ir/typed_hir_lower.hpp"
#include "ahfl/compiler/ir/verify.hpp"
#include "ahfl/compiler/ir/visitor.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/type_context.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <variant>

namespace {

[[nodiscard]] ahfl::ir::ExprRef make_call_expr(ahfl::ir::ExprArena &arena, std::string callee) {
    return arena.make(ahfl::ir::CallExpr{
        .callee = std::move(callee),
        .arguments = {},
    });
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

class RenameTemporalCalledRewriter final : public ahfl::ir::ProgramRewriter {
  private:
    bool on_temporal_expr(ahfl::ir::TemporalExpr &expr) override {
        auto *called = std::get_if<ahfl::ir::CalledTemporalExpr>(&expr.node);
        if (called == nullptr || called->capability == "pkg::NewCall") {
            return false;
        }

        called->capability = "pkg::NewCall";
        return true;
    }
};

[[nodiscard]] ahfl::ir::TypeRef unit_type() {
    return ahfl::ir::TypeRef{
        .kind = ahfl::ir::TypeRefKind::Unit,
        .display_name = "Unit",
    };
}

[[nodiscard]] ahfl::ir::TypeRef bool_type() {
    return ahfl::ir::TypeRef{
        .kind = ahfl::ir::TypeRefKind::Bool,
        .display_name = "Bool",
    };
}

[[nodiscard]] ahfl::ir::TypeRef unresolved_type(std::string display_name = "<invalid-type>") {
    return ahfl::ir::TypeRef{
        .kind = ahfl::ir::TypeRefKind::Unresolved,
        .display_name = std::move(display_name),
    };
}

[[nodiscard]] ahfl::ir::SymbolRef resolved_symbol(ahfl::ir::SymbolRefKind kind,
                                                  std::string local_name,
                                                  std::size_t id,
                                                  std::string module_name = "pkg") {
    const auto canonical_name = module_name + "::" + local_name;
    return ahfl::ir::SymbolRef{
        .kind = kind,
        .canonical_name = canonical_name,
        .local_name = std::move(local_name),
        .module_name = std::move(module_name),
        .id = id,
    };
}

[[nodiscard]] ahfl::ir::ExprRef make_bool_literal(ahfl::ir::Program &program, bool value = true) {
    return program.expr_arena.make(ahfl::ir::BoolLiteralExpr{.value = value},
                                   {},
                                   bool_type(),
                                   static_cast<std::uint32_t>(program.expr_arena.size()));
}

[[nodiscard]] bool has_ir_diagnostic_containing(const ahfl::ir::VerificationResult &result,
                                                ahfl::ir::VerificationSeverity severity,
                                                const std::string &needle) {
    for (const auto &diagnostic : result.diagnostics) {
        if (diagnostic.severity == severity &&
            diagnostic.message.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool contains_string(const std::vector<std::string> &values,
                                   const std::string &needle) {
    return std::find(values.begin(), values.end(), needle) != values.end();
}

[[nodiscard]] bool has_formal_observation_symbol(const ahfl::ir::Program &program,
                                                 const std::string &needle) {
    const auto &observations = ahfl::ir::formal_observations(program);
    return std::any_of(observations.begin(), observations.end(), [&](const auto &observation) {
        return observation.symbol.find(needle) != std::string::npos;
    });
}

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

    const ahfl::ir::TypeRef type{
        .kind = ahfl::ir::TypeRefKind::Struct,
        .display_name = "Request",
        .canonical_name = "pkg::Request",
    };
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
            ahfl::ir::TypeRef{
                .kind = ahfl::ir::TypeRefKind::Struct,
                .display_name = "LocalRequest",
                .canonical_name = "pkg::Request",
            },
        .context_type_ref =
            ahfl::ir::TypeRef{
                .kind = ahfl::ir::TypeRefKind::Unit,
                .display_name = "Unit",
            },
        .output_type_ref =
            ahfl::ir::TypeRef{
                .kind = ahfl::ir::TypeRefKind::Struct,
                .display_name = "LocalResponse",
                .canonical_name = "pkg::Response",
            },
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
    ahfl::ir::Program program;
    ahfl::ir::WorkflowDecl workflow{
        .provenance = {},
        .name = "pkg::Workflow",
        .nodes = {},
        .safety = {},
        .liveness = {},
        .return_value = make_call_expr(program.expr_arena, "ReturnCall"),
        .input_type_ref =
            ahfl::ir::TypeRef{
                .kind = ahfl::ir::TypeRefKind::Unit,
                .display_name = "Unit",
            },
        .output_type_ref =
            ahfl::ir::TypeRef{
                .kind = ahfl::ir::TypeRefKind::Unit,
                .display_name = "Unit",
            },
        .symbol_ref = {},
    };
    workflow.nodes.push_back(ahfl::ir::WorkflowNode{
        .name = "run",
        .input = make_call_expr(program.expr_arena, "LocalCall"),
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

TEST_CASE("IR mutation safety refreshes flow summaries after expression rewrite") {
    ahfl::ir::Program program;
    const auto call = make_call_expr(program.expr_arena, "pkg::OldCall");
    call->id = 42;
    call->source_range = ahfl::SourceRange{.begin_offset = 10, .end_offset = 20};
    call->resolved_type = bool_type();

    auto stmt = ahfl::make_owned<ahfl::ir::Statement>(ahfl::ir::Statement{
        .node = ahfl::ir::ExprStatement{.expr = call},
        .source_range = {},
        .id = 0,
    });

    ahfl::ir::StateHandler handler;
    handler.state_name = "Init";
    handler.body.statements.push_back(std::move(stmt));

    ahfl::ir::FlowDecl flow;
    flow.state_handlers.push_back(std::move(handler));
    program.declarations.push_back(std::move(flow));

    ahfl::ir::recompute_derived_analyses(program);
    const auto *flow_before = std::get_if<ahfl::ir::FlowDecl>(&program.declarations.front());
    REQUIRE(flow_before != nullptr);
    const auto *summary_before = ahfl::ir::find_state_handler_summary(
        program, *flow_before, flow_before->state_handlers.front());
    REQUIRE(summary_before != nullptr);
    CHECK(contains_string(summary_before->called_targets, "pkg::OldCall"));

    RenameCallRewriter rewriter;
    CHECK(rewriter.rewrite(program));
    ahfl::ir::mark_derived_analyses_stale(program);
    ahfl::ir::ensure_derived_analyses(program);

    const auto *flow_after = std::get_if<ahfl::ir::FlowDecl>(&program.declarations.front());
    REQUIRE(flow_after != nullptr);
    const auto *summary_after = ahfl::ir::find_state_handler_summary(
        program, *flow_after, flow_after->state_handlers.front());
    REQUIRE(summary_after != nullptr);
    CHECK_FALSE(contains_string(summary_after->called_targets, "pkg::OldCall"));
    CHECK(contains_string(summary_after->called_targets, "pkg::CanonicalCall"));
    CHECK_FALSE(ahfl::ir::verify_ir_program(program).has_errors());

    std::ostringstream json;
    ahfl::print_program_ir_json(program, json);
    const auto json_text = json.str();
    CHECK(json_text.find("\"id\": 42") != std::string::npos);
    CHECK(json_text.find("\"source_range\"") != std::string::npos);
    CHECK(json_text.find("\"resolved_type\"") != std::string::npos);
    CHECK(json_text.find("\"kind\": \"bool\"") != std::string::npos);
}

TEST_CASE("IR mutation safety refreshes formal observations after temporal rewrite") {
    auto temporal = ahfl::make_owned<ahfl::ir::TemporalExpr>(ahfl::ir::TemporalExpr{
        .node = ahfl::ir::CalledTemporalExpr{.capability = "pkg::OldCall"},
        .source_range = {},
    });

    ahfl::ir::ContractDecl contract;
    contract.target_ref = ahfl::ir::SymbolRef{
        .kind = ahfl::ir::SymbolRefKind::Agent,
        .canonical_name = "pkg::Agent",
        .local_name = "Agent",
        .module_name = "pkg",
    };
    contract.clauses.push_back(ahfl::ir::ContractClause{
        .kind = ahfl::ir::ContractClauseKind::Requires,
        .value = std::variant<ahfl::ir::ExprRef, ahfl::ir::TemporalExprPtr>{std::move(temporal)},
        .source_range = {},
    });

    ahfl::ir::Program program;
    program.declarations.push_back(std::move(contract));

    ahfl::ir::recompute_derived_analyses(program);
    CHECK(has_formal_observation_symbol(program, "OldCall"));

    RenameTemporalCalledRewriter rewriter;
    CHECK(rewriter.rewrite(program));
    ahfl::ir::mark_derived_analyses_stale(program);
    ahfl::ir::ensure_derived_analyses(program);

    CHECK_FALSE(has_formal_observation_symbol(program, "OldCall"));
    CHECK(has_formal_observation_symbol(program, "NewCall"));
    CHECK_FALSE(ahfl::ir::verify_ir_program(program).has_errors());
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
    const auto typed_ir =
        ahfl::lower_typed_program(type_result.typed_program, *parse_result.program);
    CHECK_FALSE(ahfl::ir::verify_ir_program(typed_ir).has_errors());

    CHECK(typed_ir.declarations.size() == ast_ir.declarations.size());
    REQUIRE_FALSE(typed_ir.declarations.empty());

    std::uint32_t expected_decl_id = 0;
    for (const auto &decl : typed_ir.declarations) {
        std::visit([&](const auto &value) { CHECK(value.provenance.id == expected_decl_id); },
                   decl);
        ++expected_decl_id;
    }

    const auto *flow = std::get_if<ahfl::ir::FlowDecl>(&typed_ir.declarations.back());
    REQUIRE(flow != nullptr);
    REQUIRE(flow->state_handlers.size() == 1);
    const auto &statements = flow->state_handlers.front().body.statements;
    REQUIRE(statements.size() == 2);
    CHECK(statements[0]->id == 0);
    CHECK(statements[1]->id == 1);

    const auto *let_stmt = std::get_if<ahfl::ir::LetStatement>(&statements[0]->node);
    REQUIRE(let_stmt != nullptr);
    REQUIRE(let_stmt->initializer);
    CHECK(let_stmt->initializer.index < typed_ir.expr_arena.size());
    CHECK(&typed_ir.expr_arena.get(let_stmt->initializer.index) == let_stmt->initializer.get());

    const auto *return_stmt = std::get_if<ahfl::ir::ReturnStatement>(&statements[1]->node);
    REQUIRE(return_stmt != nullptr);
    REQUIRE(return_stmt->value);
    CHECK(return_stmt->value.index < typed_ir.expr_arena.size());
    CHECK(&typed_ir.expr_arena.get(return_stmt->value.index) == return_stmt->value.get());

    REQUIRE(typed_ir.all_exprs().size() >= 2);
    for (std::uint32_t index = 0; index < typed_ir.all_exprs().size(); ++index) {
        REQUIRE(typed_ir.all_exprs()[index] != nullptr);
        CHECK(typed_ir.all_exprs()[index]->id == index);
    }
}

TEST_CASE("Typed HIR lowering preserves SymbolRef ids across declarations and references") {
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

capability Echo(value: String) -> Response;

agent SymbolAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [Echo];
}

flow for SymbolAgent {
    state Done {
        return Response { value: input.value };
    }
}

workflow SymbolWorkflow {
    input: Request;
    output: Response;
    node run: SymbolAgent(input);
    return: run;
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("typed_hir_symbol_ref_ids.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker checker;
    const auto type_result = checker.check(*parse_result.program, resolve_result);
    REQUIRE_FALSE(type_result.has_errors());

    const auto request_symbol =
        resolve_result.symbol_table.find_local(ahfl::SymbolNamespace::Types, "Request");
    const auto response_symbol =
        resolve_result.symbol_table.find_local(ahfl::SymbolNamespace::Types, "Response");
    const auto echo_symbol =
        resolve_result.symbol_table.find_local(ahfl::SymbolNamespace::Capabilities, "Echo");
    const auto agent_symbol =
        resolve_result.symbol_table.find_local(ahfl::SymbolNamespace::Agents, "SymbolAgent");
    const auto workflow_symbol =
        resolve_result.symbol_table.find_local(ahfl::SymbolNamespace::Workflows, "SymbolWorkflow");
    REQUIRE(request_symbol.has_value());
    REQUIRE(response_symbol.has_value());
    REQUIRE(echo_symbol.has_value());
    REQUIRE(agent_symbol.has_value());
    REQUIRE(workflow_symbol.has_value());

    const auto typed_ir =
        ahfl::lower_typed_program(type_result.typed_program, *parse_result.program);
    CHECK_FALSE(ahfl::ir::verify_ir_program(typed_ir).has_errors());

    const ahfl::ir::StructDecl *request_decl = nullptr;
    const ahfl::ir::AgentDecl *agent_decl = nullptr;
    const ahfl::ir::FlowDecl *flow_decl = nullptr;
    const ahfl::ir::WorkflowDecl *workflow_decl = nullptr;
    for (const auto &decl : typed_ir.declarations) {
        if (const auto *value = std::get_if<ahfl::ir::StructDecl>(&decl);
            value != nullptr && value->name == "Request") {
            request_decl = value;
        } else if (const auto *value = std::get_if<ahfl::ir::AgentDecl>(&decl);
                   value != nullptr && value->name == "SymbolAgent") {
            agent_decl = value;
        } else if (const auto *value = std::get_if<ahfl::ir::FlowDecl>(&decl); value != nullptr) {
            flow_decl = value;
        } else if (const auto *value = std::get_if<ahfl::ir::WorkflowDecl>(&decl);
                   value != nullptr && value->name == "SymbolWorkflow") {
            workflow_decl = value;
        }
    }
    REQUIRE(request_decl != nullptr);
    REQUIRE(agent_decl != nullptr);
    REQUIRE(flow_decl != nullptr);
    REQUIRE(workflow_decl != nullptr);

    REQUIRE(request_decl->symbol_ref.id.has_value());
    CHECK(*request_decl->symbol_ref.id == request_symbol->get().id.value);
    REQUIRE(agent_decl->symbol_ref.id.has_value());
    CHECK(*agent_decl->symbol_ref.id == agent_symbol->get().id.value);
    REQUIRE(agent_decl->capability_refs.size() == 1);
    REQUIRE(agent_decl->capability_refs.front().id.has_value());
    CHECK(*agent_decl->capability_refs.front().id == echo_symbol->get().id.value);

    REQUIRE(flow_decl->target_ref.id.has_value());
    CHECK(*flow_decl->target_ref.id == agent_symbol->get().id.value);
    REQUIRE(workflow_decl->symbol_ref.id.has_value());
    CHECK(*workflow_decl->symbol_ref.id == workflow_symbol->get().id.value);
    REQUIRE(workflow_decl->nodes.size() == 1);
    REQUIRE(workflow_decl->nodes.front().target_ref.id.has_value());
    CHECK(*workflow_decl->nodes.front().target_ref.id == agent_symbol->get().id.value);

    CHECK(agent_decl->input_type_ref.kind == ahfl::ir::TypeRefKind::Struct);
    CHECK(agent_decl->input_type_ref.canonical_name == request_symbol->get().canonical_name);
    CHECK(agent_decl->output_type_ref.kind == ahfl::ir::TypeRefKind::Struct);
    CHECK(agent_decl->output_type_ref.canonical_name == response_symbol->get().canonical_name);
    CHECK(workflow_decl->input_type_ref.kind == ahfl::ir::TypeRefKind::Struct);
    CHECK(workflow_decl->input_type_ref.canonical_name == request_symbol->get().canonical_name);
    CHECK(workflow_decl->output_type_ref.kind == ahfl::ir::TypeRefKind::Struct);
    CHECK(workflow_decl->output_type_ref.canonical_name == response_symbol->get().canonical_name);
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

    const auto lowered = ahfl::lower_typed_program(detached, *parse_result.program);

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

    const auto redirected_symbol =
        resolve_result.symbol_table.find_local(ahfl::SymbolNamespace::Capabilities, "Redirected");
    REQUIRE(redirected_symbol.has_value());

    auto detached = type_result.typed_program;
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

    const auto lowered = ahfl::lower_typed_program(detached, *parse_result.program);

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

    const auto redirected_symbol =
        resolve_result.symbol_table.find_local(ahfl::SymbolNamespace::Types, "RedirectedResponse");
    REQUIRE(redirected_symbol.has_value());

    auto detached = type_result.typed_program;
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

    const auto lowered = ahfl::lower_typed_program(detached, *parse_result.program);

    const auto *flow = std::get_if<ahfl::ir::FlowDecl>(&lowered.declarations.back());
    REQUIRE(flow != nullptr);
    REQUIRE(flow->state_handlers.size() == 1);
    REQUIRE_FALSE(flow->state_handlers.front().body.statements.empty());

    const auto *return_statement = std::get_if<ahfl::ir::ReturnStatement>(
        &flow->state_handlers.front().body.statements.front()->node);
    REQUIRE(return_statement != nullptr);
    REQUIRE(return_statement->value != nullptr);
    const auto *return_literal =
        std::get_if<ahfl::ir::StructLiteralExpr>(&return_statement->value->node);
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
    const auto parse_result =
        frontend.parse_text("typed_hir_qualified_value_lowering.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker checker;
    const auto type_result = checker.check(*parse_result.program, resolve_result);
    REQUIRE_FALSE(type_result.has_errors());

    const auto redirected_symbol =
        resolve_result.symbol_table.find_local(ahfl::SymbolNamespace::Types, "RedirectedStatus");
    REQUIRE(redirected_symbol.has_value());

    auto detached = type_result.typed_program;
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

    const auto lowered = ahfl::lower_typed_program(detached, *parse_result.program);

    const auto *flow = std::get_if<ahfl::ir::FlowDecl>(&lowered.declarations.back());
    REQUIRE(flow != nullptr);
    REQUIRE(flow->state_handlers.size() == 1);
    REQUIRE_FALSE(flow->state_handlers.front().body.statements.empty());

    const auto *return_statement = std::get_if<ahfl::ir::ReturnStatement>(
        &flow->state_handlers.front().body.statements.front()->node);
    REQUIRE(return_statement != nullptr);
    REQUIRE(return_statement->value != nullptr);
    const auto *return_literal =
        std::get_if<ahfl::ir::StructLiteralExpr>(&return_statement->value->node);
    REQUIRE(return_literal != nullptr);
    REQUIRE(return_literal->fields.size() == 1);
    REQUIRE(return_literal->fields.front().value != nullptr);
    const auto *return_value =
        std::get_if<ahfl::ir::QualifiedValueExpr>(&return_literal->fields.front().value->node);
    REQUIRE(return_value != nullptr);
    CHECK(return_value->value == "RedirectedStatus::Forwarded");
}

TEST_CASE("Semantic IR verifier accepts lowered checked programs") {
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

agent VerifyAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for VerifyAgent {
    state Done {
        let reply = Response { value: input.value };
        return reply;
    }
}
)AHFL";

    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text("verify_ir_program.ahfl", source);
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker checker;
    const auto type_result = checker.check(*parse_result.program, resolve_result);
    REQUIRE_FALSE(type_result.has_errors());

    const auto ir = ahfl::lower_program_ir(*parse_result.program, resolve_result, type_result);
    const auto structural_result = ahfl::ir::verify_ir_program(ir);
    CHECK_FALSE(structural_result.has_errors());

    const auto backend_ready_result =
        ahfl::ir::verify_ir_program(ir, ahfl::ir::IrVerificationMode::BackendReady);
    CHECK_FALSE(backend_ready_result.has_errors());
}

TEST_CASE("Semantic IR backend-ready verifier rejects missing symbol identity") {
    ahfl::ir::Program program;
    const auto expr = make_bool_literal(program);
    program.declarations.push_back(ahfl::ir::ConstDecl{
        .provenance = {},
        .name = "BROKEN",
        .value = expr,
        .type_ref = bool_type(),
        .symbol_ref = {},
    });

    const auto structural_result = ahfl::ir::verify_ir_program(program);
    CHECK_FALSE(structural_result.has_errors());

    const auto backend_ready_result =
        ahfl::ir::verify_ir_program(program, ahfl::ir::IrVerificationMode::BackendReady);
    CHECK(backend_ready_result.has_errors());
    CHECK(has_ir_diagnostic_containing(backend_ready_result,
                                       ahfl::ir::VerificationSeverity::Error,
                                       "required symbol reference is missing id"));
}

TEST_CASE("Semantic IR backend-ready verifier rejects unresolved type references") {
    ahfl::ir::Program program;
    const auto expr = make_bool_literal(program);
    program.declarations.push_back(ahfl::ir::ConstDecl{
        .provenance = {},
        .name = "BROKEN",
        .value = expr,
        .type_ref = unresolved_type(),
        .symbol_ref = resolved_symbol(ahfl::ir::SymbolRefKind::Const, "BROKEN", 1),
    });

    const auto result =
        ahfl::ir::verify_ir_program(program, ahfl::ir::IrVerificationMode::BackendReady);

    CHECK(result.has_errors());
    CHECK(has_ir_diagnostic_containing(
        result, ahfl::ir::VerificationSeverity::Error, "required type reference is unresolved"));
    CHECK(has_ir_diagnostic_containing(result,
                                       ahfl::ir::VerificationSeverity::Error,
                                       "type reference contains sentinel identity"));
}

TEST_CASE("Semantic IR backend-ready verifier rejects wrong symbol kind") {
    ahfl::ir::Program program;
    program.declarations.push_back(ahfl::ir::AgentDecl{
        .provenance = {},
        .name = "BadAgent",
        .states = {},
        .initial_state = {},
        .final_states = {},
        .quota = {},
        .transitions = {},
        .input_type_ref = unit_type(),
        .context_type_ref = unit_type(),
        .output_type_ref = unit_type(),
        .capability_refs =
            {
                resolved_symbol(ahfl::ir::SymbolRefKind::Agent, "Tool", 2),
            },
        .symbol_ref = resolved_symbol(ahfl::ir::SymbolRefKind::Agent, "BadAgent", 1),
    });

    const auto result =
        ahfl::ir::verify_ir_program(program, ahfl::ir::IrVerificationMode::BackendReady);

    CHECK(result.has_errors());
    CHECK(has_ir_diagnostic_containing(result,
                                       ahfl::ir::VerificationSeverity::Error,
                                       "symbol reference has kind agent, expected capability"));
}

TEST_CASE("Semantic IR backend-ready verifier rejects sentinel expression payloads") {
    ahfl::ir::Program program;
    const auto expr =
        program.expr_arena.make(ahfl::ir::QualifiedValueExpr{.value = "<missing-typed-expr>"},
                                {},
                                bool_type(),
                                static_cast<std::uint32_t>(program.expr_arena.size()));
    program.declarations.push_back(ahfl::ir::ConstDecl{
        .provenance = {},
        .name = "BROKEN",
        .value = expr,
        .type_ref = bool_type(),
        .symbol_ref = resolved_symbol(ahfl::ir::SymbolRefKind::Const, "BROKEN", 1),
    });

    const auto result =
        ahfl::ir::verify_ir_program(program, ahfl::ir::IrVerificationMode::BackendReady);

    CHECK(result.has_errors());
    CHECK(has_ir_diagnostic_containing(result,
                                       ahfl::ir::VerificationSeverity::Error,
                                       "qualified value expression contains sentinel payload"));
}

TEST_CASE("Semantic IR backend-ready verifier rejects declaration symbol drift") {
    ahfl::ir::Program program;
    const auto expr = make_bool_literal(program);
    program.declarations.push_back(ahfl::ir::ConstDecl{
        .provenance = {},
        .name = "BROKEN",
        .value = expr,
        .type_ref = bool_type(),
        .symbol_ref = resolved_symbol(ahfl::ir::SymbolRefKind::Const, "OTHER", 1),
    });

    const auto result =
        ahfl::ir::verify_ir_program(program, ahfl::ir::IrVerificationMode::BackendReady);

    CHECK(result.has_errors());
    CHECK(has_ir_diagnostic_containing(
        result,
        ahfl::ir::VerificationSeverity::Error,
        "declaration name 'BROKEN' drifts from symbol local name 'OTHER'"));
}

TEST_CASE("Semantic IR path root kinds print and serialize distinctly") {
    ahfl::ir::Program program;

    const auto add_path_const = [&](std::string name,
                                    ahfl::ir::PathRootKind root_kind,
                                    std::string root_name) {
        ahfl::ir::Path path;
        path.root_kind = root_kind;
        path.root_name = std::move(root_name);
        path.members = {"value"};

        auto expr = program.expr_arena.make(ahfl::ir::PathExpr{.path = std::move(path)},
                                            {},
                                            bool_type(),
                                            static_cast<std::uint32_t>(program.expr_arena.size()));
        program.declarations.push_back(ahfl::ir::ConstDecl{
            .provenance = {},
            .name = std::move(name),
            .value = expr,
            .type_ref = bool_type(),
            .symbol_ref = {},
        });
    };

    add_path_const("input_path", ahfl::ir::PathRootKind::Input, "input");
    add_path_const("context_path", ahfl::ir::PathRootKind::Context, "ctx");
    add_path_const("output_path", ahfl::ir::PathRootKind::Output, "output");
    add_path_const("state_path", ahfl::ir::PathRootKind::State, "state");
    add_path_const("local_path", ahfl::ir::PathRootKind::Local, "reply");
    add_path_const("identifier_path", ahfl::ir::PathRootKind::Identifier, "node");

    std::ostringstream json;
    ahfl::print_program_ir_json(program, json);
    const auto json_text = json.str();

    CHECK(json_text.find("\"root_kind\": \"input\"") != std::string::npos);
    CHECK(json_text.find("\"root_kind\": \"context\"") != std::string::npos);
    CHECK(json_text.find("\"root_kind\": \"output\"") != std::string::npos);
    CHECK(json_text.find("\"root_kind\": \"state\"") != std::string::npos);
    CHECK(json_text.find("\"root_kind\": \"local\"") != std::string::npos);
    CHECK(json_text.find("\"root_kind\": \"identifier\"") != std::string::npos);

    std::ostringstream text;
    ahfl::print_program_ir(program, text);
    const auto ir_text = text.str();

    CHECK(ir_text.find("input.value") != std::string::npos);
    CHECK(ir_text.find("ctx.value") != std::string::npos);
    CHECK(ir_text.find("output.value") != std::string::npos);
    CHECK(ir_text.find("state.value") != std::string::npos);
    CHECK(ir_text.find("reply.value") != std::string::npos);
    CHECK(ir_text.find("node.value") != std::string::npos);
}

TEST_CASE("Semantic IR verifier rejects expression refs outside the arena") {
    ahfl::ir::Program program;
    auto expr = program.expr_arena.make(ahfl::ir::BoolLiteralExpr{.value = true}, {}, bool_type());
    expr.index = 99;

    program.declarations.push_back(ahfl::ir::ConstDecl{
        .provenance = {},
        .name = "BROKEN",
        .value = expr,
        .type_ref = bool_type(),
        .symbol_ref = {},
    });

    const auto result = ahfl::ir::verify_ir_program(program);

    CHECK(result.has_errors());
    CHECK(has_ir_diagnostic_containing(result,
                                       ahfl::ir::VerificationSeverity::Error,
                                       "expression reference index is out of range"));
}

TEST_CASE("Semantic IR verifier rejects malformed TypeRef children") {
    ahfl::ir::TypeRef optional_without_element;
    optional_without_element.kind = ahfl::ir::TypeRefKind::Optional;
    optional_without_element.display_name = "Optional";

    ahfl::ir::Program program;
    program.declarations.push_back(ahfl::ir::AgentDecl{
        .provenance = {},
        .name = "BadAgent",
        .states = {},
        .initial_state = {},
        .final_states = {},
        .quota = {},
        .transitions = {},
        .input_type_ref = std::move(optional_without_element),
        .context_type_ref = unit_type(),
        .output_type_ref = unit_type(),
        .capability_refs = {},
        .symbol_ref = {},
    });

    const auto result = ahfl::ir::verify_ir_program(program);

    CHECK(result.has_errors());
    CHECK(has_ir_diagnostic_containing(
        result, ahfl::ir::VerificationSeverity::Error, "type is missing its element type"));
}

TEST_CASE("Semantic IR verifier rejects duplicate statement ids") {
    auto first = ahfl::make_owned<ahfl::ir::Statement>(ahfl::ir::Statement{
        .node = ahfl::ir::GotoStatement{.target_state = "Done"},
        .source_range = {},
        .id = 7,
    });
    auto second = ahfl::make_owned<ahfl::ir::Statement>(ahfl::ir::Statement{
        .node = ahfl::ir::GotoStatement{.target_state = "Done"},
        .source_range = {},
        .id = 7,
    });

    ahfl::ir::StateHandler handler;
    handler.state_name = "Init";
    handler.body.statements.push_back(std::move(first));
    handler.body.statements.push_back(std::move(second));

    ahfl::ir::FlowDecl flow;
    flow.state_handlers.push_back(std::move(handler));

    ahfl::ir::Program program;
    program.declarations.push_back(std::move(flow));

    const auto result = ahfl::ir::verify_ir_program(program);

    CHECK(result.has_errors());
    CHECK(has_ir_diagnostic_containing(
        result, ahfl::ir::VerificationSeverity::Error, "duplicate statement id"));
}

TEST_CASE("Semantic IR verifier rejects null temporal child pointers") {
    auto temporal = ahfl::make_owned<ahfl::ir::TemporalExpr>(ahfl::ir::TemporalExpr{
        .node =
            ahfl::ir::TemporalUnaryExpr{
                .op = ahfl::ir::TemporalUnaryOp::Always,
                .operand = nullptr,
            },
        .source_range = {},
    });

    ahfl::ir::ContractDecl contract;
    contract.clauses.push_back(ahfl::ir::ContractClause{
        .kind = ahfl::ir::ContractClauseKind::Requires,
        .value = std::variant<ahfl::ir::ExprRef, ahfl::ir::TemporalExprPtr>{std::move(temporal)},
        .source_range = {},
    });

    ahfl::ir::Program program;
    program.declarations.push_back(std::move(contract));

    const auto result = ahfl::ir::verify_ir_program(program);

    CHECK(result.has_errors());
    CHECK(has_ir_diagnostic_containing(
        result, ahfl::ir::VerificationSeverity::Error, "temporal expression pointer is null"));
}

TEST_CASE("Semantic IR verifier rejects analyzed programs with stale analysis bundle") {
    ahfl::ir::StateHandler handler;
    handler.state_name = "Init";

    ahfl::ir::FlowDecl flow;
    flow.state_handlers.push_back(std::move(handler));

    ahfl::ir::Program program;
    program.phase = ahfl::ir::ProgramPhase::Analyzed;
    program.declarations.push_back(std::move(flow));

    const auto result = ahfl::ir::verify_ir_program(program);

    CHECK(result.has_errors());
    CHECK(has_ir_diagnostic_containing(
        result, ahfl::ir::VerificationSeverity::Error, "missing state handler summaries"));
}

TEST_CASE("Semantic IR verifier rejects marked stale analysis revision") {
    ahfl::ir::StateHandler handler;
    handler.state_name = "Init";

    ahfl::ir::FlowDecl flow;
    flow.state_handlers.push_back(std::move(handler));

    ahfl::ir::Program program;
    program.declarations.push_back(std::move(flow));
    ahfl::ir::recompute_derived_analyses(program);
    REQUIRE(ahfl::ir::has_fresh_derived_analyses(program));

    ahfl::ir::mark_derived_analyses_stale(program);

    const auto result = ahfl::ir::verify_ir_program(program);

    CHECK(result.has_errors());
    CHECK(has_ir_diagnostic_containing(
        result, ahfl::ir::VerificationSeverity::Error, "derived analyses are stale"));
}

TEST_CASE("Semantic IR verifier rejects analysis entries with mismatched owner or index") {
    ahfl::ir::StateHandler handler;
    handler.state_name = "Init";

    ahfl::ir::FlowDecl flow;
    flow.target_ref = resolved_symbol(ahfl::ir::SymbolRefKind::Agent, "Worker", 1);
    flow.state_handlers.push_back(std::move(handler));

    ahfl::ir::Program program;
    program.declarations.push_back(std::move(flow));
    ahfl::ir::recompute_derived_analyses(program);
    REQUIRE(ahfl::ir::has_fresh_derived_analyses(program));
    REQUIRE(program.analyses.state_handler_summaries.size() == 1);

    program.analyses.state_handler_summaries.front().handler_index = 99;

    const auto result = ahfl::ir::verify_ir_program(program);

    CHECK(result.has_errors());
    CHECK(has_ir_diagnostic_containing(result,
                                       ahfl::ir::VerificationSeverity::Error,
                                       "analysis entry has no matching declaration owner/index"));
}
