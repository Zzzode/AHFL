#pragma once

// RFC 0026 KR6.8 WH-2: the thin P6-frame host driver. Composes the WH-1 wasm3
// engine with the WH-2 packer + reader to execute a P6-frame module in-process:
//
//   fresh_instance -> mutable_whole_memory -> pack_p6_input -> invoke_runv
//   -> status/trap check -> read_whole_memory -> encode_p6_output
//
// The driver is intentionally thin: it owns no layout logic, no region policy,
// no JSON rendering. Its only job is the lifecycle glue that lets a test (and,
// in WH-3+, the production runtime) drive a real P6-frame module through the
// packer/reader pair and observe the canonical output JSON. The packer writes
// the input frame directly into the engine's live mutable memory (exactly as
// the JS oracle writes e.memory.buffer), preserving the rodata region the
// module's Data section initialized.

#include "runtime/wasm_host/frame_packer.hpp"
#include "runtime/wasm_host/frame_reader.hpp"
#include "runtime/wasm_host/wasm3_engine.hpp"
#include "runtime/value/value.hpp"

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <variant>

#include "ahfl/compiler/ir/core_frame_layout.hpp"
#include "ahfl/compiler/ir/core_wire_migration.hpp"

namespace ahfl::runtime::wasm_host {

/// A runv execution failure, distinct from a frame pack/read failure. The
/// driver classifies the RunvOutcome here so callers can distinguish a module
/// trap, a host abort (the host's ImportAbort unwound runv), and a non-OK
/// module status (which carries the raw status word for diagnostics).
struct RunvError {
    enum class Kind { Trapped, HostAborted, NonOkStatus };
    Kind kind{Kind::Trapped};
    /// The raw status word; meaningful only when kind == NonOkStatus.
    std::uint32_t raw_status{0};

    [[nodiscard]] friend bool operator==(const RunvError &,
                                         const RunvError &) noexcept = default;
};

/// The union of every typed error the P6-frame driver can fail with. The
/// caller visits to classify; no error carries a diagnostic string.
using P6FrameError =
    std::variant<FramePackError, FrameReadError, RunvError,
                 core_wasm_resume_engine::EngineError>;

/// Execute a P6-frame module in-process and return its canonical output JSON.
///
/// `engine` must be a fresh (not-yet-instantiated) Wasm3ResumeEngine. The
/// driver calls fresh_instance exactly once (a second call on the same engine
/// is InvalidSequence). `section` + `input_binding` + `output_binding` are the
/// admitted authorities from `admit_core_wasm_frame_sections`. `final_kind`
/// selects the runv root authorization (identity -> input base, computed ->
/// output base); the caller sources it from the codegen descriptor's frame
/// lane (decision doc section 11.2). `import_callback` is delivered
/// synchronously for every ahfl_cap import the module invokes during runv
/// (empty for import-free P6-frame modules).
[[nodiscard]] std::expected<std::string, P6FrameError>
execute_p6_frame(Wasm3ResumeEngine &engine,
                 std::span<const std::uint8_t> module_bytes,
                 const ahfl::ir::core::CoreFrameLayoutSection &section,
                 const ahfl::ir::core::VerifiedWireSchemaBinding &input_binding,
                 const ahfl::ir::core::VerifiedWireSchemaBinding &output_binding,
                 const Value &input, P6FinalKind final_kind,
                 core_wasm_resume_engine::ImportCallback import_callback);

} // namespace ahfl::runtime::wasm_host
