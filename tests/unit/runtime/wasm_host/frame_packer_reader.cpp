// RFC 0026 KR6.8 WH-2: unit tests for the P6-frame packer + reader + thin
// driver (src/runtime/wasm_host/frame_packer.cpp, frame_reader.cpp,
// p6_frame_driver.cpp). Every module is emitted in-process through the real
// wasm backend (no shelling out, no hand-built bytes for the execution tests)
// and admitted through the production admit_core_wasm_frame_sections path, so
// the tests prove the REAL pipeline: emit -> admit -> pack -> wasm3 runv ->
// authorize root -> read output -> canonical value JSON.
//
// Pinned behaviors (the slice contract's eight families):
//   1. scalar round-trip (bool/i64) canonical-JSON byte equality;
//   2. payload-enum + nested-struct inline offsets (v2b_enum_string);
//   3. collection backing incl p6_frame_two_containers (disjoint placements);
//   4. String arena pack + rodata region accepted + scratch/OOB fail-closed;
//   5. runv root auth rejection (1024 identity / 12288 computed);
//   6. schema fail-closed family (string bounds, bool word, enum tag,
//      len>capacity, placement overrun, int range);
//   7. region zeroing (padding/stale bytes never read);
//   8. oracle-derived parity: native canonical output JSON byte-equals the
//      expected observation in the case.json for >=3 fixtures.
// Plus: a closure value pack attempt MUST fail closed.

#include "runtime/wasm_host/frame_packer.hpp"
#include "runtime/wasm_host/frame_reader.hpp"
#include "runtime/wasm_host/p6_frame_driver.hpp"
#include "runtime/wasm_host/transcode.hpp"
#include "runtime/wasm_host/wasm3_engine.hpp"

#include "runtime/engine/core_wasm_frame_module.hpp"
#include "runtime/value/value.hpp"
#include "runtime/value/value_json.hpp"

#include "ahfl/compiler/ir/core_frame_layout.hpp"
#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/core_layout.hpp"
#include "ahfl/compiler/ir/core_wasm_abi_constants.hpp"
#include "ahfl/compiler/ir/core_wire_migration.hpp"
#include "ahfl/compiler/ir/core_wire_schema.hpp"
#include "ahfl/runtime/ahfl_host.h"
#include "compiler/backends/wasm/core_wasm_codegen.hpp"
#include "conformance/compile_source.hpp"
#include "conformance/conformance_case.hpp"
#include "unit/runtime/wasm_host/wasm_host_test_support.hpp"

#include "common/project_input_support.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {

namespace ir = ahfl::ir;
namespace irc = ahfl::ir::core;
namespace wh = ahfl::runtime::wasm_host;
namespace fr = ahfl::runtime::core_wasm_frame_module;
namespace eng = ahfl::runtime::core_wasm_resume_engine;
namespace conf = ahfl::conformance;
namespace wht = ahfl::runtime::wasm_host_test_support;

using ahfl::runtime::Value;
using ahfl::runtime::value_from_json;

int g_test_count = 0;
int g_pass_count = 0;

void check(bool condition, const std::string &name) {
    ++g_test_count;
    if (condition) {
        ++g_pass_count;
    } else {
        std::cerr << "FAIL: " << name << "\n";
    }
}

// --- emit + admit helpers ----------------------------------------------------

struct EmittedFixture {
    std::vector<std::uint8_t> module_bytes;
    fr::AdmittedFrameSections admitted;
    wh::P6FinalKind final_kind{wh::P6FinalKind::Identity};
};

// Emit a real wasm module from a source .ahfl file and admit its frame
// sections. Returns nullopt on any pipeline failure.
[[nodiscard]] std::optional<EmittedFixture>
emit_and_admit(const std::filesystem::path &source_path) {
    std::string error;
    auto program = conf::compile_conformance_source(source_path, error);
    if (!program.has_value()) {
        std::cerr << "  compile failed: " << error << "\n";
        return std::nullopt;
    }
    const auto core = ir::core::lower_ahfl_to_core(*program);
    if (!core.ok()) {
        std::cerr << "  core lower failed\n";
        return std::nullopt;
    }
    const auto layouts = ir::core::compute_core_layouts(core.program);
    if (!layouts.ok() || !layouts.table.has_value()) {
        std::cerr << "  layout failed\n";
        return std::nullopt;
    }
    const auto emitted = ahfl::backends::emit_core_wasm(
        core.program, *layouts.table,
        {ir::core::CoreAgentId{0}, ahfl::backends::WasmProfileKind::Wasi});
    if (!emitted.ok() || !emitted.artifact.has_value() ||
        !emitted.descriptor.has_value()) {
        std::cerr << "  emit failed\n";
        return std::nullopt;
    }

    auto admitted = fr::admit_core_wasm_frame_sections(emitted.artifact->bytes);
    if (!admitted.ok()) {
        std::cerr << "  admit failed\n";
        return std::nullopt;
    }

    // final_kind from the codegen C++ descriptor (decision doc section 11.2):
    // the admitted layout section does not carry it, and input_layout ==
    // output_layout cannot infer it (v2b_string_passthrough is computed).
    const wh::P6FinalKind final_kind =
        (emitted.descriptor->frame.has_value() &&
         emitted.descriptor->frame->final_kind == "computed")
            ? wh::P6FinalKind::Computed
            : wh::P6FinalKind::Identity;
    return EmittedFixture{
        .module_bytes = emitted.artifact->bytes,
        .admitted = std::move(*admitted.sections),
        .final_kind = final_kind,
    };
}

