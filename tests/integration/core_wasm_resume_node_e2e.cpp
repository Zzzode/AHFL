// RFC 0026 KR6.5 E4-B2-D2a (F5) end-to-end durable-resume replay against a REAL
// WebAssembly engine: the F4 engine-agnostic production resume-host driver is
// driven over the Node embedded-engine port (core_wasm_node_resume_engine.hpp),
// replaying a B2-C-emitted capability workflow through a REAL IntegrityPayloadStore.
//
// Scenario (the single-import e3_capability_workflow module, bounded String so
// the F1 fixed-page preflight is finite):
//   1. the real emit-only probe emits the artifact from the checked golden;
//   2. the artifact is A2-admitted (digest + manifest + wire schema + the F3
//      fixed single-page Memory declaration);
//   3. generation 1 is hand-seeded Suspended: the opaque Frame entry slot plus a
//      pending frontier on the workflow's sole capability call site, the pending
//      arg_hash computed the same way the controller computes it
//      (resume_test_support.param_arg_hash over the A2 Param binding);
//   4. run_resume opens the REAL store, gates, instantiates a FRESH Node
//      instance, L0-transfers the verbatim entry, and inside run2 serves the one
//      synchronous ahfl_cap import by binding + CAS-publishing + ACKing the
//      injected frame -- there is NO live capability result: the import's reply
//      bytes come solely from the authenticated store/controller path;
//   5. the real scheduler publishes both node events, run2 returns OK and the
//      identity second node forwards the injected Frame byte-for-byte;
//   6. the generation is a ResolvedConsumed tombstone on reopen, and the terminal
//      node-event region equals the contract-derived golden.
//
// Output text explicitly says: Node embedded-engine durable-resume evidence, NOT
// wasmtime evidence. SKIPs (77) when node is absent OR off a durable Linux FS.
//
// Hand-rolled check()/main(); NO gtest. Links PRIVATE only ahfl_runtime_engine;
// the probe is launched as a subprocess (build dependency at the ctest level).

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <signal.h>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <unistd.h>

#include "base/support/process.hpp"
#include "core_wasm_node_resume_engine.hpp"
#include "runtime/engine/core_wasm_resume_host.hpp"
#include "unit/runtime/engine/resume_test_support.hpp"

namespace {

using namespace ahfl::runtime::resume_test_support;

namespace fs = std::filesystem;

namespace neng = ahfl::runtime::core_wasm_node_resume_engine;
namespace engine = ahfl::runtime::core_wasm_resume_engine;
namespace host = ahfl::runtime::core_wasm_resume_host;
namespace ps = ahfl::runtime::payload_store;
namespace support = ahfl::support;

using ahfl::runtime::core_wasm_resume::PayloadSlotId;
using ahfl::runtime::payload_store::ResumeCheckpointId;

int g_failures = 0;
int g_total = 0;

void check(bool ok, std::string_view name) {
    ++g_total;
    if (!ok) {
        ++g_failures;
        std::cerr << "FAIL: " << name << "\n";
    }
}

[[nodiscard]] std::optional<std::vector<std::uint8_t>> read_file(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                     std::istreambuf_iterator<char>());
}

[[nodiscard]] bool write_file(const fs::path &path, std::string_view contents) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return static_cast<bool>(out);
}

// The contract-derived 88-byte node-event region golden for the two-node
// capability workflow (count=2, cap node 0 cap_1 src 1, identity node 1). It is
// the SAME known-answer value the Node B2-C host test and the C++ decoder KAT
// use: derived from the wire grammar + this fixture's semantics, never captured
// from this execution.
constexpr std::string_view kNodeEventGoldenHex =
    "02000000000000000100000000000000000000000000000001000000000000000000000000000000"
    "00000000000000000000000001000000010000000000000000000000000000000000000000000000"
    "0000000000000000";

