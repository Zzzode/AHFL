#pragma once

// RFC 0026 KR6.5 E4-B2-D2a (F4): SHARED hand-built fixture builders for the
// durable-resume replay tests. Promoted verbatim (into a named namespace) from
// the D1b controller regression TU's anonymous namespace so the production
// resume-host driver test (F4) and the Node embedded-engine e2e (F5) reuse the
// SAME canonical module/event/store builders instead of duplicating them
// (CLAUDE.md Principle 1: one fixture authority, no parallel copies).
//
// Everything here is TEST-SUPPORT evidence infrastructure, never production
// code: the module bytes are emitter-free hand-built wasm sections (the same
// canonical two-section fixture cluster the A2 schema-module test uses), the
// node-event memory is a hand-built 40-byte-record synthesizer, and the store
// helpers open a REAL IntegrityPayloadStore (Linux durable FS only; a TU that
// cannot open one SKIPs with exit 77).

#include "ahfl/compiler/ir/core_wire_schema.hpp"
#include "ahfl/runtime/ahfl_host.h"
#include "base/json/json_value.hpp"
#include "base/support/sha256.hpp"
#include "runtime/engine/core_wasm_resume_controller.hpp"
#include "runtime/engine/core_wasm_resume_record.hpp"
#include "runtime/engine/core_wasm_schema_module.hpp"
#include "runtime/engine/core_wire_codec.hpp"
#include "runtime/engine/payload_store.hpp"
#include "runtime/evaluator/value.hpp"
#include "runtime/evaluator/value_json.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ahfl::runtime::resume_test_support {

namespace rc = ahfl::runtime::core_wasm_resume_controller;
namespace ps = ahfl::runtime::payload_store;
namespace csm = ahfl::runtime::core_wasm_schema_module;

using ahfl::ir::core::CoreCapabilityId;
using ahfl::ir::core::CoreWireCapabilitySchema;
using ahfl::ir::core::CoreWireSchemaInt;
using ahfl::ir::core::CoreWireSchemaNode;
using ahfl::ir::core::CoreWireSchemaNodeId;
using ahfl::ir::core::CoreWireSchemaSequence;
using ahfl::ir::core::CoreWireSchemaString;
using ahfl::ir::core::CoreWireSchemaTable;
using ahfl::ir::core::CoreWireSequenceKind;
using ahfl::runtime::core_wasm_resume::CoreWasmResumeRecord;
using ahfl::runtime::core_wasm_resume::DigestHex;
using ahfl::runtime::core_wasm_resume::InvocationOrdinal;
using ahfl::runtime::core_wasm_resume::NodeKind;
using ahfl::runtime::core_wasm_resume::PayloadSlotId;
using ahfl::runtime::core_wasm_resume::ResumeMemoEntry;
using ahfl::runtime::core_wasm_resume::ResumeNode;
using ahfl::runtime::core_wasm_resume::ResumePendingEntry;
using ahfl::runtime::core_wasm_resume::ResumeState;
using ahfl::runtime::payload_store::ResumeCheckpointId;
using ahfl::runtime::core_wasm_schema_module::ArtifactDigest;
using ahfl::runtime::core_wasm_schema_module::make_verified_core_wasm_schema_module;
using ahfl::runtime::core_wasm_schema_module::VerifiedCoreWasmSchemaModule;

namespace fs = std::filesystem;

// ==== emitter-free canonical module byte builders (NOT a production authority) ====

inline void put_uleb(std::vector<std::uint8_t> &out, std::uint64_t value) {
    do {
        auto b = static_cast<std::uint8_t>(value & 0x7fU);
        value >>= 7U;
        if (value != 0) {
            b |= 0x80U;
        }
        out.push_back(b);
    } while (value != 0);
}

inline void put_section(std::vector<std::uint8_t> &out, std::uint8_t id,
                        const std::vector<std::uint8_t> &payload) {
    out.push_back(id);
    put_uleb(out, payload.size());
    out.insert(out.end(), payload.begin(), payload.end());
}

inline std::vector<std::uint8_t> func_type(const std::vector<std::uint8_t> &params,
                                           const std::vector<std::uint8_t> &results) {
    std::vector<std::uint8_t> t;
    t.push_back(0x60);
    put_uleb(t, params.size());
    t.insert(t.end(), params.begin(), params.end());
    put_uleb(t, results.size());
    t.insert(t.end(), results.begin(), results.end());
    return t;
}

