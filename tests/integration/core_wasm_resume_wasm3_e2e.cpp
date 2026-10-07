// RFC 0026 KR6.8 WH-3: end-to-end resume test over the REAL wasm3 engine with
// the production capability-import executor as the import callback. Unlike the
// unit tests (which drive the executor directly with hand-built observations),
// this TU goes through the full invoke_run2 path: the engine's trampoline
// calls the executor, the executor invokes a scripted capability through the
// ContextualCapabilityInvoker, and the module's compiled classifier arms
// consume the raw ahfl_cap_status reply.
//
// Three evidence classes:
//   1. Live cap invocation through the executor (opaque lane, OK / ERROR /
//      PENDING), proving the full module -> import -> executor -> invoker ->
//      reply -> classifier -> run2-return path over real wasm3.
//   2. Suspended -> Injected -> Consumed replay: a first run suspends
//      (PENDING), the host injects the result (scripted Success on a fresh
//      instance), and the second run consumes it (OK). This is the D1b
//      replay controller's flow exercised at the executor + engine layer
//      (WH-3 does NOT deliver the durable store; the injection is simulated
//      by the scripted mock, exactly as the Node e2e simulates the host).
//   3. ImportAbort on engine fault: the executor returns ImportAbort (unknown
//      capability name), and invoke_run2 unwinds as Run2HostAborted (distinct
//      from Run2Trapped).
//
// The bridge lane (i32)->(i32,i32) is covered by the unit tests
// (ahfl_wasm_host_capability_import_tests) AND by the real bridge e2e below
// (test_bridge_e2e): WH-3 made A2 admission accept the bridge functype
// alongside the opaque tuple, and the WH-4 fix-forward (D-C) made the codegen
// emit a REAL AHFLXM exec-manifest for every capability agent (bridge-lane
// included), so a real emitted bridge module (v2c_single_arg_bridge) is
// A2-admitted directly (no synthetic manifest injection, no frame admit) and
// driven end-to-end through the capability_import executor on the real wasm3
// engine.

#include "runtime/wasm_host/capability_import.hpp"
#include "runtime/wasm_host/frame_packer.hpp"
#include "runtime/wasm_host/frame_reader.hpp"
#include "runtime/wasm_host/p6_frame_driver.hpp"
#include "runtime/wasm_host/wasm3_engine.hpp"

#include "runtime/engine/core_wasm_frame_module.hpp"
#include "runtime/engine/core_wasm_resume_host.hpp"
#include "runtime/value/value_json.hpp"

#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/core_layout.hpp"
#include "ahfl/compiler/ir/core_wasm_abi_constants.hpp"
#include "ahfl/compiler/ir/core_wire_schema.hpp"
#include "compiler/backends/wasm/core_wasm_codegen.hpp"
#include "common/project_input_support.hpp"
#include "conformance/compile_source.hpp"

#include "unit/runtime/wasm_host/wasm_host_test_support.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace eng = ahfl::runtime::core_wasm_resume_engine;
namespace wht = ahfl::runtime::wasm_host_test_support;
namespace csm = ahfl::runtime::core_wasm_schema_module;
namespace wh = ahfl::runtime::wasm_host;
namespace ir = ahfl::ir;
namespace runtime = ahfl::runtime;
namespace rts = ahfl::runtime::resume_test_support;
namespace host = ahfl::runtime::core_wasm_resume_host;
namespace ps = ahfl::runtime::payload_store;
namespace rc = ahfl::runtime::core_wasm_resume_controller;

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

// A scripted invoker the e2e tests drive. The `result` member is mutable so a
// replay test can flip PENDING -> OK between runs.
struct ScriptedInvoker {
    runtime::CapabilityCallResult result;
    int call_count{0};

    runtime::ContextualCapabilityInvoker as_invoker() {
        return [this](const runtime::CapabilityInvocationContext &,
                      const std::string &,
                      const std::vector<runtime::Value> &) -> runtime::CapabilityCallResult {
            ++call_count;
            return std::move(result);
        };
    }
};

// Create a fresh engine + executor for one invoke_run2 call. The `module_bytes`
// must already carry the A2 admission sections. `name_resolver` resolves
// source_symbol -> capability name.
struct E2eSession {
    wh::Wasm3ResumeEngine engine;
    wh::CapabilityImportState state;
    ScriptedInvoker &mock;
    ir::core::CoreFrameLayoutSection section; // unused by opaque lane
    runtime::CapabilityInvocationContext context;
    csm::VerifiedCoreWasmSchemaModuleResult admitted;
    runtime::ContextualCapabilityInvoker invoker;
    bool instantiated{false};

    E2eSession(std::span<const std::uint8_t> module_bytes,
               ScriptedInvoker &m,
               std::function<std::optional<std::string>(std::uint64_t)> nr)
        : mock(m),
          admitted(csm::make_verified_core_wasm_schema_module(module_bytes)),
          invoker(mock.as_invoker()) {
        if (!admitted.ok()) {
            return;
        }
        wh::CapabilityImportConfig config{
            .engine = engine,
            .module = *admitted.module,
            .frame_section = section,
            .invoker = invoker,
            .context = context,
            .name_resolver = std::move(nr),
            .state = state,
        };
        auto callback = wh::make_capability_import_callback(std::move(config));
        auto inst = engine.fresh_instance(module_bytes, std::move(callback));
        instantiated = inst.has_value();
        if (!instantiated) {
            std::cerr << "FAIL: fresh_instance: "
                      << static_cast<int>(inst.error()) << "\n";
        }
    }
};