// WH-3 P1-2 regression: the Node script's parseImportArities must stay in sync
// across a multi-type Type section (the desync fix). Launches the embedded
// script with --self-test-arity on a synthetic module whose Type section
// carries the opaque tuple, the bridge signature, and an unrelated
// (i32)->i32 type, with imports referencing the first two, and asserts the
// arity table is exactly [3,2] through V8 (Node), not a faked parse.
[[nodiscard]] bool test_arity_self_test(const fs::path &node_executable, const fs::path &base) {
    const fs::path script = base / "arity_selftest.mjs";
    if (!write_file(script, neng::node_resume_host_script())) {
        std::cerr << "FAIL: arity.self_test.script_written\n";
        return false;
    }

    // Build the synthetic module: header + Type[3] + Import[2].
    std::vector<std::uint8_t> m = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
    auto put_uleb = [](std::vector<std::uint8_t> &out, std::uint64_t v) {
        do {
            auto b = static_cast<std::uint8_t>(v & 0x7fU);
            v >>= 7U;
            if (v != 0) {
                b |= 0x80U;
            }
            out.push_back(b);
        } while (v != 0);
    };
    auto put_section = [&](std::uint8_t id, const std::vector<std::uint8_t> &payload) {
        m.push_back(id);
        put_uleb(m, payload.size());
        m.insert(m.end(), payload.begin(), payload.end());
    };
    auto put_str = [&put_uleb](std::vector<std::uint8_t> &out, const std::string &s) {
        put_uleb(out, s.size());
        out.insert(out.end(), s.begin(), s.end());
    };

    // Type section: tuple (i32,i32)->(i32,i32,i32), bridge (i32)->(i32,i32),
    // and an unrelated (i32)->i32.
    std::vector<std::uint8_t> types;
    put_uleb(types, 3);
    types.push_back(0x60);
    put_uleb(types, 2);
    types.push_back(0x7f);
    types.push_back(0x7f);
    put_uleb(types, 3);
    types.push_back(0x7f);
    types.push_back(0x7f);
    types.push_back(0x7f);
    types.push_back(0x60);
    put_uleb(types, 1);
    types.push_back(0x7f);
    put_uleb(types, 2);
    types.push_back(0x7f);
    types.push_back(0x7f);
    types.push_back(0x60);
    put_uleb(types, 1);
    types.push_back(0x7f);
    put_uleb(types, 1);
    types.push_back(0x7f);
    put_section(1, types);

    // Import section: two ahfl_cap func imports referencing type 0 and type 1.
    std::vector<std::uint8_t> imports;
    put_uleb(imports, 2);
    put_str(imports, "ahfl_cap");
    put_str(imports, "cap_0");
    imports.push_back(0x00);
    put_uleb(imports, 0);
    put_str(imports, "ahfl_cap");
    put_str(imports, "cap_1");
    imports.push_back(0x00);
    put_uleb(imports, 1);
    put_section(2, imports);

    const fs::path module_path = base / "arity_selftest.wasm";
    if (!write_file(module_path,
                    std::string_view(reinterpret_cast<const char *>(m.data()), m.size()))) {
        std::cerr << "FAIL: arity.self_test.module_written\n";
        return false;
    }

    support::ProcessConfig cfg;
    cfg.executable = node_executable.string();
    cfg.arguments = {script.string(), "--self-test-arity", module_path.string()};
    cfg.timeout = std::chrono::seconds(30);
    const auto result = support::launch_process(cfg);
    if (result.exit_code != 0) {
        std::cerr << "FAIL: arity.self_test.exit_zero (exit=" << result.exit_code << ")\n";
        std::cerr << "  stderr: " << result.stderr_output << "\n";
        return false;
    }
    // The stdout must contain "[3,2]" (the arity table for types 0 and 1).
    if (result.stdout_output.find("[3,2]") == std::string::npos) {
        std::cerr << "FAIL: arity.self_test.table_is_3_2\n";
        std::cerr << "  stdout: " << result.stdout_output << "\n";
        return false;
    }
    std::cout << "arity self-test: [3,2] confirmed through V8\n";
    return true;
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 4) {
        std::cerr << "usage: core_wasm_resume_node_e2e <emit-probe> <bounded-source.ahfl>"
                     " <work-dir>\n";
        return 2;
    }
    const fs::path probe = argv[1];
    const fs::path source = argv[2];
    const fs::path base = argv[3];

    // Node gate: a visibly SKIPped lane when the embedded engine is unavailable.
    auto node_executable = support::find_executable("node");
    if (!node_executable.has_value()) {
        std::cerr << "SKIP: node is unavailable for Node embedded-engine durable-resume\n";
        return 77;
    }

    std::error_code ec;
    fs::remove_all(base, ec);
    if (!fs::create_directories(base, ec)) {
        std::cerr << "FAIL: cannot create work dir " << base << "\n";
        return 1;
    }

    // WH-3 P1-2: the parseImportArities desync regression. Runs on any FS,
    // before the durable-FS gate below, so the arity-table evidence is
    // collected even when the integrity store cannot open.
    if (!test_arity_self_test(*node_executable, base)) {
        return 1;
    }

    // Durable-filesystem gate (the integrity store is Linux durable FS only).
    if (!open_store(base / "probe").has_value()) {
        std::cerr << "SKIP: Node resume e2e needs a Linux durable FS\n";
        return 77;
    }
    nuke(base / "probe");

    const fs::path artifact = base / "capability_workflow.wasm";

    // (1) Emit through the real frontend -> Core -> B2-C emitter.
    support::ProcessConfig emit_config;
    emit_config.executable = probe.string();
    emit_config.arguments = {source.string(), artifact.string()};
    emit_config.timeout = std::chrono::seconds(60);
    const auto emitted = support::launch_process(emit_config);
    check(emitted.exit_code == 0, "emit.probe_exit");
    if (emitted.exit_code != 0) {
        std::cerr << "probe stderr: " << emitted.stderr_output << "\n";
        return 1;
    }
    check(emitted.stdout_output.find("import_count=1") != std::string::npos,
          "emit.import_count_one");

    const auto module_bytes = read_file(artifact);
    check(module_bytes.has_value() && !module_bytes->empty(), "emit.artifact_read");
    if (!module_bytes.has_value()) {
        return 1;
    }

    // (2) A2 admission over the real artifact bytes.
    auto admitted =
        make_verified_core_wasm_schema_module(std::span<const std::uint8_t>(*module_bytes));
    check(admitted.ok(), "a2.module_admits");
    if (!admitted.ok()) {
        for (const auto &d : admitted.diagnostics) {
            std::cerr << "A2: " << d.code << ": " << d.message << "\n";
        }
        return 1;
    }
    const auto mod = *admitted.module;
    check(mod.node_count() == 2 && mod.call_site_count() == 1, "a2.two_nodes_one_callsite");
    check(mod.entry_id().value == 0, "a2.entry_id_zero");

    // (3) Seed generation 1: Suspended at the workflow's sole capability call
    // site (schedule 0). The real module runs [capability first] -> [identity
    // second], so the dense ledger prefix is exactly [capability frontier]
    // (the identity node publishes only AFTER the injected import returns).
    const auto all_specs = module_node_specs(mod);
    check(all_specs.size() == 2 && all_specs[0].cap_call_count == 1 &&
              all_specs[1].cap_call_count == 0,
          "seed.schedule_cap_then_identity");
    const std::vector<ManifestNodeSpec> frontier_prefix = {all_specs[0]};

    // The wire codec Verified-decodes structs with an exact `_type`
    // discriminator (module-qualified declaration name); the Wasm module
    // forwards these frames opaquely, the controller/A2 binding enforces shape.
    const std::string param_json =
        R"({"_type":"wasm::e3_capability_workflow_resume::Frame","value":"workflow-input"})";
    const std::string injected_json =
        R"({"_type":"wasm::e3_capability_workflow_resume::Frame","value":"durable-echo"})";
    const auto entry_bytes = bytes_of(param_json);
    const auto injected_bytes = bytes_of(injected_json);

    const ResumeCheckpointId ckpt{3};
    const PayloadSlotId entry_slot{9};
    const PayloadSlotId injected_slot{5};

    auto store = open_store(base / "store");
    check(store.has_value(), "store.open");
    if (!store.has_value()) {
        return 1;
    }

    auto record = make_suspended_record(
        mod, mod.entry_id(), frontier_prefix, entry_slot, param_json, PayloadSlotId{101});
    check(record.nodes.size() == 1, "seed.prefix_is_frontier_only");
    const std::vector<ps::Slot> gen1_slots = {
        ps::Slot{entry_slot, std::span<const std::uint8_t>(entry_bytes)}};
    const auto key = test_key();
    const auto key_id = test_key_id();
    const std::span<const std::uint8_t, 16> id_span(key_id);
    auto gen1 = store->publish_available(
        mod.entry_id(), ckpt, 0, record, gen1_slots, id_span, std::span<const std::uint8_t>(key));
    check(gen1.has_value() && *gen1 == 1, "seed.gen1_published");

    // (4) Materialize host.mjs and launch the real Node embedded engine.
    const fs::path host_script = base / "host.mjs";
    check(write_file(host_script, neng::node_resume_host_script()), "node.host_script_written");

    auto launched =
        neng::launch_node_resume_engine(*node_executable, host_script.string(), artifact.string());
    check(launched.has_value(), "node.engine_launches");
    if (!launched.has_value()) {
        std::cerr << "Node engine launch failed: " << launched.error().diagnostic << "\n";
        return 1;
    }
    auto engine = std::move(*launched);

    // (5) The production driver over the real engine + real store.
    host::ResumeRequest request;
    request.module = &mod;
    request.module_bytes = std::span<const std::uint8_t>(*module_bytes);
    request.engine = &engine;
    request.store = &*store;
    request.workflow = mod.entry_id();
    request.checkpoint = ckpt;
    request.key_id = id_span;
    request.key = std::span<const std::uint8_t>(key);
    request.injected_result = std::span<const std::uint8_t>(injected_bytes);
    request.chosen_injected_slot = injected_slot;

    auto done = host::run_resume(request);
    check(done.has_value(), "resume.completes");
    if (!done.has_value()) {
        std::cerr << "resume failed with host code: " << host::host_code(done.error()) << "\n";
        return 1;
    }

    // No live capability supplied the frame: the forwarded output IS the
    // injected Frame the driver bound/CAS-published/ACKed through the store.
    check(done->raw_status == 0, "resume.raw_status_ok");
    check(done->output == injected_bytes, "resume.forwards_injected_frame");
    check(done->consumed_from == 2 && done->consumed_generation == 3,
          "resume.consumed_gen3_from_gen2");

    // (6) Terminal evidence: the node-event region equals the contract golden.
    auto terminal = engine.read_whole_memory();
    check(terminal.has_value(), "engine.final_memory_read");
    if (terminal.has_value()) {
        constexpr std::size_t kEventBase = 1024;
        constexpr std::size_t kEventRegionBytes = 88;
        check(terminal->size() >= kEventBase + kEventRegionBytes, "memory.page_size");
        std::string actual_hex;
        actual_hex.reserve(kEventRegionBytes * 2);
        static const char *kHex = "0123456789abcdef";
        for (std::size_t i = 0; i < kEventRegionBytes; ++i) {
            const auto b = (*terminal)[kEventBase + i];
            actual_hex.push_back(kHex[(b >> 4) & 0xF]);
            actual_hex.push_back(kHex[b & 0xF]);
        }
        check(actual_hex == kNodeEventGoldenHex, "event_region.golden");
    }

    // Reopen: the live generation is the ResolvedConsumed tombstone {M=3,N=2}.
    auto after = store->load(mod.entry_id(), ckpt, id_span, std::span<const std::uint8_t>(key));
    check(after.has_value() && std::holds_alternative<ps::ResolvedConsumed>(*after),
          "store.tombstone_on_reopen");
    if (after.has_value() && std::holds_alternative<ps::ResolvedConsumed>(*after)) {
        const auto &c = std::get<ps::ResolvedConsumed>(*after);
        check(c.generation == 3 && c.consumed_generation == 2, "store.resolved_consumed_3_2");
    }

    // (7) Regression for the child-death fail-closed contract: a Node child that
    // is SIGKILLed mid-session must surface as a typed engine error and a clean
    // host exit -- never a SIGPIPE that signal-kills the host (exit 141).
    {
        auto dead = neng::launch_node_resume_engine(
            *node_executable, host_script.string(), artifact.string());
        check(dead.has_value(), "childdeath.relaunch");
        if (dead.has_value()) {
            auto dead_engine = std::move(*dead);
            const int child_pid = dead_engine.child_pid_for_test();
            check(child_pid > 0, "childdeath.pid_visible");
            if (child_pid > 0) {
                ::kill(child_pid, SIGKILL);
            }
            // Reap-and-drain window so the kernel closes the child's read end.
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            // A command to the dead child WRITES to the command channel: the
            // MSG_NOSIGNAL socketpair turns that into EPIPE -> typed
            // InstanceUnavailable. Under the pre-fix default-disposition SIGPIPE
            // the next call below would signal-kill this process (exit 141).
            const auto typed_err = dead_engine.fresh_instance(
                std::span<const std::uint8_t>(*module_bytes),
                [](const auto &) -> engine::ImportCallbackResult { return engine::ImportAbort{}; });
            check(!typed_err.has_value() &&
                      typed_err.error() == engine::EngineError::InstanceUnavailable,
                  "childdeath.command_fails_typed");
            // Destruction runs teardown() -> send(kCmdBye) on the dead channel;
            // reaching the success print below is the exit-0 assertion.
        }
    }

    if (g_failures != 0) {
        std::cerr << "core_wasm_resume_node_e2e: " << g_failures << " of " << g_total
                  << " check(s) failed\n";
        return 1;
    }
    std::cout << "Node embedded-engine durable-resume evidence, NOT wasmtime evidence: all "
              << g_total << " checks passed\n";
    return 0;
}
