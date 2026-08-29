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

TEST_CASE("verifier fails closed on an enum constructed as a plain struct literal (kind gate)") {
    GoodProgram g = make_good_program();
    // Flag is an Enum. A struct-literal constructor (is_enum_variant=false)
    // targeting it must be rejected — not silently pass the struct branch just
    // because an enum carries no fields.
    CoreConstructExpr ctor;
    ctor.type_name = "Flag";
    ctor.is_enum_variant = false; // masquerading as a struct construct
    ctor.type_id = CoreTypeId{2}; // Flag (an enum)
    ctor.resolved = true;
    ctor.args = {};
    g.flow->exprs.push_back(CoreExpr{std::move(ctor), std::nullopt});
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kConstructTypeInvalid));
}

TEST_CASE("verifier fails closed on a struct constructed as an enum variant (kind gate)") {
    GoodProgram g = make_good_program();
    // Ctx is a Struct. An enum-variant constructor targeting it must be rejected.
    CoreConstructExpr ctor;
    ctor.type_name = "Ctx";
    ctor.variant_name = "Bogus";
    ctor.is_enum_variant = true; // masquerading as an enum variant
    ctor.type_id = CoreTypeId{1}; // Ctx (a struct)
    ctor.variant = CoreVariantId{0};
    ctor.resolved = true;
    ctor.args = {};
    g.flow->exprs.push_back(CoreExpr{std::move(ctor), std::nullopt});
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kConstructTypeInvalid));
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


// --- ③-1 match-pattern arena ---
//
// The good program's Flag enum has two Unit variants (On=0, Off=1, arity 0).
// These build a pattern arena on the flow and tamper one invariant each.

TEST_CASE("verifier accepts a well-formed pattern arena") {
    GoodProgram g = make_good_program();
    // patterns: [0] wildcard, [1] Flag::On (unit variant), [2] (On | wildcard)
    g.flow->patterns.push_back(CorePattern{CoreWildcardPat{}, std::nullopt});
    CoreVariantPat on;
    on.owner_enum = CoreTypeId{2}; // Flag
    on.variant = CoreVariantId{0}; // On (unit, arity 0)
    g.flow->patterns.push_back(CorePattern{on, std::nullopt});
    CoreOrPat orp;
    orp.alternatives = {CorePatternId{1}, CorePatternId{0}};
    g.flow->patterns.push_back(CorePattern{orp, std::nullopt});
    const auto result = verify_core_program(g.program);
    for (const auto &d : result.diagnostics) {
        INFO("unexpected diagnostic: " << d.code << " — " << d.message);
        CHECK(false);
    }
    CHECK(result.ok());
}

TEST_CASE("verifier fails closed on an out-of-range pattern id") {
    GoodProgram g = make_good_program();
    CoreOrPat orp;
    orp.alternatives = {CorePatternId{0}, CorePatternId{99}}; // 99 out of range
    g.flow->patterns.push_back(CorePattern{CoreWildcardPat{}, std::nullopt});
    g.flow->patterns.push_back(CorePattern{orp, std::nullopt});
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kPatternIdOutOfRange));
}

TEST_CASE("verifier fails closed on a self-referential pattern (cycle)") {
    GoodProgram g = make_good_program();
    // pattern #0 = binding whose nested is itself.
    CoreBindingPat b;
    b.has_nested = true;
    b.nested = CorePatternId{0};
    g.flow->patterns.push_back(CorePattern{b, std::nullopt});
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kPatternCycle));
}

TEST_CASE("verifier fails closed on a variant pattern whose owner is not an enum") {
    GoodProgram g = make_good_program();
    CoreVariantPat v;
    v.owner_enum = CoreTypeId{1}; // Ctx is a struct, not an enum
    v.variant = CoreVariantId{0};
    g.flow->patterns.push_back(CorePattern{v, std::nullopt});
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kPatternVariantInvalid));
}

TEST_CASE("verifier fails closed on a variant pattern with an out-of-range variant id") {
    GoodProgram g = make_good_program();
    CoreVariantPat v;
    v.owner_enum = CoreTypeId{2}; // Flag
    v.variant = CoreVariantId{7}; // Flag has 2 variants
    g.flow->patterns.push_back(CorePattern{v, std::nullopt});
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kPatternVariantInvalid));
}

