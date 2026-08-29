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
        inner.field_has_default = {false};
        p.types.push_back(std::move(inner));

        CoreTypeDecl ctx;
        ctx.kind = CoreTypeDecl::Kind::Struct;
        ctx.name = "Ctx";
        ctx.fields = {"saved", "nested"};
        ctx.field_types = {CoreTypeId{}, CoreTypeId{0}}; // saved: Int, nested: Inner (type 0)
        ctx.field_has_default = {true, true};            // both default (stateless-friendly)
        p.types.push_back(std::move(ctx));

        CoreTypeDecl flag;
        flag.kind = CoreTypeDecl::Kind::Enum;
        flag.name = "Flag";
        flag.variants = {"On", "Off"};
        flag.variant_payloads = {CoreTypeDecl::VariantPayload{}, CoreTypeDecl::VariantPayload{}};
        p.types.push_back(std::move(flag));

        // input / output structs (Sema requires agent input/output to be Struct).
        CoreTypeDecl req;
        req.kind = CoreTypeDecl::Kind::Struct;
        req.name = "Req";
        req.fields = {"amount"};
        req.field_types = {CoreTypeId{}}; // Int
        req.field_has_default = {false};   // required
        p.types.push_back(std::move(req));

        CoreTypeDecl reply;
        reply.kind = CoreTypeDecl::Kind::Struct;
        reply.name = "Reply";
        reply.fields = {"ok"};
        reply.field_types = {CoreTypeId{}}; // Bool
        reply.field_has_default = {false};   // required
        p.types.push_back(std::move(reply));
    }
    const CoreTypeId inner_ty{0};
    const CoreTypeId ctx_ty{1};
    const CoreTypeId req_ty{3};
    const CoreTypeId reply_ty{4};

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
        a.input_type = req_ty;
        a.output_type = reply_ty;
        a.context_type = ctx_ty;
        a.context_kind = CoreAgentDecl::ContextKind::Struct;
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

// --- ② construct completeness (missing required field / payload arity) ---

TEST_CASE("verifier fails closed on a struct literal missing a required field") {
    GoodProgram g = make_good_program();
    // Reply has one REQUIRED field `ok` (no default). A Reply{} literal that
    // assigns nothing must fail completeness.
    CoreConstructExpr ctor;
    ctor.type_name = "Reply";
    ctor.is_enum_variant = false;
    ctor.type_id = CoreTypeId{4}; // Reply
    ctor.resolved = true;
    ctor.args = {}; // assigns nothing
    g.flow->exprs.push_back(CoreExpr{std::move(ctor), std::nullopt});
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kConstructFieldMissing));
}

TEST_CASE("verifier accepts a struct literal that omits only DEFAULTED fields") {
    GoodProgram g = make_good_program();
    // Ctx's fields are both defaulted, so a Ctx{} literal is complete.
    CoreConstructExpr ctor;
    ctor.type_name = "Ctx";
    ctor.is_enum_variant = false;
    ctor.type_id = CoreTypeId{1}; // Ctx
    ctor.resolved = true;
    ctor.args = {}; // omits both defaulted fields — legal
    g.flow->exprs.push_back(CoreExpr{std::move(ctor), std::nullopt});
    const auto result = verify_core_program(g.program);
    for (const auto &d : result.diagnostics) {
        INFO("unexpected diagnostic: " << d.code << " — " << d.message);
        CHECK(false);
    }
    CHECK(result.ok());
}

TEST_CASE("verifier fails closed on an enum-variant construct with the wrong payload arity") {
    GoodProgram g = make_good_program();
    // Flag::On is a Unit variant (arity 0). Passing a payload slot must fail.
    CoreConstructExpr ctor;
    ctor.type_name = "Flag";
    ctor.variant_name = "On";
    ctor.is_enum_variant = true;
    ctor.type_id = CoreTypeId{2}; // Flag
    ctor.variant = CoreVariantId{0}; // On
    ctor.resolved = true;
    ctor.args = {CoreConstructArg{CoreFieldId{0}, CoreValueId{0}}}; // 1 slot, expected 0
    g.flow->exprs.push_back(CoreExpr{std::move(ctor), std::nullopt});
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kConstructPayloadArity));
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

TEST_CASE("verifier fails closed on a typed shell that is an enum, not a struct") {
    GoodProgram g = make_good_program();
    g.program.agents[0].context_type = CoreTypeId{2}; // Flag is an enum, not a struct
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kTypedShellInvalid));
}

TEST_CASE("verifier fails closed on a required input shell left kInvalid") {
    GoodProgram g = make_good_program();
    g.program.agents[0].input_type = CoreTypeId{}; // missing required input struct
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kTypedShellInvalid));
}

TEST_CASE("verifier fails closed on a required output shell left kInvalid") {
    GoodProgram g = make_good_program();
    g.program.agents[0].output_type = CoreTypeId{}; // missing required output struct
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kTypedShellInvalid));
}

