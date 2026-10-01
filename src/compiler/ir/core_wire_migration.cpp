#include "ahfl/compiler/ir/core_wire_migration.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace ahfl::ir::core {

// Factory access to the private VerifiedWireSchemaTable + VerifiedWireSchemaBinding
// constructors. Bindings are constructor-guarded so only a verifying path can mint
// one, and the gates here enforce the trust invariants the codec relies on: the
// table passes the public local verifier, the selector's capability id +
// source_symbol match a real capability entry, and the root is DERIVED from the
// selector's kind/param_index (never a caller-supplied raw NodeId, and never
// another capability's or a descendant node).
struct WireSchemaBindingFactory {
    // Admit a table into an opaque verified authority. Local verification runs
    // EXACTLY ONCE here, per new authority. A handle is minted iff the local
    // diagnostic bag is EMPTY (reject-on-any-diagnostic, matching the pre-B2-A-pre
    // `make` gate below), so a returned authority is unforgeably verified.
    //
    // Takes the table by rvalue reference so verification BORROWS the owned object;
    // the SOLE material ownership move is into the `shared_ptr` backing (all callers
    // already pass `std::move`). On rejection the table is left untouched.
    [[nodiscard]] static VerifiedWireSchemaTableResult make_verified(CoreWireSchemaTable &&table) {
        VerifiedWireSchemaTableResult result;
        auto local = verify_core_wire_schema_table_local(table);
        if (!local.empty()) {
            result.diagnostics = std::move(local);
            return result;
        }
        auto shared = std::make_shared<const CoreWireSchemaTable>(std::move(table));
        result.table = VerifiedWireSchemaTable(std::move(shared));
        return result;
    }

    // Mint a binding that SHARES an already-verified authority's one immutable table
    // backing. NO local re-verification (the authority is verified-once/immutable);
    // only the root-derivation SSOT runs. `diagnostics` is cleared on entry, so a
    // nullopt carries exactly this call's derivation failure and never an empty bag.
    [[nodiscard]] static std::optional<VerifiedWireSchemaBinding>
    mint_from_verified(const VerifiedWireSchemaTable &verified,
                       const CoreWireRootSelector &selector,
                       std::vector<CoreLowerDiagnostic> &diagnostics) {
        diagnostics.clear();
        const auto root = derive_root(*verified.table_, selector, diagnostics);
        if (!root.has_value()) {
            return std::nullopt;
        }
        auto payload = std::make_shared<const VerifiedWireSchemaBinding::Payload>(
            VerifiedWireSchemaBinding::Payload{verified.table_, selector, std::nullopt,
                                               std::nullopt, *root});
        return VerifiedWireSchemaBinding(std::move(payload));
    }

    // Legacy verify-then-mint path (TypeRef migration in C2, transported table in
    // E4-B1): admit the table into an authority, then mint from it. `diagnostics`
    // is CLEARED on entry so the result is unambiguous; on admission failure the
    // exact local verifier bag is forwarded verbatim (same behavior/priority as
    // before B2-A-pre); the derivation SSOT is unchanged.
    [[nodiscard]] static std::optional<VerifiedWireSchemaBinding>
    make(CoreWireSchemaTable table, const CoreWireRootSelector &selector,
         std::vector<CoreLowerDiagnostic> &diagnostics) {
        diagnostics.clear();
        auto admitted = make_verified(std::move(table));
        if (!admitted.table.has_value()) {
            diagnostics = std::move(admitted.diagnostics);
            return std::nullopt;
        }
        return mint_from_verified(*admitted.table, selector, diagnostics);
    }

