// RFC 0026 KR6.5 E4-B2-D2b-2 tests for the typed DurableEffectIntent record,
// IntentCoordinate, and the frozen-authority checkpoint-namespace builder.
// Hand-rolled check()/main(), mirroring the slice-1 idempotency token tests.
//
// Coverage (per slice spec):
//  (a) minting requires a bound IdempotencyAuthorityId + checkpoint namespace
//      + full call coordinate, and the record carries the slice-1 token;
//  (b) the record is immutable once built (const accessors only);
//  (c) a simulated retry that changes only an attempt number re-mints the same
//      token and an equal intent;
//  (d) a checkpoint-namespace swap with identical coordinates yields a
//      different token, and rebinding authority within one builder lifetime is
//      a typed rejection;
//  (e) lifecycle state is a std::variant visited exhaustively with Overloaded;
//  (f) the optional display name (the only string field) participates in
//      neither the token nor equality.

#include "runtime/engine/durable_effect_intent.hpp"

#include "ahfl/base/support/overloaded.hpp"
#include "base/support/sha256.hpp"
#include "runtime/engine/core_wasm_idempotency_token.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

namespace {

using ahfl::Overloaded;
using ahfl::ir::core::CoreCapabilityId;
using ahfl::ir::core::CoreWorkflowId;
using ahfl::ir::core::CoreWorkflowNodeId;
using ahfl::runtime::core_wasm_idempotency_token::compute_idempotency_token;
using ahfl::runtime::core_wasm_idempotency_token::IdempotencyAuthorityId;
using ahfl::runtime::core_wasm_idempotency_token::IdempotencyCoordinate;
using ahfl::runtime::core_wasm_idempotency_token::IdempotencyToken;
using ahfl::runtime::core_wasm_resume::InvocationOrdinal;
using ahfl::runtime::durable_effect_intent::CheckpointNamespace;
using ahfl::runtime::durable_effect_intent::DurableEffectIntent;
using ahfl::runtime::durable_effect_intent::FrozenAuthorityNamespaceBuilder;
using ahfl::runtime::durable_effect_intent::IntentBindError;
using ahfl::runtime::durable_effect_intent::IntentCoordinate;
using ahfl::runtime::durable_effect_intent::IntentFailed;
using ahfl::runtime::durable_effect_intent::IntentLifecycle;
using ahfl::runtime::durable_effect_intent::IntentMintError;
using ahfl::runtime::durable_effect_intent::IntentPending;
using ahfl::runtime::durable_effect_intent::IntentSucceeded;
using ahfl::runtime::payload_store::ResumeCheckpointId;
using ahfl::support::Sha256Digest;

int g_failures = 0;

void check(bool ok, std::string_view name) {
    if (!ok) {
        ++g_failures;
        std::cerr << "FAIL: " << name << "\n";
    }
}

IdempotencyAuthorityId make_authority(std::uint8_t seed) {
    std::array<std::uint8_t, 16> raw{};
    for (std::size_t i = 0; i < raw.size(); ++i) {
        raw[i] = static_cast<std::uint8_t>(seed + static_cast<int>(i) * 7);
    }
    return IdempotencyAuthorityId{raw};
}

Sha256Digest make_digest(std::uint8_t seed) {
    Sha256Digest d{};
    for (std::size_t i = 0; i < d.size(); ++i) {
        d[i] = static_cast<std::uint8_t>(seed + static_cast<int>(i) * 13);
    }
    return d;
}

CheckpointNamespace make_namespace(std::uint32_t workflow, std::uint64_t checkpoint) {
    return CheckpointNamespace{
        .workflow = CoreWorkflowId{workflow},
        .checkpoint = ResumeCheckpointId{checkpoint},
    };
}

IntentCoordinate make_coordinate(CheckpointNamespace ns,
                                 std::uint32_t node = 0x21222324u,
                                 std::uint64_t ordinal = 0x3132333435363738ULL,
                                 std::uint32_t capability = 0x41424344u,
                                 std::uint64_t source_symbol = 0x5152535455565758ULL,
                                 std::uint8_t digest_seed = 0x61) {
    return IntentCoordinate{
        .checkpoint_namespace = ns,
        .node = CoreWorkflowNodeId{node},
        .ordinal = InvocationOrdinal{ordinal},
        .capability = CoreCapabilityId{capability},
        .source_symbol = source_symbol,
        .param_digest = make_digest(digest_seed),
    };
}

// SFINAE probes proving the record exposes no assignable (mutable) view of the
// identity fields returned by reference: each const-ref accessor rejects
// assignment in an immediate context, so a built intent cannot be edited in
// place.
template <typename T, typename = void> struct coordinate_accessor_assignable : std::false_type {};
template <typename T>
struct coordinate_accessor_assignable<
    T,
    std::void_t<decltype(std::declval<T &>().coordinate() = std::declval<IntentCoordinate>())>>
    : std::true_type {};

template <typename T, typename = void> struct lifecycle_accessor_assignable : std::false_type {};
template <typename T>
struct lifecycle_accessor_assignable<
    T,
    std::void_t<decltype(std::declval<T &>().lifecycle() = std::declval<IntentLifecycle>())>>
    : std::true_type {};

// Every accessor on the record is a const noexcept member function: there is
// no non-const overload a mutable intent could use to mutate a field.
template <typename Fn> struct is_const_noexcept_member_fn : std::false_type {};
template <typename R, typename C, typename... Args>
struct is_const_noexcept_member_fn<R (C::*)(Args...) const noexcept> : std::true_type {};

int lifecycle_arm(const IntentLifecycle &lifecycle) {
    // Exhaustive visitor: a fourth lifecycle arm makes this fail to compile
    // until handled (Principle 4).
    return std::visit(Overloaded{
                          [](IntentPending) -> int { return 0; },
                          [](IntentSucceeded) -> int { return 1; },
                          [](IntentFailed) -> int { return 2; },
                      },
                      lifecycle);
}

} // namespace

