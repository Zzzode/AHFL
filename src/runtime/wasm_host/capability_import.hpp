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
#include <string>

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
    const CapabilityInvocationContext &context;
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

} // namespace ahfl::runtime::wasm_host
