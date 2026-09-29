#include "runtime/engine/core_wasm_schema_module.hpp"

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/core_layout.hpp"
#include "ahfl/compiler/ir/core_wasm_abi_constants.hpp"
#include "ahfl/compiler/ir/core_wire_schema.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/validate.hpp"
#include "compiler/backends/wasm/core_wasm_codegen.hpp"
#include "common/project_input_support.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <type_traits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// RFC 0026 KR6.5 E4-B2-A2 permanent regression for the verified Core-Wasm schema
// MODULE context + exec-manifest decoder. Hand-rolled check()/main().
//
// Evidence is split by attribution:
//   * REAL emitter artifact: a genuine `emit_core_wasm` E2 module (no AHFLXM) must
//     FAIL CLOSED (missing exec-manifest).
//   * REAL emitter bytes + SYNTHETIC manifest injection: a test-built canonical
//     AHFLXM injected immediately before the EOF AHFLWS proves A2's framing /
//     table-decode / cross-check / eager-mint are compatible with genuine
//     Type/Import/AHFLWS bytes. This is NOT capability-workflow emitter/topology
//     evidence.
//   * GENUINE B2-C emitter -> A2 (no injection): a real capability-workflow
//     `emit_core_wasm` artifact already carries its AHFLXM exec-manifest, so its
//     bytes are admitted directly and its exact topology / call-site coordinates
//     are asserted -- true compiler->A2 cross-layer topology evidence.
//   * HAND-BUILT canonical two-section fixture: all placement/canonical/set-equality
//     negatives, sparse {3,7} cap ids, SymbolId{0}, repeated capability, and the
//     distinct import-vs-invocation ordinal, exercised without any emitter.
// Node/Wasmtime/native are NOT evidence of B2 real-Wasm durable resume.

namespace {

using namespace ahfl::runtime::core_wasm_schema_module;
using ahfl::ir::core::CoreCapabilityId;
using ahfl::ir::core::CoreWireCapabilitySchema;
using ahfl::ir::core::CoreWireSchemaInt;
using ahfl::ir::core::CoreWireSchemaNode;
using ahfl::ir::core::CoreWireSchemaNodeId;
using ahfl::ir::core::CoreWireSchemaString;
using ahfl::ir::core::CoreWireSchemaTable;
using ahfl::ir::core::CoreWorkflowId;
using ahfl::ir::core::CoreWorkflowNodeId;

int g_failures = 0;

void check(bool ok, std::string_view name) {
    if (!ok) {
        ++g_failures;
        std::cerr << "FAIL: " << name << "\n";
    }
}

// ---- test-only canonical byte builders (NOT a production authority) ----------

void put_uleb(std::vector<std::uint8_t> &out, std::uint64_t value) {
    do {
        auto b = static_cast<std::uint8_t>(value & 0x7fU);
        value >>= 7U;
        if (value != 0) {
            b |= 0x80U;
        }
        out.push_back(b);
    } while (value != 0);
}

void put_section(std::vector<std::uint8_t> &out, std::uint8_t id,
                 const std::vector<std::uint8_t> &payload) {
    out.push_back(id);
    put_uleb(out, payload.size());
    out.insert(out.end(), payload.begin(), payload.end());
}

std::vector<std::uint8_t> func_type(const std::vector<std::uint8_t> &params,
                                    const std::vector<std::uint8_t> &results) {
    std::vector<std::uint8_t> t;
    t.push_back(0x60);
    put_uleb(t, params.size());
    t.insert(t.end(), params.begin(), params.end());
    put_uleb(t, results.size());
    t.insert(t.end(), results.begin(), results.end());
    return t;
}

std::vector<std::uint8_t> capability_tuple() {
    return func_type({0x7f, 0x7f}, {0x7f, 0x7f, 0x7f});
}

// WH-3: the bridge (i32)->(i32,i32) ahfl_cap functype, accepted alongside the
// opaque 3-result tuple (decision doc section 11.3).
std::vector<std::uint8_t> bridge_signature() {
    return func_type({0x7f}, {0x7f, 0x7f});
}

std::vector<std::uint8_t> type_payload(const std::vector<std::vector<std::uint8_t>> &types) {
    std::vector<std::uint8_t> p;
    put_uleb(p, types.size());
    for (const auto &t : types) {
        p.insert(p.end(), t.begin(), t.end());
    }
    return p;
}

// One import: ahfl_cap / cap_<symbol> / func / typeidx.
std::vector<std::uint8_t>
import_payload(const std::vector<std::pair<std::uint64_t, std::uint32_t>> &imports) {
    std::vector<std::uint8_t> p;
    put_uleb(p, imports.size());
    for (const auto &[symbol, typeidx] : imports) {
        const std::string module_name = "ahfl_cap";
        put_uleb(p, module_name.size());
        p.insert(p.end(), module_name.begin(), module_name.end());
        const std::string field = "cap_" + std::to_string(symbol);
        put_uleb(p, field.size());
        p.insert(p.end(), field.begin(), field.end());
        p.push_back(0x00); // func import
        put_uleb(p, typeidx);
    }
    return p;
}

std::vector<std::uint8_t> custom_payload(const std::string &name,
                                         const std::vector<std::uint8_t> &body) {
    std::vector<std::uint8_t> p;
    put_uleb(p, name.size());
    p.insert(p.end(), name.begin(), name.end());
    p.insert(p.end(), body.begin(), body.end());
    return p;
}

// D2a-F3: a Memory (wasm section id 5) section payload: count then one limits
// entry per memory (flags 0 = min only, 1 = min + max). Test-only.
struct MemorySectionSpec {
    std::uint32_t count = 1;
    std::uint8_t flags = 0; // 0 = min only; 1 = min + max; 2 = reserved (malformed)
    std::uint32_t min_pages = 1;
    std::uint32_t max_pages = 1; // read iff flags == 1
    // Append an extra trailing byte after the declared entries so the section is
    // structurally present but not exact-consumed.
    bool trailing_byte = false;
};

std::vector<std::uint8_t> memory_payload(const MemorySectionSpec &spec) {
    std::vector<std::uint8_t> p;
    put_uleb(p, spec.count);
    for (std::uint32_t i = 0; i < spec.count; ++i) {
        p.push_back(spec.flags);
        put_uleb(p, spec.min_pages);
        if (spec.flags == 1) {
            put_uleb(p, spec.max_pages);
        }
    }
    if (spec.trailing_byte) {
        p.push_back(0x00);
    }
    return p;
}

// A canonical AHFLXM manifest body for a Workflow with the given node specs.
struct ManifestNodeSpec {
    std::uint32_t workflow_node_id;
    std::uint8_t cap_call_count; // 0 or 1
    std::uint32_t capability;    // used iff cap_call_count == 1
    std::uint64_t source_symbol; // used iff cap_call_count == 1
};

std::vector<std::uint8_t> exec_manifest_body(std::uint32_t entry_id,
                                             const std::vector<ManifestNodeSpec> &nodes) {
    std::vector<std::uint8_t> b;
    const char magic[6] = {'A', 'H', 'F', 'L', 'X', 'M'};
    for (char c : magic) {
        b.push_back(static_cast<std::uint8_t>(c));
    }
    b.push_back(1); // version
    b.push_back(0); // entry.kind = Workflow
    put_uleb(b, entry_id);
    put_uleb(b, nodes.size());
    for (std::uint32_t i = 0; i < nodes.size(); ++i) {
        const auto &n = nodes[i];
        put_uleb(b, n.workflow_node_id);
        put_uleb(b, i); // schedule_pos == index
        b.push_back(n.cap_call_count);
        if (n.cap_call_count == 1) {
            put_uleb(b, n.capability);
            put_uleb(b, n.source_symbol);
        }
    }
    return b;
}

// Build a wire-schema table with the given capabilities; each cap result is a
// String struct field, param is a single Int (so param cardinality == 1).
CoreWireSchemaTable schema_table(const std::vector<std::pair<std::uint32_t, std::uint64_t>> &caps) {
    CoreWireSchemaTable table;
    table.nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}});    // 0: Int param
    table.nodes.push_back(CoreWireSchemaNode{CoreWireSchemaString{}}); // 1: String result
    for (const auto &[cap_id, symbol] : caps) {
        CoreWireCapabilitySchema c;
        c.capability = CoreCapabilityId{cap_id};
        c.source_symbol = symbol;
        c.params = {CoreWireSchemaNodeId{0}};
        c.result = CoreWireSchemaNodeId{1};
        table.capabilities.push_back(c);
    }
    return table;
}

std::vector<std::uint8_t> encode_schema(const CoreWireSchemaTable &table) {
    auto enc = ahfl::ir::core::encode_core_wire_schema_table(table);
    if (!enc.ok() || !enc.bytes.has_value()) {
        return {};
    }
    return *enc.bytes;
}

