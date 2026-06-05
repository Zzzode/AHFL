#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/base/support/ownership.hpp"
#include "ahfl/compiler/handoff/package.hpp"
#include "ahfl/compiler/ir/identity.hpp"
#include "ahfl/compiler/ir/program_view.hpp"
#include "ahfl/compiler/ir/visitor.hpp"

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
    ahfl::ir::WorkflowDecl workflow{
        .provenance = {},
        .name = "pkg::Workflow",
        .nodes = {},
        .safety = {},
        .liveness = {},
        .return_value = make_call_expr("ReturnCall"),
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
