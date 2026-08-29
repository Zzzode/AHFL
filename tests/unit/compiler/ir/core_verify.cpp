#include <doctest.h>

#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/core_verify.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// RFC 0026 P3 (KR6.4): Core-IR structural verifier. These tests build a
// KNOWN-GOOD Core-IR program directly (no lowering), assert it verifies clean,
// then tamper ONE invariant at a time and assert the verifier fails closed with
// the expected stable `core.verify.*` code. This is the "corrupt a typed
// ID/step/value" negative battery Codex asked for: it proves a dangling
// reference or a broken step chain cannot slip past into a backend.

namespace {

using namespace ahfl;
using namespace ahfl::ir::core;

// Does the result carry an ERROR diagnostic with this code?
[[nodiscard]] bool has_code(const CoreVerifyResult &r, std::string_view code) {
    for (const auto &d : r.diagnostics) {
        if (d.severity == CoreDiagnosticSeverity::Error && d.code == code) {
            return true;
        }
    }
    return false;
}

// --------------------------------------------------------------------------
// A known-good program builder. Shape:
//   types:   [0] struct Inner { n: Int }
//            [1] struct Ctx   { saved: Int, nested: Inner }
//            [2] enum   Flag  { On, Off }
//   caps:    [0] Charge(Int) -> Bool
//   agents:  [0] A { states [Init, Done], initial Init, final [Done],
//                    context_type = Ctx }
//   flow -> A:
//     Init:
//        %0 = literal 1
//        %1 = capability_call Charge(%0)         (arity 1, ok)
//        store ctx.saved <- %1 uses projection [Ctx.saved]
//        %2 = path ctx.nested.n (projection [Ctx.nested, Inner.n])
//        if %1 { %3 = literal 2; return %3 } else { goto Done }
//     Done:
//        return
// Every typed id is in range and every invariant holds.
// --------------------------------------------------------------------------
struct GoodProgram {
    CoreProgram program;
    // handles for tampering
    CoreFlowDecl *flow{nullptr};
};

[[nodiscard]] GoodProgram make_good_program() {
    GoodProgram g;
    CoreProgram &p = g.program;

    // types
    {
        CoreTypeDecl inner;
        inner.kind = CoreTypeDecl::Kind::Struct;
        inner.name = "Inner";
        inner.fields = {"n"};
        inner.field_types = {CoreTypeId{}}; // Int -> primitive
        p.types.push_back(std::move(inner));

        CoreTypeDecl ctx;
        ctx.kind = CoreTypeDecl::Kind::Struct;
        ctx.name = "Ctx";
        ctx.fields = {"saved", "nested"};
        ctx.field_types = {CoreTypeId{}, CoreTypeId{0}}; // saved: Int, nested: Inner (type 0)
        p.types.push_back(std::move(ctx));

        CoreTypeDecl flag;
        flag.kind = CoreTypeDecl::Kind::Enum;
        flag.name = "Flag";
        flag.variants = {"On", "Off"};
        p.types.push_back(std::move(flag));
    }
    const CoreTypeId inner_ty{0};
    const CoreTypeId ctx_ty{1};

    // capability Charge(Int) -> Bool
    {
        CoreCapabilityDecl cap;
        cap.name = "Charge";
        ir::TypeRef amount;
        amount.kind = ir::TypeRefKind::Int;
        cap.param_types.push_back(std::move(amount));
        cap.return_type_ref.kind = ir::TypeRefKind::Bool;
        p.capabilities.push_back(std::move(cap));
    }

    // agent A
    {
        CoreAgentDecl a;
        a.name = "A";
        a.states = {"Init", "Done"};
        a.initial = CoreStateId{0};
        a.finals = {CoreStateId{1}};
        a.transitions = {CoreTransition{CoreStateId{0}, CoreStateId{1}}};
        a.context_type = ctx_ty;
        p.agents.push_back(std::move(a));
    }

    // flow
    CoreFlowDecl flow;
    flow.target = CoreAgentId{0};
    flow.agent_name = "A";

    // expr arena
    // %lit1 spelling "1"
    const CoreExprId e_lit1{static_cast<std::uint32_t>(flow.exprs.size())};
    flow.exprs.push_back(CoreExpr{CoreLiteralExpr{CoreLiteralKind::Integer, "1"}, std::nullopt});
    // path ctx.nested.n  (root Context, projection [Ctx.nested -> Inner, Inner.n -> prim])
    CorePathExpr nested_path;
    nested_path.root = CorePathRoot::Context;
    nested_path.root_name = "ctx";
    nested_path.members = {"nested", "n"};
    nested_path.root_type = ctx_ty;
    nested_path.projection = {
        CoreProjectionStep{ctx_ty, CoreFieldId{1}, inner_ty},  // Ctx.nested -> Inner
        CoreProjectionStep{inner_ty, CoreFieldId{0}, CoreTypeId{}}, // Inner.n -> primitive (last)
    };
    nested_path.projection_resolved = true;
    const CoreExprId e_nested{static_cast<std::uint32_t>(flow.exprs.size())};
    flow.exprs.push_back(CoreExpr{std::move(nested_path), std::nullopt});
    // %lit2 spelling "2"
    const CoreExprId e_lit2{static_cast<std::uint32_t>(flow.exprs.size())};
    flow.exprs.push_back(CoreExpr{CoreLiteralExpr{CoreLiteralKind::Integer, "2"}, std::nullopt});

    // values: %0 = lit1, %1 = Charge(%0), %2 = nested path, %3 = lit2
    flow.value_count = 4;
    const CoreValueId v0{0};
    const CoreValueId v1{1};
    const CoreValueId v2{2};
    const CoreValueId v3{3};

    // Init handler body
    CoreFlowState init;
    init.state = CoreStateId{0};
    init.state_name = "Init";
    // %0 = lit1
    init.body.statements.push_back(CoreStmt{CoreLetStmt{v0, e_lit1}, std::nullopt});
    // %1 = Charge(%0)
    {
        CoreCapabilityCallStmt call;
        call.result = v1;
        call.capability = CoreCapabilityId{0};
        call.callee_name = "Charge";
        call.args = {v0};
        init.body.statements.push_back(CoreStmt{std::move(call), std::nullopt});
    }
    // store ctx.saved <- %1
    {
        CorePlace place;
        place.root = CorePathRoot::Context;
        place.root_name = "ctx";
        place.members = {"saved"};
        place.root_type = ctx_ty;
        place.projection = {CoreProjectionStep{ctx_ty, CoreFieldId{0}, CoreTypeId{}}}; // Ctx.saved prim
        place.projection_resolved = true;
        init.body.statements.push_back(CoreStmt{CoreStoreStmt{std::move(place), v1}, std::nullopt});
    }
    // %2 = path ctx.nested.n
    init.body.statements.push_back(CoreStmt{CoreLetStmt{v2, e_nested}, std::nullopt});
    // if %1 { %3 = lit2; return %3 } else { goto Done }
    {
        CoreIfStmt if_stmt;
        if_stmt.condition = v1;
        auto then_region = std::make_unique<CoreRegion>();
        then_region->statements.push_back(CoreStmt{CoreLetStmt{v3, e_lit2}, std::nullopt});
        then_region->statements.push_back(CoreStmt{CoreReturnStmt{true, v3}, std::nullopt});
        auto else_region = std::make_unique<CoreRegion>();
        else_region->statements.push_back(CoreStmt{CoreGotoStmt{CoreStateId{1}, "Done"}, std::nullopt});
        if_stmt.then_region = std::move(then_region);
        if_stmt.else_region = std::move(else_region);
        init.body.statements.push_back(CoreStmt{std::move(if_stmt), std::nullopt});
    }
    flow.states.push_back(std::move(init));

    // Done handler: return
    CoreFlowState done;
    done.state = CoreStateId{1};
    done.state_name = "Done";
    done.body.statements.push_back(CoreStmt{CoreReturnStmt{false, CoreValueId{}}, std::nullopt});
    flow.states.push_back(std::move(done));

    p.flows.push_back(std::move(flow));
    g.flow = &p.flows.back();
    return g;
}

} // namespace