TEST_CASE("verifier fails closed on a unit variant pattern given tuple subpatterns (arity)") {
    GoodProgram g = make_good_program();
    g.flow->patterns.push_back(CorePattern{CoreWildcardPat{}, std::nullopt}); // #0
    CoreVariantPat v;
    v.owner_enum = CoreTypeId{2};   // Flag
    v.variant = CoreVariantId{0};   // On (arity 0)
    v.tuple_subpatterns = {CorePatternId{0}}; // 1 subpattern vs arity 0
    g.flow->patterns.push_back(CorePattern{v, std::nullopt});
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kPatternPayloadArity));
}

TEST_CASE("verifier fails closed on an or-pattern with fewer than two alternatives") {
    GoodProgram g = make_good_program();
    g.flow->patterns.push_back(CorePattern{CoreWildcardPat{}, std::nullopt}); // #0
    CoreOrPat orp;
    orp.alternatives = {CorePatternId{0}}; // only one
    g.flow->patterns.push_back(CorePattern{orp, std::nullopt});
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kPatternShapeInvalid));
}

// --- ③-3a pattern-kind completeness: IntRange + Tuple ---

TEST_CASE("verifier accepts a well-formed IntRange + Tuple pattern arena") {
    GoodProgram g = make_good_program();
    // #0 int-range 1..10, #1 wildcard, #2 tuple (int-range, wildcard)
    g.flow->patterns.push_back(CorePattern{CoreIntRangePat{1, 10}, std::nullopt});
    g.flow->patterns.push_back(CorePattern{CoreWildcardPat{}, std::nullopt});
    CoreTuplePat t;
    t.elements = {CorePatternId{0}, CorePatternId{1}};
    g.flow->patterns.push_back(CorePattern{t, std::nullopt});
    const auto result = verify_core_program(g.program);
    for (const auto &d : result.diagnostics) {
        INFO("unexpected diagnostic: " << d.code << " — " << d.message);
        CHECK(false);
    }
    CHECK(result.ok());
}

TEST_CASE("verifier fails closed on a reverse int-range pattern (start > end)") {
    GoodProgram g = make_good_program();
    g.flow->patterns.push_back(CorePattern{CoreIntRangePat{10, 1}, std::nullopt}); // reverse
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kPatternShapeInvalid));
}

TEST_CASE("verifier fails closed on a tuple pattern with an out-of-range element id") {
    GoodProgram g = make_good_program();
    CoreTuplePat t;
    t.elements = {CorePatternId{0}, CorePatternId{99}}; // 99 out of range
    g.flow->patterns.push_back(CorePattern{CoreWildcardPat{}, std::nullopt}); // #0
    g.flow->patterns.push_back(CorePattern{t, std::nullopt});                 // #1
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kPatternIdOutOfRange));
}

TEST_CASE("verifier fails closed on a self-referential tuple pattern (cycle)") {
    GoodProgram g = make_good_program();
    // pattern #0 = tuple whose only element is itself.
    CoreTuplePat t;
    t.elements = {CorePatternId{0}};
    g.flow->patterns.push_back(CorePattern{t, std::nullopt});
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kPatternCycle));
}

// --- ③-2 CoreMatchStmt / CoreYieldStmt / CoreTrapStmt ---
//
// Build a statement match into the Done handler: define a fresh scrutinee value,
// then `match %s { _ => <yield none> } fallback { trap }`. Helpers bump
// value_count for the fresh ids these tests introduce.

namespace {
// Replace Done's body with: %sid = lit; <match>; (match provided by caller).
// Returns a reference to the match for tampering.
CoreMatchStmt make_wildcard_match(std::uint32_t scrutinee, bool trap_fallback) {
    CoreMatchStmt m;
    m.scrutinee = CoreValueId{scrutinee};
    m.has_result = false;
    // one wildcard arm whose body yields nothing (statement match)
    CoreMatchArm arm;
    arm.pattern = CorePatternId{0}; // caller ensures pattern #0 is a wildcard
    arm.body = std::make_unique<CoreRegion>();
    arm.body->statements.push_back(CoreStmt{CoreYieldStmt{false, CoreValueId{}}, std::nullopt});
    m.arms.push_back(std::move(arm));
    m.fallback_region = std::make_unique<CoreRegion>();
    if (trap_fallback) {
        m.fallback_region->statements.push_back(
            CoreStmt{CoreTrapStmt{CoreTrapKind::NonExhaustiveMatch}, std::nullopt});
    } else {
        m.fallback_region->statements.push_back(
            CoreStmt{CoreYieldStmt{false, CoreValueId{}}, std::nullopt});
    }
    return m;
}
} // namespace

