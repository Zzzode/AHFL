// RFC 0026 KR6.8 WH-4: unit tests for the state-trace ring decoder
// (src/runtime/wasm_host/state_trace_decoder.cpp).
//
// Two real-run KATs plus synthetic fail-closed tests:
//   1. trace_ring_kat: a P6-frame workflow (wh4_trace_workflow) with a
//      computed-goto routing agent, driven on the REAL wasm3 engine through
//      run_workflow with a host-packed P6 input. The decoded trace ring is pinned
//      against the descriptor's all_states for BOTH branches (n=1 -> High,
//      n=0 -> Low), proving the ring captures input-dependent computed
//      handlers a static walk cannot derive.
//   2. node_event_kat: a P6 capability workflow (e3_capability_workflow)
//      driven on the REAL wasm3 engine through run_workflow with an echo import
//      callback. The decoded node-event buffer (88 bytes = 8 header + 2*40
//      records) is pinned against the descriptor's schedule -- the same
//      common-KAT the JS oracle pins byte-for-byte against the golden hex.
//   3. synthetic: hand-built rings exercising every fail-closed error
//      (Truncated, BadCapacity, CountExceedsCapacity, RunnerStateOutOfRange)
//      plus the zero-capacity shortcut and prefix-decode support.
//
// Every module is emitted in-process through the real wasm backend (no
// shelling out, no hand-built bytes for the execution tests) and driven on
// the real wasm3 engine, so the tests prove the REAL pipeline:
// emit -> pack -> run_workflow -> decode trace ring / node events.

#include "runtime/wasm_host/state_trace_decoder.hpp"
#include "runtime/wasm_host/frame_packer.hpp"
#include "runtime/wasm_host/frame_reader.hpp"
#include "runtime/wasm_host/frame_walk.hpp"
#include "runtime/wasm_host/wasm3_engine.hpp"

#include "runtime/engine/core_wasm_node_events.hpp"
#include "runtime/engine/core_wasm_resume_engine.hpp"
#include "runtime/engine/core_wasm_resume_record.hpp"
#include "runtime/engine/core_wasm_schema_module.hpp"
#include "runtime/value/value.hpp"
#include "runtime/value/value_json.hpp"
#include "runtime/wasm_host/capability_import.hpp"

#include "ahfl/compiler/ir/core_frame_layout.hpp"
#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/core_layout.hpp"
#include "ahfl/compiler/ir/core_wasm_abi_constants.hpp"
#include "ahfl/compiler/ir/core_wire_schema.hpp"
#include "compiler/backends/wasm/core_wasm_codegen.hpp"
#include "conformance/compile_source.hpp"

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
namespace eng = ahfl::runtime::core_wasm_resume_engine;
namespace resume = ahfl::runtime::core_wasm_resume;
namespace ne = ahfl::runtime::core_wasm_node_events;
namespace csm = ahfl::runtime::core_wasm_schema_module;
namespace conf = ahfl::conformance;

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

// Emit a real wasm workflow module from a source .ahfl file. Returns the
// module bytes and the machine-readable execution descriptor.
struct EmittedWorkflow {
    std::vector<std::uint8_t> module_bytes;
    ahfl::backends::CoreWasmExecutionDescriptor descriptor;
};

[[nodiscard]] std::optional<EmittedWorkflow>
emit_workflow(const std::filesystem::path &source_path) {
    std::string error;
    auto program = conf::compile_conformance_source(source_path, error);
    if (!program.has_value()) {
        std::cerr << "  compile failed: " << error << "\n";
        return std::nullopt;
    }
    const auto core = irc::lower_ahfl_to_core(*program);
    if (!core.ok()) {
        std::cerr << "  core lower failed\n";
        for (const auto &d : core.diagnostics) {
            std::cerr << "    [" << d.code << "] " << d.message << "\n";
        }
        return std::nullopt;
    }
    const auto layouts = irc::compute_core_layouts(core.program);
    if (!layouts.ok() || !layouts.table.has_value()) {
        std::cerr << "  layout failed\n";
        return std::nullopt;
    }
    const auto emitted = ahfl::backends::emit_core_wasm(
        core.program, *layouts.table,
        {irc::CoreWorkflowId{0}, ahfl::backends::WasmProfileKind::Wasi});
    if (!emitted.artifact.has_value()) {
        std::cerr << "  emit failed: no artifact\n";
        for (const auto &d : emitted.diagnostics) {
            std::cerr << "    [" << d.code << "] " << d.message << "\n";
        }
        return std::nullopt;
    }
    if (!emitted.descriptor.has_value()) {
        std::cerr << "  emit failed: no descriptor\n";
        return std::nullopt;
    }
    return EmittedWorkflow{
        .module_bytes = emitted.artifact->bytes,
        .descriptor = std::move(*emitted.descriptor),
    };
}

