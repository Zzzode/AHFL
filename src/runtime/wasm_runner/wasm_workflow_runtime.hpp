#pragma once

// RFC 0026 KR6.8 WH-4 fix-forward D-A: the wasm3-backed workflow runtime
// FACADE. Compiles every workflow in an AHFL-IR program to wasm once in the
// constructor, then run(name, input) drives the wasm_host engine-session
// layer. This is the ONLY runtime-tier component that links a compiler
// backend (ahfl_compiler_backend_wasm); the wasm_host free functions stay
// backend-free.
//
// The surface mirrors the evaluator-backed WorkflowRuntime
// (src/runtime/engine/workflow_runtime.hpp): same constructor shape
// (const ir::Program& + config), same run(name, input) -> WorkflowResult.
// A host that already drives the evaluator-backed runtime through the shared
// WasmRuntimeHooks can switch to this facade without changing its hook
// wiring.

#include "runtime/wasm_host/wasm_runtime_hooks.hpp"

#include "runtime/engine/capability_bridge.hpp"
#include "runtime/engine/workflow_recovery.hpp"
#include "runtime/engine/workflow_result.hpp"
#include "runtime/value/value.hpp"

#include "ahfl/compiler/ir/expr.hpp"
#include "ahfl/compiler/ir/program.hpp"
#include "compiler/backends/wasm/core_wasm_codegen.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ahfl::runtime::wasm_runner {

// Configuration for the wasm3-backed workflow runtime.
struct WasmWorkflowRuntimeConfig {
    // The shared hook set (identical signatures to WorkflowRuntimeConfig).
    wasm_host::WasmRuntimeHooks hooks;
    // The contextual capability invoker (production host capability dispatch).
    ContextualCapabilityInvoker invoker;
    // Resolves a capability's source_symbol to its canonical name.
    std::function<std::optional<std::string>(std::uint64_t)> name_resolver;

    // WH-4b: recovery snapshot for a resume run. When set, the session loads
    // the snapshot's memo + pending frontier and replays (memo-supply /
    // inject / live). Mirrors WorkflowRuntimeConfig::recovery_snapshot.
    std::optional<WorkflowRecoverySnapshot> recovery_snapshot;

    // WH-4b: the store to persist the recovery snapshot on suspend. Null
    // means no persistence (the snapshot rides back in the result; a save
    // failure downgrades Suspended to NodeFailed).
    const WorkflowRecoveryStore *recovery_store{nullptr};

    // WH-4b: the pending capability result for a resume run, as a native
    // Value. Mutually exclusive with resume_pending_result_wire_json.
    std::optional<Value> resume_pending_result;

    // WH-4b: the pending capability result for a resume run, as raw wire
    // JSON. Mutually exclusive with resume_pending_result.
    std::optional<std::string> resume_pending_result_wire_json;

    // WH-4b: write-ahead intent sink for durable_write / financial_write
    // effects. The facade wraps the user invoker with an intent-emitting
    // wrapper that fires this sink with the idempotency key right before a
    // durable effect is dispatched (mirrors the evaluator at
    // workflow_runtime.cpp:1050-1058). Memo hits and frontier injections
    // never reach the invoker, so a resumed run emits zero intents.
    std::function<void(std::uint64_t idempotency_key,
                       std::string_view capability_name)>
        durable_write_intent_sink;

    // WH-5c.5 test seam: forwarded to WorkflowSessionConfig after run2
    // returns, before the post-run per-node output join. A fail-closed
    // mutation pin corrupts the stash table / output bytes and asserts the
    // host decodes the corruption as kOutputDecodeFailed. Production code
    // never sets this.
    std::function<void(std::span<std::uint8_t>)> post_run2_memory_mutator;
};

// The wasm3-backed workflow runtime. Compiles every workflow in the program
// to wasm in the constructor; run(name, input) looks up the compiled module
// by workflow name and drives the wasm_host workflow session.
//
// If compilation fails (lowering / layout / emission), the constructor
// records the diagnostics and every run() call returns a failed
// WorkflowResult carrying the diagnostic bag (wasm.compile-failed plus
// the first-class stage diagnostics with their source ranges).
class WasmWorkflowRuntime {
  public:
    WasmWorkflowRuntime(const ir::Program &program,
                        WasmWorkflowRuntimeConfig config);

    // Execute the named workflow.
    [[nodiscard]] WorkflowResult run(const std::string &workflow_name,
                                     Value input);

  private:
    struct CompiledWorkflow {
        std::vector<std::uint8_t> module_bytes;
        ahfl::backends::CoreWasmExecutionDescriptor descriptor;
    };

    // Populated by the constructor. Empty when compilation failed.
    std::unordered_map<std::string, CompiledWorkflow> workflows_;
    // WH-4b: capability name -> effect kind, pre-computed from the IR program
    // for the intent-emitting wrapper (avoids storing a ProgramIndex, which
    // would require the program to outlive the facade).
    std::unordered_map<std::string, ir::CapabilityEffectKind>
        capability_effects_;
    // WH-5c.6: per-workflow SOURCE-ORDER node source ranges, indexed by the
    // descriptor's dense node_id. Pre-computed from the IR program for the
    // session's node_range_resolver (same lifetime pattern as
    // capability_effects_: avoids storing a ProgramIndex).
    std::unordered_map<std::string, std::vector<ir::SourceRangeOpt>>
        node_ranges_by_workflow_;
    // WH-5c.6: capability source_symbol (SymbolId u64) -> declaration
    // provenance source range, for the session's capability_range_resolver.
    std::unordered_map<std::uint64_t, ir::SourceRangeOpt> capability_ranges_;
    // WH-6 (kr68 §12.7.8): compilation diagnostics from the IR->Core->wasm
    // pipeline (lowering / layout / codegen), each carrying its own code +
    // message + SourceRange. Populated by the constructor when the pipeline
    // fails; every run() returns a failed WorkflowResult carrying this bag
    // (the CLI renders the SourceRanges, Principle 5). Replaces the old
    // flattened optional<string>.
    DiagnosticBag compile_errors_;
    WasmWorkflowRuntimeConfig config_;
};

} // namespace ahfl::runtime::wasm_runner
