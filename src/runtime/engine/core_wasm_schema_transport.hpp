#pragma once

#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/core_wire_migration.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// RFC 0026 KR6.5 E4-B1 C3: the runtime-side inspector that mints a verified
// wire-schema binding directly from a transported Wasm module's bytes.
//
// A generic embedder receives module bytes, not a `CoreProgram`. The C2 writer
// carries the deterministic logical wire schema for the module's reachable
// capability imports in exactly one `ahfl.wire-schema.v1` custom section fixed at
// module EOF. This inspector is the ONLY runtime entry that turns those raw bytes
// into a `VerifiedWireSchemaBinding` for the codec:
//
//   1. It frames the module (header + section walk) far enough to locate the sole
//      target custom section, and to cross-check the module's `ahfl_cap` import
//      table against the transported schema table. It does NOT re-implement a
//      Wasm validator: only the Type and Import sections are parsed; every other
//      section is skipped by its canonical, bounds-checked size. Full module
//      legality remains `WebAssembly.validate`'s job (the Node/Wasmtime lane).
//   2. It strips the custom section's Wasm name framing and hands the raw table
//      bytes to the C1 decoder (`decode_core_wire_schema_table`), which remains
//      the SOLE payload-admission authority (magic / version / local verify /
//      canonical re-encode). This inspector never duplicates those checks.
//   3. It cross-checks the module import table against `table.capabilities`
//      (count, per-ordinal `source_symbol`, per-ordinal `(i32,i32)->(i32,i32,i32)`
//      signature), then derives a typed `CoreWireRootSelector` from
//      `table.capabilities[ordinal]` and moves the table ONCE into the existing
//      transported-table factory (`make_wire_binding_from_transported_table`),
//      which re-runs the local verifier and derives the root. The caller never
//      supplies a bare `CoreCapabilityId`, `source_symbol`, or `CoreWireSchemaNodeId`.
//
// Each call fully re-parses the module and mints exactly one binding; there is no
// batch, cache, or shared table. On any failure the binding is absent and only
// fixed schema diagnostics are emitted (never the raw bytes, a decoded string, a
// wire name, an import name, or a custom-section name).

namespace ahfl::runtime::core_wasm_schema {

/// The result of inspecting a transported module and minting one binding. Mirrors
/// the C1/migration result shape: an optional binding plus fixed diagnostics.
struct CoreWasmWireBindingResult {
    std::optional<ir::core::VerifiedWireSchemaBinding> binding;
    std::vector<ir::core::CoreLowerDiagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept;
    [[nodiscard]] bool ok() const noexcept { return binding.has_value() && !has_errors(); }
};

/// Inspect a transported Wasm module and mint the verified wire-schema binding for
/// one capability signature slot. `capability_import_ordinal` selects the entry by
/// position in the (cap-id-ascending) import table, which is aligned 1:1 with
/// `table.capabilities`. `root_kind` / `param_index` name the slot: for `Result`,
/// `param_index` must be 0; for `Param`, it indexes that capability's params. All
/// four arguments are explicit (no defaults). Fails closed with no binding on any
/// framing, import, schema, or selector error.
[[nodiscard]] CoreWasmWireBindingResult
make_wire_binding_from_core_wasm(std::span<const std::uint8_t> module_bytes,
                                 std::uint32_t capability_import_ordinal,
                                 ir::core::CoreWireRootKind root_kind,
                                 std::uint32_t param_index);

} // namespace ahfl::runtime::core_wasm_schema
