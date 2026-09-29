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
// alongside the opaque tuple, so a real emitted bridge module
// (v2c_single_arg_bridge) now passes make_verified_core_wasm_schema_module +
// admit_core_wasm_frame_sections and is driven end-to-end through the
// capability_import executor on the real wasm3 engine.

#include "runtime/wasm_host/capability_import.hpp"
#include "runtime/wasm_host/p6_frame_driver.hpp"
#include "runtime/wasm_host/wasm3_engine.hpp"

#include "runtime/engine/core_wasm_frame_module.hpp"
#include "runtime/value/value_json.hpp"

#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/core_layout.hpp"
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

// Memory layout for the e2e fixtures (well within the 64 KiB page, past the
// rodata region [256,1024) and the heap start at 1032).
constexpr std::uint32_t kEnvelopeBase = 1032; // opaque arg envelope

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

// Write the opaque arg envelope into the engine's memory at kEnvelopeBase.
void write_opaque_envelope(wh::Wasm3ResumeEngine &engine,
                           const std::string &envelope_json) {
    auto page = engine.mutable_whole_memory();
    if (!page.has_value()) {
        return;
    }
    std::memcpy(page->data() + kEnvelopeBase, envelope_json.data(),
                envelope_json.size());
}

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

namespace fr = ahfl::runtime::core_wasm_frame_module;
namespace irc = ahfl::ir::core;
namespace conf = ahfl::conformance;

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

std::vector<std::uint8_t> custom_payload(const std::string &name,
                                         const std::vector<std::uint8_t> &body) {
    std::vector<std::uint8_t> p;
    put_uleb(p, name.size());
    p.insert(p.end(), name.begin(), name.end());
    p.insert(p.end(), body.begin(), body.end());
    return p;
}

// A synthetic canonical AHFLXM body for a Workflow with ONE cap node. The
// agent-emitted bridge module carries no exec-manifest (the AHFLXM is a
// workflow-section artifact), so the e2e injects a test-synthesized manifest
// matching the module's REAL import + wire-schema capability to prove A2's
// framing / table-decode / cross-check / eager-mint accept the genuine
// Type/Import/AHFLWS bytes (the same evidence class as the schema_module
// test's real-emitter + synthetic-manifest injection).
std::vector<std::uint8_t> bridge_manifest_body(std::uint32_t capability,
                                               std::uint64_t source_symbol) {
    std::vector<std::uint8_t> b;
    const char magic[6] = {'A', 'H', 'F', 'L', 'X', 'M'};
    for (char c : magic) {
        b.push_back(static_cast<std::uint8_t>(c));
    }
    b.push_back(1); // version
    b.push_back(0); // entry.kind = Workflow
    put_uleb(b, 0); // entry_id
    put_uleb(b, 1); // one node
    put_uleb(b, 0); // workflow_node_id
    put_uleb(b, 0); // schedule_pos
    b.push_back(1); // cap_call_count = 1
    put_uleb(b, capability);
    put_uleb(b, source_symbol);
    return b;
}

// Inject a synthetic AHFLXM section immediately before the EOF AHFLWS section.
std::optional<std::vector<std::uint8_t>>
inject_manifest_before_schema(const std::vector<std::uint8_t> &module,
                              const std::vector<std::uint8_t> &manifest_body) {
    std::size_t off = 8;
    std::size_t last_section_start = std::string::npos;
    while (off < module.size()) {
        last_section_start = off;
        ++off; // id
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

// Read the emitter's sole capability identity (cap id, source_symbol) out of
// the genuine AHFLWS section at module EOF, via the C1 decoder.
std::optional<std::pair<std::uint32_t, std::uint64_t>>
real_sole_capability(const std::vector<std::uint8_t> &module) {
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
        return std::nullopt;
    }
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
    auto decoded = irc::decode_core_wire_schema_table(table_bytes);
    if (!decoded.ok() || !decoded.table.has_value() ||
        decoded.table->capabilities.size() != 1) {
        return std::nullopt;
    }
    const auto &cap = decoded.table->capabilities.front();
    return std::make_pair(cap.capability.value, cap.source_symbol);
}

struct EmittedBridgeFixture {
    std::vector<std::uint8_t> module_bytes;
    fr::AdmittedFrameSections admitted;
    wh::P6FinalKind final_kind{wh::P6FinalKind::Identity};
};

// Emit the real v2c_single_arg_bridge agent in-process and admit its frame
// sections. Returns nullopt on any pipeline failure.
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
    auto admitted = fr::admit_core_wasm_frame_sections(emitted.artifact->bytes);
    if (!admitted.ok()) {
        std::cerr << "  bridge frame admit failed\n";
        return std::nullopt;
    }
    const wh::P6FinalKind final_kind =
        (emitted.descriptor->frame.has_value() &&
         emitted.descriptor->frame->final_kind == "computed")
            ? wh::P6FinalKind::Computed
            : wh::P6FinalKind::Identity;
    return EmittedBridgeFixture{
        .module_bytes = emitted.artifact->bytes,
        .admitted = std::move(*admitted.sections),
        .final_kind = final_kind,
    };
}

