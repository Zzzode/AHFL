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
// future B2-D host); A2 does not itself emit the manifest (the landed compiler-side
// B2-C supplies the AHFLXM section); it COMPUTES and EXPOSES the three artifact
// digests (whole module / raw AHFLWS payload / raw AHFLXM payload) as authority but
// does NOT compare them and does NOT close the digest gate (that comparison is
// future B2-D / D1b); it adds no
// compiler_ir Wasm/manifest knowledge, and B2 / KR6.5 stay false.
//
// The wire-schema section is admitted through the existing C1
// `decode_core_wire_schema_table` authority; this file re-implements only the
// module framing (the E4-B1 C3 helpers are private to their translation unit) and
// the exec-manifest codec. It never exposes a raw `CoreWireSchemaTable`,
// `CoreWireSchemaNodeId`, or the raw manifest.

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "ahfl/compiler/ir/core_ir.hpp"            // CoreWorkflowId/NodeId, CoreCapabilityId
#include "ahfl/compiler/ir/core_wire_migration.hpp" // VerifiedWireSchemaBinding
#include "runtime/engine/core_wasm_resume_record.hpp" // InvocationOrdinal (A1 strong type)

namespace ahfl::runtime::core_wasm_schema_module {

// A raw SHA-256 artifact digest (32 bytes, NOT hex). A2 computes three of these
// during its single framing pass -- over the whole emitted module, the raw AHFLWS
// wire-schema payload (no custom-name framing), and the raw AHFLXM exec-manifest
// payload (no custom-name framing) -- and exposes them by value. It never COMPARES
// them against a resume record: the digest gate is future B2-D (D1b). This is a
// plain std::array, so the header leaks no base-support type.
using ArtifactDigest = std::array<std::uint8_t, 32>;

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

// WH-5b.2 (manifest v2): one decoded in-runner bridge call site. A P6 bridge
// node's scheduler-boundary cap_call_count is 0 (its completion is a tag-0
// identity event); its in-handler bridge imports are accounted here. The
// ordinal is the dense per-node bridge-call ordinal (0-based, execution order,
// aligned with the host recorder's per_node_counters_); call_site_id joins to
// the frame section's bridge_call_sites for result placement. The host
// cross-checks this table against the frame section at session admission.
struct ManifestBridgeSite {
    std::uint8_t ordinal{0};
    std::uint32_t call_site_id{0};
    ir::core::CoreCapabilityId capability{};
    std::uint64_t source_symbol{0};

    [[nodiscard]] friend bool operator==(const ManifestBridgeSite &,
                                         const ManifestBridgeSite &) noexcept = default;
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
    // WH-5b.2: the node's in-runner bridge call sites (manifest v2). Empty for
    // an opaque cap site or a pure identity node; non-empty for a P6 bridge
    // node (whose cap_call_count is 0). The host cross-checks this table
    // against the frame section's bridge_call_sites.
    [[nodiscard]] std::span<const ManifestBridgeSite> bridge_sites() const noexcept;

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

// RFC 0026 KR6.5 E4-B2-D2a-F3: the linear-memory capacity the admitted artifact
// DECLARES, read from the module's own Memory (wasm section id 5) section during
// A2's single framing pass over the exact digest-authenticated bytes. This is the
// artifact-derived authority a host cross-checks against the F1 fixed-page SSOT
// (`kCoreWasmFixedLinearMemoryCapacityBytes`) BEFORE instantiation -- no VM needed.
// A conforming Core-Wasm artifact declares exactly one memory with one 64 KiB
// page and no declared maximum, so an admitted capacity is always
// {capacity_bytes == 65536, min_pages == 1, has_max == false}.
struct ArtifactMemoryCapacity {
    std::uint32_t capacity_bytes = 0; // min_pages * 65536 (the declared floor)
    std::uint32_t min_pages = 0;
    bool has_max = false;

