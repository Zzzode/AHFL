#pragma once

// RFC 0026 KR6.8 WH-1: SHARED hand-built EXECUTABLE wasm module fixtures for
// the wasm3 engine tests. Unlike resume_test_support.hpp's admission-only
// modules (Type/Import/Memory + custom sections, NO code), these carry real
// function bodies the wasm3 interpreter executes: the identity run_workflow/alloc lane,
// the capability-import lanes (opaque 2-param and section-9 1-param), and the
// trap / out-of-bounds fault shapes.
//
// Everything here is TEST-SUPPORT evidence infrastructure, never production
// code. The bytes are emitter-free hand-built wasm (the same canonical
// discipline as resume_test_support.hpp), so the expected behaviour stays
// auditable side by side with the bytes and the test has no wat2wasm / fixture
// dependency.

#include "unit/runtime/engine/resume_test_support.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ahfl::runtime::wasm_host_test_support {

namespace rts = ahfl::runtime::resume_test_support;

using rts::custom_payload;
using rts::encode_schema;
using rts::exec_manifest_body;
using rts::func_type;
using rts::put_section;
using rts::put_uleb;
using rts::schema_table;
using rts::CapSpec;
using rts::ManifestNodeSpec;

using ahfl::ir::core::CoreWireSchemaNodeId;

// The fixed single-page geometry (F1 SSOT). The bump allocator's heap starts
// at the 0-node heap base: align_up(1024 + 8 + 0*40, 8) == 1032.
constexpr std::uint32_t kPageSize = 65536;
constexpr std::uint32_t kHeapBase = 1032;

// wasm opcodes used by the hand-built bodies.
constexpr std::uint8_t kOpUnreachable = 0x00;
constexpr std::uint8_t kOpI32Const = 0x41;
constexpr std::uint8_t kOpI32Load = 0x28;
constexpr std::uint8_t kOpI32Add = 0x6a;
constexpr std::uint8_t kOpI32GtU = 0x4a;
constexpr std::uint8_t kOpLocalGet = 0x20;
constexpr std::uint8_t kOpLocalSet = 0x21;
constexpr std::uint8_t kOpGlobalGet = 0x23;
constexpr std::uint8_t kOpGlobalSet = 0x24;
constexpr std::uint8_t kOpCall = 0x10;
constexpr std::uint8_t kOpDrop = 0x1a;
constexpr std::uint8_t kOpMemoryGrow = 0x40;
constexpr std::uint8_t kOpIf = 0x04;
constexpr std::uint8_t kOpElse = 0x05;
constexpr std::uint8_t kOpEnd = 0x0b;
constexpr std::uint8_t kOpReturn = 0x0f;
constexpr std::uint8_t kOpI32Eqz = 0x45;
constexpr std::uint8_t kOpI32Eq = 0x46;

// The Wasm void block type (0x40): an if/block/loop that produces no values.
constexpr std::uint8_t kBlockTypeVoid = 0x40;

constexpr std::uint8_t kI32 = 0x7f;
constexpr std::uint8_t kExportKindFunc = 0x00;
constexpr std::uint8_t kExportKindMemory = 0x02;
constexpr std::uint8_t kExportKindGlobal = 0x03;

// Signed LEB128 (i32.const operands). The unsigned put_uleb comes from
// resume_test_support.hpp.
inline void put_sleb(std::vector<std::uint8_t> &out, std::int64_t value) {
    bool more = true;
    while (more) {
        auto byte = static_cast<std::uint8_t>(value & 0x7f);
        value >>= 7;
        if ((value == 0 && (byte & 0x40) == 0) ||
            (value == -1 && (byte & 0x40) != 0)) {
            more = false;
        } else {
            byte |= 0x80;
        }
        out.push_back(byte);
    }
}

inline void put_op(std::vector<std::uint8_t> &out, std::uint8_t opcode) {
    out.push_back(opcode);
}
inline void put_op_uleb(std::vector<std::uint8_t> &out, std::uint8_t opcode,
                        std::uint32_t operand) {
    out.push_back(opcode);
    put_uleb(out, operand);
}
inline void put_i32_const(std::vector<std::uint8_t> &out, std::int32_t value) {
    out.push_back(kOpI32Const);
    put_sleb(out, value);
}