// Load a conformance case.json and return the first scenario's input JSON and
// expected output JSON (as canonical wire JSON strings).
struct CaseData {
    std::string input_json;
    std::optional<std::string> expected_output_json;
};

[[nodiscard]] std::optional<CaseData>
load_case(const std::filesystem::path &case_path,
          const std::filesystem::path &repo_root) {
    auto loaded = conf::load_conformance_case(case_path, repo_root);
    if (!loaded.conformance_case.has_value()) {
        std::cerr << "  case load failed: " << case_path << "\n";
        return std::nullopt;
    }
    const auto &manifest = loaded.conformance_case->manifest;
    if (manifest.scenarios.empty()) {
        std::cerr << "  no scenarios: " << case_path << "\n";
        return std::nullopt;
    }
    CaseData data;
    data.input_json = manifest.scenarios[0].input_json;
    data.expected_output_json = manifest.scenarios[0].expect.output_json;
    return data;
}

// Execute a fixture through the thin driver and return the output JSON.
[[nodiscard]] std::optional<std::string>
execute_fixture(const EmittedFixture &fixture, const Value &input) {
    wh::Wasm3ResumeEngine engine;
    auto result = wh::execute_p6_frame(
        engine, fixture.module_bytes, fixture.admitted.layout,
        fixture.admitted.input_binding, fixture.admitted.output_binding, input,
        fixture.final_kind,
        [](const eng::ImportObservation &) -> eng::ImportCallbackResult {
            return eng::ImportAbort{}; // never called: no imports on these
        });
    if (!result.has_value()) {
        std::cerr << "  execute failed\n";
        return std::nullopt;
    }
    return *result;
}

// Execute a fixture from a case.json and compare the output JSON byte-for-byte
// with the case's expected observation.
void run_case(const std::filesystem::path &repo_root, const char *case_rel,
              const char *label) {
    const auto case_path = repo_root / "tests/conformance/cases" / case_rel;
    auto data = load_case(case_path, repo_root);
    check(data.has_value(), std::string(label) + ".load_case");
    if (!data.has_value()) {
        return;
    }
    auto input = value_from_json(data->input_json);
    check(input.has_value(), std::string(label) + ".input_from_json");
    if (!input.has_value()) {
        return;
    }

    // Derive the source path from the case.json's `source` field.
    auto loaded = conf::load_conformance_case(case_path, repo_root);
    const auto source_path = loaded.conformance_case->source_path;
    auto fixture = emit_and_admit(source_path);
    check(fixture.has_value(), std::string(label) + ".emit_and_admit");
    if (!fixture.has_value()) {
        return;
    }

    auto output = execute_fixture(*fixture, *input);
    check(output.has_value(), std::string(label) + ".execute");
    if (!output.has_value()) {
        return;
    }

    if (data->expected_output_json.has_value()) {
        check(*output == *data->expected_output_json,
              std::string(label) + ".output_json_parity");
    }
}

// --- 1. scalar round-trip ----------------------------------------------------

void test_scalar_roundtrip(const std::filesystem::path &repo_root) {
    // p6_aggregate: i64 struct fields, identity final.
    run_case(repo_root, "p6_aggregate.case.json", "scalar.i64_aggregate");

    // v2b_bounded_string: bool input, rodata String output, computed final.
    run_case(repo_root, "v2b_bounded_string.case.json", "scalar.bool_rodata");
}

// --- 2. payload enum + nested struct ----------------------------------------

void test_payload_enum(const std::filesystem::path &repo_root) {
    // v2b_enum_string: computed final with a String-payload enum variant.
    // No case.json exists; construct the input + expected output manually.
    const auto source = repo_root / "tests/golden/wasm/v2b_enum_string.ahfl";
    auto fixture = emit_and_admit(source);
    check(fixture.has_value(), "enum_string.emit_and_admit");
    if (!fixture.has_value()) {
        return;
    }

    // Hit branch: flag=true -> Match::Hit("hit")
    {
        auto input = value_from_json(
            "{\"_type\":\"wasm::v2b::enum_string::In\",\"flag\":true}");
        check(input.has_value(), "enum_string.hit.input");
        if (!input.has_value()) {
            return;
        }
        auto output = execute_fixture(*fixture, *input);
        check(output.has_value(), "enum_string.hit.execute");
        if (output.has_value()) {
            const std::string expected =
                "{\"_type\":\"wasm::v2b::enum_string::Out\",\"result\":{"
                "\"_enum\":\"wasm::v2b::enum_string::Match\","
                "\"_variant\":\"Hit\",\"_payload\":[\"hit\"]}}";
            check(*output == expected, "enum_string.hit.output_parity");
        }
    }

    // Miss branch: flag=false -> Match::Miss
    {
        auto input = value_from_json(
            "{\"_type\":\"wasm::v2b::enum_string::In\",\"flag\":false}");
        check(input.has_value(), "enum_string.miss.input");
        if (!input.has_value()) {
            return;
        }
        auto output = execute_fixture(*fixture, *input);
        check(output.has_value(), "enum_string.miss.execute");
        if (output.has_value()) {
            const std::string expected =
                "{\"_type\":\"wasm::v2b::enum_string::Out\",\"result\":{"
                "\"_enum\":\"wasm::v2b::enum_string::Match\","
                "\"_variant\":\"Miss\"}}";
            check(*output == expected, "enum_string.miss.output_parity");
        }
    }
}

