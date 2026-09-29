// RFC 0026 KR6.8 WH-1: unit tests for the wasm3-backed CoreWasmResumeEngine
// port (src/runtime/wasm_host/wasm3_engine.cpp). Every module is hand-built in
// wasm_host_test_support.hpp (emitter-free, auditable bytes), so the tests
// prove the REAL wasm3 interpreter instantiates and drives them through the
// engine port: the identity lane, the A2-admitted capability-import lane
// (including the nested alloc inside the import callback), the section-9
// single-arg slot discipline, trap mapping, host-abort mapping, fresh-instance
// discipline, capacity exhaustion with no partial mutation, the exact capacity
// boundary, the memory.grow mid-invoke fail-closed invariant, non-zero raw
// status pass-through, move semantics (RAII teardown under ASan), and the
// fail-closed structural gates (unbound import, bad memory, missing exports).
//
// Evidence honesty: this is a real wasm3 VM executing real modules (unlike the
// in-test Fake / the FOUNDATION caveat), but there is still NO production
// runtime caller in WH-1. It is not wasmtime evidence.

#include "runtime/wasm_host/wasm3_engine.hpp"

#include "unit/runtime/wasm_host/wasm_host_test_support.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace eng = ahfl::runtime::core_wasm_resume_engine;
namespace wht = ahfl::runtime::wasm_host_test_support;
namespace csm = ahfl::runtime::core_wasm_schema_module;

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

[[nodiscard]] std::vector<std::uint8_t> entry_bytes() {
    return {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x11, 0x22, 0x33};
}

// 1. The identity module (no imports) through the production admission path:
//    fresh_instance -> alloc_then_write (L0) -> read_whole_memory (the whole
//    64 KiB page) -> invoke_run2 -> raw tuple assertion.
void test_identity_end_to_end() {
    const auto bytes = wht::identity_module();
    ahfl::runtime::wasm_host::Wasm3ResumeEngine engine;

    auto instantiated = engine.fresh_instance(
        std::span<const std::uint8_t>(bytes),
        [](const eng::ImportObservation &) -> eng::ImportCallbackResult {
            return eng::ImportAbort{}; // never called: no imports
        });
    check(instantiated.has_value(), "identity.fresh_instance");

    auto memory = engine.read_whole_memory();
    check(memory.has_value(), "identity.read_whole_memory before alloc");
    check(memory->size() == 65536, "identity.whole page is 65536 bytes");

    const auto entry = entry_bytes();
    auto ptr = engine.alloc_then_write(std::span<const std::uint8_t>(entry));
    check(ptr.has_value(), "identity.alloc_then_write");
    check(ptr->value == wht::kHeapBase, "identity.alloc returns heap base 1032");

    // The exact bytes must be at the guest pointer.
    auto after_alloc = engine.read_whole_memory();
    check(after_alloc.has_value() && after_alloc->size() == 65536,
          "identity.read_whole_memory after alloc still 65536");
    check(std::memcmp(after_alloc->data() + ptr->value, entry.data(), entry.size()) == 0,
          "identity.entry bytes written at guest pointer");

    auto outcome = engine.invoke_run2(*ptr, static_cast<std::uint32_t>(entry.size()));
    check(outcome.has_value(), "identity.invoke_run2");
    if (outcome) {
        const auto *tuple = std::get_if<eng::Run2ResultTuple>(&*outcome);
        check(tuple != nullptr, "identity.outcome is a tuple");
        if (tuple != nullptr) {
            // The engine passes the raw u32 status through verbatim (0 =
            // AHFL_CAP_OK from the identity run2); it must NOT classify.
            check(tuple->raw_status == 0, "identity.raw_status == 0 (verbatim)");
            check(tuple->output_ptr == *ptr, "identity.output_ptr == entry ptr");
            check(tuple->output_len == entry.size(), "identity.output_len == entry len");
        }
    }
}

