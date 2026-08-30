#include <doctest.h>

#include "ahfl/compiler/frontend/frontend.hpp"
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