// --- 3. collection backing ---------------------------------------------------

void test_collection_backing(const std::filesystem::path &repo_root) {
    run_case(repo_root, "p6_collection.case.json", "collection.single");
    run_case(repo_root, "p6_frame_two_containers.case.json",
             "collection.two_disjoint");
}

// --- 4. String arena + rodata ------------------------------------------------

void test_string_arenas(const std::filesystem::path &repo_root) {
    // v2b_string_passthrough: computed final, String arena passthrough.
    run_case(repo_root, "v2b_string_passthrough.case.json", "string.arena");

    // v2b_computed_string: computed final, rodata String + i64.
    run_case(repo_root, "v2b_computed_string.case.json", "string.rodata");
}

// --- 5. runv root authorization ----------------------------------------------

void test_runv_root_auth(const std::filesystem::path &repo_root) {
    const auto source = repo_root / "tests/golden/wasm/p6_aggregate.ahfl";
    auto fixture = emit_and_admit(source);
    check(fixture.has_value(), "root_auth.emit_and_admit");
    if (!fixture.has_value()) {
        return;
    }

    // p6_aggregate is identity final: value_ptr must equal 1024.
    // A computed-style value_ptr (12288) must be rejected.
    std::vector<std::uint8_t> page(65536, 0);
    auto rejected = wh::encode_p6_output(
        page, fixture->admitted.layout, fixture->admitted.output_binding,
        irc::kP6AggregateOutputBase, wh::P6FinalKind::Identity);
    check(!rejected.has_value() &&
              rejected.error() == wh::FrameReadError::RunvRootUnauthorized,
          "root_auth.identity_rejects_output_base");

    // Strict identity success: write a=100, b=7 as little-endian i64 at the
    // input base (1024), then read through the identity final. The output
    // JSON must be the exact canonical observation.
    {
        std::vector<std::uint8_t> wpage(65536, 0);
        const auto base = irc::kP6AggregateInputBase;
        // a=100 at offset 0, b=7 at offset 8 (little-endian i64).
        wpage[base + 0] = 100;
        wpage[base + 8] = 7;
        auto accepted = wh::encode_p6_output(
            wpage, fixture->admitted.layout, fixture->admitted.output_binding,
            irc::kP6AggregateInputBase, wh::P6FinalKind::Identity);
        check(accepted.has_value(),
              "root_auth.identity_accepts_input_base (strict success)");
        if (accepted.has_value()) {
            const std::string expected =
                "{\"_type\":\"wasm::p6_aggregate::Frame\",\"a\":100,\"b\":7}";
            check(*accepted == expected,
                  "root_auth.identity_output_json_exact");
        }
    }

    // Boundary probes on the identity final: every value_ptr other than 1024
    // must be rejected with RunvRootUnauthorized.
    for (const auto ptr : {std::uint32_t{1025}, std::uint32_t{12287},
                           std::uint32_t{0}, std::uint32_t{65535}}) {
        auto probe = wh::encode_p6_output(
            page, fixture->admitted.layout, fixture->admitted.output_binding,
            ptr, wh::P6FinalKind::Identity);
        check(!probe.has_value() &&
                  probe.error() == wh::FrameReadError::RunvRootUnauthorized,
              "root_auth.identity_rejects_ptr_" + std::to_string(ptr));
    }

    // v2b_bounded_string is computed final: value_ptr must equal 12288.
    const auto csource = repo_root / "tests/golden/wasm/v2b_bounded_string.ahfl";
    auto cfixture = emit_and_admit(csource);
    check(cfixture.has_value(), "root_auth.computed.emit_and_admit");
    if (!cfixture.has_value()) {
        return;
    }
    auto crejected = wh::encode_p6_output(
        page, cfixture->admitted.layout, cfixture->admitted.output_binding,
        irc::kP6AggregateInputBase, wh::P6FinalKind::Computed);
    check(!crejected.has_value() &&
              crejected.error() == wh::FrameReadError::RunvRootUnauthorized,
          "root_auth.computed_rejects_input_base");

    // Boundary probes on the computed final: every value_ptr other than 12288
    // must be rejected with RunvRootUnauthorized.
    for (const auto ptr : {std::uint32_t{1025}, std::uint32_t{12287},
                           std::uint32_t{0}, std::uint32_t{65535}}) {
        auto probe = wh::encode_p6_output(
            page, cfixture->admitted.layout, cfixture->admitted.output_binding,
            ptr, wh::P6FinalKind::Computed);
        check(!probe.has_value() &&
                  probe.error() == wh::FrameReadError::RunvRootUnauthorized,
              "root_auth.computed_rejects_ptr_" + std::to_string(ptr));
    }

    // Direct computed+12288 happy-path: write "some" at the rodata base (256)
    // and a PtrLen(256, 4) at the output base (12288), then read through the
    // computed final. The output JSON must be the exact canonical observation.
    {
        std::vector<std::uint8_t> cpage(65536, 0);
        const auto rodata = irc::kP6RodataBase; // 256
        const auto out_base = irc::kP6AggregateOutputBase; // 12288
        // Write "some" at rodata.
        const std::string some = "some";
        for (std::size_t i = 0; i < some.size(); ++i) {
            cpage[rodata + i] = static_cast<std::uint8_t>(some[i]);
        }
        // Write PtrLen(256, 4) at the output base (little-endian u32 each).
        // 256 = 0x100, so LE bytes are [0x00, 0x01, 0x00, 0x00].
        cpage[out_base + 0] = 0x00;
        cpage[out_base + 1] = 0x01;
        cpage[out_base + 2] = 0x00;
        cpage[out_base + 3] = 0x00;
        cpage[out_base + 4] = 4;
        cpage[out_base + 5] = 0;
        cpage[out_base + 6] = 0;
        cpage[out_base + 7] = 0;
        auto caccepted = wh::encode_p6_output(
            cpage, cfixture->admitted.layout, cfixture->admitted.output_binding,
            irc::kP6AggregateOutputBase, wh::P6FinalKind::Computed);
        check(caccepted.has_value(),
              "root_auth.computed_accepts_output_base (strict success)");
        if (caccepted.has_value()) {
            const std::string expected =
                "{\"_type\":\"wasm::v2b::bounded_string::Out\",\"label\":\"some\"}";
            check(*caccepted == expected,
                  "root_auth.computed_output_json_exact");
        }
    }
}