// One code-section entry: body-size LEB + locals decl + instructions.
inline std::vector<std::uint8_t>
code_entry(const std::vector<std::uint8_t> &locals_and_body) {
    std::vector<std::uint8_t> entry;
    put_uleb(entry, locals_and_body.size());
    entry.insert(entry.end(), locals_and_body.begin(), locals_and_body.end());
    return entry;
}

// A locals declaration: `count` groups of (local_count, type).
inline std::vector<std::uint8_t>
locals_decl(const std::vector<std::pair<std::uint32_t, std::uint8_t>> &groups) {
    std::vector<std::uint8_t> decl;
    put_uleb(decl, groups.size());
    for (const auto &[count, type] : groups) {
        put_uleb(decl, count);
        decl.push_back(type);
    }
    return decl;
}

inline void put_export(std::vector<std::uint8_t> &payload, const std::string &name,
                       std::uint8_t kind, std::uint32_t index) {
    put_uleb(payload, name.size());
    payload.insert(payload.end(), name.begin(), name.end());
    payload.push_back(kind);
    put_uleb(payload, index);
}

inline std::vector<std::uint8_t>
function_section(const std::vector<std::uint32_t> &type_indices) {
    std::vector<std::uint8_t> payload;
    put_uleb(payload, type_indices.size());
    for (auto idx : type_indices) {
        put_uleb(payload, idx);
    }
    return payload;
}

inline std::vector<std::uint8_t>
memory_section(std::uint32_t min_pages, bool has_max = false,
               std::uint32_t max_pages = 0) {
    std::vector<std::uint8_t> payload;
    put_uleb(payload, 1); // exactly one memory
    payload.push_back(has_max ? 0x01 : 0x00);
    put_uleb(payload, min_pages);
    if (has_max) {
        put_uleb(payload, max_pages);
    }
    return payload;
}

// One mutable i32 global with a constant initializer (the bump-allocator heap
// pointer).
inline std::vector<std::uint8_t> global_i32_mut(std::int32_t init) {
    std::vector<std::uint8_t> payload;
    put_uleb(payload, 1); // one global
    payload.push_back(kI32);
    payload.push_back(0x01); // mutable
    put_i32_const(payload, init);
    payload.push_back(kOpEnd);
    return payload;
}

// The checked bump allocator every executable fixture exports as `alloc`:
//   (func $alloc (param $len i32) (result i32)
//     (local $old i32)
//     global.get $heap / local.set $old
//     if (local.get $old + local.get $len) > 65536: i32.const 0  ;; OOM
//     else: heap = old+len; local.get $old)
// The OOM arm returns 0 WITHOUT advancing the bump (no partial mutation), the
// same contract the engine maps to MemoryCapacityExceeded.
inline std::vector<std::uint8_t> checked_alloc_body() {
    std::vector<std::uint8_t> body = locals_decl({{1, kI32}}); // $old = local 1
    put_op_uleb(body, kOpGlobalGet, 0);
    put_op_uleb(body, kOpLocalSet, 1); // old = heap
    put_op_uleb(body, kOpLocalGet, 1);
    put_op_uleb(body, kOpLocalGet, 0); // $len
    put_op(body, kOpI32Add);
    put_i32_const(body, static_cast<std::int32_t>(kPageSize));
    put_op(body, kOpI32GtU);
    body.push_back(kOpIf);
    body.push_back(kI32); // if (result i32)
    put_i32_const(body, 0); // OOM -> 0
    body.push_back(kOpElse);
    put_op_uleb(body, kOpLocalGet, 1);
    put_op_uleb(body, kOpLocalGet, 0);
    put_op(body, kOpI32Add);
    put_op_uleb(body, kOpGlobalSet, 0); // heap = old+len
    put_op_uleb(body, kOpLocalGet, 1); // return old
    body.push_back(kOpEnd); // end if
    body.push_back(kOpEnd); // end func
    return body;
}

