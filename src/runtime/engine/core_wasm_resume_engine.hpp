#pragma once

// RFC 0026 KR6.5 E4-B2-D2a (F4): the ENGINE PORT over which the production
// resume-host driver (core_wasm_resume_host.hpp) runs one fresh Core-Wasm
// instance.
//
// The seam (docs/design/core-ir-kr6-5-e4b-wire-resume-seam.zh.md section 5 step
// 7 and section 5.3 L0-L5) splits the replay into a host-INDEPENDENT decision
// authority (the landed D1b controller) and an engine-facing adapter. THIS is
// that adapter's port: every action the controller only DECIDES -- fresh
// instantiation, linear-memory allocation/write, the run2 invocation, reading
// the whole 64 KiB page, the synchronous capability import callback -- crosses
// this interface. The production driver is therefore engine-agnostic: the
// first port is the in-test synchronous FakeResumeEngine (same hand-built
// evidence class as D1b's memory fixtures); the Node embedded-engine port
// (F5) implements the SAME interface over a subprocess WebAssembly engine.
//
// Port contract (the minimal sound subset the replay needs):
//   * fresh_instance() once, before any other call; instantiates a FRESH
//     module instance (never a reused/persistent one) with exactly one linear
//     memory of the fixed single-page capacity.
//   * read_whole_memory() returns the WHOLE fixed 64 KiB page as a
//     non-owning span (the controller re-decodes the node-event prefix from
//     it, never from a caller-cooked shorter buffer).
//   * alloc_then_write(bytes) bumps one instance-lifetime, never-reclaimed
//     L0/L3/L4 frame allocation (seam 5.3: instance-lifetime ownership, no
//     GC) and returns the guest pointer it wrote the exact bytes at.
//   * invoke_run2(ptr,len) invokes the module entry with the L0 entry frame
//     and blocks until run2 returns; a module-issued ahfl_cap import is
//     delivered SYNCHRONOUSLY to the host callback set at fresh_instance, so
//     the callback always runs nested inside invoke_run2 on the same call
//     stack. The raw run2 result is the guest multivalue tuple
//     (status,ptr,len); a Wasm trap is its own arm.
// The engine MUST NOT classify OK / ERROR / PENDING and MUST NOT invent a
// status: the raw u32 word is classified solely by the D1b controller
// (seam: "the adapter never pre-classifies").
//
// FOUNDATION: the port itself carries no store, no decision logic, and no
// resume.* string. The synchronous in-test double is NOT a self-built VM;
// this slice remains FOUNDATION evidence (no real-Wasm production caller).

#include <cstdint>
#include <expected>
#include <functional>
#include <span>
#include <variant>
namespace ahfl::runtime::core_wasm_resume_engine {

// A guest linear-memory pointer (wasm32 address space). Distinct from every
// payload-store / symbol id so the three can never share an integer.
struct GuestPointer {
    std::uint32_t value{0};
    [[nodiscard]] friend bool operator==(GuestPointer, GuestPointer) noexcept = default;
};

// The exact multivalue run2 returns: the raw AHFL_CAP_* status word plus the
// output (ptr,len) the module forwarded. The driver/controller, not the
// engine, classifies `status`.
struct Run2ResultTuple {
    std::uint32_t raw_status{0};
    GuestPointer output_ptr{};
    std::uint32_t output_len{0};
};

// run2 raised a Wasm trap rather than returning a tuple.
struct Run2Trapped {};

// run2 unwound because the host import callback returned ImportAbort: the
// host's decision failed closed and the engine never resumed module execution
// (distinct from an unrelated Wasm trap -- the host, not the module, aborted).
struct Run2HostAborted {};

using Run2Outcome = std::variant<Run2ResultTuple, Run2Trapped, Run2HostAborted>;

// One synchronous module import, observed by the engine inside run2 and
// handed to the host callback. `import_ordinal` is the position in the
// module's ahfl_cap import table (the A2 call-site cursor is joined from it);
// `param_frame` is the L2 module-owned Param view (read-only borrow of guest
// memory, never retained past the callback's return); `whole_memory` is the
// whole fixed page at the moment of the call, the controller's sole
// node-event authority.
struct ImportObservation {
    std::uint32_t import_ordinal{0};
    std::span<const std::uint8_t> param_frame{};
    std::span<const std::uint8_t> whole_memory{};
};

// The host's synchronous answer to one import is EITHER the frame to return
// (its bytes already written into guest memory by the host via
// alloc_then_write) OR an explicit abort: the host decision failed closed, no
// result frame may be returned, and the engine must unwind run2 WITHOUT
// resuming module execution (a real embedded engine realizes the unwind by
// throwing out of the JS import callback, which traps the Wasm call). The host
// records the precise reason in its own state; the abort tag itself carries no
// error classification.
struct ImportReply {
    GuestPointer result_ptr{};
    std::uint32_t result_len{0};
};
struct ImportAbort {};
using ImportCallbackResult = std::variant<ImportReply, ImportAbort>;

// The engine invokes this synchronously, nested in invoke_run2, and blocks on
// its return before resuming module execution. It receives the observation by
// const ref (valid only for the call).
using ImportCallback = std::function<ImportCallbackResult(const ImportObservation &)>;

// The typed, fail-closed reasons an engine-port OPERATION (instantiate /
// read / alloc / invoke plumbing) can fail with. A host DECISION failing
// inside the import callback is NOT reported here: it returns ImportAbort and
// run2 unwinds with Run2HostAborted, carrying the host's own typed reason.
enum class EngineError : std::uint8_t {
    InvalidSequence,  // an operation ran before fresh_instance / out of order
    MemoryCapacityExceeded, // a write/alloc does not fit the fixed page
    InstanceUnavailable, // the engine could not instantiate / drive the module
};

// One fresh-instance engine session. The production driver owns exactly one
// per replay. Implementations wrap one concrete Wasm engine (the in-test
// synchronous double, or F5's Node embedded engine).
class CoreWasmResumeEngine {
  public:
    virtual ~CoreWasmResumeEngine() = default;

    // Instantiate a FRESH instance from the digest-admitted module bytes with
    // the single synchronous import callback. Called once; a second call is
    // InvalidSequence.
    [[nodiscard]] virtual std::expected<void, EngineError>
    fresh_instance(std::span<const std::uint8_t> module_bytes, ImportCallback import_callback) = 0;

    // The WHOLE fixed 64 KiB linear-memory page as a read-only span. The
    // returned view is valid until the next alloc_then_write / invoke_run2
    // call (a real engine may detach/re-grow the buffer), so the driver hands
    // it to the controller and finishes that decision BEFORE mutating memory.
    [[nodiscard]] virtual std::expected<std::span<const std::uint8_t>, EngineError>
    read_whole_memory() = 0;

    // One bump allocation: reserve `bytes.size()` bytes of instance memory,
    // write the exact bytes, and return the guest pointer. Instance-lifetime,
    // never reclaimed. Fails MemoryCapacityExceeded without partial mutation
    // when the frame does not fit.
    [[nodiscard]] virtual std::expected<GuestPointer, EngineError>
    alloc_then_write(std::span<const std::uint8_t> bytes) = 0;

    // Invoke run2(entry_ptr, entry_len) with the L0 entry frame and block
    // until it returns or traps, delivering every ahfl_cap import
    // synchronously to the fresh_instance callback on the same stack.
    [[nodiscard]] virtual std::expected<Run2Outcome, EngineError>
    invoke_run2(GuestPointer entry_ptr, std::uint32_t entry_len) = 0;
};

} // namespace ahfl::runtime::core_wasm_resume_engine
