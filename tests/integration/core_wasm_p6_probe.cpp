// RFC 0026 P6-1 (KR6.6) same-frontend scalar-computation producer. One checked
// program is forked to native AgentRuntime and AHFL->Core->P4-D->wasm. Unlike
// the E1 identity probe, the non-final handlers carry real scalar computation
// (Literal/ValueRef/Unary/Binary) that drives computed gotos; the result is
// observable purely through the state ids step()/run transition through, with
// no frame encoding. The probe prints the native state-entry id sequence and
// final state so the Node embedded host can drive step() and assert the same
// ids and transition_count (real Node-engine evidence, NOT wasmtime).
//
// RFC 0026 P7 (KR6.7): `--workflow <entry>` selects the WORKFLOW lane instead.
// The entry is resolved through `resolve_core_wasm_entry` with the SAME typed
// PackageMetadata the wasm eligibility classifier builds from a manifest, so the
// artifact this probe writes is by construction the artifact the classifier
// certified (never a same-program look-alike selected by declaration position).
// The classifier pinned `float_output_e2e` as a WORKFLOW-lane promotion, and the
// Node execution witness must run THAT module, not an agent-lane sibling.

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/handoff/package.hpp"
#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/core_layout.hpp"
#include "ahfl/compiler/ir/core_wasm_abi_constants.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/validate.hpp"
#include "compiler/backends/infra/core_wasm_codegen.hpp"
#include "runtime/engine/agent_runtime.hpp"
#include "runtime/engine/workflow_runtime.hpp"
#include "runtime/evaluator/value.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace ahfl;

// RFC 0026 P6-4: the input-frame values this probe writes for a fixture whose
// input struct is `{ a: Int; b: Int }`, in DECLARATION order. A fixture that
// wants a specific branch supplies its own by editing these two constants; the
// probe prints the P4-D offsets it used so the Node host writes the SAME bytes.
constexpr std::int64_t kAggregateInputA = 100;
constexpr std::int64_t kAggregateInputB = 7;

[[nodiscard]] std::optional<ir::Program> compile_fixture(const std::filesystem::path &path) {
    const Frontend frontend;
    const auto parse = frontend.parse_file(path);
    if (parse.has_errors() || parse.program == nullptr) {
        parse.diagnostics.render(std::cerr);
        return std::nullopt;
    }
    const Resolver resolver;
    const auto resolve = resolver.resolve(*parse.program);
    if (resolve.has_errors()) {
        resolve.diagnostics.render(std::cerr);
        return std::nullopt;
    }
    const TypeChecker checker;
    const auto typecheck = checker.check(*parse.program, resolve);
    if (typecheck.has_errors()) {
        typecheck.diagnostics.render(std::cerr);
        return std::nullopt;
    }
    const Validator validator;
    const auto validation = validator.validate(*parse.program, resolve, typecheck);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cerr);
        return std::nullopt;
    }
    return lower_program_ir(*parse.program, resolve, typecheck);
}

[[nodiscard]] evaluator::Value fixture_input() {
    evaluator::FieldMap fields;
    fields.set("value", std::make_unique<evaluator::Value>(evaluator::make_string("identity")));
    return evaluator::Value{evaluator::StructValue{"wasm::p6::Frame", std::move(fields)}};
}

// RFC 0026 P6-4: the aggregate fixture's input frame. The struct is
// `{ a: Int; b: Int }` (both unbounded Int -> i64), so the frame is written with
// those two typed values in DECLARATION order and the native run reads them
// through the same `input.a` / `input.b` projections the wasm path uses.
[[nodiscard]] evaluator::Value aggregate_input() {
    evaluator::FieldMap fields;
    fields.set("a", std::make_unique<evaluator::Value>(evaluator::make_int(kAggregateInputA)));
    fields.set("b", std::make_unique<evaluator::Value>(evaluator::make_int(kAggregateInputB)));
    return evaluator::Value{evaluator::StructValue{"wasm::p6::Frame", std::move(fields)}};
}

// The single argument-less nominal value type naming core type `type` (index
// identity, never a name — Principle 2).
[[nodiscard]] std::optional<ir::core::CoreValueTypeId>
nominal_value_type(const ir::core::CoreProgram &program, ir::core::CoreTypeId type) {
    for (std::uint32_t i = 0; i < program.value_types.size(); ++i) {
        const auto *nominal = std::get_if<ir::core::CoreVtNominal>(&program.value_types[i].node);
        if (nominal != nullptr && nominal->base == type && nominal->args.empty()) {
            return ir::core::CoreValueTypeId{i};
        }
    }
    return std::nullopt;
}

