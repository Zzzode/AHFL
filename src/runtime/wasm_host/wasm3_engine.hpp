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
//   * invoke_run2 invokes run2 and delivers every ahfl_cap import
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
//   * every func import must be ahfl_cap.cap_<decimal> with the opaque
//     (i32,i32)->(i32,i32,i32) or the section-9 (i32)->(i32,i32,i32) shape;
//     anything else is InstanceUnavailable;
//   * a host ImportAbort (or a C++ exception escaping the callback across the C
//     boundary) unwinds run2 as Run2HostAborted via a distinct sentinel trap;
//   * wasm3 trap pointers map to Run2Trapped by exact pointer identity.

#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <variant>

#include "runtime/engine/core_wasm_resume_engine.hpp"

namespace ahfl::runtime::wasm_host {

namespace detail {
struct Wasm3EngineImpl;
} // namespace detail

// RFC 0026 KR6.8 WH-2: the P6-frame runv outcome. A P6-frame module's
// `runv() -> (status:i32, value_ptr:i32)` is a DISTINCT entry point from the
// run2 resume port: it drives the state walk and returns the borrowed input
// base (identity final) or the materialized output base (computed final). The
// engine grows these two additive capabilities (runv invocation + mutable
// whole-memory access for the host frame writer) without touching the port
// header or changing WH-1 semantics (decision doc section 11.1).
struct RunvResult {
    std::uint32_t raw_status{0};
    core_wasm_resume_engine::GuestPointer value_ptr{};
};
using RunvOutcome = std::variant<RunvResult, core_wasm_resume_engine::Run2Trapped>;

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

    [[nodiscard]] std::expected<core_wasm_resume_engine::Run2Outcome,
                                core_wasm_resume_engine::EngineError>
    invoke_run2(core_wasm_resume_engine::GuestPointer entry_ptr,
                std::uint32_t entry_len) override;

    // WH-2 additive: the mutable counterpart of read_whole_memory, for the host
    // frame writer (the P6-frame packer writes the input frame at its fixed
    // regions directly into the live instance memory, exactly as the JS oracle
    // writes e.memory.buffer). The span is valid until the next mutating call.
    // The caller is the trusted host frame writer; it must not clobber the
    // read-only rodata region [256,1024) the Data section initialized.
    [[nodiscard]] std::expected<std::span<std::uint8_t>,
                                core_wasm_resume_engine::EngineError>
    mutable_whole_memory();

    // WH-2 additive: invoke the module's `runv() -> (i32,i32)` P6-frame entry.
    // Found lazily on first call (m3_FindFunction compiles eagerly, surfacing a
    // compile error here rather than at fresh_instance, so non-P6 modules that
    // do not export runv still instantiate). One-shot like invoke_run2 (shares
    // the run_started gate): a session runs exactly one lane.
    [[nodiscard]] std::expected<RunvOutcome, core_wasm_resume_engine::EngineError>
    invoke_runv();

  private:
    std::unique_ptr<detail::Wasm3EngineImpl> impl_;
};

} // namespace ahfl::runtime::wasm_host