// 2. The import-free workflow end-to-end, reusing the shared fixture (no
//    copy-paste): a second engine on the same identity module, proving the
//    fixture is reusable and two sessions are independent.
void test_import_free_workflow_shared_fixture() {
    const auto bytes = wht::identity_module();

    ahfl::runtime::wasm_host::Wasm3ResumeEngine engine_a;
    ahfl::runtime::wasm_host::Wasm3ResumeEngine engine_b;

    auto inst_a = engine_a.fresh_instance(std::span<const std::uint8_t>(bytes),
                                          [](const eng::ImportObservation &) {
                                              return eng::ImportAbort{};
                                          });
    auto inst_b = engine_b.fresh_instance(std::span<const std::uint8_t>(bytes),
                                          [](const eng::ImportObservation &) {
                                              return eng::ImportAbort{};
                                          });
    check(inst_a.has_value() && inst_b.has_value(),
          "shared-fixture.two engines fresh_instance on same bytes");

    const auto entry = entry_bytes();
    auto ptr_a = engine_a.alloc_then_write(std::span<const std::uint8_t>(entry));
    auto ptr_b = engine_b.alloc_then_write(std::span<const std::uint8_t>(entry));
    check(ptr_a.has_value() && ptr_b.has_value(), "shared-fixture.both allocs");
    // Each session has its own bump allocator: both return the heap base.
    check(ptr_a->value == wht::kHeapBase && ptr_b->value == wht::kHeapBase,
          "shared-fixture.independent sessions each return heap base");

    auto out_a = engine_a.invoke_run2(*ptr_a, static_cast<std::uint32_t>(entry.size()));
    auto out_b = engine_b.invoke_run2(*ptr_b, static_cast<std::uint32_t>(entry.size()));
    check(out_a.has_value() && out_b.has_value(), "shared-fixture.both invoke_run2");
    if (out_a && out_b) {
        const auto *ta = std::get_if<eng::Run2ResultTuple>(&*out_a);
        const auto *tb = std::get_if<eng::Run2ResultTuple>(&*out_b);
        check(ta != nullptr && tb != nullptr && ta->raw_status == tb->raw_status &&
                  ta->output_ptr == tb->output_ptr && ta->output_len == tb->output_len,
              "shared-fixture.deterministic identical tuples");
    }
}

// 3. The opaque 2-param capability import, A2-admitted, end-to-end. run2
//    forwards its entry (ptr,len) to the import; the callback's param_frame
//    must be the exact entry bytes (proving the trampoline reads args at
//    sp[3..], the raw-ABI slot discipline), and the callback allocs a result
//    frame NESTED inside invoke_run2 (re-entrant m3_Call) and returns it.
void test_opaque_import_a2_admitted() {
    const auto symbol = 42;
    auto bytes = wht::with_a2_admission(wht::capability_module(symbol, 2), symbol);

    // The SAME bytes must pass A2 admission (the production driver's order is
    // A2 -> F3 -> engine).
    auto admitted = csm::make_verified_core_wasm_schema_module(
        std::span<const std::uint8_t>(bytes));
    check(admitted.ok(), "opaque.A2 admission succeeds");

    ahfl::runtime::wasm_host::Wasm3ResumeEngine engine;
    const auto entry = entry_bytes();
    bool callback_ran = false;
    std::uint32_t observed_ordinal = 999;
    std::vector<std::uint8_t> observed_param;

    auto instantiated = engine.fresh_instance(
        std::span<const std::uint8_t>(bytes),
        [&](const eng::ImportObservation &obs) -> eng::ImportCallbackResult {
            callback_ran = true;
            observed_ordinal = obs.import_ordinal;
            observed_param.assign(obs.param_frame.begin(), obs.param_frame.end());
            // Nested alloc inside the import callback (re-entrant m3_Call).
            const std::vector<std::uint8_t> result = {0xCA, 0xFE};
            auto rp = engine.alloc_then_write(std::span<const std::uint8_t>(result));
            if (!rp) {
                return eng::ImportAbort{};
            }
            return eng::ImportReply{.result_ptr = *rp, .result_len = 2};
        });
    check(instantiated.has_value(), "opaque.fresh_instance");

    auto ptr = engine.alloc_then_write(std::span<const std::uint8_t>(entry));
    check(ptr.has_value(), "opaque.alloc_then_write entry");

    auto outcome = engine.invoke_run2(*ptr, static_cast<std::uint32_t>(entry.size()));
    check(outcome.has_value(), "opaque.invoke_run2");
    check(callback_ran, "opaque.import callback ran");
    check(observed_ordinal == 0, "opaque.import_ordinal == 0");
    check(observed_param == entry, "opaque.param_frame == entry bytes (sp[3..] slot discipline)");

    if (outcome) {
        const auto *tuple = std::get_if<eng::Run2ResultTuple>(&*outcome);
        check(tuple != nullptr, "opaque.outcome is a tuple");
        if (tuple != nullptr) {
            check(tuple->raw_status == 0, "opaque.raw_status == 0 (verbatim)");
            check(tuple->output_len == 2, "opaque.output_len == result len");
            // The result frame the callback allocated must hold the reply bytes.
            auto mem = engine.read_whole_memory();
            check(mem.has_value(), "opaque.read_whole_memory after invoke");
            if (mem) {
                check(std::memcmp(mem->data() + tuple->output_ptr.value, "\xCA\xFE", 2) == 0,
                      "opaque.result bytes at output_ptr");
            }
        }
    }
}

