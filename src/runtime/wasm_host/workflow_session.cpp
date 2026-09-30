// RFC 0026 KR6.8 WH-4: the workflow session implementation.
//
// Runs a WORKFLOW module end-to-end on the wasm3 engine, wrapping the WH-3
// capability_import executor with the D1 hook-firing discipline (see the
// header comment for the full contract).

#include "runtime/wasm_host/workflow_session.hpp"

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

// Fire state_entered_hook for a batch of new trace records. Fail-closed:
// a record whose runner or state is out of range is evidence of a corrupt
// module (D-B: no silent OOR skip); returns an error string instead.
[[nodiscard]] std::optional<std::string> fire_state_entries(
    const std::vector<StateTraceRecord> &records, std::size_t from,
    const ahfl::backends::CoreWasmExecutionDescriptor &descriptor,
    const WorkflowSessionConfig &config,
    std::vector<StateEntry> &collected_states) {
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
    }
    return std::nullopt;
}

// Reconstruct the state_sequence for a WireJson workflow from decoded
// node-event records joined to the descriptor's agent walks (schedule order).
// Identity workflows (no capability imports, no event buffer) reconstruct from
// the descriptor nodes directly. Fail-closed on any out-of-range runner or
// unknown node (D-B: mirrors the JS oracle's recordStates, which fails rather
// than skips).
[[nodiscard]] std::optional<std::string> reconstruct_wirejson_states(
    std::span<const std::uint8_t> linear_memory,
    const ahfl::backends::CoreWasmExecutionDescriptor &descriptor,
    const WorkflowSessionConfig &config,
    std::vector<StateEntry> &collected_states) {
    const auto &nodes = descriptor.nodes;
    const auto &agents = descriptor.agents;

    // Identity workflow: no event buffer. The descriptor nodes (in Kahn
    // schedule order) ARE the schedule; join each to its runner's walk.
    if (descriptor.imports.empty()) {
        for (const auto &node : nodes) {
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
        for (const auto &nd : nodes) {
            if (nd.node_id == event.workflow_node_id.value) {
                node_desc = &nd;
                break;
            }
        }
        if (node_desc == nullptr) {
            return "node-event record names unknown node";
        }
        if (node_desc->runner >= agents.size()) {
            return "node-event record runner index out of range";
        }
        const auto &agent = agents[node_desc->runner];
        for (const auto &state_name : agent.walk) {
            if (config.state_entered_hook) {
                config.state_entered_hook(
                    AgentId{node_desc->runner}, agent.agent, node_desc->name,
                    state_name);
            }
            collected_states.push_back({agent.agent, state_name});
        }
    }
    return std::nullopt;
}

// Populate the ExecutionMetadataStore + ExecutionReport from the descriptor.
// Called once per session run so the WorkflowResult carries the same metadata
// shape as the evaluator-backed WorkflowRuntime (P1-5).
void populate_metadata_and_report(
    WorkflowResult &result,
    const ahfl::backends::CoreWasmExecutionDescriptor &descriptor,
    RunTerminalStatus status,
    std::optional<WorkflowFailureKind> failure_kind) {
    auto wf_id = result.metadata.add_workflow("workflow");
    result.report.workflow = wf_id;
    result.report.status = status;
    result.report.failure_kind = failure_kind;

    // Agents + agent states.
    std::vector<AgentId> agent_ids;
    agent_ids.reserve(descriptor.agents.size());
    for (const auto &agent : descriptor.agents) {
        auto agent_id = result.metadata.add_agent(agent.agent);
        agent_ids.push_back(agent_id);
        for (const auto &state_name : agent.all_states) {
            (void)result.metadata.add_agent_state(agent_id, state_name);
        }
    }

    // Nodes.
    for (const auto &node : descriptor.nodes) {
        const auto agent_id =
            node.runner < agent_ids.size() ? agent_ids[node.runner] : AgentId{};
        (void)result.metadata.add_node(node.name, wf_id, agent_id);
        ExecutionNodeReport node_report;
        node_report.node = WorkflowNodeId{node.node_id};
        node_report.agent = agent_id;
        node_report.status =
            (status == RunTerminalStatus::Completed)
                ? NodeReportStatus::Completed
                : NodeReportStatus::Failed;
        result.report.nodes.push_back(std::move(node_report));
        result.report.execution_order.push_back(
            WorkflowNodeId{node.node_id});
    }

    // Capabilities.
    for (const auto &import : descriptor.imports) {
        (void)result.metadata.add_capability(import.canonical_name);
    }
}

// D-D: build a failed WorkflowSessionResult for a terminal run failure
// (trap / host-abort / engine error). Populates the metadata + report from
// the descriptor and adds a diagnostic with the given code + message. The
// collected observation data is preserved (states/capabilities gathered
// before the terminal failure are still valid evidence).
[[nodiscard]] WorkflowSessionResult make_failed_session_result(
    const ahfl::backends::CoreWasmExecutionDescriptor &descriptor,
    std::string diagnostic_code, std::string diagnostic_message,
    std::vector<StateEntry> states,
    std::vector<std::string> capabilities,
    std::vector<std::string> capability_arguments,
    std::vector<CapabilityFailureKind> capability_failures) {
    WorkflowSessionResult session_result;
    populate_metadata_and_report(session_result.result, descriptor,
                                 RunTerminalStatus::Failed,
                                 WorkflowFailureKind::NodeFailed);
    session_result.result.diagnostics.error()
        .code(std::move(diagnostic_code))
        .message(std::move(diagnostic_message))
        .emit();
    session_result.states = std::move(states);
    session_result.capabilities = std::move(capabilities);
    session_result.capability_arguments = std::move(capability_arguments);
    session_result.capability_failures = std::move(capability_failures);
    return session_result;
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

    // P1-6: build a map from canonical capability name to runner (agent_id)
    // so the capability hooks receive the REAL per-import agent_id instead of
    // a hardcoded AgentId{0}. Each capability node's runner is the agent that
    // invokes the capability.
    std::unordered_map<std::string, std::uint32_t> name_to_runner;
    for (const auto &node : descriptor.nodes) {
        if (node.has_capability &&
            node.capability_ordinal < descriptor.imports.size()) {
            name_to_runner.emplace(
                descriptor.imports[node.capability_ordinal].canonical_name,
                node.runner);
        }
    }

    // --- 3. Build the wrapped invoker (fires hooks, collects data) ---
    // Capture config by VALUE (copy): the lambda is stored in a
    // ContextualCapabilityInvoker that the CapabilityImportConfig references,
    // and ASan's stack-use-after-scope detection flags the by-reference
    // capture when the lambda is invoked through the import callback chain.
    // WorkflowSessionConfig is copyable (all std::function members).
    ContextualCapabilityInvoker wrapped_invoker =
        [config, &collected_capabilities, &collected_cap_args,
         &collected_failures, &name_to_runner](
            const CapabilityInvocationContext &ctx,
            const std::string &name,
            const std::vector<Value> &args)
        -> CapabilityCallResult {
        // P1-6: resolve the REAL per-import agent_id from the capability's
        // canonical name. The WH-3 executor passes a single context for all
        // calls; the session layer overrides agent_id per-call so the hooks
        // and invoker see the agent that actually invokes the capability.
        CapabilityInvocationContext real_ctx = ctx;
        if (auto it = name_to_runner.find(name);
            it != name_to_runner.end()) {
            real_ctx.agent_id = AgentId{it->second};
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
             &trace_error, has_trace, inner = std::move(inner_callback)](
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
                            collected_states);
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
    // D-D: trap / host-abort / engine error map to NodeFailed + DiagnosticBag
    // (codes wasm.trap / wasm.host-abort), NOT a bare std::string error. The
    // facade maps the WorkflowResult status; std::unexpected is reserved for
    // pre-run setup failures (admission, instantiation, pack).
    auto outcome = engine.invoke_run2(entry_ptr, entry_len);
    if (!outcome.has_value()) {
        return make_failed_session_result(
            descriptor, "wasm.trap",
            "run_workflow_session: invoke_run2 failed (engine error)",
            std::move(collected_states), std::move(collected_capabilities),
            std::move(collected_cap_args), std::move(collected_failures));
    }

    const auto *tuple = std::get_if<eng::Run2ResultTuple>(&*outcome);
    if (tuple == nullptr) {
        if (std::holds_alternative<eng::Run2HostAborted>(*outcome)) {
            std::string msg =
                "run_workflow_session: run2 host-aborted (capability import "
                "failure)";
            if (import_state.last_error.has_value()) {
                msg += " (CapabilityImportError=";
                msg += std::to_string(
                    static_cast<int>(*import_state.last_error));
                msg += ")";
            }
            return make_failed_session_result(
                descriptor, "wasm.host-abort", std::move(msg),
                std::move(collected_states), std::move(collected_capabilities),
                std::move(collected_cap_args),
                std::move(collected_failures));
        }
        return make_failed_session_result(
            descriptor, "wasm.trap", "run_workflow_session: run2 trapped",
            std::move(collected_states), std::move(collected_capabilities),
            std::move(collected_cap_args), std::move(collected_failures));
    }

    const bool run_ok = (tuple->raw_status == 0);

    // --- 9. Post-run: decode the FULL trace ring and fire remaining
    //        state_entered_hook ---
    if (has_trace) {
        if (trace_error.has_value()) {
            return std::unexpected(
                "run_workflow_session: " + std::move(*trace_error));
        }
        const auto &section = *descriptor.frame_section;
        auto mem = engine.read_whole_memory();
        if (mem.has_value()) {
            auto decoded = decode_state_trace(
                *mem, section.state_trace_base, section.state_trace_capacity);
            if (decoded.has_value() && decoded->size() > last_trace_count) {
                auto err = fire_state_entries(*decoded, last_trace_count,
                                              descriptor, config,
                                              collected_states);
                if (err.has_value()) {
                    return std::unexpected(
                        "run_workflow_session: " + std::move(*err));
                }
                last_trace_count = decoded->size();
            }
        }
    }

    // --- 9b. Post-run: reconstruct WireJson state_sequence from node-event
    //         records joined to agent walks (D-B) ---
    // P6 workflows use the trace ring (section 9); WireJson workflows have no
    // trace ring, so the state sequence is reconstructed from the node-event
    // buffer (capability workflows) or the descriptor nodes (identity
    // workflows), joined to each runner's declared walk. Fires
    // state_entered_hook POST-RUN for every reconstructed state.
    if (!is_p6 && run_ok) {
        auto mem = engine.read_whole_memory();
        if (mem.has_value()) {
            auto err = reconstruct_wirejson_states(
                *mem, descriptor, config, collected_states);
            if (err.has_value()) {
                return std::unexpected(
                    "run_workflow_session: " + std::move(*err));
            }
        }
    }

    // --- 10. Post-run: decode node events and fire node_completed_hook ---
    if (run_ok && config.node_completed_hook &&
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
                    for (const auto &nd : descriptor.nodes) {
                        if (nd.schedule_pos == event.schedule_pos.value) {
                            node_desc = &nd;
                            break;
                        }
                    }
                    if (node_desc == nullptr) {
                        return std::unexpected(
                            "run_workflow_session: node-event record references "
                            "unknown schedule_pos");
                    }

                    const auto runner = node_desc->runner;
                    if (runner >= descriptor.agents.size()) {
                        return std::unexpected(
                            "run_workflow_session: node-event record runner "
                            "index out of range");
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
                    config.node_completed_hook(
                        AgentId{runner}, node_desc->name, node_output);
                }
            }
        }
    }

    // --- 11. Read the workflow output ---
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
                    }
                }
            } else {
                // WireJson: the output is raw JSON bytes at
                // (output_ptr, output_len).
                if (tuple->output_ptr.value != 0 && tuple->output_len > 0) {
                    const std::string output_json(
                        reinterpret_cast<const char *>(
                            mem->data() + tuple->output_ptr.value),
                        tuple->output_len);
                    auto parsed = value_from_json(output_json);
                    if (parsed.has_value()) {
                        workflow_output = std::move(*parsed);
                    }
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

    // --- 13. Build the WorkflowResult ---
    // P1-5: populate the ExecutionMetadataStore + ExecutionReport from the
    // descriptor so the result carries the same metadata shape as the
    // evaluator-backed WorkflowRuntime. D-D: a non-zero run2 status maps to
    // NodeFailed + a wasm.run-failed diagnostic.
    WorkflowResult result;
    if (run_ok) {
        populate_metadata_and_report(result, descriptor,
                                     RunTerminalStatus::Completed, std::nullopt);
    } else {
        populate_metadata_and_report(result, descriptor,
                                     RunTerminalStatus::Failed,
                                     WorkflowFailureKind::NodeFailed);
        result.diagnostics.error()
            .code("wasm.run-failed")
            .message("run_workflow_session: run2 returned non-zero status " +
                     std::to_string(tuple->raw_status))
            .emit();
    }
    if (workflow_output.has_value()) {
        result.values.push_back(std::move(*workflow_output));
        result.report.output = RuntimeValueId{0};
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