TEST_CASE("verifier accepts a well-formed statement match") {
    GoodProgram g = make_good_program();
    g.flow->patterns.push_back(CorePattern{CoreWildcardPat{}, std::nullopt}); // #0
    const std::uint32_t sid = g.flow->value_count; // fresh scrutinee id
    g.flow->value_count += 1;
    auto &done = g.flow->states[1].body;
    done.statements.clear();
    done.statements.push_back(
        CoreStmt{CoreLetStmt{CoreValueId{sid}, CoreExprId{0}}, std::nullopt}); // %sid = lit1
    done.statements.push_back(CoreStmt{make_wildcard_match(sid, /*trap_fallback=*/false), std::nullopt});
    done.statements.push_back(CoreStmt{CoreReturnStmt{false, CoreValueId{}}, std::nullopt});
    const auto result = verify_core_program(g.program);
    for (const auto &d : result.diagnostics) {
        INFO("unexpected diagnostic: " << d.code << " — " << d.message);
        CHECK(false);
    }
    CHECK(result.ok());
}

TEST_CASE("verifier fails closed on a yield in an ordinary flow region") {
    GoodProgram g = make_good_program();
    auto &done = g.flow->states[1].body;
    done.statements.clear();
    done.statements.push_back(CoreStmt{CoreYieldStmt{false, CoreValueId{}}, std::nullopt});
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kYieldOutsideMatchArm));
}

TEST_CASE("verifier fails closed on a match without a fallback region") {
    GoodProgram g = make_good_program();
    g.flow->patterns.push_back(CorePattern{CoreWildcardPat{}, std::nullopt}); // #0
    const std::uint32_t sid = g.flow->value_count;
    g.flow->value_count += 1;
    auto match = make_wildcard_match(sid, /*trap_fallback=*/false);
    match.fallback_region.reset(); // remove fallback
    auto &done = g.flow->states[1].body;
    done.statements.clear();
    done.statements.push_back(CoreStmt{CoreLetStmt{CoreValueId{sid}, CoreExprId{0}}, std::nullopt});
    done.statements.push_back(CoreStmt{std::move(match), std::nullopt});
    done.statements.push_back(CoreStmt{CoreReturnStmt{false, CoreValueId{}}, std::nullopt});
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kMatchNotTotal));
}

TEST_CASE("verifier fails closed on a statement match arm that falls through (no yield)") {
    GoodProgram g = make_good_program();
    g.flow->patterns.push_back(CorePattern{CoreWildcardPat{}, std::nullopt}); // #0
    const std::uint32_t sid = g.flow->value_count;
    g.flow->value_count += 1;
    auto match = make_wildcard_match(sid, /*trap_fallback=*/true);
    match.arms[0].body->statements.clear(); // arm body no longer yields -> fallthrough
    auto &done = g.flow->states[1].body;
    done.statements.clear();
    done.statements.push_back(CoreStmt{CoreLetStmt{CoreValueId{sid}, CoreExprId{0}}, std::nullopt});
    done.statements.push_back(CoreStmt{std::move(match), std::nullopt});
    done.statements.push_back(CoreStmt{CoreReturnStmt{false, CoreValueId{}}, std::nullopt});
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kMatchArmYield));
}

TEST_CASE("verifier fails closed on a statement match arm that yields a value") {
    GoodProgram g = make_good_program();
    g.flow->patterns.push_back(CorePattern{CoreWildcardPat{}, std::nullopt}); // #0
    const std::uint32_t sid = g.flow->value_count;
    g.flow->value_count += 1;
    auto match = make_wildcard_match(sid, /*trap_fallback=*/true);
    // statement match arm yields a VALUE (sid) — illegal for a unit arm.
    match.arms[0].body->statements.clear();
    match.arms[0].body->statements.push_back(
        CoreStmt{CoreYieldStmt{true, CoreValueId{sid}}, std::nullopt});
    auto &done = g.flow->states[1].body;
    done.statements.clear();
    done.statements.push_back(CoreStmt{CoreLetStmt{CoreValueId{sid}, CoreExprId{0}}, std::nullopt});
    done.statements.push_back(CoreStmt{std::move(match), std::nullopt});
    done.statements.push_back(CoreStmt{CoreReturnStmt{false, CoreValueId{}}, std::nullopt});
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kMatchArmYield));
}