inline std::vector<std::uint8_t>
type_payload(const std::vector<std::vector<std::uint8_t>> &types) {
    std::vector<std::uint8_t> p;
    put_uleb(p, types.size());
    for (const auto &t : types) {
        p.insert(p.end(), t.begin(), t.end());
    }
    return p;
}

inline std::vector<std::uint8_t>
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

inline std::vector<std::uint8_t> custom_payload(const std::string &name,
                                                const std::vector<std::uint8_t> &body) {
    std::vector<std::uint8_t> p;
    put_uleb(p, name.size());
    p.insert(p.end(), name.begin(), name.end());
    p.insert(p.end(), body.begin(), body.end());
    return p;
}

struct ManifestNodeSpec {
    std::uint32_t workflow_node_id;
    std::uint8_t cap_call_count; // 0 identity or 1 capability
    std::uint32_t capability;
    std::uint64_t source_symbol;
};

inline std::vector<std::uint8_t> exec_manifest_body(std::uint32_t entry_id,
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

struct CapSpec {
    std::uint32_t cap_id;
    std::uint64_t symbol;
    CoreWireSchemaNodeId result;
};

inline std::vector<std::uint8_t> encode_schema(const CoreWireSchemaTable &table) {
    auto enc = ahfl::ir::core::encode_core_wire_schema_table(table);
    if (!enc.ok() || !enc.bytes.has_value()) {
        return {};
    }
    return *enc.bytes;
}

// Node 0 = Int param; caller-supplied extra nodes append after it. Each cap uses param
// node 0 and its own result node id.
inline CoreWireSchemaTable schema_table(std::vector<CoreWireSchemaNode> extra_nodes,
                                        const std::vector<CapSpec> &caps) {
    CoreWireSchemaTable table;
    table.nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}}); // 0: Int param
    for (auto &n : extra_nodes) {
        table.nodes.push_back(std::move(n));
    }
    for (const auto &c : caps) {
        CoreWireCapabilitySchema cs;
        cs.capability = CoreCapabilityId{c.cap_id};
        cs.source_symbol = c.symbol;
        cs.params = {CoreWireSchemaNodeId{0}};
        cs.result = c.result;
        table.capabilities.push_back(cs);
    }
    return table;
}

struct ModuleSpec {
    std::uint32_t entry_id = 7;
    std::vector<CapSpec> caps;
    std::vector<ManifestNodeSpec> nodes;
    std::vector<CoreWireSchemaNode> extra_schema_nodes;
};

inline std::vector<std::uint8_t> build_module(const ModuleSpec &spec) {
    std::vector<std::uint8_t> m = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
    put_section(m, 1, type_payload({func_type({0x7f, 0x7f}, {0x7f, 0x7f, 0x7f})}));
    std::vector<std::pair<std::uint64_t, std::uint32_t>> imports;
    for (const auto &c : spec.caps) {
        imports.emplace_back(c.symbol, 0u);
    }
    put_section(m, 2, import_payload(imports));
    put_section(m, 0,
                custom_payload("ahfl.wasm-exec-manifest.v1",
                               exec_manifest_body(spec.entry_id, spec.nodes)));
    put_section(m, 0,
                custom_payload("ahfl.wire-schema.v1",
                               encode_schema(schema_table(spec.extra_schema_nodes, spec.caps))));
    return m;
}

// Build + verify a module spec in one step.
inline csm::VerifiedCoreWasmSchemaModuleResult admit_module(const ModuleSpec &spec) {
    const auto bytes = build_module(spec);
    return make_verified_core_wasm_schema_module(std::span<const std::uint8_t>(bytes));
}

// schema node builders
inline CoreWireSchemaNode int_node() { return CoreWireSchemaNode{CoreWireSchemaInt{}}; }
inline CoreWireSchemaNode bounded_string_node(std::int64_t max_bytes) {
    CoreWireSchemaString s;
    s.length_bounds = std::pair<std::int64_t, std::int64_t>{0, max_bytes};
    return CoreWireSchemaNode{s};
}
inline CoreWireSchemaNode unbounded_string_node() {
    return CoreWireSchemaNode{CoreWireSchemaString{}};
}
inline CoreWireSchemaNode list_node(std::uint32_t elem, std::optional<std::uint64_t> cap) {
    CoreWireSchemaSequence s;
    s.kind = CoreWireSequenceKind::List;
    s.element = CoreWireSchemaNodeId{elem};
    s.capacity = cap;
    return CoreWireSchemaNode{s};
}

// ==== digest + record + store helpers ====