// The export surface every executable fixture shares: memory, run_workflow, alloc.
// `run_workflow_func_index` / `alloc_func_index` are the defined-function indices
// (imports occupy the low indices). Either export may be suppressed to
// exercise the engine's missing-export rejection.
inline std::vector<std::uint8_t>
standard_exports(std::uint32_t run_workflow_func_index, std::uint32_t alloc_func_index,
                 bool emit_run_workflow = true, bool emit_alloc = true) {
    std::vector<std::uint8_t> payload;
    const std::uint32_t count = 1 + (emit_run_workflow ? 1u : 0u) + (emit_alloc ? 1u : 0u);
    put_uleb(payload, count);
    put_export(payload, "memory", kExportKindMemory, 0);
    if (emit_run_workflow) {
        put_export(payload, "run_workflow", kExportKindFunc, run_workflow_func_index);
    }
    if (emit_alloc) {
        put_export(payload, "alloc", kExportKindFunc, alloc_func_index);
    }
    return payload;
}

// Append the two standard code-section entries (run_workflow body + checked alloc
// body) and emit the code section.
inline void append_standard_code(std::vector<std::uint8_t> &m,
                                 const std::vector<std::uint8_t> &run_workflow_body) {
    std::vector<std::uint8_t> code_payload;
    put_uleb(code_payload, 2);
    auto run_workflow_entry = code_entry(run_workflow_body);
    code_payload.insert(code_payload.end(), run_workflow_entry.begin(), run_workflow_entry.end());
    auto alloc_entry = code_entry(checked_alloc_body());
    code_payload.insert(code_payload.end(), alloc_entry.begin(), alloc_entry.end());
    put_section(m, 10, code_payload);
}

// ==== module builders ====

// The import-free identity module: run_workflow forwards (0, entry_ptr, entry_len) and
// alloc is the checked bump allocator. No imports, so A2 cannot admit it (A2
// requires a capability Import section); the engine applies the F3-equivalent
// fixed-page cross-check itself. `memory_min_pages` and the export-suppression
// flags exist for the negative tests (bad memory / missing export).
[[nodiscard]] inline std::vector<std::uint8_t>
identity_module(std::uint32_t memory_min_pages = 1, bool emit_run_workflow = true,
                bool emit_alloc = true) {
    std::vector<std::uint8_t> m = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
    // type 0: (i32,i32)->(i32,i32,i32); type 1: (i32)->i32
    put_section(m, 1, rts::type_payload({func_type({kI32, kI32}, {kI32, kI32, kI32}),
                                         func_type({kI32}, {kI32})}));
    // no imports
    put_section(m, 3, function_section({0, 1})); // func 0 = run_workflow (type 0), func 1 = alloc (type 1)
    put_section(m, 5, memory_section(memory_min_pages));
    put_section(m, 6, global_i32_mut(static_cast<std::int32_t>(kHeapBase)));
    put_section(m, 7, standard_exports(0, 1, emit_run_workflow, emit_alloc));
    // run_workflow body: i32.const 0; local.get 0; local.get 1; end
    std::vector<std::uint8_t> run_workflow_body = locals_decl({});
    put_i32_const(run_workflow_body, 0);
    put_op_uleb(run_workflow_body, kOpLocalGet, 0);
    put_op_uleb(run_workflow_body, kOpLocalGet, 1);
    run_workflow_body.push_back(kOpEnd);
    append_standard_code(m, run_workflow_body);
    return m;
}

