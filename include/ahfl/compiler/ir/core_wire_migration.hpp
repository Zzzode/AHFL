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

#include <memory>
#include <optional>
#include <vector>

#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/core_wire_schema.hpp"
#include "ahfl/compiler/ir/types.hpp"

namespace ahfl::ir::core {

/// Which slot of a capability signature a binding root denotes. A binding root is
/// ALWAYS a capability param or result — never a mere reachable descendant.
enum class CoreWireRootKind { Param, Result };

/// A typed selector that names EXACTLY one capability signature slot. The binding
/// factory takes this instead of a raw `CoreWireSchemaNodeId`, so a caller cannot
/// point a binding at an arbitrary in-range node (e.g. another capability's root,
/// or a descendant field) — the factory looks the capability up by id, requires
/// its `source_symbol` to match `expected_source_symbol`, and then DERIVES the
/// root from `kind` / `param_index`. `param_index` is meaningful only for
/// `Param`; for `Result` it must be 0.
struct CoreWireRootSelector {
    CoreCapabilityId capability{};
    std::uint64_t expected_source_symbol{0};
    CoreWireRootKind kind{CoreWireRootKind::Result};
    std::uint32_t param_index{0};
};

/// An immutable, copy-only binding of a verified wire-schema table to a single
/// capability signature slot. The ONLY ways to obtain one are the verifying
/// factories below (TypeRef migration in C2, transported-table in E4-B1); both
/// run the public local verifier AND derive the root from a typed
/// `CoreWireRootSelector` whose capability id + source_symbol are cross-checked
/// against the table (a reachable descendant or another capability's root is NOT
/// a valid binding root). The codec consumes ONLY this type, so a hand-built /
/// tampered `CoreWireSchemaTable` can never reach the decode/validate rule engine.
///
/// The payload is a `shared_ptr<const>`: the type is copy-only (no move ctor /
/// assignment is declared, so a user-declared copy suppresses the implicit move
/// and an rvalue copies the cheap shared_ptr). There is therefore NO public
/// moved-from state whose invariant could be violated — every live instance holds
/// a fully-verified payload with a selector and its derived root pinned together.
class VerifiedWireSchemaBinding {
  public:
    VerifiedWireSchemaBinding(const VerifiedWireSchemaBinding &) = default;
    VerifiedWireSchemaBinding &operator=(const VerifiedWireSchemaBinding &) = default;
    // Intentionally NO move ctor/assignment: copy-only immutable handle.

    [[nodiscard]] const CoreWireSchemaTable &table() const noexcept { return payload_->table; }
    [[nodiscard]] CoreWireSchemaNodeId root() const noexcept { return payload_->root; }
    [[nodiscard]] const CoreWireRootSelector &selector() const noexcept {
        return payload_->selector;
    }

  private:
    friend struct WireSchemaBindingFactory;
    struct Payload {
        CoreWireSchemaTable table;
        CoreWireRootSelector selector;
        CoreWireSchemaNodeId root; // derived from selector; pinned with it
    };
    explicit VerifiedWireSchemaBinding(std::shared_ptr<const Payload> payload)
        : payload_(std::move(payload)) {}

    std::shared_ptr<const Payload> payload_;
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

/// Migrate one AHFL `ir::TypeRef` into a verified wire-schema binding, using a
/// verified type environment (its type declarations resolve user nominals
/// id-first through the single `resolve_nominal_strict` path). Pure: neither the
/// environment nor any global arena is mutated. Fails closed with no binding on
/// any non-projectable type.
[[nodiscard]] WireSchemaMigrationResult
migrate_type_ref_to_wire_binding(const ir::TypeRef &type,
                                 const VerifiedCoreTypeEnvironment &environment);

/// Convenience overload that builds the type environment from `program`
/// internally (fails closed if the environment does not verify). Prefer the
/// environment-taking overload when migrating multiple TypeRefs against one
/// program.
[[nodiscard]] WireSchemaMigrationResult
migrate_type_ref_to_wire_binding(const ir::TypeRef &type, const AhflIr &program);

/// E4-B1 transport factory (declared here so both bindings share one type): wrap
/// a table that arrived over transport, after running the public local verifier
/// AND deriving the root from a typed `CoreWireRootSelector` (capability id +
/// source_symbol cross-check + kind/param_index; never a raw NodeId). Defined in
/// this translation unit so the codec's binding type is stable across slices;
/// E4-B1 will call it with a transported table. `diagnostics` is cleared on
/// entry, so a returned nullopt carries exactly this call's failure.
[[nodiscard]] std::optional<VerifiedWireSchemaBinding>
make_wire_binding_from_transported_table(CoreWireSchemaTable table,
                                         const CoreWireRootSelector &selector,
                                         std::vector<CoreLowerDiagnostic> &diagnostics);

} // namespace ahfl::ir::core
