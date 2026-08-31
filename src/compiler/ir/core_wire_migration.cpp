#include "ahfl/compiler/ir/core_wire_migration.hpp"

#include <string>
#include <utility>

namespace ahfl::ir::core {

// Factory access to the private VerifiedWireSchemaBinding constructor. The
// binding is constructor-guarded so only a verifying path can mint one, and the
// gate here enforces the trust invariants the codec relies on: the table passes
// the public local verifier, the selector's capability id + source_symbol match a
// real capability entry, and the root is DERIVED from the selector's
// kind/param_index (never a caller-supplied raw NodeId, and never another
// capability's or a descendant node). `diagnostics` is CLEARED on entry so the
// result is unambiguous.
struct WireSchemaBindingFactory {
    [[nodiscard]] static std::optional<VerifiedWireSchemaBinding>
    make(CoreWireSchemaTable table, const CoreWireRootSelector &selector,
         std::vector<CoreLowerDiagnostic> &diagnostics) {
        diagnostics.clear();
        auto local = verify_core_wire_schema_table_local(table);
        if (!local.empty()) {
            diagnostics = std::move(local);
            return std::nullopt;
        }
        const auto root = derive_root(table, selector, diagnostics);
        if (!root.has_value()) {
            return std::nullopt;
        }
        auto payload = std::make_shared<const VerifiedWireSchemaBinding::Payload>(
            VerifiedWireSchemaBinding::Payload{std::move(table), selector, *root});
        return VerifiedWireSchemaBinding(std::move(payload));
    }

  private:
    static void fail(std::vector<CoreLowerDiagnostic> &diagnostics, std::string message) {
        diagnostics.push_back(CoreLowerDiagnostic{CoreDiagnosticSeverity::Error,
                                                  std::string(wire_schema::kInvalid),
                                                  std::move(message), std::nullopt});
    }

    // Derive the binding root from a typed selector, enforcing every trust
    // invariant: the capability id is in range; its source_symbol matches the
    // caller's expectation (no cross-capability confusion); a Result selector
    // carries param_index 0; a Param selector's index is strictly in bounds.
    [[nodiscard]] static std::optional<CoreWireSchemaNodeId>
    derive_root(const CoreWireSchemaTable &table, const CoreWireRootSelector &selector,
                std::vector<CoreLowerDiagnostic> &diagnostics) {
        if (selector.capability.value >= table.capabilities.size()) {
            fail(diagnostics, "wire-schema binding selector capability id is out of range");
            return std::nullopt;
        }
        const auto &capability = table.capabilities[selector.capability.value];
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

} // namespace ahfl::ir::core