// The workflow lane's native observation: run the manifest entry through the
// native WorkflowRuntime and report the completion / transition counters the
// emitted workflow module mirrors in its globals. Node names are display-only;
// the counters are the index identity the wasm module carries.
struct WorkflowObservation {
    std::size_t completed_nodes{0};
    std::int64_t transition_count{0};
    std::string status;
};

// The declared input struct for the workflow entry, materialized field by field
// in DECLARATION order (index identity, never a name-keyed lookup). The workflow
// lane's native observation only needs a well-typed frame to run; the identity
// passthrough's counters do not depend on the field values.
[[nodiscard]] std::optional<evaluator::Value>
workflow_input_fixture(const ir::WorkflowDecl &workflow, const ir::Program &program) {
    const ir::StructDecl *input_struct = nullptr;
    for (const auto &decl : program.declarations) {
        if (const auto *candidate = std::get_if<ir::StructDecl>(&decl);
            candidate != nullptr &&
            candidate->name == workflow.input_type_ref.canonical_name) {
            input_struct = candidate;
        }
    }
    if (input_struct == nullptr) {
        std::cerr << "workflow entry input type is not a declared struct\n";
        return std::nullopt;
    }
    evaluator::FieldMap fields;
    for (const auto &field : input_struct->fields) {
        switch (field.type_ref.kind) {
        case ir::TypeRefKind::Float:
            fields.set(field.name,
                       std::make_unique<evaluator::Value>(evaluator::make_float(0.0)));
            break;
        case ir::TypeRefKind::Int:
        case ir::TypeRefKind::BoundedInt:
            fields.set(field.name, std::make_unique<evaluator::Value>(evaluator::make_int(0)));
            break;
        case ir::TypeRefKind::Bool:
            fields.set(field.name,
                       std::make_unique<evaluator::Value>(evaluator::make_bool(false)));
            break;
        case ir::TypeRefKind::String:
        case ir::TypeRefKind::BoundedString:
            fields.set(field.name,
                       std::make_unique<evaluator::Value>(evaluator::make_string("")));
            break;
        default:
            std::cerr << "workflow input field '" << field.name
                      << "' has a type the probe cannot materialize\n";
            return std::nullopt;
        }
    }
    return evaluator::Value{
        evaluator::StructValue{input_struct->name, std::move(fields)}};
}

[[nodiscard]] std::optional<WorkflowObservation>
observe_workflow_native(const ir::Program &program, std::string_view entry) {
    const ir::WorkflowDecl *workflow = nullptr;
    for (const auto &decl : program.declarations) {
        if (const auto *candidate = std::get_if<ir::WorkflowDecl>(&decl);
            candidate != nullptr && candidate->symbol_ref.canonical_name == entry) {
            workflow = candidate;
        }
    }
    if (workflow == nullptr) {
        std::cerr << "entry workflow '" << entry << "' is not declared\n";
        return std::nullopt;
    }
    auto input = workflow_input_fixture(*workflow, program);
    if (!input.has_value()) {
        return std::nullopt;
    }
    std::size_t completed_nodes = 0;
    std::int64_t state_entries = 0;
    runtime::WorkflowRuntimeConfig config;
    config.node_completed_hook =
        [&](runtime::AgentId, std::string_view, const runtime::Value &) { ++completed_nodes; };
    config.state_entered_hook =
        [&](runtime::AgentId, std::string_view, std::string_view, std::string_view) {
            ++state_entries;
        };
    runtime::WorkflowRuntime native(program, std::move(config));
    const auto result = native.run(std::string{entry}, std::move(*input));
    switch (result.status()) {
    case runtime::WorkflowStatus::Completed:
        break;
    default:
        std::cerr << "native workflow run did not complete\n";
        return std::nullopt;
    }
    WorkflowObservation observation;
    observation.status = "completed";
    observation.completed_nodes = completed_nodes;
    observation.transition_count = state_entries - static_cast<std::int64_t>(completed_nodes);
    return observation;
}

