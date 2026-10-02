// RFC 0026 KR6.8 WH-4: the wasm3-backed agent runner implementation.
//
// Drives an AGENT module end-to-end on the wasm3 engine with the D1 dual-mode
// discipline (see the header comment for the full contract):
//
//   1. A SEPARATE effects-free instance drives the step-walk
//      (run_agent_step_walk). state_entered_hook fires LIVE per step.
//   2. The CANONICAL instance runs runv (P6-frame) or run2 (WireJson) exactly
//      once. capability_invoked_hook / capability_result_observer fire LIVE at
//      its imports.
//
// WH-4 fix-forward D-C: BOTH instances reuse the WH-3 capability_import
// executor (make_capability_import_callback) -- the single production import
// path for the opaque (wire-JSON) AND bridge (P4-D control-block) lanes. The
// module is A2-admitted ONCE (make_verified_core_wasm_schema_module); the
// admitted module + descriptor frame_section + a source_symbol->canonical_name
// resolver feed both callbacks. The canonical instance's invoker fires hooks
// and collects observation data; the effects-free step instance's invoker is
// the scripted states_invoker (defaults to the canonical invoker for
// side-effect-free mock invokers -- the conformance case).

#include "runtime/wasm_host/wasm_agent_runner.hpp"

#include "runtime/engine/core_wasm_schema_module.hpp"
#include "runtime/engine/wire_value.hpp"
#include "runtime/wasm_host/capability_import.hpp"
#include "runtime/wasm_host/frame_packer.hpp"
#include "runtime/wasm_host/frame_reader.hpp"
#include "runtime/wasm_host/frame_walk.hpp"
#include "runtime/wasm_host/wasm_lifecycle.hpp"
#include "runtime/wasm_host/wasm_error_codes.hpp"

#include "ahfl/compiler/ir/core_wasm_abi_constants.hpp"
#include "ahfl/compiler/ir/core_wire_migration.hpp"
#include "runtime/value/value_json.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ahfl::runtime::wasm_host {

