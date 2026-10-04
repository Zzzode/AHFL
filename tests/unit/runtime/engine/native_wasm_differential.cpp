// native_wasm_differential.cpp — RFC 0026 E4-B1 C3 the runtime wire-schema
// module inspector.
//
// SCOPE: a generic host receives transported Wasm MODULE BYTES (not a
// CoreProgram) and mints a verified wire binding from the `ahfl.wire-schema.v1`
// custom section. This file frames a canonical module via a TEST-ONLY builder
// and exercises the inspector positive controls, resource gates, and
// single-gate negatives; the real admission authority is the inspector + the
// C1 decoder + the typed factory.

#include "runtime/engine/core_wasm_schema_transport.hpp"
#include "runtime/value/value.hpp"
#include "runtime/value/value_json.hpp"

#include "ahfl/compiler/ir/core_wire_schema.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <variant>
#include <vector>

namespace {

using namespace ahfl;
using namespace ahfl::runtime;
using namespace ahfl::ir;

int test_count = 0;
int pass_count = 0;

void check(bool condition, const std::string &name) {
    ++test_count;
    if (condition) {
        ++pass_count;
    } else {
        std::cerr << "FAIL: " << name << "\n";
    }
}


// =========================================================================
// RFC 0026 E4-B1 C3: the runtime wire-schema module inspector.
//
// A TEST-ONLY canonical Wasm module builder. It is NOT a production authority or
// helper: it only assembles the byte framing a conforming C2 writer would emit,
// so each negative case can perturb exactly ONE gate at a known offset/section.
// The real admission authority remains the inspector + the C1 decoder + the typed
// factory; this builder never validates anything.
// =========================================================================
namespace c3 {

using ahfl::ir::core::CoreCapabilityId;
using ahfl::ir::core::CoreLowerDiagnostic;
using ahfl::ir::core::CoreWireCapabilitySchema;
using ahfl::ir::core::CoreWireRootKind;
using ahfl::ir::core::CoreWireSchemaInt;
using ahfl::ir::core::CoreWireSchemaNode;
using ahfl::ir::core::CoreWireSchemaNodeId;
using ahfl::ir::core::CoreWireSchemaTable;
using ahfl::runtime::core_wasm_schema::CoreWasmWireBindingResult;
using ahfl::runtime::core_wasm_schema::make_wire_binding_from_core_wasm;

// Append a canonical unsigned LEB128 encoding of `value`.
void put_uleb(std::vector<std::uint8_t> &out, std::uint64_t value) {
    do {
        auto byte = static_cast<std::uint8_t>(value & 0x7fu);
        value >>= 7u;
        if (value != 0) {
            byte |= 0x80u;
        }
        out.push_back(byte);
    } while (value != 0);
}

// Append a section = id + canonical size + payload.
void put_section(std::vector<std::uint8_t> &out, std::uint8_t id,
                 const std::vector<std::uint8_t> &payload) {
    out.push_back(id);
    put_uleb(out, payload.size());
    out.insert(out.end(), payload.begin(), payload.end());
}

// One func type entry: form 0x60 + params + results (value-type byte arrays).
std::vector<std::uint8_t> func_type(const std::vector<std::uint8_t> &params,
                                    const std::vector<std::uint8_t> &results) {
    std::vector<std::uint8_t> out;
    out.push_back(0x60);
    put_uleb(out, params.size());
    out.insert(out.end(), params.begin(), params.end());
    put_uleb(out, results.size());
    out.insert(out.end(), results.begin(), results.end());
    return out;
}

// The canonical ahfl_cap tuple signature (i32,i32)->(i32,i32,i32).
std::vector<std::uint8_t> capability_tuple_type() {
    return func_type({0x7f, 0x7f}, {0x7f, 0x7f, 0x7f});
}

// WH-2: the bridge ahfl_cap signature (i32)->(i32,i32). The transport admission
// accepts this alongside the opaque 3-result tuple (decision doc section 11.3).
std::vector<std::uint8_t> bridge_signature_type() {
    return func_type({0x7f}, {0x7f, 0x7f});
}

// Encode a real wire-schema table to its canonical payload via the C1 encoder, so
// the wrapped module carries genuine `AHFLWS...` bytes (no hand-rolled table).
std::vector<std::uint8_t> encode_table(const CoreWireSchemaTable &table) {
    auto encoded = ahfl::ir::core::encode_core_wire_schema_table(table);
    if (!encoded.ok() || !encoded.bytes.has_value()) {
        return {};
    }
    return *encoded.bytes;
}

// A one-capability table whose Result root is a bare Int, source_symbol = 42.
CoreWireSchemaTable single_int_table(std::uint64_t source_symbol = 42) {
    CoreWireSchemaTable table;
    table.nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}});
    CoreWireCapabilitySchema cap;
    cap.capability = CoreCapabilityId{0};
    cap.source_symbol = source_symbol;
    cap.result = CoreWireSchemaNodeId{0};
    table.capabilities.push_back(cap);
    return table;
}