// ==== bridge lane e2e (real emitted bridge module) ====

namespace irc = ahfl::ir::core;
namespace conf = ahfl::conformance;

struct EmittedBridgeFixture {
    std::vector<std::uint8_t> module_bytes;
    irc::CoreFrameLayoutSection layout;
    std::optional<irc::VerifiedWireSchemaBinding> input_binding;
    std::optional<irc::VerifiedWireSchemaBinding> output_binding;
    wh::P6FinalKind final_kind{wh::P6FinalKind::Identity};
};

// Emit the real v2c_single_arg_bridge agent in-process. The agent now carries
// a REAL AHFLXM exec-manifest (WH-4 fix-forward D-C: every capability agent
// emits one, bridge-lane included), so the host A2-admits the genuine bytes
// directly -- no synthetic manifest injection. The frame section + wire schema
// come from the codegen descriptor (the same authorities the frame admit would
// have produced), and the input/output bindings are minted from the verified
// wire schema. Returns nullopt on any pipeline failure.
std::optional<EmittedBridgeFixture> emit_bridge_fixture() {
    namespace fs = std::filesystem;
    const fs::path repo = ahfl::test_support::repo_root_from_source_file(__FILE__);
    const fs::path fixture = repo / "tests" / "golden" / "wasm" / "v2c_single_arg_bridge.ahfl";
    std::string error;
    auto program = conf::compile_conformance_source(fixture, error);
    if (!program.has_value()) {
        std::cerr << "  bridge compile failed: " << error << "\n";
        return std::nullopt;
    }
    const auto core = irc::lower_ahfl_to_core(*program);
    if (!core.ok()) {
        std::cerr << "  bridge core lower failed\n";
        return std::nullopt;
    }
    const auto layouts = irc::compute_core_layouts(core.program);
    if (!layouts.ok() || !layouts.table.has_value()) {
        std::cerr << "  bridge layout failed\n";
        return std::nullopt;
    }
    const auto emitted = ahfl::backends::emit_core_wasm(
        core.program, *layouts.table,
        {irc::CoreAgentId{0}, ahfl::backends::WasmProfileKind::Wasi});
    if (!emitted.ok() || !emitted.artifact.has_value() || !emitted.descriptor.has_value()) {
        std::cerr << "  bridge emit failed\n";
        return std::nullopt;
    }
    // Mint the input/output frame bindings from the descriptor's verified wire
    // schema (the same authorities admit_core_wasm_frame_sections produces).
    auto verified = irc::make_verified_wire_schema_table(emitted.descriptor->wire_schema);
    if (!verified.table.has_value()) {
        std::cerr << "  bridge wire schema verify failed\n";
        return std::nullopt;
    }
    std::vector<irc::CoreLowerDiagnostic> in_diag;
    auto input_binding = irc::make_frame_binding_from_verified_table(
        *verified.table, {irc::CoreWireFrameRootKind::Input}, in_diag);
    if (!input_binding.has_value()) {
        std::cerr << "  bridge input binding mint failed\n";
        return std::nullopt;
    }
    std::vector<irc::CoreLowerDiagnostic> out_diag;
    auto output_binding = irc::make_frame_binding_from_verified_table(
        *verified.table, {irc::CoreWireFrameRootKind::Output}, out_diag);
    if (!output_binding.has_value()) {
        std::cerr << "  bridge output binding mint failed\n";
        return std::nullopt;
    }

    const wh::P6FinalKind final_kind =
        (emitted.descriptor->frame.final_kind == "computed")
            ? wh::P6FinalKind::Computed
            : wh::P6FinalKind::Identity;
    return EmittedBridgeFixture{
        .module_bytes = emitted.artifact->bytes,
        .layout = emitted.descriptor->frame_section,
        .input_binding = std::move(*input_binding),
        .output_binding = std::move(*output_binding),
        .final_kind = final_kind,
    };
}

