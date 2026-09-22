#include <doctest.h>

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/core_verify.hpp"
#include "ahfl/compiler/ir/typed_hir_lower.hpp"
#include "ahfl/compiler/ir/verify.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace {

using namespace ahfl;

constexpr std::string_view kScalarSource = R"AHFL(
module coercion;

struct Box<T> { value: T; }
struct Sink<T> { consume: Fn(T) -> Unit; }
struct Req { narrow: Int(0, 0); boxed: Box<Int(0, 0)>; sink: Sink<Int>; }
struct Ctx { seen: Int = 0; }
struct Resp { value: Int; }
enum Maybe { Some(Int), None, }

agent A {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for A {
    state Done {
        let wide: Int = input.narrow;
        let boxed_wide: Box<Int> = input.boxed;
        let sink_narrow: Sink<Int(0, 0)> = input.sink;
        let variant = Maybe::Some(input.narrow);
        let owner: Maybe = variant;
        return Resp { value: wide };
    }
}
)AHFL";

struct LoweredFixture {
    TypedProgram typed;
    ir::Program ir;
};

[[nodiscard]] std::optional<LoweredFixture> lower_scalar_fixture() {
    const Frontend frontend;
    auto parse = frontend.parse_text("coercion_adjustment.ahfl", std::string(kScalarSource));
    if (parse.has_errors() || parse.program == nullptr) {
        return std::nullopt;
    }
    const Resolver resolver;
    const auto resolve = resolver.resolve(*parse.program);
    if (resolve.has_errors()) {
        return std::nullopt;
    }
    const TypeChecker checker;
    auto result = checker.check(*parse.program, resolve);
    if (result.has_errors()) {
        return std::nullopt;
    }
    auto lowered = lower_typed_program(result.typed_program, *parse.program);
    return LoweredFixture{.typed = std::move(result.typed_program), .ir = std::move(lowered)};
}

[[nodiscard]] ir::LetStatement *first_let(ir::Program &program) {
    for (auto &decl : program.declarations) {
        auto *flow = std::get_if<ir::FlowDecl>(&decl);
        if (flow == nullptr) {
            continue;
        }
        for (auto &handler : flow->state_handlers) {
            for (auto &statement : handler.body.statements) {
                if (statement != nullptr) {
                    if (auto *let = std::get_if<ir::LetStatement>(&statement->node)) {
                        return let;
                    }
                }
            }
        }
    }
    return nullptr;
}

[[nodiscard]] ir::LetStatement *find_let(ir::Program &program, std::string_view name) {
    for (auto &decl : program.declarations) {
        auto *flow = std::get_if<ir::FlowDecl>(&decl);
        if (flow == nullptr) {
            continue;
        }
        for (auto &handler : flow->state_handlers) {
            for (auto &statement : handler.body.statements) {
                if (statement != nullptr) {
                    if (auto *let = std::get_if<ir::LetStatement>(&statement->node);
                        let != nullptr && let->name == name) {
                        return let;
                    }
                }
            }
        }
    }
    return nullptr;
}