    [[nodiscard]] static std::optional<VerifiedWireSchemaBinding>
    mint_frame_from_verified(const VerifiedWireSchemaTable &verified,
                             const CoreWireFrameRootSelector &selector,
                             std::vector<CoreLowerDiagnostic> &diagnostics) {
        diagnostics.clear();
        const auto &table = *verified.table_;
        if (!table.frame_roots.has_value()) {
            fail(diagnostics, "wire-schema table carries no agent frame-root block");
            return std::nullopt;
        }
        const CoreWireSchemaNodeId root = selector.kind == CoreWireFrameRootKind::Input
                                              ? table.frame_roots->input
                                              : table.frame_roots->output;
        // A frame binding records its true boundary provenance and NO
        // capability selector: it must never be indistinguishable from a
        // capability Result root. The codec consumes only table()/root();
        // any authorization logic branches on frame_kind() before selector().
        auto payload = std::make_shared<VerifiedWireSchemaBinding::Payload>();
        payload->table = verified.table_;
        payload->selector = CoreWireRootSelector{}; // not a capability slot
        payload->frame_kind = selector.kind;
        payload->root = root;
        return VerifiedWireSchemaBinding(std::move(payload));
    }

    // WH-5b.3: the node-boundary sibling mint. Derives the root from the typed
    // node selector against the admitted frame-root block's per-node
    // `node_inputs`/`node_outputs` table (parallel to the packaged-instance
    // table). A table without node roots, or an out-of-range ordinal, fails
    // closed. The minted binding records its node provenance and is never
    // mistaken for an agent-boundary frame binding or a capability binding.
    [[nodiscard]] static std::optional<VerifiedWireSchemaBinding>
    mint_node_from_verified(const VerifiedWireSchemaTable &verified,
                            const CoreWireNodeRootSelector &selector,
                            std::vector<CoreLowerDiagnostic> &diagnostics) {
        diagnostics.clear();
        const auto &table = *verified.table_;
        if (!table.frame_roots.has_value()) {
            fail(diagnostics, "wire-schema table carries no agent frame-root block");
            return std::nullopt;
        }
        const auto &roots = *table.frame_roots;
        const bool is_input = selector.kind == CoreWireNodeRootKind::Input;
        const auto &nodes = is_input ? roots.node_inputs : roots.node_outputs;
        if (selector.node_ordinal >= nodes.size()) {
            fail(diagnostics,
                 "wire-schema node-binding selector ordinal is out of range");
            return std::nullopt;
        }
        const CoreWireSchemaNodeId root = nodes[selector.node_ordinal];
        auto payload = std::make_shared<VerifiedWireSchemaBinding::Payload>();
        payload->table = verified.table_;
        payload->selector = CoreWireRootSelector{}; // not a capability slot
        payload->frame_kind = std::nullopt;         // not an agent boundary root
        payload->node_selector = selector;
        payload->root = root;
        return VerifiedWireSchemaBinding(std::move(payload));
    }

  private:
    static void fail(std::vector<CoreLowerDiagnostic> &diagnostics, std::string message) {
        diagnostics.push_back(CoreLowerDiagnostic{CoreDiagnosticSeverity::Error,
                                                  std::string(wire_schema::kInvalid),
                                                  std::move(message), std::nullopt});
    }

    // Derive the binding root from a typed selector, enforcing every trust
    // invariant: the capability id names a REAL entry in this table; its
    // source_symbol matches the caller's expectation (no cross-capability
    // confusion); a Result selector carries param_index 0; a Param selector's
    // index is strictly in bounds.
    //
    // The capability id is a program-global `CoreCapabilityId`, NOT a positional
    // index into `table.capabilities`: a wire table is the selected-capability
    // subset, so it may hold sparse, non-zero-based ids (e.g. {3, 7}). Using the
    // id as a vector subscript would (a) wrongly reject a legal in-table id that
    // exceeds the subset size and (b) — far worse — silently bind selector id=1
    // to whatever entry happens to sit at index 1, breaking the "selector + root
    // pinned together" invariant. The local verifier guarantees the entries are
    // strictly increasing and unique by `capability.value`, so we lower_bound to
    // the exact entry and reject when it is absent.
    [[nodiscard]] static std::optional<CoreWireSchemaNodeId>
    derive_root(const CoreWireSchemaTable &table, const CoreWireRootSelector &selector,
                std::vector<CoreLowerDiagnostic> &diagnostics) {
        const auto it = std::lower_bound(
            table.capabilities.begin(), table.capabilities.end(), selector.capability,
            [](const CoreWireCapabilitySchema &entry, CoreCapabilityId target) noexcept {
                return entry.capability.value < target.value;
            });
        if (it == table.capabilities.end() || it->capability != selector.capability) {
            fail(diagnostics,
                 "wire-schema binding selector capability id is not present in the table");
            return std::nullopt;
        }
        const auto &capability = *it;
        if (capability.source_symbol != selector.expected_source_symbol) {
            fail(diagnostics,
                 "wire-schema binding selector source_symbol does not match the capability entry");
            return std::nullopt;
        }
        switch (selector.kind) {
        case CoreWireRootKind::Result:
            if (selector.param_index != 0) {
                fail(diagnostics, "wire-schema binding Result selector must carry param_index 0");
                return std::nullopt;
            }
            return capability.result;
        case CoreWireRootKind::Param:
            if (selector.param_index >= capability.params.size()) {
                fail(diagnostics, "wire-schema binding Param selector index is out of range");
                return std::nullopt;
            }
            return capability.params[selector.param_index];
        }
        fail(diagnostics, "wire-schema binding selector kind is invalid");
        return std::nullopt;
    }
};

