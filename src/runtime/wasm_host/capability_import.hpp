#pragma once

// RFC 0026 KR6.8 WH-3: the capability-import executor. The production
// ImportCallback the engine delivers every ahfl_cap import to. It resolves the
// call site from the import ordinal, decodes the arguments (opaque lane: wire
// JSON envelope; bridge lane: P4-D span walk), invokes the capability through
// the ContextualCapabilityInvoker, and returns the ImportReply carrying the raw
// ahfl_cap_status verbatim (the engine never classifies; the guest's compiled
// code is the classifier, exactly as run2's raw_status is carried verbatim and
// classified by the D1b controller).
//
// Two lanes (the engine admits both functypes at fresh_instance):
//   * opaque  (i32,i32)->(i32,i32,i32): the module writes a wire-JSON argument
//     envelope at (ptr,len); the host decodes it, invokes, serializes the
//     result to wire JSON, alloc_then_writes it, and replies
//     (status, result_ptr, result_len). The module's compiled code has graceful
//     ERROR/PENDING arms.
//   * bridge (i32)->(i32,i32): the single arg is the control-block pointer; the
//     host walks the dense P4-D argument spans (spill/root classification +
//     region authorization, mirroring the JS oracle's bridgeParamKind /
//     readBridgeArgValue), invokes, packs the result at the call site's
//     disjoint result placement, and replies (status, result_base). Any non-zero
//     status traps the module (the bridge has no graceful ERROR/PENDING arm).
//
// ImportAbort stays HOST-DECISION-failure-only (unknown call site, unknown
// capability, bad control block, arg decode failure, engine fault): a
// capability that executed but failed/pending is a raw non-OK reply, never an
// abort (2026-09-30 decision, Option A).

#include "runtime/engine/capability_bridge.hpp"
#include "runtime/engine/core_wasm_resume_engine.hpp"
#include "runtime/engine/core_wasm_schema_module.hpp"
#include "runtime/wasm_host/wasm3_engine.hpp"

#include "ahfl/compiler/ir/core_frame_layout.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ahfl::runtime::wasm_host {

/// Typed fail-closed reasons for the capability-import executor. Every error is
/// a fixed enum code; no diagnostic echoes page bytes, decoded names, or values.
enum class CapabilityImportError {
    /// No call site in the admitted module matches the import ordinal.
    CallSiteNotFound,
    /// The call site's source_symbol is not present in the wire-schema
    /// capability table.
    CapabilityNotInWireSchema,
    /// The name_resolver returned nullopt for the capability's source_symbol.
    CapabilityNameUnknown,
    /// The bridge control-block pointer lies outside the dense fixed-stride
    /// control-block region.
    BridgeBlockOutOfRange,
    /// The bridge control-block pointer is not at a stride-aligned offset.
    BridgeBlockStride,
    /// The control block's call_site_id word disagrees with its dense block
    /// address.
    BridgeCallSiteIdMismatch,
    /// The control block's arg_count word disagrees with the site's arity.
    BridgeArgCountMismatch,
    /// The bridge call site index is out of range for the frame section.
    BridgeSiteNotFound,
    /// The wire-schema capability's param count disagrees with the bridge
    /// site's arity.
    BridgeWireSchemaMismatch,
    /// A bridge argument span failed the P4-D walk (bounds/schema/region).
    ArgDecodeFailed,
    /// The opaque-lane param JSON failed schema-bound decode against the
    /// wire-schema param binding. Pre-effect: the capability is NEVER invoked.
    ParamSchemaInvalid,
    /// The opaque-lane result value failed schema-bound validation against the
    /// wire-schema result binding. Post-effect: the capability WAS invoked and
    /// returned Success, but the host rejects the result as a host fault.
    ResultSchemaInvalid,
    /// The opaque-lane result value is not wire-encodable (a closure).
    ResultEncodeFailed,
    /// The bridge result failed the P4-D pack.
    BridgeResultPackFailed,
    /// The engine's alloc_then_write failed.
    EngineAllocFailed,
    /// The engine's mutable_whole_memory failed.
    EngineMemoryFailed,
};

/// WH-4 fix-forward P2-1: the stable enum name for a CapabilityImportError,
/// for diagnostics and fail-closed messages. Never the raw integer.
[[nodiscard]] std::string_view
to_string(CapabilityImportError error) noexcept;

/// Shared error state: the callback writes the precise reason here before
/// returning ImportAbort, so the driver can classify the host-decision failure.
struct CapabilityImportState {
    std::optional<CapabilityImportError> last_error;
};

/// Configuration for the capability-import callback. All references must
/// outlive the callback (the driver owns them for the session).
struct CapabilityImportConfig {
    Wasm3ResumeEngine &engine;
    const core_wasm_schema_module::VerifiedCoreWasmSchemaModule &module;
    const ir::core::CoreFrameLayoutSection &frame_section;
    const ContextualCapabilityInvoker &invoker;
    /// Mutable per-call context: the executor sets `source_capability_symbol_id`
    /// from the resolved call site before each invoker call so the session
    /// layer can resolve the owning agent/node per-import (P2-2), not by
    /// name-keyed first-wins.
    CapabilityInvocationContext &context;
    /// Resolves a capability's source_symbol to its canonical name. The wire
    /// schema carries source_symbol (u64) but NOT the canonical name; the
    /// executor needs the name to invoke through the invoker.
    std::function<std::optional<std::string>(std::uint64_t)> name_resolver;
    CapabilityImportState &state;
};

