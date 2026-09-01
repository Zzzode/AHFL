#pragma once

// RFC 0026 KR6.5 E4-B2-A2: verified Core-Wasm schema MODULE context + execution
// manifest (`ahfl.wasm-exec-manifest.v1`, raw payload magic "AHFLXM") decoder.
//
// This is the runtime CALL-SITE AUTHORITY for a capability-bearing workflow module.
// It frames the module ONCE, admits BOTH custom sections (the exec-manifest
// immediately before the EOF wire-schema section), decodes + cross-checks them, and
// EAGERLY mints the typed Param/Result bindings for every capability call site. An
// admitted `VerifiedCoreWasmSchemaModule` therefore GUARANTEES every call site is
// usable; a caller can never submit an arbitrary `(capability, source_symbol)` to
// mint. It is FOUNDATION only: it has no production caller yet (the first is the
// future B2-D host), it does not emit the manifest (that is future B2-C), it makes
// NO artifact-digest claim (that is the future B2-D digest gate), it adds no
// compiler_ir Wasm/manifest knowledge, and B2 / KR6.5 stay false.
//
// The wire-schema section is admitted through the existing C1
// `decode_core_wire_schema_table` authority; this file re-implements only the
// module framing (the E4-B1 C3 helpers are private to their translation unit) and
// the exec-manifest codec. It never exposes a raw `CoreWireSchemaTable`,
// `CoreWireSchemaNodeId`, or the raw manifest.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "ahfl/compiler/ir/core_ir.hpp"            // CoreWorkflowId/NodeId, CoreCapabilityId
#include "ahfl/compiler/ir/core_wire_migration.hpp" // VerifiedWireSchemaBinding
#include "runtime/engine/core_wasm_resume_record.hpp" // InvocationOrdinal (A1 strong type)

namespace ahfl::runtime::core_wasm_schema_module {

namespace detail {
// The single immutable payload shared by an admitted module handle and every node
// / call-site token it resolves. Forward-declared here and defined in the .cpp; all
// handles hold a `std::shared_ptr<const SchemaModulePayload>` to THE SAME type, so a
// token stays valid after any other handle is dropped and never exposes the raw
// table / manifest / NodeId.
struct SchemaModulePayload;
} // namespace detail

// A capability call SITE index: enumerates, in schedule order, the manifest nodes
// whose `cap_call_count == 1`. This is the host's import cursor authority. It is a
// DISTINCT strong type from a Wasm import ordinal and from a per-node invocation
// ordinal, so the three can never share a variable or diagnostic.
struct ManifestCallSiteIndex {
    std::size_t value{0};
    [[nodiscard]] friend bool operator==(ManifestCallSiteIndex,
                                         ManifestCallSiteIndex) noexcept = default;
};

// A manifest NODE index: the dense schedule position 0..node_count-1 (every node,
// including `cap_call_count == 0` identity nodes). Distinct from a call-site index.
struct ManifestNodeIndex {
    std::size_t value{0};
    [[nodiscard]] friend bool operator==(ManifestNodeIndex, ManifestNodeIndex) noexcept = default;
};

// Position of a capability import in the module's `ahfl_cap` import table (equal to
// the position of its entry in the verified wire-schema capability table). Distinct
// from a call-site index and from an invocation ordinal.
struct CapabilityImportOrdinal {
    std::uint32_t value{0};
    [[nodiscard]] friend bool operator==(CapabilityImportOrdinal,
                                         CapabilityImportOrdinal) noexcept = default;
};

// A verified view of ONE manifest node (any kind). Opaque + copy-only: it shares
// the admitted module's immutable payload, so it stays valid after the module
// handle is dropped. It exposes only the narrow coordinates B2-D needs to compare a
// record ledger node-by-node; it never exposes the raw table / manifest / NodeId.
class VerifiedCoreWasmNode {
  public:
    VerifiedCoreWasmNode(const VerifiedCoreWasmNode &) = default;
    VerifiedCoreWasmNode &operator=(const VerifiedCoreWasmNode &) = default;

    [[nodiscard]] ir::core::CoreWorkflowNodeId workflow_node_id() const noexcept;
    [[nodiscard]] ManifestNodeIndex schedule_pos() const noexcept;
    [[nodiscard]] std::uint8_t cap_call_count() const noexcept;

  private:
    friend struct SchemaModuleFactory;
    friend class VerifiedCoreWasmSchemaModule;
    VerifiedCoreWasmNode(std::shared_ptr<const detail::SchemaModulePayload> payload,
                         std::size_t node_index)
        : payload_(std::move(payload)), node_index_(node_index) {}