// The capability-import module. The import uses the bridge-lane
// (i32)->(i32,i32) signature (control-block pointer in, status + value_ptr
// out); run_workflow is (i32,i32)->(i32,i32,i32) and forwards the entry pointer to
// the import, returning (status, ptr, 0).
[[nodiscard]] inline std::vector<std::uint8_t>
capability_module(std::uint64_t symbol) {
    std::vector<std::uint8_t> m = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};

    // type 0: (i32)->(i32,i32)            (bridge import)
    // type 1: (i32,i32)->(i32,i32,i32)    (run_workflow)
    // type 2: (i32)->i32                  (alloc)
    put_section(m, 1,
                rts::type_payload({func_type({kI32}, {kI32, kI32}),
                                   func_type({kI32, kI32}, {kI32, kI32, kI32}),
                                   func_type({kI32}, {kI32})}));

    // import: ahfl_cap.cap_<symbol> : func type 0
    put_section(m, 2, rts::import_payload({{symbol, 0}}));

    // defined funcs: run_workflow (type 1) at func index 1, alloc (type 2) at func
    // index 2 (the import is func index 0).
    put_section(m, 3, function_section({1, 2}));
    put_section(m, 5, memory_section(1));
    put_section(m, 6, global_i32_mut(static_cast<std::int32_t>(kHeapBase)));
    put_section(m, 7, standard_exports(1, 2));

    // run_workflow body: forward the entry pointer (local 0) to the bridge import
    // (func 0) as the control-block pointer, then return (status, ptr, 0).
    std::vector<std::uint8_t> run_workflow_body = locals_decl({{2, kI32}}); // local 2=status, 3=ptr
    put_op_uleb(run_workflow_body, kOpLocalGet, 0); // control-block pointer
    put_op_uleb(run_workflow_body, kOpCall, 0);     // bridge import -> (status, ptr)
    put_op_uleb(run_workflow_body, kOpLocalSet, 3); // ptr
    put_op_uleb(run_workflow_body, kOpLocalSet, 2); // status
    put_op_uleb(run_workflow_body, kOpLocalGet, 2); // status
    put_op_uleb(run_workflow_body, kOpLocalGet, 3); // ptr
    put_i32_const(run_workflow_body, 0);            // output_len = 0 (bridge has no len word)
    run_workflow_body.push_back(kOpEnd);
    append_standard_code(m, run_workflow_body);
    return m;
}

// WH-3: the bridge-lane CLASSIFIER module. run_workflow calls the (i32)->(i32,i32)
// bridge import with the entry pointer as the control-block pointer, then
// classifies the first result (the raw ahfl_cap_status): any non-zero status
// traps (unreachable, the bridge lane's guest contract); OK binds the result
// pointer and returns (0, ptr, 0).
[[nodiscard]] inline std::vector<std::uint8_t>
bridge_classifier_module(std::uint64_t symbol) {
    std::vector<std::uint8_t> m = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
    // type 0: (i32)->(i32,i32) (bridge import); type 1: (i32,i32)->(i32,i32,i32) (run_workflow);
    // type 2: (i32)->i32 (alloc)
    put_section(m, 1, rts::type_payload({func_type({kI32}, {kI32, kI32}),
                                         func_type({kI32, kI32}, {kI32, kI32, kI32}),
                                         func_type({kI32}, {kI32})}));
    put_section(m, 2, rts::import_payload({{symbol, 0}})); // ahfl_cap.cap_<symbol> : type 0
    put_section(m, 3, function_section({1, 2})); // func 1 = run_workflow (type 1), func 2 = alloc (type 2)
    put_section(m, 5, memory_section(1));
    put_section(m, 6, global_i32_mut(static_cast<std::int32_t>(kHeapBase)));
    put_section(m, 7, standard_exports(1, 2));

    // run_workflow body: call the bridge import (func 0) with local 0 as the
    // control-block pointer; classify the raw status.
    std::vector<std::uint8_t> run_workflow_body = locals_decl({{2, kI32}}); // local 2=status, 3=ptr
    put_op_uleb(run_workflow_body, kOpLocalGet, 0); // control-block pointer
    put_op_uleb(run_workflow_body, kOpCall, 0);    // bridge import -> (status, ptr)
    // Stack: [status, ptr] (ptr on top). Save to locals.
    put_op_uleb(run_workflow_body, kOpLocalSet, 3); // ptr
    put_op_uleb(run_workflow_body, kOpLocalSet, 2); // status
    // Bridge lane guest contract: any non-zero status traps.
    put_op_uleb(run_workflow_body, kOpLocalGet, 2);
    run_workflow_body.push_back(kOpIf);
    run_workflow_body.push_back(kBlockTypeVoid);
    run_workflow_body.push_back(kOpUnreachable);
    run_workflow_body.push_back(kOpEnd);
    // OK: bind the result pointer, return (0, ptr, 0).
    put_i32_const(run_workflow_body, 0);
    put_op_uleb(run_workflow_body, kOpLocalGet, 3);
    put_i32_const(run_workflow_body, 0);
    run_workflow_body.push_back(kOpEnd); // end func
    append_standard_code(m, run_workflow_body);
    return m;
}

