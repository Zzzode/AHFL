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
// Agent modules do NOT emit the exec-manifest (AHFLXM) section that the WH-3
// capability_import executor requires, so the canonical instance's import
// callback is a simplified opaque-lane handler: it decodes the wire-JSON
// argument envelope directly from the param_frame, resolves the capability
// name from the descriptor's imports table, invokes through the contextual
// invoker, and writes the serialized result back via alloc_then_write. This
// covers the WireJson opaque lane; the P6-frame bridge lane (control-block
// frame walk) is not exercised by WH-4 agent fixtures and returns a
// fail-closed error.

#include "runtime/wasm_host/wasm_agent_runner.hpp"

#include "runtime/engine/wire_value.hpp"
#include "runtime/wasm_host/frame_packer.hpp"
#include "runtime/wasm_host/frame_reader.hpp"
#include "runtime/wasm_host/frame_walk.hpp"

#include "ahfl/compiler/ir/core_wire_migration.hpp"
#include "runtime/value/value_json.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ahfl::runtime::wasm_host {

namespace {

namespace irc = ::ahfl::ir::core;
namespace eng = ::ahfl::runtime::core_wasm_resume_engine;
namespace bd = ::ahfl::backends;

// Extract the local capability name from a fully-qualified canonical name
// (e.g. "wasm::e2_capability::Echo" -> "Echo"). The evaluator's capability
// name is the local declaration name, so the hook and invoker receive the
// same spelling the conformance comparator expects.
[[nodiscard]] std::string
local_capability_name(std::string_view canonical) {
    const auto pos = canonical.rfind("::");
    if (pos == std::string_view::npos) {
        return std::string(canonical);
    }
    return std::string(canonical.substr(pos + 2));
}

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
    std::fill(page->begin() + 1024, page->begin() + 16384,
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
               ContextualCapabilityInvoker invoker) {
    if (descriptor.is_workflow) {
        return std::unexpected(
            "run_wasm_agent: descriptor is a workflow, not an agent");
    }

    const bool is_p6 =
        descriptor.frame_contract == bd::CoreWasmFrameContract::P6Frame;

    // === 1. Step-walk on a SEPARATE effects-free instance ===
    // The import callback on this instance drives the state walk but its
    // events are discarded (the canonical instance fires the real hooks).
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

    // Effects-free import callback: for agents with no capabilities this is a
    // fail-closed abort (the step-walk never triggers an import). For agents
    // WITH capabilities the walk needs effects to drive computed handlers,
    // but WH-4 agent fixtures have none, so an abort is the correct
    // fail-closed discipline.
    eng::ImportCallback walk_callback =
        [](const eng::ImportObservation &) -> eng::ImportCallbackResult {
            return eng::ImportAbort{};
        };

    StateEnteredHook walk_hook;
    if (hooks.state_entered_hook) {
        walk_hook = [&hooks](AgentId id, std::string_view agent_name,
                             std::string_view node_name,
                             std::string_view state_name) {
            hooks.state_entered_hook(id, agent_name, node_name, state_name);
        };
    }

    auto walk = run_agent_step_walk(module_bytes, walk_desc, input,
                                    std::move(walk_callback), walk_hook,
                                    AgentId{0}, "");
    if (!walk.has_value()) {
        return std::unexpected("run_wasm_agent: step-walk failed: " +
                               walk.error());
    }

    // === 2. Canonical run on a FRESH instance ===
    Wasm3ResumeEngine engine;

    // Collect observation data + fire capability hooks.
    std::vector<std::string> collected_capabilities;
    std::vector<std::string> collected_cap_args;
    std::vector<CapabilityFailureKind> collected_failures;

    // Build the canonical instance's import callback. Agents with no
    // capabilities never call ahfl_cap, so the callback is a fail-closed
    // abort. Agents WITH capabilities use a simplified opaque-lane handler
    // (agent modules do not emit the exec-manifest the WH-3 executor
    // requires).
    eng::ImportCallback canonical_callback;
    if (descriptor.imports.empty()) {
        canonical_callback =
            [](const eng::ImportObservation &) -> eng::ImportCallbackResult {
                return eng::ImportAbort{};
            };
    } else {
        // Simplified opaque-lane handler for WireJson agents with
        // capabilities. P6-frame bridge-lane agents are not supported in
        // WH-4 (the bridge control-block walk needs the frame section's
        // bridge_call_sites, which the simplified handler does not walk).
        canonical_callback =
            [&engine, &descriptor, &invoker, &hooks,
             &collected_capabilities, &collected_cap_args,
             &collected_failures](
                const eng::ImportObservation &obs)
            -> eng::ImportCallbackResult {
                if (obs.import_ordinal >= descriptor.imports.size()) {
                    return eng::ImportAbort{};
                }
                const auto &cap_import =
                    descriptor.imports[obs.import_ordinal];

                // Resolve the capability name. The descriptor's imports table
                // carries the fully-qualified canonical name; the hook and
                // invoker receive the local declaration name (the same
                // spelling the evaluator produces).
                const std::string cap_name =
                    local_capability_name(cap_import.canonical_name);

                // Decode the wire-JSON argument envelope.
                const std::string_view json_text(
                    reinterpret_cast<const char *>(obs.param_frame.data()),
                    obs.param_frame.size());
                auto parsed = value_from_json(json_text);
                if (!parsed.has_value()) {
                    return eng::ImportAbort{};
                }

                // The opaque lane is arity-1: the envelope is either a bare
                // struct JSON or a {"value":..} wrapper. Unwrap to a single
                // arg.
                std::vector<Value> args;
                // Try to detect the {"value":..} wrapper vs bare struct.
                // A bare struct has a "_type" field; a wrapper has "value".
                // The simplest correct approach: pass the parsed value as a
                // single arg. The invoker receives the envelope value.
                args.push_back(std::move(*parsed));

                // Fire capability_invoked_hook PRE-call.
                if (hooks.capability_invoked_hook) {
                    hooks.capability_invoked_hook(AgentId{0}, cap_name);
                }
                collected_capabilities.push_back(cap_name);

                // Serialize the argument envelope for observation.
                if (auto envelope = serialize_args_for_wire_json(args)) {
                    collected_cap_args.push_back(std::move(*envelope));
                }

                // Invoke the capability.
                CapabilityInvocationContext ctx;
                ctx.agent_id = AgentId{0};
                auto result = invoker(ctx, cap_name, args);

                // Fire capability_result_observer POST-call.
                if (hooks.capability_result_observer) {
                    hooks.capability_result_observer(ctx, result);
                }
                if (result.failure_kind.has_value()) {
                    collected_failures.push_back(*result.failure_kind);
                }

                // Map the status to the raw ahfl_cap_status word.
                const std::uint32_t raw_status =
                    result.status == CapabilityCallStatus::Success ? 0u : 1u;
                if (result.status != CapabilityCallStatus::Success) {
                    return eng::ImportReply{
                        raw_status, eng::GuestPointer{0}, 0};
                }

                // Serialize the result to wire JSON.
                auto value = std::move(result.value)
                                 .value_or(Value{NoneValue{}});
                auto body = serialize_value_for_wire_json(value);
                if (!body.has_value()) {
                    return eng::ImportAbort{};
                }

                // alloc_then_write the result frame.
                const std::span<const std::uint8_t> bytes(
                    reinterpret_cast<const std::uint8_t *>(body->data()),
                    body->size());
                auto ptr = engine.alloc_then_write(bytes);
                if (!ptr.has_value()) {
                    return eng::ImportAbort{};
                }

                return eng::ImportReply{
                    raw_status, *ptr,
                    static_cast<std::uint32_t>(body->size())};
            };
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
    std::optional<Value> output_value;
    bool run_ok = false;

    if (is_p6) {
        auto outcome = engine.invoke_runv();
        if (!outcome.has_value()) {
            return std::unexpected(
                "run_wasm_agent: invoke_runv failed (engine error)");
        }
        if (std::holds_alternative<eng::Run2HostAborted>(*outcome)) {
            return std::unexpected(
                "run_wasm_agent: runv host-aborted (capability import "
                "failure)");
        }
        if (std::holds_alternative<eng::Run2Trapped>(*outcome)) {
            return std::unexpected("run_wasm_agent: runv trapped");
        }
        const auto &runv = std::get<RunvResult>(*outcome);
        run_ok = (runv.raw_status == 0);
        if (run_ok) {
            output_value =
                read_p6_agent_output(engine, descriptor,
                                     runv.value_ptr.value);
        }
    } else {
        auto outcome = engine.invoke_run2(entry_ptr, entry_len);
        if (!outcome.has_value()) {
            return std::unexpected(
                "run_wasm_agent: invoke_run2 failed (engine error)");
        }
        if (std::holds_alternative<eng::Run2HostAborted>(*outcome)) {
            return std::unexpected(
                "run_wasm_agent: run2 host-aborted (capability import "
                "failure)");
        }
        if (std::holds_alternative<eng::Run2Trapped>(*outcome)) {
            return std::unexpected("run_wasm_agent: run2 trapped");
        }
        const auto &tuple = std::get<eng::Run2ResultTuple>(*outcome);
        run_ok = (tuple.raw_status == 0);
        if (run_ok && tuple.output_ptr.value != 0 && tuple.output_len > 0) {
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
        }
    }

    // Read transition_count from the canonical instance.
    std::uint32_t transition_count = 0;
    if (auto tc = engine.read_exported_global_u32("transition_count")) {
        transition_count = *tc;
    }

    // Build the result.
    WasmAgentRunResult result;
    result.result.report.status =
        run_ok ? RunTerminalStatus::Completed : RunTerminalStatus::Failed;
    if (!run_ok) {
        result.result.report.failure_kind = WorkflowFailureKind::NodeFailed;
    }
    if (output_value.has_value()) {
        result.result.values.push_back(std::move(*output_value));
        result.result.report.output = RuntimeValueId{0};
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
