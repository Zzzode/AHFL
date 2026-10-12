#pragma once

// RFC 0026 KR6.8 WH-1: the CoreWasmResumeEngine port over the vendored wasm3
// interpreter (third_party/wasm3, MIT). This is the first REAL WebAssembly
// execution engine for the embedded host: unlike the in-test synchronous Fake
// and the Node subprocess port, it instantiates and drives actual wasm modules
// in-process through the wasm3 C API. It still has NO production runtime
// caller in this slice (the production driver wiring is WH-3+); the evidence
// class is "real wasm3 VM executing real modules", not wasmtime.
//
// The engine implements the port contract in
// src/runtime/engine/core_wasm_resume_engine.hpp EXACTLY:
//   * fresh_instance once (a second call is InvalidSequence);
//   * read_whole_memory returns the WHOLE fixed 64 KiB page;
//   * alloc_then_write drives the module's own exported checked allocator and
//     writes the exact bytes (instance-lifetime, never reclaimed);
//   * invoke_run_workflow invokes run_workflow and delivers every ahfl_cap import
//     synchronously, nested on the same stack, passing the raw u32 status
//     through verbatim (the engine never classifies OK/ERROR/PENDING).
//
// Fail-closed discipline (decision doc section 5):
//   * the module's Memory section is validated structurally (exactly one
//     memory, no max, min one page) and cross-checked against the F1 SSOT
//     capacity BEFORE any wasm3 memory access (m3_GetMemory dereferences the
//     memory unconditionally and would crash on a memory-less module);
//   * every guest pointer the module hands back is bounds-checked against the
//     live page (m3ApiCheckMem discipline), including the import param frame;
//   * every func import must be ahfl_cap.cap_<decimal> with the bridge
//     (i32)->(i32,i32) shape; anything else is InstanceUnavailable;
//   * a host ImportAbort (or a C++ exception escaping the callback across the C
//     boundary) unwinds run_workflow as WorkflowRunHostAborted via a distinct sentinel trap;
//   * wasm3 trap pointers map to WorkflowRunTrapped by exact pointer identity.

#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string_view>
#include <variant>

#include "runtime/engine/core_wasm_resume_engine.hpp"

namespace ahfl::runtime::wasm_host {

namespace detail {
struct Wasm3EngineImpl;
} // namespace detail

// RFC 0026 KR6.8 WH-2: the P6-frame run_frame outcome. A P6-frame module's
// `run_frame() -> (status:i32, value_ptr:i32)` is a DISTINCT entry point from the
// run_workflow resume port: it drives the state walk and returns the borrowed input
// base (identity final) or the materialized output base (computed final). The
// engine grows these two additive capabilities (run_frame invocation + mutable
// whole-memory access for the host frame writer) without touching the port
// header or changing WH-1 semantics (decision doc section 11.1).
//
// The three arms mirror WorkflowRunOutcome exactly: a host ImportAbort unwinds run_frame
// as WorkflowRunHostAborted (the host, not the module, aborted -- distinct from a
// Wasm trap), so the driver can classify the two failure modes separately.
struct FrameRunResult {
    std::uint32_t raw_status{0};
    core_wasm_resume_engine::GuestPointer value_ptr{};
};
using FrameRunOutcome = std::variant<FrameRunResult, core_wasm_resume_engine::WorkflowRunTrapped,
                                 core_wasm_resume_engine::WorkflowRunHostAborted>;

// One fresh-instance wasm3 engine session. Move-only; the Impl destructor is
// the single owner of wasm3 teardown (the runtime, which owns the loaded
// module after a successful m3_LoadModule; a module that failed to load; and
// the environment), so the defaulted move ops are safe: a moved-from engine
// holds a null impl_ and a move-assigned engine's old session is torn down.
class Wasm3ResumeEngine final : public core_wasm_resume_engine::CoreWasmResumeEngine {
  public:
    Wasm3ResumeEngine() noexcept;
    ~Wasm3ResumeEngine() override;