int main() {
    static_assert(std::variant_size_v<IntentLifecycle> == 3,
                  "IntentLifecycle must be exactly the three closed arms");
    static_assert(std::is_same_v<std::variant_alternative_t<0, IntentLifecycle>, IntentPending>);
    static_assert(std::is_same_v<std::variant_alternative_t<1, IntentLifecycle>, IntentSucceeded>);
    static_assert(std::is_same_v<std::variant_alternative_t<2, IntentLifecycle>, IntentFailed>);

    static_assert(!std::is_aggregate_v<DurableEffectIntent>,
                  "intent must not be an aggregate (fields are private)");
    static_assert(!std::is_constructible_v<DurableEffectIntent,
                                           IdempotencyAuthorityId,
                                           IntentCoordinate,
                                           IdempotencyToken,
                                           IntentLifecycle,
                                           std::nullopt_t>,
                  "intent must be minted only through the bound builder");
    static_assert(!coordinate_accessor_assignable<DurableEffectIntent>::value,
                  "coordinate() must not expose a mutable view");
    static_assert(!lifecycle_accessor_assignable<DurableEffectIntent>::value,
                  "lifecycle() must not expose a mutable view");

    // Every accessor is a const noexcept member: no non-const overload exists
    // through which a mutable intent could edit a field in place.
    static_assert(is_const_noexcept_member_fn<decltype(&DurableEffectIntent::authority)>::value,
                  "authority() must be const noexcept");
    static_assert(
        is_const_noexcept_member_fn<decltype(&DurableEffectIntent::checkpoint_namespace)>::value,
        "checkpoint_namespace() must be const noexcept");
    static_assert(is_const_noexcept_member_fn<decltype(&DurableEffectIntent::coordinate)>::value,
                  "coordinate() must be const noexcept");
    static_assert(is_const_noexcept_member_fn<decltype(&DurableEffectIntent::token)>::value,
                  "token() must be const noexcept");
    static_assert(is_const_noexcept_member_fn<decltype(&DurableEffectIntent::lifecycle)>::value,
                  "lifecycle() must be const noexcept");
    static_assert(is_const_noexcept_member_fn<decltype(&DurableEffectIntent::display_name)>::value,
                  "display_name() must be const noexcept");

    static_assert(std::is_same_v<decltype(std::declval<const DurableEffectIntent &>().coordinate()),
                                 const IntentCoordinate &>,
                  "coordinate() on a const intent is a const reference");
    static_assert(std::is_same_v<decltype(std::declval<const DurableEffectIntent &>().token()),
                                 IdempotencyToken>,
                  "token() returns by value");

    const IdempotencyAuthorityId authority_a = make_authority(0x11);
    const IdempotencyAuthorityId authority_b = make_authority(0x22);
    const CheckpointNamespace ns1 = make_namespace(7, 42);
    const CheckpointNamespace ns2 = make_namespace(7, 43);
    const IntentCoordinate coord1 = make_coordinate(ns1);

    // ---- (a) mint requires bound authority + namespace + full coordinate ---
    {
        FrozenAuthorityNamespaceBuilder unbound;
        check(!unbound.bound(), "fresh builder is unbound");
        const auto rejected = unbound.mint(coord1);
        check(!rejected.has_value() && rejected.error() == IntentMintError::NotBound,
              "mint_before_bind_is_typed_not_bound");

        auto builder = FrozenAuthorityNamespaceBuilder{};
        const auto bind_result = builder.bind(authority_a, ns1);
        check(bind_result.has_value(), "bind_succeeds_once");
        check(builder.bound(), "builder_reports_bound");

        auto intent = builder.mint(coord1);
        check(intent.has_value(), "mint_succeeds_when_bound");
        check(intent->authority() == authority_a, "intent_round_trips_authority");
        check(intent->checkpoint_namespace() == ns1, "intent_round_trips_namespace");
        check(intent->coordinate() == coord1, "intent_round_trips_full_coordinate");

        // The record carries the exact slice-1 token, independently projected
        // from the frozen binding + call coordinate.
        IdempotencyCoordinate expected_preimage{};
        expected_preimage.authority = authority_a;
        expected_preimage.workflow = ns1.workflow;
        expected_preimage.checkpoint = ns1.checkpoint;
        expected_preimage.node = coord1.node;
        expected_preimage.ordinal = coord1.ordinal;
        expected_preimage.capability = coord1.capability;
        expected_preimage.source_symbol = coord1.source_symbol;
        expected_preimage.param_digest = coord1.param_digest;
        check(intent->token() == compute_idempotency_token(expected_preimage),
              "intent_token_is_slice1_token_of_binding_plus_coordinate");
    }

    // ---- (b) immutable once built ------------------------------------------
    {
        auto builder = FrozenAuthorityNamespaceBuilder{};
        static_cast<void>(builder.bind(authority_a, ns1));
        const auto intent = builder.mint(coord1).value();
        // Every accessor is const: the loop below compiles against a const
        // intent and the static_asserts above prove no mutable view exists.
        check(intent.authority() == authority_a, "const_intent_authority_accessor");
        check(intent.coordinate() == coord1, "const_intent_coordinate_accessor");
        check(intent.lifecycle().index() == 0, "fresh_intent_is_pending");
        check(lifecycle_arm(intent.lifecycle()) == 0, "pending_visits_intent_pending");
        check(!intent.display_name().has_value(), "fresh_intent_has_no_display_name");

        // Copy construction preserves identity.
        const DurableEffectIntent copy = intent;
        check(copy == intent, "copied_intent_preserves_identity");
    }

    // ---- (c) retry changing only the attempt number is the same intent -----
    {
        auto builder = FrozenAuthorityNamespaceBuilder{};
        static_cast<void>(builder.bind(authority_a, ns1));

        const auto attempt_one = builder.mint(coord1, std::string{"first"});
        // The simulated retry loop carries an attempt counter that, by
        // construction, never reaches the coordinate (there is no such field).
        for (unsigned attempt : {2u, 3u, 42u}) {
            static_cast<void>(attempt);
            const auto retried = builder.mint(coord1, std::string{"first"});
            check(retried.has_value(), "retry_mint_succeeds");
            check(retried->token() == attempt_one->token(), "retry_changing_attempt_keeps_token");
            check(retried.value() == attempt_one.value(), "retry_changing_attempt_keeps_identity");
        }
    }

    // ---- (d) namespace swap + authority rebind rejection -------------------
    {
        auto builder1 = FrozenAuthorityNamespaceBuilder{};
        static_cast<void>(builder1.bind(authority_a, ns1));
        auto builder2 = FrozenAuthorityNamespaceBuilder{};
        static_cast<void>(builder2.bind(authority_a, ns2));
        auto builder3 = FrozenAuthorityNamespaceBuilder{};
        static_cast<void>(builder3.bind(authority_b, ns1));

        // Identical call coordinate body, swapped checkpoint namespace.
        const IntentCoordinate in_ns2 = make_coordinate(ns2);
        const auto i1 = builder1.mint(coord1).value();
        const auto i2 = builder2.mint(in_ns2).value();
        check(i1.token() != i2.token(), "namespace_swap_changes_token");
        check(!(i1 == i2), "namespace_swap_changes_identity");

        // A coordinate that names another namespace is rejected by the bound
        // builder rather than minted cross-namespace.
        const auto cross = builder1.mint(in_ns2);
        check(!cross.has_value() && cross.error() == IntentMintError::NamespaceMismatch,
              "foreign_namespace_coordinate_is_typed_rejection");

        // Swapping only the authority (same namespace, same coordinate body)
        // isolates the token and identity.
        const auto i3 = builder3.mint(coord1).value();
        check(i3.token() != i1.token(), "authority_swap_changes_token");
        check(!(i3 == i1), "authority_swap_changes_identity");

        // Rebinding within one lifetime is a typed rejection and leaves the
        // original binding intact (never a silent authority switch).
        const auto rebind = builder1.bind(authority_b, ns2);
        check(!rebind.has_value() && rebind.error() == IntentBindError::AlreadyBound,
              "rebind_is_typed_already_bound");
        const auto after_rebind = builder1.mint(coord1).value();
        check(after_rebind.authority() == authority_a, "rebind_keeps_original_authority");
        check(after_rebind.token() == i1.token(), "rebind_keeps_original_namespace");

        // Invalid namespace ids fail closed at bind time and leave the builder
        // unbound, so a later legal bind still works.
        FrozenAuthorityNamespaceBuilder picky;
        const auto bad_wf = picky.bind(authority_a, make_namespace(CoreWorkflowId::kInvalid, 1));
        check(!bad_wf.has_value() && bad_wf.error() == IntentBindError::InvalidWorkflow,
              "invalid_workflow_namespace_rejected");
        const auto bad_ckpt =
            picky.bind(authority_a, make_namespace(1, ResumeCheckpointId::kInvalid));
        check(!bad_ckpt.has_value() && bad_ckpt.error() == IntentBindError::InvalidCheckpoint,
              "invalid_checkpoint_namespace_rejected");
        check(!picky.bound(), "failed_bind_leaves_builder_unbound");
        check(picky.bind(authority_a, ns1).has_value(), "legal_bind_after_rejection");

        // Invalid call-coordinate sentinels fail closed at mint time; capability
        // id 0 (not the UINT32_MAX sentinel) stays legal like slice 1.
        const auto bad_node = builder1.mint(make_coordinate(ns1, CoreWorkflowNodeId::kInvalid));
        check(!bad_node.has_value() && bad_node.error() == IntentMintError::InvalidNode,
              "invalid_node_coordinate_rejected");
        const auto bad_cap =
            builder1.mint(make_coordinate(ns1, 0x21222324u, 1, CoreCapabilityId::kInvalid));
        check(!bad_cap.has_value() && bad_cap.error() == IntentMintError::InvalidCapability,
              "invalid_capability_coordinate_rejected");
        const auto zero_cap = builder1.mint(make_coordinate(ns1, 0x21222324u, 0, 0, 0));
        check(zero_cap.has_value(), "zero_ordinal_cap_symbol_coordinate_is_legal");
    }

    // ---- (e) lifecycle variant visited with Overloaded ---------------------
    {
        auto builder = FrozenAuthorityNamespaceBuilder{};
        static_cast<void>(builder.bind(authority_a, ns1));
        const auto intent = builder.mint(coord1).value();

        check(std::holds_alternative<IntentPending>(intent.lifecycle()),
              "minted_intent_holds_IntentPending");

        const IntentLifecycle succeeded{IntentSucceeded{}};
        const IntentLifecycle failed{IntentFailed{}};
        check(lifecycle_arm(succeeded) == 1, "succeeded_visits_intent_succeeded");
        check(lifecycle_arm(failed) == 2, "failed_visits_intent_failed");

        // Holds-alternative plus a bool-returning exhaustive visitor.
        const bool is_pending = std::visit(Overloaded{
                                               [](IntentPending) { return true; },
                                               [](IntentSucceeded) { return false; },
                                               [](IntentFailed) { return false; },
                                           },
                                           intent.lifecycle());
        check(is_pending, "bool_visitor_classifies_pending");
    }

    // ---- (f) display name is excluded from token and equality --------------
    {
        auto builder = FrozenAuthorityNamespaceBuilder{};
        static_cast<void>(builder.bind(authority_a, ns1));

        const auto unnamed = builder.mint(coord1);
        const auto named_alpha = builder.mint(coord1, std::string{"alpha"});
        const auto named_beta = builder.mint(coord1, std::string{"beta"});

        check(unnamed->token() == named_alpha->token() &&
                  named_alpha->token() == named_beta->token(),
              "display_name_never_enters_token");
        check(unnamed.value() == named_alpha.value() && named_alpha.value() == named_beta.value(),
              "display_name_never_enters_equality");
        check(!unnamed->display_name().has_value(), "unnamed_intent_name_is_nullopt");
        check(named_alpha->display_name().has_value() && *named_alpha->display_name() == "alpha",
              "named_intent_round_trips_display_name");

        // A genuinely different param digest breaks identity and token even
        // when the display name matches: identity is the coordinate, not the
        // string.
        const IntentCoordinate other_params = make_coordinate(ns1, 0x21222324u, 1, 2, 3, 0x77);
        const auto same_name_other_params = builder.mint(other_params, std::string{"alpha"});
        check(same_name_other_params->token() != named_alpha->token(),
              "matching_display_name_does_not_collide_distinct_coordinate");
        check(!(same_name_other_params.value() == named_alpha.value()),
              "matching_display_name_does_not_identify_distinct_coordinate");
    }

    if (g_failures == 0) {
        std::cout << "all durable-effect intent tests passed\n";
        return 0;
    }
    std::cerr << g_failures << " durable-effect intent test(s) failed\n";
    return 1;
}
