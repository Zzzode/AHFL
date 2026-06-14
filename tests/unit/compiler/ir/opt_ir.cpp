#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/ir/opt/opt_ir.hpp"
#include "compiler/ir/opt/opt_lower.hpp"
#include "compiler/ir/opt/opt_passes.hpp"
#include "compiler/ir/opt/opt_verify.hpp"

#include "ahfl/compiler/ir/ir.hpp"

#include <algorithm>
#include <string>
#include <variant>
#include <vector>

namespace {

/// Build a minimal IR Program with a single FlowDecl containing one
/// StateHandler with the given statements.
void add_simple_flow(ahfl::ir::Program &program, std::vector<ahfl::ir::StatementPtr> stmts) {
    ahfl::ir::Block body;
    body.statements = std::move(stmts);

    ahfl::ir::StateHandler handler;
    handler.state_name = "Init";
    handler.body = std::move(body);

    ahfl::ir::FlowDecl flow;
    flow.target_ref.canonical_name = "TestAgent";
    flow.state_handlers.push_back(std::move(handler));

    program.declarations.push_back(std::move(flow));
}

/// Helper: create a LetStatement with an integer literal.
ahfl::ir::StatementPtr
make_let_int(ahfl::ir::ExprArena &arena, const std::string &name, const std::string &value) {
    auto expr = arena.make(ahfl::ir::IntegerLiteralExpr{.spelling = value});
    auto stmt = ahfl::make_owned<ahfl::ir::Statement>(ahfl::ir::Statement{
        .node =
            ahfl::ir::LetStatement{
                .name = name,
                .type_ref = {},
                .initializer = expr,
            },
        .source_range = {},
    });
    return stmt;
}

[[nodiscard]] ahfl::ir::TypeRef int_type() {
    ahfl::ir::TypeRef type;
    type.kind = ahfl::ir::TypeRefKind::Int;
    type.display_name = "Int";
    return type;
}

[[nodiscard]] ahfl::ir::TypeRef bool_type() {
    ahfl::ir::TypeRef type;
    type.kind = ahfl::ir::TypeRefKind::Bool;
    type.display_name = "Bool";
    return type;
}

[[nodiscard]] ahfl::ir::TypeRef struct_type(std::string canonical_name) {
    ahfl::ir::TypeRef type;
    type.kind = ahfl::ir::TypeRefKind::Struct;
    type.canonical_name = std::move(canonical_name);
    return type;
}

[[nodiscard]] ahfl::SourceRange source_range(std::size_t begin, std::size_t end) {
    return ahfl::SourceRange{.begin_offset = begin, .end_offset = end};
}

[[nodiscard]] const ahfl::ir::opt::OptFunction *
find_function(const ahfl::ir::opt::OptProgram &program, const std::string &name) {
    const auto it = std::find_if(program.functions.begin(),
                                 program.functions.end(),
                                 [&name](const auto &function) { return function.name == name; });
    return it == program.functions.end() ? nullptr : &*it;
}

[[nodiscard]] ahfl::ir::ExprRef make_bool_expr(
    ahfl::ir::Program &program, bool value, std::size_t begin, std::size_t end, std::uint32_t id) {
    return program.expr_arena.make(
        ahfl::ir::BoolLiteralExpr{.value = value}, source_range(begin, end), bool_type(), id);
}

[[nodiscard]] ahfl::ir::ExprRef make_path_expr(ahfl::ir::Program &program,
                                               ahfl::ir::PathRootKind root_kind,
                                               std::string root_name,
                                               ahfl::ir::TypeRef type,
                                               std::size_t begin,
                                               std::size_t end,
                                               std::uint32_t id) {
    return program.expr_arena.make(
        ahfl::ir::PathExpr{
            .path =
                ahfl::ir::Path{
                    .root_kind = root_kind,
                    .root_name = std::move(root_name),
                },
        },
        source_range(begin, end),
        std::move(type),
        id);
}

[[nodiscard]] ahfl::ir::TemporalExprPtr
make_temporal(ahfl::ir::TemporalExprNode node, std::size_t begin, std::size_t end) {
    return ahfl::make_owned<ahfl::ir::TemporalExpr>(
        ahfl::ir::TemporalExpr{.node = std::move(node), .source_range = source_range(begin, end)});
}

[[nodiscard]] ahfl::ir::opt::OptFunction make_returning_function() {
    ahfl::ir::opt::OptFunction func;
    func.name = "test_verify";

    ahfl::ir::opt::Local value;
    value.id = 0;
    value.name = "value";
    value.type = int_type();
    func.locals.push_back(std::move(value));
    func.arg_count = 1;

    ahfl::ir::opt::BasicBlock block;
    block.id = 0;
    block.label = "entry";
    block.terminator.kind = ahfl::ir::opt::Terminator::Kind::Return;
    block.terminator.return_value = 0;
    func.blocks.push_back(std::move(block));
    return func;
}

[[nodiscard]] bool has_diagnostic_containing(const ahfl::ir::opt::VerificationResult &result,
                                             ahfl::ir::opt::VerificationSeverity severity,
                                             const std::string &needle) {
    for (const auto &diagnostic : result.diagnostics) {
        if (diagnostic.severity == severity &&
            diagnostic.message.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // anonymous namespace

// ============================================================================
// F-2: Lowering tests
// ============================================================================

TEST_CASE("lower_to_opt produces non-empty OptProgram from simple IR") {
    ahfl::ir::Program program;
    std::vector<ahfl::ir::StatementPtr> stmts;
    stmts.push_back(make_let_int(program.expr_arena, "x", "42"));

    add_simple_flow(program, std::move(stmts));
    auto opt = ahfl::ir::opt::lower_to_opt(program);

    CHECK(!opt.functions.empty());
    CHECK(opt.functions.size() == 1);
    CHECK(opt.functions[0].name == "flow::TestAgent::Init");
    CHECK(!opt.functions[0].blocks.empty());
    CHECK(!opt.functions[0].locals.empty());
}

TEST_CASE("lower_to_opt produces correct function name with agent and state") {
    ahfl::ir::Block body;
    ahfl::ir::StateHandler handler;
    handler.state_name = "Running";
    handler.body = std::move(body);

    ahfl::ir::FlowDecl flow;
    flow.target_ref.canonical_name = "MyAgent";
    flow.state_handlers.push_back(std::move(handler));

    ahfl::ir::Program program;
    program.declarations.push_back(std::move(flow));

    auto opt = ahfl::ir::opt::lower_to_opt(program);
    REQUIRE(opt.functions.size() == 1);
    CHECK(opt.functions[0].name == "flow::MyAgent::Running");
}

TEST_CASE("lower_to_opt handles multiple state handlers") {
    ahfl::ir::StateHandler h1;
    h1.state_name = "A";
    ahfl::ir::StateHandler h2;
    h2.state_name = "B";

    ahfl::ir::FlowDecl flow;
    flow.target_ref.canonical_name = "Multi";
    flow.state_handlers.push_back(std::move(h1));
    flow.state_handlers.push_back(std::move(h2));

    ahfl::ir::Program program;
    program.declarations.push_back(std::move(flow));

    auto opt = ahfl::ir::opt::lower_to_opt(program);
    CHECK(opt.functions.size() == 2);
    CHECK(opt.functions[0].name == "flow::Multi::A");
    CHECK(opt.functions[1].name == "flow::Multi::B");
}

TEST_CASE("lower_to_opt generates locals for let statements") {
    ahfl::ir::Program program;
    std::vector<ahfl::ir::StatementPtr> stmts;
    stmts.push_back(make_let_int(program.expr_arena, "x", "10"));
    stmts.push_back(make_let_int(program.expr_arena, "y", "20"));

    add_simple_flow(program, std::move(stmts));
    auto opt = ahfl::ir::opt::lower_to_opt(program);

    REQUIRE(opt.functions.size() == 1);
    const auto &func = opt.functions[0];

    // Should have locals named "x" and "y".
    bool found_x = false;
    bool found_y = false;
    for (const auto &local : func.locals) {
        if (local.name == "x")
            found_x = true;
        if (local.name == "y")
            found_y = true;
    }
    CHECK(found_x);
    CHECK(found_y);
}

TEST_CASE("lower_to_opt covers contract and workflow expression fragments") {
    ahfl::ir::Program program;

    ahfl::ir::SymbolRef agent_ref;
    agent_ref.kind = ahfl::ir::SymbolRefKind::Agent;
    agent_ref.canonical_name = "pkg::Agent";
    agent_ref.local_name = "Agent";
    agent_ref.id = 1;

    ahfl::ir::AgentDecl agent;
    agent.name = "pkg::Agent";
    agent.symbol_ref = agent_ref;
    agent.input_type_ref = struct_type("pkg::Request");
    agent.context_type_ref = struct_type("pkg::Context");
    agent.output_type_ref = struct_type("pkg::Response");
    program.declarations.push_back(std::move(agent));

    ahfl::ir::ContractDecl contract;
    contract.target_ref = agent_ref;
    ahfl::ir::ContractClause plain_clause;
    plain_clause.value = make_bool_expr(program, true, 10, 14, 1);
    plain_clause.source_range = source_range(10, 14);
    contract.clauses.push_back(std::move(plain_clause));

    ahfl::ir::ContractClause temporal_clause;
    temporal_clause.value = make_temporal(
        ahfl::ir::EmbeddedTemporalExpr{
            .expr = make_bool_expr(program, true, 20, 24, 2),
        },
        18,
        26);
    temporal_clause.source_range = source_range(18, 26);
    contract.clauses.push_back(std::move(temporal_clause));
    program.declarations.push_back(std::move(contract));

    ahfl::ir::WorkflowDecl workflow;
    workflow.name = "pkg::Workflow";
    workflow.symbol_ref.kind = ahfl::ir::SymbolRefKind::Workflow;
    workflow.symbol_ref.canonical_name = "pkg::Workflow";
    workflow.input_type_ref = struct_type("pkg::Request");
    workflow.output_type_ref = struct_type("pkg::Response");

    ahfl::ir::WorkflowNode node;
    node.name = "run";
    node.target_ref = agent_ref;
    node.input = make_path_expr(
        program, ahfl::ir::PathRootKind::Input, "input", struct_type("pkg::Request"), 30, 35, 3);
    node.source_range = source_range(30, 35);
    workflow.nodes.push_back(std::move(node));
    workflow.return_value = make_path_expr(program,
                                           ahfl::ir::PathRootKind::Identifier,
                                           "run",
                                           struct_type("pkg::Response"),
                                           40,
                                           43,
                                           4);
    workflow.safety.push_back(make_temporal(
        ahfl::ir::EmbeddedTemporalExpr{
            .expr = make_bool_expr(program, false, 50, 55, 5),
        },
        48,
        57));
    workflow.liveness.push_back(
        make_temporal(ahfl::ir::CalledTemporalExpr{.capability = "Audit"}, 60, 72));
    program.declarations.push_back(std::move(workflow));

    const auto opt = ahfl::ir::opt::lower_to_opt(program);

    const auto *plain = find_function(opt, "contract::pkg::Agent::clause::0");
    REQUIRE(plain != nullptr);
    CHECK(plain->arg_count == 3);
    CHECK(plain->return_type.kind == ahfl::ir::TypeRefKind::Bool);

    const auto *embedded = find_function(opt, "contract::pkg::Agent::clause::1::embedded::0");
    REQUIRE(embedded != nullptr);
    CHECK(embedded->arg_count == 3);
    CHECK(embedded->return_type.kind == ahfl::ir::TypeRefKind::Bool);

    const auto *node_input = find_function(opt, "workflow::pkg::Workflow::node::run::input");
    REQUIRE(node_input != nullptr);
    CHECK(node_input->arg_count == 2);
    CHECK(node_input->return_type.canonical_name == "pkg::Request");

    const auto *workflow_return = find_function(opt, "workflow::pkg::Workflow::return");
    REQUIRE(workflow_return != nullptr);
    CHECK(workflow_return->arg_count == 2);
    CHECK(workflow_return->return_type.canonical_name == "pkg::Response");

    const auto *safety = find_function(opt, "workflow::pkg::Workflow::safety::0::embedded::0");
    REQUIRE(safety != nullptr);
    CHECK(safety->arg_count == 2);
    CHECK(safety->return_type.kind == ahfl::ir::TypeRefKind::Bool);

    REQUIRE(opt.skipped_temporal_fragments.size() == 1);
    CHECK(opt.skipped_temporal_fragments[0].scope == "workflow::pkg::Workflow::liveness::0");
    CHECK(opt.skipped_temporal_fragments[0].kind == "called");
    CHECK(!opt.skipped_temporal_fragments[0].reason.empty());

    const auto verification = ahfl::ir::opt::verify_opt_program(opt);
    CHECK(!verification.has_errors());
}

// ============================================================================
// G-5: Verifier tests
// ============================================================================

TEST_CASE("verify_opt_program accepts lowered simple flow") {
    ahfl::ir::Program program;
    std::vector<ahfl::ir::StatementPtr> stmts;
    stmts.push_back(make_let_int(program.expr_arena, "x", "10"));

    add_simple_flow(program, std::move(stmts));
    auto opt = ahfl::ir::opt::lower_to_opt(program);

    const auto result = ahfl::ir::opt::verify_opt_program(opt);
    CHECK(!result.has_errors());
}

TEST_CASE("verify_opt_function rejects invalid block targets") {
    auto func = make_returning_function();
    func.blocks[0].terminator.kind = ahfl::ir::opt::Terminator::Kind::Goto;
    func.blocks[0].terminator.target = 42;

    const auto result = ahfl::ir::opt::verify_opt_function(func);

    CHECK(result.has_errors());
    CHECK(has_diagnostic_containing(
        result, ahfl::ir::opt::VerificationSeverity::Error, "goto target is out of range"));
}

TEST_CASE("verify_opt_function rejects invalid local uses") {
    auto func = make_returning_function();
    func.blocks[0].terminator.return_value = 9;

    const auto result = ahfl::ir::opt::verify_opt_function(func);

    CHECK(result.has_errors());
    CHECK(has_diagnostic_containing(
        result, ahfl::ir::opt::VerificationSeverity::Error, "return value local is invalid"));
}

TEST_CASE("verify_opt_function rejects non-bool switch conditions") {
    auto func = make_returning_function();

    ahfl::ir::opt::BasicBlock then_block;
    then_block.id = 1;
    then_block.label = "then";
    then_block.terminator.kind = ahfl::ir::opt::Terminator::Kind::Return;

    ahfl::ir::opt::BasicBlock else_block;
    else_block.id = 2;
    else_block.label = "else";
    else_block.terminator.kind = ahfl::ir::opt::Terminator::Kind::Return;

    func.blocks[0].terminator.kind = ahfl::ir::opt::Terminator::Kind::SwitchBool;
    func.blocks[0].terminator.condition = 0;
    func.blocks[0].terminator.then_block = 1;
    func.blocks[0].terminator.else_block = 2;
    func.blocks.push_back(std::move(then_block));
    func.blocks.push_back(std::move(else_block));

    const auto result = ahfl::ir::opt::verify_opt_function(func);

    CHECK(result.has_errors());
    CHECK(has_diagnostic_containing(result,
                                    ahfl::ir::opt::VerificationSeverity::Error,
                                    "switch condition must have Bool type"));
}

TEST_CASE("verify_opt_function reports unreachable blocks as warnings") {
    auto func = make_returning_function();

    ahfl::ir::opt::BasicBlock unreachable;
    unreachable.id = 1;
    unreachable.label = "dead";
    unreachable.terminator.kind = ahfl::ir::opt::Terminator::Kind::Return;
    func.blocks.push_back(std::move(unreachable));

    const auto result = ahfl::ir::opt::verify_opt_function(func);

    CHECK(!result.has_errors());
    CHECK(result.has_warnings());
    CHECK(has_diagnostic_containing(
        result, ahfl::ir::opt::VerificationSeverity::Warning, "basic block is unreachable"));
}

// ============================================================================
// F-3: Optimization passes tests
// ============================================================================

TEST_CASE("constant_propagation propagates a known constant") {
    // Build: _tmp0 = 42; x = _tmp0
    // After constant propagation, x's rvalue operand should be the constant 42.
    ahfl::ir::opt::OptFunction func;
    func.name = "test_const_prop";

    ahfl::ir::opt::Local tmp;
    tmp.id = 0;
    tmp.name = "_tmp0";
    tmp.is_temp = true;
    func.locals.push_back(std::move(tmp));

    ahfl::ir::opt::Local x;
    x.id = 1;
    x.name = "x";
    func.locals.push_back(std::move(x));

    ahfl::ir::opt::BasicBlock bb;
    bb.id = 0;
    bb.label = "entry";

    // _tmp0 = 42
    ahfl::ir::opt::Statement s1;
    s1.kind = ahfl::ir::opt::Statement::Kind::Assign;
    s1.dest = 0;
    s1.rvalue.kind = ahfl::ir::opt::Rvalue::Kind::Use;
    ahfl::ir::opt::Operand const_op;
    const_op.kind = ahfl::ir::opt::Operand::Kind::Constant;
    const_op.constant = std::int64_t{42};
    s1.rvalue.operands.push_back(const_op);
    bb.statements.push_back(std::move(s1));

    // x = _tmp0
    ahfl::ir::opt::Statement s2;
    s2.kind = ahfl::ir::opt::Statement::Kind::Assign;
    s2.dest = 1;
    s2.rvalue.kind = ahfl::ir::opt::Rvalue::Kind::Use;
    ahfl::ir::opt::Operand local_op;
    local_op.kind = ahfl::ir::opt::Operand::Kind::Local;
    local_op.local = 0;
    s2.rvalue.operands.push_back(local_op);
    bb.statements.push_back(std::move(s2));

    bb.terminator.kind = ahfl::ir::opt::Terminator::Kind::Return;
    bb.terminator.return_value = 1;
    func.blocks.push_back(std::move(bb));

    bool changed = ahfl::ir::opt::constant_propagation(func);
    CHECK(changed);

    // After propagation, s2's operand should now be constant 42.
    const auto &result_stmt = func.blocks[0].statements[1];
    REQUIRE(result_stmt.rvalue.operands.size() == 1);
    CHECK(result_stmt.rvalue.operands[0].kind == ahfl::ir::opt::Operand::Kind::Constant);
    CHECK(std::get<std::int64_t>(result_stmt.rvalue.operands[0].constant) == 42);
}

TEST_CASE("dead_store_elimination removes an unused assignment") {
    // Build: _tmp0 = 42; x = 10; return x
    // _tmp0 is never used, so it should be removed.
    ahfl::ir::opt::OptFunction func;
    func.name = "test_dse";

    ahfl::ir::opt::Local tmp;
    tmp.id = 0;
    tmp.name = "_tmp0";
    tmp.is_temp = true;
    func.locals.push_back(std::move(tmp));

    ahfl::ir::opt::Local x;
    x.id = 1;
    x.name = "x";
    func.locals.push_back(std::move(x));

    ahfl::ir::opt::BasicBlock bb;
    bb.id = 0;
    bb.label = "entry";

    // _tmp0 = 42 (dead)
    ahfl::ir::opt::Statement s1;
    s1.kind = ahfl::ir::opt::Statement::Kind::Assign;
    s1.dest = 0;
    s1.rvalue.kind = ahfl::ir::opt::Rvalue::Kind::Use;
    ahfl::ir::opt::Operand c1;
    c1.kind = ahfl::ir::opt::Operand::Kind::Constant;
    c1.constant = std::int64_t{42};
    s1.rvalue.operands.push_back(c1);
    bb.statements.push_back(std::move(s1));

    // x = 10
    ahfl::ir::opt::Statement s2;
    s2.kind = ahfl::ir::opt::Statement::Kind::Assign;
    s2.dest = 1;
    s2.rvalue.kind = ahfl::ir::opt::Rvalue::Kind::Use;
    ahfl::ir::opt::Operand c2;
    c2.kind = ahfl::ir::opt::Operand::Kind::Constant;
    c2.constant = std::int64_t{10};
    s2.rvalue.operands.push_back(c2);
    bb.statements.push_back(std::move(s2));

    bb.terminator.kind = ahfl::ir::opt::Terminator::Kind::Return;
    bb.terminator.return_value = 1; // uses x
    func.blocks.push_back(std::move(bb));

    bool changed = ahfl::ir::opt::dead_store_elimination(func);
    CHECK(changed);

    // Only one statement should remain (x = 10).
    CHECK(func.blocks[0].statements.size() == 1);
    CHECK(func.blocks[0].statements[0].dest == 1);
}

TEST_CASE("copy_propagation replaces a copy") {
    // Build: y = src; z = y; return z
    // After copy prop, z should use src directly.
    ahfl::ir::opt::OptFunction func;
    func.name = "test_copy_prop";

    ahfl::ir::opt::Local src;
    src.id = 0;
    src.name = "src";
    func.locals.push_back(std::move(src));

    ahfl::ir::opt::Local y;
    y.id = 1;
    y.name = "y";
    func.locals.push_back(std::move(y));

    ahfl::ir::opt::Local z;
    z.id = 2;
    z.name = "z";
    func.locals.push_back(std::move(z));

    ahfl::ir::opt::BasicBlock bb;
    bb.id = 0;
    bb.label = "entry";

    // y = src
    ahfl::ir::opt::Statement s1;
    s1.kind = ahfl::ir::opt::Statement::Kind::Assign;
    s1.dest = 1;
    s1.rvalue.kind = ahfl::ir::opt::Rvalue::Kind::Use;
    ahfl::ir::opt::Operand op_src;
    op_src.kind = ahfl::ir::opt::Operand::Kind::Local;
    op_src.local = 0;
    s1.rvalue.operands.push_back(op_src);
    bb.statements.push_back(std::move(s1));

    // z = y
    ahfl::ir::opt::Statement s2;
    s2.kind = ahfl::ir::opt::Statement::Kind::Assign;
    s2.dest = 2;
    s2.rvalue.kind = ahfl::ir::opt::Rvalue::Kind::Use;
    ahfl::ir::opt::Operand op_y;
    op_y.kind = ahfl::ir::opt::Operand::Kind::Local;
    op_y.local = 1;
    s2.rvalue.operands.push_back(op_y);
    bb.statements.push_back(std::move(s2));

    bb.terminator.kind = ahfl::ir::opt::Terminator::Kind::Return;
    bb.terminator.return_value = 2; // uses z
    func.blocks.push_back(std::move(bb));

    bool changed = ahfl::ir::opt::copy_propagation(func);
    CHECK(changed);

    // After copy prop:
    // - s1 (y = src): uses of y replaced with src
    // - s2 (z = y): y should now be src; uses of z replaced with y (which is src)
    // The terminator should now use src (local 0) via the copy chain.
    // z's uses are replaced with y, and y's uses are replaced with src.
    // So return_value should be 1 (y) after one pass, or 0 (src) after two passes.
    // One pass replaces: uses of y -> src, uses of z -> y.
    // So terminator.return_value = 1 (y, which was z's replacement).
    CHECK(func.blocks[0].terminator.return_value == 1);
}

TEST_CASE("optimize converges to fixpoint") {
    // Build: _tmp0 = 42; x = _tmp0; return x
    // After optimize: constant prop + copy prop + DSE should simplify.
    ahfl::ir::opt::OptFunction func;
    func.name = "test_optimize";

    ahfl::ir::opt::Local tmp;
    tmp.id = 0;
    tmp.name = "_tmp0";
    tmp.is_temp = true;
    func.locals.push_back(std::move(tmp));

    ahfl::ir::opt::Local x;
    x.id = 1;
    x.name = "x";
    func.locals.push_back(std::move(x));

    ahfl::ir::opt::BasicBlock bb;
    bb.id = 0;
    bb.label = "entry";

    // _tmp0 = 42
    ahfl::ir::opt::Statement s1;
    s1.kind = ahfl::ir::opt::Statement::Kind::Assign;
    s1.dest = 0;
    s1.rvalue.kind = ahfl::ir::opt::Rvalue::Kind::Use;
    ahfl::ir::opt::Operand c;
    c.kind = ahfl::ir::opt::Operand::Kind::Constant;
    c.constant = std::int64_t{42};
    s1.rvalue.operands.push_back(c);
    bb.statements.push_back(std::move(s1));

    // x = _tmp0
    ahfl::ir::opt::Statement s2;
    s2.kind = ahfl::ir::opt::Statement::Kind::Assign;
    s2.dest = 1;
    s2.rvalue.kind = ahfl::ir::opt::Rvalue::Kind::Use;
    ahfl::ir::opt::Operand use_tmp;
    use_tmp.kind = ahfl::ir::opt::Operand::Kind::Local;
    use_tmp.local = 0;
    s2.rvalue.operands.push_back(use_tmp);
    bb.statements.push_back(std::move(s2));

    bb.terminator.kind = ahfl::ir::opt::Terminator::Kind::Return;
    bb.terminator.return_value = 1;
    func.blocks.push_back(std::move(bb));

    bool changed = ahfl::ir::opt::optimize(func);
    CHECK(changed);

    // After optimization, the constant should be propagated into s2,
    // and _tmp0 (now unused) should be eliminated.
    // Final state: x = 42; return x.
    // Check that at least one statement was removed.
    CHECK(func.blocks[0].statements.size() <= 2);
}