TEST_CASE("verifier accepts an if-both-branches-yield arm body (per-path yield)") {
    GoodProgram g = make_good_program();
    g.flow->patterns.push_back(CorePattern{CoreWildcardPat{}, std::nullopt}); // #0
    const std::uint32_t sid = g.flow->value_count;
    g.flow->value_count += 1;
    auto match = make_wildcard_match(sid, /*trap_fallback=*/true);
    // arm body: if %sid { yield } else { yield } — both paths yield unit.
    match.arms[0].body->statements.clear();
    CoreIfStmt iff;
    iff.condition = CoreValueId{sid};
    iff.then_region = std::make_unique<CoreRegion>();
    iff.then_region->statements.push_back(CoreStmt{CoreYieldStmt{false, CoreValueId{}}, std::nullopt});
    iff.else_region = std::make_unique<CoreRegion>();
    iff.else_region->statements.push_back(CoreStmt{CoreYieldStmt{false, CoreValueId{}}, std::nullopt});
    match.arms[0].body->statements.push_back(CoreStmt{std::move(iff), std::nullopt});
    auto &done = g.flow->states[1].body;
    done.statements.clear();
    done.statements.push_back(CoreStmt{CoreLetStmt{CoreValueId{sid}, CoreExprId{0}}, std::nullopt});
    done.statements.push_back(CoreStmt{std::move(match), std::nullopt});
    done.statements.push_back(CoreStmt{CoreReturnStmt{false, CoreValueId{}}, std::nullopt});
    const auto result = verify_core_program(g.program);
    for (const auto &d : result.diagnostics) {
        INFO("unexpected diagnostic: " << d.code << " — " << d.message);
        CHECK(false);
    }
    CHECK(result.ok());
}

TEST_CASE("verifier fails closed on a match arm binding defined more than once (flow-global SSA)") {
    GoodProgram g = make_good_program();
    // pattern #0 = binding pattern naming arm binding 0.
    CoreBindingPat bp;
    bp.binding = CorePatternBindingId{0};
    g.flow->patterns.push_back(CorePattern{bp, std::nullopt});
    const std::uint32_t sid = g.flow->value_count;
    const std::uint32_t bind_v = sid + 1;
    g.flow->value_count += 2;
    CoreMatchStmt m;
    m.scrutinee = CoreValueId{sid};
    m.has_result = false;
    CoreMatchArm arm;
    arm.pattern = CorePatternId{0};
    arm.bindings.push_back(CorePatternBinding{CoreValueId{bind_v}});
    // The arm body defines bind_v AGAIN via a let -> flow-global redefinition.
    arm.body = std::make_unique<CoreRegion>();
    arm.body->statements.push_back(
        CoreStmt{CoreLetStmt{CoreValueId{bind_v}, CoreExprId{0}}, std::nullopt});
    arm.body->statements.push_back(CoreStmt{CoreYieldStmt{false, CoreValueId{}}, std::nullopt});
    m.arms.push_back(std::move(arm));
    m.fallback_region = std::make_unique<CoreRegion>();
    m.fallback_region->statements.push_back(
        CoreStmt{CoreTrapStmt{CoreTrapKind::NonExhaustiveMatch}, std::nullopt});
    auto &done = g.flow->states[1].body;
    done.statements.clear();
    done.statements.push_back(CoreStmt{CoreLetStmt{CoreValueId{sid}, CoreExprId{0}}, std::nullopt});
    done.statements.push_back(CoreStmt{std::move(m), std::nullopt});
    done.statements.push_back(CoreStmt{CoreReturnStmt{false, CoreValueId{}}, std::nullopt});
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kValueRedefined));
}

// --- ③-2 forward-fix: arm binding <-> pattern binding-site bijection ---

