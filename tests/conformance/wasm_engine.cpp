#include "conformance/wasm_engine.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include "ahfl/compiler/handoff/package.hpp"
#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/core_layout.hpp"
#include "conformance/compile_source.hpp"

namespace ahfl::conformance {

namespace {

namespace json = ahfl::json;
using ahfl::backends::CoreWasmFrameContract;

[[nodiscard]] std::unique_ptr<json::JsonValue> jstr(std::string value) {
    return json::JsonValue::make_string(std::move(value));
}

[[nodiscard]] std::unique_ptr<json::JsonValue> juint(std::uint64_t value) {
    return json::JsonValue::make_int(static_cast<std::int64_t>(value));
}

[[nodiscard]] const char *mock_status_name(CapabilityOutcomeStatus status) {
    switch (status) {
    case CapabilityOutcomeStatus::Ok:
        return "ok";
    case CapabilityOutcomeStatus::Error:
        return "error";
    case CapabilityOutcomeStatus::Pending:
        return "pending";
    }
    return "error";
}

[[nodiscard]] const char *frame_contract_name(CoreWasmFrameContract contract) {
    switch (contract) {
    case CoreWasmFrameContract::WireJson:
        return "wire_json";
    case CoreWasmFrameContract::RawP6Frame:
        return "raw_p6_frame";
    }
    return "wire_json";
}

// Builds the scenario + capability-mock section. The frame values cross the
// opaque Core-Wasm ABI boundary as CANONICAL WIRE BYTES, so the descriptor
// carries those exact bytes as `input_wire` / mock `result_wire` strings (not a
// parsed DOM): an integral float's "2.0" spelling is only recoverable from the
// bytes, and the module forwards/writes them verbatim. The manifest parser has
// already byte-verified they are canonical.
[[nodiscard]] std::unique_ptr<json::JsonValue> build_scenarios(const ConformanceCase &manifest) {
    auto scenarios = json::JsonValue::make_array();

    // Index the mock table by canonical capability name.
    std::unordered_map<std::string, const CapabilityExpectation *> mocks_by_name;
    mocks_by_name.reserve(manifest.capabilities.size());
    for (const auto &capability : manifest.capabilities) {
        mocks_by_name.emplace(capability.name, &capability);
    }

    for (const auto &scenario : manifest.scenarios) {
        auto scenario_node = json::JsonValue::make_object();
        scenario_node->set("name", jstr(scenario.name));
        scenario_node->set("input_wire", jstr(scenario.input_json));

        const char *expected_status = "failed";
        switch (scenario.expect.run_status) {
        case ExpectedRunStatus::Completed:
            expected_status = "completed";
            break;
        case ExpectedRunStatus::Suspended:
            expected_status = "suspended";
            break;
        case ExpectedRunStatus::Failed:
            expected_status = "failed";
            break;
        }
        scenario_node->set("expected_run_status", jstr(expected_status));

        // Per-scenario ordered capability mocks, in the scenario's declared
        // invocation order. The host replays each invocation against the named
        // capability's outcome; an `ok` mock carries its result frame bytes.
        auto mocks = json::JsonValue::make_array();
        for (const auto &called_name : scenario.expect.capability_sequence) {
            const auto found = mocks_by_name.find(called_name);
            if (found == mocks_by_name.end()) {
                return nullptr; // manifest parser already cross-checked this
            }
            const CapabilityExpectation &capability = *found->second;
            auto mock = json::JsonValue::make_object();
            mock->set("name", jstr(capability.name));
            mock->set("status", jstr(mock_status_name(capability.status)));
            if (capability.result_json.has_value()) {
                mock->set("result_wire", jstr(*capability.result_json));
            }
            mocks->push(std::move(mock));
        }
        scenario_node->set("mocks", std::move(mocks));
        scenarios->push(std::move(scenario_node));
    }
    return scenarios;
}

// Builds the reachable-import catalogue (ordinal -> wasm field + canonical
// name), the stable seam the host binds ahfl_cap through.
[[nodiscard]] std::unique_ptr<json::JsonValue>
build_imports(const ahfl::backends::CoreWasmExecutionDescriptor &descriptor) {
    auto imports = json::JsonValue::make_array();
    for (const auto &import_descriptor : descriptor.imports) {
        auto node = json::JsonValue::make_object();
        node->set("ordinal", juint(import_descriptor.ordinal));
        node->set("field", jstr(import_descriptor.field));
        node->set("name", jstr(import_descriptor.canonical_name));
        imports->push(std::move(node));
    }
    return imports;
}

[[nodiscard]] std::unique_ptr<json::JsonValue>
build_agent_lane(const ahfl::backends::CoreWasmExecutionDescriptor &descriptor) {
    auto lane = json::JsonValue::make_object();
    lane->set("agent", jstr(descriptor.agent_name));
    lane->set("initial_state", juint(descriptor.initial_state));
    auto states = json::JsonValue::make_array();
    for (const auto &state : descriptor.states) {
        states->push(jstr(state));
    }
    lane->set("states", std::move(states));
    return lane;
}

[[nodiscard]] std::unique_ptr<json::JsonValue>
build_workflow_lane(const ahfl::backends::CoreWasmExecutionDescriptor &descriptor) {
    auto lane = json::JsonValue::make_object();

    auto agents = json::JsonValue::make_array();
    for (const auto &walk : descriptor.agents) {
        auto agent_node = json::JsonValue::make_object();
        agent_node->set("agent", jstr(walk.agent));
        auto walk_states = json::JsonValue::make_array();
        for (const auto &state : walk.walk) {
            walk_states->push(jstr(state));
        }
        agent_node->set("walk", std::move(walk_states));
        agents->push(std::move(agent_node));
    }
    lane->set("agents", std::move(agents));

    auto nodes = json::JsonValue::make_array();
    for (const auto &node : descriptor.nodes) {
        auto node_node = json::JsonValue::make_object();
        node_node->set("node_id", juint(node.node_id));
        node_node->set("schedule_pos", juint(node.schedule_pos));
        node_node->set("runner", juint(node.runner));
        node_node->set("has_capability", json::JsonValue::make_bool(node.has_capability));
        node_node->set("capability_ordinal", juint(node.capability_ordinal));
        node_node->set("source_symbol", juint(node.source_symbol));
        nodes->push(std::move(node_node));
    }
    lane->set("nodes", std::move(nodes));
    return lane;
}

[[nodiscard]] std::string
render_descriptor(const ConformanceCase &manifest,
                  const ahfl::backends::CoreWasmExecutionDescriptor &descriptor) {
    auto root = json::JsonValue::make_object();
    root->set("schema", jstr(std::string(kConformanceWasmDescriptorFormat)));
    root->set("case", jstr(manifest.source));
    root->set("entry", jstr(manifest.entry));
    root->set("kind", jstr(manifest.kind == CaseKind::Workflow ? "workflow" : "agent"));
    root->set("frame_contract", jstr(frame_contract_name(descriptor.frame_contract)));
    root->set("imports", build_imports(descriptor));

    auto event = json::JsonValue::make_object();
    event->set("log_base", juint(descriptor.event_log_base));
    event->set("header_bytes", juint(descriptor.event_header_bytes));
    event->set("record_bytes", juint(descriptor.event_record_bytes));
    event->set("records_base", juint(descriptor.event_records_base));
    root->set("event_buffer", std::move(event));
    root->set("heap_base", juint(descriptor.heap_base));
    root->set("workflow_node_count", juint(descriptor.workflow_node_count));

    if (descriptor.is_workflow) {
        root->set("workflow_lane", build_workflow_lane(descriptor));
    } else {
        root->set("agent_lane", build_agent_lane(descriptor));
    }

    auto scenarios = build_scenarios(manifest);
    if (scenarios == nullptr) {
        return {};
    }
    root->set("scenarios", std::move(scenarios));
    return json::serialize_json(*root);
}

} // namespace

WasmProduceResult produce_conformance_wasm(const LoadedConformanceCase &loaded,
                                           const ConformanceScenario &scenario) {
    (void)scenario; // module emission is case-level; scenario data is in the descriptor
    const ConformanceCase &manifest = loaded.manifest;
    WasmProduceResult result;

    auto program = compile_conformance_source(loaded.source_path, result.reason);
    if (!program.has_value()) {
        result.skip = WasmProduceSkip::Blocked;
        result.code = "source";
        return result;
    }

    const auto core = ahfl::ir::core::lower_ahfl_to_core(*program);
    if (!core.ok()) {
        result.skip = WasmProduceSkip::Blocked;
        result.code = core.diagnostics.front().code;
        result.reason = core.diagnostics.front().message;
        return result;
    }

    const auto layouts = ahfl::ir::core::compute_core_layouts(core.program);
    if (!layouts.ok() || !layouts.table.has_value()) {
        result.skip = WasmProduceSkip::Blocked;
        if (!layouts.diagnostics.empty()) {
            result.code = layouts.diagnostics.front().code;
            result.reason = layouts.diagnostics.front().message;
        } else {
            result.code = "core.layout.INVALID";
        }
        return result;
    }

    ahfl::handoff::PackageMetadata package_metadata;
    package_metadata.entry_target = ahfl::handoff::ExecutableRef{
        manifest.kind == CaseKind::Agent ? ahfl::handoff::ExecutableKind::Agent
                                         : ahfl::handoff::ExecutableKind::Workflow,
        manifest.entry,
    };
    const auto entry = ahfl::backends::resolve_core_wasm_entry(core.program, &package_metadata);
    if (!entry.has_value()) {
        result.skip = WasmProduceSkip::Blocked;
        result.code = entry.error().code;
        result.reason = entry.error().message;
        return result;
    }

    const auto emitted = ahfl::backends::emit_core_wasm(
        core.program, *layouts.table, {*entry, ahfl::backends::WasmProfileKind::Wasi});
    if (!emitted.ok() || !emitted.artifact.has_value()) {
        result.skip = WasmProduceSkip::Blocked;
        if (!emitted.diagnostics.empty()) {
            result.code = emitted.diagnostics.front().code;
            result.reason = emitted.diagnostics.front().message;
        } else {
            result.code = "wasm.INTERNAL_INVALID";
        }
        return result;
    }

    if (!emitted.descriptor.has_value()) {
        result.skip = WasmProduceSkip::Blocked;
        result.code = "wasm.INTERNAL_INVALID";
        result.reason = "emitted artifact carries no execution descriptor";
        return result;
    }

    if (emitted.descriptor->frame_contract == CoreWasmFrameContract::RawP6Frame) {
        result.skip = WasmProduceSkip::RawP6FrameAwaitsP67;
        result.code = "p6-7";
        result.reason = "module projects raw P4-D input-frame bytes; canonical wire-JSON output "
                        "observation awaits the P6-7 output-frame decision";
        return result;
    }

    const std::string descriptor_json = render_descriptor(manifest, *emitted.descriptor);
    if (descriptor_json.empty()) {
        result.skip = WasmProduceSkip::Blocked;
        result.code = "wasm.INTERNAL_INVALID";
        result.reason = "failed to render the execution descriptor";
        return result;
    }

    result.ok = true;
    result.artifact_bytes = emitted.artifact->bytes;
    result.descriptor_json = std::move(descriptor_json);
    return result;
}

} // namespace ahfl::conformance