TEST_CASE("verifier accepts a well-formed Core-IR program") {
    const GoodProgram g = make_good_program();
    const auto result = verify_core_program(g.program);
    for (const auto &d : result.diagnostics) {
        INFO("unexpected verifier diagnostic: " << d.code << " — " << d.message);
        CHECK(false);
    }
    CHECK(result.ok());
}

TEST_CASE("verifier fails closed on a dangling flow target agent id") {
    GoodProgram g = make_good_program();
    g.flow->target = CoreAgentId{99}; // no such agent
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kFlowTargetInvalid));
}

TEST_CASE("verifier fails closed on an out-of-range goto target state") {
    GoodProgram g = make_good_program();
    // Reach into Init's if-else and corrupt the goto in the else region.
    auto &init_body = g.flow->states[0].body;
    for (auto &stmt : init_body.statements) {
        if (auto *iff = std::get_if<CoreIfStmt>(&stmt.node)) {
            auto &else_stmt = iff->else_region->statements[0];
            std::get<CoreGotoStmt>(else_stmt.node).target = CoreStateId{42};
        }
    }
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kGotoTargetInvalid));
}

TEST_CASE("verifier fails closed on a use-before-def value") {
    GoodProgram g = make_good_program();
    // Charge's arg %0 is defined by the first let; delete that let so %0 is used
    // (in the call) before any definition.
    auto &init_body = g.flow->states[0].body;
    init_body.statements.erase(init_body.statements.begin()); // drop `%0 = lit1`
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kValueUseBeforeDef));
}

TEST_CASE("verifier fails closed on a value id out of range") {
    GoodProgram g = make_good_program();
    g.flow->value_count = 2; // %2 and %3 now out of range
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kValueIdOutOfRange));
}

