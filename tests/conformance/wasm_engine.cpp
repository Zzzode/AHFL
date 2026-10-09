#include "conformance/wasm_engine.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>

#include "ahfl/compiler/handoff/package.hpp"
#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/core_layout.hpp"
#include "compiler/backends/wasm/descriptor_emit.hpp"
#include "conformance/compile_source.hpp"

namespace ahfl::conformance {

namespace {

namespace json = ahfl::json;

[[nodiscard]] std::unique_ptr<json::JsonValue> jstr(std::string value) {
    return json::JsonValue::make_string(std::move(value));
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

[[nodiscard]] std::string
render_descriptor(const ConformanceCase &manifest,
                  const ahfl::backends::CoreWasmExecutionDescriptor &descriptor) {
    auto root = json::JsonValue::make_object();
    root->set("schema", jstr(std::string(kConformanceWasmDescriptorFormat)));
    root->set("case", jstr(manifest.source));
    root->set("entry", jstr(manifest.entry));
    root->set("kind", jstr(manifest.kind == CaseKind::Workflow ? "workflow" : "agent"));
    // The descriptor-derived fields (imports, event buffer, frame lane, wire
    // schema, workflow/agent lane) are projected by the production emitter so
    // the conformance descriptor and the generic embedded-host descriptor
    // share one projection.
    ahfl::backends::populate_wasm_descriptor_fields(*root, descriptor);

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