// WH-3 bridge e2e: a real emitted bridge module (v2c_single_arg_bridge) is
// A2-admitted directly (it now carries a REAL AHFLXM exec-manifest -- WH-4
// fix-forward D-C emits one for every capability agent, bridge-lane included)
// and driven end-to-end through the capability_import executor on the real
// wasm3 engine. The OK case proves the bridge reply binds the result_base (the
// executor packs the capability result at the call site's disjoint placement
// and the module materializes it); the non-OK case proves a bridge
// Error/Pending reaches the guest's unreachable -> Run2Trapped (the bridge has
// no graceful ERROR/PENDING arm).
void test_bridge_e2e() {
    auto fixture = emit_bridge_fixture();
    check(fixture.has_value(), "bridge e2e: emit + binding mint");
    if (!fixture.has_value()) {
        return;
    }

    // A2-admit the REAL module (no synthetic manifest injection).
    auto admitted = csm::make_verified_core_wasm_schema_module(
        std::span<const std::uint8_t>(fixture->module_bytes));
    check(admitted.ok(), "bridge e2e: A2 admission accepts real bridge module");
    if (!admitted.ok() || !admitted.module.has_value()) {
        return;
    }
    check(admitted.module->call_site_count() == 1, "bridge e2e: one call site");
    check(admitted.module->is_agent(), "bridge e2e: module is an agent");

    // Resolve the sole call site to get the source_symbol for the name_resolver.
    auto cs = admitted.module->resolve(csm::ManifestCallSiteIndex{0});
    check(cs.ok() && cs.call_site.has_value(), "bridge e2e: resolve call site 0");
    if (!cs.ok() || !cs.call_site.has_value()) {
        return;
    }
    const auto source_symbol = cs.call_site->source_symbol();
    auto name_resolver = [source_symbol](std::uint64_t sym) -> std::optional<std::string> {
        if (sym == source_symbol) {
            return std::string{"RouteTicket"};
        }
        return std::nullopt;
    };

    // --- OK case: the mock returns Success + RoutingDecision{owner:"alice"};
    //     the executor packs it at the result placement and the module
    //     materializes the owner through the computed final.
    {
        wh::Wasm3ResumeEngine engine;
        wh::CapabilityImportState state;
        ScriptedInvoker mock;
        mock.result.status = runtime::CapabilityCallStatus::Success;
        auto result_value = runtime::value_from_json(R"({"owner":"alice"})");
        check(result_value.has_value(), "bridge e2e ok: result value built");
        if (!result_value.has_value()) {
            return;
        }
        mock.result.value = std::move(*result_value);
        auto invoker = mock.as_invoker();
        runtime::CapabilityInvocationContext context;
        wh::CapabilityImportConfig config{
            .engine = engine,
            .module = *admitted.module,
            .frame_section = fixture->layout,
            .invoker = invoker,
            .context = context,
            .name_resolver = name_resolver,
            .state = state,
        };
        auto callback = wh::make_capability_import_callback(std::move(config));

        auto input = runtime::value_from_json(R"({"ticket_id":"T-123"})");
        check(input.has_value(), "bridge e2e ok: input value built");
        if (!input.has_value()) {
            return;
        }
        auto output = wh::execute_p6_frame(
            engine, fixture->module_bytes, fixture->layout,
            *fixture->input_binding, *fixture->output_binding, *input,
            fixture->final_kind, std::move(callback));
        check(output.has_value(), "bridge e2e ok: execute_p6_frame succeeded");
        if (output.has_value()) {
            check(output->find("\"owner\":\"alice\"") != std::string::npos,
                  "bridge e2e ok: output carries mock owner 'alice'");
        }
        check(mock.call_count == 1, "bridge e2e ok: invoker called once");
    }

    // --- non-OK case: the mock returns Error; the bridge has no graceful
    //     ERROR/PENDING arm, so the guest traps -> Run2Trapped.
    {
        wh::Wasm3ResumeEngine engine;
        wh::CapabilityImportState state;
        ScriptedInvoker mock;
        mock.result.status = runtime::CapabilityCallStatus::Error;
        auto invoker = mock.as_invoker();
        runtime::CapabilityInvocationContext context;
        wh::CapabilityImportConfig config{
            .engine = engine,
            .module = *admitted.module,
            .frame_section = fixture->layout,
            .invoker = invoker,
            .context = context,
            .name_resolver = name_resolver,
            .state = state,
        };
        auto callback = wh::make_capability_import_callback(std::move(config));

        auto input = runtime::value_from_json(R"({"ticket_id":"T-123"})");
        check(input.has_value(), "bridge e2e err: input value built");
        if (!input.has_value()) {
            return;
        }
        auto output = wh::execute_p6_frame(
            engine, fixture->module_bytes, fixture->layout,
            *fixture->input_binding, *fixture->output_binding, *input,
            fixture->final_kind, std::move(callback));
        check(!output.has_value(), "bridge e2e err: execute_p6_frame failed");
        if (!output.has_value()) {
            const bool trapped =
                std::holds_alternative<wh::RunvError>(output.error()) &&
                std::get<wh::RunvError>(output.error()).kind == wh::RunvError::Kind::Trapped;
            check(trapped, "bridge e2e err: Run2Trapped (no graceful arm)");
        }
        check(mock.call_count == 1, "bridge e2e err: invoker called once");
    }
}


// ==== P1-3: production run_resume on real wasm3 + real IntegrityPayloadStore ====

namespace fs = std::filesystem;

// Emit the e3 capability-workflow fixture in-process (the same real
// frontend -> Core -> B2-C emitter path the probe subprocess uses, but
// without the subprocess dependency).
struct EmittedWorkflowFixture {
    std::vector<std::uint8_t> module_bytes;
    irc::CoreFrameLayoutSection frame_section;
    irc::CoreWireSchemaTable wire_schema;
};