// --- 6. schema fail-closed family --------------------------------------------

void test_schema_fail_closed(const std::filesystem::path &repo_root) {
    // 6a. String length bounds: v2b_bounded_string output is String(0,8).
    //     A PtrLen with len=99 must fail with StringLengthOutOfBounds.
    {
        const auto source = repo_root / "tests/golden/wasm/v2b_bounded_string.ahfl";
        auto fixture = emit_and_admit(source);
        check(fixture.has_value(), "fail_closed.string_bounds.emit");
        if (!fixture.has_value()) {
            return;
        }
        std::vector<std::uint8_t> page(65536, 0);
        // Write a PtrLen at the output frame (12288) with len=99 (> 8).
        const auto out_base = irc::kP6AggregateOutputBase;
        page[out_base + 0] = 256 & 0xFF; // ptr = 256 (rodata, authorized)
        page[out_base + 1] = 0;
        page[out_base + 2] = 0;
        page[out_base + 3] = 0;
        page[out_base + 4] = 99; // len = 99 (> 8)
        page[out_base + 5] = 0;
        page[out_base + 6] = 0;
        page[out_base + 7] = 0;
        auto result = wh::encode_p6_output(
            page, fixture->admitted.layout, fixture->admitted.output_binding,
            out_base, wh::P6FinalKind::Computed);
        check(!result.has_value() &&
                  result.error() == wh::FrameReadError::StringLengthOutOfBounds,
              "fail_closed.string_bounds");
    }

    // 6b. Bool word invalid: synthetic layout with input==output and a Bool
    //     field. A bool word of 2 must fail with BoolWordInvalid. We pack a
    //     valid Bool, corrupt the word, then read through the same binding
    //     (input==output so encode_p6_output's output_layout read is valid).
    {
        irc::CoreFrameLayoutSection section;
        section.table.target = irc::TargetDataLayout{};
        section.table.layouts.push_back(
            irc::CoreLayout{4, 4, false, irc::CoreLayoutScalar{irc::CoreScalarRepr::I32}});
        section.input_layout = irc::CoreLayoutId{0};
        section.output_layout = irc::CoreLayoutId{0};
        section.payload_arena_base = 16384;
        section.payload_arena_capacity = 4096;

        irc::CoreWireSchemaTable wire;
        wire.format_version = 1;
        irc::CoreWireSchemaNode node;
        node.shape = irc::CoreWireSchemaBool{};
        wire.nodes.push_back(node);
        wire.frame_roots = irc::CoreWireFrameRoots{
            irc::CoreWireSchemaNodeId{0}, irc::CoreWireSchemaNodeId{0}, {}, {}};

        auto verified = irc::make_verified_wire_schema_table(std::move(wire));
        check(verified.table.has_value(), "fail_closed.bool_word.verified");
        if (!verified.table.has_value()) {
            return;
        }
        std::vector<irc::CoreLowerDiagnostic> diagnostics;
        auto binding = irc::make_frame_binding_from_verified_table(
            *verified.table, {irc::CoreWireFrameRootKind::Input}, diagnostics);
        check(binding.has_value(), "fail_closed.bool_word.binding");
        if (!binding.has_value()) {
            return;
        }

        std::vector<std::uint8_t> page(65536, 0);
        auto packed = wh::pack_p6_input(page, section, *binding,
                                        ahfl::runtime::make_bool(true));
        check(packed.has_value(), "fail_closed.bool_word.pack");
        if (!packed.has_value()) {
            return;
        }
        // Corrupt the bool word at offset 0 of the input frame.
        page[irc::kP6AggregateInputBase + 0] = 2; // invalid bool word
        auto result = wh::encode_p6_output(
            page, section, *binding,
            irc::kP6AggregateInputBase, wh::P6FinalKind::Identity);
        check(!result.has_value() &&
                  result.error() == wh::FrameReadError::BoolWordInvalid,
              "fail_closed.bool_word");
    }

    // 6b2. Float round-trip: the f64 pack/read branches are NOT dead code --
    //      the durable-resume CLI smoke (#423) exercises a Float field in the
    //      workflow output (RichReply.ratio) through the JSON_TO_P4D
    //      workflow-output crossing, which calls pack_value_at. A Float wire
    //      schema node with an F64 layout must pack and read back the exact
    //      f64 bits (memcpy-identical, no epsilon).
    //      (The P2-1 review finding claimed the f64 branches were dead code;
    //      that was a factual error -- #423 depends on them. f64 support is
    //      tracked for promotion in WH-5c.7.)
    {
        irc::CoreFrameLayoutSection section;
        section.table.target = irc::TargetDataLayout{};
        section.table.layouts.push_back(
            irc::CoreLayout{8, 8, false, irc::CoreLayoutScalar{irc::CoreScalarRepr::F64}});
        section.input_layout = irc::CoreLayoutId{0};
        section.output_layout = irc::CoreLayoutId{0};
        section.payload_arena_base = 16384;
        section.payload_arena_capacity = 4096;

        irc::CoreWireSchemaTable wire;
        wire.format_version = 1;
        irc::CoreWireSchemaNode node;
        node.shape = irc::CoreWireSchemaFloat{};
        wire.nodes.push_back(node);
        wire.frame_roots = irc::CoreWireFrameRoots{
            irc::CoreWireSchemaNodeId{0}, irc::CoreWireSchemaNodeId{0}, {}, {}};

        auto verified = irc::make_verified_wire_schema_table(std::move(wire));
        check(verified.table.has_value(), "float_roundtrip.verified");
        if (!verified.table.has_value()) {
            return;
        }
        std::vector<irc::CoreLowerDiagnostic> diagnostics;
        auto binding = irc::make_frame_binding_from_verified_table(
            *verified.table, {irc::CoreWireFrameRootKind::Input}, diagnostics);
        check(binding.has_value(), "float_roundtrip.binding");
        if (!binding.has_value()) {
            return;
        }

        constexpr double kFloatValue = 3.14;
        std::vector<std::uint8_t> page(65536, 0);
        auto packed = wh::pack_p6_input(page, section, *binding,
                                        ahfl::runtime::make_float(kFloatValue));
        check(packed.has_value(), "float_roundtrip.pack");

        // Read back through the generic reader at the P6 input base.
        wh::FrameWalkContext ctx(section, binding->table());
        const std::vector<wh::StringRegion> no_strings;
        auto read_back = wh::read_value_at(
            ctx, page, binding->root(), section.input_layout,
            irc::kP6AggregateInputBase, no_strings);
        check(read_back.has_value(), "float_roundtrip.read");
        if (read_back.has_value()) {
            const auto *fv =
                std::get_if<ahfl::runtime::FloatValue>(&read_back->node);
            check(fv != nullptr, "float_roundtrip.is_float");
            if (fv != nullptr) {
                // Exact bit comparison (no epsilon: the round-trip is
                // memcpy-identical f64 bits).
                std::uint64_t expected_bits = 0;
                std::uint64_t actual_bits = 0;
                std::memcpy(&expected_bits, &kFloatValue, sizeof(expected_bits));
                std::memcpy(&actual_bits, &fv->value, sizeof(actual_bits));
                check(expected_bits == actual_bits, "float_roundtrip.bit_exact");
            }
        }
    }

    // 6c. Enum tag out of range: v2b_enum_string output is Out{result:Match}
    //     with Miss=0, Hit=1. A tag of 2 must fail with EnumTagOutOfRange.
    {
        const auto source = repo_root / "tests/golden/wasm/v2b_enum_string.ahfl";
        auto fixture = emit_and_admit(source);
        check(fixture.has_value(), "fail_closed.enum_tag.emit");
        if (!fixture.has_value()) {
            return;
        }
        std::vector<std::uint8_t> page(65536, 0);
        // Write enum tag=2 at the output frame (12288).
        const auto out_base = irc::kP6AggregateOutputBase;
        page[out_base + 0] = 2; // tag = 2 (out of range: only 0,1)
        auto result = wh::encode_p6_output(
            page, fixture->admitted.layout, fixture->admitted.output_binding,
            out_base, wh::P6FinalKind::Computed);
        check(!result.has_value() &&
                  result.error() == wh::FrameReadError::EnumTagOutOfRange,
              "fail_closed.enum_tag");
    }

    // 6d. Collection length > capacity: p6_collection has List<Int>(4).
    //     A len of 5 must fail with CollectionLengthOutOfRange.
    {
        const auto source = repo_root / "tests/golden/wasm/p6_collection.ahfl";
        auto fixture = emit_and_admit(source);
        check(fixture.has_value(), "fail_closed.coll_len.emit");
        if (!fixture.has_value()) {
            return;
        }
        std::vector<std::uint8_t> page(65536, 0);
        // The input frame is a struct with one field: the List container.
        // The container's (ptr,len) header is at the field's offset.
        // p6_collection's Frame has items: List<Int>(4) at offset 0.
        // Write len=5 at offset 4 (the len word of the PtrLen header).
        const auto in_base = irc::kP6AggregateInputBase;
        // The struct field offset for `items` is 0 (first field).
        page[in_base + 4] = 5; // len = 5 > capacity 4
        auto result = wh::encode_p6_output(
            page, fixture->admitted.layout, fixture->admitted.output_binding,
            in_base, wh::P6FinalKind::Identity);
        check(!result.has_value() &&
                  (result.error() == wh::FrameReadError::CollectionLengthOutOfRange ||
                   result.error() == wh::FrameReadError::CollectionOverrunsPlacement),
              "fail_closed.coll_len");
    }

    // 6e. String outside authorized region: a PtrLen pointing into scratch
    //     must fail with StringOutsideAuthorizedRegion.
    {
        const auto source = repo_root / "tests/golden/wasm/v2b_bounded_string.ahfl";
        auto fixture = emit_and_admit(source);
        check(fixture.has_value(), "fail_closed.string_region.emit");
        if (!fixture.has_value()) {
            return;
        }
        std::vector<std::uint8_t> page(65536, 0);
        const auto out_base = irc::kP6AggregateOutputBase;
        // PtrLen at output frame: ptr=8000 (scratch region), len=4.
        page[out_base + 0] = 0x40; // ptr = 0x1F40 = 8000 (scratch)
        page[out_base + 1] = 0x1F;
        page[out_base + 4] = 4; // len = 4 (within bounds 0..8)
        auto result = wh::encode_p6_output(
            page, fixture->admitted.layout, fixture->admitted.output_binding,
            out_base, wh::P6FinalKind::Computed);
        check(!result.has_value() &&
                  result.error() == wh::FrameReadError::StringOutsideAuthorizedRegion,
              "fail_closed.string_region");
    }

    // 6f. Int range: synthetic bounded-Int layout. Pack an out-of-range value
    //     and verify the packer fails with IntOutOfSchemaBounds.
    {
        // Construct a minimal frame section with one i32 scalar layout and a
        // wire schema with a bounded Int (0, 100).
        irc::CoreFrameLayoutSection section;
        section.table.target = irc::TargetDataLayout{};
        section.table.layouts.push_back(
            irc::CoreLayout{4, 4, false, irc::CoreLayoutScalar{irc::CoreScalarRepr::I32}});
        section.input_layout = irc::CoreLayoutId{0};
        section.output_layout = irc::CoreLayoutId{0};
        section.payload_arena_base = 16384;
        section.payload_arena_capacity = 4096;

        irc::CoreWireSchemaTable wire;
        wire.format_version = 1;
        irc::CoreWireSchemaNode node;
        node.shape = irc::CoreWireSchemaInt{
            std::pair<std::int64_t, std::int64_t>{0, 100}};
        wire.nodes.push_back(node);
        wire.frame_roots = irc::CoreWireFrameRoots{
            irc::CoreWireSchemaNodeId{0}, irc::CoreWireSchemaNodeId{0}, {}, {}};

        auto verified = irc::make_verified_wire_schema_table(std::move(wire));
        check(verified.table.has_value(), "fail_closed.int_range.verified");
        if (!verified.table.has_value()) {
            return;
        }
        std::vector<irc::CoreLowerDiagnostic> diagnostics;
        auto input_binding = irc::make_frame_binding_from_verified_table(
            *verified.table, {irc::CoreWireFrameRootKind::Input}, diagnostics);
        check(input_binding.has_value(), "fail_closed.int_range.binding");
        if (!input_binding.has_value()) {
            return;
        }

        // In-range value (50) must pack successfully.
        std::vector<std::uint8_t> page(65536, 0);
        auto ok_pack = wh::pack_p6_input(page, section, *input_binding,
                                         ahfl::runtime::make_int(50));
        check(ok_pack.has_value(), "fail_closed.int_range.in_range_packs");

        // Out-of-range value (200) must fail with IntOutOfSchemaBounds.
        auto bad_pack = wh::pack_p6_input(page, section, *input_binding,
                                          ahfl::runtime::make_int(200));
        check(!bad_pack.has_value() &&
                  bad_pack.error() == wh::FramePackError::IntOutOfSchemaBounds,
              "fail_closed.int_range.out_of_range_fails");
    }
}