// Find a state's dense id by name in the descriptor's all_states table.
[[nodiscard]] std::uint32_t
state_id(const std::vector<std::string> &all_states, const std::string &name) {
    for (std::size_t i = 0; i < all_states.size(); ++i) {
        if (all_states[i] == name) {
            return static_cast<std::uint32_t>(i);
        }
    }
    return UINT32_MAX;
}

// Build the string regions for P6-frame output reading (mirrors the
// workflow_session's build_string_regions).
[[nodiscard]] std::vector<wh::StringRegion>
build_string_regions(const irc::CoreFrameLayoutSection &section) {
    std::vector<wh::StringRegion> regions;
    if (section.rodata_extent > 0) {
        regions.push_back(
            {section.rodata_base, section.rodata_base + section.rodata_extent});
    }
    if (section.entry_payload_capacity > 0) {
        regions.push_back({section.entry_payload_base,
                           section.entry_payload_base +
                               section.entry_payload_capacity});
    }
    for (const auto &site : section.bridge_call_sites) {
        if (site.result_payload_capacity > 0) {
            regions.push_back({site.result_payload_base,
                               site.result_payload_base +
                                   site.result_payload_capacity});
        }
    }
    return regions;
}

// ==== 1. trace ring KAT (P6-frame workflow, real wasm3 run_workflow) ====