std::optional<EmittedWorkflowFixture> emit_workflow_fixture() {
    const fs::path repo = ahfl::test_support::repo_root_from_source_file(__FILE__);
    const fs::path fixture =
        repo / "tests" / "golden" / "wasm" / "e3_capability_workflow_resume.ahfl";
    std::string error;
    auto program = conf::compile_conformance_source(fixture, error);
    if (!program.has_value()) {
        std::cerr << "  workflow compile failed: " << error << "\n";
        return std::nullopt;
    }
    const auto core = irc::lower_ahfl_to_core(*program);
    if (!core.ok()) {
        std::cerr << "  workflow core lower failed\n";
        return std::nullopt;
    }
    const auto layouts = irc::compute_core_layouts(core.program);
    if (!layouts.ok() || !layouts.table.has_value()) {
        std::cerr << "  workflow layout failed\n";
        return std::nullopt;
    }
    const auto emitted = ahfl::backends::emit_core_wasm(
        core.program, *layouts.table,
        {irc::CoreWorkflowId{0}, ahfl::backends::WasmProfileKind::Wasi});
    if (!emitted.ok() || !emitted.artifact.has_value() || !emitted.descriptor.has_value()) {
        std::cerr << "  workflow emit failed\n";
        return std::nullopt;
    }
    return EmittedWorkflowFixture{.module_bytes = emitted.artifact->bytes,
                                  .frame_section = emitted.descriptor->frame_section,
                                  .wire_schema = emitted.descriptor->wire_schema};
}

// Build a module that passes A2 admission (AHFLXM + AHFLWS + Memory + Type +
// Import) AND has a real Code section whose run2 executes unreachable (a wasm
// trap, not a host abort). This exercises the run_resume trap -> ModuleTrap
// path on the real wasm3 engine.
std::vector<std::uint8_t> build_a2_trap_module() {
    std::vector<std::uint8_t> m = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
    // Type: bridge (i32)->(i32,i32) for the ahfl_cap import, (i32)->i32 for
    // alloc, and (i32,i32)->(i32,i32,i32) for the run2 workflow scheduler entry.
    // RFC 0026 dual-track elimination: every capability import must use the
    // bridge signature; the opaque (i32,i32)->(i32,i32,i32) import type is gone.
    rts::put_section(m, 1,
                     rts::type_payload({rts::func_type({wht::kI32},
                                                       {wht::kI32, wht::kI32}),
                                        rts::func_type({wht::kI32}, {wht::kI32}),
                                        rts::func_type({wht::kI32, wht::kI32},
                                                       {wht::kI32, wht::kI32, wht::kI32})}));
    // Import: 1 ahfl_cap import (type 0, the bridge signature)
    rts::put_section(m, 2, rts::import_payload({{0, 0}}));
    // Function: run2 (type 2, defined index 1), alloc (type 1, defined index 2)
    wht::put_section(m, 3, wht::function_section({2, 1}));
    // Memory: 1 page
    wht::put_section(m, 5, wht::memory_section(1));
    // Global: heap pointer
    wht::put_section(m, 6, wht::global_i32_mut(static_cast<std::int32_t>(wht::kHeapBase)));
    // Export: memory(0), run2(1), alloc(2)
    wht::put_section(m, 7, wht::standard_exports(1, 2));
    // Code: run2 = unreachable, alloc = checked bump allocator
    std::vector<std::uint8_t> run2_body = wht::locals_decl({});
    run2_body.push_back(wht::kOpUnreachable);
    run2_body.push_back(wht::kOpEnd);
    wht::append_standard_code(m, run2_body);
    // AHFLXM: 1 P6 bridge node (workflow_node_id=0, cap_call_count=0, one
    // in-runner bridge site: ordinal=0, call_site_id=0, capability=0, sym=0).
    rts::put_section(m, 0,
                     rts::custom_payload("ahfl.wasm-exec-manifest.v1",
                                         rts::exec_manifest_body(
                                             0, {{0, 0, 0, 0,
                                                  {{0, 0, 0, 0}}}})));
    // AHFLWS: 1 cap (cap_id=0, sym=0, result=node 0) with Int param (node 0)
    rts::put_section(
        m, 0,
        rts::custom_payload(
            "ahfl.wire-schema.v1",
            rts::encode_schema(
                rts::schema_table({}, {rts::CapSpec{0, 0, rts::CoreWireSchemaNodeId{0}}}))));
    return m;
}

