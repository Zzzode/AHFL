#include <doctest.h>

#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/tower.hpp"

#include <type_traits>
#include <variant>

// RFC 0026 slice P1 / KR6.2: runtime coverage of the IR tower's layer/path
// fork. The design invariants are also locked at compile time in
// src/compiler/ir/tower.cpp via static_assert; these cases give the tower a
// registered ctest so the skeleton is exercised (not just compiled) and so a
// later slice that re-points a layer's path is caught by a failing test as well
// as a failing build.

namespace {

using ahfl::ir::tower::Layer;
using ahfl::ir::tower::Path;
using ahfl::ir::tower::path_of;

TEST_CASE("IR tower layers lower in TypedHir -> AhflIr -> CoreIr order") {
    CHECK(static_cast<int>(Layer::TypedHir) == 0);
    CHECK(static_cast<int>(Layer::AhflIr) == 1);
    CHECK(static_cast<int>(Layer::CoreIr) == 2);
}

TEST_CASE("IR tower forks: verification consumes AhflIr, execution consumes CoreIr") {
    // Dafny-style split (RFC 0026 Design): temporal/contract stay on the
    // verification path, monomorphization/memory-layout on the execution path.
    CHECK(path_of(Layer::TypedHir) == Path::Diagnostics);
    CHECK(path_of(Layer::AhflIr) == Path::Verification);
    CHECK(path_of(Layer::CoreIr) == Path::Execution);
}

TEST_CASE("IR tower layer tags carry their path in the type") {
    CHECK(ahfl::ir::tower::AhflIrTag::path == Path::Verification);
    CHECK(ahfl::ir::tower::CoreIrTag::path == Path::Execution);
    CHECK(ahfl::ir::tower::TypedHirTag::layer == Layer::TypedHir);
}

// ============================================================================
// KR6.4 tail (erasure barrier): the Core-IR node sets and the verification-only
// member surface are pinned at COMPILE time. A reviewer adding a new Core node
// alternative or a verification-only member (quota/contract/safety/liveness/
// domain/receipt/retry/compensation) to a Core decl must consciously update
// these pins (and, for members, stop and fix the erasure invariant instead).
// Counts derive from core_ir.hpp today; keep them in lockstep with the header.
// ============================================================================

namespace {

template <class T, class = void> struct type_has_quota : std::false_type {};
template <class T>
struct type_has_quota<T, std::void_t<decltype(std::declval<const T &>().quota)>>
    : std::true_type {};

template <class T, class = void> struct type_has_contract : std::false_type {};
template <class T>
struct type_has_contract<T, std::void_t<decltype(std::declval<const T &>().contract)>>
    : std::true_type {};

template <class T, class = void> struct type_has_safety : std::false_type {};
template <class T>
struct type_has_safety<T, std::void_t<decltype(std::declval<const T &>().safety)>>
    : std::true_type {};

template <class T, class = void> struct type_has_liveness : std::false_type {};
template <class T>
struct type_has_liveness<T, std::void_t<decltype(std::declval<const T &>().liveness)>>
    : std::true_type {};

template <class T, class = void> struct type_has_domain : std::false_type {};
template <class T>
struct type_has_domain<T, std::void_t<decltype(std::declval<const T &>().domain)>>
    : std::true_type {};

template <class T, class = void> struct type_has_receipt : std::false_type {};
template <class T>
struct type_has_receipt<T, std::void_t<decltype(std::declval<const T &>().receipt)>>
    : std::true_type {};

template <class T, class = void> struct type_has_retry : std::false_type {};
template <class T>
struct type_has_retry<T, std::void_t<decltype(std::declval<const T &>().retry)>>
    : std::true_type {};

template <class T, class = void> struct type_has_compensation : std::false_type {};
template <class T>
struct type_has_compensation<T, std::void_t<decltype(std::declval<const T &>().compensation)>>
    : std::true_type {};

} // namespace

TEST_CASE("Core-IR closed node sets keep their exact erasure-barrier size") {
    using namespace ahfl::ir::core;
    static_assert(std::variant_size_v<CoreExprNode> == 11,
                  "CoreExprNode alternatives: literal/value-ref/path/qualified/unary/binary/"
                  "construct/coerce/collection/unsupported/call");
    static_assert(std::variant_size_v<CoreStmtNode> == 9,
                  "CoreStmtNode alternatives: let/capability-call/store/if/goto/return/yield/"
                  "trap/match");
    static_assert(std::variant_size_v<CorePatternNode> == 7,
                  "CorePatternNode alternatives: wildcard/literal/int-range/binding/variant/"
                  "tuple/or");
    static_assert(std::variant_size_v<CoreValueTypeNode> == 14,
                  "CoreValueTypeNode alternatives: unit/never/bool/int/float/string/decimal/"
                  "duration/timestamp/uuid/nominal/tuple/fn/closure");
    static_assert(std::variant_size_v<CoreInstancePayload> == 5,
                  "CoreInstancePayload alternatives: capability/predicate/agent/workflow/fn");
    static_assert(std::variant_size_v<CoreDecl> == 2,
                  "CoreDecl alternatives: agent/capability (flows and workflows live in "
                  "CoreProgram flat stores, never in the decl variant)");
    CHECK(std::variant_size_v<CoreExprNode> == 11);
    CHECK(std::variant_size_v<CoreStmtNode> == 9);
    CHECK(std::variant_size_v<CorePatternNode> == 7);
    CHECK(std::variant_size_v<CoreValueTypeNode> == 14);
    CHECK(std::variant_size_v<CoreInstancePayload> == 5);
    CHECK(std::variant_size_v<CoreDecl> == 2);
}

TEST_CASE("Core decls expose no verification-only members (erasure barrier)") {
    using namespace ahfl::ir::core;
    // An agent carries its state-machine skeleton + authorization facts only:
    // its quota and any contract are erased at the Core boundary.
    static_assert(!type_has_quota<CoreAgentDecl>::value,
                  "CoreAgentDecl must not carry a quota: verification-layer only");
    static_assert(!type_has_contract<CoreAgentDecl>::value,
                  "CoreAgentDecl must not carry a contract: verification-layer only");
    // A workflow carries its DAG + regions only: safety/liveness are erased.
    static_assert(!type_has_safety<CoreWorkflowDecl>::value,
                  "CoreWorkflowDecl must not carry safety properties: verification-layer only");
    static_assert(!type_has_liveness<CoreWorkflowDecl>::value,
                  "CoreWorkflowDecl must not carry liveness properties: verification-layer only");
    // A capability keeps ONLY its effect category: the rest of the source
    // CapabilityEffectSpec (domain/receipt/retry/compensation) is erased.
    static_assert(!type_has_domain<CoreCapabilityDecl>::value,
                  "CoreCapabilityDecl must not carry an effect domain: verification-layer only");
    static_assert(!type_has_receipt<CoreCapabilityDecl>::value,
                  "CoreCapabilityDecl must not carry a receipt mode: verification-layer only");
    static_assert(!type_has_retry<CoreCapabilityDecl>::value,
                  "CoreCapabilityDecl must not carry a retry mode: verification-layer only");
    static_assert(!type_has_compensation<CoreCapabilityDecl>::value,
                  "CoreCapabilityDecl must not carry compensation: verification-layer only");
    CHECK(!type_has_quota<CoreAgentDecl>::value);
    CHECK(!type_has_contract<CoreAgentDecl>::value);
    CHECK(!type_has_safety<CoreWorkflowDecl>::value);
    CHECK(!type_has_liveness<CoreWorkflowDecl>::value);
    CHECK(!type_has_domain<CoreCapabilityDecl>::value);
    CHECK(!type_has_receipt<CoreCapabilityDecl>::value);
    CHECK(!type_has_retry<CoreCapabilityDecl>::value);
    CHECK(!type_has_compensation<CoreCapabilityDecl>::value);
}

} // namespace