TEST_CASE("verifier fails closed on an arm that declares a binding the pattern never binds") {
    GoodProgram g = make_good_program();
    g.flow->patterns.push_back(CorePattern{CoreWildcardPat{}, std::nullopt}); // #0 wildcard binds nothing
    const std::uint32_t sid = g.flow->value_count;
    const std::uint32_t bind_v = sid + 1;
    g.flow->value_count += 2;
    CoreMatchStmt m;
    m.scrutinee = CoreValueId{sid};
    CoreMatchArm arm;
    arm.pattern = CorePatternId{0};
    arm.bindings.push_back(CorePatternBinding{CoreValueId{bind_v}}); // extra, unbound-by-pattern
    arm.body = std::make_unique<CoreRegion>();
    arm.body->statements.push_back(CoreStmt{CoreYieldStmt{false, CoreValueId{}}, std::nullopt});
    m.arms.push_back(std::move(arm));
    m.fallback_region = std::make_unique<CoreRegion>();
    m.fallback_region->statements.push_back(
        CoreStmt{CoreTrapStmt{CoreTrapKind::NonExhaustiveMatch}, std::nullopt});
    auto &done = g.flow->states[1].body;
    done.statements.clear();
    done.statements.push_back(CoreStmt{CoreLetStmt{CoreValueId{sid}, CoreExprId{0}}, std::nullopt});
    done.statements.push_back(CoreStmt{std::move(m), std::nullopt});
    done.statements.push_back(CoreStmt{CoreReturnStmt{false, CoreValueId{}}, std::nullopt});
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kPatternBindingInvalid));
}

TEST_CASE("verifier fails closed on two tuple slots reusing one arm binding id") {
    GoodProgram g = make_good_program();
    // A struct-payload variant with 2 slots on Ctx? No — use a tuple: reuse the
    // Flag enum's On variant but give it a synthetic 2-arity tuple payload here.
    // Simpler: add a dedicated enum type with a 2-tuple variant.
    CoreTypeDecl pair_enum;
    pair_enum.kind = CoreTypeDecl::Kind::Enum;
    pair_enum.name = "PairE";
    pair_enum.variants = {"Both"};
    CoreTypeDecl::VariantPayload pv;
    pv.kind = CoreTypeDecl::VariantPayload::Kind::Tuple;
    pv.slot_types = {CoreTypeId{}, CoreTypeId{}}; // arity 2, primitive slots
    pair_enum.variant_payloads = {pv};
    g.program.types.push_back(std::move(pair_enum));
    const auto pair_ty = static_cast<std::uint32_t>(g.program.types.size() - 1);

    // patterns: #0,#1 binding patterns both naming arm binding 0; #2 = Both(#0,#1)
    CoreBindingPat b0;
    b0.binding = CorePatternBindingId{0};
    g.flow->patterns.push_back(CorePattern{b0, std::nullopt}); // #0
    CoreBindingPat b1;
    b1.binding = CorePatternBindingId{0}; // SAME binding id -> ambiguous
    g.flow->patterns.push_back(CorePattern{b1, std::nullopt}); // #1
    CoreVariantPat both;
    both.owner_enum = CoreTypeId{pair_ty};
    both.variant = CoreVariantId{0};
    both.tuple_subpatterns = {CorePatternId{0}, CorePatternId{1}};
    g.flow->patterns.push_back(CorePattern{both, std::nullopt}); // #2

    const std::uint32_t sid = g.flow->value_count;
    const std::uint32_t bind_v = sid + 1;
    g.flow->value_count += 2;
    CoreMatchStmt m;
    m.scrutinee = CoreValueId{sid};
    CoreMatchArm arm;
    arm.pattern = CorePatternId{2};
    arm.bindings.push_back(CorePatternBinding{CoreValueId{bind_v}}); // one binding, referenced twice
    arm.body = std::make_unique<CoreRegion>();
    arm.body->statements.push_back(CoreStmt{CoreYieldStmt{false, CoreValueId{}}, std::nullopt});
    m.arms.push_back(std::move(arm));
    m.fallback_region = std::make_unique<CoreRegion>();
    m.fallback_region->statements.push_back(
        CoreStmt{CoreTrapStmt{CoreTrapKind::NonExhaustiveMatch}, std::nullopt});
    auto &done = g.flow->states[1].body;
    done.statements.clear();
    done.statements.push_back(CoreStmt{CoreLetStmt{CoreValueId{sid}, CoreExprId{0}}, std::nullopt});
    done.statements.push_back(CoreStmt{std::move(m), std::nullopt});
    done.statements.push_back(CoreStmt{CoreReturnStmt{false, CoreValueId{}}, std::nullopt});
    const auto result = verify_core_program(g.program);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result, verify::kPatternBindingInvalid));
}