// 4. The section-9 single-arg (i32)->(i32,i32,i32) import: the trampoline must
//    NOT bounds-check the scalar arg as a memory frame (param_frame is empty),
//    and the tuple round-trips the reply. run2 forwards the entry pointer (a
//    real guest address) as the single scalar arg; if the trampoline
//    misread it as a (ptr,len) pair it would trap.
void test_single_arg_import_slot_discipline() {
    const auto bytes = wht::capability_module(7, 1);
    ahfl::runtime::wasm_host::Wasm3ResumeEngine engine;

    bool callback_ran = false;
    auto instantiated = engine.fresh_instance(
        std::span<const std::uint8_t>(bytes),
        [&](const eng::ImportObservation &obs) -> eng::ImportCallbackResult {
            callback_ran = true;
            check(obs.import_ordinal == 0, "single-arg.ordinal == 0");
            check(obs.param_frame.empty(),
                  "single-arg.param_frame empty (scalar, not a memory frame)");
            const std::vector<std::uint8_t> result = {0x01, 0x02, 0x03};
            auto rp = engine.alloc_then_write(std::span<const std::uint8_t>(result));
            if (!rp) {
                return eng::ImportAbort{};
            }
            return eng::ImportReply{.result_ptr = *rp, .result_len = 3};
        });
    check(instantiated.has_value(), "single-arg.fresh_instance");

    const auto entry = entry_bytes();
    auto ptr = engine.alloc_then_write(std::span<const std::uint8_t>(entry));
    check(ptr.has_value(), "single-arg.alloc_then_write");

    auto outcome = engine.invoke_run2(*ptr, static_cast<std::uint32_t>(entry.size()));
    check(outcome.has_value(), "single-arg.invoke_run2 (no trap on scalar arg)");
    check(callback_ran, "single-arg.callback ran");
    if (outcome) {
        const auto *tuple = std::get_if<eng::Run2ResultTuple>(&*outcome);
        check(tuple != nullptr && tuple->raw_status == 0 && tuple->output_len == 3,
              "single-arg.tuple round-trip (0, ptr, 3)");
    }
}

// 5. Trap mapping: unreachable, OOB load, and OOB import param all map to
//    Run2Trapped (not Run2HostAborted, not a tuple).
void test_trap_mapping() {
    {
        const auto bytes = wht::trap_module();
        ahfl::runtime::wasm_host::Wasm3ResumeEngine engine;
        auto inst = engine.fresh_instance(std::span<const std::uint8_t>(bytes),
                                          [](const eng::ImportObservation &) {
                                              return eng::ImportAbort{};
                                          });
        check(inst.has_value(), "trap.unreachable.fresh_instance");
        auto ptr = engine.alloc_then_write(std::span<const std::uint8_t>(entry_bytes()));
        check(ptr.has_value(), "trap.unreachable.alloc");
        auto out = engine.invoke_run2(*ptr, 8);
        check(out.has_value() && std::holds_alternative<eng::Run2Trapped>(*out),
              "trap.unreachable -> Run2Trapped");
    }
    {
        const auto bytes = wht::oob_load_module();
        ahfl::runtime::wasm_host::Wasm3ResumeEngine engine;
        auto inst = engine.fresh_instance(std::span<const std::uint8_t>(bytes),
                                          [](const eng::ImportObservation &) {
                                              return eng::ImportAbort{};
                                          });
        check(inst.has_value(), "trap.oob-load.fresh_instance");
        auto ptr = engine.alloc_then_write(std::span<const std::uint8_t>(entry_bytes()));
        check(ptr.has_value(), "trap.oob-load.alloc");
        auto out = engine.invoke_run2(*ptr, 8);
        check(out.has_value() && std::holds_alternative<eng::Run2Trapped>(*out),
              "trap.oob-load -> Run2Trapped");
    }
    {
        const auto bytes = wht::oob_param_module();
        ahfl::runtime::wasm_host::Wasm3ResumeEngine engine;
        auto inst = engine.fresh_instance(
            std::span<const std::uint8_t>(bytes),
            [](const eng::ImportObservation &) -> eng::ImportCallbackResult {
                return eng::ImportReply{}; // never reached: trampoline OOB-checks first
            });
        check(inst.has_value(), "trap.oob-param.fresh_instance");
        auto ptr = engine.alloc_then_write(std::span<const std::uint8_t>(entry_bytes()));
        check(ptr.has_value(), "trap.oob-param.alloc");
        auto out = engine.invoke_run2(*ptr, 8);
        check(out.has_value() && std::holds_alternative<eng::Run2Trapped>(*out),
              "trap.oob-param -> Run2Trapped (fail closed)");
    }
}