// Append the A2 admission sections (exec-manifest + wire-schema) to an
// already-assembled executable module, so the SAME bytes pass A2 admission and
// the engine. One capability node (cap 0, source_symbol) with an Int result.
[[nodiscard]] inline std::vector<std::uint8_t>
with_a2_admission(std::vector<std::uint8_t> module_bytes, std::uint64_t source_symbol) {
    put_section(module_bytes, 0,
                custom_payload("ahfl.execution-manifest",
                               exec_manifest_body(
                                   7, {{.workflow_node_id = 1,
                                        .cap_call_count = 1,
                                        .capability = 0,
                                        .source_symbol = source_symbol}})));
    put_section(module_bytes, 0,
                custom_payload("ahfl.wire-schema",
                               encode_schema(schema_table(
                                   {}, {CapSpec{.cap_id = 0,
                                                .symbol = source_symbol,
                                                .result = CoreWireSchemaNodeId{0}}}))));
    return module_bytes;
}

// run_workflow executes `unreachable` (a wasm trap, not a host abort).
[[nodiscard]] inline std::vector<std::uint8_t> trap_module() {
    std::vector<std::uint8_t> m = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
    put_section(m, 1, rts::type_payload({func_type({kI32, kI32}, {kI32, kI32, kI32}),
                                         func_type({kI32}, {kI32})}));
    put_section(m, 3, function_section({0, 1}));
    put_section(m, 5, memory_section(1));
    put_section(m, 6, global_i32_mut(static_cast<std::int32_t>(kHeapBase)));
    put_section(m, 7, standard_exports(0, 1));
    std::vector<std::uint8_t> run_workflow_body = locals_decl({});
    run_workflow_body.push_back(kOpUnreachable);
    run_workflow_body.push_back(kOpEnd);
    append_standard_code(m, run_workflow_body);
    return m;
}

// run_workflow performs an out-of-bounds i32.load at 0xFFFF0000 (a wasm trap).
[[nodiscard]] inline std::vector<std::uint8_t> oob_load_module() {
    std::vector<std::uint8_t> m = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
    put_section(m, 1, rts::type_payload({func_type({kI32, kI32}, {kI32, kI32, kI32}),
                                         func_type({kI32}, {kI32})}));
    put_section(m, 3, function_section({0, 1}));
    put_section(m, 5, memory_section(1));
    put_section(m, 6, global_i32_mut(static_cast<std::int32_t>(kHeapBase)));
    put_section(m, 7, standard_exports(0, 1));
    // run_workflow body: i32.const -65536; i32.load align=2 offset=0; drop;
    //            i32.const 0; local.get 0; local.get 1; end
    std::vector<std::uint8_t> run_workflow_body = locals_decl({});
    put_i32_const(run_workflow_body, -65536); // 0xFFFF0000 as u32
    run_workflow_body.push_back(kOpI32Load);
    put_uleb(run_workflow_body, 2); // align = 2 (natural for i32)
    put_uleb(run_workflow_body, 0); // offset = 0
    run_workflow_body.push_back(kOpDrop);
    put_i32_const(run_workflow_body, 0);
    put_op_uleb(run_workflow_body, kOpLocalGet, 0);
    put_op_uleb(run_workflow_body, kOpLocalGet, 1);
    run_workflow_body.push_back(kOpEnd);
    append_standard_code(m, run_workflow_body);
    return m;
}