// Knobs for the canonical module builder. Each negative test flips exactly one.
struct ModuleSpec {
    // Type section: the func-type entries, and whether to emit the section at all.
    std::vector<std::vector<std::uint8_t>> types = {capability_tuple_type()};
    bool emit_type = true;
    bool duplicate_type = false;
    // Import section: (source_symbol, typeidx) per ahfl_cap import; emit toggle.
    std::vector<std::pair<std::uint64_t, std::uint32_t>> imports = {{42, 0}};
    bool emit_import = true;
    bool duplicate_import = false;
    bool type_after_import = false; // emit Import before Type (order violation)
    // The wire-schema table encoded into the target custom section (empty = none).
    std::optional<std::vector<std::uint8_t>> target_table = std::nullopt;
    std::string target_name = "ahfl.wire-schema.v1";
    bool duplicate_target = false;
    bool section_after_target = false; // append a nonzero section AFTER the target
    // Unknown non-target custom sections emitted BEFORE the target.
    std::vector<std::string> unknown_customs_before_target;
    // An extra bounded nonzero standard section the writer never emits (DataCount,
    // id 12) to prove the reader generically size-skips it.
    bool emit_datacount_skip = false;
};

std::vector<std::uint8_t> import_payload(
    const std::vector<std::pair<std::uint64_t, std::uint32_t>> &imports) {
    std::vector<std::uint8_t> payload;
    put_uleb(payload, imports.size());
    for (const auto &[symbol, typeidx] : imports) {
        const std::string module_name = "ahfl_cap";
        const std::string field_name = "cap_" + std::to_string(symbol);
        put_uleb(payload, module_name.size());
        payload.insert(payload.end(), module_name.begin(), module_name.end());
        put_uleb(payload, field_name.size());
        payload.insert(payload.end(), field_name.begin(), field_name.end());
        payload.push_back(0x00); // kind = function
        put_uleb(payload, typeidx);
    }
    return payload;
}

std::vector<std::uint8_t> type_payload(const std::vector<std::vector<std::uint8_t>> &types) {
    std::vector<std::uint8_t> payload;
    put_uleb(payload, types.size());
    for (const auto &entry : types) {
        payload.insert(payload.end(), entry.begin(), entry.end());
    }
    return payload;
}

std::vector<std::uint8_t> custom_payload(const std::string &name,
                                         const std::vector<std::uint8_t> &body) {
    std::vector<std::uint8_t> payload;
    put_uleb(payload, name.size());
    payload.insert(payload.end(), name.begin(), name.end());
    payload.insert(payload.end(), body.begin(), body.end());
    return payload;
}