namespace {

namespace irc = ::ahfl::ir::core;
namespace eng = ::ahfl::runtime::core_wasm_resume_engine;
namespace bd = ::ahfl::backends;
namespace csm = ::ahfl::runtime::core_wasm_schema_module;

// Build the string regions for P6-frame output reading (mirrors the JS
// oracle's stringRegions construction in node_embedded_host.mjs).
[[nodiscard]] std::vector<StringRegion>
build_string_regions(const irc::CoreFrameLayoutSection &section) {
    std::vector<StringRegion> regions;
    if (section.rodata_extent > 0) {
        regions.push_back(
            {section.rodata_base, section.rodata_base + section.rodata_extent});
    }
    // The agent frame-payload arena (packed input String bytes). A computed
    // final that passes an input String through to the output (e.g.
    // v2b_string_passthrough) points the output PtrLen at THIS region, not at
    // rodata or a bridge result payload.
    if (section.payload_arena_capacity > 0) {
        regions.push_back({section.payload_arena_base,
                           section.payload_arena_base +
                               section.payload_arena_capacity});
    }
    // The workflow entry payload arena (host-packed String bytes for the
    // entry node's I frame). Zero on an agent section.
    if (section.entry_payload_capacity > 0) {
        regions.push_back({section.entry_payload_base,
                           section.entry_payload_base +
                               section.entry_payload_capacity});
    }
    for (const auto &site : section.bridge_call_sites) {
        if (site.result_payload_capacity > 0) {
            regions.push_back({site.result_payload_base,
                               site.result_payload_base +
                                   site.result_payload_capacity});
        }
    }
    return regions;
}

// Pack the P6-frame input into the engine's live mutable memory at the fixed
// P6 aggregate input base. Returns nullopt on success, or an error string.
[[nodiscard]] std::optional<std::string>
pack_p6_agent_input(Wasm3ResumeEngine &engine,
                    const bd::CoreWasmExecutionDescriptor &descriptor,
                    const Value &input) {
    if (!descriptor.frame_section.has_value() ||
        !descriptor.wire_schema.has_value()) {
        return "P6-frame agent missing frame_section or wire_schema";
    }

    auto verified =
        irc::make_verified_wire_schema_table(*descriptor.wire_schema);
    if (!verified.table.has_value()) {
        return "failed to verify wire schema for P6-frame agent input";
    }
    std::vector<irc::CoreLowerDiagnostic> diagnostics;
    auto binding = irc::make_frame_binding_from_verified_table(
        *verified.table, {irc::CoreWireFrameRootKind::Input}, diagnostics);
    if (!binding.has_value()) {
        return "failed to mint input binding for P6-frame agent";
    }

    auto page = engine.mutable_whole_memory();
    if (!page.has_value()) {
        return "failed to acquire mutable memory for P6-frame packing";
    }

    // Zero the reserved named regions so padding/union words are 0 (the same
    // discipline as the JS oracle and the agent_session).
    std::fill(page->begin() + irc::kP6AggregateInputBase,
              page->begin() + irc::kP6CollectionBackingBase,
              std::uint8_t{0});

    auto packed = pack_p6_input(*page, *descriptor.frame_section, *binding,
                                input);
    if (!packed.has_value()) {
        return "failed to pack P6-frame agent input";
    }
    return std::nullopt;
}

// Read the P6-frame output from the authorized base. For an "identity" final
// the output is the borrowed input base; for a "computed" final it is the
// materialized output base. Mirrors encodeP6Output in the JS oracle.
[[nodiscard]] std::optional<Value>
read_p6_agent_output(Wasm3ResumeEngine &engine,
                     const bd::CoreWasmExecutionDescriptor &descriptor,
                     std::uint32_t value_ptr) {
    if (!descriptor.frame.has_value() ||
        !descriptor.frame_section.has_value() ||
        !descriptor.wire_schema.has_value()) {
        return std::nullopt;
    }

    const auto &lane = *descriptor.frame;
    const auto authorized_base =
        lane.final_kind == "computed" ? lane.output_base : lane.input_base;
    if (value_ptr != authorized_base) {
        return std::nullopt;
    }

    auto mem = engine.read_whole_memory();
    if (!mem.has_value()) {
        return std::nullopt;
    }

    const auto &section = *descriptor.frame_section;
    const auto &wire = *descriptor.wire_schema;
    if (!wire.frame_roots.has_value()) {
        return std::nullopt;
    }

    const auto string_regions = build_string_regions(section);
    FrameWalkContext ctx(section, wire);
    auto output = read_value_at(ctx, *mem, wire.frame_roots->output,
                                section.output_layout, value_ptr,
                                string_regions);
    if (!output.has_value()) {
        return std::nullopt;
    }
    return std::move(*output);
}

} // namespace

