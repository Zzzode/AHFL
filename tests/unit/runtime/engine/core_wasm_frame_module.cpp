#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/core_frame_layout.hpp"
#include "ahfl/compiler/ir/core_layout.hpp"
#include "ahfl/compiler/ir/core_wasm_abi_constants.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/validate.hpp"
#include "compiler/backends/infra/core_wasm_codegen.hpp"
#include "common/project_input_support.hpp"
#include "base/support/sha256.hpp"
#include "runtime/engine/core_wasm_frame_module.hpp"

// RFC 0026 P6-7 rung A: host-side admission of the `ahfl.core-layout.v1` +
// boundary-root `ahfl.wire-schema.v1` section pair from REAL emitted P6-frame
// modules. Proves module-side and host-side readers agree over the
// digest-authenticated module bytes, plus fail-closed framing negatives
// (truncated, unknown intervening section, reordered/duplicated sections).

namespace {

using namespace ahfl;

int g_failures = 0;

void check(bool ok, std::string_view name) {
    if (!ok) {
        ++g_failures;
        std::cerr << "FAIL: " << name << "\n";
    }
}

namespace fr = runtime::core_wasm_frame_module;

[[nodiscard]] std::optional<backends::CoreWasmCodegenResult>
emit_fixture_full(const std::filesystem::path &fixture) {
    const Frontend frontend;
    const auto parse = frontend.parse_file(fixture);
    if (parse.has_errors() || parse.program == nullptr) {
        return std::nullopt;
    }
    const Resolver resolver;
    const auto resolve = resolver.resolve(*parse.program);
    if (resolve.has_errors()) {
        return std::nullopt;
    }
    const TypeChecker checker;
    const auto typecheck = checker.check(*parse.program, resolve);
    if (typecheck.has_errors()) {
        return std::nullopt;
    }
    const Validator validator;
    const auto validation = validator.validate(*parse.program, resolve, typecheck);
    if (validation.has_errors()) {
        return std::nullopt;
    }
    const auto ir = lower_program_ir(*parse.program, resolve, typecheck);
    const auto core = ir::core::lower_ahfl_to_core(ir);
    if (!core.ok()) {
        return std::nullopt;
    }
    const auto layouts = ir::core::compute_core_layouts(core.program);
    if (!layouts.ok() || !layouts.table.has_value()) {
        return std::nullopt;
    }
    return backends::emit_core_wasm(
        core.program, *layouts.table,
        {ir::core::CoreAgentId{0}, backends::WasmProfileKind::Wasi});
}

[[nodiscard]] std::optional<std::vector<std::uint8_t>>
emit_fixture(const std::filesystem::path &fixture) {
    auto emitted = emit_fixture_full(fixture);
    if (!emitted.has_value() || !emitted->artifact.has_value()) {
        return std::nullopt;
    }
    return emitted->artifact->bytes;
}

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

[[nodiscard]] std::vector<std::uint8_t>
make_custom_section(std::string_view name, std::span<const std::uint8_t> body) {
    std::vector<std::uint8_t> payload;
    put_uleb(payload, name.size());
    payload.insert(payload.end(), name.begin(), name.end());
    payload.insert(payload.end(), body.begin(), body.end());
    std::vector<std::uint8_t> record;
    record.push_back(0); // custom section id
    put_uleb(record, payload.size());
    record.insert(record.end(), payload.begin(), payload.end());
    return record;
}

void append_custom(std::vector<std::uint8_t> &module, std::string_view name,
                   std::span<const std::uint8_t> body) {
    const auto record = make_custom_section(name, body);
    module.insert(module.end(), record.begin(), record.end());
}

[[nodiscard]] std::optional<std::uint64_t> read_uleb(std::span<const std::uint8_t> bytes,
                                                     std::size_t &pos) {
    std::uint64_t value = 0;
    std::uint32_t shift = 0;
    while (pos < bytes.size()) {
        const std::uint8_t cur = bytes[pos++];
        if (shift >= 64) {
            return std::nullopt;
        }
        value |= static_cast<std::uint64_t>(cur & 0x7fU) << shift;
        if ((cur & 0x80U) == 0) {
            return value;
        }
        shift += 7;
    }
    return std::nullopt;
}

// Return the [start,end) byte span of the named custom section (id byte through
// the end of its payload), or nullopt.
[[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>>
find_custom_section_span(std::span<const std::uint8_t> module_bytes, std::string_view wanted) {
    if (module_bytes.size() < 8) {
        return std::nullopt;
    }
    std::size_t pos = 8;
    while (pos < module_bytes.size()) {
        const std::size_t start = pos;
        const std::uint8_t id = module_bytes[pos++];
        auto size = read_uleb(module_bytes, pos);
        if (!size.has_value() || pos + *size > module_bytes.size()) {
            return std::nullopt;
        }
        const std::size_t content = pos;
        pos = content + static_cast<std::size_t>(*size);
        if (id != 0) {
            continue;
        }
        std::size_t q = content;
        auto name_len = read_uleb(module_bytes, q);
        if (!name_len.has_value() || q + *name_len <= module_bytes.size()) {
            const std::string_view name(
                reinterpret_cast<const char *>(module_bytes.data() + q), *name_len);
            if (name == wanted) {
                return std::pair{start, pos};
            }
        }
    }
    return std::nullopt;
}

namespace irc = ahfl::ir::core;

// A self-consistent acyclic one-field-struct chain S_{i} { f0: S_{i-1} }
// terminating in an i32 leaf, expressed in BOTH the P4-D layout table and the
// parallel logical wire schema. The host consistency checker walks untrusted
// decoded graphs, so a legal but deep acyclic chain must be handled WITHOUT C++
// recursion (a million-deep chain used to SIGSEGV the host at admission).
struct ChainTables {
    irc::CoreLayoutTable layouts;
    irc::CoreLayoutId layout_root{};
    irc::CoreWireSchemaTable wire;
    irc::CoreWireSchemaNodeId wire_root{};
};

[[nodiscard]] ChainTables make_struct_chain(std::uint32_t depth) {
    ChainTables out;
    out.layouts.target = irc::TargetDataLayout{};
    out.wire.format_version = 1;
    // Node 0: the narrow-Int i32 leaf shared by layout and schema.
    out.layouts.layouts.push_back(
        irc::CoreLayout{4, 4, false, irc::CoreLayoutScalar{irc::CoreScalarRepr::I32}});
    irc::CoreWireSchemaNode leaf;
    leaf.shape = irc::CoreWireSchemaInt{
        std::pair<std::int64_t, std::int64_t>{-2147483648LL, 2147483647LL}};
    out.wire.nodes.push_back(leaf);
    for (std::uint32_t i = 1; i <= depth; ++i) {
        const irc::CoreLayoutId child{i - 1U};
        out.layouts.layouts.push_back(irc::CoreLayout{
            4, 4, false,
            irc::CoreLayoutStruct{std::vector<std::uint64_t>{0},
                                  std::vector<irc::CoreLayoutId>{child}}});
        irc::CoreWireSchemaNode node;
        auto shape = irc::CoreWireSchemaStruct{};
        shape.wire_name = "S" + std::to_string(i);
        shape.fields.push_back(
            irc::CoreWireSchemaField{"f0", irc::CoreWireSchemaNodeId{i - 1U}});
        node.shape = std::move(shape);
        out.wire.nodes.push_back(std::move(node));
    }
    out.layout_root = irc::CoreLayoutId{depth};
    out.wire_root = irc::CoreWireSchemaNodeId{depth};
    return out;
}

// A minimal valid frame-layout section: one i64 scalar element layout (node 0),
// one bounded `List<Int>(4)` container layout (node 1, stride 8, backing 32),
// both boundary roots at the container, one placement [16384, +32), arena at
// 16416. Canonical as built; tests mutate copies to prove host re-derivation.
[[nodiscard]] irc::CoreFrameLayoutSection make_container_section() {
    irc::CoreFrameLayoutSection section;
    section.table.target = irc::TargetDataLayout{};
    section.table.layouts.push_back(
        irc::CoreLayout{8, 8, false, irc::CoreLayoutScalar{irc::CoreScalarRepr::I64}});
    section.table.layouts.push_back(
        irc::CoreLayout{8, 4, false,
                        irc::CoreLayoutContainer{irc::CoreLayoutId{0}, std::nullopt,
                                                 /*capacity=*/4, /*stride=*/8,
                                                 /*value_offset=*/0, /*backing_size=*/32}});
    section.input_layout = irc::CoreLayoutId{1};
    section.output_layout = irc::CoreLayoutId{1};
    section.placements.push_back(
        irc::CoreFrameBackingPlacement{/*edge_index=*/0, /*container_layout=*/{1},
                                       /*base=*/16384, /*extent=*/32});
    section.payload_arena_base = 16416;
    section.payload_arena_capacity = 0;
    return section;
}

// Raw frame-layout payload prefix (magic/version/target + the two boundary
// roots), followed by whatever count/payload bytes a test appends. A giant
// count placed here must fail bounded_count BEFORE any vector reserve.
[[nodiscard]] std::vector<std::uint8_t>
frame_payload_prefix_with_roots(std::uint32_t input_root, std::uint32_t output_root) {
    std::vector<std::uint8_t> bytes{'A', 'H', 'F', 'L', 'C', 'L', /*version=*/1,
                                    /*target=*/0};
    put_uleb(bytes, input_root);
    put_uleb(bytes, output_root);
    return bytes;
}

} // namespace

int main() {
    const auto repo = test_support::repo_root_from_source_file(__FILE__);

    for (const char *rel : {"tests/golden/wasm/p6_aggregate.ahfl",
                            "tests/golden/wasm/p6_collection.ahfl"}) {
        const auto bytes = emit_fixture(repo / rel);
        check(bytes.has_value(), std::string("emit ") + rel);
        if (!bytes.has_value()) {
            continue;
        }

        // The real P6-frame module must carry BOTH sections and admit cleanly.
        auto admitted = fr::admit_core_wasm_frame_sections(*bytes);
        check(admitted.ok(), std::string("admit ") + rel);
        if (admitted.ok()) {
            const auto &s = *admitted.sections;
            check(s.layout.input_layout.value < s.layout.table.layouts.size(),
                  std::string(rel) + " input root resolves");
            check(s.layout.output_layout.value < s.layout.table.layouts.size(),
                  std::string(rel) + " output root resolves");
            // Both frame bindings were minted and root at the boundary nodes.
            check(s.input_binding.root().value <
                      s.input_binding.table().nodes.size(),
                  std::string(rel) + " input binding rooted at a schema node");
            check(s.output_binding.root().value <
                      s.output_binding.table().nodes.size(),
                  std::string(rel) + " output binding rooted at a schema node");
            check(s.module_sha256 == fr::ArtifactDigest(support::sha256(*bytes)),
                  std::string(rel) + " module digest matches parsed bytes");
            check(s.core_layout_sha256 != s.wire_schema_sha256,
                  std::string(rel) + " two section digests differ");
        }

        // Negative: truncated module fails closed.
        std::vector<std::uint8_t> truncated(bytes->begin(), bytes->end() - 4);
        check(!fr::admit_core_wasm_frame_sections(truncated).ok(),
              std::string(rel) + " truncated module rejected");

        // Negative: an unknown trailing custom section after the schema breaks
        // the schema-must-be-final rule.
        {
            auto tampered = *bytes;
            static constexpr std::array<std::uint8_t, 1> body{0x00};
            append_custom(tampered, "unknown.trailing", body);
            check(!fr::admit_core_wasm_frame_sections(tampered).ok(),
                  std::string(rel) + " trailing custom section rejected");
        }
    }

    // Negative: an identity E1 module carries NO frame sections and must fail
    // admission (rather than be silently treated as a frame module).
    {
        const auto bytes = emit_fixture(repo / "tests/golden/wasm/e1_identity_agent.ahfl");
        check(bytes.has_value(), "emit identity agent fixture");
        if (bytes.has_value()) {
            check(!fr::admit_core_wasm_frame_sections(*bytes).ok(),
                  "identity module without frame sections rejected");
        }
    }

    // RFC 0026 P6-7 fix-forward, finding 1: TWO independently-live bounded
    // containers of the SAME value type hash-cons to one source layout node, yet
    // each fixed-edge occurrence must receive its OWN DISJOINT backing
    // placement (design section 6.2). The emitter KAT pins two placements and
    // the host must admit the section.
    {
        const auto emitted =
            emit_fixture_full(repo / "tests/golden/wasm/p6_frame_two_containers.ahfl");
        check(emitted.has_value() && emitted->ok(), "emit two-containers fixture");
        if (emitted.has_value() && emitted->ok() && emitted->artifact.has_value() &&
            emitted->descriptor.has_value() && emitted->descriptor->frame.has_value()) {
            const auto &lane = *emitted->descriptor->frame;
            check(lane.placements.size() == 2,
                  "descriptor names two per-edge container placements");
            if (lane.placements.size() == 2) {
                check(lane.placements[0].edge_index == 0 &&
                          lane.placements[1].edge_index == 1,
                      "two placements use dense edge indices 0 and 1");
                check(lane.placements[0].base == 16384 &&
                          lane.placements[1].base == 16416,
                      "two placements are disjoint on the sum-of-prior rule");
                check(lane.placements[0].extent == 32 &&
                          lane.placements[1].extent == 32,
                      "each List<Int>(4) placement spans its own 32-byte backing");
            }
            auto admitted = fr::admit_core_wasm_frame_sections(emitted->artifact->bytes);
            check(admitted.ok(), "two-containers module admits");
            if (admitted.ok()) {
                const auto &placements = admitted.sections->layout.placements;
                check(placements.size() == 2,
                      "admitted section carries two disjoint placements");
                if (placements.size() == 2) {
                    check(placements[0].container_layout != placements[1].container_layout,
                          "the two placements name distinct per-occurrence container layouts");
                    check(placements[0].base == 16384 && placements[1].base == 16416 &&
                              placements[0].extent == 32 && placements[1].extent == 32 &&
                              placements[0].edge_index == 0 && placements[1].edge_index == 1,
                          "admitted placement coordinates match the planner's");
                }
            }
        }
    }

    // RFC 0026 P6-7 fix-forward, finding 8: framing negatives beyond truncation
    // and a trailing unknown section -- an unknown section BETWEEN the two
    // required sections, duplicate core-layout / wire-schema sections, and a
    // reordered (schema-before-layout) pair must all fail closed.
    {
        const auto bytes = emit_fixture(repo / "tests/golden/wasm/p6_collection.ahfl");
        check(bytes.has_value(), "emit p6_collection for framing negatives");
        if (bytes.has_value()) {
            const auto layout_span =
                find_custom_section_span(*bytes, "ahfl.core-layout.v1");
            const auto schema_span =
                find_custom_section_span(*bytes, "ahfl.wire-schema.v1");
            check(layout_span.has_value() && schema_span.has_value(),
                  "p6_collection frames both frame sections");
            if (layout_span.has_value() && schema_span.has_value()) {
                const std::vector<std::uint8_t> prefix(
                    bytes->begin(), bytes->begin() + layout_span->first);
                const std::vector<std::uint8_t> layout_record(
                    bytes->begin() + layout_span->first,
                    bytes->begin() + layout_span->second);
                const std::vector<std::uint8_t> schema_record(
                    bytes->begin() + schema_span->first,
                    bytes->begin() + schema_span->second);
                static constexpr std::array<std::uint8_t, 1> junk_body{0x00};
                const std::vector<std::uint8_t> unknown_record =
                    make_custom_section("unknown.intervening", junk_body);

                auto rebuild = [&](const std::vector<std::vector<std::uint8_t>> &records) {
                    std::vector<std::uint8_t> module = prefix;
                    for (const auto &record : records) {
                        module.insert(module.end(), record.begin(), record.end());
                    }
                    return module;
                };

                check(!fr::admit_core_wasm_frame_sections(
                           rebuild({layout_record, unknown_record, schema_record}))
                           .ok(),
                      "unknown custom section between layout and schema rejected");
                check(!fr::admit_core_wasm_frame_sections(
                           rebuild({layout_record, layout_record, schema_record}))
                           .ok(),
                      "duplicate core-layout section rejected");
                check(!fr::admit_core_wasm_frame_sections(
                           rebuild({schema_record, layout_record}))
                           .ok(),
                      "schema-before-layout section ordering rejected");
                check(!fr::admit_core_wasm_frame_sections(
                           rebuild({layout_record, schema_record, schema_record}))
                           .ok(),
                      "duplicate wire-schema section rejected");
            }
        }
    }

    // RFC 0026 P6-7 fix-forward, finding 3: an attacker-controlled count must be
    // bounded against the reserved sentinel and the remaining bytes BEFORE any
    // vector reserve(); a 4GiB count used to terminate the host with
    // std::bad_alloc instead of returning a failed decode. Every path returns a
    // failed result and never throws.
    {
        bool threw = false;
        auto decode_safely = [&](const std::vector<std::uint8_t> &payload) {
            try {
                return irc::decode_core_frame_layout_section(payload);
            } catch (...) {
                threw = true;
                return irc::CoreFrameLayoutDecodeResult{};
            }
        };

        // Giant LAYOUT count: sentinel value, then a value just below it but far
        // beyond the remaining byte count.
        for (const std::uint64_t giant : {static_cast<std::uint64_t>(0xFFFFFFFFU),
                                         static_cast<std::uint64_t>(0xFFFFFFFEU)}) {
            auto payload = frame_payload_prefix_with_roots(0, 0);
            put_uleb(payload, giant);
            auto decoded = decode_safely(payload);
            check(!decoded.ok() && !threw,
                  "giant layout-table count fails closed without reserving");
        }

        // A valid two-layout table shared by the value-map and placement
        // count probes below.
        auto table_prefix = frame_payload_prefix_with_roots(1, 1);
        put_uleb(table_prefix, 2); // two layouts
        // node 0: i64 scalar
        table_prefix.push_back(8); // size
        table_prefix.push_back(4); // align
        table_prefix.push_back(0); // not zero-sized
        table_prefix.push_back(1); // shape = scalar
        table_prefix.push_back(1); // repr = I64
        // node 1: container List<Int>(4)
        table_prefix.push_back(8); // size
        table_prefix.push_back(4); // align
        table_prefix.push_back(0); // not zero-sized
        table_prefix.push_back(8); // shape = container
        table_prefix.push_back(0); // element layout id 0
        table_prefix.push_back(0); // no map-value edge
        put_uleb(table_prefix, 4);  // capacity
        put_uleb(table_prefix, 8);  // stride
        put_uleb(table_prefix, 0);  // value_offset
        put_uleb(table_prefix, 32); // backing_size

        // Giant VALUE-LAYOUT mapping count right after the layout table.
        {
            auto payload = table_prefix;
            put_uleb(payload, static_cast<std::uint64_t>(0xFFFFFFFEU));
            auto decoded = decode_safely(payload);
            check(!decoded.ok() && !threw,
                  "giant value-layout mapping count fails closed without reserving");
        }

        // Giant PLACEMENT count: a valid value-map (empty) first, then an
        // unbounded placement count.
        {
            auto payload = table_prefix;
            put_uleb(payload, 0); // value-layout mapping count
            put_uleb(payload, static_cast<std::uint64_t>(0xFFFFFFFFU));
            auto decoded = decode_safely(payload);
            check(!decoded.ok() && !threw,
                  "giant placement count fails closed without reserving");
        }

        // Giant STRUCT field counts (two equal counts, both beyond remaining).
        {
            auto payload = frame_payload_prefix_with_roots(0, 0);
            put_uleb(payload, 1); // one layout follows
            payload.push_back(4); // size
            payload.push_back(4); // align
            payload.push_back(0); // not zero-sized
            payload.push_back(6); // shape = struct
            put_uleb(payload, static_cast<std::uint64_t>(0xFFFFFFFEU)); // offset count
            put_uleb(payload, static_cast<std::uint64_t>(0xFFFFFFFEU)); // layout count
            auto decoded = decode_safely(payload);
            check(!decoded.ok() && !threw,
                  "giant struct field count fails closed without reserving");
        }

        // Giant ENUM variant counts.
        {
            auto payload = frame_payload_prefix_with_roots(0, 0);
            put_uleb(payload, 1); // one layout follows
            payload.push_back(4); // size
            payload.push_back(4); // align
            payload.push_back(0); // not zero-sized
            payload.push_back(7); // shape = enum
            put_uleb(payload, 4); // tag_size
            put_uleb(payload, 0); // payload_offset
            put_uleb(payload, static_cast<std::uint64_t>(0xFFFFFFFEU)); // payload layouts
            put_uleb(payload, static_cast<std::uint64_t>(0xFFFFFFFEU)); // payload sizes
            auto decoded = decode_safely(payload);
            check(!decoded.ok() && !threw,
                  "giant enum variant count fails closed without reserving");
        }
    }

    // RFC 0026 P6-7 fix-forward, finding 5: the host re-derives the container
    // backing geometry and placement extent from the child layouts, so a
    // tampered section cannot shrink the backing (or the extent) while leaving
    // capacity intact. The canonical section admits; both shrink variants fail.
    {
        const auto canonical = make_container_section();
        const auto encoded = irc::encode_core_frame_layout_section(canonical);
        check(encoded.ok(), "hand-built canonical container section encodes");
        if (encoded.ok()) {
            check(irc::decode_core_frame_layout_section(*encoded.bytes).ok(),
                  "canonical container section decodes and verifies");

            // Shrink the SHIPPED backing size (and its extent/arena to match):
            // re-derived stride*capacity disagrees, so the layout node is
            // rejected before the placement arm can trust it.
            auto shrunk_backing = canonical;
            auto &shrunk_container = std::get<irc::CoreLayoutContainer>(
                shrunk_backing.table.layouts[1].shape);
            shrunk_container.backing_size = 8;
            shrunk_backing.placements[0].extent = 8;
            shrunk_backing.payload_arena_base = 16392;
            if (const auto bad = irc::encode_core_frame_layout_section(shrunk_backing);
                bad.ok()) {
                check(!irc::decode_core_frame_layout_section(*bad.bytes).ok(),
                      "shipped backing size below stride*capacity rejected");
            }

            // Keep backing geometry canonical but shrink ONLY the placement
            // extent (with a matching arena base): the placement arm recomputes
            // the aligned 32-byte extent and rejects the 8-byte claim.
            auto shrunk_extent = canonical;
            shrunk_extent.placements[0].extent = 8;
            shrunk_extent.payload_arena_base = 16392;
            if (const auto bad = irc::encode_core_frame_layout_section(shrunk_extent);
                bad.ok()) {
                check(!irc::decode_core_frame_layout_section(*bad.bytes).ok(),
                      "placement extent below stride*capacity rejected");
            }
        }
    }

    // RFC 0026 P6-7 fix-forward, finding 4: the layout/wire consistency check
    // over a DECODED, untrusted pair is iterative, so a legal but deep acyclic
    // struct chain completes without exhausting the C++ stack. The pre-fix
    // recursive checker SIGSEGVed past a depth of 50k.
    {
        constexpr std::uint32_t kDeepChain = 100000;
        const ChainTables chain = make_struct_chain(kDeepChain);
        bool threw = false;
        std::size_t diag_count = 0;
        try {
            const auto diags = irc::verify_frame_layout_wire_consistency(
                chain.layouts, chain.layout_root, chain.wire, chain.wire_root);
            diag_count = diags.size();
        } catch (...) {
            threw = true;
        }
        check(!threw && diag_count == 0,
              "deep acyclic layout/wire chain verifies iteratively without a crash");
    }

    // RFC 0026 P6-7 fix-forward, finding 6: a genuine hard frame-section
    // failure (here an oversized backing placement reached by a LENGTH-ONLY
    // read, which the per-element address gate cannot catch) must surface
    // wasm.RESOURCE_EXHAUSTED and reject the build, not silently downgrade to
    // the sectionless legacy module.
    {
        const auto emitted =
            emit_fixture_full(repo / "tests/golden/wasm/p6_frame_oversized_backing.ahfl");
        check(emitted.has_value(), "oversized-backing fixture reaches codegen");
        if (emitted.has_value()) {
            check(!emitted->ok() && !emitted->artifact.has_value(),
                  "oversized backing rejects with no partial artifact");
            const bool resource =
                std::ranges::any_of(emitted->diagnostics, [](const auto &diag) {
                    return diag.code == backends::core_wasm_diag::kResourceExhausted;
                });
            check(resource, "oversized backing reports wasm.RESOURCE_EXHAUSTED");
        }
    }

    if (g_failures == 0) {
        std::cout << "core_wasm_frame_module: all checks passed\n";
        return 0;
    }
    std::cerr << "core_wasm_frame_module: " << g_failures << " failure(s)\n";
    return 1;
}