// 6. Host-abort mapping: an ImportAbort callback and a throwing callback both
//    unwind run2 as Run2HostAborted (distinct from a wasm trap).
void test_host_abort_mapping() {
    {
        const auto bytes = wht::capability_module(1, 2);
        ahfl::runtime::wasm_host::Wasm3ResumeEngine engine;
        auto inst = engine.fresh_instance(
            std::span<const std::uint8_t>(bytes),
            [](const eng::ImportObservation &) -> eng::ImportCallbackResult {
                return eng::ImportAbort{};
            });
        check(inst.has_value(), "abort.import-abort.fresh_instance");
        auto ptr = engine.alloc_then_write(std::span<const std::uint8_t>(entry_bytes()));
        auto out = engine.invoke_run2(*ptr, 8);
        check(out.has_value() && std::holds_alternative<eng::Run2HostAborted>(*out),
              "abort.ImportAbort -> Run2HostAborted");
    }
    {
        const auto bytes = wht::capability_module(1, 2);
        ahfl::runtime::wasm_host::Wasm3ResumeEngine engine;
        auto inst = engine.fresh_instance(
            std::span<const std::uint8_t>(bytes),
            [](const eng::ImportObservation &) -> eng::ImportCallbackResult {
                throw std::runtime_error("host callback failed");
            });
        check(inst.has_value(), "abort.exception.fresh_instance");
        auto ptr = engine.alloc_then_write(std::span<const std::uint8_t>(entry_bytes()));
        auto out = engine.invoke_run2(*ptr, 8);
        check(out.has_value() && std::holds_alternative<eng::Run2HostAborted>(*out),
              "abort.exception -> Run2HostAborted (fail closed across C boundary)");
    }
}

// 6b. runv host-abort mapping: a module whose runv calls an ahfl_cap import.
//     When the callback aborts, invoke_runv must return Run2HostAborted
//     (distinct from Run2Trapped, mirroring the run2 three-arm outcome).
void test_runv_host_abort_mapping() {
    const auto bytes = wht::runv_import_module();
    ahfl::runtime::wasm_host::Wasm3ResumeEngine engine;
    auto inst = engine.fresh_instance(
        std::span<const std::uint8_t>(bytes),
        [](const eng::ImportObservation &) -> eng::ImportCallbackResult {
            return eng::ImportAbort{};
        });
    check(inst.has_value(), "runv-abort.fresh_instance");
    auto out = engine.invoke_runv();
    check(out.has_value() &&
              std::holds_alternative<eng::Run2HostAborted>(*out),
          "runv-abort.ImportAbort -> Run2HostAborted (distinct from Run2Trapped)");
}