/// Factory: build the ImportCallback the engine delivers every ahfl_cap import
/// to. The callback handles both lanes (opaque + bridge) transparently: the
/// engine's trampoline sets param_frame only for the 2-param opaque lane, so
/// param_frame.size() > 0 selects opaque and param_frame.size() == 0 selects
/// bridge.
[[nodiscard]] core_wasm_resume_engine::ImportCallback
make_capability_import_callback(CapabilityImportConfig config);

/// WH-4b: resolve the call site for an import ordinal (first match). Exposed
/// for the session memo-replay layer, which needs the call site's
/// source_symbol + result binding to cross-check memo hits and inject the
/// frontier without invoking the capability. Returns nullopt if no call site
/// in the module matches.
[[nodiscard]] std::optional<core_wasm_schema_module::VerifiedCoreWasmCallSite>
resolve_import_call_site(
    const core_wasm_schema_module::VerifiedCoreWasmSchemaModule &module,
    std::uint32_t import_ordinal);

/// WH-4b: the decoded opaque-lane arguments for an import. The session
/// memo-replay layer uses this to cross-check a memo hit's arg_hash without
/// invoking the capability (the invoker is never called on a memo hit, so the
/// arg_hash must be computed from the import observation itself).
struct OpaqueImportArgs {
    std::uint64_t source_symbol{0};
    std::vector<runtime::Value> args;
};

/// WH-4b: decode the opaque-lane argument envelope for an import. Resolves the
/// call site, finds the capability in the wire schema, parses the envelope,
/// and schema-bound decodes the single arity-1 argument. Returns nullopt on
/// any resolution / parse / decode failure (fail-closed: the caller treats it
/// as a replay divergence, never a live re-invoke).
[[nodiscard]] std::optional<OpaqueImportArgs>
decode_opaque_import_args(
    const core_wasm_schema_module::VerifiedCoreWasmSchemaModule &module,
    std::uint32_t import_ordinal,
    std::span<const std::uint8_t> param_frame);

/// WH-5b.2: the resolved bridge call site for an import observation. The
/// session memo-replay layer uses this to capture (origination) and inject
/// (replay) the exact P4-D frame + String payload bytes at the site's
/// disjoint result placement.
struct ResolvedBridgeSite {
    std::size_t site_index{0};
    const ir::core::CoreFrameBridgeCallSite *site{nullptr};
};

/// WH-5b.2: resolve the bridge call site from the import observation's
/// scalar_arg (the control-block pointer). Validates the block pointer is in
/// the dense fixed-stride control region, stride-aligned, and names the
/// expected site. Returns nullopt + sets error on any structural failure.
[[nodiscard]] std::optional<ResolvedBridgeSite>
resolve_bridge_call_site(const ir::core::CoreFrameLayoutSection &section,
                         std::uint32_t block_ptr,
                         CapabilityImportError &error) noexcept;

/// WH-5b.2: find a bridge call site by its call_site_id (the manifest
/// bridge-sites table joins to the frame section on this id). Returns nullptr
/// when no site carries the id.
[[nodiscard]] const ir::core::CoreFrameBridgeCallSite *
find_bridge_site_by_id(const ir::core::CoreFrameLayoutSection &section,
                       std::uint32_t call_site_id) noexcept;

/// WH-5b.2: decode a bridge call's P4-D arguments into host Values. Shared by
/// handle_bridge (live) and the session memo-replay layer (arg_hash), so the
/// origination and replay arg_hash are computed by the SAME code path. The
/// caller supplies the resolved call site (for the wire binding), bridge site
/// (for the P4-D layouts + spill regions), and control-block pointer (the
/// descriptor addresses are read at block_ptr + 8 + 8*i).
[[nodiscard]] std::optional<std::vector<runtime::Value>>
decode_bridge_import_args(
    const ir::core::CoreFrameLayoutSection &section,
    const core_wasm_schema_module::VerifiedCoreWasmCallSite &call_site,
    const ir::core::CoreFrameBridgeCallSite &site,
    std::uint32_t block_ptr,
    std::span<const std::uint8_t> whole_memory,
    CapabilityImportError &error);

/// WH-5b.2: pack a host Value into a bridge call site's disjoint result
/// placement (the same packing handle_bridge performs on a live success).
/// Used by the session memo-replay layer's Frontier path to inject the
/// pending capability result without invoking the capability. Zero-fills the
/// frame region, packs the value via pack_value_at (String bytes bump into
/// the site's result_payload arena), and returns the ImportReply identical in
/// shape to live placement.
[[nodiscard]] core_wasm_resume_engine::ImportCallbackResult
pack_bridge_result_value(
    Wasm3ResumeEngine &engine,
    const ir::core::CoreFrameLayoutSection &section,
    const core_wasm_schema_module::VerifiedCoreWasmCallSite &call_site,
    const ir::core::CoreFrameBridgeCallSite &site,
    runtime::Value value,
    CapabilityImportError &error);

} // namespace ahfl::runtime::wasm_host