void test_trace_ring_kat(const std::filesystem::path &repo_root) {
    const auto source = repo_root / "tests/golden/wasm/wh4_trace_workflow.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "trace_kat.emit");
    if (!wf.has_value()) {
        return;
    }

    const auto &desc = wf->descriptor;
    check(desc.is_workflow, "trace_kat.is_workflow");

    const auto &section = desc.frame_section;
    const auto &wire = desc.wire_schema;

    check(section.state_trace_capacity > 0, "trace_kat.trace_capacity > 0");
    check(section.node_blocks.size() == 1, "trace_kat.one_node_block");
    check(desc.agents.size() == 1, "trace_kat.one_agent");
    if (section.node_blocks.empty() || desc.agents.empty()) {
        return;
    }

    const auto &block = section.node_blocks[0];
    const auto &all_states = desc.agents[0].all_states;
    check(all_states.size() == 4, "trace_kat.four_states");
    if (all_states.size() != 4) {
        return;
    }

    const auto decide_id = state_id(all_states, "Decide");
    const auto high_id = state_id(all_states, "High");
    const auto low_id = state_id(all_states, "Low");
    const auto done_id = state_id(all_states, "Done");
    check(decide_id != UINT32_MAX && high_id != UINT32_MAX &&
              low_id != UINT32_MAX && done_id != UINT32_MAX,
          "trace_kat.all_states_found");
    if (decide_id == UINT32_MAX || high_id == UINT32_MAX ||
        low_id == UINT32_MAX || done_id == UINT32_MAX) {
        return;
    }

    check(wire.frame_roots.has_value(), "trace_kat.has_frame_roots");
    if (!wire.frame_roots.has_value()) {
        return;
    }
    check(wire.frame_roots->node_inputs.size() == 1,
          "trace_kat.one_node_input");
    if (wire.frame_roots->node_inputs.empty()) {
        return;
    }
    const auto wId = wire.frame_roots->node_inputs[0];
    const auto lId = block.input_layout;

    // Run for n=1 (Decide -> High -> Done) and n=0 (Decide -> Low -> Done).
    struct BranchCase {
        int n;
        std::uint32_t mid_state;
        const char *label;
    };
    const BranchCase cases[] = {
        {1, high_id, "n1"},
        {0, low_id, "n0"},
    };

    for (const auto &bc : cases) {
        const std::string prefix = std::string("trace_kat.") + bc.label;
        auto input = value_from_json(
            std::string("{\"_type\":\"wasm::wh4_trace::Frame\",\"n\":") +
            std::to_string(bc.n) + "}");
        check(input.has_value(), prefix + ".input");
        if (!input.has_value()) {
            return;
        }

        wh::Wasm3ResumeEngine engine;
        auto inst = engine.fresh_instance(
            wf->module_bytes,
            [](const eng::ImportObservation &) -> eng::ImportCallbackResult {
                return eng::ImportAbort{}; // no imports on this module
            });
        check(inst.has_value(), prefix + ".fresh_instance");
        if (!inst.has_value()) {
            return;
        }

        auto page = engine.mutable_whole_memory();
        check(page.has_value(), prefix + ".mutable_memory");
        if (!page.has_value()) {
            return;
        }

        wh::FrameWalkContext ctx(section, wire);
        std::uint32_t arena_cursor = section.entry_payload_base;
        auto packed = wh::pack_value_at(
            ctx, *page, wId, lId, *input, block.input_base, arena_cursor,
            section.entry_payload_base, section.entry_payload_capacity);
        check(packed.has_value(), prefix + ".pack");
        if (!packed.has_value()) {
            return;
        }

        auto outcome = engine.invoke_run_workflow(
            eng::GuestPointer{block.input_base}, block.input_size);
        check(outcome.has_value(), prefix + ".invoke_run_workflow");
        if (!outcome.has_value()) {
            return;
        }

        const auto *tuple = std::get_if<eng::WorkflowRunResultTuple>(&*outcome);
        check(tuple != nullptr, prefix + ".result_tuple");
        if (tuple == nullptr) {
            return;
        }
        check(tuple->raw_status == 0, prefix + ".status_ok");
        check(tuple->output_ptr.value == section.workflow_output_base,
              prefix + ".output_base");

        auto mem = engine.read_whole_memory();
        check(mem.has_value(), prefix + ".read_memory");
        if (!mem.has_value()) {
            return;
        }

        auto decoded = wh::decode_state_trace(
            *mem, section.state_trace_base, section.state_trace_capacity);
        check(decoded.has_value(), prefix + ".decode");
        if (!decoded.has_value()) {
            return;
        }

        const std::vector<wh::StateTraceRecord> expected = {
            {0, decide_id}, {0, bc.mid_state}, {0, done_id}};
        check(*decoded == expected, prefix + ".records");

        // Validate bounds against the descriptor's agent table.
        const std::vector<std::uint32_t> state_counts = {
            static_cast<std::uint32_t>(all_states.size())};
        auto valid = wh::validate_state_trace_bounds(*decoded, 1, state_counts);
        check(valid.has_value(), prefix + ".bounds_valid");
    }
}

// ==== 2. node-event KAT (P6 capability workflow, real wasm3 run_workflow) ====

