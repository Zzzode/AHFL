#pragma once

// RFC 0026 P6-7 rung A: host-side admission of the two P6-frame custom sections
// carried by a value-frame Core-Wasm module:
//   * `ahfl.core-layout.v1` -- the deterministic P4-D layout table, boundary
//     root ids, DISJOINT backing placements, and the payload-arena span;
//   * `ahfl.wire-schema.v1`  -- the logical wire schema whose reachability roots
//     include the agent input/output boundary nominals.
//
// The host never re-derives a physical fact. This unit:
//   1. walks the module bytes structurally (header + size-skipped sections),
//      requiring the core-layout section EXACTLY ONCE, IMMEDIATELY BEFORE the
//      EOF wire-schema section;
//   2. decodes each raw payload through its canonical-re-encode admission codec;
//   3. verifies the layout and the wire schema describe the SAME boundary type
//      at both roots;
//   4. mints the typed input/output frame bindings and reports SHA-256 digests
//      over the module bytes it actually parsed ("digest-authenticated").
//
// Malformed, truncated, duplicated, misordered, or unknown intervening sections
// fail closed with fixed diagnostics that echo NO module bytes, names, addresses
// or decoded payload content -- the same discipline as the existing section
// parsers.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "ahfl/compiler/ir/core_frame_layout.hpp"
#include "ahfl/compiler/ir/core_wire_migration.hpp"
#include "ahfl/compiler/ir/core_wire_schema.hpp"

namespace ahfl::runtime::core_wasm_frame_module {

/// A raw 32-byte SHA-256 artifact digest.
using ArtifactDigest = std::array<std::uint8_t, 32>;

/// The admitted P6-frame section pair, returned only after every gate passes.
struct AdmittedFrameSections {
    /// The decoded + locally-verified core-layout section (owned by value).
    ahfl::ir::core::CoreFrameLayoutSection layout;
    /// Typed bindings minted from the verified boundary-root wire schema.
    ahfl::ir::core::VerifiedWireSchemaBinding input_binding;
    ahfl::ir::core::VerifiedWireSchemaBinding output_binding;
    /// SHA-256 over the exact module bytes parsed and the two raw (post-name)
    /// section payloads. A host correlates these against its artifact record so
    /// it never walks a page against a layout it did not authenticate.
    ArtifactDigest module_sha256{};
    ArtifactDigest core_layout_sha256{};
    ArtifactDigest wire_schema_sha256{};
};

struct AdmitFrameResult {
    std::optional<AdmittedFrameSections> sections;
    std::vector<ahfl::ir::core::CoreLowerDiagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept;
    [[nodiscard]] bool ok() const noexcept { return sections.has_value() && !has_errors(); }
};

/// Admit the P6-frame section pair from emitted module bytes. Fails closed on
/// any framing, codec, or consistency violation.
[[nodiscard]] AdmitFrameResult
admit_core_wasm_frame_sections(std::span<const std::uint8_t> module_bytes);

} // namespace ahfl::runtime::core_wasm_frame_module