// WH-3 P1-3: the production run_resume driver over the REAL wasm3 engine with
// a REAL IntegrityPayloadStore. Four scenarios mirroring the F4 host-driver
// tests and the Node e2e, but on the real wasm3 VM:
//
// 1. Injected memo replay (ReturnMemo): the frontier cap node carries a
//    committed memo; the driver serves it from the store with NO live
//    capability invocation. The driver has no capability invoker by
//    construction -- ReadyForLive is blocked (aborts) -- so a successful
//    resume proves the invoker call count is ZERO.
// 2. Injected-slot publish/ack (NeedInjectedSlot): the Suspended frontier
//    publishes the injected frame to a fresh slot, ACKs, and serves it.
// 3. Module trap -> ModuleTrap: a module whose run2 executes unreachable
//    traps; the driver classifies it ModuleTrap (not a host abort).
// 4. Host abort (arg_hash mismatch): the entry frame's arg_hash does not
//    match the ledgered pending; the controller returns CoordinateMismatch,
//    the import callback aborts, and run_resume surfaces state.abort_failure.
void test_run_resume_wasm3_e2e() {
    // Durable-FS gate (the IntegrityPayloadStore is Linux durable FS only).
    const fs::path store_path = "/tmp/ahfl-wh3-run-resume-wasm3";
    auto store = rts::open_store(store_path);
    if (!store.has_value()) {
        std::cerr << "SKIP: run_resume wasm3 e2e needs a durable FS\n";
        return;
    }

    const auto key = rts::test_key();
    const auto key_id = rts::test_key_id();
    const std::span<const std::uint8_t, 16> id_span(key_id);

    // ---- Emit + A2-admit the e3 workflow module ----
    auto fixture = emit_workflow_fixture();
    check(fixture.has_value(), "run_resume e2e: emit workflow");
    if (!fixture.has_value()) {
        return;
    }
    auto admitted = csm::make_verified_core_wasm_schema_module(
        std::span<const std::uint8_t>(fixture->module_bytes));
    check(admitted.ok(), "run_resume e2e: A2 admission");
    if (!admitted.ok() || !admitted.module.has_value()) {
        return;
    }
    const auto &mod = *admitted.module;
    check(mod.node_count() == 2 && mod.call_site_count() == 1,
          "run_resume e2e: 2 nodes 1 callsite");

    const auto all_specs = rts::module_node_specs(mod);
    const std::string param_json =
        R"({"_type":"wasm::e3_capability_workflow_resume::Frame","value":"workflow-input"})";
    const std::string injected_json =
        R"({"_type":"wasm::e3_capability_workflow_resume::Frame","value":"durable-echo"})";
    const auto entry_bytes = rts::bytes_of(param_json);
    const auto injected_bytes = rts::bytes_of(injected_json);
    const rts::PayloadSlotId entry_slot{9};
    const rts::PayloadSlotId injected_slot{5};

    // P4-D entry frame packer: decode the wire-JSON entry bytes against the
    // entry node's input binding, then pack the Value into the node's fixed
    // I block (the bridge lane requires the entry at the node's fixed input
    // region, not a bump allocation). Mirrors the production workflow
    // session's entry packing (workflow_session.cpp section 7).
    auto entry_packer = [&fixture](
                            eng::CoreWasmResumeEngine &engine,
                            std::span<const std::uint8_t> entry_json)
        -> std::expected<std::pair<eng::GuestPointer, std::uint32_t>,
                         eng::EngineError> {
        const auto &section = fixture->frame_section;
        const auto &wire = fixture->wire_schema;
        if (section.node_blocks.empty() || !wire.frame_roots.has_value() ||
            wire.frame_roots->node_inputs.empty()) {
            return std::unexpected(eng::EngineError::InvalidSequence);
        }
        constexpr std::uint32_t p6_ord = 0; // entry node is the first P6 node
        if (p6_ord >= section.node_blocks.size()) {
            return std::unexpected(eng::EngineError::InvalidSequence);
        }
        const auto &block = section.node_blocks[p6_ord];
        const auto w_id = wire.frame_roots->node_inputs[p6_ord];
        const auto l_id = block.input_layout;

        // Mint the input binding for the entry node.
        auto verified = irc::make_verified_wire_schema_table(wire);
        if (!verified.ok()) {
            return std::unexpected(eng::EngineError::InvalidSequence);
        }
        std::vector<irc::CoreLowerDiagnostic> diags;
        auto binding = irc::make_node_frame_binding_from_verified_table(
            *verified.table,
            irc::CoreWireNodeRootSelector{p6_ord, irc::CoreWireNodeRootKind::Input},
            diags);
        if (!binding.has_value()) {
            return std::unexpected(eng::EngineError::InvalidSequence);
        }

        // Decode the wire-JSON entry bytes.
        auto dom = ahfl::json::parse_json(
            std::string_view(reinterpret_cast<const char *>(entry_json.data()),
                             entry_json.size()));
        if (!dom.has_value() || !*dom) {
            return std::unexpected(eng::EngineError::InvalidSequence);
        }
        auto decoded =
            ahfl::runtime::wire_codec::decode_json(**dom, *binding);
        if (!decoded.ok()) {
            return std::unexpected(eng::EngineError::InvalidSequence);
        }

        // Pack the Value into the entry node's I block.
        auto page = engine.mutable_whole_memory();
        if (!page.has_value()) {
            return std::unexpected(page.error());
        }
        std::fill(page->begin() + irc::kP6AggregateInputBase,
                  page->begin() + irc::kP6CollectionBackingBase,
                  std::uint8_t{0});
        wh::FrameWalkContext ctx(section, wire);
        std::uint32_t arena_cursor = section.entry_payload_base;
        auto packed = wh::pack_value_at(ctx, *page, w_id, l_id, *decoded.value,
                                        block.input_base, arena_cursor,
                                        section.entry_payload_base,
                                        section.entry_payload_capacity);
        if (!packed.has_value()) {
            return std::unexpected(eng::EngineError::InvalidSequence);
        }
        return std::make_pair(eng::GuestPointer{block.input_base},
                              block.input_size);
    };

    // P4-D bridge result packer: resolve the bridge call site from the
    // control-block pointer, decode the wire-JSON result against the call
    // site's result binding, and pack into the fixed result block. Mirrors
    // the production workflow session's memo-replay packing.
    auto result_packer = [&fixture](
                             eng::CoreWasmResumeEngine &engine,
                             const eng::ImportObservation &obs,
                             const csm::VerifiedCoreWasmCallSite &call_site,
                             std::span<const std::uint8_t> result_json)
        -> eng::ImportCallbackResult {
        const auto &section = fixture->frame_section;
        wh::CapabilityImportError resolve_error{};
        auto resolved =
            wh::resolve_bridge_call_site(section, obs.scalar_arg, resolve_error);
        if (!resolved.has_value()) {
            return eng::ImportAbort{};
        }
        auto dom = ahfl::json::parse_json(
            std::string_view(reinterpret_cast<const char *>(result_json.data()),
                             result_json.size()));
        if (!dom.has_value() || !*dom) {
            return eng::ImportAbort{};
        }
        auto decoded = ahfl::runtime::wire_codec::decode_json(
            **dom, call_site.result_binding());
        if (!decoded.ok()) {
            return eng::ImportAbort{};
        }
        wh::CapabilityImportError pack_error{};
        return wh::pack_bridge_result_value(engine, section, call_site,
                                            *resolved->site,
                                            std::move(*decoded.value),
                                            pack_error);
    };

    // Bridge-lane arg hasher: decode the P4-D args from the control block and
    // compute the wire-canonical arg_hash the controller cross-checks against
    // the ledgered memo/pending.
    auto arg_hasher = [&fixture](
                          eng::CoreWasmResumeEngine &,
                          const eng::ImportObservation &obs,
                          const csm::VerifiedCoreWasmCallSite &call_site)
        -> std::optional<std::uint64_t> {
        const auto &section = fixture->frame_section;
        wh::CapabilityImportError resolve_error{};
        auto resolved =
            wh::resolve_bridge_call_site(section, obs.scalar_arg, resolve_error);
        if (!resolved.has_value()) {
            return std::nullopt;
        }
        wh::CapabilityImportError decode_error{};
        wh::ArgDecodeSubReason sub_reason{wh::ArgDecodeSubReason::kUnspecified};
        auto args = wh::decode_bridge_import_args(
            section, call_site, *resolved->site, obs.scalar_arg,
            obs.whole_memory, decode_error, sub_reason);
        if (!args.has_value()) {
            return std::nullopt;
        }
        return ahfl::runtime::hash_values(*args);
    };

    // P4-D workflow output decoder: read the workflow output frame from the
    // engine's whole linear memory and decode it to a runtime::Value via
    // read_value_at (the same primitive the production workflow session
    // uses). Strings live in the rodata / entry-payload / bridge-result
    // arenas, so the whole page (not just the output span) is required.
    auto build_string_regions = [](const irc::CoreFrameLayoutSection &section)
        -> std::vector<wh::StringRegion> {
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
        if (section.construct_heap_base != 0) {
            regions.push_back(
                {section.construct_heap_base,
                 irc::kCoreWasmFixedLinearMemoryCapacityBytes});
        }
        return regions;
    };

    auto decode_workflow_output =
        [&fixture, &build_string_regions](
            eng::CoreWasmResumeEngine &engine) -> std::optional<ahfl::runtime::Value> {
        const auto &section = fixture->frame_section;
        const auto &wire = fixture->wire_schema;
        if (!wire.frame_roots.has_value()) {
            return std::nullopt;
        }
        auto page = engine.read_whole_memory();
        if (!page.has_value()) {
            return std::nullopt;
        }
        auto regions = build_string_regions(section);
        wh::FrameWalkContext ctx(section, wire);
        auto output = wh::read_value_at(
            ctx, *page, wire.frame_roots->output, section.output_layout,
            section.workflow_output_base, regions);
        if (!output.has_value()) {
            return std::nullopt;
        }
        return std::move(*output);
    };

    // Canonical wire-JSON comparison helper: decode the expected wire JSON
    // against the workflow output binding, re-encode both the expected and
    // the P4-D-decoded actual to canonical wire JSON, and compare.
    auto output_matches =
        [&fixture, &decode_workflow_output](
            eng::CoreWasmResumeEngine &engine,
            std::span<const std::uint8_t> expected_json) -> bool {
        const auto &wire = fixture->wire_schema;
        auto verified = irc::make_verified_wire_schema_table(wire);
        if (!verified.ok()) {
            return false;
        }
        // The workflow output binding uses the workflow-level output root.
        std::vector<irc::CoreLowerDiagnostic> diags;
        auto binding = irc::make_frame_binding_from_verified_table(
            *verified.table,
            irc::CoreWireFrameRootSelector{irc::CoreWireFrameRootKind::Output},
            diags);
        if (!binding.has_value()) {
            return false;
        }
        auto dom = ahfl::json::parse_json(
            std::string_view(reinterpret_cast<const char *>(expected_json.data()),
                             expected_json.size()));
        if (!dom.has_value() || !*dom) {
            return false;
        }
        auto expected =
            ahfl::runtime::wire_codec::decode_json(**dom, *binding);
        if (!expected.ok()) {
            return false;
        }
        auto actual = decode_workflow_output(engine);
        if (!actual.has_value()) {
            return false;
        }
        const std::string expected_canonical =
            ahfl::runtime::value_to_json(*expected.value);
        const std::string actual_canonical =
            ahfl::runtime::value_to_json(*actual);
        return expected_canonical == actual_canonical;
    };

    // ==== Scenario 1: Injected memo replay (ReturnMemo, call count ZERO) ====
    {
        const ps::ResumeCheckpointId ckpt{101};
        auto suspended = rts::make_suspended_record(
            mod, mod.entry_id(), {all_specs[0]}, entry_slot, param_json,
            rts::PayloadSlotId{101});
        auto record = rts::make_injected_record(suspended, injected_slot);
        const std::vector<ps::Slot> slots = {
            ps::Slot{entry_slot, std::span<const std::uint8_t>(entry_bytes)},
            ps::Slot{injected_slot, std::span<const std::uint8_t>(injected_bytes)}};
        auto gen = store->publish_available(mod.entry_id(), ckpt, 0, record, slots,
                                            id_span, std::span<const std::uint8_t>(key));
        check(gen.has_value() && *gen == 1, "run_resume e2e memo: gen1 published");

        wh::Wasm3ResumeEngine engine;
        host::ResumeRequest request;
        request.module = &mod;
        request.module_bytes = std::span<const std::uint8_t>(fixture->module_bytes);
        request.engine = &engine;
        request.store = &*store;
        request.workflow = mod.entry_id();
        request.checkpoint = ckpt;
        request.key_id = id_span;
        request.key = std::span<const std::uint8_t>(key);
        request.pack_entry_frame = entry_packer;
        request.pack_bridge_result = result_packer;
        request.bridge_arg_hasher = arg_hasher;

        auto done = host::run_resume(request);
        if (!done.has_value()) {
            std::cerr << "  memo error: " << host::host_code(done.error())
                      << " variant=" << done.error().index() << "\n";
        }
        check(done.has_value(), "run_resume e2e memo: completes");
        if (done.has_value()) {
            check(done->raw_status == 0, "run_resume e2e memo: raw_status OK");
            check(output_matches(engine, injected_bytes),
                  "run_resume e2e memo: output is memo bytes (no live capability)");
            // The driver's import callback served the memo from the store. It
            // has NO live capability invoker: ReadyForLive is blocked (aborts).
            // A successful resume proves the invoker call count is ZERO.
        }
    }

    // ==== Scenario 2: Injected-slot publish/ack (NeedInjectedSlot) ====
    {
        const ps::ResumeCheckpointId ckpt{102};
        auto record = rts::make_suspended_record(
            mod, mod.entry_id(), {all_specs[0]}, entry_slot, param_json,
            rts::PayloadSlotId{101});
        const std::vector<ps::Slot> slots = {
            ps::Slot{entry_slot, std::span<const std::uint8_t>(entry_bytes)}};
        auto gen = store->publish_available(mod.entry_id(), ckpt, 0, record, slots,
                                            id_span, std::span<const std::uint8_t>(key));
        check(gen.has_value() && *gen == 1, "run_resume e2e inject: gen1 published");

        wh::Wasm3ResumeEngine engine;
        host::ResumeRequest request;
        request.module = &mod;
        request.module_bytes = std::span<const std::uint8_t>(fixture->module_bytes);
        request.engine = &engine;
        request.store = &*store;
        request.workflow = mod.entry_id();
        request.checkpoint = ckpt;
        request.key_id = id_span;
        request.key = std::span<const std::uint8_t>(key);
        request.injected_result = std::span<const std::uint8_t>(injected_bytes);
        request.chosen_injected_slot = injected_slot;
        request.pack_entry_frame = entry_packer;
        request.pack_bridge_result = result_packer;
        request.bridge_arg_hasher = arg_hasher;

        auto done = host::run_resume(request);
        if (!done.has_value()) {
            std::cerr << "  inject error: " << host::host_code(done.error()) << "\n";
        }
        check(done.has_value(), "run_resume e2e inject: completes");
        if (done.has_value()) {
            check(done->raw_status == 0, "run_resume e2e inject: raw_status OK");
            check(output_matches(engine, injected_bytes),
                  "run_resume e2e inject: output is injected bytes");
        }
    }

    // ==== Scenario 3: Module trap -> ModuleTrap ====
    {
        const ps::ResumeCheckpointId ckpt{103};
        auto trap_bytes = build_a2_trap_module();
        auto trap_admitted = csm::make_verified_core_wasm_schema_module(
            std::span<const std::uint8_t>(trap_bytes));
        check(trap_admitted.ok(), "run_resume e2e trap: A2 admission");
        if (!trap_admitted.ok() || !trap_admitted.module.has_value()) {
            return;
        }
        const auto &trap_mod = *trap_admitted.module;

        auto trap_specs = rts::module_node_specs(trap_mod);
        auto record = rts::make_suspended_record(
            trap_mod, trap_mod.entry_id(), trap_specs, entry_slot, rts::kIntParamJson,
            rts::PayloadSlotId{101});
        const auto memo_bytes = rts::bytes_of(rts::kIntParamJson);
        const std::vector<ps::Slot> slots =
            rts::suspended_slots(record, std::span<const std::uint8_t>(memo_bytes));
        auto gen = store->publish_available(trap_mod.entry_id(), ckpt, 0, record, slots,
                                            id_span, std::span<const std::uint8_t>(key));
        check(gen.has_value() && *gen == 1, "run_resume e2e trap: gen1 published");

        wh::Wasm3ResumeEngine engine;
        host::ResumeRequest request;
        request.module = &trap_mod;
        request.module_bytes = std::span<const std::uint8_t>(trap_bytes);
        request.engine = &engine;
        request.store = &*store;
        request.workflow = trap_mod.entry_id();
        request.checkpoint = ckpt;
        request.key_id = id_span;
        request.key = std::span<const std::uint8_t>(key);
        request.injected_result = std::span<const std::uint8_t>(memo_bytes);
        request.chosen_injected_slot = injected_slot;
        // The hand-built trap module has no frame layout section, so the P4-D
        // packer cannot work. Use the bump-packers: run2 traps before any
        // import is served, so the entry frame placement is irrelevant.
        request.pack_entry_frame = rts::make_bump_entry_packer();
        request.pack_bridge_result = rts::make_bump_result_packer();

        auto done = host::run_resume(request);
        if (done.has_value()) {
            std::cerr << "  trap: unexpected success\n";
        } else {
            std::cerr << "  trap error: " << host::host_code(done.error()) << "\n";
        }
        check(!done.has_value(), "run_resume e2e trap: fails");
        if (!done.has_value()) {
            const auto &f = done.error();
            check(std::holds_alternative<host::StepFailure>(f),
                  "run_resume e2e trap: StepFailure");
            if (std::holds_alternative<host::StepFailure>(f)) {
                check(std::get<host::StepFailure>(f).reason == rc::ResumeStepReason::ModuleTrap,
                      "run_resume e2e trap: ModuleTrap");
            }
        }
    }

    // ==== Scenario 4: Host abort (arg_hash mismatch -> CoordinateMismatch) ====
    {
        const ps::ResumeCheckpointId ckpt{104};
        const std::string mismatched_json =
            R"({"_type":"wasm::e3_capability_workflow_resume::Frame","value":"different-input"})";
        const auto mismatched_bytes = rts::bytes_of(mismatched_json);

        // The ledgered arg_hash is computed from param_json ("workflow-input"),
        // but the entry frame carries "different-input". The controller
        // detects the mismatch and the import callback aborts.
        auto record = rts::make_suspended_record(
            mod, mod.entry_id(), {all_specs[0]}, entry_slot, param_json,
            rts::PayloadSlotId{101});
        const std::vector<ps::Slot> slots = {
            ps::Slot{entry_slot, std::span<const std::uint8_t>(mismatched_bytes)}};
        auto gen = store->publish_available(mod.entry_id(), ckpt, 0, record, slots,
                                            id_span, std::span<const std::uint8_t>(key));
        check(gen.has_value() && *gen == 1, "run_resume e2e abort: gen1 published");

        wh::Wasm3ResumeEngine engine;
        host::ResumeRequest request;
        request.module = &mod;
        request.module_bytes = std::span<const std::uint8_t>(fixture->module_bytes);
        request.engine = &engine;
        request.store = &*store;
        request.workflow = mod.entry_id();
        request.checkpoint = ckpt;
        request.key_id = id_span;
        request.key = std::span<const std::uint8_t>(key);
        request.injected_result = std::span<const std::uint8_t>(injected_bytes);
        request.chosen_injected_slot = injected_slot;
        request.pack_entry_frame = entry_packer;
        request.pack_bridge_result = result_packer;
        request.bridge_arg_hasher = arg_hasher;

        auto done = host::run_resume(request);
        if (done.has_value()) {
            std::cerr << "  abort: unexpected success\n";
        } else {
            std::cerr << "  abort error: " << host::host_code(done.error()) << "\n";
        }
        check(!done.has_value(), "run_resume e2e abort: fails");
        if (!done.has_value()) {
            const auto &f = done.error();
            check(std::holds_alternative<host::StepFailure>(f),
                  "run_resume e2e abort: StepFailure");
            if (std::holds_alternative<host::StepFailure>(f)) {
                check(std::get<host::StepFailure>(f).reason ==
                          rc::ResumeStepReason::CoordinateMismatch,
                      "run_resume e2e abort: CoordinateMismatch");
            }
        }
    }

    rts::nuke(store_path);
}

} // anonymous namespace

int main() {
    test_bridge_e2e();
    test_run_resume_wasm3_e2e();

    std::cout << pass_count << "/" << test_count << " checks passed\n";
    return (pass_count == test_count) ? 0 : 1;
}