namespace {

[[nodiscard]] CoreLowerDiagnostic migration_error(std::string message) {
    return CoreLowerDiagnostic{CoreDiagnosticSeverity::Error,
                               std::string(wire_schema::kInvalidCore), std::move(message),
                               std::nullopt};
}

} // namespace

WireSchemaMigrationResult
migrate_type_ref_to_wire_binding(const ir::TypeRef &type,
                                 const VerifiedCoreTypeEnvironment &environment) {
    WireSchemaMigrationResult result;

    // Scratch CoreProgram seeded ONLY with the verified type table + value-type
    // arena. No agent/flow/workflow/body is present, so an unrelated (not-yet-
    // lowered) handler cannot affect this projection. The environment is
    // immutable + verified, so this copy is a trustworthy starting point.
    CoreProgram scratch;
    scratch.types = environment.types();
    scratch.value_types = environment.value_types();

    // Lower the AHFL TypeRef through the single Core value-type SSOT. Fails closed
    // (no binding) on Any / Unresolved / erased TypeVar / an unresolved user
    // nominal — exactly the erasure cases we must never paper over.
    std::string reason;
    const auto root_value_type = lower_value_type_into(scratch, type, &reason);
    if (!root_value_type.has_value()) {
        result.diagnostics.push_back(migration_error(
            "wire-schema migration cannot project this type: " +
            (reason.empty() ? std::string("unprojectable type") : reason)));
        return result;
    }

    // A synthetic capability whose RESULT is the migrated root. The projector
    // walks capability param/result roots; a zero-param synthetic capability lets
    // us reuse the C1 projector + verifier verbatim to reach the root's closure.
    // The SymbolId is a fixed synthetic value the binding selector cross-checks.
    constexpr std::uint64_t kSyntheticSymbol = 0;
    CoreCapabilityDecl synthetic;
    synthetic.name = "__ahfl_wire_migration_root";
    synthetic.symbol_ref.kind = ir::SymbolRefKind::Capability;
    synthetic.symbol_ref.canonical_name = "__ahfl::wire::migration_root";
    synthetic.symbol_ref.id = static_cast<std::size_t>(kSyntheticSymbol);
    synthetic.return_type = *root_value_type;
    const auto capability_id =
        CoreCapabilityId{static_cast<std::uint32_t>(scratch.capabilities.size())};
    scratch.capabilities.push_back(std::move(synthetic));

    auto projected = project_core_wire_schema(scratch, {capability_id});
    if (!projected.ok()) {
        // Never/Fn/Closure/non-String-Map and any other C1 fail-closed reason
        // surface here as the projector's own diagnostics.
        result.diagnostics = std::move(projected.diagnostics);
        if (result.diagnostics.empty()) {
            result.diagnostics.push_back(
                migration_error("wire-schema migration projection failed with no diagnostic"));
        }
        return result;
    }

    CoreWireSchemaTable table = std::move(*projected.table);
    // Mint the binding through the SAME typed-selector factory a transported table
    // uses: {synthetic capability, Result}. The factory re-runs the public local
    // verifier, cross-checks the source_symbol, and DERIVES the root from the
    // selector — so even our own projection cannot smuggle a descendant or
    // cross-capability root.
    const CoreWireRootSelector selector{capability_id, kSyntheticSymbol,
                                        CoreWireRootKind::Result, 0};
    std::vector<CoreLowerDiagnostic> binding_diagnostics;
    auto binding = WireSchemaBindingFactory::make(std::move(table), selector, binding_diagnostics);
    if (!binding.has_value()) {
        result.diagnostics = std::move(binding_diagnostics);
        if (result.diagnostics.empty()) {
            result.diagnostics.push_back(
                migration_error("wire-schema migration binding failed with no diagnostic"));
        }
        return result;
    }
    result.binding = std::move(binding);
    return result;
}