// Assemble a full module: header + Type + Import + [extra sections] + AHFLXM +
// AHFLWS-at-EOF. `caps` drives both the import table and the schema table so the
// strict one-to-one cross-check passes for a conforming module.
struct ModuleSpec {
    std::vector<std::pair<std::uint32_t, std::uint64_t>> caps; // (cap_id, source_symbol)
    std::vector<ManifestNodeSpec> manifest_nodes;
    std::uint32_t entry_id = 7;
    bool emit_manifest = true;
    bool emit_schema = true;
    bool duplicate_manifest = false;
    bool manifest_after_schema = false;
    bool custom_between = false;      // an unknown custom between manifest and schema
    bool section_after_schema = false; // a standard section after AHFLWS
    // D1a-2 test-only: an ACCEPTED unknown custom section emitted BEFORE the AHFLXM
    // manifest (the section walk skips an unknown custom seen before the manifest).
    // Its body is hashed into the whole-module digest but lies OUTSIDE both the raw
    // AHFLXM and raw AHFLWS payloads, so a one-byte change there flips only the
    // module digest. When set, the body is these exact bytes.
    std::optional<std::vector<std::uint8_t>> pre_manifest_custom_body;
    std::optional<std::vector<std::uint8_t>> manifest_override;
    std::optional<std::vector<std::uint8_t>> schema_override;
    // D2a-F3: an optional Memory (id 5) section payload emitted right after the
    // Import section. Absent -> the module carries no Memory section at all.
    std::optional<std::vector<std::uint8_t>> memory_section_override;
    bool duplicate_memory = false; // emit the Memory section twice
};

std::vector<std::uint8_t> build_module(const ModuleSpec &spec) {
    std::vector<std::uint8_t> m = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
    // Type section: one tuple type at index 0.
    put_section(m, 1, type_payload({capability_tuple()}));
    // Import section: every cap imports type 0, in cap order (ascending symbol as
    // the table is ascending by cap id; we keep imports aligned 1:1 by position).
    std::vector<std::pair<std::uint64_t, std::uint32_t>> imports;
    for (const auto &[cap_id, symbol] : spec.caps) {
        (void)cap_id;
        imports.emplace_back(symbol, 0u);
    }
    put_section(m, 2, import_payload(imports));

    if (spec.memory_section_override.has_value()) {
        put_section(m, 5, *spec.memory_section_override);
        if (spec.duplicate_memory) {
            put_section(m, 5, *spec.memory_section_override);
        }
    }

    const auto manifest = spec.manifest_override.has_value()
                              ? *spec.manifest_override
                              : exec_manifest_body(spec.entry_id, spec.manifest_nodes);
    const auto schema = spec.schema_override.has_value()
                            ? *spec.schema_override
                            : encode_schema(schema_table(spec.caps));

    const auto emit_manifest_section = [&]() {
        put_section(m, 0, custom_payload("ahfl.wasm-exec-manifest.v1", manifest));
    };
    const auto emit_schema_section = [&]() {
        put_section(m, 0, custom_payload("ahfl.wire-schema.v1", schema));
    };

    if (spec.manifest_after_schema) {
        if (spec.emit_schema) {
            emit_schema_section();
        }
        if (spec.emit_manifest) {
            emit_manifest_section();
        }
        return m;
    }
    if (spec.pre_manifest_custom_body.has_value()) {
        put_section(m, 0, custom_payload("ahfl.test.premanifest", *spec.pre_manifest_custom_body));
    }
    if (spec.emit_manifest) {
        emit_manifest_section();
    }
    if (spec.duplicate_manifest) {
        emit_manifest_section();
    }
    if (spec.custom_between) {
        put_section(m, 0, custom_payload("producers", {0x00}));
    }
    if (spec.emit_schema) {
        emit_schema_section();
    }
    if (spec.section_after_schema) {
        put_section(m, 12, {0x00}); // DataCount-like, after the target
    }
    return m;
}

bool admit_ok(const std::vector<std::uint8_t> &module) {
    return make_verified_core_wasm_schema_module(std::span<const std::uint8_t>(module)).ok();
}

bool admit_fails(const std::vector<std::uint8_t> &module) {
    auto r = make_verified_core_wasm_schema_module(std::span<const std::uint8_t>(module));
    if (r.ok() || r.module.has_value() || !r.has_errors()) {
        return false;
    }
    for (const auto &d : r.diagnostics) {
        if (d.code != std::string(ahfl::ir::core::wire_schema::kInvalid) ||
            d.source_range.has_value()) {
            return false;
        }
    }
    return true;
}

// ---- D1a-2 test-only raw-payload locator -------------------------------------
// An INDEPENDENT, fully bounds-checked byte walker (NOT the production framer and
// NOT the getter under test) that returns the half-open [begin,end) byte offset
// range of the raw AHFLXM / AHFLWS payload inside a module -- i.e. the bytes AFTER
// the custom-section name-length ULEB + name and BEFORE the next section. Returns
// {0,0} on any malformed framing (the caller asserts a non-empty range).
struct RawRange {
    std::size_t begin = 0;
    std::size_t end = 0;
    [[nodiscard]] bool empty() const noexcept { return begin >= end; }
    [[nodiscard]] bool contains(std::size_t off) const noexcept {
        return off >= begin && off < end;
    }
};

// Minimal bounds-checked ULEB reader over a byte vector at *pos; returns false on
// truncation / non-terminating encoding.
bool locator_read_uleb(const std::vector<std::uint8_t> &m, std::size_t &pos, std::uint64_t &out) {
    out = 0;
    std::uint32_t shift = 0;
    while (pos < m.size()) {
        const std::uint8_t b = m[pos++];
        if (shift >= 64) {
            return false;
        }
        out |= static_cast<std::uint64_t>(b & 0x7fU) << shift;
        if ((b & 0x80U) == 0) {
            return true;
        }
        shift += 7;
    }
    return false;
}

RawRange locate_custom_payload(const std::vector<std::uint8_t> &m, std::string_view section_name) {
    // Skip the 8-byte module header.
    if (m.size() < 8) {
        return {};
    }
    std::size_t pos = 8;
    while (pos < m.size()) {
        const std::uint8_t id = m[pos++];
        std::uint64_t size = 0;
        if (!locator_read_uleb(m, pos, size)) {
            return {};
        }
        const std::size_t body_begin = pos;
        if (size > m.size() - body_begin) {
            return {};
        }
        const std::size_t body_end = body_begin + static_cast<std::size_t>(size);
        if (id == 0) { // custom section
            std::size_t np = body_begin;
            std::uint64_t name_len = 0;
            if (!locator_read_uleb(m, np, name_len)) {
                return {};
            }
            // The name-length ULEB itself must not have run past this section, and
            // the name bytes must lie fully inside it (no unsigned underflow below).
            if (np > body_end || name_len > body_end - np) {
                return {};
            }
            const std::string_view name(reinterpret_cast<const char *>(m.data() + np),
                                        static_cast<std::size_t>(name_len));
            const std::size_t payload_begin = np + static_cast<std::size_t>(name_len);
            if (name == section_name) {
                return RawRange{payload_begin, body_end};
            }
        }
        pos = body_end;
    }
    return {};
}

// Count the number of differing byte offsets between two equal-length vectors and,
// when exactly one differs, report that offset. Returns false if the sizes differ.
bool single_byte_diff(const std::vector<std::uint8_t> &a, const std::vector<std::uint8_t> &b,
                      std::size_t &diff_offset, std::size_t &diff_count) {
    if (a.size() != b.size()) {
        return false;
    }
    diff_count = 0;
    diff_offset = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) {
            ++diff_count;
            diff_offset = i;
        }
    }
    return true;
}

// ---- real emitter helpers ---------------------------------------------------

std::optional<std::vector<std::uint8_t>> real_e2_module_bytes() {
    namespace fs = std::filesystem;
    const fs::path repo = ahfl::test_support::repo_root_from_source_file(__FILE__);
    const fs::path fixture = repo / "tests" / "golden" / "wasm" / "e2_capability_agent.ahfl";
    const ahfl::Frontend frontend;
    const auto parse = frontend.parse_file(fixture);
    if (parse.has_errors() || parse.program == nullptr) {
        return std::nullopt;
    }
    const ahfl::Resolver resolver;
    const auto resolve = resolver.resolve(*parse.program);
    if (resolve.has_errors()) {
        return std::nullopt;
    }
    const ahfl::TypeChecker checker;
    const auto typecheck = checker.check(*parse.program, resolve);
    if (typecheck.has_errors()) {
        return std::nullopt;
    }
    const ahfl::Validator validator;
    const auto validation = validator.validate(*parse.program, resolve, typecheck);
    if (validation.has_errors()) {
        return std::nullopt;
    }
    const auto ir = ahfl::lower_program_ir(*parse.program, resolve, typecheck);
    const auto core = ahfl::ir::core::lower_ahfl_to_core(ir);
    if (!core.ok()) {
        return std::nullopt;
    }
    const auto layouts = ahfl::ir::core::compute_core_layouts(core.program);
    if (!layouts.ok() || !layouts.table.has_value()) {
        return std::nullopt;
    }
    const auto emitted = ahfl::backends::emit_core_wasm(
        core.program, *layouts.table,
        {ahfl::ir::core::CoreAgentId{0}, ahfl::backends::WasmProfileKind::Wasi});
    if (!emitted.ok() || !emitted.artifact.has_value()) {
        return std::nullopt;
    }
    return emitted.artifact->bytes;
}

