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
// (ahfl_wasm_host_capability_import_tests): A2 admission's
// spans_are_capability_tuple gate currently seals the opaque
// (i32,i32)->(i32,i32,i32) functype only, so a bridge-functype module cannot
// pass make_verified_core_wasm_schema_module. The engine's fresh_instance
// admits both functypes; the A2 gate's bridge acceptance is a separate slice.

#include "runtime/wasm_host/capability_import.hpp"
#include "runtime/wasm_host/wasm3_engine.hpp"

#include "unit/runtime/wasm_host/wasm_host_test_support.hpp"

#include <cstdint>
#include <cstring>
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
    test_opaque_e2e_ok();
    test_opaque_e2e_error();
    test_opaque_e2e_pending();
    test_replay_suspended_injected_consumed();
    test_import_abort_e2e();

    std::cout << pass_count << "/" << test_count << " checks passed\n";
    return (pass_count == test_count) ? 0 : 1;
}