TEST_CASE("verifier fails closed on a branch-local value escaping its scope") {
    GoodProgram g = make_good_program();
    // %3 is defined only inside the then-branch. Add a Done-handler return that
    // reads %3 — it must be out of scope there.
    g.flow->states[1].body.statements.clear();
    g.flow->states[1].body.statements.push_back(
        CoreStmt{CoreReturnStmt{true, CoreValueId{3}}, std::nullopt});
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kValueUseBeforeDef));
}

TEST_CASE("verifier fails closed on a capability-call arity mismatch") {
    GoodProgram g = make_good_program();
    auto &init_body = g.flow->states[0].body;
    for (auto &stmt : init_body.statements) {
        if (auto *call = std::get_if<CoreCapabilityCallStmt>(&stmt.node)) {
            call->args.clear(); // Charge expects 1 arg
        }
    }
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kCapabilityArityMismatch));
}

TEST_CASE("verifier fails closed on a capability id out of range") {
    GoodProgram g = make_good_program();
    auto &init_body = g.flow->states[0].body;
    for (auto &stmt : init_body.statements) {
        if (auto *call = std::get_if<CoreCapabilityCallStmt>(&stmt.node)) {
            call->capability = CoreCapabilityId{7};
        }
    }
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kCapabilityIdOutOfRange));
}

TEST_CASE("verifier fails closed on a broken projection step chain (discontinuity)") {
    GoodProgram g = make_good_program();
    // Corrupt the ctx.nested.n read: make step 1's owner NOT the previous result.
    for (auto &expr : g.flow->exprs) {
        if (auto *path = std::get_if<CorePathExpr>(&expr.node)) {
            if (path->projection.size() == 2) {
                path->projection[1].owner_type = CoreTypeId{1}; // Ctx, not Inner
            }
        }
    }
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kProjectionDiscontinuity));
}

TEST_CASE("verifier fails closed on a projection field id out of range") {
    GoodProgram g = make_good_program();
    for (auto &expr : g.flow->exprs) {
        if (auto *path = std::get_if<CorePathExpr>(&expr.node)) {
            if (!path->projection.empty()) {
                path->projection[0].field = CoreFieldId{9}; // Ctx has 2 fields
            }
        }
    }
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kProjectionFieldInvalid));
}

TEST_CASE("verifier fails closed on a non-terminal primitive projection step") {
    GoodProgram g = make_good_program();
    // Make the FIRST step of ctx.nested.n a primitive (result kInvalid) while a
    // second step still follows — invariant 3 violation.
    for (auto &expr : g.flow->exprs) {
        if (auto *path = std::get_if<CorePathExpr>(&expr.node)) {
            if (path->projection.size() == 2) {
                path->projection[0].result_type = CoreTypeId{}; // now primitive, but not last
                // keep field types consistent so it's the "primitive not last"
                // arm, not the "disagrees with declared type" arm: point field
                // at Ctx.saved (a primitive field) instead of Ctx.nested.
                path->projection[0].field = CoreFieldId{0}; // Ctx.saved: Int
            }
        }
    }
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kProjectionPrimitiveNotLast));
}

TEST_CASE("verifier fails closed on a construct with a duplicated field id") {
    GoodProgram g = make_good_program();
    // Append a struct-literal construct for Ctx that assigns field 0 twice.
    CoreConstructExpr ctor;
    ctor.type_name = "Ctx";
    ctor.is_enum_variant = false;
    ctor.type_id = CoreTypeId{1};
    ctor.resolved = true;
    ctor.args = {CoreConstructArg{CoreFieldId{0}, CoreValueId{0}},
                 CoreConstructArg{CoreFieldId{0}, CoreValueId{1}}};
    g.flow->exprs.push_back(CoreExpr{std::move(ctor), std::nullopt});
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kConstructFieldDuplicated));
}

TEST_CASE("verifier fails closed on an unlowered expression in an executable program") {
    GoodProgram g = make_good_program();
    g.flow->exprs.push_back(CoreExpr{CoreUnsupportedExpr{"MatchExpr", std::nullopt}, std::nullopt});
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kUnsupportedExpr));
}

TEST_CASE("verifier fails closed on a statement after a terminator") {
    GoodProgram g = make_good_program();
    // Done handler is a single return; append a statement after it.
    g.flow->states[1].body.statements.push_back(
        CoreStmt{CoreReturnStmt{false, CoreValueId{}}, std::nullopt});
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kStmtAfterTerminator));
}

TEST_CASE("verifier fails closed on a typed shell that is not a struct") {
    GoodProgram g = make_good_program();
    g.program.agents[0].context_type = CoreTypeId{2}; // Flag is an enum, not a struct
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kTypedShellInvalid));
}

TEST_CASE("verifier fails closed on an out-of-range agent state id") {
    GoodProgram g = make_good_program();
    g.program.agents[0].initial = CoreStateId{5};
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kStateIdOutOfRange));
}
