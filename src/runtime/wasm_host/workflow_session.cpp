// RFC 0026 KR6.8 WH-4: the workflow session implementation.
//
// Runs a WORKFLOW module end-to-end on the wasm3 engine, wrapping the WH-3
// capability_import executor with the D1 hook-firing discipline (see the
// header comment for the full contract).
//
// WH-4 fix-forward P1-2/P1-3: the session populates the
// ExecutionMetadataStore, emits the lifecycle event stream, and builds the
// ExecutionReport from those events via the shared wasm_lifecycle helper --
// the SAME projection the evaluator-backed WorkflowRuntime uses. No
// hand-populated report path survives. P2-2: per-import runner resolution
// via source_symbol (not name-keyed first-wins). P2-3: node-event record
// cross-checks against the descriptor. P2-4: workflow_completed_count
// validation against descriptor.workflow_node_count.

#include "runtime/wasm_host/workflow_session.hpp"
#include "runtime/wasm_host/wasm_lifecycle.hpp"
#include "runtime/wasm_host/wasm_error_codes.hpp"

#include "runtime/engine/core_wasm_node_events.hpp"
#include "runtime/engine/core_wasm_schema_module.hpp"
#include "runtime/engine/wire_value.hpp"
#include "runtime/wasm_host/capability_import.hpp"
#include "runtime/wasm_host/frame_packer.hpp"
#include "runtime/wasm_host/frame_reader.hpp"
#include "runtime/wasm_host/frame_walk.hpp"

#include "ahfl/compiler/ir/core_frame_layout.hpp"
#include "ahfl/compiler/ir/core_wasm_abi_constants.hpp"
#include "ahfl/compiler/ir/core_wire_schema.hpp"
#include "ahfl/runtime/execution_event.hpp"
#include "runtime/value/value_json.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ahfl::runtime::wasm_host {

