#pragma once

// RFC 0026 KR6.8 WH-5 P2-6: single source of truth for wasm runtime-lane
// diagnostic codes. The wasm3-backed execution lanes (wasm_host sessions +
// the wasm_runner facade) emit these codes into the WorkflowResult diagnostic
// bag. Before this header every code was a scattered string literal; this
// registry is the ONE place they are defined.
//
// Why a separate registry instead of error_codes in diagnostics.hpp: the
// ErrorCode<Cat> template renders codes as "CATEGORY.ID" (uppercase, dotted
// category), which cannot express the established lowercase-hyphen wasm
// spellings ("wasm.trap", "wasm.host-abort", ...). The codegen lane already
// solved this the same way with core_wasm_diag in core_wasm_codegen.hpp;
// this header is the runtime-lane mirror of that pattern. One SSOT per layer:
// core_wasm_diag for codegen-time failures, wasm_diag for runtime-lane
// failures.

#include <string_view>

namespace ahfl::runtime::wasm_host {

namespace wasm_diag {

// The wasm module trapped (unreachable / out-of-bounds / division by zero /
// ...). Emitted by both the workflow session and the agent runner when the
// wasm3 engine reports a trap during step or resume.
inline constexpr std::string_view kTrap = "wasm.trap";

// The module called the host-abort import (capability failure surfaced as an
// abort, or an explicit abort from generated code).
inline constexpr std::string_view kHostAbort = "wasm.host-abort";

// The run failed for a reason that is neither a trap nor a host abort (e.g.
// a capability call failed without an abort, or a frame contract was
// violated at runtime).
inline constexpr std::string_view kRunFailed = "wasm.run-failed";

// The workflow session itself failed (session-level failure in the facade,
// e.g. the session could not be driven to completion).
inline constexpr std::string_view kSessionFailed = "wasm.session-failed";

// The emitted lifecycle event stream violated the accepted lifecycle
// contract (build_execution_report rejected it). The report stays fail-closed
// Failed; this diagnostic records why.
inline constexpr std::string_view kEventStreamInvalid =
    "wasm.event-stream-invalid";

// Facade: compiling the program to wasm failed in the constructor.
inline constexpr std::string_view kCompileFailed = "wasm.compile-failed";

// Facade: run() was called for a workflow name that is not in the program.
inline constexpr std::string_view kWorkflowNotFound = "wasm.workflow-not-found";

// A successful run produced workflow output bytes that fail to decode
// (WireJson: value_from_json rejects the bytes; P6-frame: read_value_at
// rejects the frame). Evidence of a corrupt module; the session fails
// closed rather than returning Completed with a null output.
inline constexpr std::string_view kOutputDecodeFailed =
    "wasm.output-decode-failed";

} // namespace wasm_diag

} // namespace ahfl::runtime::wasm_host
