#pragma once

// WH-5b.3: the deterministic encoding-boundary transcode handler. The
// production ImportCallback the engine delivers every ahfl_xcode import to.
// A transcode import is a PURE CODEC ADAPTER: it converts one value between
// the P4-D binary frame encoding and the wire-JSON transport encoding at the
// host/wasm boundary. It NEVER invokes a capability, touches memo, fires
// capability hooks, or writes an event record. The session routes it BEFORE
// the memo/replay/event_count machinery (isomorphic to the bridge skip).
//
// Two directions:
//   * P4D_TO_JSON  (i32,i32)->(i32,i32,i32): the module pushes a P4-D source
//     frame (entry shadow or a P6 producer's O_k); the host reads it via
//     read_value_at, validates the Value against the wire binding, serializes
//     it to wire JSON, alloc_then_writes it, and replies (0, json_ptr,
//     json_len). The result becomes the opaque runner's argument.
//   * JSON_TO_P4D  (i32,i32)->(i32,i32,i32): the module pushes a wire-JSON
//     source (the entry pointer or an opaque producer's result); the host
//     decodes it under the wire binding, packs it INLINE into the reused
//     static shadow region via pack_value_at (String bytes bump-allocate in
//     the disjoint payload arena), and replies (0, shadow_base,
//     shadow_extent). The materializer then reads from the shadow.
//
// Every OOM / overflow / schema failure returns a nonzero status that the
// guest converts to NodeFailed WITHOUT writing an event record or bumping
// completed_count (the scheduler traps on a nonzero transcode status).

#include "runtime/engine/core_wasm_resume_engine.hpp"
#include "runtime/wasm_host/wasm3_engine.hpp"

#include "ahfl/compiler/ir/core_frame_layout.hpp"
#include "ahfl/compiler/ir/core_wire_migration.hpp"
#include "ahfl/compiler/ir/core_wire_schema.hpp"
#include "compiler/backends/wasm/core_wasm_codegen.hpp"

#include <cstdint>

namespace ahfl::runtime::wasm_host {

/// Configuration for the transcode handler. All references must outlive the
/// callback (the session owns them for the run).
struct TranscodeConfig {
    Wasm3ResumeEngine &engine;
    const ir::core::CoreFrameLayoutSection &frame_section;
    const ir::core::CoreWireSchemaTable &wire_table;
    const backends::CoreWasmExecutionDescriptor &descriptor;
    /// The verified wire-schema authority (admitted once per session) from
    /// which the handler mints the typed node/frame binding for each
    /// direction's boundary root.
    const ir::core::VerifiedWireSchemaTable &verified_wire;
    /// The append-only payload arena cursor for JSON_TO_P4D String bytes.
    /// Initialized to transcode_payload_base by the session; bumped by
    /// pack_value_at on every JSON_TO_P4D call so an earlier consumer's
    /// PtrLen stays valid within a run.
    std::uint32_t &arena_cursor;
};

/// Handle one ahfl_xcode transcode import. The site carries the direction,
/// source kind, boundary ordinals, and the dense P4-D layout root. Fails
/// closed (nonzero status) on any bounds / schema / overflow / OOM violation.
[[nodiscard]] core_wasm_resume_engine::ImportCallbackResult
handle_transcode(const TranscodeConfig &config,
                 const ir::core::CoreFrameTranscodeSite &site,
                 const core_wasm_resume_engine::ImportObservation &obs);

} // namespace ahfl::runtime::wasm_host