namespace {

namespace ir = ahfl::ir;
namespace irc = ahfl::ir::core;
namespace eng = ahfl::runtime::core_wasm_resume_engine;
namespace ne = ahfl::runtime::core_wasm_node_events;
namespace csm = ahfl::runtime::core_wasm_schema_module;

// Build the string regions for P6-frame output reading (mirrors the JS
// oracle's stringRegions construction in node_embedded_host.mjs).
[[nodiscard]] std::vector<StringRegion>
build_string_regions(const irc::CoreFrameLayoutSection &section) {
    std::vector<StringRegion> regions;
    if (section.rodata_extent > 0) {
        regions.push_back(
            {section.rodata_base, section.rodata_base + section.rodata_extent});
    }
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

// Fire state_entered_hook for a batch of new trace records. Also populates
// states_per_node for lifecycle event emission. Fail-closed: a record whose
// runner or state is out of range is evidence of a corrupt module (D-B: no
// silent OOR skip); returns an error string instead.
[[nodiscard]] std::optional<std::string> fire_state_entries(
    const std::vector<StateTraceRecord> &records, std::size_t from,
    const ahfl::backends::CoreWasmExecutionDescriptor &descriptor,
    const WorkflowSessionConfig &config,
    std::vector<StateEntry> &collected_states,
    std::vector<std::vector<WasmNodeStateEntry>> &states_per_node,
    const std::unordered_map<std::uint32_t, std::size_t> &runner_to_schedule) {
    for (std::size_t i = from; i < records.size(); ++i) {
        const auto &rec = records[i];
        if (rec.runner >= descriptor.agents.size()) {
            return "state-trace record runner index out of range";
        }
        const auto &agent = descriptor.agents[rec.runner];
        if (rec.state >= agent.all_states.size()) {
            return "state-trace record state id out of range";
        }
        const std::string &agent_name = agent.agent;
        const std::string &state_name = agent.all_states[rec.state];
        if (config.state_entered_hook) {
            config.state_entered_hook(AgentId{rec.runner}, agent_name, "",
                                      state_name);
        }
        collected_states.push_back({agent_name, state_name});
        // P1-2: also collect per-node for lifecycle event emission.
        auto it = runner_to_schedule.find(rec.runner);
        if (it != runner_to_schedule.end()) {
            states_per_node[it->second].push_back({rec.runner, state_name});
        }
    }
    return std::nullopt;
}

// Reconstruct the state_sequence for a WireJson workflow from decoded
// node-event records joined to the descriptor's agent walks (schedule order).
// Identity workflows (no capability imports, no event buffer) reconstruct from
// the descriptor nodes directly. Fail-closed on any out-of-range runner or
// unknown node (D-B: mirrors the JS oracle's recordStates, which fails rather
// than skips). P2-3: cross-checks event capability/source_symbol against the
// descriptor node. Also populates states_per_node for lifecycle events.
[[nodiscard]] std::optional<std::string> reconstruct_wirejson_states(
    std::span<const std::uint8_t> linear_memory,
    const ahfl::backends::CoreWasmExecutionDescriptor &descriptor,
    const WorkflowSessionConfig &config,
    std::vector<StateEntry> &collected_states,
    std::vector<std::vector<WasmNodeStateEntry>> &states_per_node) {
    const auto &nodes = descriptor.nodes;
    const auto &agents = descriptor.agents;

    // Identity workflow: no event buffer. The descriptor nodes (in Kahn
    // schedule order) ARE the schedule; join each to its runner's walk.
    if (descriptor.imports.empty()) {
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            const auto &node = nodes[i];
            if (node.runner >= agents.size()) {
                return "identity workflow node runner index out of range";
            }
            const auto &agent = agents[node.runner];
            for (const auto &state_name : agent.walk) {
                if (config.state_entered_hook) {
                    config.state_entered_hook(
                        AgentId{node.runner}, agent.agent, node.name,
                        state_name);
                }
                collected_states.push_back({agent.agent, state_name});
                states_per_node[i].push_back({node.runner, state_name});
            }
        }
        return std::nullopt;
    }

    // Capability workflow: decode node-event records from the event buffer.
    auto events =
        ne::decode_node_events(linear_memory, descriptor.workflow_node_count);
    if (!events.has_value()) {
        return "node-event decode failed";
    }
    for (const auto &event : *events) {
        // Join to the descriptor node by workflow_node_id.
        const ahfl::backends::CoreWasmNodeDescriptor *node_desc = nullptr;
        std::size_t node_schedule = 0;
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            if (nodes[i].node_id == event.workflow_node_id.value) {
                node_desc = &nodes[i];
                node_schedule = i;
                break;
            }
        }
        if (node_desc == nullptr) {
            return "node-event record names unknown node";
        }
        if (node_desc->runner >= agents.size()) {
            return "node-event record runner index out of range";
        }

        // P2-3: cross-check event capability kind + source_symbol against
        // the descriptor node. A mismatch is evidence of a corrupt module.
        const bool event_is_capability =
            event.kind == core_wasm_resume::NodeKind::Capability;
        if (event_is_capability != node_desc->has_capability) {
            return "node-event record capability kind disagrees with descriptor";
        }
        if (node_desc->has_capability) {
            if (event.source_symbol != node_desc->source_symbol) {
                return "node-event record source_symbol disagrees with descriptor";
            }
        } else {
            if (event.source_symbol != 0) {
                return "identity node-event record has nonzero source_symbol";
            }
        }

        const auto &agent = agents[node_desc->runner];
        for (const auto &state_name : agent.walk) {
            if (config.state_entered_hook) {
                config.state_entered_hook(
                    AgentId{node_desc->runner}, agent.agent, node_desc->name,
                    state_name);
            }
            collected_states.push_back({agent.agent, state_name});
            states_per_node[node_schedule].push_back(
                {node_desc->runner, state_name});
        }
    }
    return std::nullopt;
}

} // namespace