// A module whose only import is NOT ahfl_cap (wasi_snapshot_preview1): the
// engine's structural import gate must reject it at fresh_instance.
[[nodiscard]] inline std::vector<std::uint8_t> unbound_import_module() {
    std::vector<std::uint8_t> m = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
    put_section(m, 1, rts::type_payload({func_type({kI32, kI32}, {kI32, kI32, kI32}),
                                         func_type({kI32}, {kI32})}));
    // wasi import (not ahfl_cap)
    std::vector<std::uint8_t> import_payload;
    put_uleb(import_payload, 1);
    const std::string mod_name = "wasi_snapshot_preview1";
    put_uleb(import_payload, mod_name.size());
    import_payload.insert(import_payload.end(), mod_name.begin(), mod_name.end());
    const std::string field = "fd_write";
    put_uleb(import_payload, field.size());
    import_payload.insert(import_payload.end(), field.begin(), field.end());
    import_payload.push_back(0x00); // func import
    put_uleb(import_payload, 0);    // type 0
    put_section(m, 2, import_payload);
    put_section(m, 3, function_section({0, 1}));
    put_section(m, 5, memory_section(1));
    put_section(m, 6, global_i32_mut(static_cast<std::int32_t>(kHeapBase)));
    put_section(m, 7, standard_exports(0, 1));
    std::vector<std::uint8_t> run_workflow_body = locals_decl({});
    put_op_uleb(run_workflow_body, kOpLocalGet, 0);
    put_op_uleb(run_workflow_body, kOpLocalGet, 1);
    put_op_uleb(run_workflow_body, kOpCall, 0);
    run_workflow_body.push_back(kOpEnd);
    append_standard_code(m, run_workflow_body);
    return m;
}

// run_workflow executes memory.grow(1) and drops the result, then returns (0,0,0).
// The Memory section is well-formed (min=1, no max) so the module passes the
// structural gate, but the grow breaks the fixed-single-page invariant
// mid-invoke (wasm3 v0.9.0 reallocates the linear memory): the engine must
// fail closed (P2-2 review fix-forward). The grow body opcode sequence:
//   i32.const 1 = 41 01; memory.grow = 40 00 (reserved byte); drop = 1A;
//   three i32.const 0 = 41 00; end = 0B.
[[nodiscard]] inline std::vector<std::uint8_t> memory_grow_module() {
    std::vector<std::uint8_t> m = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
    put_section(m, 1, rts::type_payload({func_type({kI32, kI32}, {kI32, kI32, kI32}),
                                         func_type({kI32}, {kI32})}));
    put_section(m, 3, function_section({0, 1}));
    put_section(m, 5, memory_section(1));
    put_section(m, 6, global_i32_mut(static_cast<std::int32_t>(kHeapBase)));
    put_section(m, 7, standard_exports(0, 1));
    std::vector<std::uint8_t> run_workflow_body = locals_decl({});
    put_i32_const(run_workflow_body, 1);
    run_workflow_body.push_back(kOpMemoryGrow);
    run_workflow_body.push_back(0x00); // reserved immediate (wasm memory.grow encoding)
    run_workflow_body.push_back(kOpDrop);
    put_i32_const(run_workflow_body, 0);
    put_i32_const(run_workflow_body, 0);
    put_i32_const(run_workflow_body, 0);
    run_workflow_body.push_back(kOpEnd);
    append_standard_code(m, run_workflow_body);
    return m;
}

// run_workflow returns a fixed non-zero raw status word (0xDEADBEEF) with the entry
// ptr/len, proving the engine passes the raw u32 status through verbatim with
// NO classification (P2-4 review fix-forward).
[[nodiscard]] inline std::vector<std::uint8_t> nonzero_status_module() {
    std::vector<std::uint8_t> m = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
    put_section(m, 1, rts::type_payload({func_type({kI32, kI32}, {kI32, kI32, kI32}),
                                         func_type({kI32}, {kI32})}));
    put_section(m, 3, function_section({0, 1}));
    put_section(m, 5, memory_section(1));
    put_section(m, 6, global_i32_mut(static_cast<std::int32_t>(kHeapBase)));
    put_section(m, 7, standard_exports(0, 1));
    // run_workflow body: i32.const 0xDEADBEEF; local.get 0; local.get 1; end
    std::vector<std::uint8_t> run_workflow_body = locals_decl({});
    put_i32_const(run_workflow_body, static_cast<std::int32_t>(0xDEADBEEFu));
    put_op_uleb(run_workflow_body, kOpLocalGet, 0);
    put_op_uleb(run_workflow_body, kOpLocalGet, 1);
    run_workflow_body.push_back(kOpEnd);
    append_standard_code(m, run_workflow_body);
    return m;
}