void test_node_event_kat(const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/e3_capability_workflow.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "node_event_kat.emit");
    if (!wf.has_value()) {
        return;
    }

    const auto &desc = wf->descriptor;
    check(desc.is_workflow, "node_event_kat.is_workflow");
    check(desc.workflow_node_count == 2, "node_event_kat.two_nodes");
    check(desc.imports.size() == 1, "node_event_kat.one_import");
    if (desc.imports.empty()) {
        return;
    }

    // The canonical workflow input JSON (sorted keys, no whitespace).
    const std::string input_json =
        "{\"_type\":\"wasm::e3_capability_workflow::Frame\",\"value\":\"identity\"}";
    auto input = value_from_json(input_json);
    check(input.has_value(), "node_event_kat.input_json");
    if (!input.has_value()) {
        return;
    }

    wh::Wasm3ResumeEngine engine;

    // Build the production bridge callback (the same executor the
    // workflow_session and wasm_agent_runner use). The Echo invoker returns
    // its single argument unchanged; the bridge executor packs the result
    // into the call site's result region.
    auto admitted = csm::make_verified_core_wasm_schema_module(wf->module_bytes);
    check(admitted.ok() && admitted.module.has_value(),
          "node_event_kat.wire_admission");
    if (!admitted.ok() || !admitted.module.has_value()) {
        return;
    }

    const auto &frame_section = desc.frame_section;
    ahfl::runtime::CapabilityInvocationContext invocation_context;
    invocation_context.agent_id = ahfl::runtime::AgentId{0};

    ahfl::runtime::ContextualCapabilityInvoker echo_invoker =
        [](const ahfl::runtime::CapabilityInvocationContext &,
           const std::string &,
           const std::vector<Value> &args)
        -> ahfl::runtime::CapabilityCallResult {
        ahfl::runtime::CapabilityCallResult r;
        r.status = ahfl::runtime::CapabilityCallStatus::Success;
        if (!args.empty()) {
            r.value = ahfl::runtime::clone_value(args[0]);
        }
        return r;
    };

    std::function<std::optional<std::string>(std::uint64_t)> name_resolver =
        [&desc](std::uint64_t source_symbol) -> std::optional<std::string> {
        for (const auto &node : desc.nodes) {
            if (!node.has_capability) {
                continue;
            }
            if (!node.all_capabilities.empty()) {
                for (const auto &[ordinal, sym] : node.all_capabilities) {
                    if (sym == source_symbol &&
                        ordinal < desc.imports.size()) {
                        return desc.imports[ordinal].canonical_name;
                    }
                }
            }
        }
        return std::nullopt;
    };

    wh::CapabilityImportState import_state;
    wh::CapabilityImportConfig cap_config{
        .engine = engine,
        .module = *admitted.module,
        .frame_section = frame_section,
        .invoker = echo_invoker,
        .context = invocation_context,
        .name_resolver = std::move(name_resolver),
        .state = import_state,
    };
    auto echo_callback =
        wh::make_capability_import_callback(std::move(cap_config));

    auto inst = engine.fresh_instance(wf->module_bytes, std::move(echo_callback));
    check(inst.has_value(), "node_event_kat.fresh_instance");
    if (!inst.has_value()) {
        return;
    }

    // Pack the workflow input as a P4-D frame into the entry node's I_k block
    // (the P6-only world has no wire-JSON entry lane; run_workflow receives the P4-D
    // frame pointer, not a JSON pointer).
    const auto &section = desc.frame_section;
    const auto &wire = desc.wire_schema;
    check(wire.frame_roots.has_value(), "node_event_kat.has_frame_roots");
    if (!wire.frame_roots.has_value()) {
        return;
    }
    const auto p6_ord = desc.nodes[0].p6_block_ordinal;
    check(p6_ord < section.node_blocks.size(),
          "node_event_kat.p6_ord_in_range");
    if (p6_ord >= section.node_blocks.size()) {
        return;
    }
    const auto &block = section.node_blocks[p6_ord];
    const auto w_id = wire.frame_roots->node_inputs[p6_ord];
    const auto l_id = block.input_layout;

    auto page = engine.mutable_whole_memory();
    check(page.has_value(), "node_event_kat.mutable_memory");
    if (!page.has_value()) {
        return;
    }
    // Zero the reserved named regions so padding/union words are 0 (the same
    // discipline as the workflow_session entry pack).
    std::fill(page->begin() + irc::kP6AggregateInputBase,
              page->begin() + irc::kP6CollectionBackingBase,
              std::uint8_t{0});

    wh::FrameWalkContext pack_ctx(section, wire);
    std::uint32_t arena_cursor = section.entry_payload_base;
    auto packed = wh::pack_value_at(pack_ctx, *page, w_id, l_id, *input,
                                    block.input_base, arena_cursor,
                                    section.entry_payload_base,
                                    section.entry_payload_capacity);
    check(packed.has_value(), "node_event_kat.pack_input");
    if (!packed.has_value()) {
        return;
    }

    auto outcome = engine.invoke_run_workflow(
        eng::GuestPointer{block.input_base}, block.input_size);
    check(outcome.has_value(), "node_event_kat.invoke_run_workflow");
    if (!outcome.has_value()) {
        return;
    }

    const auto *tuple = std::get_if<eng::WorkflowRunResultTuple>(&*outcome);
    check(tuple != nullptr, "node_event_kat.result_tuple");
    if (tuple == nullptr) {
        return;
    }
    check(tuple->raw_status == 0, "node_event_kat.status_ok");

    // The output must equal the input (Echo echoes, SecondAgent is identity).
    // In the P6-only world the output is a binary P4-D frame at the workflow
    // output base; read it through the frame reader and compare as JSON.
    auto mem = engine.read_whole_memory();
    check(mem.has_value(), "node_event_kat.read_memory");
    if (!mem.has_value()) {
        return;
    }
    check(desc.wire_schema.frame_roots.has_value(),
          "node_event_kat.has_frame_roots");
    if (!desc.wire_schema.frame_roots.has_value()) {
        return;
    }
    {
        const auto &section = desc.frame_section;
        const auto &wire = desc.wire_schema;
        const auto string_regions = build_string_regions(section);
        wh::FrameWalkContext ctx(section, wire);
        auto output = wh::read_value_at(
            ctx, *mem, wire.frame_roots->output, section.output_layout,
            section.workflow_output_base, string_regions);
        check(output.has_value(), "node_event_kat.output_read");
        if (output.has_value()) {
            const auto output_json = ahfl::runtime::value_to_json(*output);
            check(output_json == input_json, "node_event_kat.output_echo");
        }
    }

    // Decode the node-event buffer (88 bytes = 8 header + 2 * 40 records).
    auto events = ne::decode_node_events(*mem, desc.workflow_node_count);
    check(events.has_value(), "node_event_kat.decode_events");
    if (!events.has_value()) {
        return;
    }
    check(events->size() == 2, "node_event_kat.two_records");
    if (events->size() != 2) {
        return;
    }

    // Record 0: capability node (Echo, source_symbol from descriptor).
    const auto &r0 = (*events)[0];
    check(r0.kind == resume::NodeKind::Capability,
          "node_event_kat.r0.capability");
    check(r0.workflow_node_id.value == 0, "node_event_kat.r0.node_id");
    check(r0.schedule_pos.value == 0, "node_event_kat.r0.schedule_pos");
    check(r0.capability.value == 0, "node_event_kat.r0.capability_ordinal");
    check(!desc.nodes[0].all_capabilities.empty() &&
              r0.source_symbol == desc.nodes[0].all_capabilities[0].second,
          "node_event_kat.r0.source_symbol");
    check(r0.status == 0, "node_event_kat.r0.status_ok");

    // Record 1: identity node.
    const auto &r1 = (*events)[1];
    check(r1.kind == resume::NodeKind::Identity,
          "node_event_kat.r1.identity");
    check(r1.workflow_node_id.value == 1, "node_event_kat.r1.node_id");
    check(r1.schedule_pos.value == 1, "node_event_kat.r1.schedule_pos");
    check(r1.capability.value == 0, "node_event_kat.r1.capability_zero");
    check(r1.source_symbol == 0, "node_event_kat.r1.source_symbol_zero");
    check(r1.status == 0, "node_event_kat.r1.status_ok");
}