// Assemble a canonical module per spec. Section ids: Type=1, Import=2,
// DataCount=12, Custom=0.
std::vector<std::uint8_t> build_module(const ModuleSpec &spec) {
    std::vector<std::uint8_t> module{0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};

    const auto emit_type = [&] {
        if (spec.emit_type) {
            put_section(module, 1, type_payload(spec.types));
            if (spec.duplicate_type) {
                put_section(module, 1, type_payload(spec.types));
            }
        }
    };
    const auto emit_import = [&] {
        if (spec.emit_import) {
            put_section(module, 2, import_payload(spec.imports));
            if (spec.duplicate_import) {
                put_section(module, 2, import_payload(spec.imports));
            }
        }
    };

    if (spec.type_after_import) {
        emit_import();
        emit_type();
    } else {
        emit_type();
        emit_import();
    }

    if (spec.emit_datacount_skip) {
        // A DataCount section (id 12) the C2 writer never emits: one canonical u32
        // body. The reader must size-skip it without interpreting it.
        std::vector<std::uint8_t> datacount;
        put_uleb(datacount, 0);
        put_section(module, 12, datacount);
    }

    for (const auto &name : spec.unknown_customs_before_target) {
        put_section(module, 0, custom_payload(name, {0x01, 0x02, 0x03}));
    }

    if (spec.target_table.has_value()) {
        put_section(module, 0, custom_payload(spec.target_name, *spec.target_table));
        if (spec.duplicate_target) {
            put_section(module, 0, custom_payload(spec.target_name, *spec.target_table));
        }
        if (spec.section_after_target) {
            std::vector<std::uint8_t> datacount;
            put_uleb(datacount, 0);
            put_section(module, 12, datacount);
        }
    }
    return module;
}

// A conforming module: one ahfl_cap import (symbol 42, typeidx 0 = tuple) + the
// single-Int table in the target custom section at EOF.
std::vector<std::uint8_t> conforming_module() {
    ModuleSpec spec;
    spec.target_table = encode_table(single_int_table());
    return build_module(spec);
}

[[nodiscard]] bool has_error_diag(const std::vector<CoreLowerDiagnostic> &bag) {
    for (const auto &d : bag) {
        if (d.severity == ahfl::ir::core::CoreDiagnosticSeverity::Error) {
            return true;
        }
    }
    return false;
}

// Generic negative: no binding + at least one Error diagnostic.
[[nodiscard]] bool clean_failure(const CoreWasmWireBindingResult &result) {
    return !result.binding.has_value() && !result.ok() && has_error_diag(result.diagnostics);
}

