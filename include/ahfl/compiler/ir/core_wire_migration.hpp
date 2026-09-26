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

/// RFC 0026 P6-7: which agent boundary a frame-root selector names. A frame
/// binding root is always the admitted P6-frame module's own input or output
/// boundary nominal — never a capability slot.
enum class CoreWireFrameRootKind { Input, Output };

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

/// RFC 0026 P6-7: a typed selector naming ONE agent boundary root of a
/// frame-root schema table. The factory verifies the table carries a frame-root
/// block and derives the binding root from it, so a caller cannot point a frame
/// binding at a capability root or an arbitrary descendant node. This is a
/// SIBLING selector: it does not weaken the capability-slot-only invariant of
/// `CoreWireRootSelector`.
struct CoreWireFrameRootSelector {
    CoreWireFrameRootKind kind{CoreWireFrameRootKind::Input};
};

/// An opaque, immutable, copy-only authority proving that ONE `CoreWireSchemaTable`
/// passed the public local verifier. It is the B2-A-pre shared-authority handle:
/// many typed Param/Result bindings can be minted from a single instance
/// (`make_wire_binding_from_verified_table`) WITHOUT re-verifying or re-copying the
/// table — every minted binding shares this handle's one `shared_ptr<const>`
/// backing. The only way to obtain one is `make_verified_wire_schema_table`, which
/// mints a handle ONLY when the local verifier's diagnostic bag is empty.
///
/// The handle is deliberately OPAQUE: it exposes NO table / node / capability
/// accessor, so it can never leak a raw `CoreWireSchemaNodeId` or an unminted table
/// view. Reading verified shape is done only through a minted
/// `VerifiedWireSchemaBinding::table()`. Copy-only (no move declared, no default
/// ctor, private ctor): a copy shares the same immutable backing, and an rvalue
/// copies the cheap shared_ptr, so a source instance stays fully usable after a
/// copy and there is no moved-from state.
class VerifiedWireSchemaTable {
  public:
    VerifiedWireSchemaTable(const VerifiedWireSchemaTable &) = default;
    VerifiedWireSchemaTable &operator=(const VerifiedWireSchemaTable &) = default;
    // Intentionally NO move ctor/assignment: copy-only immutable authority.

  private:
    friend struct WireSchemaBindingFactory;
    explicit VerifiedWireSchemaTable(std::shared_ptr<const CoreWireSchemaTable> table)
        : table_(std::move(table)) {}

    std::shared_ptr<const CoreWireSchemaTable> table_;
};

/// An immutable, copy-only binding of a verified wire-schema table to a single
/// capability signature slot. The ONLY ways to obtain one are the verifying
/// factories below. The legacy/source and transport paths (TypeRef migration in
/// C2, transported-table in E4-B1) run the public local verifier AND derive the
/// root from a typed `CoreWireRootSelector`. The B2-A-pre verified-table mint
/// (`make_wire_binding_from_verified_table`) performs NO local re-verification —
/// it relies on the prior one-time admission that produced the immutable
/// `VerifiedWireSchemaTable` authority and only derives the root. In every case
/// the root comes from a typed `CoreWireRootSelector` whose capability id +
/// source_symbol are cross-checked against the table (a reachable descendant or
/// another capability's root is NOT a valid binding root). The codec consumes ONLY
/// this type, so a hand-built / tampered `CoreWireSchemaTable` can never reach the
/// decode/validate rule engine.
///
/// The outer payload is a `shared_ptr<const>`: the type is copy-only (no move ctor
/// / assignment is declared, so a user-declared copy suppresses the implicit move
/// and an rvalue copies the cheap shared_ptr). There is therefore NO public
/// moved-from state whose invariant could be violated — every live instance holds
/// a fully-verified payload with a selector and its derived root pinned together.
/// The payload holds the verified `CoreWireSchemaTable` behind ITS OWN
/// `shared_ptr<const>`, so many bindings minted from one `VerifiedWireSchemaTable`
/// share a single verified-once table allocation (see `table()` identity below).
class VerifiedWireSchemaBinding {
  public:
    VerifiedWireSchemaBinding(const VerifiedWireSchemaBinding &) = default;
    VerifiedWireSchemaBinding &operator=(const VerifiedWireSchemaBinding &) = default;
    // Intentionally NO move ctor/assignment: copy-only immutable handle.

    [[nodiscard]] const CoreWireSchemaTable &table() const noexcept { return *payload_->table; }
    [[nodiscard]] CoreWireSchemaNodeId root() const noexcept { return payload_->root; }
    [[nodiscard]] const CoreWireRootSelector &selector() const noexcept {
        return payload_->selector;
    }

  private:
    friend struct WireSchemaBindingFactory;
    struct Payload {
        std::shared_ptr<const CoreWireSchemaTable> table; // shared verified backing
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

/// Result of admitting a `CoreWireSchemaTable` into a `VerifiedWireSchemaTable`
/// authority. Shape mirrors `WireSchemaMigrationResult`: on success `table` holds
/// the opaque handle and `diagnostics` is empty; on rejection `table` is nullopt
/// and `diagnostics` carries the local verifier's exact bag.
struct VerifiedWireSchemaTableResult {
    std::optional<VerifiedWireSchemaTable> table;
    std::vector<CoreLowerDiagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        for (const auto &d : diagnostics) {
            if (d.severity == CoreDiagnosticSeverity::Error) {
                return true;
            }
        }
        return false;
    }
    [[nodiscard]] bool ok() const noexcept { return table.has_value() && !has_errors(); }
};

/// B2-A-pre admission factory: run the public local verifier over `table` ONCE and,
/// iff its diagnostic bag is EMPTY (preserving today's reject-on-any-diagnostic
/// behavior, not merely no-Error), mint an opaque `VerifiedWireSchemaTable`
/// authority backed by one `shared_ptr<const CoreWireSchemaTable>`. This is the
/// ONLY way to obtain the authority. It performs local verification exactly once
/// per authority; per-binding mints below add NO further table verification/copy.
[[nodiscard]] VerifiedWireSchemaTableResult
make_verified_wire_schema_table(CoreWireSchemaTable table);

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

/// B2-A-pre shared-authority mint: derive a typed binding root from `selector` and
/// mint a `VerifiedWireSchemaBinding` that SHARES `verified`'s one immutable table
/// backing. This performs NO local table re-verification (the authority is
/// verified-once and immutable); it runs only the unchanged root-derivation SSOT
/// (`derive_root`: capability id present + source_symbol cross-check + kind/param).
/// `diagnostics` is cleared on entry; a returned nullopt therefore carries exactly
/// this call's derivation failure (and can never be nullopt with an empty bag).
[[nodiscard]] std::optional<VerifiedWireSchemaBinding>
make_wire_binding_from_verified_table(const VerifiedWireSchemaTable &verified,
                                      const CoreWireRootSelector &selector,
                                      std::vector<CoreLowerDiagnostic> &diagnostics);

/// RFC 0026 P6-7: mint a binding for a P6-frame agent boundary root (input or
/// output) from a verified table that CARRIES the frame-root block. The root is
/// derived from the typed frame selector, so a table without frame roots (a
/// capability-only table) cannot produce a frame binding and no raw NodeId can
/// be supplied by the caller.
[[nodiscard]] std::optional<VerifiedWireSchemaBinding>
make_frame_binding_from_verified_table(const VerifiedWireSchemaTable &verified,
                                       const CoreWireFrameRootSelector &selector,
                                       std::vector<CoreLowerDiagnostic> &diagnostics);

} // namespace ahfl::ir::core