std::expected<WasmAgentRunResult, std::string>
run_wasm_agent(std::span<const std::uint8_t> module_bytes,
               const bd::CoreWasmExecutionDescriptor &descriptor,
               const Value &input, WasmRuntimeHooks hooks,
               ContextualCapabilityInvoker invoker,
               ContextualCapabilityInvoker states_invoker) {
    if (descriptor.is_workflow) {
        return std::unexpected(
            "run_wasm_agent: descriptor is a workflow, not an agent");
    }

    const bool is_p6 =
        descriptor.frame_contract == bd::CoreWasmFrameContract::P6Frame;
    const bool has_imports = !descriptor.imports.empty();

    // === A2-admit the module ONCE (if capability-bearing) ===
    // The WH-3 capability_import executor requires the digest-authenticated
    // VerifiedCoreWasmSchemaModule (the exec-manifest + wire-schema admission
    // that every capability agent now carries -- WH-4 fix-forward D-C). The
    // admitted module MUST outlive both callbacks (the config holds a
    // reference to it), so it is declared here, outside the callback builds.
    csm::VerifiedCoreWasmSchemaModuleResult admitted;
    if (has_imports) {
        admitted = csm::make_verified_core_wasm_schema_module(module_bytes);
        if (!admitted.ok() || !admitted.module.has_value()) {
            return std::unexpected(
                "run_wasm_agent: wire-schema admission failed");
        }
    }

    // === Build the source_symbol -> canonical_name resolver ===
    // The agent descriptor has no workflow nodes, so the workflow_session's
    // node-based fallback does not apply. Join the A2 call sites
    // (source_symbol -> import_ordinal) with the descriptor's imports
    // (ordinal -> canonical_name).
    std::function<std::optional<std::string>(std::uint64_t)> name_resolver =
        [&admitted, &descriptor, has_imports](
            std::uint64_t source_symbol) -> std::optional<std::string> {
        if (!has_imports || !admitted.module.has_value()) {
            return std::nullopt;
        }
        for (std::size_t i = 0; i < admitted.module->call_site_count(); ++i) {
            auto cs = admitted.module->resolve(csm::ManifestCallSiteIndex{i});
            if (cs.ok() && cs.call_site.has_value() &&
                cs.call_site->source_symbol() == source_symbol) {
                const auto ord = cs.call_site->import_ordinal().value;
                if (ord < descriptor.imports.size()) {
                    return descriptor.imports[ord].canonical_name;
                }
            }
        }
        return std::nullopt;
    };

    // === Build the wrapped canonical invoker (fires hooks, collects data) ===
    // Capture hooks + invoker by VALUE (copy): the lambda is stored in a
    // ContextualCapabilityInvoker that the CapabilityImportConfig references,
    // and ASan's stack-use-after-scope detection flags by-reference capture
    // when the lambda is invoked through the import callback chain (the same
    // discipline as workflow_session).
    std::vector<std::string> collected_capabilities;
    std::vector<std::string> collected_cap_args;
    std::vector<CapabilityFailureKind> collected_failures;
    // P2-4: collect full capability call records (name + success + output)
    // so the lifecycle helper can emit CapabilityStarted /
    // CapabilityCompleted events on the agent lane. node_id is 0: the
    // agent lane wraps the bare agent in a synthetic single-node workflow.
    std::vector<WasmCapabilityCall> collected_cap_calls;

    ContextualCapabilityInvoker wrapped_invoker =
        [hooks, invoker, &collected_capabilities, &collected_cap_args,
         &collected_failures, &collected_cap_calls](
            const CapabilityInvocationContext &ctx,
            const std::string &name,
            const std::vector<Value> &args)
        -> CapabilityCallResult {
        if (hooks.capability_invoked_hook) {
            hooks.capability_invoked_hook(ctx.agent_id, name);
        }
        collected_capabilities.push_back(name);
        if (auto envelope = serialize_args_for_wire_json(args)) {
            collected_cap_args.push_back(std::move(*envelope));
        }
        auto result = invoker(ctx, name, args);
        if (hooks.capability_result_observer) {
            hooks.capability_result_observer(ctx, result);
        }
        if (result.failure_kind.has_value()) {
            collected_failures.push_back(*result.failure_kind);
        }
        // WH-5c.8: collect the FULL aggregate CapabilityCallResult (with the
        // workflow lane's Pending skip — a Pending call has no terminal yet,
        // so it is not projected; the node-level NodeSuspended terminal
        // records the pause). The result is deep-cloned
        // (clone_capability_call_result) so the collected copy is independent
        // of the result returned to the import callback.
        if (result.status != CapabilityCallStatus::Pending) {
            WasmCapabilityCall cap_call;
            cap_call.node_id = 0;
            cap_call.capability_name = name;
            cap_call.result = clone_capability_call_result(result);
            collected_cap_calls.push_back(std::move(cap_call));
        }
        return result;
    };

    // === Build the effective states invoker (scripted replay) ===
    // The effects-free step instance's invoker returns scripted results (never
    // a live side effect). Defaults to the canonical invoker for
    // side-effect-free mock invokers (the conformance case, where the harness
    // supplies the mock as both).
    ContextualCapabilityInvoker effective_states_invoker =
        states_invoker ? std::move(states_invoker) : invoker;

    // === 1. Step-walk on a SEPARATE effects-free instance ===
    // The import callback factory builds a WH-3 callback with the states
    // invoker; its results drive the state walk but its events are discarded
    // (the canonical instance fires the real hooks).
    AgentWalkDescriptor walk_desc;
    walk_desc.agent_name = descriptor.agent_name;
    walk_desc.states = descriptor.states;
    walk_desc.initial_state = descriptor.initial_state;
    walk_desc.is_p6_frame = is_p6;
    if (is_p6 && descriptor.frame_section.has_value() &&
        descriptor.wire_schema.has_value()) {
        walk_desc.frame_section = &*descriptor.frame_section;
        walk_desc.wire_schema = &*descriptor.wire_schema;
    }

    // These MUST outlive the step-walk callback: the CapabilityImportConfig
    // holds references to them, and the callback is invoked during
    // invoke_step (long after the factory lambda ends).
    CapabilityImportState walk_import_state;
    CapabilityInvocationContext walk_context;
    walk_context.agent_id = AgentId{0};
    irc::CoreFrameLayoutSection walk_default_section;

    auto walk_factory =
        [&admitted, &descriptor, &name_resolver, &effective_states_invoker,
         &walk_import_state, &walk_context, &walk_default_section,
         has_imports, is_p6](Wasm3ResumeEngine &engine) -> eng::ImportCallback {
        if (!has_imports || !admitted.module.has_value()) {
            return [](const eng::ImportObservation &)
                       -> eng::ImportCallbackResult {
                return eng::ImportAbort{};
            };
        }
        const irc::CoreFrameLayoutSection &frame_section =
            is_p6 && descriptor.frame_section.has_value()
                ? *descriptor.frame_section
                : walk_default_section;
        CapabilityImportConfig cap_config{
            .engine = engine,
            .module = *admitted.module,
            .frame_section = frame_section,
            .invoker = effective_states_invoker,
            .context = walk_context,
            .name_resolver = name_resolver,
            .state = walk_import_state,
        };
        return make_capability_import_callback(std::move(cap_config));
    };

    StateEnteredHook walk_hook;
    if (hooks.state_entered_hook) {
        walk_hook = [&hooks](AgentId id, std::string_view agent_name,
                             std::string_view node_name,
                             std::string_view state_name) {
            hooks.state_entered_hook(id, agent_name, node_name, state_name);
        };
    }

    // D-D: fire agent_input_hook LIVE before the step-walk (agent lane only).
    // Signature parity with WorkflowRuntimeConfig::agent_input_hook: the wasm
    // agent lane has no workflow node, so `node_name` is always empty (the
    // descriptor.agent_name string outlives the call, so the agent_name view
    // does not dangle).
    if (hooks.agent_input_hook) {
        hooks.agent_input_hook(AgentId{0}, descriptor.agent_name, "", input);
    }

    auto walk = run_agent_step_walk(module_bytes, walk_desc, input,
                                    std::move(walk_factory), walk_hook,
                                    AgentId{0}, "");
    if (!walk.has_value()) {
        return std::unexpected("run_wasm_agent: step-walk failed: " +
                               walk.error());
    }

    // === 2. Canonical run on a FRESH instance ===
    Wasm3ResumeEngine engine;

    // These MUST outlive the canonical callback (same discipline as above).
    CapabilityImportState canonical_import_state;
    CapabilityInvocationContext canonical_context;
    canonical_context.agent_id = AgentId{0};
    irc::CoreFrameLayoutSection canonical_default_section;

    eng::ImportCallback canonical_callback;
    if (!has_imports) {
        canonical_callback =
            [](const eng::ImportObservation &) -> eng::ImportCallbackResult {
                return eng::ImportAbort{};
            };
    } else {
        const irc::CoreFrameLayoutSection &frame_section =
            is_p6 && descriptor.frame_section.has_value()
                ? *descriptor.frame_section
                : canonical_default_section;
        CapabilityImportConfig cap_config{
            .engine = engine,
            .module = *admitted.module,
            .frame_section = frame_section,
            .invoker = wrapped_invoker,
            .context = canonical_context,
            .name_resolver = name_resolver,
            .state = canonical_import_state,
        };
        canonical_callback =
            make_capability_import_callback(std::move(cap_config));
    }

    // Instantiate the canonical module.
    auto inst = engine.fresh_instance(module_bytes,
                                      std::move(canonical_callback));
    if (!inst.has_value()) {
        return std::unexpected(
            "run_wasm_agent: fresh_instance failed for canonical run");
    }

    // Pack / write the input.
    eng::GuestPointer entry_ptr{};
    std::uint32_t entry_len = 0;

    if (is_p6) {
        if (auto err = pack_p6_agent_input(engine, descriptor, input);
            err.has_value()) {
            return std::unexpected("run_wasm_agent: " + *err);
        }
        // runv takes no arguments; the input is packed at the fixed P6 base.
    } else {
        // WireJson: serialize the input and write via the module's allocator.
        auto input_json = serialize_value_for_wire_json(input);
        if (!input_json.has_value()) {
            return std::unexpected(
                "run_wasm_agent: input is not wire-encodable");
        }

        // FB-1 construct-heap aliasing guard: a module with outlined-fn
        // aggregate constructs (construct_heap_enabled) initializes heap_next
        // at construct_heap_base and RESETS it there on every computed-handler
        // entry. On a fresh canonical instance the host's first alloc() would
        // land the input frame exactly at construct_heap_base, and the first
        // handler reset would overwrite the input JSON with construct scratch,
        // corrupting the identity-final output the run2 tuple points back at.
        // The Node embedded host avoids this by driving step() on the SAME
        // instance before writeInput(), advancing heap_next past the
        // construct-scratch high-water. The D1 dual-mode discipline walks on a
        // SEPARATE effects-free instance, so the canonical instance starts
        // fresh; replay the walk's probed heap_next advancement here.
        if (walk->heap_next_after_walk != 0) {
            auto current = engine.alloc_then_write({});
            if (current.has_value() &&
                walk->heap_next_after_walk > current->value) {
                const auto advance =
                    walk->heap_next_after_walk - current->value;
                std::vector<std::uint8_t> scratch(advance, 0);
                auto advanced = engine.alloc_then_write(scratch);
                if (!advanced.has_value()) {
                    return std::unexpected(
                        "run_wasm_agent: heap_next advancement failed");
                }
            }
        }

        const auto input_span = std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t *>(input_json->data()),
            input_json->size());
        auto ptr = engine.alloc_then_write(input_span);
        if (!ptr.has_value()) {
            return std::unexpected(
                "run_wasm_agent: alloc_then_write failed for input");
        }
        entry_ptr = *ptr;
        entry_len = static_cast<std::uint32_t>(input_json->size());
    }

    // Invoke the canonical entry point.
    // P1-4: trap / host-abort / non-OK map to a FAILED WorkflowResult with
    // diagnostic codes wasm.trap / wasm.host-abort / wasm.run-failed, NOT a
    // bare std::string error. std::unexpected is reserved for PRE-RUN setup
    // failures (admission, instantiation, pack, step-walk).
    std::optional<Value> output_value;
    RunTerminalStatus run_status = RunTerminalStatus::Completed;
    std::optional<WorkflowFailureKind> run_failure_kind;
    std::string run_failure_code;
    std::string run_failure_message;
    bool run_ok = false;

    if (is_p6) {
        auto outcome = engine.invoke_runv();
        if (!outcome.has_value()) {
            run_status = RunTerminalStatus::Failed;
            run_failure_kind = WorkflowFailureKind::NodeFailed;
            run_failure_code = wasm_diag::kTrap;
            run_failure_message =
                "run_wasm_agent: invoke_runv failed (engine error)";
        } else if (std::holds_alternative<eng::Run2HostAborted>(*outcome)) {
            run_status = RunTerminalStatus::Failed;
            run_failure_kind = WorkflowFailureKind::NodeFailed;
            run_failure_code = wasm_diag::kHostAbort;
            run_failure_message =
                "run_wasm_agent: runv host-aborted (capability import "
                "failure)";
            // P2-1: use the CapabilityImportError ENUM NAME, not the raw int.
            if (canonical_import_state.last_error.has_value()) {
                run_failure_message += " (CapabilityImportError=";
                run_failure_message += std::string(
                    to_string(*canonical_import_state.last_error));
                // WH-5c.1: surface the precise arg-decode sub-reason,
                // mirroring the workflow session path.
                if (*canonical_import_state.last_error ==
                        CapabilityImportError::ArgDecodeFailed &&
                    canonical_import_state.last_arg_decode_sub_reason
                        .has_value()) {
                    run_failure_message += ": ";
                    run_failure_message += std::string(to_string(
                        *canonical_import_state.last_arg_decode_sub_reason));
                }
                run_failure_message += ")";
            }
        } else if (std::holds_alternative<eng::Run2Trapped>(*outcome)) {
            run_status = RunTerminalStatus::Failed;
            run_failure_kind = WorkflowFailureKind::NodeFailed;
            run_failure_code = wasm_diag::kTrap;
            run_failure_message = "run_wasm_agent: runv trapped";
        } else {
            const auto &runv = std::get<RunvResult>(*outcome);
            run_ok = (runv.raw_status == 0);
            if (run_ok) {
                output_value =
                    read_p6_agent_output(engine, descriptor,
                                         runv.value_ptr.value);
            } else {
                run_status = RunTerminalStatus::Failed;
                run_failure_kind = WorkflowFailureKind::NodeFailed;
                run_failure_code = wasm_diag::kRunFailed;
                run_failure_message =
                    "run_wasm_agent: runv returned non-zero status " +
                    std::to_string(runv.raw_status);
            }
        }
    } else {
        auto outcome = engine.invoke_run2(entry_ptr, entry_len);
        if (!outcome.has_value()) {
            run_status = RunTerminalStatus::Failed;
            run_failure_kind = WorkflowFailureKind::NodeFailed;
            run_failure_code = wasm_diag::kTrap;
            run_failure_message =
                "run_wasm_agent: invoke_run2 failed (engine error)";
        } else if (std::holds_alternative<eng::Run2HostAborted>(*outcome)) {
            run_status = RunTerminalStatus::Failed;
            run_failure_kind = WorkflowFailureKind::NodeFailed;
            run_failure_code = wasm_diag::kHostAbort;
            run_failure_message =
                "run_wasm_agent: run2 host-aborted (capability import "
                "failure)";
            // P2-1: use the CapabilityImportError ENUM NAME, not the raw int.
            if (canonical_import_state.last_error.has_value()) {
                run_failure_message += " (CapabilityImportError=";
                run_failure_message += std::string(
                    to_string(*canonical_import_state.last_error));
                // WH-5c.1: surface the precise arg-decode sub-reason,
                // mirroring the workflow session path.
                if (*canonical_import_state.last_error ==
                        CapabilityImportError::ArgDecodeFailed &&
                    canonical_import_state.last_arg_decode_sub_reason
                        .has_value()) {
                    run_failure_message += ": ";
                    run_failure_message += std::string(to_string(
                        *canonical_import_state.last_arg_decode_sub_reason));
                }
                run_failure_message += ")";
            }
        } else if (std::holds_alternative<eng::Run2Trapped>(*outcome)) {
            run_status = RunTerminalStatus::Failed;
            run_failure_kind = WorkflowFailureKind::NodeFailed;
            run_failure_code = wasm_diag::kTrap;
            run_failure_message = "run_wasm_agent: run2 trapped";
            // WH-5c.1: surface the human-readable capability failure that
            // caused the trap (mirrors the workflow session path).
            if (canonical_import_state.last_capability_error.has_value()) {
                run_failure_message += " (";
                run_failure_message += *canonical_import_state.last_capability_error;
                run_failure_message += ")";
            }
        } else {
            const auto &tuple = std::get<eng::Run2ResultTuple>(*outcome);
            run_ok = (tuple.raw_status == 0);
            if (run_ok && tuple.output_ptr.value != 0 &&
                tuple.output_len > 0) {
                auto mem = engine.read_whole_memory();
                if (mem.has_value()) {
                    const std::string output_json(
                        reinterpret_cast<const char *>(
                            mem->data() + tuple.output_ptr.value),
                        tuple.output_len);
                    auto parsed = value_from_json(output_json);
                    if (parsed.has_value()) {
                        output_value = std::move(*parsed);
                    }
                }
            } else if (!run_ok) {
                // WH-4b: a Pending (raw_status==2) capability call on the
                // agent lane lands here and is classified as NodeFailed.
                // Agent-lane suspend is NOT a CLI contract (the CLI runs
                // workflows; --recovery-store drives WorkflowRuntime), the
                // agent runner has no recovery-snapshot consumer, and the
                // conformance census has zero pending mocks (no alignment
                // pressure). Agent-lane suspend belongs to WH-7/WH-8 (the
                // REPL/DAP agent-runner contract); see the design doc
                // §12.6.10.
                run_status = RunTerminalStatus::Failed;
                run_failure_kind = WorkflowFailureKind::NodeFailed;
                run_failure_code = wasm_diag::kRunFailed;
                run_failure_message =
                    "run_wasm_agent: run2 returned non-zero status " +
                    std::to_string(tuple.raw_status);
            }
        }
    }

    // Read transition_count from the canonical instance.
    std::uint32_t transition_count = 0;
    if (auto tc = engine.read_exported_global_u32("transition_count")) {
        transition_count = *tc;
    }

    // P1-2: build the result via the shared lifecycle helper (metadata +
    // lifecycle events + report from events, the SAME projection the
    // evaluator uses). The agent lane wraps the bare agent in a synthetic
    // single-node workflow.
    WasmAgentRunResult result;
    const bool report_ok = finalize_wasm_agent_run(
        result.result, descriptor, walk->states,
        std::move(collected_cap_calls), std::move(output_value),
        run_status, run_failure_kind, std::move(run_failure_code),
        std::move(run_failure_message));
    if (!report_ok) {
        // build_report already emitted wasm.event-stream-invalid and left
        // the report fail-closed (status=Failed,
        // failure_kind=EvaluationFailed). The event stream is constructed
        // by finalize_wasm_agent_run (correct by construction); a false
        // return means an internal invariant was violated. The fail-closed
        // report is the correct response.
        result.result.report.status = RunTerminalStatus::Failed;
    }

    // Collect the state entries from the step-walk.
    for (const auto &state_name : walk->states) {
        result.states.push_back({descriptor.agent_name, state_name});
    }
    result.transition_count = transition_count;
    result.capabilities = std::move(collected_capabilities);
    result.capability_arguments = std::move(collected_cap_args);
    result.capability_failures = std::move(collected_failures);

    return result;
}

} // namespace ahfl::runtime::wasm_host