// --- (3)-3c forward-fix: shared structural may-fallthrough helper ---
//
// core_region_may_fallthrough is the ONE definition the lowerer (seal) and the
// verifier's per-path analysis both rely on. These fixtures lock its per-path
// semantics so the two never drift.
namespace {
CoreStmt let_stmt() { return CoreStmt{CoreLetStmt{CoreValueId{0}, CoreExprId{0}}, std::nullopt}; }
CoreStmt return_stmt() { return CoreStmt{CoreReturnStmt{false, CoreValueId{}}, std::nullopt}; }
CoreStmt goto_stmt() { return CoreStmt{CoreGotoStmt{CoreStateId{0}, ""}, std::nullopt}; }
CoreStmt yield_stmt() { return CoreStmt{CoreYieldStmt{false, CoreValueId{}}, std::nullopt}; }
CoreStmt trap_stmt() {
    return CoreStmt{CoreTrapStmt{CoreTrapKind::NonExhaustiveMatch}, std::nullopt};
}
std::unique_ptr<CoreRegion> region_of(CoreStmt stmt) {
    auto r = std::make_unique<CoreRegion>();
    r->statements.push_back(std::move(stmt));
    return r;
}
} // namespace

TEST_CASE("core_region_may_fallthrough: straight-line + terminators") {
    CoreRegion empty;
    CHECK(core_region_may_fallthrough(empty)); // empty region falls through
    CoreRegion only_let;
    only_let.statements.push_back(let_stmt());
    CHECK(core_region_may_fallthrough(only_let)); // let falls through
    {
        CoreRegion r;
        r.statements.push_back(return_stmt());
        CHECK_FALSE(core_region_may_fallthrough(r));
    }
    {
        CoreRegion r;
        r.statements.push_back(goto_stmt());
        CHECK_FALSE(core_region_may_fallthrough(r));
    }
    {
        CoreRegion r;
        r.statements.push_back(yield_stmt());
        CHECK_FALSE(core_region_may_fallthrough(r));
    }
    {
        CoreRegion r;
        r.statements.push_back(trap_stmt());
        CHECK_FALSE(core_region_may_fallthrough(r));
    }
}

TEST_CASE("core_region_may_fallthrough: if terminates iff BOTH branches terminate") {
    // if { return } else { return } -> both terminate -> region does NOT fall through.
    CoreIfStmt both;
    both.condition = CoreValueId{0};
    both.then_region = region_of(return_stmt());
    both.else_region = region_of(goto_stmt());
    CoreRegion r_both;
    r_both.statements.push_back(CoreStmt{std::move(both), std::nullopt});
    CHECK_FALSE(core_region_may_fallthrough(r_both));

    // if { return } else { let } -> else falls through -> region falls through.
    CoreIfStmt one;
    one.condition = CoreValueId{0};
    one.then_region = region_of(return_stmt());
    one.else_region = region_of(let_stmt());
    CoreRegion r_one;
    r_one.statements.push_back(CoreStmt{std::move(one), std::nullopt});
    CHECK(core_region_may_fallthrough(r_one));

    // else-less if { return } -> can skip the then-branch -> falls through.
    CoreIfStmt no_else;
    no_else.condition = CoreValueId{0};
    no_else.then_region = region_of(return_stmt());
    CoreRegion r_no_else;
    r_no_else.statements.push_back(CoreStmt{std::move(no_else), std::nullopt});
    CHECK(core_region_may_fallthrough(r_no_else));
}

TEST_CASE("core_region_may_fallthrough: match terminates iff all arms + fallback terminate") {
    const auto make_match = [](bool arm_terminates, bool fallback_terminates) {
        CoreMatchStmt m;
        m.scrutinee = CoreValueId{0};
        CoreMatchArm arm;
        arm.pattern = CorePatternId{0};
        // A "terminating" arm body ends in goto; a "falls through" arm body is
        // let-only (a yield would also terminate, but this keeps intent clear).
        arm.body = arm_terminates ? region_of(goto_stmt()) : region_of(let_stmt());
        m.arms.push_back(std::move(arm));
        m.fallback_region =
            fallback_terminates ? region_of(trap_stmt()) : region_of(let_stmt());
        CoreRegion r;
        r.statements.push_back(CoreStmt{std::move(m), std::nullopt});
        return r;
    };
    CHECK_FALSE(core_region_may_fallthrough(make_match(true, true)));  // all terminate
    CHECK(core_region_may_fallthrough(make_match(false, true)));       // arm falls through
    CHECK(core_region_may_fallthrough(make_match(true, false)));       // fallback falls through
}