// --- 7. region zeroing -------------------------------------------------------

void test_region_zeroing(const std::filesystem::path &repo_root) {
    const auto source = repo_root / "tests/golden/wasm/p6_aggregate.ahfl";
    auto fixture = emit_and_admit(source);
    check(fixture.has_value(), "zeroing.emit_and_admit");
    if (!fixture.has_value()) {
        return;
    }
    auto input = value_from_json(
        "{\"_type\":\"wasm::p6_aggregate::Frame\",\"a\":100,\"b\":7}");
    check(input.has_value(), "zeroing.input");
    if (!input.has_value()) {
        return;
    }

    // Fill the page with 0xFF, then pack. The packer must zero
    // [1024, 16384) first, so every padding word in that range is 0.
    std::vector<std::uint8_t> page(65536, 0xFF);
    auto packed = wh::pack_p6_input(page, fixture->admitted.layout,
                                    fixture->admitted.input_binding, *input);
    check(packed.has_value(), "zeroing.pack");
    if (!packed.has_value()) {
        return;
    }

    // The reserved named regions [1024, 16384) must be zeroed EXCEPT where the
    // packer wrote the input frame (at 1024) and the collection placements
    // (>= 16384, outside the zeroed range). p6_aggregate has no collections,
    // so the only non-zero bytes in [1024, 16384) are the two i64 fields.
    // Pin the EXACT count and offsets: a=100 (0x64) at field offset 0, b=7
    // (0x07) at field offset 8, both little-endian i64 (only the low byte
    // is non-zero for these small values).
    const auto &input_layout =
        fixture->admitted.layout.table.layouts[fixture->admitted.layout.input_layout.value];
    const auto *input_struct =
        std::get_if<irc::CoreLayoutStruct>(&input_layout.shape);
    check(input_struct != nullptr && input_struct->field_offsets.size() == 2,
          "zeroing.input layout is a 2-field struct");
    if (input_struct != nullptr && input_struct->field_offsets.size() == 2) {
        const auto base = irc::kP6AggregateInputBase;
        const auto off_a = input_struct->field_offsets[0];
        const auto off_b = input_struct->field_offsets[1];
        std::size_t non_zero_in_reserved = 0;
        for (std::size_t i = 1024; i < 16384; ++i) {
            if (page[i] != 0) {
                ++non_zero_in_reserved;
            }
        }
        check(non_zero_in_reserved == 2, "zeroing.exactly 2 non-zero bytes in reserved");
        check(page[base + off_a] == 0x64, "zeroing.a=100 (0x64) at derived offset");
        check(page[base + off_b] == 0x07, "zeroing.b=7 (0x07) at derived offset");
        // All other bytes in the two i64 slots must be zero (high bytes).
        bool high_bytes_zero = true;
        for (std::size_t i = 1; i < 8; ++i) {
            if (page[base + off_a + i] != 0 || page[base + off_b + i] != 0) {
                high_bytes_zero = false;
            }
        }
        check(high_bytes_zero, "zeroing.i64 high bytes are zero");
    }

    // The collection backing region [16384, 65536) must NOT be zeroed by the
    // packer (it was 0xFF and stays 0xFF for p6_aggregate, which has no
    // collections).
    bool backing_untouched = true;
    for (std::size_t i = 16384; i < 16384 + 64; ++i) {
        if (page[i] != 0xFF) {
            backing_untouched = false;
            break;
        }
    }
    check(backing_untouched, "zeroing.backing_not_zeroed");
}