// 7. Fresh-instance discipline: a second fresh_instance is InvalidSequence;
//    every op before fresh_instance is InvalidSequence; a null/OOB entry is
//    InvalidSequence (and consumes the session, matching the Fake's
//    run_started-before-entry-check discipline); a second invoke_run2 is
//    InvalidSequence.
void test_fresh_instance_discipline() {
    const auto bytes = wht::identity_module();
    ahfl::runtime::wasm_host::Wasm3ResumeEngine engine;

    // Ops before fresh_instance.
    auto mem = engine.read_whole_memory();
    check(!mem.has_value() && mem.error() == eng::EngineError::InvalidSequence,
          "discipline.read before fresh_instance -> InvalidSequence");
    auto alloc = engine.alloc_then_write(std::span<const std::uint8_t>(entry_bytes()));
    check(!alloc.has_value() && alloc.error() == eng::EngineError::InvalidSequence,
          "discipline.alloc before fresh_instance -> InvalidSequence");
    auto invoke = engine.invoke_run2(eng::GuestPointer{1032}, 8);
    check(!invoke.has_value() && invoke.error() == eng::EngineError::InvalidSequence,
          "discipline.invoke before fresh_instance -> InvalidSequence");

    auto inst = engine.fresh_instance(std::span<const std::uint8_t>(bytes),
                                      [](const eng::ImportObservation &) {
                                          return eng::ImportAbort{};
                                      });
    check(inst.has_value(), "discipline.first fresh_instance");

    auto inst2 = engine.fresh_instance(std::span<const std::uint8_t>(bytes),
                                       [](const eng::ImportObservation &) {
                                           return eng::ImportAbort{};
                                       });
    check(!inst2.has_value() && inst2.error() == eng::EngineError::InvalidSequence,
          "discipline.second fresh_instance -> InvalidSequence");

    auto ptr = engine.alloc_then_write(std::span<const std::uint8_t>(entry_bytes()));
    check(ptr.has_value(), "discipline.alloc after fresh_instance");

    // Null entry (consumes the session: run_started is set before the entry
    // check, matching the Fake).
    auto null_invoke = engine.invoke_run2(eng::GuestPointer{0}, 8);
    check(!null_invoke.has_value() &&
              null_invoke.error() == eng::EngineError::InvalidSequence,
          "discipline.null entry -> InvalidSequence");
    // A subsequent valid invoke is also InvalidSequence: the session was
    // consumed by the null entry attempt.
    auto consumed = engine.invoke_run2(*ptr, 8);
    check(!consumed.has_value() && consumed.error() == eng::EngineError::InvalidSequence,
          "discipline.null entry consumes the session");

    // A fresh engine for the OOB-entry + valid-invoke sequence.
    ahfl::runtime::wasm_host::Wasm3ResumeEngine engine2;
    auto inst_b = engine2.fresh_instance(std::span<const std::uint8_t>(bytes),
                                         [](const eng::ImportObservation &) {
                                             return eng::ImportAbort{};
                                         });
    check(inst_b.has_value(), "discipline.engine2 fresh_instance");
    auto ptr_b = engine2.alloc_then_write(std::span<const std::uint8_t>(entry_bytes()));
    check(ptr_b.has_value(), "discipline.engine2 alloc");
    auto oob_invoke = engine2.invoke_run2(eng::GuestPointer{65530}, 16);
    check(!oob_invoke.has_value() &&
              oob_invoke.error() == eng::EngineError::InvalidSequence,
          "discipline.OOB entry -> InvalidSequence");

    // A third engine for the valid + second-invoke sequence.
    ahfl::runtime::wasm_host::Wasm3ResumeEngine engine3;
    auto inst_c = engine3.fresh_instance(std::span<const std::uint8_t>(bytes),
                                         [](const eng::ImportObservation &) {
                                             return eng::ImportAbort{};
                                         });
    check(inst_c.has_value(), "discipline.engine3 fresh_instance");
    auto ptr_c = engine3.alloc_then_write(std::span<const std::uint8_t>(entry_bytes()));
    check(ptr_c.has_value(), "discipline.engine3 alloc");
    auto good = engine3.invoke_run2(*ptr_c, 8);
    check(good.has_value(), "discipline.first invoke_run2");
    auto second = engine3.invoke_run2(*ptr_c, 8);
    check(!second.has_value() && second.error() == eng::EngineError::InvalidSequence,
          "discipline.second invoke_run2 -> InvalidSequence");
}