// A module with run_workflow + alloc + run_frame exports and one bridge-lane ahfl_cap
// import that run_frame calls. When the host callback aborts, invoke_run_frame must
// map the sentinel to WorkflowRunHostAborted (distinct from WorkflowRunTrapped). The run_frame
// body calls the (i32)->(i32,i32) bridge import with a null control-block
// pointer and returns the (status, ptr) pair.
[[nodiscard]] inline std::vector<std::uint8_t> run_frame_import_module() {
    std::vector<std::uint8_t> m = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
    // type 0: (i32)->(i32,i32)            (bridge import)
    // type 1: (i32,i32)->(i32,i32,i32)    (run_workflow)
    // type 2: (i32)->i32                  (alloc)
    // type 3: ()->(i32,i32)               (run_frame)
    put_section(m, 1, rts::type_payload({func_type({kI32}, {kI32, kI32}),
                                         func_type({kI32, kI32}, {kI32, kI32, kI32}),
                                         func_type({kI32}, {kI32}),
                                         func_type({}, {kI32, kI32})}));
    // import: ahfl_cap.cap_0 : func type 0 (func index 0)
    put_section(m, 2, rts::import_payload({{0, 0}}));
    // defined funcs: run_workflow (func 1, type 1), alloc (func 2, type 2),
    //               run_frame (func 3, type 3)
    put_section(m, 3, function_section({1, 2, 3}));
    put_section(m, 5, memory_section(1));
    put_section(m, 6, global_i32_mut(static_cast<std::int32_t>(kHeapBase)));
    // exports: memory, run_workflow (func 1), alloc (func 2), run_frame (func 3)
    {
        std::vector<std::uint8_t> payload;
        put_uleb(payload, 4);
        put_export(payload, "memory", kExportKindMemory, 0);
        put_export(payload, "run_workflow", kExportKindFunc, 1);
        put_export(payload, "alloc", kExportKindFunc, 2);
        put_export(payload, "run_frame", kExportKindFunc, 3);
        put_section(m, 7, payload);
    }
    // code section: run_workflow (identity), alloc (checked bump), run_frame (calls import)
    {
        std::vector<std::uint8_t> code_payload;
        put_uleb(code_payload, 3);
        // run_workflow body: i32.const 0; local.get 0; local.get 1; end
        std::vector<std::uint8_t> run_workflow_body = locals_decl({});
        put_i32_const(run_workflow_body, 0);
        put_op_uleb(run_workflow_body, kOpLocalGet, 0);
        put_op_uleb(run_workflow_body, kOpLocalGet, 1);
        run_workflow_body.push_back(kOpEnd);
        auto run_workflow_entry = code_entry(run_workflow_body);
        code_payload.insert(code_payload.end(), run_workflow_entry.begin(), run_workflow_entry.end());
        // alloc body
        auto alloc_entry = code_entry(checked_alloc_body());
        code_payload.insert(code_payload.end(), alloc_entry.begin(), alloc_entry.end());
        // run_frame body: i32.const 0; call 0; end (bridge import, null ctrl ptr)
        std::vector<std::uint8_t> run_frame_body = locals_decl({});
        put_i32_const(run_frame_body, 0);
        put_op_uleb(run_frame_body, kOpCall, 0);
        run_frame_body.push_back(kOpEnd);
        auto run_frame_entry = code_entry(run_frame_body);
        code_payload.insert(code_payload.end(), run_frame_entry.begin(), run_frame_entry.end());
        put_section(m, 10, code_payload);
    }
    return m;
}

} // namespace ahfl::runtime::wasm_host_test_support