TEST_CASE("verifier accepts a Unit context (ContextKind::Unit, kInvalid type)") {
    GoodProgram g = make_good_program();
    g.program.agents[0].context_kind = CoreAgentDecl::ContextKind::Unit;
    g.program.agents[0].context_type = CoreTypeId{}; // Unit context: no struct
    // The flow's Init handler reads/stores ctx.* — remove those so a Unit-context
    // program is otherwise well-formed. Simplest: drop the whole Init body's ctx
    // touches by clearing it to just a goto, and keep Done's return.
    g.flow->states[0].body.statements.clear();
    g.flow->states[0].body.statements.push_back(
        CoreStmt{CoreGotoStmt{CoreStateId{1}, "Done"}, std::nullopt});
    const auto result = verify_core_program(g.program);
    for (const auto &d : result.diagnostics) {
        INFO("unexpected diagnostic: " << d.code << " — " << d.message);
        CHECK(false);
    }
    CHECK(result.ok());
}

TEST_CASE("verifier fails closed on a Unit context whose type id is nonetheless set") {
    GoodProgram g = make_good_program();
    g.program.agents[0].context_kind = CoreAgentDecl::ContextKind::Unit; // Unit context ...
    g.program.agents[0].context_type = CoreTypeId{1}; // ... but a struct id is set (broken)
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

TEST_CASE("verifier fails closed on an agent with no states") {
    GoodProgram g = make_good_program();
    g.program.agents[0].states.clear();
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kAgentStateInvalid));
}

// --- P0-1: SSA single-definition is FLOW-GLOBAL, not scope-local ---

TEST_CASE("verifier fails closed when the SAME value id is defined in both branches (P0-1)") {
    GoodProgram g = make_good_program();
    // Init's if defines %3 in the then-branch. Make the else-branch ALSO define
    // %3 (before its goto). Flow-global single-definition must catch this even
    // though the two definitions are in mutually-exclusive branches.
    auto &init_body = g.flow->states[0].body;
    for (auto &stmt : init_body.statements) {
        if (auto *iff = std::get_if<CoreIfStmt>(&stmt.node)) {
            // else region currently: [goto Done]. Prepend `%3 = lit2`.
            auto redecl = CoreStmt{CoreLetStmt{CoreValueId{3}, CoreExprId{2}}, std::nullopt};
            iff->else_region->statements.insert(iff->else_region->statements.begin(),
                                                std::move(redecl));
        }
    }
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kValueRedefined));
}

TEST_CASE("verifier fails closed when the same value id is defined in two states (P0-1)") {
    GoodProgram g = make_good_program();
    // %2 is defined in Init (the nested path let). Define it again in Done.
    g.flow->states[1].body.statements.insert(
        g.flow->states[1].body.statements.begin(),
        CoreStmt{CoreLetStmt{CoreValueId{2}, CoreExprId{0}}, std::nullopt});
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kValueRedefined));
}

// --- P0-2: expression reference graph must be acyclic, whole-arena checked ---

TEST_CASE("verifier fails closed on a self-referential expression (P0-2)") {
    GoodProgram g = make_good_program();
    // Append a unary expr that references ITSELF: expr#N = Not(expr#N).
    const auto self = CoreExprId{static_cast<std::uint32_t>(g.flow->exprs.size())};
    g.flow->exprs.push_back(CoreExpr{CoreUnaryExpr{CoreUnaryOp::Not, self}, std::nullopt});
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kExprCycle));
}

TEST_CASE("verifier fails closed on a two-node expression cycle (P0-2)") {
    GoodProgram g = make_good_program();
    const auto a = CoreExprId{static_cast<std::uint32_t>(g.flow->exprs.size())};
    const auto b = CoreExprId{a.value + 1};
    g.flow->exprs.push_back(CoreExpr{CoreUnaryExpr{CoreUnaryOp::Not, b}, std::nullopt}); // a -> b
    g.flow->exprs.push_back(CoreExpr{CoreUnaryExpr{CoreUnaryOp::Not, a}, std::nullopt}); // b -> a
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kExprCycle));
}

TEST_CASE("verifier checks EVERY arena expr, even one no statement references (P0-2)") {
    GoodProgram g = make_good_program();
    // An unlowered expr that no statement binds must still be rejected.
    g.flow->exprs.push_back(CoreExpr{CoreUnsupportedExpr{"LambdaExpr", std::nullopt}, std::nullopt});
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kUnsupportedExpr));
}

TEST_CASE("verifier accepts a shared DAG expr node (revisit is not a cycle)") {
    GoodProgram g = make_good_program();
    // expr %lit1 (id 0) is referenced by two different unary exprs — a shared
    // DAG node, NOT a cycle. Both must pass.
    g.flow->exprs.push_back(CoreExpr{CoreUnaryExpr{CoreUnaryOp::Not, CoreExprId{0}}, std::nullopt});
    g.flow->exprs.push_back(CoreExpr{CoreUnaryExpr{CoreUnaryOp::Neg, CoreExprId{0}}, std::nullopt});
    const auto result = verify_core_program(g.program);
    CHECK(result.ok());
}