// Emit the GENUINE capability-bearing workflow (RFC 0026 E4-B2-C) end-to-end from
// its committed golden source. Unlike real_e2_module_bytes + synthetic AHFLXM
// injection, this is a real emitter artifact that ALREADY carries the AHFLXM
// exec-manifest, so feeding it straight to make_verified_core_wasm_schema_module
// is genuine emitter->A2 admission evidence (no test-synthesized manifest).
std::optional<std::vector<std::uint8_t>> real_capability_workflow_module_bytes() {
    namespace fs = std::filesystem;
    const fs::path repo = ahfl::test_support::repo_root_from_source_file(__FILE__);
    const fs::path fixture =
        repo / "tests" / "golden" / "wasm" / "e3_capability_workflow.ahfl";
    const ahfl::Frontend frontend;
    const auto parse = frontend.parse_file(fixture);
    if (parse.has_errors() || parse.program == nullptr) {
        return std::nullopt;
    }
    const ahfl::Resolver resolver;
    const auto resolve = resolver.resolve(*parse.program);
    if (resolve.has_errors()) {
        return std::nullopt;
    }
    const ahfl::TypeChecker checker;
    const auto typecheck = checker.check(*parse.program, resolve);
    if (typecheck.has_errors()) {
        return std::nullopt;
    }
    const ahfl::Validator validator;
    const auto validation = validator.validate(*parse.program, resolve, typecheck);
    if (validation.has_errors()) {
        return std::nullopt;
    }
    const auto ir = ahfl::lower_program_ir(*parse.program, resolve, typecheck);
    const auto core = ahfl::ir::core::lower_ahfl_to_core(ir);
    if (!core.ok()) {
        return std::nullopt;
    }
    const auto layouts = ahfl::ir::core::compute_core_layouts(core.program);
    if (!layouts.ok() || !layouts.table.has_value()) {
        return std::nullopt;
    }
    const auto emitted = ahfl::backends::emit_core_wasm(
        core.program, *layouts.table,
        {ahfl::ir::core::CoreWorkflowId{0}, ahfl::backends::WasmProfileKind::Wasi});
    if (!emitted.ok() || !emitted.artifact.has_value()) {
        return std::nullopt;
    }
    return emitted.artifact->bytes;
}

// Inject a synthetic canonical AHFLXM section immediately before the EOF AHFLWS
// section of a real module. Returns nullopt if the module does not end with an
// AHFLWS custom section (it always should for an E2 capability artifact).
std::optional<std::vector<std::uint8_t>>
inject_manifest_before_schema(const std::vector<std::uint8_t> &module,
                              const std::vector<std::uint8_t> &manifest_body) {
    // The AHFLWS section is the final section. Find its start by walking sections.
    // Simpler + robust: the E2 writer appends exactly one custom section at EOF, so
    // we locate the last section header by re-walking from offset 8.
    std::size_t off = 8;
    std::size_t last_section_start = std::string::npos;
    while (off < module.size()) {
        last_section_start = off;
        // id
        ++off;
        // size ULEB
        std::uint64_t size = 0;
        std::uint32_t shift = 0;
        while (off < module.size()) {
            const std::uint8_t b = module[off++];
            size |= static_cast<std::uint64_t>(b & 0x7fU) << shift;
            if ((b & 0x80U) == 0) {
                break;
            }
            shift += 7;
        }
        off += static_cast<std::size_t>(size);
    }
    if (last_section_start == std::string::npos || off != module.size()) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> out(module.begin(),
                                  module.begin() + static_cast<std::ptrdiff_t>(last_section_start));
    put_section(out, 0, custom_payload("ahfl.wasm-exec-manifest.v1", manifest_body));
    out.insert(out.end(),
               module.begin() + static_cast<std::ptrdiff_t>(last_section_start), module.end());
    return out;
}

// Read the emitter's sole capability identity (cap id, source_symbol) out of the
// genuine AHFLWS section at module EOF, via the C1 decoder. Returns nullopt if the
// module does not end with a decodable single-capability wire-schema section.
std::optional<std::pair<std::uint32_t, std::uint64_t>>
real_e2_sole_capability(const std::vector<std::uint8_t> &module) {
    std::size_t off = 8;
    std::span<const std::uint8_t> last_payload;
    std::uint8_t last_id = 0xff;
    while (off < module.size()) {
        const std::uint8_t id = module[off++];
        std::uint64_t size = 0;
        std::uint32_t shift = 0;
        while (off < module.size()) {
            const std::uint8_t b = module[off++];
            size |= static_cast<std::uint64_t>(b & 0x7fU) << shift;
            if ((b & 0x80U) == 0) {
                break;
            }
            shift += 7;
        }
        if (off + size > module.size()) {
            return std::nullopt;
        }
        last_id = id;
        last_payload = std::span<const std::uint8_t>(module.data() + off, size);
        off += static_cast<std::size_t>(size);
    }
    if (last_id != 0) {
        return std::nullopt; // last section is not a custom section
    }
    // Strip the custom name framing.
    std::size_t p = 0;
    std::uint64_t name_len = 0;
    std::uint32_t shift = 0;
    while (p < last_payload.size()) {
        const std::uint8_t b = last_payload[p++];
        name_len |= static_cast<std::uint64_t>(b & 0x7fU) << shift;
        if ((b & 0x80U) == 0) {
            break;
        }
        shift += 7;
    }
    if (p + name_len > last_payload.size()) {
        return std::nullopt;
    }
    const auto table_bytes = last_payload.subspan(p + name_len);
    auto decoded = ahfl::ir::core::decode_core_wire_schema_table(table_bytes);
    if (!decoded.ok() || !decoded.table.has_value() ||
        decoded.table->capabilities.size() != 1) {
        return std::nullopt;
    }
    const auto &cap = decoded.table->capabilities.front();
    return std::make_pair(cap.capability.value, cap.source_symbol);
}

} // namespace

