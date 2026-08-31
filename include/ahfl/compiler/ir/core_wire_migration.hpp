#pragma once

// RFC 0026 KR6.5 E4-B0-C2: AHFL -> wire-schema migration projector.
//
// The wire-schema codec (E4-B0-C2) and its E4-B1 transport consume ONE authority:
// a verified `CoreWireSchemaTable` rooted at a `CoreWireSchemaNodeId`. A native
// host, however, holds an AHFL `ir::TypeRef` (+ the verified `ir::Program`), not a
// Core-projected table. This migration projector is the ONLY bridge: it lowers a
// TypeRef through the SAME Core value-type + P4-C member-template SSOT
// (`build_core_type_environment` + `lower_value_type_into`) and the SAME C1
// projector (`project_core_wire_schema`), then wraps the result in an
// un-forgeable `VerifiedWireSchemaBinding`. It NEVER re-implements schema
// semantics, and it fails closed (no binding) on any type that cannot be
// faithfully projected (Any / Unresolved / erased TypeVar / a user nominal with
// no verified declaration / Never / Fn / Closure / non-String Map key).
//
// This is design §2.5's "TypeRef migration projector into the same
// WireSchemaTable" — NOT a second schema authority.

#include <optional>
#include <vector>

#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/core_wire_schema.hpp"
#include "ahfl/compiler/ir/types.hpp"

namespace ahfl::ir::core {

/// An immutable, constructor-guarded binding of a verified wire-schema table to a
/// single root node. The ONLY ways to obtain one are the verifying factories
/// below (TypeRef migration in C2, transported-table in E4-B1); both run the
/// public local verifier + root gate before minting one. The codec consumes ONLY
/// this type, so a hand-built / tampered `CoreWireSchemaTable` can never reach the
/// decode/validate rule engine.
class VerifiedWireSchemaBinding {
  public:
    VerifiedWireSchemaBinding(const VerifiedWireSchemaBinding &) = default;
    VerifiedWireSchemaBinding(VerifiedWireSchemaBinding &&) = default;
    VerifiedWireSchemaBinding &operator=(const VerifiedWireSchemaBinding &) = default;
    VerifiedWireSchemaBinding &operator=(VerifiedWireSchemaBinding &&) = default;

    [[nodiscard]] const CoreWireSchemaTable &table() const noexcept { return table_; }
    [[nodiscard]] CoreWireSchemaNodeId root() const noexcept { return root_; }

  private:
    friend struct WireSchemaBindingFactory;
    VerifiedWireSchemaBinding(CoreWireSchemaTable table, CoreWireSchemaNodeId root)
        : table_(std::move(table)), root_(root) {}

    CoreWireSchemaTable table_;
    CoreWireSchemaNodeId root_{};
};

struct WireSchemaMigrationResult {
    std::optional<VerifiedWireSchemaBinding> binding;
    std::vector<CoreLowerDiagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        for (const auto &d : diagnostics) {
            if (d.severity == CoreDiagnosticSeverity::Error) {
                return true;
            }
        }
        return false;
    }
    [[nodiscard]] bool ok() const noexcept { return binding.has_value() && !has_errors(); }
};

/// Migrate one AHFL `ir::TypeRef` into a verified wire-schema binding, using the
/// nominal universe of `program` (its type declarations resolve user nominals
/// id-first through the single `resolve_nominal_strict` path). Pure: neither
/// `program` nor any global arena is mutated. Fails closed with no binding on any
/// non-projectable type. The `seed` is the shared type-table projection of
/// `program` (`build_core_type_environment`); callers that migrate many TypeRefs
/// against one program should build the seed once and pass it in.
[[nodiscard]] WireSchemaMigrationResult
migrate_type_ref_to_wire_binding(const ir::TypeRef &type,
                                 const CoreTypeEnvironmentSeed &seed);

/// Convenience overload that builds the seed from `program` internally. Prefer the
/// seed-taking overload when migrating multiple TypeRefs against one program.
[[nodiscard]] WireSchemaMigrationResult
migrate_type_ref_to_wire_binding(const ir::TypeRef &type, const AhflIr &program);

/// E4-B1 factory (declared here so both bindings share one type): wrap a table
/// that arrived over transport, after running the public local verifier + root
/// gate. Not used in C2 (no transport yet); present so the codec's binding type is
/// stable across slices. Defined in E4-B1.
[[nodiscard]] std::optional<VerifiedWireSchemaBinding>
make_wire_binding_from_transported_table(CoreWireSchemaTable table, CoreWireSchemaNodeId root,
                                         std::vector<CoreLowerDiagnostic> &diagnostics);

} // namespace ahfl::ir::core