std::expected<WorkflowSessionResult, std::string>
run_workflow_session(std::span<const std::uint8_t> module_bytes,
                     const ahfl::backends::CoreWasmExecutionDescriptor &descriptor,
                     const Value &input, WorkflowSessionConfig config) {
    if (!descriptor.is_workflow) {
        return std::unexpected("run_workflow_session: descriptor is not a workflow");
    }

    const bool is_p6 =
        descriptor.frame_contract == ahfl::backends::CoreWasmFrameContract::P6Frame;
    const bool has_trace =
        is_p6 && descriptor.frame_section.has_value() &&
        descriptor.frame_section->state_trace_capacity > 0;

    // --- 1. Create the engine ---
    Wasm3ResumeEngine engine;

    // --- 2. Collect observation data ---
    std::vector<StateEntry> collected_states;
    std::vector<std::string> collected_capabilities;
    std::vector<std::string> collected_cap_args;
    std::vector<CapabilityFailureKind> collected_failures;
    std::size_t last_trace_count = 0;
    // Set when the P6 trace-ring prefix decode at an import boundary hits an
    // out-of-range record (D-B fail-closed). Checked post-run.
    std::optional<std::string> trace_error;

    // P1-2: per-node state collection for lifecycle event emission.
    std::vector<std::vector<WasmNodeStateEntry>> states_per_node(
        descriptor.nodes.size());

    // P6-frame: map runner -> schedule index (each runner maps to exactly one
    // node; codegen rejects runner reuse for P6-frame workflows).
    std::unordered_map<std::uint32_t, std::size_t> runner_to_schedule;
    for (std::size_t i = 0; i < descriptor.nodes.size(); ++i) {
        runner_to_schedule[descriptor.nodes[i].runner] = i;
    }

    // P2-2: per-import runner resolution via source_symbol. Each capability
    // node carries a unique source_symbol; the WH-3 executor sets it in the
    // invocation context per-call, so the session resolves the owning
    // agent/node per-import (not name-keyed first-wins).
    std::unordered_map<std::uint64_t, std::uint32_t> symbol_to_runner;
    // P1-2: capability name -> descriptor node_id (for lifecycle events).
    std::unordered_map<std::string, std::uint32_t> cap_name_to_node;
    for (const auto &node : descriptor.nodes) {
        if (node.has_capability) {
            symbol_to_runner[node.source_symbol] = node.runner;
            if (node.capability_ordinal < descriptor.imports.size()) {
                cap_name_to_node[descriptor.imports[node.capability_ordinal]
                                     .canonical_name] = node.node_id;
            }
        }
    }

    // P1-2: collected capability calls for lifecycle events.
    std::vector<WasmCapabilityCall> collected_cap_calls;

    // --- 3. Build the wrapped invoker (fires hooks, collects data) ---
    // Capture config by VALUE (copy): the lambda is stored in a
    // ContextualCapabilityInvoker that the CapabilityImportConfig references,
    // and ASan's stack-use-after-scope detection flags the by-reference
    // capture when the lambda is invoked through the import callback chain.
    // WorkflowSessionConfig is copyable (all std::function members).
    ContextualCapabilityInvoker wrapped_invoker =
        [config, &collected_capabilities, &collected_cap_args,
         &collected_failures, &symbol_to_runner, &collected_cap_calls,
         &cap_name_to_node](
            const CapabilityInvocationContext &ctx,
            const std::string &name,
            const std::vector<Value> &args)
        -> CapabilityCallResult {
        // P2-2: resolve the REAL per-import agent_id from the capability's
        // source_symbol (set by the WH-3 executor per-call). Falls back to
        // the context's agent_id when the symbol is not in the map (e.g.,
        // a capability not called from a workflow node).
        CapabilityInvocationContext real_ctx = ctx;
        if (ctx.source_capability_symbol_id.has_value()) {
            auto it = symbol_to_runner.find(*ctx.source_capability_symbol_id);
            if (it != symbol_to_runner.end()) {
                real_ctx.agent_id = AgentId{it->second};
            }
        }
        collected_capabilities.push_back(name);
        if (config.capability_invoked_hook) {
            config.capability_invoked_hook(real_ctx.agent_id, name);
        }
        if (auto envelope = serialize_args_for_wire_json(args)) {
            collected_cap_args.push_back(std::move(*envelope));
        }
        auto result = config.invoker(real_ctx, name, args);
        if (config.capability_result_observer) {
            config.capability_result_observer(real_ctx, result);
        }
        if (result.failure_kind.has_value()) {
            collected_failures.push_back(*result.failure_kind);
        }
        // P1-2: collect for lifecycle events (CapabilityStarted /
        // CapabilityCompleted). Resolve the descriptor node_id from the
        // capability name.
        WasmCapabilityCall cap_call;
        cap_call.capability_name = name;
        cap_call.success = result.status == CapabilityCallStatus::Success;
        if (cap_call.success && result.value.has_value()) {
            cap_call.output = clone_value(*result.value);
        }
        cap_call.attempts = result.attempts;
        cap_call.cache_hit = result.cache_hit;
        cap_call.usage = result.usage;
        auto node_it = cap_name_to_node.find(name);
        cap_call.node_id = node_it != cap_name_to_node.end()
                               ? node_it->second
                               : std::uint32_t{0};
        collected_cap_calls.push_back(std::move(cap_call));
        return result;
    };

    // --- 4. Build the import callback ---
    // A workflow with no capability imports never calls ahfl_cap, so the
    // callback is a fail-closed abort (the same discipline as the
    // state_trace_decoder KAT). A workflow WITH imports uses the WH-3
    // capability_import executor, which requires the digest-authenticated
    // VerifiedCoreWasmSchemaModule (the exec-manifest + wire-schema admission
    // that only capability workflows carry). The admitted module MUST outlive
    // the callback (the config holds a reference to it), so it is declared
    // here, outside the else block.
    eng::ImportCallback wrapped_callback;
    CapabilityImportState import_state;
    csm::VerifiedCoreWasmSchemaModuleResult admitted;
    // These MUST outlive the callback: the CapabilityImportConfig holds
    // references to them, and the config is moved into the
    // make_capability_import_callback lambda that is called during
    // invoke_run2 (long after the else block below ends). Declaring them
    // inside the else block would dangle the references.
    irc::CoreFrameLayoutSection default_section;
    CapabilityInvocationContext invocation_context;
    invocation_context.agent_id = AgentId{0};

    if (descriptor.imports.empty()) {
        wrapped_callback =
            [](const eng::ImportObservation &) -> eng::ImportCallbackResult {
                return eng::ImportAbort{};
            };
    } else {
        admitted = csm::make_verified_core_wasm_schema_module(module_bytes);
        if (!admitted.ok() || !admitted.module.has_value()) {
            return std::unexpected(
                "run_workflow_session: wire-schema admission failed");
        }

        // A default frame section for WireJson workflows (the opaque lane
        // does not use it, but the CapabilityImportConfig requires a
        // reference).
        const irc::CoreFrameLayoutSection &frame_section =
            is_p6 && descriptor.frame_section.has_value()
                ? *descriptor.frame_section
                : default_section;

        // Build a fallback name_resolver from the descriptor if the caller
        // did not supply one.
        std::function<std::optional<std::string>(std::uint64_t)>
            fallback_resolver;
        const std::function<std::optional<std::string>(std::uint64_t)>
            *resolver = &config.name_resolver;
        if (!resolver->operator bool()) {
            fallback_resolver = [&descriptor](std::uint64_t source_symbol)
                -> std::optional<std::string> {
                for (const auto &node : descriptor.nodes) {
                    if (node.has_capability &&
                        node.source_symbol == source_symbol &&
                        node.capability_ordinal <
                            descriptor.imports.size()) {
                        return descriptor
                            .imports[node.capability_ordinal]
                            .canonical_name;
                    }
                }
                return std::nullopt;
            };
            resolver = &fallback_resolver;
        }

        CapabilityImportConfig cap_config{
            .engine = engine,
            .module = *admitted.module,
            .frame_section = frame_section,
            .invoker = wrapped_invoker,
            .context = invocation_context,
            .name_resolver = *resolver,
            .state = import_state,
        };
        auto inner_callback =
            make_capability_import_callback(std::move(cap_config));

        // --- 5. Wrap the import callback to fire state_entered_hook at
        //        import boundaries (P6-frame trace ring only) ---
        // Capture config by VALUE (copy) for the same ASan stack-use-after-
        // scope reason as the wrapped_invoker above.
        wrapped_callback =
            [config, &descriptor, &last_trace_count, &collected_states,
             &states_per_node, &runner_to_schedule, &trace_error, has_trace,
             inner = std::move(inner_callback)](
                const eng::ImportObservation &obs)
            -> eng::ImportCallbackResult {
                if (has_trace) {
                    const auto &section = *descriptor.frame_section;
                    auto decoded = decode_state_trace(
                        obs.whole_memory, section.state_trace_base,
                        section.state_trace_capacity);
                    if (decoded.has_value()) {
                        auto err = fire_state_entries(
                            *decoded, last_trace_count, descriptor, config,
                            collected_states, states_per_node,
                            runner_to_schedule);
                        if (err.has_value()) {
                            trace_error = std::move(*err);
                        } else {
                            last_trace_count = decoded->size();
                        }
                    }
                }
                return inner(obs);
            };
    }

    // --- 6. Instantiate the module ---
    auto inst = engine.fresh_instance(module_bytes, std::move(wrapped_callback));
    if (!inst.has_value()) {
        return std::unexpected(
            "run_workflow_session: fresh_instance failed");
    }

    // --- 7. Pack / write the input ---
    eng::GuestPointer entry_ptr{};
    std::uint32_t entry_len = 0;

    if (is_p6) {
        if (!descriptor.frame_section.has_value() ||
            !descriptor.wire_schema.has_value()) {
            return std::unexpected(
                "run_workflow_session: P6-frame workflow missing frame_section "
                "or wire_schema");
        }
        const auto &section = *descriptor.frame_section;
        const auto &wire = *descriptor.wire_schema;
        if (section.node_blocks.empty()) {
            return std::unexpected(
                "run_workflow_session: P6-frame workflow has no node blocks");
        }
        if (!wire.frame_roots.has_value() ||
            wire.frame_roots->node_inputs.empty()) {
            return std::unexpected(
                "run_workflow_session: P6-frame workflow missing node_inputs");
        }

        const auto &block = section.node_blocks[0];
        const auto w_id = wire.frame_roots->node_inputs[0];
        const auto l_id = block.input_layout;

        auto page = engine.mutable_whole_memory();
        if (!page.has_value()) {
            return std::unexpected(
                "run_workflow_session: mutable_whole_memory failed");
        }

        // Zero the reserved named regions so padding/union words are 0 (the
        // same discipline as the JS oracle and the agent_session).
        std::fill(page->begin() + irc::kP6AggregateInputBase,
                  page->begin() + irc::kP6CollectionBackingBase,
                  std::uint8_t{0});

        FrameWalkContext ctx(section, wire);
        std::uint32_t arena_cursor = section.entry_payload_base;
        auto packed = pack_value_at(ctx, *page, w_id, l_id, input,
                                    block.input_base, arena_cursor,
                                    section.entry_payload_base,
                                    section.entry_payload_capacity);
        if (!packed.has_value()) {
            return std::unexpected(
                "run_workflow_session: pack_value_at failed");
        }

        entry_ptr = eng::GuestPointer{block.input_base};
        entry_len = block.input_size;
    } else {
        // WireJson: serialize the input to canonical wire JSON and write it
        // via the module's own exported allocator.
        auto input_json = serialize_value_for_wire_json(input);
        if (!input_json.has_value()) {
            return std::unexpected(
                "run_workflow_session: input is not wire-encodable");
        }
        const auto input_span = std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t *>(input_json->data()),
            input_json->size());
        auto ptr = engine.alloc_then_write(input_span);
        if (!ptr.has_value()) {
            return std::unexpected(
                "run_workflow_session: alloc_then_write failed");
        }
        entry_ptr = *ptr;
        entry_len = static_cast<std::uint32_t>(input_json->size());
    }

    // --- 8. Invoke run2 ---
    // D-D: trap / host-abort / engine error map to a FAILED WorkflowResult
    // with diagnostic codes wasm.trap / wasm.host-abort / wasm.run-failed,
    // NOT a bare std::string error. std::unexpected is reserved for PRE-RUN
    // setup failures (admission, instantiation, pack).
    auto outcome = engine.invoke_run2(entry_ptr, entry_len);

    RunTerminalStatus run_status = RunTerminalStatus::Completed;
    std::optional<WorkflowFailureKind> run_failure_kind;
    std::string run_failure_code;
    std::string run_failure_message;
    bool run_ok = false;
    const eng::Run2ResultTuple *tuple = nullptr;

    if (!outcome.has_value()) {
        run_status = RunTerminalStatus::Failed;
        run_failure_kind = WorkflowFailureKind::NodeFailed;
        run_failure_code = wasm_diag::kTrap;
        run_failure_message =
            "run_workflow_session: invoke_run2 failed (engine error)";
    } else if (std::holds_alternative<eng::Run2HostAborted>(*outcome)) {
        run_status = RunTerminalStatus::Failed;
        run_failure_kind = WorkflowFailureKind::NodeFailed;
        run_failure_code = wasm_diag::kHostAbort;
        run_failure_message =
            "run_workflow_session: run2 host-aborted (capability import "
            "failure)";
        // P2-1: use the CapabilityImportError ENUM NAME, not the raw integer.
        if (import_state.last_error.has_value()) {
            run_failure_message +=
                " (CapabilityImportError=";
            run_failure_message +=
                std::string(to_string(*import_state.last_error));
            run_failure_message += ")";
        }
    } else if (std::holds_alternative<eng::Run2Trapped>(*outcome)) {
        run_status = RunTerminalStatus::Failed;
        run_failure_kind = WorkflowFailureKind::NodeFailed;
        run_failure_code = wasm_diag::kTrap;
        run_failure_message = "run_workflow_session: run2 trapped";
    } else {
        tuple = &std::get<eng::Run2ResultTuple>(*outcome);
        run_ok = (tuple->raw_status == 0);
        if (!run_ok) {
            run_status = RunTerminalStatus::Failed;
            run_failure_kind = WorkflowFailureKind::NodeFailed;
            run_failure_code = wasm_diag::kRunFailed;
            run_failure_message =
                "run_workflow_session: run2 returned non-zero status " +
                std::to_string(tuple->raw_status);
        }
    }

    // --- 9. Post-run: decode the FULL trace ring and fire remaining
    //        state_entered_hook (P6-frame) ---
    if (has_trace) {
        if (trace_error.has_value()) {
            // D-B fail-closed: a trace-ring OOR record is evidence of a
            // corrupt module. On the success path this is a hard error; on
            // the failure path the run already failed, so the collection
            // error is secondary (proceed with whatever was collected).
            if (run_ok) {
                return std::unexpected(
                    "run_workflow_session: " + std::move(*trace_error));
            }
        } else {
            const auto &section = *descriptor.frame_section;
            auto mem = engine.read_whole_memory();
            if (mem.has_value()) {
                auto decoded = decode_state_trace(
                    *mem, section.state_trace_base,
                    section.state_trace_capacity);
                if (decoded.has_value() && decoded->size() > last_trace_count) {
                    auto err = fire_state_entries(
                        *decoded, last_trace_count, descriptor, config,
                        collected_states, states_per_node,
                        runner_to_schedule);
                    if (err.has_value() && run_ok) {
                        return std::unexpected(
                            "run_workflow_session: " + std::move(*err));
                    }
                    if (!err.has_value()) {
                        last_trace_count = decoded->size();
                    }
                }
            }
        }
    }

    // --- 9b. Post-run: reconstruct WireJson state_sequence from node-event
    //         records joined to agent walks (D-B) ---
    if (!is_p6) {
        auto mem = engine.read_whole_memory();
        if (mem.has_value()) {
            auto err = reconstruct_wirejson_states(
                *mem, descriptor, config, collected_states, states_per_node);
            if (err.has_value() && run_ok) {
                return std::unexpected(
                    "run_workflow_session: " + std::move(*err));
            }
            if (err.has_value()) {
                // On the failure path, a reconstruction error is secondary.
                // Proceed with whatever was collected (might be empty).
            }
        }
    }

    // --- 10. Post-run: decode node events, fire node_completed_hook, and
    //         decode P6-frame node outputs (reuse for lifecycle events) ---
    std::vector<std::optional<Value>> node_outputs(descriptor.nodes.size());
    std::vector<bool> node_completed(descriptor.nodes.size(), false);

    if (config.node_completed_hook &&
        descriptor.workflow_node_count > 0) {
        auto mem = engine.read_whole_memory();
        if (mem.has_value()) {
            auto events = ne::decode_node_events(*mem,
                                                 descriptor.workflow_node_count);
            if (events.has_value()) {
                const auto string_regions =
                    is_p6 && descriptor.frame_section.has_value()
                        ? build_string_regions(*descriptor.frame_section)
                        : std::vector<StringRegion>{};

                for (const auto &event : *events) {
                    // Find the node descriptor by schedule_pos.
                    const ahfl::backends::CoreWasmNodeDescriptor *node_desc =
                        nullptr;
                    std::size_t node_schedule = 0;
                    for (std::size_t i = 0; i < descriptor.nodes.size(); ++i) {
                        if (descriptor.nodes[i].schedule_pos ==
                            event.schedule_pos.value) {
                            node_desc = &descriptor.nodes[i];
                            node_schedule = i;
                            break;
                        }
                    }
                    if (node_desc == nullptr) {
                        if (run_ok) {
                            return std::unexpected(
                                "run_workflow_session: node-event record "
                                "references unknown schedule_pos");
                        }
                        continue;
                    }

                    node_completed[node_schedule] = true;

                    const auto runner = node_desc->runner;
                    if (runner >= descriptor.agents.size()) {
                        if (run_ok) {
                            return std::unexpected(
                                "run_workflow_session: node-event record "
                                "runner index out of range");
                        }
                        continue;
                    }

                    // P6-frame: read the node output from its runner's O_k
                    // block. WireJson: individual node outputs are not
                    // host-observable (the module's compiled code passes them
                    // through its own heap), so fire with Unit.
                    Value node_output{NoneValue{}};
                    if (is_p6 && descriptor.frame_section.has_value() &&
                        descriptor.wire_schema.has_value()) {
                        const auto &section = *descriptor.frame_section;
                        const auto &wire = *descriptor.wire_schema;
                        if (runner < section.node_blocks.size() &&
                            wire.frame_roots.has_value() &&
                            runner <
                                wire.frame_roots->node_outputs.size()) {
                            const auto &block = section.node_blocks[runner];
                            const auto w_id =
                                wire.frame_roots->node_outputs[runner];
                            const auto l_id = block.output_layout;
                            FrameWalkContext ctx(section, wire);
                            auto output = read_value_at(
                                ctx, *mem, w_id, l_id, block.output_base,
                                string_regions);
                            if (output.has_value()) {
                                node_output = std::move(*output);
                            }
                        }
                    }
                    // P1-3: store the decoded output for lifecycle events
                    // (reuse, don't decode twice).
                    node_outputs[node_schedule] = std::move(node_output);
                    Value none_value{NoneValue{}};
                    const Value &hook_output = node_outputs[node_schedule]
                                                   ? *node_outputs[node_schedule]
                                                   : none_value;
                    config.node_completed_hook(
                        AgentId{runner}, node_desc->name, hook_output);
                }
            }
        }
    }

    // --- 11. Read the workflow output ---
    // P2-2: fail CLOSED on a successful run whose output bytes fail to
    // decode. A module that reports success but writes unparseable output
    // is corrupt; never return Completed with a null output.
    std::optional<Value> workflow_output;
    if (run_ok) {
        auto mem = engine.read_whole_memory();
        if (mem.has_value()) {
            if (is_p6 && descriptor.frame_section.has_value() &&
                descriptor.wire_schema.has_value()) {
                const auto &section = *descriptor.frame_section;
                const auto &wire = *descriptor.wire_schema;
                if (wire.frame_roots.has_value()) {
                    const auto string_regions =
                        build_string_regions(section);
                    FrameWalkContext ctx(section, wire);
                    auto output = read_value_at(
                        ctx, *mem, wire.frame_roots->output,
                        section.output_layout, section.workflow_output_base,
                        string_regions);
                    if (output.has_value()) {
                        workflow_output = std::move(*output);
                    } else {
                        run_ok = false;
                        run_status = RunTerminalStatus::Failed;
                        run_failure_kind = WorkflowFailureKind::EvaluationFailed;
                        run_failure_code =
                            std::string{wasm_diag::kOutputDecodeFailed};
                        run_failure_message =
                            "run_workflow_session: P6-frame workflow output "
                            "failed to decode";
                    }
                }
            } else if (tuple != nullptr &&
                       tuple->output_ptr.value != 0 && tuple->output_len > 0) {
                // WireJson: the output is raw JSON bytes at
                // (output_ptr, output_len).
                const std::string output_json(
                    reinterpret_cast<const char *>(
                        mem->data() + tuple->output_ptr.value),
                    tuple->output_len);
                auto parsed = value_from_json(output_json);
                if (parsed.has_value()) {
                    workflow_output = std::move(*parsed);
                } else {
                    run_ok = false;
                    run_status = RunTerminalStatus::Failed;
                    run_failure_kind = WorkflowFailureKind::EvaluationFailed;
                    run_failure_code =
                        std::string{wasm_diag::kOutputDecodeFailed};
                    run_failure_message =
                        "run_workflow_session: WireJson workflow output "
                        "failed to parse";
                }
            }
        }
    }

    // --- 12. Read transition_count and workflow_completed_count globals ---
    std::uint32_t transition_count = 0;
    if (auto tc = engine.read_exported_global_u32("transition_count")) {
        transition_count = *tc;
    }
    std::uint32_t workflow_completed_count = 0;
    if (auto wc = engine.read_exported_global_u32("workflow_completed_count")) {
        workflow_completed_count = *wc;
    }

    // P2-4: validate workflow_completed_count against
    // descriptor.workflow_node_count. A successful run MUST complete every
    // scheduled node; a failed run completes at most every node. A mismatch
    // is evidence of a corrupt module (mirrors the JS oracle's validation).
    if (run_ok) {
        if (workflow_completed_count != descriptor.workflow_node_count) {
            return std::unexpected(
                "run_workflow_session: workflow_completed_count (" +
                std::to_string(workflow_completed_count) +
                ") disagrees with descriptor.workflow_node_count (" +
                std::to_string(descriptor.workflow_node_count) + ")");
        }
    } else {
        if (workflow_completed_count > descriptor.workflow_node_count) {
            return std::unexpected(
                "run_workflow_session: workflow_completed_count (" +
                std::to_string(workflow_completed_count) +
                ") exceeds descriptor.workflow_node_count (" +
                std::to_string(descriptor.workflow_node_count) + ")");
        }
    }

    // For identity workflows (no capability imports, no event buffer) the
    // module writes no node-event records, so node_completed is derived from
    // the workflow_completed_count global: the module increments it once per
    // node completion (core_wasm_codegen.cpp), so the first
    // workflow_completed_count nodes (in schedule order) completed and the
    // rest never executed. On a failed run the node at index
    // workflow_completed_count is the trapping node (it produced states but
    // did not complete); subsequent nodes are Skipped. The previous
    // state-collection heuristic (!states_per_node[i].empty()) misattributed
    // the trapping node as Completed because a trap mid-walk still leaves
    // state entries behind.
    if (descriptor.imports.empty()) {
        for (std::size_t i = 0; i < descriptor.nodes.size(); ++i) {
            node_completed[i] = i < workflow_completed_count;
        }
    }

    // --- 13. Build per-node run facts and finalize via the lifecycle helper ---
    WasmWorkflowRunFacts facts;
    facts.status = run_status;
    facts.failure_kind = run_failure_kind;
    facts.failure_code = run_failure_code;
    facts.failure_message = run_failure_message;
    facts.workflow_output = std::move(workflow_output);
    facts.capability_calls = std::move(collected_cap_calls);
    facts.nodes.reserve(descriptor.nodes.size());

    // On a failed run, the first non-completed node (in schedule order) is
    // the Failed node; subsequent nodes are Skipped.
    std::optional<std::size_t> failed_node_index;
    if (!run_ok) {
        for (std::size_t i = 0; i < descriptor.nodes.size(); ++i) {
            if (!node_completed[i]) {
                failed_node_index = i;
                break;
            }
        }
    }

    for (std::size_t i = 0; i < descriptor.nodes.size(); ++i) {
        WasmNodeRunFacts node_facts;
        node_facts.states = std::move(states_per_node[i]);
        node_facts.output = std::move(node_outputs[i]);

        if (run_ok || node_completed[i]) {
            node_facts.terminal = WasmNodeRunFacts::Terminal::Completed;
        } else if (failed_node_index.has_value() &&
                   i == *failed_node_index) {
            node_facts.terminal = WasmNodeRunFacts::Terminal::Failed;
            node_facts.failure_kind = NodeFailureKind::AgentFailed;
            node_facts.failure_code = run_failure_code;
            node_facts.failure_message = run_failure_message;
        } else {
            node_facts.terminal = WasmNodeRunFacts::Terminal::Skipped;
            // Blocking dependency: the failed node.
            if (failed_node_index.has_value()) {
                node_facts.blocking_dependencies.push_back(
                    descriptor.nodes[*failed_node_index].node_id);
            }
        }
        facts.nodes.push_back(std::move(node_facts));
    }

    WorkflowResult result;
    const bool report_ok =
        finalize_wasm_workflow_run(result, descriptor, std::move(facts));
    if (!report_ok) {
        // build_report already emitted wasm.event-stream-invalid and left
        // the report fail-closed (status=Failed,
        // failure_kind=EvaluationFailed). The event stream is constructed
        // by emit_workflow_events (correct by construction); a false
        // return means an internal invariant was violated. The fail-closed
        // report is the correct response.
        result.report.status = RunTerminalStatus::Failed;
    }

    return WorkflowSessionResult{
        .result = std::move(result),
        .states = std::move(collected_states),
        .capabilities = std::move(collected_capabilities),
        .capability_arguments = std::move(collected_cap_args),
        .transition_count = transition_count,
        .workflow_completed_count = workflow_completed_count,
        .capability_failures = std::move(collected_failures),
    };
}

} // namespace ahfl::runtime::wasm_host
