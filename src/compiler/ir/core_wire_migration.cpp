#include "ahfl/compiler/ir/core_wire_migration.hpp"

#include <string>
#include <utility>

namespace ahfl::ir::core {

// Factory access to the private VerifiedWireSchemaBinding constructor. The
// binding is constructor-guarded so only a verifying path can mint one, and the
// gate here enforces the trust invariants the codec relies on: the table passes
// the public local verifier AND the root is one of the table's capability
// param/result roots (never a mere reachable descendant, which would silently
// narrow a signature to a sub-node). `diagnostics` is CLEARED on entry so the
// result is unambiguous.
struct WireSchemaBindingFactory {
    [[nodiscard]] static std::optional<VerifiedWireSchemaBinding>
    make(CoreWireSchemaTable table, CoreWireSchemaNodeId root,
         std::vector<CoreLowerDiagnostic> &diagnostics) {
        diagnostics.clear();
        auto local = verify_core_wire_schema_table_local(table);
        if (!local.empty()) {
            diagnostics = std::move(local);
            return std::nullopt;
        }
        if (!root_is_capability_root(table, root)) {
            diagnostics.push_back(CoreLowerDiagnostic{
                CoreDiagnosticSeverity::Error, std::string(wire_schema::kInvalid),
                "wire-schema binding root is not a capability param/result root", std::nullopt});
            return std::nullopt;
        }
        auto payload = std::make_shared<const VerifiedWireSchemaBinding::Payload>(
            VerifiedWireSchemaBinding::Payload{std::move(table), root});
        return VerifiedWireSchemaBinding(std::move(payload));
    }

  private:
    // A binding root MUST be one of the table's capability param/result roots.
    [[nodiscard]] static bool root_is_capability_root(const CoreWireSchemaTable &table,
                                                      CoreWireSchemaNodeId root) {
        for (const auto &capability : table.capabilities) {
            if (capability.result == root) {
                return true;
            }
            for (const auto param : capability.params) {
                if (param == root) {
                    return true;
                }
            }
        }
        return false;
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
migrate_type_ref_to_wire_binding(const ir::TypeRef &type, const CoreTypeEnvironmentSeed &seed) {
    WireSchemaMigrationResult result;

    // A non-projectable type table seed can never yield a faithful binding.
    if (!seed.ok()) {
        result.diagnostics.push_back(
            migration_error("wire-schema migration requires a well-formed type environment"));
        return result;
    }

    // Scratch CoreProgram seeded ONLY with the shared type table + value-type
    // arena. No agent/flow/workflow/body is present, so an unrelated (not-yet-
    // lowered) handler cannot affect this projection.
    CoreProgram scratch;
    scratch.types = seed.types;
    scratch.value_types = seed.value_types;

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

    // A synthetic capability whose RETURN is the migrated root. The projector
    // walks capability param/result roots; a zero-param synthetic capability lets
    // us reuse the C1 projector + verifier verbatim to reach the root's closure.
    // The SymbolId is a fixed synthetic value: this scratch program has no other
    // capability, so uniqueness is trivial and the id never escapes the binding.
    CoreCapabilityDecl synthetic;
    synthetic.name = "__ahfl_wire_migration_root";
    synthetic.symbol_ref.kind = ir::SymbolRefKind::Capability;
    synthetic.symbol_ref.canonical_name = "__ahfl::wire::migration_root";
    synthetic.symbol_ref.id = 0;
    synthetic.return_type = *root_value_type;
    const auto capability_id = CoreCapabilityId{static_cast<std::uint32_t>(scratch.capabilities.size())};
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
    if (table.capabilities.size() != 1) {
        result.diagnostics.push_back(
            migration_error("wire-schema migration produced an unexpected capability count"));
        return result;
    }
    const CoreWireSchemaNodeId root = table.capabilities.front().result;
    // The gated factory re-runs the public local verifier AND enforces
    // root ∈ capability param/result before minting the binding — the same trust
    // gate a transported table gets, applied even to our own projection.
    std::vector<CoreLowerDiagnostic> binding_diagnostics;
    auto binding = WireSchemaBindingFactory::make(std::move(table), root, binding_diagnostics);
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
    return migrate_type_ref_to_wire_binding(type, build_core_type_environment(program));
}

std::optional<VerifiedWireSchemaBinding>
make_wire_binding_from_transported_table(CoreWireSchemaTable table, CoreWireSchemaNodeId root,
                                         std::vector<CoreLowerDiagnostic> &diagnostics) {
    // E4-B1 transport gate: a table that did not originate from our own projector
    // must pass the public local verifier AND have a root that is one of the
    // table's capability param/result roots (the factory enforces both; a mere
    // reachable descendant is rejected).
    return WireSchemaBindingFactory::make(std::move(table), root, diagnostics);
}

} // namespace ahfl::ir::core
