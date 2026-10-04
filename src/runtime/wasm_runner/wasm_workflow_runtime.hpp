#pragma once

// RFC 0026 KR6.8 WH-4 fix-forward D-A: the wasm3-backed workflow runtime
// FACADE. Compiles every workflow in an AHFL-IR program to wasm once in the
// constructor, then run(name, input) drives the wasm_host engine-session
// layer. This is the ONLY runtime-tier component that links a compiler
// backend (ahfl_compiler_backend_wasm); the wasm_host free functions stay
// backend-free.
//
// The wasm3-backed workflow runtime: constructor takes (const ir::Program& +
// config), run(name, input) -> WorkflowResult.

#include "runtime/wasm_host/wasm_runtime_hooks.hpp"

#include "runtime/engine/capability_bridge.hpp"
#include "runtime/engine/workflow_recovery.hpp"
#include "runtime/engine/workflow_result.hpp"
#include "runtime/value/value.hpp"

#include "ahfl/base/support/source.hpp"
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

// kr68 §12.16: the host-supplied resolution of a compile diagnostic's owning
// module to a displayable source location. The locator maps a module_name +
// SourceRange to a source file's display name and a line:col position. A
// single-file program resolves against its one SourceFile; a SourceGraph
// resolves module_name -> SourceUnit. A nullopt return means the module is
// unknown or the range is out of bounds (fail-closed: the renderer falls back
// to the module label with no line:col).
struct LocatedDiagnosticSource {
    std::string source_name;
    std::optional<ahfl::SourcePosition> position;
};

// Configuration for the wasm3-backed workflow runtime.
struct WasmWorkflowRuntimeConfig {
    // The shared hook set.
    wasm_host::WasmRuntimeHooks hooks;
    // The contextual capability invoker (production host capability dispatch).
    ContextualCapabilityInvoker invoker;
    // Resolves a capability's source_symbol to its canonical name.
    std::function<std::optional<std::string>(std::uint64_t)> name_resolver;
    // kr68 §12.16: resolves a compile diagnostic's owning module + source range
    // to a displayable source location. When unset, compile diagnostics render
    // with the module label only (no line:col).
    std::function<std::optional<LocatedDiagnosticSource>(std::string_view module_name,
                                                         ahfl::SourceRange range)>
        diagnostic_source_locator;

    // WH-4b: recovery snapshot for a resume run. When set, the session loads
    // the snapshot's memo + pending frontier and replays (memo-supply /
    // inject / live).
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
    // durable effect is dispatched. Memo hits and frontier injections
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

    // WH-8 (kr68 section 12.9.2): cancellation / interruption checks. The
    // wasm lane checks these at capability import boundaries
    // (wrapped_callback top, before memo classification) and at
    // run_workflow_session entry (pre-run, before admission). A workflow
    // with no capability imports has no mid-run cancellation point (its
    // guest computation is bounded by the grammar); the pre-run check is
    // the only observation. NOT in WasmRuntimeHooks: hooks are value-
    // observation channels, not control channels.
    std::function<bool()> cancellation_requested;
    std::function<bool()> interruption_requested;
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

    // WH-8 (kr68 section 12.9.14): look up the compiled descriptor for a
    // workflow. The DAP uses this to classify breakpoint liveness (the
    // eight-row liveness table, including the F6 shared-line row, needs the
    // descriptor's state walks, capability flags, and last_cap_walk_index).
    // Returns nullptr when the workflow is not in the compiled set (e.g.
    // compilation failed or the name is unknown).
    [[nodiscard]] const ahfl::backends::CoreWasmExecutionDescriptor *
    descriptor_for(const std::string &workflow_name) const {
        auto it = workflows_.find(workflow_name);
        if (it == workflows_.end()) {
            return nullptr;
        }
        return &it->second.descriptor;
    }

  private:
    struct CompiledWorkflow {
        std::vector<std::uint8_t> module_bytes;
        ahfl::backends::CoreWasmExecutionDescriptor descriptor;
    };

    // WH-6 (kr68 §12.16): record a compile-pipeline failure into a
    // DiagnosticBag. A stage-level wasm.compile-failed diagnostic names the
    // failing stage (and the workflow, for emission), then every pipeline
    // diagnostic is recorded as a first-class entry carrying its own code +
    // message + SourceRange + source_module (Principle 5). The umbrella entry
    // is deliberately unstamped (it names the stage, not a declaration).
    void record_compile_failure(DiagnosticBag &bag, std::string stage,
                                const std::vector<ahfl::ir::core::CoreLowerDiagnostic> &diags);
    void record_compile_failure(DiagnosticBag &bag, std::string stage,
                                const std::vector<ahfl::backends::CoreWasmDiagnostic> &diags);

    // kr68 §12.16: stamp a compile diagnostic's source location through the
    // host locator. A diagnostic with an owning module resolves (module +
    // range -> display name + line:col); a locator miss or an unset locator
    // falls back to the module label with no position. A whole-program
    // diagnostic (empty source_module) is left unstamped.
    void stamp_compile_diag_source(ahfl::DiagnosticBuilder &builder,
                                   std::string_view source_module,
                                   ahfl::ir::SourceRangeOpt range) const;

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