// WH-3 bridge e2e: a real emitted bridge module (v2c_single_arg_bridge) is
// A2-admitted (with a synthetic manifest matching its real import + schema),
// frame-admitted, and driven end-to-end through the capability_import executor
// on the real wasm3 engine. The OK case proves the bridge reply binds the
// result_base (the executor packs the capability result at the call site's
// disjoint placement and the module materializes it); the non-OK case proves a
// bridge Error/Pending reaches the guest's unreachable -> Run2Trapped (the
// bridge has no graceful ERROR/PENDING arm).
void test_bridge_e2e() {
    auto fixture = emit_bridge_fixture();
    check(fixture.has_value(), "bridge e2e: emit + frame admit");
    if (!fixture.has_value()) {
        return;
    }

    // A2-admit with a synthetic manifest matching the real import + schema.
    auto identity = real_sole_capability(fixture->module_bytes);
    check(identity.has_value(), "bridge e2e: read sole capability from AHFLWS");
    if (!identity.has_value()) {
        return;
    }
    const auto manifest = bridge_manifest_body(identity->first, identity->second);
    auto injected = inject_manifest_before_schema(fixture->module_bytes, manifest);
    check(injected.has_value(), "bridge e2e: manifest injected");
    if (!injected.has_value()) {
        return;
    }
    auto admitted = csm::make_verified_core_wasm_schema_module(
        std::span<const std::uint8_t>(*injected));
    check(admitted.ok(), "bridge e2e: A2 admission accepts bridge functype");
    if (!admitted.ok() || !admitted.module.has_value()) {
        return;
    }
    check(admitted.module->call_site_count() == 1, "bridge e2e: one call site");

    const auto source_symbol = identity->second;
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
            .frame_section = fixture->admitted.layout,
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
            engine, fixture->module_bytes, fixture->admitted.layout,
            fixture->admitted.input_binding, fixture->admitted.output_binding, *input,
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
            .frame_section = fixture->admitted.layout,
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
            engine, fixture->module_bytes, fixture->admitted.layout,
            fixture->admitted.input_binding, fixture->admitted.output_binding, *input,
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

// ==== opaque lane e2e tests ====

// 1. Opaque OK: the module calls the opaque import; the executor invokes the
//    mock (Success + IntValue(42)), serializes "42", alloc_then_writes, and
//    replies (0, ptr, 2). The classifier returns (0, ptr, 2).
void test_opaque_e2e_ok() {
    constexpr std::uint64_t kSymbol = 100;
    auto bytes = wht::with_a2_admission(wht::opaque_classifier_module(kSymbol), kSymbol);

    ScriptedInvoker mock;
    mock.result.status = runtime::CapabilityCallStatus::Success;
    mock.result.value = runtime::Value{runtime::IntValue{42}};

    E2eSession session(bytes, mock,
                       [](std::uint64_t) { return std::optional<std::string>{"test_cap"}; });
    check(session.admitted.ok(), "opaque e2e ok: A2 admission");

    write_opaque_envelope(session.engine, "{\"value\":42}");
    auto outcome = session.engine.invoke_run2(eng::GuestPointer{kEnvelopeBase}, 12);
    check(outcome.has_value(), "opaque e2e ok: invoke_run2 succeeded");
    if (outcome.has_value()) {
        check(std::holds_alternative<eng::Run2ResultTuple>(*outcome),
              "opaque e2e ok: outcome is Run2ResultTuple");
        if (const auto *r = std::get_if<eng::Run2ResultTuple>(&*outcome)) {
            check(r->raw_status == 0, "opaque e2e ok: raw_status == 0");
            check(r->output_ptr.value != 0, "opaque e2e ok: output_ptr != 0");
            check(r->output_len == 2, "opaque e2e ok: output_len == 2");
            auto mem = session.engine.read_whole_memory();
            std::string result_json(
                mem->begin() + r->output_ptr.value,
                mem->begin() + r->output_ptr.value + r->output_len);
            check(result_json == "42", "opaque e2e ok: result == '42'");
        }
    }
    check(mock.call_count == 1, "opaque e2e ok: invoker called once");
}

// 2. Opaque ERROR: the mock returns Error; the executor replies (1, 0, 0); the
//    classifier returns (1, 0, 0).
void test_opaque_e2e_error() {
    constexpr std::uint64_t kSymbol = 101;
    auto bytes = wht::with_a2_admission(wht::opaque_classifier_module(kSymbol), kSymbol);

    ScriptedInvoker mock;
    mock.result.status = runtime::CapabilityCallStatus::Error;

    E2eSession session(bytes, mock,
                       [](std::uint64_t) { return std::optional<std::string>{"test_cap"}; });
    check(session.admitted.ok(), "opaque e2e error: A2 admission");

    write_opaque_envelope(session.engine, "{\"value\":42}");
    auto outcome = session.engine.invoke_run2(eng::GuestPointer{kEnvelopeBase}, 12);
    check(outcome.has_value(), "opaque e2e error: invoke_run2 succeeded");
    if (outcome.has_value()) {
        check(std::holds_alternative<eng::Run2ResultTuple>(*outcome),
              "opaque e2e error: outcome is Run2ResultTuple");
        if (const auto *r = std::get_if<eng::Run2ResultTuple>(&*outcome)) {
            check(r->raw_status == 1, "opaque e2e error: raw_status == 1");
            check(r->output_ptr.value == 0, "opaque e2e error: output_ptr == 0");
            check(r->output_len == 0, "opaque e2e error: output_len == 0");
        }
    }
    check(mock.call_count == 1, "opaque e2e error: invoker called once");
}

// 3. Opaque PENDING: the mock returns Pending; the executor replies (2, 0, 0);
//    the classifier sets the latch and returns (2, 0, 0).
void test_opaque_e2e_pending() {
    constexpr std::uint64_t kSymbol = 102;
    auto bytes = wht::with_a2_admission(wht::opaque_classifier_module(kSymbol), kSymbol);

    ScriptedInvoker mock;
    mock.result.status = runtime::CapabilityCallStatus::Pending;

    E2eSession session(bytes, mock,
                       [](std::uint64_t) { return std::optional<std::string>{"test_cap"}; });
    check(session.admitted.ok(), "opaque e2e pending: A2 admission");

    write_opaque_envelope(session.engine, "{\"value\":42}");
    auto outcome = session.engine.invoke_run2(eng::GuestPointer{kEnvelopeBase}, 12);
    check(outcome.has_value(), "opaque e2e pending: invoke_run2 succeeded");
    if (outcome.has_value()) {
        check(std::holds_alternative<eng::Run2ResultTuple>(*outcome),
              "opaque e2e pending: outcome is Run2ResultTuple");
        if (const auto *r = std::get_if<eng::Run2ResultTuple>(&*outcome)) {
            check(r->raw_status == 2, "opaque e2e pending: raw_status == 2");
            check(r->output_ptr.value == 0, "opaque e2e pending: output_ptr == 0");
            check(r->output_len == 0, "opaque e2e pending: output_len == 0");
        }
    }
    check(mock.call_count == 1, "opaque e2e pending: invoker called once");
}

// 4. Suspended -> Injected -> Consumed replay: the first run suspends
//    (PENDING); the host injects the result (scripted Success on a fresh
//    instance); the second run consumes it (OK).
void test_replay_suspended_injected_consumed() {
    constexpr std::uint64_t kSymbol = 103;
    auto bytes = wht::with_a2_admission(wht::opaque_classifier_module(kSymbol), kSymbol);

    ScriptedInvoker mock;
    mock.result.status = runtime::CapabilityCallStatus::Pending;

    // --- Run 1: Suspended ---
    {
        E2eSession session(bytes, mock,
                           [](std::uint64_t) { return std::optional<std::string>{"test_cap"}; });
        check(session.admitted.ok(), "replay run1: A2 admission");

        write_opaque_envelope(session.engine, "{\"value\":42}");
        auto outcome = session.engine.invoke_run2(eng::GuestPointer{kEnvelopeBase}, 12);
        check(outcome.has_value(), "replay run1: invoke_run2 succeeded");
        if (outcome.has_value()) {
            if (const auto *r = std::get_if<eng::Run2ResultTuple>(&*outcome)) {
                check(r->raw_status == 2, "replay run1: raw_status == 2 (Suspended)");
            } else {
                check(false, "replay run1: expected Run2ResultTuple");
            }
        }
        check(mock.call_count == 1, "replay run1: invoker called once");
    }

    // --- Inject: flip the mock to Success ---
    mock.result.status = runtime::CapabilityCallStatus::Success;
    mock.result.value = runtime::Value{runtime::IntValue{42}};

    // --- Run 2: Injected + Consumed (fresh instance, latch is 0) ---
    {
        E2eSession session(bytes, mock,
                           [](std::uint64_t) { return std::optional<std::string>{"test_cap"}; });
        check(session.admitted.ok(), "replay run2: A2 admission");

        write_opaque_envelope(session.engine, "{\"value\":42}");
        auto outcome = session.engine.invoke_run2(eng::GuestPointer{kEnvelopeBase}, 12);
        check(outcome.has_value(), "replay run2: invoke_run2 succeeded");
        if (outcome.has_value()) {
            if (const auto *r = std::get_if<eng::Run2ResultTuple>(&*outcome)) {
                check(r->raw_status == 0, "replay run2: raw_status == 0 (Consumed)");
                check(r->output_ptr.value != 0, "replay run2: output_ptr != 0");
                check(r->output_len == 2, "replay run2: output_len == 2");
                auto mem = session.engine.read_whole_memory();
                std::string result_json(
                    mem->begin() + r->output_ptr.value,
                    mem->begin() + r->output_ptr.value + r->output_len);
                check(result_json == "42", "replay run2: result == '42'");
            } else {
                check(false, "replay run2: expected Run2ResultTuple");
            }
        }
        check(mock.call_count == 2, "replay run2: invoker called twice total");
    }
}

// ==== ImportAbort e2e test ====

// 5. ImportAbort on engine fault: the name_resolver returns nullopt; the
//    executor returns ImportAbort(CapabilityNameUnknown); invoke_run2 unwinds
//    as Run2HostAborted (distinct from Run2Trapped).
void test_import_abort_e2e() {
    constexpr std::uint64_t kSymbol = 300;
    auto bytes = wht::with_a2_admission(wht::opaque_classifier_module(kSymbol), kSymbol);

    ScriptedInvoker mock;
    mock.result.status = runtime::CapabilityCallStatus::Success;
    mock.result.value = runtime::Value{runtime::IntValue{42}};

    E2eSession session(bytes, mock,
                       [](std::uint64_t) { return std::nullopt; }); // unknown name
    check(session.admitted.ok(), "import abort e2e: A2 admission");

    write_opaque_envelope(session.engine, "{\"value\":42}");
    auto outcome = session.engine.invoke_run2(eng::GuestPointer{kEnvelopeBase}, 12);
    check(outcome.has_value(), "import abort e2e: invoke_run2 succeeded");
    if (outcome.has_value()) {
        check(std::holds_alternative<eng::Run2HostAborted>(*outcome),
              "import abort e2e: outcome is Run2HostAborted");
        check(!std::holds_alternative<eng::Run2Trapped>(*outcome),
              "import abort e2e: NOT Run2Trapped (distinct failure mode)");
    }
    check(mock.call_count == 0, "import abort e2e: invoker not called");
    check(session.state.last_error.has_value() &&
              *session.state.last_error == wh::CapabilityImportError::CapabilityNameUnknown,
          "import abort e2e: error is CapabilityNameUnknown");
}

} // anonymous namespace

int main() {
    test_bridge_e2e();
    test_opaque_e2e_ok();
    test_opaque_e2e_error();
    test_opaque_e2e_pending();
    test_replay_suspended_injected_consumed();
    test_import_abort_e2e();

    std::cout << pass_count << "/" << test_count << " checks passed\n";
    return (pass_count == test_count) ? 0 : 1;
}