// --- 8. oracle-derived parity ------------------------------------------------

void test_oracle_parity(const std::filesystem::path &repo_root) {
    // >=3 fixtures: scalar (p6_aggregate), rodata-String
    // (v2b_bounded_string), arena-String (v2b_string_passthrough).
    // These are already exercised by run_case in tests 1 and 4; the parity
    // assertion is the .output_json_parity check inside run_case.
    run_case(repo_root, "p6_aggregate.case.json", "parity.aggregate");
    run_case(repo_root, "v2b_bounded_string.case.json", "parity.bounded_string");
    run_case(repo_root, "v2b_string_passthrough.case.json", "parity.string_passthrough");
}

// --- page precondition (P2-6 review fix-forward) ------------------------------

void test_page_precondition(const std::filesystem::path &repo_root) {
    const auto source = repo_root / "tests/golden/wasm/p6_aggregate.ahfl";
    auto fixture = emit_and_admit(source);
    check(fixture.has_value(), "page-pre.emit_and_admit");
    if (!fixture.has_value()) {
        return;
    }
    auto input = value_from_json(
        "{\"_type\":\"wasm::p6_aggregate::Frame\",\"a\":1,\"b\":2}");
    check(input.has_value(), "page-pre.input");
    if (!input.has_value()) {
        return;
    }

    // A trimmed page (65535) must be rejected: the packer requires the whole
    // fixed 64 KiB page, not a partial buffer.
    {
        std::vector<std::uint8_t> small(65535, 0);
        auto r = wh::pack_p6_input(small, fixture->admitted.layout,
                                   fixture->admitted.input_binding, *input);
        check(!r.has_value() &&
                  r.error() == wh::FramePackError::PageBoundsExceeded,
              "page-pre.trimmed_65535_rejected");
    }

    // An oversized page (65537) must also be rejected.
    {
        std::vector<std::uint8_t> big(65537, 0);
        auto r = wh::pack_p6_input(big, fixture->admitted.layout,
                                   fixture->admitted.input_binding, *input);
        check(!r.has_value() &&
                  r.error() == wh::FramePackError::PageBoundsExceeded,
              "page-pre.oversized_65537_rejected");
    }

    // The exact fixed page (65536) must succeed.
    {
        std::vector<std::uint8_t> exact(65536, 0);
        auto r = wh::pack_p6_input(exact, fixture->admitted.layout,
                                   fixture->admitted.input_binding, *input);
        check(r.has_value(), "page-pre.exact_65536_accepted");
    }
}