TEST_CASE("verifier fails closed on an out-of-range value embedded in an UNUSED arena expr") {
    GoodProgram g = make_good_program();
    // A CoreValueRefExpr that no statement references still embeds a value id;
    // the static arena pass must bounds-check it (value_count is 4).
    g.flow->exprs.push_back(CoreExpr{CoreValueRefExpr{CoreValueId{999}}, std::nullopt});
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kValueIdOutOfRange));
}

TEST_CASE("verifier fails closed on an out-of-range value in an UNUSED construct arg") {
    GoodProgram g = make_good_program();
    CoreConstructExpr ctor;
    ctor.type_name = "Reply";
    ctor.is_enum_variant = false;
    ctor.type_id = CoreTypeId{4}; // Reply
    ctor.resolved = true;
    ctor.args = {CoreConstructArg{CoreFieldId{0}, CoreValueId{777}}}; // bad value id
    g.flow->exprs.push_back(CoreExpr{std::move(ctor), std::nullopt});
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kValueIdOutOfRange));
}

TEST_CASE("verifier fails closed on an enum context (not folded into Unit)") {
    GoodProgram g = make_good_program();
    // A non-struct context must be recorded as ContextKind::Struct with a
    // broken/enum id so the verifier rejects it — never silently folded to Unit.
    g.program.agents[0].context_kind = CoreAgentDecl::ContextKind::Struct;
    g.program.agents[0].context_type = CoreTypeId{2}; // Flag is an enum
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kTypedShellInvalid));
}

// --- type-table shape self-consistency (Codex P0/P1) ---

TEST_CASE("verifier fails closed when a struct's field_types length differs from fields") {
    GoodProgram g = make_good_program();
    g.program.types[1].field_types.pop_back(); // Ctx: 2 fields, 1 field_type
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kTypeTableShapeInvalid));
}

TEST_CASE("verifier fails closed when a struct's field_has_default length differs from fields") {
    GoodProgram g = make_good_program();
    g.program.types[1].field_has_default.pop_back();
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kTypeTableShapeInvalid));
}

TEST_CASE("verifier fails closed when an enum's variant_payloads length differs from variants") {
    GoodProgram g = make_good_program();
    g.program.types[2].variant_payloads.clear(); // Flag: 2 variants, 0 payloads
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kTypeTableShapeInvalid));
}

TEST_CASE("verifier fails closed when a struct carries enum metadata") {
    GoodProgram g = make_good_program();
    g.program.types[1].variants.push_back("Bogus"); // a struct must have no variants
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kTypeTableShapeInvalid));
}

TEST_CASE("verifier fails closed when an enum carries struct field metadata") {
    GoodProgram g = make_good_program();
    g.program.types[2].fields.push_back("bogus"); // an enum must have no fields
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kTypeTableShapeInvalid));
}

TEST_CASE("verifier fails closed when a Unit payload variant carries slots") {
    GoodProgram g = make_good_program();
    g.program.types[2].variant_payloads[0].slot_types.push_back(CoreTypeId{}); // On is Unit
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kTypeTableShapeInvalid));
}

TEST_CASE("verifier fails closed when a struct-payload variant's field_names/slots mismatch") {
    GoodProgram g = make_good_program();
    auto &pv = g.program.types[2].variant_payloads[0];
    pv.kind = CoreTypeDecl::VariantPayload::Kind::Struct;
    pv.slot_types = {CoreTypeId{}, CoreTypeId{}};
    pv.field_names = {"only_one"}; // 1 name vs 2 slots
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kTypeTableShapeInvalid));
}

TEST_CASE("verifier fails closed when a payload slot type id is out of range") {
    GoodProgram g = make_good_program();
    g.program.types[2].variant_payloads[0].kind = CoreTypeDecl::VariantPayload::Kind::Tuple;
    g.program.types[2].variant_payloads[0].slot_types = {CoreTypeId{999}}; // OOR
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kTypeTableShapeInvalid));
}

TEST_CASE("verifier fails closed on an enum-variant construct whose variant has no metadata (P0)") {
    GoodProgram g = make_good_program();
    // Drop Flag's payload metadata to simulate a tampered/JSON-missing table,
    // then construct Flag::On. The arity check must NOT be skipped.
    g.program.types[2].variant_payloads.clear();
    CoreConstructExpr ctor;
    ctor.type_name = "Flag";
    ctor.variant_name = "On";
    ctor.is_enum_variant = true;
    ctor.type_id = CoreTypeId{2};
    ctor.variant = CoreVariantId{0};
    ctor.resolved = true;
    ctor.args = {}; // arity unknown must NOT be a free pass
    g.flow->exprs.push_back(CoreExpr{std::move(ctor), std::nullopt});
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kTypeTableShapeInvalid));
}