// ==== 3. synthetic fail-closed tests ====

void test_synthetic_errors() {
    // Truncated: memory too small for the 8-byte count header.
    {
        const std::vector<std::uint8_t> small(4, 0);
        auto r = wh::decode_state_trace(small, 0, 100);
        check(!r.has_value() && r.error() == wh::StateTraceError::Truncated,
              "synthetic.truncated");
    }

    // BadCapacity: trace_base + trace_capacity exceeds memory (header fits).
    {
        const std::vector<std::uint8_t> mem(65536, 0);
        auto r = wh::decode_state_trace(mem, 65528, 20);
        check(!r.has_value() && r.error() == wh::StateTraceError::BadCapacity,
              "synthetic.bad_capacity");
    }

    // BadCapacity: trace_base + trace_capacity overflows u32 (header fits).
    {
        const std::vector<std::uint8_t> mem(65536, 0);
        auto r = wh::decode_state_trace(mem, 65528, UINT32_MAX);
        check(!r.has_value() && r.error() == wh::StateTraceError::BadCapacity,
              "synthetic.bad_capacity_overflow");
    }

    // CountExceedsCapacity: count=100 but capacity holds only 1 record.
    {
        std::vector<std::uint8_t> mem(65536, 0);
        mem[0] = 100; // count = 100 (LE u32, low byte)
        auto r = wh::decode_state_trace(mem, 0, 16); // 8 header + 1 record
        check(!r.has_value() &&
                  r.error() == wh::StateTraceError::CountExceedsCapacity,
              "synthetic.count_exceeds");
    }

    // RunnerStateOutOfRange: runner=5 but runner_count=1.
    {
        std::vector<std::uint8_t> mem(65536, 0);
        mem[0] = 1; // count = 1
        mem[8] = 5; // runner = 5 (LE u32, low byte)
        auto decoded = wh::decode_state_trace(mem, 0, 64);
        check(decoded.has_value(), "synthetic.runner_oor.decode");
        if (decoded.has_value()) {
            const std::vector<std::uint32_t> state_counts = {4};
            auto valid =
                wh::validate_state_trace_bounds(*decoded, 1, state_counts);
            check(!valid.has_value() &&
                      valid.error() == wh::StateTraceError::RunnerStateOutOfRange,
                  "synthetic.runner_oor.bounds");
        }
    }

    // RunnerStateOutOfRange: state=99 but runner 0 has only 4 states.
    {
        std::vector<std::uint8_t> mem(65536, 0);
        mem[0] = 1;  // count = 1
        mem[8] = 0;  // runner = 0
        mem[12] = 99; // state = 99 (LE u32, low byte)
        auto decoded = wh::decode_state_trace(mem, 0, 64);
        check(decoded.has_value(), "synthetic.state_oor.decode");
        if (decoded.has_value()) {
            const std::vector<std::uint32_t> state_counts = {4};
            auto valid =
                wh::validate_state_trace_bounds(*decoded, 1, state_counts);
            check(!valid.has_value() &&
                      valid.error() == wh::StateTraceError::RunnerStateOutOfRange,
                  "synthetic.state_oor.bounds");
        }
    }

    // RunnerStateOutOfRange: bounds table size mismatch.
    {
        const std::vector<wh::StateTraceRecord> records = {{0, 0}};
        const std::vector<std::uint32_t> state_counts = {4, 5};
        auto valid =
            wh::validate_state_trace_bounds(records, 1, state_counts);
        check(!valid.has_value() &&
                  valid.error() == wh::StateTraceError::RunnerStateOutOfRange,
              "synthetic.bounds_table_mismatch");
    }
}