    Wasm3ResumeEngine(const Wasm3ResumeEngine &) = delete;
    Wasm3ResumeEngine &operator=(const Wasm3ResumeEngine &) = delete;
    Wasm3ResumeEngine(Wasm3ResumeEngine &&) noexcept;
    Wasm3ResumeEngine &operator=(Wasm3ResumeEngine &&) noexcept;

    [[nodiscard]] std::expected<void, core_wasm_resume_engine::EngineError>
    fresh_instance(std::span<const std::uint8_t> module_bytes,
                   core_wasm_resume_engine::ImportCallback import_callback) override;

    [[nodiscard]] std::expected<std::span<const std::uint8_t>, core_wasm_resume_engine::EngineError>
    read_whole_memory() override;

    [[nodiscard]] std::expected<core_wasm_resume_engine::GuestPointer,
                                core_wasm_resume_engine::EngineError>
    alloc_then_write(std::span<const std::uint8_t> bytes) override;

    [[nodiscard]] std::expected<core_wasm_resume_engine::WorkflowRunOutcome,
                                core_wasm_resume_engine::EngineError>
    invoke_run_workflow(core_wasm_resume_engine::GuestPointer entry_ptr,
                std::uint32_t entry_len) override;

    // WH-2 additive: the mutable counterpart of read_whole_memory, for the host
    // frame writer (the P6-frame packer writes the input frame at its fixed
    // regions directly into the live instance memory, exactly as the JS oracle
    // writes e.memory.buffer). The span is valid until the next mutating call.
    // The caller is the trusted host frame writer; it must not clobber the
    // read-only rodata region [256,1024) the Data section initialized.
    [[nodiscard]] std::expected<std::span<std::uint8_t>,
                                core_wasm_resume_engine::EngineError>
    mutable_whole_memory() override;

    // WH-2 additive: invoke the module's `run_frame() -> (i32,i32)` P6-frame entry.
    // Found lazily on first call (m3_FindFunction compiles eagerly, surfacing a
    // compile error here rather than at fresh_instance, so non-P6 modules that
    // do not export run_frame still instantiate). One-shot like invoke_run_workflow (shares
    // the run_started gate): a session runs exactly one lane.
    [[nodiscard]] std::expected<FrameRunOutcome, core_wasm_resume_engine::EngineError>
    invoke_run_frame();

    // WH-4 additive: invoke the module's `step() -> i32` export. Drives one
    // state transition and returns the new state index. Found lazily on first
    // call (like run_frame); a module that does not export step (a workflow module,
    // whose step/current_state trap by contract) returns InstanceUnavailable.
    // NOT gated by run_started: the agent_session drives step() in a bounded
    // loop on a separate effects-free instance before the canonical run_frame/run_workflow.
    [[nodiscard]] std::expected<std::uint32_t, core_wasm_resume_engine::EngineError>
    invoke_step();

    // WH-4 additive: invoke the module's `current_state() -> i32` export.
    // Returns the current state index without driving a transition. Found
    // lazily like invoke_step.
    [[nodiscard]] std::expected<std::uint32_t, core_wasm_resume_engine::EngineError>
    invoke_current_state();

    // WH-4 additive: read the module's `transition_count` exported mutable
    // i32 global. Found lazily like invoke_step. The agent_session asserts
    // exactly-once bump per step() transition.
    [[nodiscard]] std::expected<std::uint32_t, core_wasm_resume_engine::EngineError>
    read_transition_count();

    // WH-4 additive: read an exported mutable i32 global by name. Found
    // lazily on first call (like run_frame). The workflow_session uses this for
    // `transition_count` and `workflow_completed_count`; a module that does
    // not export the named global returns InstanceUnavailable.
    [[nodiscard]] std::expected<std::uint32_t, core_wasm_resume_engine::EngineError>
    read_exported_global_u32(std::string_view name);

  private:
    std::unique_ptr<detail::Wasm3EngineImpl> impl_;
};

} // namespace ahfl::runtime::wasm_host