WireSchemaMigrationResult
migrate_type_ref_to_wire_binding(const ir::TypeRef &type, const AhflIr &program) {
    auto environment = build_core_type_environment(program);
    if (!environment.ok()) {
        WireSchemaMigrationResult result;
        result.diagnostics = std::move(environment.diagnostics);
        if (result.diagnostics.empty()) {
            result.diagnostics.push_back(
                migration_error("wire-schema migration type environment failed to verify"));
        }
        return result;
    }
    return migrate_type_ref_to_wire_binding(type, *environment.environment);
}

std::optional<VerifiedWireSchemaBinding>
make_wire_binding_from_transported_table(CoreWireSchemaTable table,
                                         const CoreWireRootSelector &selector,
                                         std::vector<CoreLowerDiagnostic> &diagnostics) {
    // E4-B1 transport gate: a table that did not originate from our own projector
    // must pass the public local verifier AND resolve its root through the typed
    // selector (capability id + source_symbol cross-check + kind/param_index),
    // never a raw NodeId — so a descendant node or another capability's root is
    // rejected. The factory enforces all of this.
    return WireSchemaBindingFactory::make(std::move(table), selector, diagnostics);
}

VerifiedWireSchemaTableResult make_verified_wire_schema_table(CoreWireSchemaTable table) {
    // B2-A-pre admission: local-verify once, mint an opaque authority iff the bag
    // is empty. Many bindings can then share this one verified backing.
    return WireSchemaBindingFactory::make_verified(std::move(table));
}

std::optional<VerifiedWireSchemaBinding>
make_wire_binding_from_verified_table(const VerifiedWireSchemaTable &verified,
                                      const CoreWireRootSelector &selector,
                                      std::vector<CoreLowerDiagnostic> &diagnostics) {
    // B2-A-pre shared-authority mint: derive the root through the sole SSOT and mint
    // a binding sharing `verified`'s immutable backing. No table re-verification.
    return WireSchemaBindingFactory::mint_from_verified(verified, selector, diagnostics);
}

std::optional<VerifiedWireSchemaBinding>
make_frame_binding_from_verified_table(const VerifiedWireSchemaTable &verified,
                                       const CoreWireFrameRootSelector &selector,
                                       std::vector<CoreLowerDiagnostic> &diagnostics) {
    // RFC 0026 P6-7: the frame-root sibling mint. No table re-verification (the
    // authority is verified-once/immutable); the root is derived from the typed
    // frame selector against the admitted frame-root block.
    return WireSchemaBindingFactory::mint_frame_from_verified(verified, selector, diagnostics);
}

std::optional<VerifiedWireSchemaBinding>
make_node_frame_binding_from_verified_table(const VerifiedWireSchemaTable &verified,
                                            const CoreWireNodeRootSelector &selector,
                                            std::vector<CoreLowerDiagnostic> &diagnostics) {
    // WH-5b.3: the node-boundary sibling mint. No table re-verification; the
    // root is derived from the typed node selector against the admitted
    // frame-root block's per-node table.
    return WireSchemaBindingFactory::mint_node_from_verified(verified, selector, diagnostics);
}

} // namespace ahfl::ir::core
