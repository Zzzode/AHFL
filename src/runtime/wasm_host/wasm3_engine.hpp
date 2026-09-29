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

#include "runtime/engine/core_wasm_resume_engine.hpp"

namespace ahfl::runtime::wasm_host {

namespace detail {
struct Wasm3EngineImpl;
} // namespace detail

// One fresh-instance wasm3 engine session. Move-only; the destructor frees the
// wasm3 runtime (which owns the loaded module) and then the environment.
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

  private:
    std::unique_ptr<detail::Wasm3EngineImpl> impl_;
};

} // namespace ahfl::runtime::wasm_host
