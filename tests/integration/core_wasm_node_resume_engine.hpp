#pragma once

// RFC 0026 KR6.5 E4-B2-D2a (F5): the F4 CoreWasmResumeEngine port implemented over
// a REAL WebAssembly execution engine: an embedded Node.js (V8) subprocess.
//
// This is TEST-SUPPORT evidence infrastructure (tests/integration), never
// production code. It implements the same engine-port contract the production
// resume-host driver (src/runtime/engine/core_wasm_resume_host.hpp) runs over, so
// the landed engine-agnostic driver can be driven end-to-end against a real Wasm
// engine without wasmtime: Node v22 instantiates the B2-C-emitted module with its
// ahfl_cap import and exposes the single exported 64 KiB linear memory, the bump
// `alloc`, and the multivalue run2.
//
// The protocol is the SIMPLEST SOUND synchronous one over two channels (child fd
// 0 = parent->child commands, child fd 1 = child->parent frames; child stderr is
// drained to a diagnostic string). The command channel is a SOCK_STREAM
// socketpair and every command byte is sent with MSG_NOSIGNAL, so a write to a
// dead child surfaces as EPIPE -> a typed InstanceUnavailable instead of a
// SIGPIPE that would signal-kill the host (the frames travel on an anonymous
// pipe, read-only on the parent side). Every frame is:
//   u8 kind | u32 little-endian payload length n | n payload bytes
// Directions are segregated by fd (no direction tag can desync the stream), and
// every transfer is exact-length.
//
//   parent -> child commands:
//     1 instantiate          (empty) -> reply 16 instantiated / 99 fatal
//     2 alloc_then_write     (u32 len | len bytes) -> reply 17 (u32 ptr; 0 = OOM)
//     3 invoke_run2         (u32 ptr | u32 len) -> reply 19 outcome
//     4 read_whole_memory   (empty) -> reply 18 (the exact 64 KiB page)
//     5 bye                  (empty); child exits 0
//   child -> parent frames:
//     0 ready (sent once at script startup after WebAssembly.compile)
//     10 import_observation (u32 ordinal | u32 param_ptr | u32 param_len |
//                            the verbatim 64 KiB page; total payload 12+65536
//                            bytes) -- the child is blocked in fs.readSync(0)
//     16 instantiated
//     17 alloc_result       (u32 ptr)
//     18 whole_memory       (65536 bytes)
//     19 run2_outcome       (u8 arm: 0 tuple | 1 trap | 2 host-aborted;
//                            tuple carries u32 status | u32 out_ptr | u32 out_len)
//     99 fatal              (UTF-8 diagnostic; the session is dead)
//   while run2 is parked on an import the child runs a NESTED command loop over
//   the same fd 0, accepting command 2 (alloc_then_write -> reply 17) and exactly
//   one terminal decision:
//     6 import_reply        (u32 ptr | u32 len) -> the import returns [OK,ptr,len]
//     7 import_abort        (empty) -> the import throws; run2 unwinds and the
//                            outcome reports the host-aborted arm, never a trap
// so the port's nested alloc_then_write/ImportReply sequence round-trips
// faithfully with the real guest allocation pointer.
//
// Evidence honesty: this proves the production driver against a real embedded
// Wasm engine. It is explicitly NOT wasmtime evidence and does NOT close KR6.5 /
// D2a; it also drives the non-conforming integrity store.

#include <cstdint>
#include <expected>
#include <memory>
#include <string>

#include "runtime/engine/core_wasm_resume_engine.hpp"

namespace ahfl::runtime::core_wasm_node_resume_engine {

// Short alias for the F4 engine-port namespace (file-local convenience).
namespace engine = core_wasm_resume_engine;

// Why a Node embedded-engine session could not be established or driven. Every
// arm maps to the port's EngineError::InstanceUnavailable at the e2e boundary
// (this adapter performs no classification of host decisions or run statuses).
enum class NodeEngineError : std::uint8_t {
    NodeUnavailable, // no node executable found on PATH
    ScriptFailed,    // host.mjs could not be materialized
    PipeFailed,      // the command/frame/stderr channel could not be created
    SpawnFailed,     // the subprocess could not be started
    ProtocolFailed,  // malformed / short / unknown frame, or a child fatal report
    ChildExited,     // the child exited before the expected frame
    TimedOut,        // a blocking read exceeded the session deadline
};

struct NodeEngineLaunchError {
    NodeEngineError reason{NodeEngineError::NodeUnavailable};
    // Verbatim child stderr / fatal-frame payload, retained for the test's
    // fail-closed diagnostics; never echoed into a resume.* string.
    std::string diagnostic;
};

// The F5 engine port over one Node subprocess. One instance per replay; the
// process is killed and reaped on destruction / failure.
class NodeResumeEngine final : public engine::CoreWasmResumeEngine {
  public:
    ~NodeResumeEngine() override;

    NodeResumeEngine(const NodeResumeEngine &) = delete;
    NodeResumeEngine &operator=(const NodeResumeEngine &) = delete;
    NodeResumeEngine(NodeResumeEngine &&) noexcept;
    NodeResumeEngine &operator=(NodeResumeEngine &&) noexcept;

    [[nodiscard]] std::expected<void, engine::EngineError>
    fresh_instance(std::span<const std::uint8_t> module_bytes,
                   engine::ImportCallback import_callback) override;

    [[nodiscard]] std::expected<std::span<const std::uint8_t>, engine::EngineError>
    read_whole_memory() override;

    [[nodiscard]] std::expected<engine::GuestPointer, engine::EngineError>
    alloc_then_write(std::span<const std::uint8_t> bytes) override;

    [[nodiscard]] std::expected<engine::Run2Outcome, engine::EngineError>
    invoke_run2(engine::GuestPointer entry_ptr, std::uint32_t entry_len) override;

    // TEST-SUPPORT ONLY: the child pid. Lets the regression prove that a child
    // that dies mid-session surfaces as a typed engine error / clean teardown
    // instead of a SIGPIPE that would exit the host process 141. Not part of
    // the engine-port contract.
    [[nodiscard]] int child_pid_for_test() const noexcept;

  private:
    struct Impl;
    friend std::expected<NodeResumeEngine, NodeEngineLaunchError> launch_node_resume_engine(
        std::string node_executable, std::string host_script_path, std::string module_path);

    explicit NodeResumeEngine(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

// The host.mjs SSOT: the script takes the emitted module path as process.argv[2],
// compiles it at startup, and speaks the protocol documented above.
[[nodiscard]] std::string node_resume_host_script();

// Spawn `node <host_script_path> <module_path>` and wait for the startup ready
// frame. `host_script_path` must already contain node_resume_host_script().
//
// `module_path` is the byte authority: the child compiles that file, so
// fresh_instance accepts only a span byte-equal to the launch file's content and
// refuses anything else (a divergence would execute un-admitted bytes and bypass
// the caller's A2 admission).
[[nodiscard]] std::expected<NodeResumeEngine, NodeEngineLaunchError> launch_node_resume_engine(
    std::string node_executable, std::string host_script_path, std::string module_path);

} // namespace ahfl::runtime::core_wasm_node_resume_engine