// --- duplicate placement rejection (P2-1 review fix-forward) ------------------

void test_duplicate_placement_rejected(const std::filesystem::path &repo_root) {
    // p6_frame_two_containers has two placements with DIFFERENT container
    // layouts; it must still admit (the two-containers fixture is valid).
    const auto source =
        repo_root / "tests/golden/wasm/p6_frame_two_containers.ahfl";
    auto fixture = emit_and_admit(source);
    check(fixture.has_value(), "dup-place.two_containers_admits");
    if (!fixture.has_value()) {
        return;
    }

    // Clone the admitted section and add a THIRD placement that duplicates
    // the first placement's container_layout. The local verifier must reject
    // it at decode time (the packer/reader's backing_by_layout map is indexed
    // by layout id, so a duplicate would silently shadow the first).
    auto section = fixture->admitted.layout;
    check(section.placements.size() >= 2, "dup-place.fixture has >=2 placements");
    if (section.placements.size() < 2) {
        return;
    }
    auto dup = section.placements[0];
    dup.edge_index = static_cast<std::uint32_t>(section.placements.size());
    section.placements.push_back(dup);

    auto encoded = irc::encode_core_frame_layout_section(section);
    check(encoded.ok(), "dup-place.duplicate section encodes");
    if (!encoded.ok()) {
        return;
    }
    auto decoded = irc::decode_core_frame_layout_section(*encoded.bytes);
    check(!decoded.ok(), "dup-place.duplicate container_layout rejected at decode");
}