    std::shared_ptr<const detail::SchemaModulePayload> payload_;
    std::size_t node_index_;
};

// A verified capability CALL SITE. Opaque + copy-only, sharing the admitted
// module's immutable payload. Its Param/Result bindings were minted EAGERLY at
// module admission (so there is no failable per-use mint); the getters return them
// BY VALUE (a cheap shared_ptr copy) so a caller can hold a binding independently of
// the token's lifetime. The narrow coordinate getters expose only host-required
// values; no raw table / manifest / NodeId is reachable.
class VerifiedCoreWasmCallSite {
  public:
    VerifiedCoreWasmCallSite(const VerifiedCoreWasmCallSite &) = default;
    VerifiedCoreWasmCallSite &operator=(const VerifiedCoreWasmCallSite &) = default;

    [[nodiscard]] ir::core::CoreWorkflowNodeId workflow_node_id() const noexcept;
    [[nodiscard]] ManifestNodeIndex schedule_pos() const noexcept;
    [[nodiscard]] core_wasm_resume::InvocationOrdinal invocation_ordinal() const noexcept;
    [[nodiscard]] ir::core::CoreCapabilityId capability() const noexcept;
    [[nodiscard]] std::uint64_t source_symbol() const noexcept;
    [[nodiscard]] CapabilityImportOrdinal import_ordinal() const noexcept;

    // The eagerly-minted bindings, returned by value (cheap shared_ptr copy).
    [[nodiscard]] ir::core::VerifiedWireSchemaBinding param_binding() const;
    [[nodiscard]] ir::core::VerifiedWireSchemaBinding result_binding() const;

  private:
    friend struct SchemaModuleFactory;
    friend class VerifiedCoreWasmSchemaModule;
    VerifiedCoreWasmCallSite(std::shared_ptr<const detail::SchemaModulePayload> payload,
                             std::size_t call_site_index)
        : payload_(std::move(payload)), call_site_index_(call_site_index) {}

    std::shared_ptr<const detail::SchemaModulePayload> payload_;
    std::size_t call_site_index_;
};

struct VerifiedCoreWasmNodeResult {
    std::optional<VerifiedCoreWasmNode> node;
    std::vector<ir::core::CoreLowerDiagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept;
    [[nodiscard]] bool ok() const noexcept { return node.has_value() && !has_errors(); }
};

struct VerifiedCoreWasmCallSiteResult {
    std::optional<VerifiedCoreWasmCallSite> call_site;
    std::vector<ir::core::CoreLowerDiagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept;
    [[nodiscard]] bool ok() const noexcept { return call_site.has_value() && !has_errors(); }
};

// The admitted module context: an opaque, copy-only handle over one immutable,
// fully-verified payload. Copies share the payload; a copy or a resolved token
// stays valid after any other handle is dropped. There is no public/default/move
// ctor and no raw-table / manifest / NodeId accessor.
class VerifiedCoreWasmSchemaModule {
  public:
    VerifiedCoreWasmSchemaModule(const VerifiedCoreWasmSchemaModule &) = default;
    VerifiedCoreWasmSchemaModule &operator=(const VerifiedCoreWasmSchemaModule &) = default;

    // Narrow full-workflow read surface (for B2-D coordinate comparison).
    [[nodiscard]] ir::core::CoreWorkflowId entry_id() const noexcept;
    [[nodiscard]] std::size_t node_count() const noexcept;
    [[nodiscard]] std::size_t call_site_count() const noexcept;

    // Resolve ANY node (incl. identity nodes) by schedule index.
    [[nodiscard]] VerifiedCoreWasmNodeResult resolve_node(ManifestNodeIndex index) const;
    // Resolve a capability call site (import cursor authority) by call-site index.
    [[nodiscard]] VerifiedCoreWasmCallSiteResult resolve(ManifestCallSiteIndex index) const;

  private:
    friend struct SchemaModuleFactory;
    explicit VerifiedCoreWasmSchemaModule(
        std::shared_ptr<const detail::SchemaModulePayload> payload)
        : payload_(std::move(payload)) {}

    std::shared_ptr<const detail::SchemaModulePayload> payload_;
};

struct VerifiedCoreWasmSchemaModuleResult {
    std::optional<VerifiedCoreWasmSchemaModule> module;
    std::vector<ir::core::CoreLowerDiagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept;
    [[nodiscard]] bool ok() const noexcept { return module.has_value() && !has_errors(); }
};

// Admit a capability-workflow module: frame it, admit the exec-manifest + wire
// schema (manifest immediately before the EOF wire-schema section), cross-check
// import<->schema<->manifest capability identity to an EXACT authority set, and
// eagerly mint Param{0}+Result for every call site. Fails closed (no module) on any
// framing, decode, cross-check, cardinality, or mint error, with fixed no-echo
// diagnostics. Pure over `module_bytes` (borrowed).
[[nodiscard]] VerifiedCoreWasmSchemaModuleResult
make_verified_core_wasm_schema_module(std::span<const std::uint8_t> module_bytes);

} // namespace ahfl::runtime::core_wasm_schema_module