// 8. Capacity exhaustion: the bump allocator never reclaims; a frame that does
//    not fit returns MemoryCapacityExceeded with NO partial mutation (the bump
//    does not advance, so a smaller frame still fits afterwards).
void test_capacity_exhaustion() {
    const auto bytes = wht::identity_module();
    ahfl::runtime::wasm_host::Wasm3ResumeEngine engine;
    auto inst = engine.fresh_instance(std::span<const std::uint8_t>(bytes),
                                      [](const eng::ImportObservation &) {
                                          return eng::ImportAbort{};
                                      });
    check(inst.has_value(), "capacity.fresh_instance");

    // Heap base 1032; 15 x 4096 = 61440 -> 62472; the 16th (66568) exceeds
    // 65536. Exactly one fixed-size memory, whole page 65536.
    const std::vector<std::uint8_t> frame(4096, 0xAB);
    for (int i = 0; i < 15; ++i) {
        auto p = engine.alloc_then_write(std::span<const std::uint8_t>(frame));
        check(p.has_value(), "capacity.alloc " + std::to_string(i) + " of 15");
    }
    auto overflow = engine.alloc_then_write(std::span<const std::uint8_t>(frame));
    check(!overflow.has_value() &&
              overflow.error() == eng::EngineError::MemoryCapacityExceeded,
          "capacity.16th 4096 frame -> MemoryCapacityExceeded");

    // No partial mutation: the bump did not advance, so a smaller frame fits.
    const std::vector<std::uint8_t> small(100, 0xCD);
    auto small_ptr = engine.alloc_then_write(std::span<const std::uint8_t>(small));
    check(small_ptr.has_value(), "capacity.smaller frame after OOM still fits");
    check(small_ptr->value == 62472, "capacity.bump did not advance on OOM (no partial mutation)");

    // The whole page is still exactly 65536 bytes.
    auto mem = engine.read_whole_memory();
    check(mem.has_value() && mem->size() == 65536, "capacity.whole page still 65536");
}

// 8b. Exact capacity boundary: the alloc that fills the page EXACTLY (heap
//     base 1032 -> 65536, i.e. 64504 bytes) succeeds and lands the bump on the
//     page end; one byte more (64505) fails MemoryCapacityExceeded.
void test_capacity_exact_boundary() {
    const auto bytes = wht::identity_module();
    // Heap base kHeapBase (1032); page kPageSize (65536). Exact fill =
    // 65536 - 1032 = 64504 (the bump lands exactly on 65536, not past it);
    // 64505 = fill + 1 exceeds the page.
    const std::uint32_t exact_fill = wht::kPageSize - wht::kHeapBase;

    {
        ahfl::runtime::wasm_host::Wasm3ResumeEngine engine;
        auto inst = engine.fresh_instance(std::span<const std::uint8_t>(bytes),
                                          [](const eng::ImportObservation &) {
                                              return eng::ImportAbort{};
                                          });
        check(inst.has_value(), "capacity-boundary.fresh_instance (exact fill)");
        const std::vector<std::uint8_t> frame(exact_fill, 0xAB);
        auto p = engine.alloc_then_write(std::span<const std::uint8_t>(frame));
        check(p.has_value() && p->value == wht::kHeapBase,
              "capacity-boundary.alloc 64504 (exact fill) succeeds at heap base");
        // The bump is now exactly on the page end: even a 1-byte alloc fails.
        const std::vector<std::uint8_t> one(1, 0xCD);
        auto overflow = engine.alloc_then_write(std::span<const std::uint8_t>(one));
        check(!overflow.has_value() &&
                  overflow.error() == eng::EngineError::MemoryCapacityExceeded,
              "capacity-boundary.1 byte after exact fill -> MemoryCapacityExceeded");
    }
    {
        // One byte more than the exact fill, on a fresh engine: fails.
        ahfl::runtime::wasm_host::Wasm3ResumeEngine engine;
        auto inst = engine.fresh_instance(std::span<const std::uint8_t>(bytes),
                                          [](const eng::ImportObservation &) {
                                              return eng::ImportAbort{};
                                          });
        check(inst.has_value(), "capacity-boundary.fresh_instance (fill + 1)");
        const std::vector<std::uint8_t> frame(exact_fill + 1, 0xAB);
        auto overflow = engine.alloc_then_write(std::span<const std::uint8_t>(frame));
        check(!overflow.has_value() &&
                  overflow.error() == eng::EngineError::MemoryCapacityExceeded,
              "capacity-boundary.alloc 64505 (fill + 1) -> MemoryCapacityExceeded");
    }
}