// Targeted negative: a clean failure whose diagnostic bag contains `fragment`,
// so each case is proven to trip the specific gate it targets (and cannot be a
// later gate's generic failure).
[[nodiscard]] bool
fails_with(const CoreWasmWireBindingResult &result, std::string_view fragment) {
    if (!clean_failure(result)) {
        return false;
    }
    for (const auto &d : result.diagnostics) {
        if (d.message.find(fragment) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// A distinctive marker that a genuine no-echo case seeds into the module so the
// full-bag no-echo assertion is not vacuous.
constexpr std::string_view kSecretMarker = "C3_SECRET_MARKER_ZZZ";

[[nodiscard]] bool bag_has_no_marker(const CoreWasmWireBindingResult &result) {
    for (const auto &d : result.diagnostics) {
        if (d.message.find(kSecretMarker) != std::string::npos) {
            return false;
        }
    }
    return true;
}

void test_wire_schema_module_inspector() {
    // --- positive: a conforming module mints the Result binding once. ---------
    {
        const auto module = conforming_module();
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(result.ok() && result.binding.has_value(),
              "c3.positive.conforming_module_mints_result_binding");
        if (result.binding.has_value()) {
            const auto &selector = result.binding->selector();
            check(selector.capability == CoreCapabilityId{0} &&
                      selector.expected_source_symbol == 42 &&
                      selector.kind == CoreWireRootKind::Result && selector.param_index == 0,
                  "c3.positive.selector_identity_from_table");
        }
    }

    // --- positive control: an UNREFERENCED type entry whose param/result bytes
    // are arbitrary/invalid value-type bytes still frames fine (framing-only, no
    // Wasm type validation); only the cap-import-referenced entry must be the
    // exact tuple. typeidx 0 (tuple) is referenced; typeidx 1 carries garbage
    // value-type bytes and is never referenced. ------------------------------
    {
        ModuleSpec spec;
        spec.types = {capability_tuple_type(),
                      func_type({0x01 /* not a valid value type */}, {0xfe})};
        spec.imports = {{42, 0}};
        spec.target_table = encode_table(single_int_table());
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(result.ok(),
              "c3.positive.unreferenced_type_arbitrary_value_bytes_ok");
    }

    // --- positive control: the tuple signature at a NON-zero index (index 1);
    // the reader accepts any in-range typeidx whose signature is the exact tuple,
    // never hardcoding an index. ---------------------------------------------
    {
        ModuleSpec spec;
        spec.types = {func_type({0x7f}, {0x7f}) /* i32->i32 filler */,
                      capability_tuple_type() /* index 1 = tuple */};
        spec.imports = {{42, 1}};
        spec.target_table = encode_table(single_int_table());
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(result.ok(), "c3.positive.tuple_signature_at_nonzero_typeidx");
    }

    // --- positive control: unknown non-target customs before the target + a
    // DataCount (id 12) section the writer never emits are both size-skipped. ---
    {
        ModuleSpec spec;
        spec.unknown_customs_before_target = {"name", "producers"};
        spec.emit_datacount_skip = true;
        spec.target_table = encode_table(single_int_table());
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(result.ok(),
              "c3.positive.unknown_customs_and_datacount_section_skipped");
    }

    // --- negative: bad module header (magic). --------------------------------
    {
        std::vector<std::uint8_t> module = conforming_module();
        module[0] = 0x01;
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "header is not wasm v1"), "c3.negative.bad_magic");
    }

    // --- negative: bad module version (byte 4). ------------------------------
    {
        std::vector<std::uint8_t> module = conforming_module();
        module[4] = 0x02; // version 2
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "header is not wasm v1"), "c3.negative.bad_version");
    }

    // --- negative: section size LEB is non-canonical (overlong). -------------
    {
        std::vector<std::uint8_t> module{0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
        module.push_back(1);            // Type section id
        module.push_back(0x81);         // overlong size LEB for value 1
        module.push_back(0x00);
        module.push_back(0x00);         // 1 byte of "payload"
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "section size is not a canonical u32"),
              "c3.negative.section_size_noncanonical");
    }

    // --- negative: section size overflows the 32-bit domain. -----------------
    {
        std::vector<std::uint8_t> module{0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
        module.push_back(1); // Type section id
        // Five 0xFF groups + 0x0F would exceed 32 bits; use 0xFF*5 (>32-bit shift).
        module.insert(module.end(), {0xff, 0xff, 0xff, 0xff, 0xff, 0x0f});
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "section size is not a canonical u32"),
              "c3.negative.section_size_overflow");
    }

    // --- negative: section size exceeds remaining bytes. ---------------------
    {
        std::vector<std::uint8_t> module{0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
        module.push_back(1);   // Type section id
        put_uleb(module, 100); // claims 100 payload bytes
        module.push_back(0x00);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "section exceeds its bounds"),
              "c3.negative.section_size_exceeds_remaining");
    }

    // --- negative: a stray byte after the target trips the single target-not-EOF
    // gate (checked before the next section header is read). ------------------
    {
        std::vector<std::uint8_t> module = conforming_module();
        module.push_back(1); // any trailing byte after the target section
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "a section follows the wire-schema custom section"),
              "c3.negative.trailing_after_target");
    }

    // --- negative: a section-size LEB that is truncated (id + 0x80 + EOF) in a
    // module with NO target yet, so the canonical-u32 size gate is what trips. --
    {
        std::vector<std::uint8_t> module{0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
        module.push_back(1);    // Type section id
        module.push_back(0x80); // size LEB continuation bit set, then EOF
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "section size is not a canonical u32"),
              "c3.negative.section_size_truncated_leb");
    }

    // --- NEGATIVE control for typeidx authority: entries 0..3 fillers, index 4
    // is a WRONG tuple (result arity 1), and the import references typeidx 4. The
    // failure is the signature gate, not OOR — the true counterpart to the
    // typeidx-1 exact-tuple positive control. --------------------------------
    {
        ModuleSpec spec;
        spec.types = {func_type({0x7f}, {0x7f}), func_type({0x7f}, {0x7f}),
                      func_type({0x7f}, {0x7f}), func_type({0x7f}, {0x7f}),
                      func_type({0x7f, 0x7f}, {0x7f}) /* index 4: wrong result arity */};
        spec.imports = {{42, 4}};
        spec.target_table = encode_table(single_int_table());
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "does not use the ahfl_cap tuple or bridge signature"),
              "c3.negative.typeidx4_wrong_signature");
    }

    // --- WH-2 positive: the bridge (i32)->(i32,i32) functype is accepted
    // alongside the opaque 3-result tuple (decision doc section 11.3). --------
    {
        ModuleSpec spec;
        spec.types = {bridge_signature_type()};
        spec.imports = {{42, 0}};
        spec.target_table = encode_table(single_int_table());
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(result.ok(), "c3.positive.bridge_functype_accepted");
    }

    // --- WH-2 negative: a functype that is NEITHER the opaque tuple NOR the
    // bridge signature (e.g. (i32)->(i32), one result) is rejected. ----------
    {
        ModuleSpec spec;
        spec.types = {func_type({0x7f}, {0x7f}) /* i32->i32 */};
        spec.imports = {{42, 0}};
        spec.target_table = encode_table(single_int_table());
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result,
                         "does not use the ahfl_cap tuple or bridge signature"),
              "c3.negative.neither_tuple_nor_bridge_rejected");
    }

    // --- negative: non-func type form (0x50 instead of 0x60). ----------------
    {
        ModuleSpec spec;
        spec.types = {std::vector<std::uint8_t>{0x50, 0x00, 0x00}};
        spec.target_table = encode_table(single_int_table());
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "Type section is malformed"),
              "c3.negative.non_func_type_form");
    }

    // --- resource: Type count = UINT32_MAX with a short payload. Must fail with
    // the Type-malformed gate (count > remaining/min-entry) WITHOUT bad_alloc. --
    {
        std::vector<std::uint8_t> module{0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
        std::vector<std::uint8_t> type_body;
        put_uleb(type_body, UINT32_MAX);
        type_body.push_back(0x60);
        put_section(module, 1, type_body);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "Type section is malformed"),
              "c3.resource.type_count_uint32max_short_payload");
    }

    // --- resource: Import count = UINT32_MAX with a short payload. -------------
    {
        std::vector<std::uint8_t> module{0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
        put_section(module, 1, type_payload({capability_tuple_type()}));
        std::vector<std::uint8_t> import_body;
        put_uleb(import_body, UINT32_MAX);
        import_body.push_back(0x01);
        put_section(module, 2, import_body);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "Import section is malformed"),
              "c3.resource.import_count_uint32max_short_payload");
    }

    // --- negative: custom name length non-canonical (overlong LEB). ----------
    {
        std::vector<std::uint8_t> module{0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
        put_section(module, 1, type_payload({capability_tuple_type()}));
        put_section(module, 2, import_payload({{42, 0}}));
        put_section(module, 0, std::vector<std::uint8_t>{0x81, 0x00, 'a'});
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "custom section name length is not a canonical u32"),
              "c3.negative.custom_name_length_noncanonical");
    }

    // --- negative: custom name length exceeds its section bounds. -------------
    {
        std::vector<std::uint8_t> module{0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
        put_section(module, 1, type_payload({capability_tuple_type()}));
        put_section(module, 2, import_payload({{42, 0}}));
        std::vector<std::uint8_t> body;
        put_uleb(body, 100);
        body.push_back('x');
        put_section(module, 0, body);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "custom section name exceeds its section bounds"),
              "c3.negative.custom_name_length_out_of_bounds");
    }

    // --- negative: a section follows the target (target not at EOF). ----------
    {
        ModuleSpec spec;
        spec.target_table = encode_table(single_int_table());
        spec.section_after_target = true;
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "a section follows the wire-schema custom section"),
              "c3.negative.section_after_target");
    }

    // --- negative: duplicate target custom sections (second trips target-not-EOF).
    {
        ModuleSpec spec;
        spec.target_table = encode_table(single_int_table());
        spec.duplicate_target = true;
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "a section follows the wire-schema custom section"),
              "c3.negative.duplicate_target");
    }

    // --- negative: no target custom section at all. ---------------------------
    {
        ModuleSpec spec;
        spec.target_table = std::nullopt;
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "has no wire-schema custom section"),
              "c3.negative.missing_target");
    }

    // --- negative: target with the WRONG name is treated as a non-target, so the
    // module has no target -> missing-target failure. ------------------------
    {
        ModuleSpec spec;
        spec.target_name = "ahfl.wire-schema.v2"; // not the accepted name
        spec.target_table = encode_table(single_int_table());
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "has no wire-schema custom section"),
              "c3.negative.target_wrong_name");
    }

    // --- negative: missing Type section. -------------------------------------
    {
        ModuleSpec spec;
        spec.emit_type = false;
        spec.target_table = encode_table(single_int_table());
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "has no Type section"),
              "c3.negative.missing_type_section");
    }

    // --- negative: duplicate Type section. -----------------------------------
    {
        ModuleSpec spec;
        spec.duplicate_type = true;
        spec.target_table = encode_table(single_int_table());
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "more than one Type section"),
              "c3.negative.duplicate_type_section");
    }

    // --- negative: Type section after Import (order violation). ---------------
    {
        ModuleSpec spec;
        spec.type_after_import = true;
        spec.target_table = encode_table(single_int_table());
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "Type section follows its Import section"),
              "c3.negative.type_after_import");
    }

    // --- negative: missing Import section (with a target present). ------------
    {
        ModuleSpec spec;
        spec.emit_import = false;
        spec.target_table = encode_table(single_int_table());
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "has no capability Import section"),
              "c3.negative.missing_import_section");
    }

    // --- negative: duplicate Import section. ----------------------------------
    {
        ModuleSpec spec;
        spec.duplicate_import = true;
        spec.target_table = encode_table(single_int_table());
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "more than one Import section"),
              "c3.negative.duplicate_import_section");
    }

    // --- negative: an import whose module name is not ahfl_cap. ---------------
    {
        std::vector<std::uint8_t> module{0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
        put_section(module, 1, type_payload({capability_tuple_type()}));
        std::vector<std::uint8_t> import_body;
        put_uleb(import_body, 1);
        const std::string module_name = "wasi_snapshot"; // not ahfl_cap
        const std::string field_name = "cap_42";
        put_uleb(import_body, module_name.size());
        import_body.insert(import_body.end(), module_name.begin(), module_name.end());
        put_uleb(import_body, field_name.size());
        import_body.insert(import_body.end(), field_name.begin(), field_name.end());
        import_body.push_back(0x00);
        put_uleb(import_body, 0);
        put_section(module, 2, import_body);
        put_section(module, 0, custom_payload("ahfl.wire-schema.v1",
                                              encode_table(single_int_table(42))));
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "Import section is malformed"),
              "c3.negative.non_ahfl_cap_import");
    }

    // --- negative: an import with a non-function kind (global, 0x03). ---------
    {
        std::vector<std::uint8_t> module{0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
        put_section(module, 1, type_payload({capability_tuple_type()}));
        std::vector<std::uint8_t> import_body;
        put_uleb(import_body, 1);
        const std::string module_name = "ahfl_cap";
        const std::string field_name = "cap_42";
        put_uleb(import_body, module_name.size());
        import_body.insert(import_body.end(), module_name.begin(), module_name.end());
        put_uleb(import_body, field_name.size());
        import_body.insert(import_body.end(), field_name.begin(), field_name.end());
        import_body.push_back(0x03); // global import kind
        put_uleb(import_body, 0);
        put_section(module, 2, import_body);
        put_section(module, 0, custom_payload("ahfl.wire-schema.v1",
                                              encode_table(single_int_table(42))));
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "Import section is malformed"),
              "c3.negative.import_wrong_kind");
    }

    // --- negative: no capability imports but a target present (explicit gate). --
    {
        ModuleSpec spec;
        spec.imports = {};
        spec.target_table = encode_table(single_int_table());
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "wire-schema section but no capability imports"),
              "c3.negative.no_cap_import_with_target");
    }

    // --- negative: nonempty imports but a locally-valid EMPTY-capability table.
    // This is the SECOND, non-XOR branch of the no-cap gate: the import table is
    // present but the decoded schema table carries no capability. ------------
    {
        CoreWireSchemaTable empty_table; // 0 nodes, 0 capabilities: locally valid
        ModuleSpec spec;
        spec.imports = {{42, 0}};
        spec.target_table = encode_table(empty_table);
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "table carries no capability"),
              "c3.negative.table_empty_capabilities_with_import");
    }

    // --- negative: import count mismatches the table (two imports, one-cap table).
    {
        ModuleSpec spec;
        spec.imports = {{42, 0}, {43, 0}};
        spec.target_table = encode_table(single_int_table(42));
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "import count does not match the wire-schema table"),
              "c3.negative.import_count_mismatch");
    }

    // --- negative: import source_symbol mismatches the table (count matches). --
    {
        ModuleSpec spec;
        spec.imports = {{999, 0}};
        spec.target_table = encode_table(single_int_table(42));
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "import source symbol does not match the wire-schema table"),
              "c3.negative.import_source_symbol_mismatch");
    }

    // --- negative: import references a typeidx that is out of range. ----------
    {
        ModuleSpec spec;
        spec.imports = {{42, 5}};
        spec.target_table = encode_table(single_int_table());
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "does not use the ahfl_cap tuple or bridge signature"),
              "c3.negative.import_typeidx_out_of_range");
    }

    // --- negative: non-canonical cap_ decimal (leading zero). ----------------
    {
        std::vector<std::uint8_t> module{0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
        put_section(module, 1, type_payload({capability_tuple_type()}));
        std::vector<std::uint8_t> import_body;
        put_uleb(import_body, 1);
        const std::string module_name = "ahfl_cap";
        const std::string field_name = "cap_042"; // leading zero
        put_uleb(import_body, module_name.size());
        import_body.insert(import_body.end(), module_name.begin(), module_name.end());
        put_uleb(import_body, field_name.size());
        import_body.insert(import_body.end(), field_name.begin(), field_name.end());
        import_body.push_back(0x00);
        put_uleb(import_body, 0);
        put_section(module, 2, import_body);
        put_section(module, 0, custom_payload("ahfl.wire-schema.v1",
                                              encode_table(single_int_table(42))));
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "Import section is malformed"),
              "c3.negative.cap_field_leading_zero");
    }

    // --- uint64 cap-field boundary contract: 0 and UINT64_MAX are canonical and
    // mint; a value one past UINT64_MAX is rejected by the overflow branch. -----
    {
        for (const std::uint64_t symbol : {std::uint64_t{0}, UINT64_MAX}) {
            ModuleSpec spec;
            spec.imports = {{symbol, 0}};
            spec.target_table = encode_table(single_int_table(symbol));
            const auto module = build_module(spec);
            const auto result = make_wire_binding_from_core_wasm(
                module, 0, CoreWireRootKind::Result, 0);
            check(result.ok() && result.binding.has_value() &&
                      result.binding->selector().expected_source_symbol == symbol,
                  symbol == 0 ? "c3.positive.cap_field_zero_mints"
                              : "c3.positive.cap_field_uint64max_mints");
        }
    }
    {
        // Raw field cap_18446744073709551616 = UINT64_MAX + 1: the decimal parse
        // overflow branch rejects it; the rest of the module/table is valid.
        std::vector<std::uint8_t> module{0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
        put_section(module, 1, type_payload({capability_tuple_type()}));
        std::vector<std::uint8_t> import_body;
        put_uleb(import_body, 1);
        const std::string module_name = "ahfl_cap";
        const std::string field_name = "cap_18446744073709551616"; // 2^64
        put_uleb(import_body, module_name.size());
        import_body.insert(import_body.end(), module_name.begin(), module_name.end());
        put_uleb(import_body, field_name.size());
        import_body.insert(import_body.end(), field_name.begin(), field_name.end());
        import_body.push_back(0x00);
        put_uleb(import_body, 0);
        put_section(module, 2, import_body);
        put_section(module, 0, custom_payload("ahfl.wire-schema.v1",
                                              encode_table(single_int_table(42))));
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "Import section is malformed"),
              "c3.negative.cap_field_uint64_overflow");
    }

    // --- two-cap: sparse ids + swapped source symbols. Request ordinal 0, which
    // MATCHES at ordinal 0, but ordinal 1 disagrees, proving the FULL table is
    // cross-checked before the requested ordinal is honored. ------------------
    {
        CoreWireSchemaTable table;
        table.nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}});
        CoreWireCapabilitySchema a;
        a.capability = CoreCapabilityId{3};
        a.source_symbol = 100;
        a.result = CoreWireSchemaNodeId{0};
        CoreWireCapabilitySchema b;
        b.capability = CoreCapabilityId{7};
        b.source_symbol = 200;
        b.result = CoreWireSchemaNodeId{0};
        table.capabilities.push_back(a);
        table.capabilities.push_back(b);

        ModuleSpec spec;
        spec.types = {capability_tuple_type()};
        spec.imports = {{100, 0}, {999, 0}}; // ordinal 1 SWAPPED (999 != 200)
        spec.target_table = encode_table(table);
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(fails_with(result, "import source symbol does not match the wire-schema table"),
              "c3.negative.two_cap_swapped_ordinal1_fails_requesting_ordinal0");

        ModuleSpec ok_spec;
        ok_spec.types = {capability_tuple_type()};
        ok_spec.imports = {{100, 0}, {200, 0}};
        ok_spec.target_table = encode_table(table);
        const auto ok_module = build_module(ok_spec);
        const auto ok_result = make_wire_binding_from_core_wasm(
            ok_module, 0, CoreWireRootKind::Result, 0);
        check(ok_result.ok() && ok_result.binding.has_value() &&
                  ok_result.binding->selector().capability == CoreCapabilityId{3} &&
                  ok_result.binding->selector().expected_source_symbol == 100,
              "c3.positive.two_cap_sparse_ids_ordinal0_binds_cap3");
    }

    // --- negative: requested ordinal out of range (single-cap table). ---------
    {
        const auto module = conforming_module();
        const auto result = make_wire_binding_from_core_wasm(
            module, 5, CoreWireRootKind::Result, 0);
        check(fails_with(result, "requested capability import ordinal is out of range"),
              "c3.negative.requested_ordinal_out_of_range");
    }

    // --- negative: Result selector with a nonzero param_index (factory SSOT). --
    {
        const auto module = conforming_module();
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 3);
        check(fails_with(result, "Result selector must carry param_index 0"),
              "c3.negative.result_selector_nonzero_param");
    }

    // --- negative: Param selector with an out-of-range index (no params here). -
    {
        const auto module = conforming_module();
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Param, 0);
        check(fails_with(result, "Param selector index is out of range"),
              "c3.negative.param_selector_out_of_range");
    }

    // --- negative: an invalid root_kind value reaches the factory SSOT and is
    // rejected there (the inspector forwards the caller's kind unchanged). ------
    {
        const auto module = conforming_module();
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, static_cast<CoreWireRootKind>(0xff), 0);
        check(fails_with(result, "selector kind is invalid"),
              "c3.negative.invalid_root_kind");
    }

    // --- C1 tamper attribution: a single flipped byte inside the encoded table
    // payload must surface as a C3 failure (the C1 decoder rejects it). We do NOT
    // re-test C1's whole matrix here — one integration case proves propagation. --
    {
        ModuleSpec spec;
        auto table_bytes = encode_table(single_int_table());
        if (!table_bytes.empty()) {
            table_bytes.back() ^= 0xff;
        }
        spec.target_table = table_bytes;
        const auto module = build_module(spec);
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(clean_failure(result), "c3.negative.c1_table_tamper_attribution");
    }

    // --- no-echo (non-vacuous): a module whose sole ahfl_cap import field carries
    // the secret marker (as a non-canonical cap field) parses through the module
    // name check, fails at the cap-field decode, and must NOT echo the marker into
    // any diagnostic. The marker is genuinely present in the input bytes. -------
    {
        std::vector<std::uint8_t> module{0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
        put_section(module, 1, type_payload({capability_tuple_type()}));
        std::vector<std::uint8_t> import_body;
        put_uleb(import_body, 1);
        const std::string module_name = "ahfl_cap";
        const std::string field_name = "cap_" + std::string(kSecretMarker); // non-numeric
        put_uleb(import_body, module_name.size());
        import_body.insert(import_body.end(), module_name.begin(), module_name.end());
        put_uleb(import_body, field_name.size());
        import_body.insert(import_body.end(), field_name.begin(), field_name.end());
        import_body.push_back(0x00);
        put_uleb(import_body, 0);
        put_section(module, 2, import_body);
        put_section(module, 0, custom_payload("ahfl.wire-schema.v1",
                                              encode_table(single_int_table(42))));
        const auto result = make_wire_binding_from_core_wasm(
            module, 0, CoreWireRootKind::Result, 0);
        check(clean_failure(result) && bag_has_no_marker(result),
              "c3.negative.import_field_marker_no_echo");
    }
}

} // namespace c3

} // namespace

int main() {
    c3::test_wire_schema_module_inspector();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