[[nodiscard]] bool has_adjustment_error(const ir::VerificationResult &result) {
    for (const auto &diagnostic : result.diagnostics) {
        if (diagnostic.path.find("adjustment") != std::string::npos) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool has_core_lower_code(const ir::core::CoreLowerResult &result,
                                       std::string_view code) {
    for (const auto &diagnostic : result.diagnostics) {
        if (diagnostic.severity == ir::core::CoreDiagnosticSeverity::Error &&
            diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] ir::TypeRef
int_ref(std::optional<std::pair<std::int64_t, std::int64_t>> bounds = std::nullopt) {
    ir::TypeRef type;
    type.kind = bounds.has_value() ? ir::TypeRefKind::BoundedInt : ir::TypeRefKind::Int;
    type.int_bounds = bounds;
    return type;
}

[[nodiscard]] ir::TypeRef list_ref(ir::TypeRef element, std::uint64_t capacity) {
    ir::TypeRef type;
    type.kind = ir::TypeRefKind::Struct;
    type.canonical_name = "std::collections::List";
    type.nominal_ref =
        ir::SymbolRef{.kind = ir::SymbolRefKind::Type, .canonical_name = "std::collections::List"};
    type.collection_capacity = capacity;
    type.params.push_back(std::make_unique<ir::TypeRef>(std::move(element)));
    return type;
}

[[nodiscard]] ir::TypeRef fn_ref(ir::TypeRef param, ir::TypeRef result) {
    ir::TypeRef type;
    type.kind = ir::TypeRefKind::Fn;
    type.params.push_back(std::make_unique<ir::TypeRef>(std::move(param)));
    type.first = std::make_unique<ir::TypeRef>(std::move(result));
    return type;
}

[[nodiscard]] std::vector<const ir::core::CoreCoerceExpr *>
collect_core_coercions(const ir::core::CoreFlowDecl &flow) {
    std::vector<const ir::core::CoreCoerceExpr *> out;
    for (const auto &expr : flow.storage.exprs) {
        if (const auto *coerce = std::get_if<ir::core::CoreCoerceExpr>(&expr.node)) {
            out.push_back(coerce);
        }
    }
    return out;
}

} // namespace

TEST_CASE("F2 real annotated let carries the Sema IntWiden witness into BackendReady IR") {
    auto fixture = lower_scalar_fixture();
    REQUIRE(fixture.has_value());

    const TypedStatement *typed_let = nullptr;
    for (const auto &statement : fixture->typed.statements) {
        if (statement.kind == TypedStmtKind::Let && statement.target_name == "wide") {
            typed_let = &statement;
            break;
        }
    }
    REQUIRE(typed_let != nullptr);
    REQUIRE(typed_let->let_adjustment.has_value());
    REQUIRE(typed_let->let_adjustment->nodes.size() == 1);
    REQUIRE(typed_let->let_adjustment->nodes[0].ops.size() == 1);
    CHECK(typed_let->let_adjustment->nodes[0].ops[0].kind == TypedAdjustmentOpKind::IntWiden);

    auto *let = first_let(fixture->ir);
    REQUIRE(let != nullptr);
    REQUIRE(let->adjustment.has_value());
    REQUIRE(let->adjustment->nodes.size() == 1);
    CHECK(let->adjustment->nodes[0].ops[0].kind == ir::AdjustmentOpKind::IntWiden);
    CHECK_FALSE(
        ir::verify_ir_program(fixture->ir, ir::IrVerificationMode::BackendReady).has_errors());

    auto *boxed = find_let(fixture->ir, "boxed_wide");
    REQUIRE(boxed != nullptr);
    REQUIRE(boxed->adjustment.has_value());
    const auto &boxed_root = boxed->adjustment->nodes[boxed->adjustment->root];
    REQUIRE(boxed_root.ops.size() == 1);
    CHECK(boxed_root.ops[0].kind == ir::AdjustmentOpKind::TypeArg);
    CHECK(boxed_root.ops[0].arg_index == 0);
    REQUIRE(boxed_root.ops[0].child < boxed->adjustment->nodes.size());
    CHECK(boxed->adjustment->nodes[boxed_root.ops[0].child].ops[0].kind ==
          ir::AdjustmentOpKind::IntWiden);

    auto *sink = find_let(fixture->ir, "sink_narrow");
    REQUIRE(sink != nullptr);
    REQUIRE(sink->adjustment.has_value());
    const auto &sink_root = sink->adjustment->nodes[sink->adjustment->root];
    REQUIRE(sink_root.ops.size() == 1);
    CHECK(sink_root.ops[0].kind == ir::AdjustmentOpKind::TypeArg);
    const auto &sink_child = sink->adjustment->nodes[sink_root.ops[0].child];
    CHECK(sink_child.source.kind == ir::TypeRefKind::BoundedInt);
    CHECK(sink_child.target.kind == ir::TypeRefKind::Int);

    auto *owner = find_let(fixture->ir, "owner");
    REQUIRE(owner != nullptr);
    REQUIRE(owner->adjustment.has_value());
    const auto &owner_root = owner->adjustment->nodes[owner->adjustment->root];
    REQUIRE(owner_root.ops.size() == 1);
    CHECK(owner_root.ops[0].kind == ir::AdjustmentOpKind::VariantToEnum);
}

TEST_CASE("F2 BackendReady adjustment gate rejects missing or malformed witnesses") {
    SUBCASE("top and bottom boundary leaf operations are accepted") {
        auto to_any = lower_scalar_fixture();
        REQUIRE(to_any.has_value());
        auto *let = first_let(to_any->ir);
        REQUIRE(let != nullptr);
        REQUIRE(let->adjustment.has_value());
        let->type_ref.kind = ir::TypeRefKind::Any;
        let->adjustment->target.kind = ir::TypeRefKind::Any;
        auto &node = let->adjustment->nodes[let->adjustment->root];
        node.target.kind = ir::TypeRefKind::Any;
        node.ops[0].kind = ir::AdjustmentOpKind::ToAny;
        CHECK_FALSE(
            ir::verify_ir_program(to_any->ir, ir::IrVerificationMode::BackendReady).has_errors());

        auto from_never = lower_scalar_fixture();
        REQUIRE(from_never.has_value());
        let = first_let(from_never->ir);
        REQUIRE(let != nullptr);
        REQUIRE(let->adjustment.has_value());
        let->initializer->resolved_type.kind = ir::TypeRefKind::Never;
        let->adjustment->source.kind = ir::TypeRefKind::Never;
        auto &never_node = let->adjustment->nodes[let->adjustment->root];
        never_node.source.kind = ir::TypeRefKind::Never;
        never_node.ops[0].kind = ir::AdjustmentOpKind::FromNever;
        CHECK_FALSE(ir::verify_ir_program(from_never->ir, ir::IrVerificationMode::BackendReady)
                        .has_errors());
    }

    SUBCASE("missing plan at a non-identity let boundary") {
        auto fixture = lower_scalar_fixture();
        REQUIRE(fixture.has_value());
        auto *let = first_let(fixture->ir);
        REQUIRE(let != nullptr);
        let->adjustment.reset();
        const auto verified =
            ir::verify_ir_program(fixture->ir, ir::IrVerificationMode::BackendReady);
        CHECK(verified.has_errors());
        CHECK(has_adjustment_error(verified));
    }

    SUBCASE("plan boundary source drifts from initializer type") {
        auto fixture = lower_scalar_fixture();
        REQUIRE(fixture.has_value());
        auto *let = first_let(fixture->ir);
        REQUIRE(let != nullptr);
        REQUIRE(let->adjustment.has_value());
        let->adjustment->source.kind = ir::TypeRefKind::String;
        const auto verified =
            ir::verify_ir_program(fixture->ir, ir::IrVerificationMode::BackendReady);
        CHECK(verified.has_errors());
        CHECK(has_adjustment_error(verified));
    }

    SUBCASE("IntWiden is tampered onto a nominal node") {
        auto fixture = lower_scalar_fixture();
        REQUIRE(fixture.has_value());
        auto *let = first_let(fixture->ir);
        REQUIRE(let != nullptr);
        REQUIRE(let->adjustment.has_value());
        auto &node = let->adjustment->nodes[let->adjustment->root];
        node.source.kind = ir::TypeRefKind::Struct;
        node.target.kind = ir::TypeRefKind::Struct;
        const auto verified =
            ir::verify_ir_program(fixture->ir, ir::IrVerificationMode::BackendReady);
        CHECK(verified.has_errors());
        CHECK(has_adjustment_error(verified));
    }

    SUBCASE("VariantToEnum names a variant that the owner does not declare") {
        auto fixture = lower_scalar_fixture();
        REQUIRE(fixture.has_value());
        auto *let = find_let(fixture->ir, "owner");
        REQUIRE(let != nullptr);
        REQUIRE(let->adjustment.has_value());
        auto &node = let->adjustment->nodes[let->adjustment->root];
        node.source.variant_name = "Missing";
        let->adjustment->source.variant_name = "Missing";
        let->initializer->resolved_type.variant_name = "Missing";
        const auto verified =
            ir::verify_ir_program(fixture->ir, ir::IrVerificationMode::BackendReady);
        CHECK(verified.has_errors());
        CHECK(has_adjustment_error(verified));
    }

    SUBCASE("projected child index is out of range") {
        auto fixture = lower_scalar_fixture();
        REQUIRE(fixture.has_value());
        auto *let = first_let(fixture->ir);
        REQUIRE(let != nullptr);
        REQUIRE(let->adjustment.has_value());
        auto &op = let->adjustment->nodes[let->adjustment->root].ops[0];
        op.kind = ir::AdjustmentOpKind::FnReturn;
        op.child = 99;
        const auto verified =
            ir::verify_ir_program(fixture->ir, ir::IrVerificationMode::BackendReady);
        CHECK(verified.has_errors());
        CHECK(has_adjustment_error(verified));
    }

    SUBCASE("projected child creates a cycle") {
        auto fixture = lower_scalar_fixture();
        REQUIRE(fixture.has_value());
        auto *let = first_let(fixture->ir);
        REQUIRE(let != nullptr);
        REQUIRE(let->adjustment.has_value());
        auto &op = let->adjustment->nodes[let->adjustment->root].ops[0];
        op.kind = ir::AdjustmentOpKind::FnReturn;
        op.child = let->adjustment->root;
        const auto verified =
            ir::verify_ir_program(fixture->ir, ir::IrVerificationMode::BackendReady);
        CHECK(verified.has_errors());
        CHECK(has_adjustment_error(verified));
    }

    SUBCASE("identity boundary carrying a plan is rejected") {
        auto fixture = lower_scalar_fixture();
        REQUIRE(fixture.has_value());
        auto *let = first_let(fixture->ir);
        REQUIRE(let != nullptr);
        REQUIRE(let->adjustment.has_value());
        let->type_ref = ir::clone_type_ref(let->initializer.get()->resolved_type);
        let->adjustment->target = ir::clone_type_ref(let->adjustment->source);
        let->adjustment->nodes[0].target = ir::clone_type_ref(let->adjustment->nodes[0].source);
        const auto verified =
            ir::verify_ir_program(fixture->ir, ir::IrVerificationMode::BackendReady);
        CHECK(verified.has_errors());
        CHECK(has_adjustment_error(verified));
    }
}

TEST_CASE("F3 Core lowering consumes real scalar and user-variance adjustment plans") {
    auto fixture = lower_scalar_fixture();
    REQUIRE(fixture.has_value());

    const auto lowered = ir::core::lower_ahfl_to_core(fixture->ir);
    for (const auto &diagnostic : lowered.diagnostics) {
        INFO(diagnostic.code << ": " << diagnostic.message);
    }
    REQUIRE(lowered.ok());
    REQUIRE(lowered.is_executable);
    REQUIRE(lowered.program.flows.size() == 1);
    const auto &flow = lowered.program.flows[0];
    const auto coercions = collect_core_coercions(flow);

    // `wide`, covariant `Box`, and contravariant `Sink` each produce a fresh
    // Core SSA value. VariantToEnum is erased because both endpoints intern to
    // the same nominal Core value type, so it leaves neither an expression nor
    // an orphan plan node.
    REQUIRE(coercions.size() == 3);
    REQUIRE(flow.storage.coercion_plans.size() == 5);
    std::size_t int_widen_nodes = 0;
    std::size_t type_arg_nodes = 0;
    for (const auto &node : flow.storage.coercion_plans) {
        REQUIRE_FALSE(node.ops.empty());
        if (node.ops[0].kind == ir::core::CoreCoercionOpKind::IntWiden) {
            ++int_widen_nodes;
        }
        if (node.ops[0].kind == ir::core::CoreCoercionOpKind::TypeArg) {
            ++type_arg_nodes;
        }
    }
    CHECK(int_widen_nodes == 3);
    CHECK(type_arg_nodes == 2);

    for (const auto *coerce : coercions) {
        REQUIRE(coerce->plan.value < flow.storage.coercion_plans.size());
        const auto &root = flow.storage.coercion_plans[coerce->plan.value];
        REQUIRE(coerce->operand.value < flow.storage.value_types.size());
        CHECK(flow.storage.value_types[coerce->operand.value] == root.source);

        bool found_fresh_result = false;
        for (const auto &state : flow.states) {
            for (const auto &statement : state.body.statements) {
                if (const auto *let = std::get_if<ir::core::CoreLetStmt>(&statement.node);
                    let != nullptr && let->expr.value < flow.storage.exprs.size() &&
                    std::get_if<ir::core::CoreCoerceExpr>(&flow.storage.exprs[let->expr.value].node) ==
                        coerce) {
                    CHECK(let->result.value != coerce->operand.value);
                    REQUIRE(let->result.value < flow.storage.value_types.size());
                    CHECK(flow.storage.value_types[let->result.value] == root.result);
                    found_fresh_result = true;
                }
            }
        }
        CHECK(found_fresh_result);
    }

    CHECK(ir::core::verify_core_program(lowered.program).ok());
}

TEST_CASE("F3 Core lowering preserves a composite capacity and element witness") {
    auto fixture = lower_scalar_fixture();
    REQUIRE(fixture.has_value());
    auto *let = first_let(fixture->ir);
    REQUIRE(let != nullptr);

    const auto source = list_ref(int_ref(std::pair<std::int64_t, std::int64_t>{0, 0}), 4);
    const auto target = list_ref(int_ref(), 8);
    let->initializer->resolved_type = ir::clone_type_ref(source);
    let->type_ref = ir::clone_type_ref(target);
    ir::AdjustmentPlan plan;
    plan.source = ir::clone_type_ref(source);
    plan.target = ir::clone_type_ref(target);
    plan.root = 0;
    ir::AdjustmentNode root;
    root.source = ir::clone_type_ref(source);
    root.target = ir::clone_type_ref(target);
    root.ops.push_back(ir::AdjustmentOp{.kind = ir::AdjustmentOpKind::CapacityWiden});
    root.ops.push_back(
        ir::AdjustmentOp{.kind = ir::AdjustmentOpKind::TypeArg, .arg_index = 0, .child = 1});
    ir::AdjustmentNode child;
    child.source = int_ref(std::pair<std::int64_t, std::int64_t>{0, 0});
    child.target = int_ref();
    child.ops.push_back(ir::AdjustmentOp{.kind = ir::AdjustmentOpKind::IntWiden});
    plan.nodes.push_back(std::move(root));
    plan.nodes.push_back(std::move(child));
    let->adjustment = std::move(plan);

    const auto lowered = ir::core::lower_ahfl_to_core(fixture->ir);
    for (const auto &diagnostic : lowered.diagnostics) {
        INFO(diagnostic.code << ": " << diagnostic.message);
    }
    REQUIRE(lowered.ok());
    REQUIRE(lowered.program.flows.size() == 1);
    const auto &flow = lowered.program.flows[0];
    const auto coercions = collect_core_coercions(flow);
    REQUIRE(coercions.size() == 3);

    bool found_composite = false;
    for (const auto &node : flow.storage.coercion_plans) {
        if (node.ops.size() != 2) {
            continue;
        }
        CHECK(node.ops[0].kind == ir::core::CoreCoercionOpKind::CapacityWiden);
        CHECK(node.ops[1].kind == ir::core::CoreCoercionOpKind::TypeArg);
        REQUIRE(node.ops[1].child.value < flow.storage.coercion_plans.size());
        const auto &element = flow.storage.coercion_plans[node.ops[1].child.value];
        REQUIRE(element.ops.size() == 1);
        CHECK(element.ops[0].kind == ir::core::CoreCoercionOpKind::IntWiden);
        found_composite = true;
    }
    CHECK(found_composite);
    CHECK(ir::core::verify_core_program(lowered.program).ok());
}

TEST_CASE("F3 Core lowering preserves function parameter and return variance directions") {
    auto fixture = lower_scalar_fixture();
    REQUIRE(fixture.has_value());
    auto *let = first_let(fixture->ir);
    REQUIRE(let != nullptr);

    const auto bounded = std::pair<std::int64_t, std::int64_t>{0, 0};
    const auto source = fn_ref(int_ref(), int_ref(bounded));
    const auto target = fn_ref(int_ref(bounded), int_ref());
    let->initializer->resolved_type = ir::clone_type_ref(source);
    let->type_ref = ir::clone_type_ref(target);
    ir::AdjustmentPlan plan;
    plan.source = ir::clone_type_ref(source);
    plan.target = ir::clone_type_ref(target);
    plan.root = 0;
    ir::AdjustmentNode root;
    root.source = ir::clone_type_ref(source);
    root.target = ir::clone_type_ref(target);
    root.ops.push_back(
        ir::AdjustmentOp{.kind = ir::AdjustmentOpKind::FnParam, .arg_index = 0, .child = 1});
    root.ops.push_back(ir::AdjustmentOp{.kind = ir::AdjustmentOpKind::FnReturn, .child = 2});
    ir::AdjustmentNode param;
    param.source = int_ref(bounded); // target parameter <: source parameter
    param.target = int_ref();
    param.ops.push_back(ir::AdjustmentOp{.kind = ir::AdjustmentOpKind::IntWiden});
    ir::AdjustmentNode result;
    result.source = int_ref(bounded); // source return <: target return
    result.target = int_ref();
    result.ops.push_back(ir::AdjustmentOp{.kind = ir::AdjustmentOpKind::IntWiden});
    plan.nodes.push_back(std::move(root));
    plan.nodes.push_back(std::move(param));
    plan.nodes.push_back(std::move(result));
    let->adjustment = std::move(plan);

    const auto lowered = ir::core::lower_ahfl_to_core(fixture->ir);
    for (const auto &diagnostic : lowered.diagnostics) {
        INFO(diagnostic.code << ": " << diagnostic.message);
    }
    REQUIRE(lowered.ok());
    REQUIRE(lowered.program.flows.size() == 1);
    const auto &flow = lowered.program.flows[0];
    bool found_fn = false;
    for (const auto &node : flow.storage.coercion_plans) {
        if (node.ops.size() == 2 && node.ops[0].kind == ir::core::CoreCoercionOpKind::FnParam &&
            node.ops[1].kind == ir::core::CoreCoercionOpKind::FnReturn) {
            REQUIRE(node.ops[0].child.value < flow.storage.coercion_plans.size());
            REQUIRE(node.ops[1].child.value < flow.storage.coercion_plans.size());
            const auto &param_child = flow.storage.coercion_plans[node.ops[0].child.value];
            const auto &return_child = flow.storage.coercion_plans[node.ops[1].child.value];
            CHECK(param_child.ops[0].kind == ir::core::CoreCoercionOpKind::IntWiden);
            CHECK(return_child.ops[0].kind == ir::core::CoreCoercionOpKind::IntWiden);
            found_fn = true;
        }
    }
    CHECK(found_fn);
    CHECK(ir::core::verify_core_program(lowered.program).ok());
}

TEST_CASE("F3 Core lowering fails closed on missing and non-materializable adjustments") {
    SUBCASE("missing non-identity plan") {
        auto fixture = lower_scalar_fixture();
        REQUIRE(fixture.has_value());
        auto *let = first_let(fixture->ir);
        REQUIRE(let != nullptr);
        let->adjustment.reset();
        const auto lowered = ir::core::lower_ahfl_to_core(fixture->ir);
        CHECK_FALSE(lowered.ok());
        CHECK(has_core_lower_code(lowered, ir::core::diag::kMissingAdjustment));
    }

    SUBCASE("ToAny is rejected before Any value-type materialization") {
        auto fixture = lower_scalar_fixture();
        REQUIRE(fixture.has_value());
        auto *let = first_let(fixture->ir);
        REQUIRE(let != nullptr);
        REQUIRE(let->adjustment.has_value());
        let->type_ref.kind = ir::TypeRefKind::Any;
        let->adjustment->target.kind = ir::TypeRefKind::Any;
        auto &root = let->adjustment->nodes[let->adjustment->root];
        root.target.kind = ir::TypeRefKind::Any;
        root.ops[0].kind = ir::AdjustmentOpKind::ToAny;

        const auto lowered = ir::core::lower_ahfl_to_core(fixture->ir);
        CHECK_FALSE(lowered.ok());
        CHECK(has_core_lower_code(lowered, ir::core::diag::kInvalidCoercion));
        CHECK_FALSE(has_core_lower_code(lowered, ir::core::diag::kUnresolvedType));
    }

    SUBCASE("FromNever is rejected before Never operand materialization") {
        auto fixture = lower_scalar_fixture();
        REQUIRE(fixture.has_value());
        auto *let = first_let(fixture->ir);
        REQUIRE(let != nullptr);
        REQUIRE(let->adjustment.has_value());
        let->initializer->resolved_type.kind = ir::TypeRefKind::Never;
        let->adjustment->source.kind = ir::TypeRefKind::Never;
        auto &root = let->adjustment->nodes[let->adjustment->root];
        root.source.kind = ir::TypeRefKind::Never;
        root.ops[0].kind = ir::AdjustmentOpKind::FromNever;

        const auto lowered = ir::core::lower_ahfl_to_core(fixture->ir);
        CHECK_FALSE(lowered.ok());
        CHECK(has_core_lower_code(lowered, ir::core::diag::kInvalidCoercion));
        CHECK_FALSE(has_core_lower_code(lowered, ir::core::diag::kUnresolvedType));
    }
}
