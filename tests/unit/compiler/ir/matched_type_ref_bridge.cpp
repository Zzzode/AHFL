#include <doctest.h>

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/expr.hpp"
#include "ahfl/compiler/ir/typed_hir_lower.hpp"
#include "ahfl/compiler/ir/verify.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <variant>

// RFC 0026 P4-B1: the MatchPattern.matched_type_ref bridge. Lowers a REAL match
// program via the typed-HIR path (BackendReady-clean, unlike the AST-lowering
// path), proves the resolved matched type is carried, then tampers it to prove
// the BackendReady consistency lock (matched_type_ref nominal <-> matched_enum)
// fails closed. The bridge is otherwise INERT in B1 (no Core consumer yet).

namespace {

using namespace ahfl;

constexpr std::string_view kSource = R"AHFL(
module m;

struct Req { id: Int; }
struct Ctx { seen: Int = 0; }
struct Resp { id: Int; }

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
        let m: Maybe = Maybe::Some(input.id);
        let r: Int = match m {
            Some(x) => x,
            None => 0,
        };
        return Resp { id: r };
    }
}
)AHFL";

[[nodiscard]] std::optional<ir::Program> lower_typed_match() {
    const Frontend frontend;
    auto parse = frontend.parse_text("matched_type_ref_bridge.ahfl", std::string(kSource));
    if (parse.has_errors() || parse.program == nullptr) {
        return std::nullopt;
    }
    const Resolver resolver;
    const auto resolve = resolver.resolve(*parse.program);
    if (resolve.has_errors()) {
        return std::nullopt;
    }
    const TypeChecker checker;
    const auto type_result = checker.check(*parse.program, resolve);
    if (type_result.has_errors()) {
        return std::nullopt;
    }
    return lower_typed_program(type_result.typed_program, *parse.program);
}

// Find the first MatchExpr's arm patterns in a program (walking flow handler
// bodies via the expression arena). Returns pointers into the program so a test
// can inspect / tamper matched_type_ref.
ir::MatchPattern *first_match_root_pattern(ir::Program &program) {
    for (ir::Expr *expr : program.expr_arena.span()) {
        if (expr == nullptr) {
            continue;
        }
        if (auto *m = std::get_if<ir::MatchExpr>(&expr->node)) {
            if (!m->arms.empty()) {
                return &m->arms.front().pattern;
            }
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("P4-B1: matched_type_ref bridge is populated and BackendReady-consistent") {
    auto program = lower_typed_match();
    REQUIRE(program.has_value());
    // The typed-HIR lowered program is BackendReady clean, INCLUDING the new
    // matched_type_ref <-> matched_enum consistency lock.
    CHECK_FALSE(
        ir::verify_ir_program(*program, ir::IrVerificationMode::BackendReady).has_errors());

    auto *root = first_match_root_pattern(*program);
    REQUIRE(root != nullptr);
    // Root arm matches against enum `Maybe`: matched_type_ref is a nominal Enum
    // whose nominal_ref agrees with matched_enum.
    CHECK(root->matched_type_ref.kind == ir::TypeRefKind::Enum);
    CHECK(root->matched_type_ref.canonical_name.find("Maybe") != std::string::npos);
    CHECK(root->matched_type_ref.nominal_ref.canonical_name == root->matched_enum.canonical_name);
}

TEST_CASE("P4-B1: BackendReady verifier fails closed on a tampered matched_type_ref") {
    SUBCASE("nominal matched type disagreeing with matched_enum") {
        auto program = lower_typed_match();
        REQUIRE(program.has_value());
        auto *root = first_match_root_pattern(*program);
        REQUIRE(root != nullptr);
        REQUIRE(root->matched_type_ref.kind == ir::TypeRefKind::Enum);
        // Drift the resolved nominal identity away from matched_enum.
        root->matched_type_ref.nominal_ref.canonical_name += "::Drifted";
        root->matched_type_ref.nominal_ref.id.reset();
        CHECK(ir::verify_ir_program(*program, ir::IrVerificationMode::BackendReady).has_errors());
    }
    SUBCASE("primitive matched type must not carry a matched_enum identity") {
        auto program = lower_typed_match();
        REQUIRE(program.has_value());
        auto *root = first_match_root_pattern(*program);
        REQUIRE(root != nullptr);
        // Flip the matched type to a primitive while keeping the (now stray)
        // matched_enum: a non-nominal matched type must have Unknown matched_enum.
        root->matched_type_ref = ir::TypeRef{};
        root->matched_type_ref.kind = ir::TypeRefKind::Int;
        CHECK(ir::verify_ir_program(*program, ir::IrVerificationMode::BackendReady).has_errors());
    }
}