// WH-5c.4 P2-F: a JsonToP4D transcode site carrying Source::CapabilityParam is
// a corrupt module (CapabilityParam is P4DToJson-only: the construct terminal's
// self-transcode).  The host must fail closed rather than falling through to
// the NodeOutput branch and minting a node-frame binding for an impossible
// transcode.  Codegen never emits this combo, so the test crafts the corrupt
// site directly and asserts handle_transcode returns AHFL_CAP_ERROR.
//
// The descriptor carries one node and the wire table a matching node-input
// root, so WITHOUT the P2-F early-return the fall-through would reach the
// node-frame binding mint (not the bounds check) -- the test pins that the
// P2-F branch specifically is the rejection point.
void test_transcode_capability_param_json_to_p4d_rejected(
    const std::filesystem::path & /*repo_root*/) {
    // A minimal verified wire table (one I64 node + one node-input root) so
    // the fall-through NodeOutput branch mints a valid node-0-input binding.
    irc::CoreWireSchemaTable wire;
    wire.format_version = 1;
    irc::CoreWireSchemaNode node;
    node.shape = irc::CoreWireSchemaInt{};
    wire.nodes.push_back(node);
    wire.frame_roots = irc::CoreWireFrameRoots{
        irc::CoreWireSchemaNodeId{0}, irc::CoreWireSchemaNodeId{0},
        {irc::CoreWireSchemaNodeId{0}}, {irc::CoreWireSchemaNodeId{0}}};
    auto wire_copy = wire;
    auto verified = irc::make_verified_wire_schema_table(std::move(wire));
    check(verified.table.has_value(), "p2f.verified");
    if (!verified.table.has_value()) {
        return;
    }

    // A real engine with a live 64 KiB memory page (the identity module's
    // memory) so the fall-through pack_value_at has a page to write into.
    wh::Wasm3ResumeEngine engine;
    const auto bytes = wht::identity_module();
    auto instantiated = engine.fresh_instance(
        std::span<const std::uint8_t>(bytes),
        [](const eng::ImportObservation &) -> eng::ImportCallbackResult {
            return eng::ImportAbort{}; // never called: no imports on identity
        });
    check(instantiated.has_value(), "p2f.fresh_instance");
    if (!instantiated.has_value()) {
        return;
    }

    // A frame section with valid 8-aligned shadow/payload spans inside the
    // page, and one I64 layout the corrupt site packs against.
    irc::CoreFrameLayoutSection section;
    section.transcode_shadow_base = 256;
    section.transcode_shadow_extent = 64;
    section.transcode_payload_base = 1024;
    section.transcode_payload_capacity = 64;
    irc::CoreLayout layout;
    layout.size = 8;
    layout.align = 8;
    layout.is_zero_sized = false;
    layout.shape = irc::CoreLayoutScalar{irc::CoreScalarRepr::I64};
    section.table.layouts.push_back(layout);

    ahfl::backends::CoreWasmExecutionDescriptor descriptor;
    // One node so target_node_ordinal=0 passes the bounds check, forcing the
    // fall-through to reach the node-frame binding mint.
    descriptor.nodes.emplace_back();
    descriptor.nodes.back().p6_block_ordinal = 0;
    std::uint32_t arena_cursor = 0;
    wh::TranscodeConfig config{
        engine, section, wire_copy, descriptor, *verified.table, arena_cursor};

    // The corrupt site: JsonToP4D + CapabilityParam, targeting node 0.
    irc::CoreFrameTranscodeSite site;
    site.direction = irc::CoreFrameTranscodeSite::Direction::JsonToP4D;
    site.source = irc::CoreFrameTranscodeSite::Source::CapabilityParam;
    site.target_node_ordinal = 0;
    site.layout = irc::CoreLayoutId{0};

    // Valid JSON matching the I64 node-input binding. Without the P2-F check
    // the fall-through mints the binding, decodes "42", packs it into the
    // shadow, and returns AHFL_CAP_OK -- so this test is a true mutation
    // test: it FAILS without the P2-F early-return (the corrupt transcode
    // succeeds) and PASSES with it (rejected at the CapabilityParam check).
    std::string json = "42";
    eng::ImportObservation obs;
    obs.param_frame = std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t *>(json.data()), json.size());

    auto result = wh::handle_transcode(config, site, obs);
    const auto *reply = std::get_if<eng::ImportReply>(&result);
    check(reply != nullptr, "p2f.result_is_reply");
    if (reply != nullptr) {
        check(reply->raw_status == AHFL_CAP_ERROR,
              "p2f.capability_param_json_to_p4d_rejected");
    }
}

}  // namespace

int main() {
    const auto repo = ahfl::test_support::repo_root_from_source_file(__FILE__);

    test_scalar_roundtrip(repo);
    test_payload_enum(repo);
    test_collection_backing(repo);
    test_string_arenas(repo);
    test_runv_root_auth(repo);
    test_schema_fail_closed(repo);
    test_region_zeroing(repo);
    test_oracle_parity(repo);
    test_page_precondition(repo);
    test_duplicate_placement_rejected(repo);
    test_transcode_capability_param_json_to_p4d_rejected(repo);

    std::cout << g_pass_count << "/" << g_test_count << " checks passed\n";
    return g_pass_count == g_test_count ? 0 : 1;
}
