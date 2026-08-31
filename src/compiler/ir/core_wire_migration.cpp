#include "ahfl/compiler/ir/core_wire_migration.hpp"

#include <string>
#include <utility>

namespace ahfl::ir::core {

// Factory access to the private VerifiedWireSchemaBinding constructor. The
// binding is constructor-guarded so only a verifying path can mint one.
struct WireSchemaBindingFactory {
    [[nodiscard]] static VerifiedWireSchemaBinding make(CoreWireSchemaTable table,
                                                        CoreWireSchemaNodeId root) {
        return VerifiedWireSchemaBinding(std::move(table), root);
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
    // Belt-and-suspenders: the codec-facing binding must satisfy the SAME public
    // local verifier a transported table would. (The projector already produced a
    // locally-valid table; this makes the invariant explicit at the trust seam.)
    auto local = verify_core_wire_schema_table_local(table);
    if (!local.empty()) {
        result.diagnostics = std::move(local);
        return result;
    }
    if (table.capabilities.size() != 1) {
        result.diagnostics.push_back(
            migration_error("wire-schema migration produced an unexpected capability count"));
        return result;
    }
    const CoreWireSchemaNodeId root = table.capabilities.front().result;
    result.binding = WireSchemaBindingFactory::make(std::move(table), root);
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
    // must pass the public local verifier AND have a root that is a real node.
    auto local = verify_core_wire_schema_table_local(table);
    if (!local.empty()) {
        diagnostics = std::move(local);
        return std::nullopt;
    }
    if (root.value >= table.nodes.size()) {
        diagnostics.push_back(
            migration_error("transported wire-schema root is out of range"));
        return std::nullopt;
    }
    return WireSchemaBindingFactory::make(std::move(table), root);
}

} // namespace ahfl::ir::core