// 8c. memory.grow mid-invoke fails closed: a module with a well-formed Memory
//     section (min=1, no max) passes the structural gate but grows the linear
//     memory during run2 (wasm3 v0.9.0 reallocates). The fixed single page is
//     a session invariant, so invoke_run2 returns MemoryCapacityExceeded (not
//     a tuple, not Run2Trapped) and read_whole_memory afterwards fails
//     closed (InstanceUnavailable).
void test_memory_grow_fails_closed() {
    const auto bytes = wht::memory_grow_module();
    ahfl::runtime::wasm_host::Wasm3ResumeEngine engine;
    auto inst = engine.fresh_instance(std::span<const std::uint8_t>(bytes),
                                      [](const eng::ImportObservation &) {
                                          return eng::ImportAbort{};
                                      });
    check(inst.has_value(), "grow.fresh_instance (well-formed Memory section)");
    auto ptr = engine.alloc_then_write(std::span<const std::uint8_t>(entry_bytes()));
    check(ptr.has_value(), "grow.alloc before grow");
    auto out = engine.invoke_run2(*ptr, 8);
    check(!out.has_value() && out.error() == eng::EngineError::MemoryCapacityExceeded,
          "grow.memory.grow mid-invoke -> MemoryCapacityExceeded (fixed-page invariant)");
    // The page is no longer the fixed 64 KiB: read_whole_memory fails closed.
    auto mem = engine.read_whole_memory();
    check(!mem.has_value() && mem.error() == eng::EngineError::InstanceUnavailable,
          "grow.read_whole_memory after grow fails closed (InstanceUnavailable)");
}

// 8d. Non-zero raw status pass-through: run2 returns 0xDEADBEEF with the entry
//     ptr/len; the engine must deliver the raw u32 status verbatim with NO
//     classification (zero is not special-cased).
void test_nonzero_raw_status_passthrough() {
    const auto bytes = wht::nonzero_status_module();
    ahfl::runtime::wasm_host::Wasm3ResumeEngine engine;
    auto inst = engine.fresh_instance(std::span<const std::uint8_t>(bytes),
                                      [](const eng::ImportObservation &) {
                                          return eng::ImportAbort{};
                                      });
    check(inst.has_value(), "nonzero-status.fresh_instance");
    const auto entry = entry_bytes();
    auto ptr = engine.alloc_then_write(std::span<const std::uint8_t>(entry));
    check(ptr.has_value(), "nonzero-status.alloc");
    auto out = engine.invoke_run2(*ptr, static_cast<std::uint32_t>(entry.size()));
    check(out.has_value(), "nonzero-status.invoke_run2");
    if (out) {
        const auto *tuple = std::get_if<eng::Run2ResultTuple>(&*out);
        check(tuple != nullptr, "nonzero-status.outcome is a tuple");
        if (tuple != nullptr) {
            check(tuple->raw_status == 0xDEADBEEFu,
                  "nonzero-status.raw_status == 0xDEADBEEF verbatim (no classification)");
            check(tuple->output_ptr == *ptr, "nonzero-status.output_ptr == entry ptr");
            check(tuple->output_len == entry.size(),
                  "nonzero-status.output_len == entry len");
        }
    }
}

// 8e. Move semantics: move-construct and move-assign a fresh-instantiated
//     engine and let both moved-from objects die. The Impl dtor owns wasm3
//     teardown, so a moved-from engine (null impl_) destructs as a no-op and a
//     move-assigned engine's old session is torn down (no leak, no UB). Under
//     ASan/UBSan this proves the RAII discipline (P1-1 review fix-forward).
void test_move_semantics() {
    const auto bytes = wht::identity_module();
    {
        // Move-construct from a fresh-instantiated engine.
        ahfl::runtime::wasm_host::Wasm3ResumeEngine engine;
        auto inst = engine.fresh_instance(std::span<const std::uint8_t>(bytes),
                                          [](const eng::ImportObservation &) {
                                              return eng::ImportAbort{};
                                          });
        check(inst.has_value(), "move.fresh_instance (move-construct source)");
        ahfl::runtime::wasm_host::Wasm3ResumeEngine moved(std::move(engine));
        // The moved-from engine dies at scope exit: must be a no-op (null
        // impl_, no null-deref). The moved-to engine owns the session.
        auto mem = moved.read_whole_memory();
        check(mem.has_value() && mem->size() == 65536,
              "move.move-constructed engine owns the session");
    }
    {
        // Move-assign onto a fresh-instantiated engine: the target's old
        // session must be torn down (no wasm3 resource leak) and the
        // moved-from object must destruct safely.
        ahfl::runtime::wasm_host::Wasm3ResumeEngine engine;
        auto inst = engine.fresh_instance(std::span<const std::uint8_t>(bytes),
                                          [](const eng::ImportObservation &) {
                                              return eng::ImportAbort{};
                                          });
        check(inst.has_value(), "move.fresh_instance (move-assign source)");
        ahfl::runtime::wasm_host::Wasm3ResumeEngine target;
        auto inst_t = target.fresh_instance(std::span<const std::uint8_t>(bytes),
                                            [](const eng::ImportObservation &) {
                                                return eng::ImportAbort{};
                                            });
        check(inst_t.has_value(), "move.fresh_instance (move-assign target)");
        target = std::move(engine);
        // The moved-from engine dies at scope exit: must be a no-op. The
        // move-assigned engine owns the (source's) session.
        auto mem = target.read_whole_memory();
        check(mem.has_value() && mem->size() == 65536,
              "move.move-assigned engine owns the session (old session torn down)");
    }
}