inline DigestHex hex_of_digest(const ArtifactDigest &raw) {
    static const char *k = "0123456789abcdef";
    DigestHex out{};
    for (std::size_t i = 0; i < raw.size(); ++i) {
        out[2 * i] = k[(raw[i] >> 4) & 0xF];
        out[2 * i + 1] = k[raw[i] & 0xF];
    }
    return out;
}

inline std::vector<std::uint8_t> test_key() {
    return std::vector<std::uint8_t>(32, 0x2b);
}
inline std::array<std::uint8_t, 16> test_key_id() {
    std::array<std::uint8_t, 16> id{};
    for (std::size_t i = 0; i < id.size(); ++i) {
        id[i] = static_cast<std::uint8_t>(i + 1);
    }
    return id;
}

inline ps::StoreOptions big_opts() {
    ps::StoreOptions o;
    o.limits = ps::StoreLimits{1u << 20, 1u << 20};
    return o;
}

inline void set_matching_digests(CoreWasmResumeRecord &r,
                                 const VerifiedCoreWasmSchemaModule &mod) {
    r.module_sha256 = hex_of_digest(mod.module_sha256());
    r.wire_schema_sha256 = hex_of_digest(mod.wire_schema_sha256());
    r.exec_manifest_sha256 = hex_of_digest(mod.exec_manifest_sha256());
}

// The frontier arg_hash the controller computes for a given Int param JSON against a
// call site's Param binding (mirrors the controller: decode_json -> hash_values).
inline std::uint64_t param_arg_hash(const VerifiedCoreWasmSchemaModule &mod,
                                   std::size_t call_site, std::string_view param_json) {
    auto cs = mod.resolve(csm::ManifestCallSiteIndex{call_site});
    if (!cs.ok()) {
        return 0;
    }
    auto dom = ahfl::json::parse_json(param_json);
    if (!dom.has_value()) {
        return 0;
    }
    auto decoded = ahfl::runtime::wire_codec::decode_json(**dom, cs.call_site->param_binding());
    if (!decoded.ok()) {
        return 0;
    }
    std::vector<ahfl::evaluator::Value> v;
    v.push_back(std::move(*decoded.value));
    return ahfl::evaluator::hash_values(v);
}

// D2b-4: the controller's canonical typed Param digest for a call site --
// Verified-decode the param JSON against the A2 Param binding, then SHA-256 the
// canonical wire-JSON bytes (the same SSOT value_to_json the production seam
// hashes). Mirrors the controller so a test can seal an effect under the exact
// token the controller will consult.
inline ahfl::support::Sha256Digest
param_canonical_digest(const VerifiedCoreWasmSchemaModule &mod, std::size_t call_site,
                       std::string_view param_json) {
    auto cs = mod.resolve(csm::ManifestCallSiteIndex{call_site});
    auto dom = ahfl::json::parse_json(param_json);
    auto decoded = ahfl::runtime::wire_codec::decode_json(**dom, cs.call_site->param_binding());
    const std::string canonical = ahfl::evaluator::value_to_json(*decoded.value);
    return ahfl::support::sha256(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(canonical.data()),
                                      canonical.size()));
}

// ---- node-event linear-memory synthesizer ----
constexpr std::size_t kPageBytes = 65536;
constexpr std::uint32_t kEventLogBase = 1024;
constexpr std::uint32_t kEventRecordsBase = 1032;
constexpr std::uint32_t kRecordBytes = 40;