// ==== 4. zero-capacity shortcut + prefix decode ====

void test_zero_capacity_and_prefix() {
    // Zero capacity: no trace ring; returns empty vector.
    {
        const std::vector<std::uint8_t> mem(65536, 0);
        auto r = wh::decode_state_trace(mem, 0, 0);
        check(r.has_value() && r->empty(), "synthetic.zero_capacity");
    }

    // Prefix decode: a ring with 3 fully-written records decodes exactly 3.
    // (The guest writes records before bumping the count, so a prefix read at
    // an import boundary always sees a consistent prefix of the final trace.)
    {
        std::vector<std::uint8_t> mem(65536, 0);
        mem[0] = 3; // count = 3
        // record 0: runner=0, state=3
        mem[8] = 0;
        mem[12] = 3;
        // record 1: runner=0, state=1
        mem[16] = 0;
        mem[20] = 1;
        // record 2: runner=0, state=0
        mem[24] = 0;
        mem[28] = 0;
        auto decoded = wh::decode_state_trace(mem, 0, 64);
        check(decoded.has_value(), "synthetic.prefix.decode");
        if (decoded.has_value()) {
            check(decoded->size() == 3, "synthetic.prefix.three_records");
            const std::vector<wh::StateTraceRecord> expected = {
                {0, 3}, {0, 1}, {0, 0}};
            check(*decoded == expected, "synthetic.prefix.records");
        }
    }
}

} // namespace

int main() {
    const auto repo = ahfl::test_support::repo_root_from_source_file(__FILE__);

    test_trace_ring_kat(repo);
    test_node_event_kat(repo);
    test_synthetic_errors();
    test_zero_capacity_and_prefix();

    std::cout << g_pass_count << "/" << g_test_count << " checks passed\n";
    return g_pass_count == g_test_count ? 0 : 1;
}