// Emit the WORKFLOW lane's module for one manifest entry and print the native
// counters the module's globals mirror. The entry is resolved through
// `resolve_core_wasm_entry` with the typed PackageMetadata the eligibility
// classifier builds from the manifest's `kind` + `entry`, so the module written
// here IS the module the classifier certified.
[[nodiscard]] int run_workflow_lane(const ir::Program &program,
                                    const std::string &entry,
                                    const char *output_path) {
    const auto observation = observe_workflow_native(program, entry);
    if (!observation.has_value()) {
        return 1;
    }
    const auto core = ir::core::lower_ahfl_to_core(program);
    if (!core.ok()) {
        std::cerr << core.diagnostics.front().code << ": " << core.diagnostics.front().message
                  << "\n";
        return 1;
    }
    const auto layouts = ir::core::compute_core_layouts(core.program);
    if (!layouts.ok() || !layouts.table.has_value()) {
        if (!layouts.diagnostics.empty()) {
            std::cerr << layouts.diagnostics.front().code << ": "
                      << layouts.diagnostics.front().message << "\n";
        }
        return 1;
    }
    handoff::PackageMetadata package_metadata;
    package_metadata.entry_target =
        handoff::ExecutableRef{handoff::ExecutableKind::Workflow, entry};
    const auto resolved = backends::resolve_core_wasm_entry(core.program, &package_metadata);
    if (!resolved.has_value()) {
        std::cerr << resolved.error().code << ": " << resolved.error().message << "\n";
        return 1;
    }
    const auto emitted =
        backends::emit_core_wasm(core.program,
                                 *layouts.table,
                                 {*resolved, backends::WasmProfileKind::Wasi});
    if (!emitted.ok()) {
        std::cerr << emitted.diagnostics.front().code << ": "
                  << emitted.diagnostics.front().message << "\n";
        return 1;
    }
    std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
    const auto &bytes = emitted.artifact->bytes;
    out.write(reinterpret_cast<const char *>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        std::cerr << "failed to write complete wasm artifact\n";
        return 1;
    }
    std::cout << "workflow_status=" << observation->status
              << " completed_nodes=" << observation->completed_nodes
              << " transition_count=" << observation->transition_count
              << " entry=" << entry << "\n";
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    // RFC 0026 P7 (KR6.7): `--workflow <entry>` emits the WORKFLOW lane's module
    // for the manifest entry, resolved through the SAME typed seam the classifier
    // uses. Every other invocation keeps the legacy agent-lane ABI.
    std::string workflow_entry;
    if (argc == 5 && std::string_view{argv[1]} == "--workflow") {
        workflow_entry = argv[2];
    } else if (argc != 3) {
        std::cerr << "usage: ahfl_core_wasm_p6_probe <source.ahfl> <output.wasm>\n"
                     "       ahfl_core_wasm_p6_probe --workflow <entry> <source.ahfl> "
                     "<output.wasm>\n";
        return 2;
    }
    const char *source_path = workflow_entry.empty() ? argv[1] : argv[3];
    const char *output_path = workflow_entry.empty() ? argv[2] : argv[4];

    auto program = compile_fixture(source_path);
    if (!program.has_value()) {
        return 1;
    }

    const ir::AgentDecl *agent = nullptr;
    const ir::FlowDecl *flow = nullptr;
    for (const auto &decl : program->declarations) {
        if (const auto *candidate = std::get_if<ir::AgentDecl>(&decl)) {
            if (agent != nullptr) {
                std::cerr << "expected exactly one agent\n";
                return 1;
            }
            agent = candidate;
        } else if (const auto *candidate = std::get_if<ir::FlowDecl>(&decl)) {
            if (flow != nullptr) {
                std::cerr << "expected exactly one flow\n";
                return 1;
            }
            flow = candidate;
        }
    }
    if (!workflow_entry.empty()) {
        return run_workflow_lane(*program, workflow_entry, output_path);
    }
    if (agent == nullptr || flow == nullptr) {
        std::cerr << "fixture does not contain one agent and flow\n";
        return 1;
    }

    // RFC 0026 P6-4: an input struct with a leading `a` field selects the
    // aggregate frame; every other fixture keeps the identity frame.
    const ir::StructDecl *input_struct = nullptr;
    for (const auto &decl : program->declarations) {
        if (const auto *candidate = std::get_if<ir::StructDecl>(&decl)) {
            if (candidate->name == agent->input_type_ref.canonical_name ||
                candidate->name == agent->input_type_ref.display_name) {
                input_struct = candidate;
            }
        }
    }
    const bool aggregate_fixture =
        input_struct != nullptr && !input_struct->fields.empty() &&
        input_struct->fields.front().name == "a";

    // Native observation: collect every entered state NAME in order. The
    // observer requires a valid invocation agent id to fire.
    std::vector<std::string> entered_names;
    auto input = aggregate_fixture ? aggregate_input() : fixture_input();
    runtime::AgentRuntime native(*agent, *flow);
    runtime::CapabilityInvocationContext context;
    context.agent_id = runtime::AgentId{0};
    native.set_invocation_context(std::move(context));
    native.set_state_entered_observer(
        [&](runtime::AgentId, std::string_view state_name) -> runtime::AgentStateId {
            entered_names.emplace_back(state_name);
            return runtime::AgentStateId{entered_names.size() - 1};
        });
    const auto native_result = native.run(std::move(input));
    const char *native_status = "unknown";
    switch (native_result.status) {
    case runtime::AgentStatus::Completed:
        native_status = "completed";
        break;
    case runtime::AgentStatus::Failed:
        native_status = "failed";
        break;
    default:
        native_status = "other";
        break;
    }

    // Resolve entered state names to dense ids (the same index identity Core
    // and the wasm globals use; names are display-only).
    std::vector<std::uint32_t> entered_ids;
    for (const auto &name : entered_names) {
        const auto it = std::find(agent->states.begin(), agent->states.end(), name);
        if (it == agent->states.end()) {
            std::cerr << "native entered an undeclared state '" << name << "'\n";
            return 1;
        }
        entered_ids.push_back(static_cast<std::uint32_t>(it - agent->states.begin()));
    }
    const auto final_it =
        std::find(agent->states.begin(), agent->states.end(), native_result.current_state);
    const std::int64_t final_id = final_it == agent->states.end()
                                      ? -1
                                      : static_cast<std::int64_t>(final_it - agent->states.begin());

    const auto core = ir::core::lower_ahfl_to_core(*program);
    if (!core.ok()) {
        std::cerr << core.diagnostics.front().code << ": " << core.diagnostics.front().message
                  << "\n";
        return 1;
    }
    const auto layouts = ir::core::compute_core_layouts(core.program);
    if (!layouts.ok() || !layouts.table.has_value()) {
        if (!layouts.diagnostics.empty()) {
            std::cerr << layouts.diagnostics.front().code << ": "
                      << layouts.diagnostics.front().message << "\n";
        }
        return 1;
    }
    const auto emitted = backends::emit_core_wasm(
        core.program, *layouts.table, {ir::core::CoreAgentId{0}, backends::WasmProfileKind::Wasi});
    if (!emitted.ok()) {
        std::cerr << emitted.diagnostics.front().code << ": " << emitted.diagnostics.front().message
                  << "\n";
        return 1;
    }

    std::ofstream out(argv[2], std::ios::binary | std::ios::trunc);
    const auto &bytes = emitted.artifact->bytes;
    out.write(reinterpret_cast<const char *>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        std::cerr << "failed to write complete wasm artifact\n";
        return 1;
    }

    std::cout << "native_status=" << native_status << " entered_ids=";
    for (std::size_t i = 0; i < entered_ids.size(); ++i) {
        std::cout << (i == 0 ? "" : ",") << entered_ids[i];
    }
    std::cout << " final_state_id=" << final_id
              << " transition_count=" << native_result.stats.state_transitions
              << " initial_state_id="
              << (std::find(agent->states.begin(), agent->states.end(), agent->initial_state) -
                  agent->states.begin())
              << "\n";

    // RFC 0026 P6-4: for the aggregate fixture, report the P4-D input frame the
    // host must write (base + per-field offset/width/value) so the Node host
    // mirrors the SAME layout instead of re-deriving it. Empty for the scalar /
    // match fixtures, which keep their own observation protocol.
    if (aggregate_fixture && input_struct != nullptr) {
        const auto value_type =
            nominal_value_type(core.program, core.program.agents.front().input_type);
        if (!value_type.has_value() || value_type->value >= layouts.table->value_layouts.size()) {
            std::cerr << "aggregate fixture input struct has no interned value type\n";
            return 1;
        }
        const auto &layout =
            layouts.table->layouts[layouts.table->value_layouts[value_type->value].value];
        const auto *structure = std::get_if<ir::core::CoreLayoutStruct>(&layout.shape);
        if (structure == nullptr || structure->field_offsets.size() != input_struct->fields.size()) {
            std::cerr << "aggregate fixture input layout is not a matching struct\n";
            return 1;
        }
        std::cout << "aggregate_base=" << ir::core::kP6AggregateInputBase << " fields=";
        for (std::size_t i = 0; i < input_struct->fields.size(); ++i) {
            const std::int64_t value =
                input_struct->fields[i].name == "a" ? kAggregateInputA : kAggregateInputB;
            if (i != 0) {
                std::cout << ",";
            }
            std::cout << input_struct->fields[i].name << "@" << structure->field_offsets[i] << ":"
                      << value;
        }
        std::cout << "\n";
    }
    return 0;
}