    [[nodiscard]] friend bool operator==(const ArtifactMemoryCapacity &,
                                         const ArtifactMemoryCapacity &) noexcept = default;
};

// Why the declared memory does NOT satisfy the fixed single-page contract. A
// malformed Memory section is NOT reported here: it fails module framing (the
// section is exact-consumed like Type/Import), so no module handle exists on
// which this getter could be called.
enum class MemoryDeclError : std::uint8_t {
    MissingMemorySection, // the module carries no Memory (wasm section id 5) section
    MemoryCountNotOne,    // the section declares zero or more than one memory
    DeclaredMaximum,      // the sole memory carries a declared maximum (limits flags == 1)
    MinPagesNotOne,       // the sole memory's minimum page count is not exactly 1
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
    // Workflow-module precondition: an agent module has no workflow identity /
    // nodes (entry_id is the invalid sentinel, node_count is 0); use is_agent()
    // / agent_id() for the agent arm.
    [[nodiscard]] ir::core::CoreWorkflowId entry_id() const noexcept;
    [[nodiscard]] std::size_t node_count() const noexcept;
    [[nodiscard]] std::size_t call_site_count() const noexcept;

    // WH-4 fix-forward D-C: the manifest entry kind. An agent module carries a
    // flat capability-list manifest (no workflow schedule); the factory builds
    // one call site per manifest capability. `agent_id()` is the agent arm's
    // declared identity (the invalid sentinel for a workflow module).
    [[nodiscard]] bool is_agent() const noexcept;
    [[nodiscard]] ir::core::CoreAgentId agent_id() const noexcept;

    // Resolve ANY node (incl. identity nodes) by schedule index.
    [[nodiscard]] VerifiedCoreWasmNodeResult resolve_node(ManifestNodeIndex index) const;
    // Resolve a capability call site (import cursor authority) by call-site index.
    [[nodiscard]] VerifiedCoreWasmCallSiteResult resolve(ManifestCallSiteIndex index) const;

    // The three raw SHA-256 artifact digests computed during framing: the whole
    // emitted module, the raw AHFLWS wire-schema payload (no custom-name framing),
    // and the raw AHFLXM exec-manifest payload (no custom-name framing). Returned by
    // value; stable after this handle (or a resolved token) is copied or the original
    // handle is dropped (the shared immutable payload holds them). A2 exposes these
    // as authority but performs NO comparison -- the digest gate is future B2-D (D1b).
    [[nodiscard]] ArtifactDigest module_sha256() const noexcept;
    [[nodiscard]] ArtifactDigest wire_schema_sha256() const noexcept;
    [[nodiscard]] ArtifactDigest exec_manifest_sha256() const noexcept;

    // The fixed single-page capacity the admitted artifact's OWN Memory section
    // declares, parsed from the exact digest-authenticated bytes during framing.
    // Fails closed with a typed `MemoryDeclError` when the declaration is missing
    // or does not satisfy the exactly-one-memory / no-maximum / one-page contract;
    // the host compares the returned `capacity_bytes` against the F1 fixed-page
    // SSOT before instantiation. A structurally malformed Memory section never
    // reaches this handle (it fails framing at admission).
    [[nodiscard]] std::expected<ArtifactMemoryCapacity, MemoryDeclError>
    declared_linear_memory_capacity() const noexcept;

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

// Admit a capability module (workflow OR agent, WH-4 fix-forward D-C): frame
// it, admit the exec-manifest + wire schema (manifest immediately before the
// EOF wire-schema section), cross-check import<->schema<->manifest capability
// identity to an EXACT authority set, and eagerly mint Param{0}+Result for
// every call site. A workflow manifest accounts its cap-bearing nodes; an
// agent manifest accounts its flat capability list (one call site each, no
// workflow schedule). Fails closed (no module) on any framing, decode,
// cross-check, cardinality, or mint error, with fixed no-echo diagnostics.
// Pure over `module_bytes` (borrowed).
[[nodiscard]] VerifiedCoreWasmSchemaModuleResult
make_verified_core_wasm_schema_module(std::span<const std::uint8_t> module_bytes);

} // namespace ahfl::runtime::core_wasm_schema_module
