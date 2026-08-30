#include <doctest.h>

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/typed_hir_lower.hpp"
#include "ahfl/compiler/ir/verify.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

// RFC 0026 P4-A nominal-identity bridge fidelity + fail-closed battery.
//
// The bridge persists a RESOLVED nominal SymbolRef on every Struct/Enum
// ir::TypeRef (typed_hir_lower) and the BackendReady verifier
// (verify_ir_program) turns that into a gate: a Struct/Enum ref MUST carry a
// resolved Type identity whose canonical name matches, and a non-nominal ref
// MUST NOT carry any stray identity. These tests lower a REAL frontend program,
// prove it is BackendReady clean, then tamper a single nominal_ref field at a
// time and prove the verifier fails closed. (typed_hir_lower's own
// fail-closed throws for a corrupt TypedProgram are covered indirectly: a
// well-formed TypedProgram never trips them; the verifier is the observable
// backend gate.)

namespace {

using namespace ahfl;

// A tiny program with a nominal struct in agent input/context/output and a
// struct-typed local, so the lowered typed IR carries several Struct TypeRefs
// with nominal_ref populated.
constexpr std::string_view kSource = R"AHFL(
module app;

struct Request { value: Int; }
struct Context { seen: Int = 0; }
struct Response { value: Int; }

agent Worker {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for Worker {
    state Done {
        let reply = Response { value: input.value };
        return reply;
    }
}
)AHFL";

// Lower the source all the way to the typed IR Program the BackendReady
// verifier consumes. Returns nullopt if any front-end stage errors.
[[nodiscard]] std::optional<ir::Program> lower_typed() {
    const Frontend frontend;
    auto parse = frontend.parse_text("nominal_ref_bridge.ahfl", std::string(kSource));
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

// Find the first Struct/Enum TypeRef anywhere in a program's declarations and
// return a mutable pointer, so a test can tamper exactly one nominal_ref.
ir::TypeRef *first_nominal_type_ref(ir::TypeRef &type) {
    if (type.kind == ir::TypeRefKind::Struct || type.kind == ir::TypeRefKind::Enum) {
        return &type;
    }
    if (type.first) {
        if (auto *found = first_nominal_type_ref(*type.first)) {
            return found;
        }
    }
    if (type.second) {
        if (auto *found = first_nominal_type_ref(*type.second)) {
            return found;
        }
    }
    for (auto &param : type.params) {
        if (param) {
            if (auto *found = first_nominal_type_ref(*param)) {
                return found;
            }
        }
    }
    return nullptr;
}

// Walk every TypeRef a declaration exposes via its structured type slots.
// AgentDecl input/context/output are the reliable nominal carriers here.
ir::TypeRef *find_any_nominal_ref(ir::Program &program) {
    for (auto &decl : program.declarations) {
        ir::TypeRef *found = nullptr;
        std::visit(
            [&](auto &value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, ir::AgentDecl>) {
                    found = first_nominal_type_ref(value.input_type_ref);
                    if (found == nullptr) {
                        found = first_nominal_type_ref(value.output_type_ref);
                    }
                }
            },
            decl);
        if (found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("nominal_ref bridge: real frontend program is BackendReady clean") {
    auto program = lower_typed();
    REQUIRE(program.has_value());
    const auto result = ir::verify_ir_program(*program, ir::IrVerificationMode::BackendReady);
    CHECK_FALSE(result.has_errors());

    // The struct refs actually carry a resolved Type identity (not name-only).
    auto *nominal = find_any_nominal_ref(*program);
    REQUIRE(nominal != nullptr);
    CHECK(nominal->nominal_ref.kind == ir::SymbolRefKind::Type);
    CHECK(nominal->nominal_ref.canonical_name == nominal->canonical_name);
    CHECK(nominal->nominal_ref.id.has_value());
}

TEST_CASE("nominal_ref bridge: verifier fails closed on tampered nominal identity") {
    SUBCASE("dropped nominal_ref on a Struct/Enum ref") {
        auto program = lower_typed();
        REQUIRE(program.has_value());
        auto *nominal = find_any_nominal_ref(*program);
        REQUIRE(nominal != nullptr);
        nominal->nominal_ref = ir::SymbolRef{}; // Unknown/empty
        CHECK(ir::verify_ir_program(*program, ir::IrVerificationMode::BackendReady).has_errors());
    }
    SUBCASE("wrong kind (Agent) on a nominal ref") {
        auto program = lower_typed();
        REQUIRE(program.has_value());
        auto *nominal = find_any_nominal_ref(*program);
        REQUIRE(nominal != nullptr);
        nominal->nominal_ref.kind = ir::SymbolRefKind::Agent;
        CHECK(ir::verify_ir_program(*program, ir::IrVerificationMode::BackendReady).has_errors());
    }
    SUBCASE("canonical drift between type and its nominal identity") {
        auto program = lower_typed();
        REQUIRE(program.has_value());
        auto *nominal = find_any_nominal_ref(*program);
        REQUIRE(nominal != nullptr);
        nominal->nominal_ref.canonical_name += "::Drifted";
        CHECK(ir::verify_ir_program(*program, ir::IrVerificationMode::BackendReady).has_errors());
    }
    SUBCASE("stray nominal identity on a non-nominal ref") {
        auto program = lower_typed();
        REQUIRE(program.has_value());
        // Find an agent and attach a stray identity to a non-nominal slot by
        // fabricating a Bool ref carrying a nominal_ref. We reuse the nominal
        // finder to locate a struct ref, then flip its kind to a primitive so
        // the (now stray) nominal_ref must be rejected.
        auto *nominal = find_any_nominal_ref(*program);
        REQUIRE(nominal != nullptr);
        nominal->kind = ir::TypeRefKind::Bool;
        nominal->canonical_name.clear();
        // nominal_ref still populated -> stray identity on a non-nominal ref.
        CHECK(ir::verify_ir_program(*program, ir::IrVerificationMode::BackendReady).has_errors());
    }
}