// 9. A non-ahfl_cap import is rejected structurally at fresh_instance.
void test_unbound_import_rejected() {
    const auto bytes = wht::unbound_import_module();
    ahfl::runtime::wasm_host::Wasm3ResumeEngine engine;
    auto inst = engine.fresh_instance(std::span<const std::uint8_t>(bytes),
                                      [](const eng::ImportObservation &) {
                                          return eng::ImportAbort{};
                                      });
    check(!inst.has_value() && inst.error() == eng::EngineError::InstanceUnavailable,
          "unbound.wasi import -> InstanceUnavailable");
}

// 10. A non-conforming memory declaration (min 2 pages, or a declared max) is
//     rejected by the fixed-page cross-check.
void test_bad_memory_rejected() {
    {
        const auto bytes = wht::identity_module(2);
        ahfl::runtime::wasm_host::Wasm3ResumeEngine engine;
        auto inst = engine.fresh_instance(std::span<const std::uint8_t>(bytes),
                                          [](const eng::ImportObservation &) {
                                              return eng::ImportAbort{};
                                          });
        check(!inst.has_value() && inst.error() == eng::EngineError::InstanceUnavailable,
              "bad-memory.min 2 pages -> InstanceUnavailable");
    }
    {
        // A module with a declared max (memory section flags 1). Build it by
        // hand from the identity fixture's sections is overkill; instead use
        // the fact that identity_module(1) with a max is the same shape. We
        // assemble a minimal module with a max memory and no run2/alloc so
        // the memory gate fires first.
        std::vector<std::uint8_t> m = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
        wht::put_section(m, 5, wht::memory_section(1, /*has_max=*/true, 2));
        ahfl::runtime::wasm_host::Wasm3ResumeEngine engine;
        auto inst = engine.fresh_instance(std::span<const std::uint8_t>(m),
                                          [](const eng::ImportObservation &) {
                                              return eng::ImportAbort{};
                                          });
        check(!inst.has_value() && inst.error() == eng::EngineError::InstanceUnavailable,
              "bad-memory.declared max -> InstanceUnavailable");
    }
}

// 11. A missing run2 or alloc export is rejected at fresh_instance.
void test_missing_exports_rejected() {
    {
        const auto bytes = wht::identity_module(1, /*emit_run2=*/false);
        ahfl::runtime::wasm_host::Wasm3ResumeEngine engine;
        auto inst = engine.fresh_instance(std::span<const std::uint8_t>(bytes),
                                          [](const eng::ImportObservation &) {
                                              return eng::ImportAbort{};
                                          });
        check(!inst.has_value() && inst.error() == eng::EngineError::InstanceUnavailable,
              "missing-export.no run2 -> InstanceUnavailable");
    }
    {
        const auto bytes = wht::identity_module(1, /*emit_run2=*/true, /*emit_alloc=*/false);
        ahfl::runtime::wasm_host::Wasm3ResumeEngine engine;
        auto inst = engine.fresh_instance(std::span<const std::uint8_t>(bytes),
                                          [](const eng::ImportObservation &) {
                                              return eng::ImportAbort{};
                                          });
        check(!inst.has_value() && inst.error() == eng::EngineError::InstanceUnavailable,
              "missing-export.no alloc -> InstanceUnavailable");
    }
}

} // namespace

int main() {
    test_identity_end_to_end();
    test_import_free_workflow_shared_fixture();
    test_opaque_import_a2_admitted();
    test_single_arg_import_slot_discipline();
    test_trap_mapping();
    test_host_abort_mapping();
    test_runv_host_abort_mapping();
    test_fresh_instance_discipline();
    test_capacity_exhaustion();
    test_capacity_exact_boundary();
    test_memory_grow_fails_closed();
    test_nonzero_raw_status_passthrough();
    test_move_semantics();
    test_unbound_import_rejected();
    test_bad_memory_rejected();
    test_missing_exports_rejected();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