inline void put_u32(std::vector<std::uint8_t> &m, std::size_t off, std::uint32_t v) {
    m[off] = static_cast<std::uint8_t>(v & 0xFF);
    m[off + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    m[off + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
    m[off + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
}
inline void put_u64(std::vector<std::uint8_t> &m, std::size_t off, std::uint64_t v) {
    for (std::size_t i = 0; i < 8; ++i) {
        m[off + i] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF);
    }
}

inline std::vector<std::uint8_t> event_memory(const std::vector<ManifestNodeSpec> &nodes,
                                              std::uint32_t count) {
    std::vector<std::uint8_t> m(kPageBytes, 0);
    put_u32(m, kEventLogBase, count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto &n = nodes[i];
        const std::size_t base = kEventRecordsBase + static_cast<std::size_t>(i) * kRecordBytes;
        const bool is_cap = n.cap_call_count == 1;
        m[base + 0] = is_cap ? 1 : 0;
        put_u32(m, base + 4, n.workflow_node_id);
        put_u32(m, base + 8, i);
        if (is_cap) {
            put_u32(m, base + 12, n.capability);
            put_u64(m, base + 16, n.source_symbol);
            put_u64(m, base + 24, 0);
        }
        put_u32(m, base + 32, 0);
    }
    return m;
}

// Write `count` node-event records into an EXISTING exact-page memory buffer (the
// engine-port fixtures need the records inside memory that also carries transferred
// frames, rather than a fresh all-zero page).
inline void write_event_records(std::vector<std::uint8_t> &page,
                                const std::vector<ManifestNodeSpec> &nodes,
                                std::uint32_t count) {
    put_u32(page, kEventLogBase, count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto &n = nodes[i];
        const std::size_t base = kEventRecordsBase + static_cast<std::size_t>(i) * kRecordBytes;
        const bool is_cap = n.cap_call_count == 1;
        page[base + 0] = is_cap ? 1 : 0;
        page[base + 1] = 0;
        page[base + 2] = 0;
        page[base + 3] = 0;
        put_u32(page, base + 4, n.workflow_node_id);
        put_u32(page, base + 8, i);
        if (is_cap) {
            put_u32(page, base + 12, n.capability);
            put_u64(page, base + 16, n.source_symbol);
            put_u64(page, base + 24, 0);
        }
        put_u32(page, base + 32, 0);
        put_u32(page, base + 36, 0);
    }
}

inline const std::string kIntParamJson{"1"};
inline const std::string kStringResultJson{"\"ok\""};
inline const std::vector<std::uint8_t> kEntryBytes = {0x01, 0x02, 0x03};

inline void nuke(const fs::path &p) {
    std::error_code ec;
    fs::remove_all(p, ec);
}

inline std::optional<ps::IntegrityPayloadStore> open_store(const fs::path &work) {
    nuke(work);
    std::error_code ec;
    fs::create_directories(work, ec);
    auto s = ps::IntegrityPayloadStore::open(work, big_opts());
    if (!s.has_value()) {
        return std::nullopt;
    }
    return std::move(*s);
}

// Flip the first byte of every on-disk artifact file under `work` whose name contains
// `marker` (e.g. "slot"), changing the artifact's whole-file digest. At admission the
// store's manifest digest cross-check (which runs BEFORE the HMAC decode) fails with
// StateMismatch. Test-only tamper hook; it does NOT re-sign the manifest, so it exercises
// the digest cross-check arm, not the HMAC IntegrityFailed arm.
inline void corrupt_artifacts(const fs::path &work, std::string_view marker) {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(work, ec);
         !ec && it != fs::recursive_directory_iterator(); ++it) {
        if (!it->is_regular_file(ec)) {
            continue;
        }
        if (it->path().filename().string().find(marker) == std::string::npos) {
            continue;
        }
        std::error_code sec;
        if (fs::file_size(it->path(), sec) == 0 || sec) {
            continue;
        }
        std::FILE *f = std::fopen(it->path().string().c_str(), "r+b");
        if (f == nullptr) {
            continue;
        }
        unsigned char b = 0;
        std::fseek(f, 0, SEEK_SET);
        if (std::fread(&b, 1, 1, f) == 1) {
            b = static_cast<unsigned char>(b ^ 0xFF);
            std::fseek(f, 0, SEEK_SET);
            std::fwrite(&b, 1, 1, f);
        }
        std::fclose(f);
    }
}

// ---- typed-error assertion helpers ----
inline bool is_prepare_reason(const rc::ResumePrepareError &e, rc::ResumePrepareReason want) {
    return std::holds_alternative<rc::ResumePrepareReason>(e) &&
           std::get<rc::ResumePrepareReason>(e) == want;
}
inline bool is_step_reason(const rc::ResumeStepError &e, rc::ResumeStepReason want) {
    return std::holds_alternative<rc::ResumeStepReason>(e) &&
           std::get<rc::ResumeStepReason>(e) == want;
}
inline bool is_store_error(const rc::ResumeStepError &e, ps::PayloadStoreError want) {
    return std::holds_alternative<ps::PayloadStoreError>(e) &&
           std::get<ps::PayloadStoreError>(e) == want;
}
inline bool is_dedup_backend_error(
    const rc::ResumeStepError &e,
    ahfl::runtime::durable_effect_authority::DurableEffectBackendError want) {
    return std::holds_alternative<
               ahfl::runtime::durable_effect_authority::DurableEffectBackendError>(e) &&
           std::get<ahfl::runtime::durable_effect_authority::DurableEffectBackendError>(e) ==
               want;
}

} // namespace ahfl::runtime::resume_test_support