int main() {
    // ==== HAND-BUILT two-section fixtures (no emitter) ====

    // Positive: two caps {3,7}, sparse ids, SymbolId 0 on cap 3, one identity node
    // + two capability nodes referencing caps 3 and 7 (import order aligns 1:1).
    {
        ModuleSpec spec;
        spec.caps = {{3, 0}, {7, 700}}; // (cap_id, source_symbol); SymbolId 0 legal
        spec.manifest_nodes = {
            {40, 0, 0, 0},   // identity node
            {41, 1, 3, 0},   // cap node -> cap 3
            {42, 1, 7, 700}, // cap node -> cap 7
        };
        const auto module = build_module(spec);
        check(admit_ok(module), "handbuilt.positive_two_cap_sparse_symbol0");

        auto admitted =
            make_verified_core_wasm_schema_module(std::span<const std::uint8_t>(module));
        check(admitted.ok(), "handbuilt.admitted");
        if (admitted.ok()) {
            const auto &mod = *admitted.module;
            check(mod.entry_id() == CoreWorkflowId{7}, "handbuilt.entry_id");
            check(mod.node_count() == 3, "handbuilt.node_count");
            check(mod.call_site_count() == 2, "handbuilt.call_site_count");

            // resolve_node covers the identity node (cap_call_count 0).
            auto n0 = mod.resolve_node(ManifestNodeIndex{0});
            check(n0.ok() && n0.node->cap_call_count() == 0 &&
                      n0.node->workflow_node_id() == CoreWorkflowNodeId{40},
                  "handbuilt.node0_identity");
            auto n1 = mod.resolve_node(ManifestNodeIndex{1});
            check(n1.ok() && n1.node->cap_call_count() == 1 &&
                      n1.node->schedule_pos() == ManifestNodeIndex{1},
                  "handbuilt.node1_capability");
            auto n_oob = mod.resolve_node(ManifestNodeIndex{9});
            check(admit_ok(module) && !n_oob.ok(), "handbuilt.node_oob_fails");

            // call site 0 -> cap 3 (import ordinal 0); call site 1 -> cap 7 (ordinal 1).
            auto cs0 = mod.resolve(ManifestCallSiteIndex{0});
            check(cs0.ok() && cs0.call_site->capability() == CoreCapabilityId{3} &&
                      cs0.call_site->source_symbol() == 0 &&
                      cs0.call_site->import_ordinal() == CapabilityImportOrdinal{0} &&
                      cs0.call_site->invocation_ordinal() ==
                          ahfl::runtime::core_wasm_resume::InvocationOrdinal{0},
                  "handbuilt.callsite0_cap3");
            auto cs1 = mod.resolve(ManifestCallSiteIndex{1});
            check(cs1.ok() && cs1.call_site->capability() == CoreCapabilityId{7} &&
                      cs1.call_site->import_ordinal() == CapabilityImportOrdinal{1},
                  "handbuilt.callsite1_cap7");

            // Eager pre-minted bindings: Param + Result from ONE call site share the
            // SAME table backing (address equal), proving no per-mint copy.
            if (cs0.ok()) {
                auto p = cs0.call_site->param_binding();
                auto r = cs0.call_site->result_binding();
                check(&p.table() == &r.table(), "handbuilt.param_result_shared_backing");

                // Lifetime: bindings + token stay valid after the module handle drops.
                admitted.module.reset();
                auto p2 = cs0.call_site->param_binding();
                check(&p2.table() == &p.table(), "handbuilt.binding_valid_after_module_drop");
            }
        }
    }

    // Two distinct fixtures resolve their OWN entry/cap (semantic isolation).
    {
        ModuleSpec a;
        a.caps = {{0, 5}};
        a.entry_id = 11;
        a.manifest_nodes = {{100, 1, 0, 5}};
        ModuleSpec b;
        b.caps = {{0, 9}};
        b.entry_id = 22;
        b.manifest_nodes = {{200, 1, 0, 9}};
        const auto bytes_a = build_module(a);
        const auto bytes_b = build_module(b);
        auto ma = make_verified_core_wasm_schema_module(std::span<const std::uint8_t>(bytes_a));
        auto mb = make_verified_core_wasm_schema_module(std::span<const std::uint8_t>(bytes_b));
        check(ma.ok() && mb.ok(), "isolation.both_admit");
        if (ma.ok() && mb.ok()) {
            check(ma.module->entry_id() == CoreWorkflowId{11} &&
                      mb.module->entry_id() == CoreWorkflowId{22},
                  "isolation.distinct_entry");
            auto csa = ma.module->resolve(ManifestCallSiteIndex{0});
            auto csb = mb.module->resolve(ManifestCallSiteIndex{0});
            check(csa.ok() && csb.ok() && csa.call_site->source_symbol() == 5 &&
                      csb.call_site->source_symbol() == 9,
                  "isolation.distinct_source_symbol");
        }
    }

    // Repeated capability across nodes is allowed (one cap in authority set).
    {
        ModuleSpec spec;
        spec.caps = {{3, 300}};
        spec.manifest_nodes = {{40, 1, 3, 300}, {41, 1, 3, 300}};
        check(admit_ok(build_module(spec)), "handbuilt.repeated_cap_across_nodes_ok");
    }

    // ==== HAND-BUILT negatives ====
    {
        ModuleSpec base;
        base.caps = {{3, 300}};
        base.manifest_nodes = {{40, 1, 3, 300}};

        // missing manifest
        {
            ModuleSpec s = base;
            s.emit_manifest = false;
            check(admit_fails(build_module(s)), "neg.missing_manifest");
        }
        // missing schema
        {
            ModuleSpec s = base;
            s.emit_schema = false;
            check(admit_fails(build_module(s)), "neg.missing_schema");
        }
        // duplicate manifest
        {
            ModuleSpec s = base;
            s.duplicate_manifest = true;
            check(admit_fails(build_module(s)), "neg.duplicate_manifest");
        }
        // manifest after schema (wrong order)
        {
            ModuleSpec s = base;
            s.manifest_after_schema = true;
            check(admit_fails(build_module(s)), "neg.manifest_after_schema");
        }
        // an unknown custom between manifest and schema (not immediately-before)
        {
            ModuleSpec s = base;
            s.custom_between = true;
            check(admit_fails(build_module(s)), "neg.custom_between_manifest_schema");
        }
        // a section after the EOF schema
        {
            ModuleSpec s = base;
            s.section_after_schema = true;
            check(admit_fails(build_module(s)), "neg.section_after_schema");
        }
        // manifest wrong magic
        {
            ModuleSpec s = base;
            auto body = exec_manifest_body(7, base.manifest_nodes);
            body[0] = 'X';
            s.manifest_override = body;
            check(admit_fails(build_module(s)), "neg.manifest_bad_magic");
        }
        // manifest bad version
        {
            ModuleSpec s = base;
            auto body = exec_manifest_body(7, base.manifest_nodes);
            body[6] = 2;
            s.manifest_override = body;
            check(admit_fails(build_module(s)), "neg.manifest_bad_version");
        }
        // manifest cap_call_count out of range (2)
        {
            ModuleSpec s = base;
            auto body = exec_manifest_body(7, base.manifest_nodes);
            body.back() = body.back(); // placeholder; rebuild via override with bad count
            // hand-build: magic+ver+kind+entry(7)+count(1)+node{id40,pos0,cap_call_count=2}
            std::vector<std::uint8_t> bad;
            const char magic[6] = {'A', 'H', 'F', 'L', 'X', 'M'};
            for (char c : magic) {
                bad.push_back(static_cast<std::uint8_t>(c));
            }
            bad.push_back(1);
            bad.push_back(0);
            put_uleb(bad, 7);
            put_uleb(bad, 1);
            put_uleb(bad, 40);
            put_uleb(bad, 0);
            bad.push_back(2); // out of range
            s.manifest_override = bad;
            check(admit_fails(build_module(s)), "neg.manifest_bad_cap_call_count");
        }
        // manifest schedule_pos not dense (node index mismatch)
        {
            ModuleSpec s = base;
            std::vector<std::uint8_t> bad;
            const char magic[6] = {'A', 'H', 'F', 'L', 'X', 'M'};
            for (char c : magic) {
                bad.push_back(static_cast<std::uint8_t>(c));
            }
            bad.push_back(1);
            bad.push_back(0);
            put_uleb(bad, 7);
            put_uleb(bad, 1);
            put_uleb(bad, 40);
            put_uleb(bad, 5); // schedule_pos != index 0
            bad.push_back(1);
            put_uleb(bad, 3);
            put_uleb(bad, 300);
            s.manifest_override = bad;
            check(admit_fails(build_module(s)), "neg.manifest_schedule_pos_gap");
        }
        // manifest trailing byte
        {
            ModuleSpec s = base;
            auto body = exec_manifest_body(7, base.manifest_nodes);
            body.push_back(0x00);
            s.manifest_override = body;
            check(admit_fails(build_module(s)), "neg.manifest_trailing");
        }
        // hidden import: manifest references cap 3 only, but the module/schema carry
        // caps 3 AND 7 -> cap 7 unreferenced -> set-equality fails.
        {
            ModuleSpec s;
            s.caps = {{3, 300}, {7, 700}};
            s.manifest_nodes = {{40, 1, 3, 300}}; // only references cap 3
            check(admit_fails(build_module(s)), "neg.hidden_unreferenced_capability");
        }
        // manifest references a cap absent from the authority set.
        {
            ModuleSpec s;
            s.caps = {{3, 300}};
            s.manifest_nodes = {{40, 1, 9, 300}}; // cap 9 not in table/imports
            check(admit_fails(build_module(s)), "neg.manifest_cap_absent_from_authority");
        }
        // manifest source symbol mismatch vs the capability entry.
        {
            ModuleSpec s;
            s.caps = {{3, 300}};
            s.manifest_nodes = {{40, 1, 3, 999}}; // wrong symbol for cap 3
            check(admit_fails(build_module(s)), "neg.manifest_source_symbol_mismatch");
        }
        // huge node_count with a short manifest payload -> count-before-reserve.
        {
            ModuleSpec s = base;
            std::vector<std::uint8_t> bad;
            const char magic[6] = {'A', 'H', 'F', 'L', 'X', 'M'};
            for (char c : magic) {
                bad.push_back(static_cast<std::uint8_t>(c));
            }
            bad.push_back(1);
            bad.push_back(0);
            put_uleb(bad, 7);
            put_uleb(bad, 0xFFFFFFF0U); // attacker node_count; body then STOPS
            s.manifest_override = bad;
            check(admit_fails(build_module(s)), "neg.manifest_huge_node_count");
        }
        // manifest noncanonical (overlong) ULEB for the entry id.
        {
            ModuleSpec s = base;
            std::vector<std::uint8_t> bad;
            const char magic[6] = {'A', 'H', 'F', 'L', 'X', 'M'};
            for (char c : magic) {
                bad.push_back(static_cast<std::uint8_t>(c));
            }
            bad.push_back(1);
            bad.push_back(0);
            bad.push_back(0x87);
            bad.push_back(0x00); // OVERLONG entry_id = 7
            put_uleb(bad, 1);
            put_uleb(bad, 40);
            put_uleb(bad, 0);
            bad.push_back(1);
            put_uleb(bad, 3);
            put_uleb(bad, 300);
            s.manifest_override = bad;
            check(admit_fails(build_module(s)), "neg.manifest_overlong_entry_id");
        }
        // manifest bad entry.kind (not Workflow).
        {
            ModuleSpec s = base;
            auto body = exec_manifest_body(7, base.manifest_nodes);
            body[7] = 1; // entry.kind byte (magic6+ver1 = offset 7)
            s.manifest_override = body;
            check(admit_fails(build_module(s)), "neg.manifest_bad_entry_kind");
        }
        // manifest entry id invalid sentinel (UINT32_MAX) -> canonical ULEB of it.
        {
            ModuleSpec s = base;
            std::vector<std::uint8_t> bad;
            const char magic[6] = {'A', 'H', 'F', 'L', 'X', 'M'};
            for (char c : magic) {
                bad.push_back(static_cast<std::uint8_t>(c));
            }
            bad.push_back(1);
            bad.push_back(0);
            put_uleb(bad, CoreWorkflowId::kInvalid);
            put_uleb(bad, 1);
            put_uleb(bad, 40);
            put_uleb(bad, 0);
            bad.push_back(1);
            put_uleb(bad, 3);
            put_uleb(bad, 300);
            s.manifest_override = bad;
            check(admit_fails(build_module(s)), "neg.manifest_entry_id_sentinel");
        }
        // manifest node id invalid sentinel.
        {
            ModuleSpec s = base;
            std::vector<std::uint8_t> bad;
            const char magic[6] = {'A', 'H', 'F', 'L', 'X', 'M'};
            for (char c : magic) {
                bad.push_back(static_cast<std::uint8_t>(c));
            }
            bad.push_back(1);
            bad.push_back(0);
            put_uleb(bad, 7);
            put_uleb(bad, 1);
            put_uleb(bad, CoreWorkflowNodeId::kInvalid);
            put_uleb(bad, 0);
            bad.push_back(1);
            put_uleb(bad, 3);
            put_uleb(bad, 300);
            s.manifest_override = bad;
            check(admit_fails(build_module(s)), "neg.manifest_node_id_sentinel");
        }
        // manifest capability invalid sentinel.
        {
            ModuleSpec s = base;
            std::vector<std::uint8_t> bad;
            const char magic[6] = {'A', 'H', 'F', 'L', 'X', 'M'};
            for (char c : magic) {
                bad.push_back(static_cast<std::uint8_t>(c));
            }
            bad.push_back(1);
            bad.push_back(0);
            put_uleb(bad, 7);
            put_uleb(bad, 1);
            put_uleb(bad, 40);
            put_uleb(bad, 0);
            bad.push_back(1);
            put_uleb(bad, CoreCapabilityId::kInvalid);
            put_uleb(bad, 300);
            s.manifest_override = bad;
            check(admit_fails(build_module(s)), "neg.manifest_capability_sentinel");
        }
        // duplicate node id across nodes.
        {
            ModuleSpec s;
            s.caps = {{3, 300}};
            std::vector<std::uint8_t> bad;
            const char magic[6] = {'A', 'H', 'F', 'L', 'X', 'M'};
            for (char c : magic) {
                bad.push_back(static_cast<std::uint8_t>(c));
            }
            bad.push_back(1);
            bad.push_back(0);
            put_uleb(bad, 7);
            put_uleb(bad, 2);   // two nodes
            put_uleb(bad, 40);  // node 0 id
            put_uleb(bad, 0);   // schedule_pos 0
            bad.push_back(0);   // identity
            put_uleb(bad, 40);  // node 1 id == node 0 (duplicate)
            put_uleb(bad, 1);   // schedule_pos 1
            bad.push_back(1);   // capability
            put_uleb(bad, 3);
            put_uleb(bad, 300);
            s.manifest_override = bad;
            check(admit_fails(build_module(s)), "neg.manifest_duplicate_node_id");
        }
        // hidden SCHEMA capability: the schema table carries an extra cap that the
        // import table does NOT (strict one-to-one import<->schema fails).
        {
            ModuleSpec s;
            s.caps = {{3, 300}}; // imports carry ONE cap
            s.manifest_nodes = {{40, 1, 3, 300}};
            s.schema_override = encode_schema(schema_table({{3, 300}, {7, 700}})); // schema has TWO
            check(admit_fails(build_module(s)), "neg.hidden_schema_capability");
        }
        // exact-one-Param violation: the cap's schema result/param shape gives the
        // capability TWO params, so eager Param{0} mint context rejects cardinality.
        {
            ModuleSpec s;
            s.caps = {{3, 300}};
            s.manifest_nodes = {{40, 1, 3, 300}};
            CoreWireSchemaTable two_param;
            two_param.nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}});    // 0
            two_param.nodes.push_back(CoreWireSchemaNode{CoreWireSchemaString{}}); // 1
            CoreWireCapabilitySchema c;
            c.capability = CoreCapabilityId{3};
            c.source_symbol = 300;
            c.params = {CoreWireSchemaNodeId{0}, CoreWireSchemaNodeId{0}}; // TWO params
            c.result = CoreWireSchemaNodeId{1};
            two_param.capabilities.push_back(c);
            s.schema_override = encode_schema(two_param);
            check(admit_fails(build_module(s)), "neg.exact_one_param_violation");
        }
        // wrong Type ordinal: the import points at a typeidx whose signature is NOT
        // the ahfl_cap tuple (two types; import -> the non-tuple one).
        {
            ModuleSpec s;
            s.caps = {{3, 300}};
            s.manifest_nodes = {{40, 1, 3, 300}};
            // Build a bespoke module with two types: index 0 tuple, index 1 wrong;
            // import references type index 1.
            std::vector<std::uint8_t> m = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
            put_section(m, 1, type_payload({capability_tuple(), func_type({0x7f}, {0x7f})}));
            put_section(m, 2, import_payload({{300, 1}})); // typeidx 1 (wrong signature)
            put_section(m, 0,
                        custom_payload("ahfl.wasm-exec-manifest.v1",
                                       exec_manifest_body(7, {{40, 1, 3, 300}})));
            put_section(m, 0, custom_payload("ahfl.wire-schema.v1",
                                             encode_schema(schema_table({{3, 300}}))));
            check(admit_fails(m), "neg.import_wrong_type_ordinal");
        }
        // WH-3 bridge positive: the import uses the bridge (i32)->(i32,i32)
        // functype (not the opaque 3-result tuple) and is ADMITTED. The schema
        // capability still has exactly one Param (the one-param rule is about the
        // schema, not the functype), so eager mint succeeds.
        {
            std::vector<std::uint8_t> m = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
            put_section(m, 1, type_payload({bridge_signature()}));
            put_section(m, 2, import_payload({{300, 0}})); // typeidx 0 == bridge
            put_section(m, 0,
                        custom_payload("ahfl.wasm-exec-manifest.v1",
                                       exec_manifest_body(7, {{40, 1, 3, 300}})));
            put_section(m, 0, custom_payload("ahfl.wire-schema.v1",
                                             encode_schema(schema_table({{3, 300}}))));
            check(admit_ok(m), "bridge.import_bridge_functype_admitted");
            auto admitted = make_verified_core_wasm_schema_module(std::span<const std::uint8_t>(m));
            check(admitted.ok() && admitted.module->call_site_count() == 1,
                  "bridge.import_one_call_site");
        }
        // WH-3 bridge + tuple coexist: two types [tuple, bridge], the import
        // references the bridge typeidx and is admitted (proves the gate is
        // tuple-OR-bridge, not tuple-only).
        {
            std::vector<std::uint8_t> m = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
            put_section(m, 1, type_payload({capability_tuple(), bridge_signature()}));
            put_section(m, 2, import_payload({{300, 1}})); // typeidx 1 == bridge
            put_section(m, 0,
                        custom_payload("ahfl.wasm-exec-manifest.v1",
                                       exec_manifest_body(7, {{40, 1, 3, 300}})));
            put_section(m, 0, custom_payload("ahfl.wire-schema.v1",
                                             encode_schema(schema_table({{3, 300}}))));
            check(admit_ok(m), "bridge.import_bridge_second_type_admitted");
        }
        // WH-3 third-functype negative: three types [tuple, bridge, (i32)->i32],
        // the import references the (i32)->i32 typeidx and is REJECTED (neither
        // tuple nor bridge). Proves the bridge acceptance does not loosen the gate
        // for an unrelated shape.
        {
            std::vector<std::uint8_t> m = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
            put_section(m, 1,
                        type_payload({capability_tuple(), bridge_signature(),
                                      func_type({0x7f}, {0x7f})}));
            put_section(m, 2, import_payload({{300, 2}})); // typeidx 2 == (i32)->i32
            put_section(m, 0,
                        custom_payload("ahfl.wasm-exec-manifest.v1",
                                       exec_manifest_body(7, {{40, 1, 3, 300}})));
            put_section(m, 0, custom_payload("ahfl.wire-schema.v1",
                                             encode_schema(schema_table({{3, 300}}))));
            check(admit_fails(m), "bridge.import_third_functype_rejected");
        }
        // duplicate EOF wire-schema section: two AHFLWS sections -> the second is a
        // section after the (first) EOF-target and fails the not-at-EOF gate.
        {
            std::vector<std::uint8_t> m = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
            put_section(m, 1, type_payload({capability_tuple()}));
            put_section(m, 2, import_payload({{300, 0}}));
            put_section(m, 0, custom_payload("ahfl.wasm-exec-manifest.v1",
                                             exec_manifest_body(7, {{40, 1, 3, 300}})));
            const auto schema = encode_schema(schema_table({{3, 300}}));
            put_section(m, 0, custom_payload("ahfl.wire-schema.v1", schema));
            put_section(m, 0, custom_payload("ahfl.wire-schema.v1", schema)); // duplicate at EOF
            check(admit_fails(m), "neg.duplicate_wire_schema_section");
        }
        // malformed custom-section name framing: a name length that overruns the
        // section payload (non-canonical/bounds) must fail closed in the framer.
        {
            std::vector<std::uint8_t> m = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
            put_section(m, 1, type_payload({capability_tuple()}));
            put_section(m, 2, import_payload({{300, 0}}));
            // A custom section whose declared name length exceeds its own payload.
            std::vector<std::uint8_t> bad_custom;
            put_uleb(bad_custom, 0xFFU); // name length 255, but no name bytes follow
            put_section(m, 0, bad_custom);
            check(admit_fails(m), "neg.malformed_custom_name_framing");
        }
    }

    // ==== no-echo: a marker planted in a wire name / import field must never
    //      surface in any diagnostic; every diagnostic is fixed code + null range. ====
    {
        // Plant the marker as the exec-manifest section NAME suffix is not possible
        // (the name is matched exactly), so plant it in a bogus custom section name
        // placed between manifest and schema (which fails the placement gate) and
        // assert the marker never appears in any diagnostic.
        static constexpr std::string_view kMarker = "A2_SECRET_MARKER_ZZZ";
        ModuleSpec s;
        s.caps = {{3, 300}};
        s.manifest_nodes = {{40, 1, 3, 300}};
        std::vector<std::uint8_t> m = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
        put_section(m, 1, type_payload({capability_tuple()}));
        put_section(m, 2, import_payload({{300, 0}}));
        put_section(m, 0, custom_payload("ahfl.wasm-exec-manifest.v1",
                                         exec_manifest_body(7, {{40, 1, 3, 300}})));
        put_section(m, 0, custom_payload(std::string(kMarker), {0x00})); // bogus custom between
        put_section(m, 0, custom_payload("ahfl.wire-schema.v1",
                                         encode_schema(schema_table({{3, 300}}))));
        auto r = make_verified_core_wasm_schema_module(std::span<const std::uint8_t>(m));
        check(!r.ok() && r.has_errors() && !r.module.has_value(), "noecho.fails_closed");
        bool clean = true;
        for (const auto &d : r.diagnostics) {
            if (d.code != std::string(ahfl::ir::core::wire_schema::kInvalid) ||
                d.source_range.has_value() ||
                d.message.find(kMarker) != std::string::npos) {
                clean = false;
            }
        }
        check(clean, "noecho.fixed_code_null_range_no_marker");
    }

    // ==== REAL emitter evidence ====
    {
        auto real = real_e2_module_bytes();
        check(real.has_value(), "real.e2_emit_ok");
        if (real) {
            // raw real E2 has NO AHFLXM -> must fail closed (missing manifest).
            check(admit_fails(*real), "real.raw_e2_missing_manifest_fails_closed");

            // real emitter bytes + SYNTHETIC manifest injection immediately before
            // the EOF AHFLWS. We read the emitter's ACTUAL sole capability identity
            // (cap id + source_symbol) out of the genuine AHFLWS section via the C1
            // decoder, then synthesize a matching single-cap-node Workflow manifest.
            // This proves A2 framing/table-decode/cross-check/eager-mint accept
            // genuine Type/Import/AHFLWS bytes -- it is NOT capability-workflow
            // emitter/topology evidence (the manifest is test-synthesized).
            auto identity = real_e2_sole_capability(*real);
            check(identity.has_value(), "real.read_sole_capability");
            if (identity) {
                const auto manifest = exec_manifest_body(
                    3, {{50, 1, identity->first, identity->second}});
                auto injected = inject_manifest_before_schema(*real, manifest);
                check(injected.has_value(), "real.injection_built");
                if (injected) {
                    // Full-chain assertion over genuine Type/Import/AHFLWS bytes:
                    // framing -> C1 table decode -> cross-check/set-equality ->
                    // eager mint -> read surface.
                    auto admitted = make_verified_core_wasm_schema_module(
                        std::span<const std::uint8_t>(*injected));
                    check(admitted.ok(), "real.injected_admitted");
                    if (admitted.ok()) {
                        check(admitted.module->call_site_count() == 1,
                              "real.injected_one_call_site");
                        auto cs = admitted.module->resolve(ManifestCallSiteIndex{0});
                        check(cs.ok(), "real.injected_resolve_ok");
                        if (cs.ok()) {
                            check(cs.call_site->capability() ==
                                          CoreCapabilityId{identity->first} &&
                                      cs.call_site->source_symbol() == identity->second,
                                  "real.injected_callsite_identity_matches_ahflws");
                            check(cs.call_site->import_ordinal() == CapabilityImportOrdinal{0},
                                  "real.injected_import_ordinal");
                            auto p = cs.call_site->param_binding();
                            auto r = cs.call_site->result_binding();
                            check(p.selector().capability ==
                                          CoreCapabilityId{identity->first} &&
                                      r.selector().capability ==
                                          CoreCapabilityId{identity->first},
                                  "real.injected_binding_selectors_match_identity");
                            check(&p.table() == &r.table(),
                                  "real.injected_param_result_shared_backing");
                        }
                    }
                }
            }
        }

        // ==== GENUINE capability-workflow emitter -> A2 admission ====
        // The real B2-C emitter already writes the AHFLXM exec-manifest, so its
        // bytes are admitted directly (no synthetic injection). This is the true
        // compiler->A2 cross-layer consistency evidence for a capability workflow.
        auto cap_wf = real_capability_workflow_module_bytes();
        check(cap_wf.has_value(), "real.capwf_emit_ok");
        if (cap_wf) {
            auto admitted = make_verified_core_wasm_schema_module(
                std::span<const std::uint8_t>(*cap_wf));
            check(admitted.ok(), "real.capwf_genuine_admitted");
            if (admitted.ok()) {
                // Genuine AHFLXM topology: entry workflow 0, two nodes.
                check(admitted.module->entry_id() == CoreWorkflowId{0},
                      "real.capwf_entry_id");
                check(admitted.module->node_count() == 2, "real.capwf_node_count");
                // node0 = capability (id 0, schedule 0, cap_call_count 1).
                auto n0 = admitted.module->resolve_node(ManifestNodeIndex{0});
                check(n0.ok() &&
                          n0.node->workflow_node_id() == CoreWorkflowNodeId{0} &&
                          n0.node->schedule_pos() == ManifestNodeIndex{0} &&
                          n0.node->cap_call_count() == 1,
                      "real.capwf_node0_capability");
                // node1 = identity (id 1, schedule 1, cap_call_count 0).
                auto n1 = admitted.module->resolve_node(ManifestNodeIndex{1});
                check(n1.ok() &&
                          n1.node->workflow_node_id() == CoreWorkflowNodeId{1} &&
                          n1.node->schedule_pos() == ManifestNodeIndex{1} &&
                          n1.node->cap_call_count() == 0,
                      "real.capwf_node1_identity");
                // Exactly one call site (node0).
                check(admitted.module->call_site_count() == 1,
                      "real.capwf_one_call_site");
                // D2a-F3: the genuine emitter artifact's OWN Memory section
                // declares exactly one no-maximum page, i.e. the F1 fixed
                // single-page capacity read from the digest-authenticated bytes.
                {
                    auto cap = admitted.module->declared_linear_memory_capacity();
                    check(cap.has_value(), "real.capwf_declared_capacity_ok");
                    if (cap.has_value()) {
                        check(cap->capacity_bytes ==
                                      ahfl::ir::core::kCoreWasmFixedLinearMemoryCapacityBytes &&
                                  cap->capacity_bytes == 65536 &&
                                  cap->min_pages ==
                                      ahfl::ir::core::kCoreWasmFixedLinearMemoryMinPages &&
                                  !cap->has_max,
                              "real.capwf_declared_capacity_fixed_single_page");
                    }
                }
                auto cs = admitted.module->resolve(ManifestCallSiteIndex{0});
                check(cs.ok(), "real.capwf_resolve_ok");
                if (cs.ok()) {
                    // Exact canonical identity: the same committed golden's binary
                    // gate hard-locks AHFLXM capability=0, source_symbol=1, so the
                    // call site must report exactly those values (cross-test proof
                    // that the emitter and A2 agree on one canonical identity).
                    check(cs.call_site->workflow_node_id() == CoreWorkflowNodeId{0} &&
                              cs.call_site->schedule_pos() == ManifestNodeIndex{0} &&
                              cs.call_site->invocation_ordinal() ==
                                  ahfl::runtime::core_wasm_resume::InvocationOrdinal{0} &&
                              cs.call_site->capability() == CoreCapabilityId{0} &&
                              cs.call_site->source_symbol() == 1 &&
                              cs.call_site->import_ordinal() == CapabilityImportOrdinal{0},
                          "real.capwf_callsite_coordinates");
                    auto p = cs.call_site->param_binding();
                    auto r = cs.call_site->result_binding();
                    check(p.selector().capability == CoreCapabilityId{0} &&
                              p.selector().expected_source_symbol == 1 &&
                              r.selector().capability == CoreCapabilityId{0} &&
                              r.selector().expected_source_symbol == 1,
                          "real.capwf_binding_selectors_match_identity");
                    check(&p.table() == &r.table(),
                          "real.capwf_param_result_shared_backing");
                }
            }
        }
    }

    // ---- D2a-F3: declared-memory capacity reader ---------------------------
    // A2 parses the Memory (wasm section id 5) section during its one framing
    // pass and exposes the artifact-declared fixed single-page capacity the
    // host cross-checks against the F1 SSOT before instantiation. Evidence:
    //   * conforming hand-built section -> 65536 / one page / no max;
    //   * missing / zero / two memories / max present / min!=1 -> typed
    //     MemoryDeclError while the module itself still admits (the fixed-page
    //     contract is the getter's, applied after digest-authenticated framing);
    //   * structurally malformed Memory sections fail framing closed;
    //   * the genuine emitter artifact asserts the same value above.
    {
        ModuleSpec base;
        base.caps = {{3, 300}};
        base.manifest_nodes = {{40, 1, 3, 300}};

        // Positive: exactly one memory, flags 0, min 1.
        {
            ModuleSpec s = base;
            s.memory_section_override = memory_payload(MemorySectionSpec{});
            const auto module = build_module(s);
            check(admit_ok(module), "memory.conforming_admits");
            auto admitted =
                make_verified_core_wasm_schema_module(std::span<const std::uint8_t>(module));
            check(admitted.ok(), "memory.conforming_admitted");
            if (admitted.ok()) {
                static_assert(
                    std::is_same_v<decltype(admitted.module->declared_linear_memory_capacity()),
                                   std::expected<ArtifactMemoryCapacity, MemoryDeclError>>,
                    "declared_linear_memory_capacity returns the typed expected");
                auto cap = admitted.module->declared_linear_memory_capacity();
                check(cap.has_value(), "memory.conforming_capacity_present");
                if (cap.has_value()) {
                    constexpr ArtifactMemoryCapacity kExpected{
                        ahfl::ir::core::kCoreWasmFixedLinearMemoryCapacityBytes,
                        ahfl::ir::core::kCoreWasmFixedLinearMemoryMinPages,
                        false};
                    check(*cap == kExpected, "memory.conforming_equals_ssot");
                    check(cap->capacity_bytes == 65536 && cap->min_pages == 1 && !cap->has_max,
                          "memory.conforming_one_page_no_max");
                }
            }
        }

        // Missing Memory section: module admits, getter fails closed.
        {
            const auto module = build_module(base);
            check(admit_ok(module), "memory.missing_section_admits");
            auto admitted =
                make_verified_core_wasm_schema_module(std::span<const std::uint8_t>(module));
            check(admitted.ok(), "memory.missing_section_admitted");
            if (admitted.ok()) {
                auto cap = admitted.module->declared_linear_memory_capacity();
                check(!cap.has_value() && cap.error() == MemoryDeclError::MissingMemorySection,
                      "memory.missing_section_error");
            }
        }

        // Memory count zero: well-formed section, contract fails closed.
        {
            ModuleSpec s = base;
            s.memory_section_override = memory_payload(MemorySectionSpec{.count = 0});
            const auto module = build_module(s);
            check(admit_ok(module), "memory.zero_count_admits");
            auto admitted =
                make_verified_core_wasm_schema_module(std::span<const std::uint8_t>(module));
            if (admitted.ok()) {
                auto cap = admitted.module->declared_linear_memory_capacity();
                check(!cap.has_value() && cap.error() == MemoryDeclError::MemoryCountNotOne,
                      "memory.zero_count_error");
            }
        }

        // Two memories: exact-consumed section, getter reports count != 1.
        {
            ModuleSpec s = base;
            s.memory_section_override = memory_payload(MemorySectionSpec{.count = 2});
            const auto module = build_module(s);
            check(admit_ok(module), "memory.two_count_admits");
            auto admitted =
                make_verified_core_wasm_schema_module(std::span<const std::uint8_t>(module));
            if (admitted.ok()) {
                auto cap = admitted.module->declared_linear_memory_capacity();
                check(!cap.has_value() && cap.error() == MemoryDeclError::MemoryCountNotOne,
                      "memory.two_count_error");
            }
        }

        // A declared maximum (flags 1) breaks the fixed-page contract.
        {
            ModuleSpec s = base;
            s.memory_section_override =
                memory_payload(MemorySectionSpec{.flags = 1, .min_pages = 1, .max_pages = 1});
            const auto module = build_module(s);
            check(admit_ok(module), "memory.with_max_admits");
            auto admitted =
                make_verified_core_wasm_schema_module(std::span<const std::uint8_t>(module));
            if (admitted.ok()) {
                auto cap = admitted.module->declared_linear_memory_capacity();
                check(!cap.has_value() && cap.error() == MemoryDeclError::DeclaredMaximum,
                      "memory.with_max_error");
            }
        }

        // Minimum of two pages (flags 0) breaks the single-page contract.
        {
            ModuleSpec s = base;
            s.memory_section_override = memory_payload(MemorySectionSpec{.min_pages = 2});
            const auto module = build_module(s);
            check(admit_ok(module), "memory.min_two_admits");
            auto admitted =
                make_verified_core_wasm_schema_module(std::span<const std::uint8_t>(module));
            if (admitted.ok()) {
                auto cap = admitted.module->declared_linear_memory_capacity();
                check(!cap.has_value() && cap.error() == MemoryDeclError::MinPagesNotOne,
                      "memory.min_two_error");
            }
        }

        // Structurally malformed Memory sections fail FRAMING (no module).
        {
            // reserved limits flags (2 = shared64-style, not the MVP 0/1).
            {
                ModuleSpec s = base;
                s.memory_section_override =
                    memory_payload(MemorySectionSpec{.flags = 2, .min_pages = 1});
                check(admit_fails(build_module(s)), "memory.malformed_flags");
            }
            // trailing byte after the one declared entry.
            {
                ModuleSpec s = base;
                s.memory_section_override =
                    memory_payload(MemorySectionSpec{.trailing_byte = true});
                check(admit_fails(build_module(s)), "memory.malformed_trailing");
            }
            // truncated minimum (count 1, flags byte, then nothing).
            {
                ModuleSpec s = base;
                s.memory_section_override = std::vector<std::uint8_t>{0x01, 0x00};
                check(admit_fails(build_module(s)), "memory.malformed_truncated_min");
            }
            // wasm validation violation: max < min.
            {
                ModuleSpec s = base;
                s.memory_section_override =
                    memory_payload(MemorySectionSpec{.flags = 1, .min_pages = 2, .max_pages = 1});
                check(admit_fails(build_module(s)), "memory.malformed_max_lt_min");
            }
            // huge count with no entries following.
            {
                ModuleSpec s = base;
                std::vector<std::uint8_t> bad;
                put_uleb(bad, 0xFFFFFFF0U);
                s.memory_section_override = bad;
                check(admit_fails(build_module(s)), "memory.malformed_huge_count");
            }
            // two Memory sections.
            {
                ModuleSpec s = base;
                s.memory_section_override = memory_payload(MemorySectionSpec{});
                s.duplicate_memory = true;
                check(admit_fails(build_module(s)), "memory.malformed_duplicate_section");
            }
        }
    }

    // ---- D1a-2: three raw artifact-digest getters ---------------------------
    // A deterministic baseline module (one capability, one cap node). Its exact raw
    // AHFLXM / AHFLWS payload ranges are located by the independent test walker, and
    // the three expected SHA-256s are hardcoded from an EXTERNAL hashlib recompute
    // over those exact byte slices (never produced by the getters or support::sha256
    // in this test).
    {
        ModuleSpec base;
        base.caps = {{0, 1}}; // (cap_id 0, source_symbol 1)
        base.entry_id = 7;
        base.manifest_nodes = {{40, 1, 0, 1}}; // one cap node -> cap 0, symbol 1
        const auto module = build_module(base);
        check(admit_ok(module), "digest.baseline_admits");

        const RawRange ahflws = locate_custom_payload(module, "ahfl.wire-schema.v1");
        const RawRange ahflxm = locate_custom_payload(module, "ahfl.wasm-exec-manifest.v1");
        check(!ahflws.empty() && !ahflxm.empty(), "digest.baseline_ranges_located");

        // Byte-lock the located raw slices for the fixed baseline. These HARD-CODED
        // vectors are independent of exec_manifest_body / encode_schema / the getter;
        // each begins with its raw magic (AHFLXM / AHFLWS) and carries NO custom-name
        // framing, so the byte-exact equality pins the exact digest scope.
        static const std::vector<std::uint8_t> kRawAhflxm = {
            0x41, 0x48, 0x46, 0x4c, 0x58, 0x4d, 0x01, 0x00,
            0x07, 0x01, 0x28, 0x00, 0x01, 0x00, 0x01}; // "AHFLXM"...
        static const std::vector<std::uint8_t> kRawAhflws = {
            0x41, 0x48, 0x46, 0x4c, 0x57, 0x53, 0x01, 0x02, 0x02,
            0x00, 0x04, 0x00, 0x01, 0x00, 0x01, 0x01, 0x00, 0x01}; // "AHFLWS"...
        const bool xm_slice_ok =
            !ahflxm.empty() && ahflxm.end <= module.size() &&
            std::vector<std::uint8_t>(module.begin() + static_cast<std::ptrdiff_t>(ahflxm.begin),
                                      module.begin() + static_cast<std::ptrdiff_t>(ahflxm.end)) ==
                kRawAhflxm;
        const bool ws_slice_ok =
            !ahflws.empty() && ahflws.end <= module.size() &&
            std::vector<std::uint8_t>(module.begin() + static_cast<std::ptrdiff_t>(ahflws.begin),
                                      module.begin() + static_cast<std::ptrdiff_t>(ahflws.end)) ==
                kRawAhflws;
        check(xm_slice_ok, "digest.raw_ahflxm_slice_byte_locked");
        check(ws_slice_ok, "digest.raw_ahflws_slice_byte_locked");

        // Externally-computed KATs: python hashlib.sha256(module[a:b]).digest() over
        // the exact byte-locked slices above (whole module / raw AHFLWS / raw AHFLXM).
        // Recompute: sha256(kModule)=9e6b8c..3b9e; sha256(kRawAhflws)=6cbb2b..0c5b;
        // sha256(kRawAhflxm)=6200b2..da28. NOT produced by the getter or support::sha256.
        static constexpr ArtifactDigest kModuleKat = {
            0x9e, 0x6b, 0x8c, 0x04, 0x52, 0xb7, 0x1b, 0x69, 0xeb, 0xb2, 0xe8,
            0xc4, 0x19, 0x9f, 0x44, 0xf6, 0x18, 0xff, 0x3a, 0x80, 0xa1, 0x62,
            0x36, 0x0e, 0xff, 0x35, 0x8c, 0xfb, 0x3b, 0x26, 0x3b, 0x9e};
        static constexpr ArtifactDigest kWireKat = {
            0x6c, 0xbb, 0x2b, 0xc1, 0x35, 0xd2, 0xe4, 0xaf, 0x2b, 0x56, 0x7e,
            0xe8, 0x20, 0x06, 0xf2, 0xf6, 0xdc, 0x85, 0xeb, 0x54, 0x63, 0x26,
            0x83, 0xcb, 0x36, 0xb0, 0x2f, 0x8b, 0x8d, 0x40, 0x0c, 0x5b};
        static constexpr ArtifactDigest kManifestKat = {
            0x62, 0x00, 0xb2, 0xda, 0xfc, 0x9b, 0xb7, 0x64, 0xca, 0xc0, 0x95,
            0x2c, 0x56, 0x23, 0x4e, 0x0c, 0x37, 0xc8, 0xa5, 0x7e, 0x9d, 0xbd,
            0x2f, 0xea, 0x35, 0x04, 0xc5, 0x44, 0xbd, 0xf8, 0xda, 0x28};

        auto admitted =
            make_verified_core_wasm_schema_module(std::span<const std::uint8_t>(module));
        check(admitted.ok(), "digest.baseline_admitted");
        if (admitted.ok()) {
            const auto &mod = *admitted.module;
            // Return-type / static contract: getters are ArtifactDigest by value.
            static_assert(std::is_same_v<decltype(mod.module_sha256()), ArtifactDigest>,
                          "module_sha256 returns ArtifactDigest by value");
            static_assert(std::is_same_v<decltype(mod.wire_schema_sha256()), ArtifactDigest>,
                          "wire_schema_sha256 returns ArtifactDigest by value");
            static_assert(std::is_same_v<decltype(mod.exec_manifest_sha256()), ArtifactDigest>,
                          "exec_manifest_sha256 returns ArtifactDigest by value");
            check(mod.module_sha256() == kModuleKat, "digest.module_kat");
            check(mod.wire_schema_sha256() == kWireKat, "digest.wire_kat");
            check(mod.exec_manifest_sha256() == kManifestKat, "digest.manifest_kat");

            // Copy + lifetime: copy the handle, resolve a token, drop the original;
            // both the copied handle's digests and the token's coordinates stay valid.
            auto copy = mod;
            auto node = mod.resolve_node(ManifestNodeIndex{0});
            admitted = VerifiedCoreWasmSchemaModuleResult{}; // drop the original handle
            check(copy.module_sha256() == kModuleKat && copy.wire_schema_sha256() == kWireKat &&
                      copy.exec_manifest_sha256() == kManifestKat,
                  "digest.copy_stable_after_original_drop");
            // Actually READ the token after the original handle is gone (shared payload).
            check(node.ok() && node.node.has_value() &&
                      node.node->workflow_node_id() == CoreWorkflowNodeId{40} &&
                      node.node->schedule_pos() == ManifestNodeIndex{0},
                  "digest.token_reads_after_original_drop");
        }

        // Mutation 1 (module-only): an accepted unknown custom section BEFORE AHFLXM;
        // its body byte 0 -> 1. Admits OK, exactly one byte differs, that offset is in
        // the whole module but OUTSIDE both raw payloads -> only module digest flips.
        {
            ModuleSpec m0 = base;
            m0.pre_manifest_custom_body = std::vector<std::uint8_t>{0x00};
            ModuleSpec m1 = base;
            m1.pre_manifest_custom_body = std::vector<std::uint8_t>{0x01};
            const auto b0 = build_module(m0);
            const auto b1 = build_module(m1);
            check(admit_ok(b0) && admit_ok(b1), "digest.mod_only_both_admit");
            std::size_t off = 0;
            std::size_t cnt = 0;
            const bool ok = single_byte_diff(b0, b1, off, cnt);
            check(ok && cnt == 1, "digest.mod_only_one_byte_diff");
            const RawRange ws = locate_custom_payload(b0, "ahfl.wire-schema.v1");
            const RawRange xm = locate_custom_payload(b0, "ahfl.wasm-exec-manifest.v1");
            check(ok && cnt == 1 && !ws.contains(off) && !xm.contains(off),
                  "digest.mod_only_offset_outside_payloads");
            auto a0 = make_verified_core_wasm_schema_module(std::span<const std::uint8_t>(b0));
            auto a1 = make_verified_core_wasm_schema_module(std::span<const std::uint8_t>(b1));
            check(a0.ok() && a1.ok(), "digest.mod_only_admitted");
            if (a0.ok() && a1.ok()) {
                check(a0.module->module_sha256() != a1.module->module_sha256(),
                      "digest.mod_only_module_flips");
                check(a0.module->wire_schema_sha256() == a1.module->wire_schema_sha256() &&
                          a0.module->exec_manifest_sha256() == a1.module->exec_manifest_sha256(),
                      "digest.mod_only_wire_manifest_stable");
            }
        }

        // Mutation 2 (AHFLWS): the unbounded Result node String -> Int (both with a
        // 0 optional-bounds tag), via schema_override. Admits OK, exactly one byte
        // differs inside the raw AHFLWS payload -> module + wire flip, manifest stable.
        {
            CoreWireSchemaTable str_table;
            str_table.nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}});    // 0: param
            str_table.nodes.push_back(CoreWireSchemaNode{CoreWireSchemaString{}}); // 1: Result
            CoreWireCapabilitySchema c_str;
            c_str.capability = CoreCapabilityId{0};
            c_str.source_symbol = 1;
            c_str.params = {CoreWireSchemaNodeId{0}};
            c_str.result = CoreWireSchemaNodeId{1};
            str_table.capabilities.push_back(c_str);

            CoreWireSchemaTable int_table = str_table;
            int_table.nodes[1] = CoreWireSchemaNode{CoreWireSchemaInt{}}; // Result String -> Int

            ModuleSpec ms = base;
            ms.schema_override = encode_schema(str_table);
            ModuleSpec mi = base;
            mi.schema_override = encode_schema(int_table);
            check(!ms.schema_override->empty() && !mi.schema_override->empty(),
                  "digest.ws_schema_encoded");
            const auto bs = build_module(ms);
            const auto bi = build_module(mi);
            check(admit_ok(bs) && admit_ok(bi), "digest.ws_both_admit");
            std::size_t off = 0;
            std::size_t cnt = 0;
            const bool ok = single_byte_diff(bs, bi, off, cnt);
            check(ok && cnt == 1, "digest.ws_one_byte_diff");
            const RawRange ws = locate_custom_payload(bs, "ahfl.wire-schema.v1");
            check(ok && cnt == 1 && ws.contains(off), "digest.ws_offset_in_wire_range");
            auto as = make_verified_core_wasm_schema_module(std::span<const std::uint8_t>(bs));
            auto ai = make_verified_core_wasm_schema_module(std::span<const std::uint8_t>(bi));
            check(as.ok() && ai.ok(), "digest.ws_admitted");
            if (as.ok() && ai.ok()) {
                check(as.module->wire_schema_sha256() != ai.module->wire_schema_sha256() &&
                          as.module->module_sha256() != ai.module->module_sha256(),
                      "digest.ws_module_and_wire_flip");
                check(as.module->exec_manifest_sha256() == ai.module->exec_manifest_sha256(),
                      "digest.ws_manifest_stable");
            }
        }

        // Mutation 3 (AHFLXM): entry_id 7 -> 8 via manifest entry. Admits OK, exactly
        // one byte differs inside the raw AHFLXM payload -> module + manifest flip,
        // wire stable.
        {
            ModuleSpec m7 = base; // entry_id 7
            ModuleSpec m8 = base;
            m8.entry_id = 8;
            const auto b7 = build_module(m7);
            const auto b8 = build_module(m8);
            check(admit_ok(b7) && admit_ok(b8), "digest.xm_both_admit");
            std::size_t off = 0;
            std::size_t cnt = 0;
            const bool ok = single_byte_diff(b7, b8, off, cnt);
            check(ok && cnt == 1, "digest.xm_one_byte_diff");
            const RawRange xm = locate_custom_payload(b7, "ahfl.wasm-exec-manifest.v1");
            check(ok && cnt == 1 && xm.contains(off), "digest.xm_offset_in_manifest_range");
            auto a7 = make_verified_core_wasm_schema_module(std::span<const std::uint8_t>(b7));
            auto a8 = make_verified_core_wasm_schema_module(std::span<const std::uint8_t>(b8));
            check(a7.ok() && a8.ok(), "digest.xm_admitted");
            if (a7.ok() && a8.ok()) {
                check(a7.module->exec_manifest_sha256() != a8.module->exec_manifest_sha256() &&
                          a7.module->module_sha256() != a8.module->module_sha256(),
                      "digest.xm_module_and_manifest_flip");
                check(a7.module->wire_schema_sha256() == a8.module->wire_schema_sha256(),
                      "digest.xm_wire_stable");
            }
        }
    }

    if (g_failures == 0) {
        std::cout << "core_wasm_schema_module: all checks passed\n";
        return 0;
    }
    std::cerr << "core_wasm_schema_module: " << g_failures << " failure(s)\n";
    return 1;
}
