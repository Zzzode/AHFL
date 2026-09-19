#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "ahfl/compiler/ir/core_layout.hpp"
#include "ahfl/compiler/ir/core_verify.hpp"
#include "ahfl/compiler/ir/core_wire_schema.hpp"
#include "compiler/backends/infra/core_wasm_codegen.hpp"
#include "compiler/backends/infra/detail/wasm_byte_buffer.hpp"
#include "compiler/backends/infra/wasm_backend.hpp"
#include "compiler/backends/infra/wasm_runtime.hpp"

static int test_count = 0;
static int pass_count = 0;

static void check(bool condition, const char* name) {
    ++test_count;
    if (condition) { ++pass_count; std::printf("  PASS: %s\n", name); }
    else { std::printf("  FAIL: %s\n", name); }
}

static ahfl::ir::core::CoreProgram make_e1_core_program() {
    using namespace ahfl::ir::core;
    CoreProgram program;

    CoreTypeDecl input;
    input.kind = CoreTypeDecl::Kind::Struct;
    input.name = "app::Input";
    program.types.push_back(std::move(input));
    program.value_types.push_back(
        CoreValueType{CoreVtNominal{CoreTypeId{0}, {}, std::nullopt}});

    CoreAgentDecl agent;
    agent.name = "Runner";
    agent.states = {"Done", "Start"};
    agent.initial = CoreStateId{1}; // proves codegen never assumes state zero
    agent.finals = {CoreStateId{0}};
    agent.transitions = {{CoreStateId{1}, CoreStateId{0}}};
    agent.input_type = CoreTypeId{0};
    agent.output_type = CoreTypeId{0};
    agent.context_kind = CoreAgentDecl::ContextKind::Unit;
    program.agents.push_back(std::move(agent));

    CoreFlowDecl flow;
    flow.target = CoreAgentId{0};
    flow.agent_name = "Runner";
    flow.exprs.push_back(CoreExpr{CorePathExpr{CorePathRoot::Input,
                                               "input",
                                               {},
                                               CoreTypeId{0},
                                               {},
                                               true,
                                               {},
                                               false,
                                               {}},
                                  std::nullopt,
                                  CoreValueTypeId{0}});
    flow.value_count = 1;
    flow.value_types = {CoreValueTypeId{0}};
    CoreFlowState done;
    done.state = CoreStateId{0};
    done.state_name = "Done";
    done.body.statements.push_back(
        CoreStmt{CoreLetStmt{CoreValueId{0}, CoreExprId{0}}, std::nullopt});
    done.body.statements.push_back(
        CoreStmt{CoreReturnStmt{true, CoreValueId{0}}, std::nullopt});
    flow.states.push_back(std::move(done));
    CoreFlowState start;
    start.state = CoreStateId{1};
    start.state_name = "Start";
    start.body.statements.push_back(
        CoreStmt{CoreGotoStmt{CoreStateId{0}, "Done"}, std::nullopt});
    flow.states.push_back(std::move(start));
    program.flows.push_back(std::move(flow));
    return program;
}

static ahfl::ir::core::CoreProgram make_e2_core_program() {
    using namespace ahfl::ir::core;
    CoreProgram program;

    CoreTypeDecl input;
    input.kind = CoreTypeDecl::Kind::Struct;
    input.name = "app::Input";
    input.symbol_ref = {ahfl::ir::SymbolRefKind::Type,
                        "app::Input",
                        "Input",
                        "app",
                        100};
    program.types.push_back(std::move(input));
    CoreTypeDecl output;
    output.kind = CoreTypeDecl::Kind::Struct;
    output.name = "app::Output";
    output.symbol_ref = {ahfl::ir::SymbolRefKind::Type,
                         "app::Output",
                         "Output",
                         "app",
                         101};
    program.types.push_back(std::move(output));
    program.value_types.push_back(
        CoreValueType{CoreVtNominal{CoreTypeId{0}, {}, std::nullopt}});
    program.value_types.push_back(
        CoreValueType{CoreVtNominal{CoreTypeId{1}, {}, std::nullopt}});

    CoreCapabilityDecl capability;
    capability.name = "Echo";
    capability.symbol_ref = {ahfl::ir::SymbolRefKind::Capability,
                             "app::Echo",
                             "Echo",
                             "app",
                             42};
    capability.param_types = {CoreValueTypeId{0}};
    capability.return_type = CoreValueTypeId{1};
    program.capabilities.push_back(std::move(capability));

    CoreAgentDecl agent;
    agent.name = "Runner";
    agent.states = {"Done", "Start"};
    agent.initial = CoreStateId{1};
    agent.finals = {CoreStateId{0}};
    agent.transitions = {{CoreStateId{1}, CoreStateId{0}}};
    agent.input_type = CoreTypeId{0};
    agent.output_type = CoreTypeId{1};
    agent.context_kind = CoreAgentDecl::ContextKind::Unit;
    agent.capabilities = {CoreCapabilityId{0}};
    program.agents.push_back(std::move(agent));

    CoreFlowDecl flow;
    flow.target = CoreAgentId{0};
    flow.agent_name = "Runner";
    flow.exprs.push_back(CoreExpr{CorePathExpr{CorePathRoot::Input,
                                               "input",
                                               {},
                                               CoreTypeId{0},
                                               {},
                                               true,
                                               {},
                                               false,
                                               {}},
                                  std::nullopt,
                                  CoreValueTypeId{0}});
    flow.value_count = 2;
    flow.value_types = {CoreValueTypeId{0}, CoreValueTypeId{1}};
    CoreFlowState done;
    done.state = CoreStateId{0};
    done.state_name = "Done";
    done.body.statements.push_back(
        CoreStmt{CoreLetStmt{CoreValueId{0}, CoreExprId{0}}, std::nullopt});
    done.body.statements.push_back(
        CoreStmt{CoreCapabilityCallStmt{CoreValueId{1},
                                        CoreCapabilityId{0},
                                        "Echo",
                                        {CoreValueId{0}}},
                 std::nullopt});
    done.body.statements.push_back(
        CoreStmt{CoreReturnStmt{true, CoreValueId{1}}, std::nullopt});
    flow.states.push_back(std::move(done));
    CoreFlowState start;
    start.state = CoreStateId{1};
    start.state_name = "Start";
    start.body.statements.push_back(
        CoreStmt{CoreGotoStmt{CoreStateId{0}, "Done"}, std::nullopt});
    flow.states.push_back(std::move(start));
    program.flows.push_back(std::move(flow));
    return program;
}

static ahfl::ir::core::CoreProgram make_e3_workflow_program() {
    using namespace ahfl::ir;
    using namespace ahfl::ir::core;
    CoreProgram program;

    CoreTypeDecl frame;
    frame.kind = CoreTypeDecl::Kind::Struct;
    frame.name = "app::Frame";
    frame.symbol_ref = {SymbolRefKind::Type, "app::Frame", "Frame", "app", 200};
    program.types.push_back(std::move(frame));
    const CoreValueTypeId frame_value{0};
    const CoreValueTypeId unit_value{1};
    program.value_types.push_back(
        CoreValueType{CoreVtNominal{CoreTypeId{0}, {}, std::nullopt}});
    program.value_types.push_back(CoreValueType{CoreVtUnit{}});

    const auto add_identity_agent = [&](std::string name, std::size_t symbol_id) {
        const auto agent_id = CoreAgentId{static_cast<std::uint32_t>(program.agents.size())};
        CoreAgentDecl agent;
        agent.name = name;
        agent.symbol_ref = {SymbolRefKind::Agent,
                            "app::" + name,
                            name,
                            "app",
                            symbol_id};
        agent.states = {"Done", "Start"};
        agent.initial = CoreStateId{1};
        agent.finals = {CoreStateId{0}};
        agent.transitions = {{CoreStateId{1}, CoreStateId{0}}};
        agent.input_type = CoreTypeId{0};
        agent.output_type = CoreTypeId{0};
        agent.context_kind = CoreAgentDecl::ContextKind::Unit;
        program.agents.push_back(std::move(agent));

        CoreFlowDecl flow;
        flow.target = agent_id;
        flow.agent_name = name;
        CorePathExpr input;
        input.root = CorePathRoot::Input;
        input.root_name = "input";
        input.root_type = CoreTypeId{0};
        flow.exprs.push_back(CoreExpr{std::move(input), std::nullopt, frame_value});
        flow.value_count = 1;
        flow.value_types = {frame_value};
        CoreFlowState done;
        done.state = CoreStateId{0};
        done.state_name = "Done";
        done.body.statements.push_back(
            CoreStmt{CoreLetStmt{CoreValueId{0}, CoreExprId{0}}, std::nullopt});
        done.body.statements.push_back(
            CoreStmt{CoreReturnStmt{true, CoreValueId{0}}, std::nullopt});
        flow.states.push_back(std::move(done));
        CoreFlowState start;
        start.state = CoreStateId{1};
        start.state_name = "Start";
        start.body.statements.push_back(
            CoreStmt{CoreGotoStmt{CoreStateId{0}, "Done"}, std::nullopt});
        flow.states.push_back(std::move(start));
        program.flows.push_back(std::move(flow));

        CoreInstanceDecl instance;
        instance.id = CoreInstanceId{static_cast<std::uint32_t>(program.instances.size())};
        instance.instance_key = "_inst_" + name;
        instance.origin = program.agents[agent_id.value].symbol_ref;
        instance.dispatch_types = {frame_value, unit_value, frame_value};
        instance.payload = CoreAgentInstance{agent_id,
                                             CoreTypeId{0},
                                             CoreAgentDecl::ContextKind::Unit,
                                             CoreTypeId{},
                                             CoreTypeId{0}};
        program.instances.push_back(std::move(instance));
    };
    add_identity_agent("First", 201);
    add_identity_agent("Second", 202);

    CoreWorkflowDecl workflow;
    workflow.id = CoreWorkflowId{0};
    workflow.name = "IdentityPipeline";
    workflow.symbol_ref = {SymbolRefKind::Workflow,
                           "app::IdentityPipeline",
                           "IdentityPipeline",
                           "app",
                           203};
    workflow.input_type = CoreTypeId{0};
    workflow.output_type = CoreTypeId{0};
    workflow.value_count = 3;
    workflow.value_types = {frame_value, frame_value, frame_value};

    CorePathExpr input;
    input.root = CorePathRoot::WorkflowInput;
    input.root_name = "input";
    input.root_type = CoreTypeId{0};
    workflow.exprs.push_back(CoreExpr{std::move(input), std::nullopt, frame_value});
    CorePathExpr first_output;
    first_output.root = CorePathRoot::WorkflowNodeOutput;
    first_output.root_name = "first";
    first_output.root_type = CoreTypeId{0};
    first_output.workflow_node = CoreWorkflowNodeId{0};
    workflow.exprs.push_back(
        CoreExpr{std::move(first_output), std::nullopt, frame_value});
    CorePathExpr second_output;
    second_output.root = CorePathRoot::WorkflowNodeOutput;
    second_output.root_name = "second";
    second_output.root_type = CoreTypeId{0};
    second_output.workflow_node = CoreWorkflowNodeId{1};
    workflow.exprs.push_back(
        CoreExpr{std::move(second_output), std::nullopt, frame_value});

    const auto yielding_region = [](CoreExprId expr, CoreValueId value) {
        auto region = std::make_unique<CoreRegion>();
        region->statements.push_back(CoreStmt{CoreLetStmt{value, expr}, std::nullopt});
        region->statements.push_back(CoreStmt{CoreYieldStmt{true, value}, std::nullopt});
        return region;
    };
    CoreWorkflowNode first;
    first.id = CoreWorkflowNodeId{0};
    first.node_name = "first";
    first.target_instance = CoreInstanceId{0};
    first.input_region = yielding_region(CoreExprId{0}, CoreValueId{0});
    workflow.nodes.push_back(std::move(first));
    CoreWorkflowNode second;
    second.id = CoreWorkflowNodeId{1};
    second.node_name = "second";
    second.target_instance = CoreInstanceId{1};
    second.after = {CoreWorkflowNodeId{0}};
    second.input_region = yielding_region(CoreExprId{1}, CoreValueId{1});
    workflow.nodes.push_back(std::move(second));
    workflow.return_region = yielding_region(CoreExprId{2}, CoreValueId{2});
    program.workflows.push_back(std::move(workflow));
    return program;
}

// A linear-chain workflow of exactly `node_count` nodes, ALL targeting a single
// packaged capability-bearing instance (packaged_instances stays 1; a repeated
// capability across nodes is legal), node i reading node i-1's output (node 0
// reads the workflow input). Drives the node-event region's compile-time capacity
// boundary (N=1612 fits the 64 KiB page at heap_base 65512; N=1613 is
// RESOURCE_EXHAUSTED) through the real public emitter, exercising the actual plan
// carriers + preflight, not a copied formula.
static ahfl::ir::core::CoreProgram make_n_node_capability_workflow(std::uint32_t node_count) {
    using namespace ahfl::ir;
    using namespace ahfl::ir::core;
    CoreProgram program = make_e3_workflow_program();
    const CoreValueTypeId frame_value{0};

    // Turn the first packaged instance's flow into a single reachable capability
    // call so every node that targets it is a capability node.
    CoreCapabilityDecl cap;
    cap.name = "Echo";
    cap.symbol_ref = {SymbolRefKind::Capability, "app::Echo", "Echo", "app", 350};
    cap.param_types = {frame_value};
    cap.return_type = frame_value;
    program.capabilities.push_back(std::move(cap));
    program.agents[0].capabilities = {CoreCapabilityId{0}};
    auto &cap_flow = program.flows[0];
    cap_flow.value_count = 2;
    cap_flow.value_types = {frame_value, frame_value};
    auto &cap_statements = cap_flow.states[0].body.statements;
    cap_statements.clear();
    cap_statements.push_back(CoreStmt{CoreLetStmt{CoreValueId{0}, CoreExprId{0}}, std::nullopt});
    cap_statements.push_back(
        CoreStmt{CoreCapabilityCallStmt{CoreValueId{1}, CoreCapabilityId{0}, "Echo",
                                        {CoreValueId{0}}},
                 std::nullopt});
    cap_statements.push_back(CoreStmt{CoreReturnStmt{true, CoreValueId{1}}, std::nullopt});

    CoreWorkflowDecl workflow;
    workflow.id = CoreWorkflowId{0};
    workflow.name = "IdentityPipeline";
    workflow.symbol_ref = {SymbolRefKind::Workflow, "app::IdentityPipeline",
                           "IdentityPipeline", "app", 203};
    workflow.input_type = CoreTypeId{0};
    workflow.output_type = CoreTypeId{0};
    workflow.value_count = node_count + 1u;
    workflow.value_types.assign(node_count + 1u, frame_value);

    CorePathExpr input;
    input.root = CorePathRoot::WorkflowInput;
    input.root_name = "input";
    input.root_type = CoreTypeId{0};
    workflow.exprs.push_back(CoreExpr{std::move(input), std::nullopt, frame_value});
    for (std::uint32_t i = 1; i <= node_count; ++i) {
        CorePathExpr out;
        out.root = CorePathRoot::WorkflowNodeOutput;
        out.root_name = "n" + std::to_string(i - 1);
        out.root_type = CoreTypeId{0};
        out.workflow_node = CoreWorkflowNodeId{i - 1};
        workflow.exprs.push_back(CoreExpr{std::move(out), std::nullopt, frame_value});
    }

    const auto yielding_region = [](CoreExprId expr, CoreValueId value) {
        auto region = std::make_unique<CoreRegion>();
        region->statements.push_back(CoreStmt{CoreLetStmt{value, expr}, std::nullopt});
        region->statements.push_back(CoreStmt{CoreYieldStmt{true, value}, std::nullopt});
        return region;
    };
    for (std::uint32_t i = 0; i < node_count; ++i) {
        CoreWorkflowNode node;
        node.id = CoreWorkflowNodeId{i};
        node.node_name = "n" + std::to_string(i);
        node.target_instance = CoreInstanceId{0};
        if (i > 0) {
            node.after = {CoreWorkflowNodeId{i - 1}};
        }
        node.input_region = yielding_region(CoreExprId{i}, CoreValueId{i});
        workflow.nodes.push_back(std::move(node));
    }
    workflow.return_region =
        yielding_region(CoreExprId{node_count}, CoreValueId{node_count});
    program.workflows.clear();
    program.workflows.push_back(std::move(workflow));
    return program;
}

static bool has_codegen_code(const ahfl::backends::CoreWasmCodegenResult &result,
                             std::string_view code) {
    for (const auto &diagnostic : result.diagnostics) {
        if (diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

static bool has_codegen_message(const ahfl::backends::CoreWasmCodegenResult &result,
                                std::string_view needle) {
    for (const auto &diagnostic : result.diagnostics) {
        if (diagnostic.message.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

static std::optional<std::uint32_t> read_u32_leb(const std::vector<std::uint8_t> &bytes,
                                                std::size_t &offset) {
    std::uint32_t value = 0;
    for (std::uint32_t shift = 0; shift < 35 && offset < bytes.size(); shift += 7) {
        const auto byte = bytes[offset++];
        value |= static_cast<std::uint32_t>(byte & 0x7fu) << shift;
        if ((byte & 0x80u) == 0) {
            return value;
        }
    }
    return std::nullopt;
}

static std::optional<std::vector<std::uint8_t>> wasm_function_body(
    const std::vector<std::uint8_t> &bytes, std::uint32_t function_index) {
    std::size_t offset = 8;
    while (offset < bytes.size()) {
        const auto section = bytes[offset++];
        const auto size = read_u32_leb(bytes, offset);
        if (!size || *size > bytes.size() - offset) {
            return std::nullopt;
        }
        const auto end = offset + *size;
        if (section != 10) {
            offset = end;
            continue;
        }
        const auto count = read_u32_leb(bytes, offset);
        if (!count || function_index >= *count) {
            return std::nullopt;
        }
        for (std::uint32_t index = 0; index < *count; ++index) {
            const auto body_size = read_u32_leb(bytes, offset);
            if (!body_size || *body_size > end - offset) {
                return std::nullopt;
            }
            if (index == function_index) {
                return std::vector<std::uint8_t>(bytes.begin() +
                                                     static_cast<std::ptrdiff_t>(offset),
                                                 bytes.begin() + static_cast<std::ptrdiff_t>(
                                                                     offset + *body_size));
            }
            offset += *body_size;
        }
        return std::nullopt;
    }
    return std::nullopt;
}

static std::optional<std::vector<std::uint8_t>> wasm_section(
    const std::vector<std::uint8_t> &bytes, std::uint8_t wanted) {
    std::size_t offset = 8;
    while (offset < bytes.size()) {
        const auto section = bytes[offset++];
        const auto size = read_u32_leb(bytes, offset);
        if (!size || *size > bytes.size() - offset) {
            return std::nullopt;
        }
        if (section == wanted) {
            return std::vector<std::uint8_t>(
                bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                bytes.begin() + static_cast<std::ptrdiff_t>(offset + *size));
        }
        offset += *size;
    }
    return std::nullopt;
}

// RFC 0026 E4-B1: count the module's custom sections and, for the sole trailing
// wire-schema section, strip its Wasm name framing (name-length LEB + name) to
// hand the raw `AHFLWS...` table bytes to the C1 decoder. Returns the number of
// custom sections observed and, when exactly one carries the wire-schema name,
// its raw table payload.
struct WireSchemaCustomSection {
    std::size_t custom_count = 0;
    std::optional<std::vector<std::uint8_t>> table_bytes;
};

static WireSchemaCustomSection wire_schema_custom_section(
    const std::vector<std::uint8_t> &bytes) {
    WireSchemaCustomSection found;
    std::size_t offset = 8;
    while (offset < bytes.size()) {
        const auto section = bytes[offset++];
        const auto size = read_u32_leb(bytes, offset);
        if (!size || *size > bytes.size() - offset) {
            return found;
        }
        const auto section_end = offset + *size;
        if (section != 0) {
            offset = section_end;
            continue;
        }
        ++found.custom_count;
        auto name_offset = offset;
        const auto name_size = read_u32_leb(bytes, name_offset);
        if (name_size && name_offset + *name_size <= section_end) {
            const std::string name(
                bytes.begin() + static_cast<std::ptrdiff_t>(name_offset),
                bytes.begin() + static_cast<std::ptrdiff_t>(name_offset + *name_size));
            if (name == "ahfl.wire-schema.v1") {
                found.table_bytes = std::vector<std::uint8_t>(
                    bytes.begin() +
                        static_cast<std::ptrdiff_t>(name_offset + *name_size),
                    bytes.begin() + static_cast<std::ptrdiff_t>(section_end));
            }
        }
        offset = section_end;
    }
    return found;
}

static bool contains_bytes(const std::vector<std::uint8_t> &bytes,
                           std::initializer_list<std::uint8_t> needle) {
    return std::search(bytes.begin(), bytes.end(), needle.begin(), needle.end()) != bytes.end();
}

static std::vector<std::uint32_t>
small_fixture_call_indices(const std::vector<std::uint8_t> &body) {
    std::vector<std::uint32_t> calls;
    for (std::size_t offset = 0; offset < body.size(); ++offset) {
        if (body[offset] != 0x10) {
            continue;
        }
        ++offset;
        const auto index = read_u32_leb(body, offset);
        if (!index.has_value()) {
            return {};
        }
        calls.push_back(*index);
        --offset;
    }
    return calls;
}

static bool rejects_as_unsupported(const ahfl::ir::core::CoreProgram &program) {
    const auto verified = ahfl::ir::core::verify_core_program(program);
    const auto layout = ahfl::ir::core::compute_core_layouts(program);
    if (!verified.ok() || !layout.ok() || !layout.table.has_value()) {
        return false;
    }
    const auto emitted = ahfl::backends::emit_core_wasm(
        program,
        *layout.table,
        {ahfl::ir::core::CoreAgentId{0}, ahfl::backends::WasmProfileKind::Wasi});
    return !emitted.artifact.has_value() &&
           has_codegen_code(emitted,
                            ahfl::backends::core_wasm_diag::kUnsupportedOrchestration);
}

static bool rejects_capability_frame(const ahfl::ir::core::CoreProgram &program) {
    const auto verified = ahfl::ir::core::verify_core_program(program);
    const auto layout = ahfl::ir::core::compute_core_layouts(program);
    if (!verified.ok() || !layout.ok() || !layout.table.has_value()) {
        return false;
    }
    const auto emitted = ahfl::backends::emit_core_wasm(
        program,
        *layout.table,
        {ahfl::ir::core::CoreAgentId{0}, ahfl::backends::WasmProfileKind::Wasi});
    return !emitted.artifact.has_value() &&
           has_codegen_code(
               emitted,
               ahfl::backends::core_wasm_diag::kUnsupportedCapabilityFrame);
}

static bool same_e1_program(const ahfl::ir::core::CoreProgram &lhs,
                            const ahfl::ir::core::CoreProgram &rhs) {
    if (lhs.format_version != rhs.format_version || lhs.types != rhs.types ||
        lhs.value_types != rhs.value_types || lhs.flows != rhs.flows ||
        lhs.instances != rhs.instances || lhs.capabilities != rhs.capabilities ||
        lhs.workflows.size() != rhs.workflows.size() || lhs.agents != rhs.agents) {
        return false;
    }
    return true;
}

// RFC 0026 P6 (KR6.6): canonical signed-LEB128 byte vectors. Each tuple is
// (signed value, minimal byte sequence) for the s32/s64 immediates backing
// i32.const / i64.const. A value-roundtrip decode cannot catch an overlong
// negative encoding (it still decodes to the same number), so the assertions
// compare BYTE-FOR-BYTE and pin the minimal length — strict wasm validation
// rejects unused sign bits in the final byte.
struct SlebCase {
    std::int64_t value;
    std::initializer_list<std::uint8_t> s32_bytes;
    std::initializer_list<std::uint8_t> s64_bytes;
};

static void run_sleb128_tests() {
    using ahfl::backends::detail::ByteBuffer;
    const SlebCase cases[] = {
        {-1, {0x7f}, {0x7f}},
        {-64, {0x40}, {0x40}},
        {-65, {0xbf, 0x7f}, {0xbf, 0x7f}},
        {-128, {0x80, 0x7f}, {0x80, 0x7f}},
        {-129, {0xff, 0x7e}, {0xff, 0x7e}},
        {-100000, {0xe0, 0xf2, 0x79}, {0xe0, 0xf2, 0x79}},
        {std::numeric_limits<std::int32_t>::min(),
         {0x80, 0x80, 0x80, 0x80, 0x78},
         {0x80, 0x80, 0x80, 0x80, 0x78}},
        {std::numeric_limits<std::int32_t>::max(),
         {0xff, 0xff, 0xff, 0xff, 0x07},
         {0xff, 0xff, 0xff, 0xff, 0x07}},
        {std::numeric_limits<std::int64_t>::min(),
         {},
         {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x7f}},
        {std::numeric_limits<std::int64_t>::max(),
         {},
         {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00}},
        {0, {0x00}, {0x00}},
        {1, {0x01}, {0x01}},
        {63, {0x3f}, {0x3f}},
        {64, {0xc0, 0x00}, {0xc0, 0x00}},
        {127, {0xff, 0x00}, {0xff, 0x00}},
        {128, {0x80, 0x01}, {0x80, 0x01}},
    };
    for (const auto &tc : cases) {
        if (tc.s32_bytes.size() != 0 &&
            tc.value >= std::numeric_limits<std::int32_t>::min() &&
            tc.value <= std::numeric_limits<std::int32_t>::max()) {
            ByteBuffer buffer;
            buffer.s32(static_cast<std::int32_t>(tc.value));
            const auto encoded = std::move(buffer).take();
            check(std::equal(tc.s32_bytes.begin(), tc.s32_bytes.end(),
                             encoded.begin(), encoded.end()) &&
                      encoded.size() == tc.s32_bytes.size(),
                  "ByteBuffer::s32 emits canonical minimal signed-LEB128 bytes");
        }
        {
            ByteBuffer buffer;
            buffer.s64(tc.value);
            const auto encoded = std::move(buffer).take();
            check(std::equal(tc.s64_bytes.begin(), tc.s64_bytes.end(),
                             encoded.begin(), encoded.end()) &&
                      encoded.size() == tc.s64_bytes.size(),
                  "ByteBuffer::s64 emits canonical minimal signed-LEB128 bytes");
        }
    }
}

int main() {
    std::printf("=== WASM Backend Tests ===\n\n");

    run_sleb128_tests();

    // Test 1: generate_wasm produces valid WAT with module header
    {
        ahfl::backends::WasmAgentConfig config;
        config.agent_name = "test_agent";
        config.states = {"idle", "running", "done"};
        config.transitions = {{"idle", "running"}, {"running", "done"}};
        config.capabilities = {"http_call"};
        config.capability_ids = {7};

        auto mod = ahfl::backends::generate_wasm(config);

        bool has_module = mod.wat_source.find("(module $test_agent") != std::string::npos;
        bool has_export = mod.wat_source.find("(export \"get_state\")") != std::string::npos;
        bool has_transition = mod.wat_source.find("(export \"transition\")") != std::string::npos;

        check(has_module && has_export && has_transition,
              "generate_wasm produces valid WAT with module header");
    }

    // Test 2: generate_wasi_imports produces import section for allowed capabilities
    {
        ahfl::backends::WasiConfig wasi_config;
        wasi_config.allowed_capabilities = {
            ahfl::backends::WasiCapability::FileRead,
            ahfl::backends::WasiCapability::NetworkAccess
        };

        auto imports = ahfl::backends::generate_wasi_imports(wasi_config);

        bool has_fd_read = imports.find("fd_read") != std::string::npos;
        bool has_sock = imports.find("sock_accept") != std::string::npos;
        bool has_wasi_import = imports.find("wasi_snapshot_preview1") != std::string::npos;

        check(has_fd_read && has_sock && has_wasi_import,
              "generate_wasi_imports produces import section for allowed capabilities");
    }

    // Test 3: generate_wasm with empty agent produces minimal module
    {
        ahfl::backends::WasmAgentConfig config;
        config.agent_name = "empty_agent";

        auto mod = ahfl::backends::generate_wasm(config);

        bool has_module = mod.wat_source.find("(module $empty_agent") != std::string::npos;
        bool has_memory = mod.wat_source.find("(memory (export \"memory\") 1)") != std::string::npos;
        bool no_states = mod.wat_source.find("state table: 0 states") != std::string::npos;

        check(has_module && has_memory && no_states,
              "generate_wasm with empty agent produces minimal module");
    }

    // Test 4 (RFC 0019 slice 1): the stable host ABI is emitted and exported.
    {
        ahfl::backends::WasmAgentConfig config;
        config.agent_name = "abi_agent";
        config.states = {"init", "done"};
        config.transitions = {{"init", "done"}};

        auto mod = ahfl::backends::generate_wasm(config);
        const auto &wat = mod.wat_source;

        bool has_version =
            wat.find("(global $ahfl_abi_version (export \"ahfl_abi_version\") i32") !=
            std::string::npos;
        bool has_alloc = wat.find("(func $alloc (export \"alloc\")") != std::string::npos;
        bool has_dealloc = wat.find("(func $dealloc (export \"dealloc\")") != std::string::npos;
        bool has_run = wat.find("(func $run (export \"run\")") != std::string::npos;
        bool has_step = wat.find("(func $step (export \"step\")") != std::string::npos;
        bool has_current =
            wat.find("(func $current_state (export \"current_state\")") != std::string::npos;

        check(has_version && has_alloc && has_dealloc && has_run && has_step && has_current,
              "generate_wasm emits the RFC 0019 stable host ABI");

        // The module's registered export list mirrors the emitted ABI.
        const auto has_export = [&](const std::string &name) {
            for (const auto &e : mod.exports) {
                if (e == name) {
                    return true;
                }
            }
            return false;
        };
        check(has_export("ahfl_abi_version") && has_export("alloc") && has_export("dealloc") &&
                  has_export("run") && has_export("step") && has_export("current_state"),
              "module export list registers the ABI symbols");
    }

    // Test 5 (RFC 0019 slice 1): the ABI contract catalogue is the single SoT.
    {
        auto abi = ahfl::backends::wasm_abi_exports();
        bool has_run = false;
        bool has_alloc = false;
        for (const auto &e : abi) {
            if (e.name == "run") {
                has_run = true;
            }
            if (e.name == "alloc") {
                has_alloc = true;
            }
        }
        check(abi.size() == 5 && has_run && has_alloc &&
                  ahfl::backends::WasmAbiContract::kVersion == 1 &&
                  ahfl::backends::WasmAbiContract::kFrameFormat == "value_json",
              "wasm_abi_exports catalogue + WasmAbiContract constants");
    }

    // Test 6 (RFC 0019 slice 2): least-privilege effect -> WASI projection.
    {
        using ahfl::backends::WasmCapabilityEffect;
        using ahfl::backends::WasiCapability;

        auto has_cap = [](const ahfl::backends::WasiConfig &c, WasiCapability cap) {
            for (auto v : c.allowed_capabilities) {
                if (v == cap) {
                    return true;
                }
            }
            return false;
        };

        // Pure agent (no capabilities) -> zero WASI capability.
        auto pure = ahfl::backends::project_wasi_config({});
        check(pure.allowed_capabilities.empty(), "pure agent projects to zero WASI capability");

        // Read-only capability -> still empty (not a WASI resource).
        auto read_only = ahfl::backends::project_wasi_config({WasmCapabilityEffect::Read});
        check(read_only.allowed_capabilities.empty(),
              "read-only capability projects to zero WASI capability");

        // External side effect -> NetworkAccess (external-effect channel).
        auto external =
            ahfl::backends::project_wasi_config({WasmCapabilityEffect::ExternalSideEffect});
        check(external.allowed_capabilities.size() == 1 &&
                  has_cap(external, WasiCapability::NetworkAccess),
              "external side effect projects to NetworkAccess");

        // Unknown effect -> conservative NetworkAccess.
        auto unknown = ahfl::backends::project_wasi_config({WasmCapabilityEffect::Unknown});
        check(has_cap(unknown, WasiCapability::NetworkAccess),
              "unknown effect conservatively projects to NetworkAccess");

        // Mixed read + durable write -> single NetworkAccess (deduped severity).
        auto mixed = ahfl::backends::project_wasi_config(
            {WasmCapabilityEffect::Read, WasmCapabilityEffect::DurableWrite});
        check(mixed.allowed_capabilities.size() == 1 &&
                  has_cap(mixed, WasiCapability::NetworkAccess),
              "mixed read + durable write projects to a single NetworkAccess");
    }

    // Test 7 (RFC 0019 slice 3): capability imports use the ahfl_cap ABI and
    // are named by SymbolId (index-based identity), not the source name.
    {
        ahfl::backends::WasmAgentConfig config;
        config.agent_name = "cap_agent";
        config.states = {"init", "done"};
        config.transitions = {{"init", "done"}};
        config.capabilities = {"http_call"};
        config.capability_ids = {42};

        auto mod = ahfl::backends::generate_wasm(config);
        const auto &wat = mod.wat_source;

        bool has_ahfl_cap =
            wat.find("(import \"ahfl_cap\" \"cap_42\" (func $cap_42 (param i32 i32) (result i32 i32 i32)))") !=
            std::string::npos;
        bool no_env_import = wat.find("(import \"env\"") == std::string::npos;
        bool no_source_name = wat.find("http_call") == std::string::npos;

        check(has_ahfl_cap && no_env_import,
              "capability import uses the ahfl_cap ABI named by SymbolId");
        check(no_source_name, "capability import does not leak the source name");

        bool import_registered = false;
        for (const auto &imp : mod.imports) {
            if (imp == "ahfl_cap.cap_42") {
                import_registered = true;
            }
        }
        check(import_registered, "module import list registers the ahfl_cap symbol");
    }

    // Test 8 (RFC 0019 slice 4/5): profiled generation + browser boundary.
    {
        using ahfl::backends::WasmCapabilityEffect;
        using ahfl::backends::WasiCapability;
        using ahfl::backends::WasmProfileKind;

        // Browser support predicate: network/clock yes, filesystem/env no.
        check(ahfl::backends::wasi_capability_browser_supported(WasiCapability::NetworkAccess) &&
                  ahfl::backends::wasi_capability_browser_supported(WasiCapability::ClockAccess),
              "network and clock are browser-supported");
        check(!ahfl::backends::wasi_capability_browser_supported(WasiCapability::FileRead) &&
                  !ahfl::backends::wasi_capability_browser_supported(WasiCapability::FileWrite) &&
                  !ahfl::backends::wasi_capability_browser_supported(WasiCapability::EnvironmentVars),
              "filesystem and environment are not browser-supported");

        // A network-effect agent has no browser-rejected capabilities (fetch).
        ahfl::backends::WasmAgentConfig net_agent;
        net_agent.agent_name = "net";
        net_agent.states = {"init", "done"};
        net_agent.transitions = {{"init", "done"}};
        net_agent.capabilities = {"FetchUrl"};
        net_agent.capability_effects = {WasmCapabilityEffect::ExternalSideEffect};
        net_agent.capability_ids = {1};
        check(ahfl::backends::browser_rejected_capabilities(net_agent).empty(),
              "network-effect capability is allowed under the browser profile");

        // Profiled generation is ABI-identical (profile only affects host
        // imports / browser restrictions, not the module ABI).
        auto wasi_mod = ahfl::backends::generate_wasm(net_agent, WasmProfileKind::Wasi);
        auto browser_mod = ahfl::backends::generate_wasm(net_agent, WasmProfileKind::Browser);
        check(wasi_mod.wat_source == browser_mod.wat_source,
              "module ABI is profile-independent");
    }

    // Test 9 (RFC 0026 KR6.5 E1): deterministic Core+layout -> binary wasm.
    // This is an always-on BINARY/STRUCTURAL probe, not real execution
    // evidence (the optional wasmtime conformance test owns that claim).
    {
        using namespace ahfl::ir::core;
        const auto program = make_e1_core_program();
        const auto program_snapshot = make_e1_core_program();
        const auto core_verified = verify_core_program(program);
        const auto layout = compute_core_layouts(program);
        check(core_verified.ok() && layout.ok(), "E1 fixture is verified Core + P4-D layout");
        if (layout.table.has_value()) {
            const auto layout_snapshot = *layout.table;
            const auto first = ahfl::backends::emit_core_wasm(
                program, *layout.table, {ahfl::ir::core::CoreAgentId{0}, ahfl::backends::WasmProfileKind::Wasi});
            const auto second = ahfl::backends::emit_core_wasm(
                program, *layout.table, {ahfl::ir::core::CoreAgentId{0}, ahfl::backends::WasmProfileKind::Wasi});
            const auto browser = ahfl::backends::emit_core_wasm(
                program, *layout.table, {ahfl::ir::core::CoreAgentId{0}, ahfl::backends::WasmProfileKind::Browser});
            const bool magic = first.ok() && first.artifact->bytes.size() >= 8 &&
                               first.artifact->bytes[0] == 0x00 &&
                               first.artifact->bytes[1] == 0x61 &&
                               first.artifact->bytes[2] == 0x73 &&
                               first.artifact->bytes[3] == 0x6d;
            check(magic, "E1 emits a wasm binary (magic/version), not textual WAT");
            check(first.ok() && second.ok() &&
                      first.artifact->bytes == second.artifact->bytes,
                  "E1 binary emission is byte-deterministic");
            check(first.ok() && browser.ok() &&
                      first.artifact->bytes == browser.artifact->bytes,
                  "E1 no-import wasi/browser binaries are byte-identical");
            check(same_e1_program(program, program_snapshot) &&
                      *layout.table == layout_snapshot,
                  "E1 emission does not mutate Core or the P4-D side artifact");

            // RFC 0026 E4-B1: an identity-only E1 agent has no reachable
            // capability import, so it carries no wire-schema (or any) custom
            // section and its imports set is empty.
            const auto e1_custom = first.ok()
                                       ? wire_schema_custom_section(first.artifact->bytes)
                                       : WireSchemaCustomSection{};
            check(first.ok() && first.artifact->imports.empty() &&
                      e1_custom.custom_count == 0 &&
                      !e1_custom.table_bytes.has_value(),
                  "E1 no-import artifact carries no custom section at all");

            // Function index 5 is run by the fixed E1 index table. Its body
            // must end in local.get 0; end and contain no frame load/store.
            // This is BINARY/STRUCTURAL evidence, not execution evidence.
            const auto run_body = first.ok()
                                      ? wasm_function_body(first.artifact->bytes, 5)
                                      : std::optional<std::vector<std::uint8_t>>{};
            const bool returns_arg_zero =
                run_body && run_body->size() >= 3 &&
                (*run_body)[run_body->size() - 3] == 0x20 &&
                (*run_body)[run_body->size() - 2] == 0x00 &&
                run_body->back() == 0x0b;
            const bool has_frame_memory_op =
                run_body && std::any_of(run_body->begin(), run_body->end(), [](std::uint8_t op) {
                    return op >= 0x28 && op <= 0x3e;
                });
            check(returns_arg_zero && !has_frame_memory_op,
                  "E1 run identity alias is structurally local.get 0 with no frame memory op");

            const auto run2_body = first.ok()
                                       ? wasm_function_body(first.artifact->bytes, 6)
                                       : std::optional<std::vector<std::uint8_t>>{};
            const bool run2_identity =
                run2_body && contains_bytes(*run2_body,
                                            {0x41, 0x00, 0x20, 0x00,
                                             0x20, 0x01, 0x0f});
            const bool run2_has_frame_memory_op =
                run2_body && std::any_of(run2_body->begin(),
                                         run2_body->end(),
                                         [](std::uint8_t op) {
                                             return op >= 0x28 && op <= 0x3e;
                                         });
            check(run2_identity && !run2_has_frame_memory_op,
                  "E2 run2 identity path returns OK,input_ptr,input_len without frame access");

            auto tampered = *layout.table;
            tampered.target.pointer_size = 8;
            const auto bad_layout = ahfl::backends::emit_core_wasm(
                program, tampered, {ahfl::ir::core::CoreAgentId{0}, ahfl::backends::WasmProfileKind::Wasi});
            check(!bad_layout.ok() &&
                      has_codegen_code(bad_layout,
                                       ahfl::backends::core_wasm_diag::kInvalidLayout),
                  "E1 rejects a tampered P4-D layout with no artifact");
        }
    }

    // Test 10 (RFC 0026 KR6.5 E1 rev2): every deviation from the one
    // canonical opaque identity return fails closed with no artifact.
    {
        using namespace ahfl::ir::core;

        auto member_projection = make_e1_core_program();
        CoreTypeDecl nested;
        nested.kind = CoreTypeDecl::Kind::Struct;
        nested.name = "app::Nested";
        member_projection.types.push_back(std::move(nested));
        member_projection.value_types.push_back(
            CoreValueType{CoreVtNominal{CoreTypeId{1}, {}, std::nullopt}});
        auto &frame = member_projection.types[0];
        frame.fields = {"nested"};
        frame.field_nominal_types = {CoreTypeId{1}};
        frame.field_has_default = {false};
        CoreMemberTypeTemplateNode nested_template;
        nested_template.kind = CoreMemberTypeTemplateKind::Concrete;
        nested_template.concrete = CoreValueTypeId{1};
        frame.member_type_templates = {nested_template};
        frame.field_type_template_roots = {CoreMemberTypeTemplateNodeId{0}};
        auto &member_expr = member_projection.flows[0].exprs[0];
        auto &member_path = std::get<CorePathExpr>(member_expr.node);
        member_path.members = {"nested"};
        member_path.projection = {
            CoreProjectionStep{CoreTypeId{0}, CoreFieldId{0}, CoreTypeId{1}}};
        member_expr.result_type = CoreValueTypeId{1};
        member_projection.flows[0].value_types[0] = CoreValueTypeId{1};
        check(rejects_as_unsupported(member_projection),
              "E1 rejects a real member projection with no artifact");

        auto literal = make_e1_core_program();
        literal.value_types.push_back(
            CoreValueType{CoreVtInt{std::make_pair<std::int64_t, std::int64_t>(1, 1)}});
        literal.flows[0].exprs[0].node = CoreLiteralExpr{CoreLiteralKind::Integer, "1"};
        literal.flows[0].exprs[0].result_type = CoreValueTypeId{1};
        literal.flows[0].value_types[0] = CoreValueTypeId{1};
        check(rejects_as_unsupported(literal),
              "E1 rejects a literal return with no artifact");

        auto construct = make_e1_core_program();
        CoreConstructExpr construction;
        construction.type_name = "app::Input";
        construction.type_id = CoreTypeId{0};
        construction.resolved = true;
        construct.flows[0].exprs[0].node = std::move(construction);
        check(rejects_as_unsupported(construct),
              "E1 rejects a constructed return with no artifact");

        auto output_mismatch = make_e1_core_program();
        CoreTypeDecl other_output;
        other_output.kind = CoreTypeDecl::Kind::Struct;
        other_output.name = "app::OtherOutput";
        output_mismatch.types.push_back(std::move(other_output));
        output_mismatch.agents[0].output_type = CoreTypeId{1};
        check(rejects_as_unsupported(output_mismatch),
              "E1 rejects different input/output declaration types with no artifact");

        auto coercion = make_e1_core_program();
        coercion.value_types.push_back(CoreValueType{CoreVtInt{
            std::make_pair<std::int64_t, std::int64_t>(0, 0)}});
        coercion.value_types.push_back(CoreValueType{CoreVtInt{std::nullopt}});
        auto &coercion_flow = coercion.flows[0];
        coercion_flow.exprs[0].node = CoreLiteralExpr{CoreLiteralKind::Integer, "0"};
        coercion_flow.exprs[0].result_type = CoreValueTypeId{1};
        coercion_flow.exprs.push_back(
            CoreExpr{CoreCoerceExpr{CoreValueId{0}, CoreCoercionPlanId{0}},
                     std::nullopt,
                     CoreValueTypeId{2}});
        coercion_flow.value_count = 2;
        coercion_flow.value_types = {CoreValueTypeId{1}, CoreValueTypeId{2}};
        CoreCoercionOp widen;
        widen.kind = CoreCoercionOpKind::IntWiden;
        widen.child = CoreCoercionPlanId{CoreCoercionPlanId::kInvalid};
        coercion_flow.coercion_plans = {
            CoreCoercionPlanNode{CoreValueTypeId{1}, CoreValueTypeId{2}, {widen}}};
        auto &final_statements = coercion_flow.states[0].body.statements;
        final_statements.clear();
        final_statements.push_back(
            CoreStmt{CoreLetStmt{CoreValueId{0}, CoreExprId{0}}, std::nullopt});
        final_statements.push_back(
            CoreStmt{CoreLetStmt{CoreValueId{1}, CoreExprId{1}}, std::nullopt});
        final_statements.push_back(
            CoreStmt{CoreReturnStmt{true, CoreValueId{1}}, std::nullopt});
        check(rejects_as_unsupported(coercion),
              "E1 rejects a coercion return with no artifact");

        auto noncanonical_return = make_e1_core_program();
        auto &ret = std::get<CoreReturnStmt>(
            noncanonical_return.flows[0].states[0].body.statements[1].node);
        ret.has_value = false;
        check(rejects_as_unsupported(noncanonical_return),
              "E1 rejects a non-canonical return with no artifact");
    }

    // Test 11 (RFC 0026 KR6.5 E2): the canonical capability final emits the
    // exact ahfl_cap import and append-only run2 ABI. These checks are binary /
    // structural evidence, not host execution evidence.
    {
        using namespace ahfl::ir::core;
        const auto program = make_e2_core_program();
        const auto snapshot = make_e2_core_program();
        const auto verified = verify_core_program(program);
        const auto layout = compute_core_layouts(program);
        check(verified.ok() && layout.ok(),
              "E2 fixture is verified Core + P4-D layout");
        if (layout.table.has_value()) {
            const auto layout_snapshot = *layout.table;
            const auto first = ahfl::backends::emit_core_wasm(
                program, *layout.table, {ahfl::ir::core::CoreAgentId{0}, ahfl::backends::WasmProfileKind::Wasi});
            const auto second = ahfl::backends::emit_core_wasm(
                program, *layout.table, {ahfl::ir::core::CoreAgentId{0}, ahfl::backends::WasmProfileKind::Wasi});
            const auto browser = ahfl::backends::emit_core_wasm(
                program, *layout.table, {ahfl::ir::core::CoreAgentId{0}, ahfl::backends::WasmProfileKind::Browser});
            check(first.ok() && second.ok() && browser.ok() &&
                      first.artifact->bytes == second.artifact->bytes &&
                      first.artifact->bytes == browser.artifact->bytes,
                  "E2 import emission is deterministic and profile-independent");
            check(first.ok() && first.artifact->imports ==
                                    std::vector<std::string>{"ahfl_cap.cap_42"},
                  "E2 artifact registers the exact SymbolId-based import");
            const auto import_section =
                first.ok() ? wasm_section(first.artifact->bytes, 2)
                           : std::optional<std::vector<std::uint8_t>>{};
            check(import_section &&
                      contains_bytes(*import_section,
                                     {0x01, 0x08, 'a', 'h', 'f', 'l', '_', 'c', 'a', 'p',
                                      0x06, 'c', 'a', 'p', '_', '4', '2', 0x00, 0x04}) &&
                      contains_bytes(first.artifact->bytes,
                                     {0x60, 0x02, 0x7f, 0x7f,
                                      0x03, 0x7f, 0x7f, 0x7f}),
                  "E2 binary encodes exact ahfl_cap module/field/multi-value type");
            check(first.ok() &&
                      std::find(first.artifact->exports.begin(),
                                first.artifact->exports.end(),
                                "run2") != first.artifact->exports.end(),
                  "E2 appends run2 without removing legacy exports");

            const auto legacy_run = first.ok()
                                        ? wasm_function_body(first.artifact->bytes, 5)
                                        : std::optional<std::vector<std::uint8_t>>{};
            const auto run2 = first.ok()
                                  ? wasm_function_body(first.artifact->bytes, 6)
                                  : std::optional<std::vector<std::uint8_t>>{};
            check(legacy_run == std::optional<std::vector<std::uint8_t>>{
                                    std::vector<std::uint8_t>{0x00, 0x00, 0x0b}},
                  "E2 capability artifact legacy run traps before every effect");
            check(run2 && contains_bytes(*run2, {0x23, 0x04, 0x04, 0x40, 0x00, 0x0b}) &&
                      contains_bytes(*run2, {0x20, 0x00, 0x20, 0x01, 0x10, 0x00}) &&
                      contains_bytes(*run2, {0x41, 0x01, 0x24, 0x04}) &&
                      contains_bytes(*run2, {0x41, 0x02, 0x41, 0x00, 0x41, 0x00, 0x0f}),
                  "E2 run2 checks pending latch first, calls import once, and latches PENDING");
            check(same_e1_program(program, snapshot) && *layout.table == layout_snapshot,
                  "E2 emission does not mutate Core or the P4-D side artifact");

            auto least_privilege = make_e2_core_program();
            CoreCapabilityDecl unused_capability;
            unused_capability.name = "Unused";
            unused_capability.symbol_ref = {ahfl::ir::SymbolRefKind::Capability,
                                            "app::Unused",
                                            "Unused",
                                            "app",
                                            7};
            unused_capability.param_types = {CoreValueTypeId{0}};
            unused_capability.return_type = CoreValueTypeId{1};
            least_privilege.capabilities.push_back(std::move(unused_capability));
            least_privilege.agents[0].capabilities.push_back(CoreCapabilityId{1});
            least_privilege.agents[0].states.push_back("Unused");
            least_privilege.agents[0].finals.push_back(CoreStateId{2});
            auto &least_flow = least_privilege.flows[0];
            least_flow.exprs.push_back(least_flow.exprs[0]);
            least_flow.value_count = 4;
            least_flow.value_types.insert(least_flow.value_types.end(),
                                          {CoreValueTypeId{0}, CoreValueTypeId{1}});
            CoreFlowState unused;
            unused.state = CoreStateId{2};
            unused.state_name = "Unused";
            unused.body.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{2}, CoreExprId{1}}, std::nullopt});
            unused.body.statements.push_back(
                CoreStmt{CoreCapabilityCallStmt{CoreValueId{3},
                                                CoreCapabilityId{1},
                                                "Unused",
                                                {CoreValueId{2}}},
                         std::nullopt});
            unused.body.statements.push_back(
                CoreStmt{CoreReturnStmt{true, CoreValueId{3}}, std::nullopt});
            least_flow.states.push_back(std::move(unused));
            const auto least_layout = compute_core_layouts(least_privilege);
            const auto least = least_layout.table.has_value()
                                   ? ahfl::backends::emit_core_wasm(
                                         least_privilege,
                                         *least_layout.table,
                                         {ahfl::ir::core::CoreAgentId{0}, ahfl::backends::WasmProfileKind::Wasi})
                                   : ahfl::backends::CoreWasmCodegenResult{};
            check(least.ok() && least.artifact->imports ==
                                    std::vector<std::string>{"ahfl_cap.cap_42"},
                  "E2 omits an unreachable final capability from the import authority set");

            // RFC 0026 E4-B1: the E2 capability artifact carries exactly one
            // wire-schema custom section. This block proves exactly-one target
            // section + that its raw table (name framing stripped) decodes and
            // re-encodes through the C1 authority; the trailing placement and the
            // byte-identical pre-B1 prefix are locked by wasm_e2_binary_gate.py.
            const auto e2_custom = first.ok()
                                       ? wire_schema_custom_section(first.artifact->bytes)
                                       : WireSchemaCustomSection{};
            check(e2_custom.custom_count == 1 && e2_custom.table_bytes.has_value(),
                  "E2 artifact carries exactly one wire-schema custom section");

            if (e2_custom.table_bytes.has_value()) {
                const auto decoded = ahfl::ir::core::decode_core_wire_schema_table(
                    std::span<const std::uint8_t>(*e2_custom.table_bytes));
                check(decoded.ok() && decoded.table.has_value(),
                      "E2 wire-schema payload decodes through the C1 admission authority");
                if (decoded.table.has_value()) {
                    const auto &table = *decoded.table;
                    check(table.format_version == 1,
                          "E2 decoded wire-schema table is format version 1");
                    check(table.capabilities.size() == 1,
                          "E2 decoded table exposes exactly the one reachable capability");
                    if (table.capabilities.size() == 1) {
                        const auto &cap = table.capabilities.front();
                        check(cap.capability.value == 0 && cap.source_symbol == 42 &&
                                  cap.params.size() == 1,
                              "E2 capability entry is id 0 / source_symbol 42 with one param root");
                        // The fixture's Echo capability is app::Input -> app::Output,
                        // both structs. Assert the transported roots resolve to the
                        // exact wire structs, not merely to any in-range node id.
                        const auto struct_wire_name =
                            [&](ahfl::ir::core::CoreWireSchemaNodeId id)
                            -> std::optional<std::string> {
                            if (id.value >= table.nodes.size()) {
                                return std::nullopt;
                            }
                            const auto *s =
                                std::get_if<ahfl::ir::core::CoreWireSchemaStruct>(
                                    &table.nodes[id.value].shape);
                            if (s == nullptr) {
                                return std::nullopt;
                            }
                            return s->wire_name;
                        };
                        const auto param_name =
                            cap.params.size() == 1
                                ? struct_wire_name(cap.params.front())
                                : std::nullopt;
                        const auto result_name = struct_wire_name(cap.result);
                        check(param_name == std::optional<std::string>{"app::Input"} &&
                                  result_name == std::optional<std::string>{"app::Output"},
                              "E2 transported param/result roots are the "
                              "app::Input/app::Output wire structs");
                    }
                    // Re-encode byte equality: the transported bytes are exactly
                    // the canonical encoding of the decoded table.
                    const auto reencoded =
                        ahfl::ir::core::encode_core_wire_schema_table(table);
                    check(reencoded.ok() && reencoded.bytes.has_value() &&
                              *reencoded.bytes == *e2_custom.table_bytes,
                          "E2 wire-schema section is the canonical re-encoding of its table");
                }
            }

            // Least-privilege: the extra unreachable capability appears in
            // neither the imports nor the decoded wire-schema table.
            const auto least_custom = least.ok()
                                          ? wire_schema_custom_section(least.artifact->bytes)
                                          : WireSchemaCustomSection{};
            check(least_custom.custom_count == 1 && least_custom.table_bytes.has_value(),
                  "E2 least-privilege artifact still carries one wire-schema section");
            if (least_custom.table_bytes.has_value()) {
                const auto decoded = ahfl::ir::core::decode_core_wire_schema_table(
                    std::span<const std::uint8_t>(*least_custom.table_bytes));
                check(decoded.ok() && decoded.table.has_value() &&
                          decoded.table->capabilities.size() == 1 &&
                          decoded.table->capabilities.front().capability.value == 0 &&
                          decoded.table->capabilities.front().source_symbol == 42,
                      "E2 least-privilege wire-schema table exposes only the "
                      "reachable cap 0/source 42");
            }
        }
    }

    // Test 12: capability ABI and frame-subset failures publish no artifact.
    {
        using namespace ahfl::ir::core;

        // RFC 0026 E4-B1: a capability whose reachable type closure is verified
        // Core + layoutable but NOT wire-transportable fails closed with no
        // artifact. Here app::Output gains a Map<Int,Int> field (Int keys are
        // legal Core but the wire value_json encoder supports only String map
        // keys), so the wire projector rejects the transitive closure. The agent
        // shell stays a plain struct; only the field closure is unprojectable.
        auto unprojectable = make_e2_core_program();
        const CoreTypeId map_type_id{
            static_cast<std::uint32_t>(unprojectable.types.size())};
        CoreTypeDecl map_decl;
        map_decl.kind = CoreTypeDecl::Kind::Struct;
        map_decl.name = "std::collections::Map";
        map_decl.role = CoreNominalRole::Map;
        map_decl.type_param_count = 2;
        map_decl.variances = {CoreVariance::Invariant, CoreVariance::Covariant};
        unprojectable.types.push_back(std::move(map_decl));
        const CoreValueTypeId int_vt{
            static_cast<std::uint32_t>(unprojectable.value_types.size())};
        unprojectable.value_types.push_back(CoreValueType{CoreVtInt{}});
        const CoreValueTypeId map_vt{
            static_cast<std::uint32_t>(unprojectable.value_types.size())};
        unprojectable.value_types.push_back(CoreValueType{
            CoreVtNominal{map_type_id, {int_vt, int_vt}, std::uint64_t{4}}});
        // app::Output (types[1]) gains one Map<Int,Int> field "items".
        auto &output = unprojectable.types[1];
        output.fields = {"items"};
        output.field_nominal_types = {map_type_id};
        output.field_has_default = {false};
        CoreMemberTypeTemplateNode items_template;
        items_template.kind = CoreMemberTypeTemplateKind::Concrete;
        items_template.concrete = map_vt;
        output.member_type_templates = {std::move(items_template)};
        output.field_type_template_roots = {CoreMemberTypeTemplateNodeId{0}};

        const auto unprojectable_verified = verify_core_program(unprojectable);
        const auto unprojectable_layout = compute_core_layouts(unprojectable);
        const auto unprojectable_result =
            (unprojectable_verified.ok() && unprojectable_layout.ok())
                ? ahfl::backends::emit_core_wasm(
                      unprojectable,
                      *unprojectable_layout.table,
                      {ahfl::ir::core::CoreAgentId{0}, ahfl::backends::WasmProfileKind::Wasi})
                : ahfl::backends::CoreWasmCodegenResult{};
        check(unprojectable_verified.ok() && unprojectable_layout.ok(),
              "E2 Map<Int,Int>-field fixture is verified Core + P4-D layout");
        check(unprojectable_verified.ok() && unprojectable_layout.ok() &&
                  !unprojectable_result.artifact.has_value() &&
                  has_codegen_code(unprojectable_result,
                                   ahfl::backends::core_wasm_diag::kInvalidCapabilityAbi),
              "E2 rejects a wire-unprojectable reachable capability with no artifact");
        // The INVALID_CAPABILITY_ABI diagnostic attributes the underlying
        // projector code so the failure is not conflated with an index-domain
        // ABI overflow.
        bool cites_map_key = false;
        for (const auto &diagnostic : unprojectable_result.diagnostics) {
            if (diagnostic.code ==
                    ahfl::backends::core_wasm_diag::kInvalidCapabilityAbi &&
                diagnostic.message.find(std::string(
                    ahfl::ir::core::wire_schema::kUnsupportedMapKey)) !=
                    std::string::npos) {
                cites_map_key = true;
            }
        }
        check(cites_map_key,
              "E2 unprojectable diagnostic cites the underlying core.wire.UNSUPPORTED_MAP_KEY");

        auto wide_symbol = make_e2_core_program();
        wide_symbol.capabilities[0].symbol_ref.id =
            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) + 1u;
        const auto wide_layout = compute_core_layouts(wide_symbol);
        const auto wide = wide_layout.table.has_value()
                              ? ahfl::backends::emit_core_wasm(
                                    wide_symbol,
                                    *wide_layout.table,
                                    {ahfl::ir::core::CoreAgentId{0}, ahfl::backends::WasmProfileKind::Wasi})
                              : ahfl::backends::CoreWasmCodegenResult{};
        check(!wide.artifact.has_value() &&
                  has_codegen_code(wide,
                                   ahfl::backends::core_wasm_diag::kInvalidCapabilityAbi),
              "E2 rejects a SymbolId outside the uint32 import ABI domain");

        auto hidden_expr = make_e2_core_program();
        hidden_expr.flows[0].exprs.push_back(hidden_expr.flows[0].exprs[0]);
        const auto hidden_layout = compute_core_layouts(hidden_expr);
        const auto hidden = hidden_layout.table.has_value()
                                ? ahfl::backends::emit_core_wasm(
                                      hidden_expr,
                                      *hidden_layout.table,
                                      {ahfl::ir::core::CoreAgentId{0}, ahfl::backends::WasmProfileKind::Wasi})
                                : ahfl::backends::CoreWasmCodegenResult{};
        check(!hidden.artifact.has_value() &&
                  has_codegen_code(
                      hidden,
                      ahfl::backends::core_wasm_diag::kUnsupportedCapabilityFrame),
              "E2 rejects a hidden capability-frame expression with no artifact");

        auto construction = make_e2_core_program();
        CoreConstructExpr construct_input;
        construct_input.type_name = "app::Input";
        construct_input.type_id = CoreTypeId{0};
        construct_input.resolved = true;
        construction.flows[0].exprs[0].node = std::move(construct_input);
        check(rejects_capability_frame(construction),
              "E2 rejects a constructed capability argument with no artifact");

        auto literal = make_e2_core_program();
        literal.value_types.push_back(CoreValueType{CoreVtInt{
            std::make_pair<std::int64_t, std::int64_t>(1, 1)}});
        literal.flows[0].exprs[0].node =
            CoreLiteralExpr{CoreLiteralKind::Integer, "1"};
        literal.flows[0].exprs[0].result_type = CoreValueTypeId{2};
        literal.flows[0].value_types[0] = CoreValueTypeId{2};
        literal.capabilities[0].param_types[0] = CoreValueTypeId{2};
        check(rejects_capability_frame(literal),
              "E2 rejects a literal capability argument with no artifact");

        auto projection = make_e2_core_program();
        CoreTypeDecl nested;
        nested.kind = CoreTypeDecl::Kind::Struct;
        nested.name = "app::Nested";
        projection.types.push_back(std::move(nested));
        projection.value_types.push_back(
            CoreValueType{CoreVtNominal{CoreTypeId{2}, {}, std::nullopt}});
        auto &projection_input = projection.types[0];
        projection_input.fields = {"nested"};
        projection_input.field_nominal_types = {CoreTypeId{2}};
        projection_input.field_has_default = {false};
        CoreMemberTypeTemplateNode nested_template;
        nested_template.kind = CoreMemberTypeTemplateKind::Concrete;
        nested_template.concrete = CoreValueTypeId{2};
        projection_input.member_type_templates = {nested_template};
        projection_input.field_type_template_roots = {CoreMemberTypeTemplateNodeId{0}};
        auto &projection_expr = projection.flows[0].exprs[0];
        auto &projection_path = std::get<CorePathExpr>(projection_expr.node);
        projection_path.members = {"nested"};
        projection_path.projection = {
            CoreProjectionStep{CoreTypeId{0}, CoreFieldId{0}, CoreTypeId{2}}};
        projection_expr.result_type = CoreValueTypeId{2};
        projection.flows[0].value_types[0] = CoreValueTypeId{2};
        projection.capabilities[0].param_types[0] = CoreValueTypeId{2};
        check(rejects_capability_frame(projection),
              "E2 rejects a projected capability argument with no artifact");

        auto coercion = make_e2_core_program();
        coercion.value_types.push_back(CoreValueType{CoreVtInt{
            std::make_pair<std::int64_t, std::int64_t>(0, 0)}});
        coercion.value_types.push_back(CoreValueType{CoreVtInt{std::nullopt}});
        auto &coercion_flow = coercion.flows[0];
        coercion_flow.exprs[0].node = CoreLiteralExpr{CoreLiteralKind::Integer, "0"};
        coercion_flow.exprs[0].result_type = CoreValueTypeId{2};
        coercion_flow.exprs.push_back(
            CoreExpr{CoreCoerceExpr{CoreValueId{0}, CoreCoercionPlanId{0}},
                     std::nullopt,
                     CoreValueTypeId{3}});
        coercion_flow.value_count = 3;
        coercion_flow.value_types = {
            CoreValueTypeId{2}, CoreValueTypeId{3}, CoreValueTypeId{1}};
        CoreCoercionOp widen;
        widen.kind = CoreCoercionOpKind::IntWiden;
        widen.child = CoreCoercionPlanId{CoreCoercionPlanId::kInvalid};
        coercion_flow.coercion_plans = {
            CoreCoercionPlanNode{CoreValueTypeId{2}, CoreValueTypeId{3}, {widen}}};
        coercion.capabilities[0].param_types[0] = CoreValueTypeId{3};
        auto &coercion_statements = coercion_flow.states[0].body.statements;
        coercion_statements.clear();
        coercion_statements.push_back(
            CoreStmt{CoreLetStmt{CoreValueId{0}, CoreExprId{0}}, std::nullopt});
        coercion_statements.push_back(
            CoreStmt{CoreLetStmt{CoreValueId{1}, CoreExprId{1}}, std::nullopt});
        coercion_statements.push_back(
            CoreStmt{CoreCapabilityCallStmt{CoreValueId{2},
                                            CoreCapabilityId{0},
                                            "Echo",
                                            {CoreValueId{1}}},
                     std::nullopt});
        coercion_statements.push_back(
            CoreStmt{CoreReturnStmt{true, CoreValueId{2}}, std::nullopt});
        check(rejects_capability_frame(coercion),
              "E2 rejects a coerced capability argument with no artifact");

        auto nested_if = make_e2_core_program();
        nested_if.value_types.push_back(CoreValueType{CoreVtBool{}});
        auto &nested_flow = nested_if.flows[0];
        nested_flow.exprs.push_back(
            CoreExpr{CoreLiteralExpr{CoreLiteralKind::Bool, "true"},
                     std::nullopt,
                     CoreValueTypeId{2}});
        nested_flow.value_count = 4;
        nested_flow.value_types = {CoreValueTypeId{0},
                                   CoreValueTypeId{1},
                                   CoreValueTypeId{2},
                                   CoreValueTypeId{1}};
        CoreIfStmt nested_capability;
        nested_capability.condition = CoreValueId{2};
        nested_capability.then_region = std::make_unique<CoreRegion>();
        nested_capability.then_region->statements.push_back(
            CoreStmt{CoreCapabilityCallStmt{CoreValueId{1},
                                            CoreCapabilityId{0},
                                            "Echo",
                                            {CoreValueId{0}}},
                     std::nullopt});
        nested_capability.then_region->statements.push_back(
            CoreStmt{CoreReturnStmt{true, CoreValueId{1}}, std::nullopt});
        nested_capability.else_region = std::make_unique<CoreRegion>();
        nested_capability.else_region->statements.push_back(
            CoreStmt{CoreCapabilityCallStmt{CoreValueId{3},
                                            CoreCapabilityId{0},
                                            "Echo",
                                            {CoreValueId{0}}},
                     std::nullopt});
        nested_capability.else_region->statements.push_back(
            CoreStmt{CoreReturnStmt{true, CoreValueId{3}}, std::nullopt});
        auto &nested_statements = nested_flow.states[0].body.statements;
        nested_statements.clear();
        nested_statements.push_back(
            CoreStmt{CoreLetStmt{CoreValueId{0}, CoreExprId{0}}, std::nullopt});
        nested_statements.push_back(
            CoreStmt{CoreLetStmt{CoreValueId{2}, CoreExprId{1}}, std::nullopt});
        nested_statements.push_back(CoreStmt{std::move(nested_capability), std::nullopt});
        const auto nested_layout = compute_core_layouts(nested_if);
        const auto nested_emitted = nested_layout.table.has_value()
                                ? ahfl::backends::emit_core_wasm(
                                      nested_if,
                                      *nested_layout.table,
                                      {ahfl::ir::core::CoreAgentId{0}, ahfl::backends::WasmProfileKind::Wasi})
                                : ahfl::backends::CoreWasmCodegenResult{};
        check(verify_core_program(nested_if).ok() &&
                  !nested_emitted.artifact.has_value() &&
                  has_codegen_code(
                      nested_emitted,
                      ahfl::backends::core_wasm_diag::kUnsupportedCapabilityFrame) &&
                  !has_codegen_code(
                      nested_emitted,
                      ahfl::backends::core_wasm_diag::kUnsupportedOrchestration),
              "E2 classifies a capability nested in CoreIf as unsupported frame, not generic orchestration");
    }

    // Test 13: entry selection and deterministic termination fail closed.
    {
        using namespace ahfl::ir::core;
        auto cycle = make_e1_core_program();
        cycle.agents[0].transitions = {
            CoreTransition{CoreStateId{1}, CoreStateId{1}}};
        auto &go =
            std::get<CoreGotoStmt>(cycle.flows[0].states[1].body.statements[0].node);
        go.target = CoreStateId{1};
        go.target_name = "Start";
        const auto cycle_layout = compute_core_layouts(cycle);
        const auto cycle_result =
            cycle_layout.table.has_value()
                ? ahfl::backends::emit_core_wasm(
                      cycle,
                      *cycle_layout.table,
                      {ahfl::ir::core::CoreAgentId{0}, ahfl::backends::WasmProfileKind::Wasi})
                : ahfl::backends::CoreWasmCodegenResult{};
        check(!cycle_result.artifact.has_value() &&
                  has_codegen_code(cycle_result,
                                   ahfl::backends::core_wasm_diag::kNonterminatingE1Run),
              "E1 rejects a deterministic goto cycle with no artifact");

        const auto single = make_e1_core_program();
        const auto single_layout = compute_core_layouts(single);
        const auto single_result = single_layout.table.has_value()
                                       ? ahfl::backends::emit_core_wasm(
                                             single,
                                             *single_layout.table,
                                             {CoreAgentId{0},
                                              ahfl::backends::WasmProfileKind::Wasi})
                                       : ahfl::backends::CoreWasmCodegenResult{};
        auto multiple = make_e1_core_program();
        auto second = make_e1_core_program();
        multiple.agents.push_back(std::move(second.agents[0]));
        second.flows[0].target = CoreAgentId{1};
        multiple.flows.push_back(std::move(second.flows[0]));
        const auto multiple_layout = compute_core_layouts(multiple);
        const auto multiple_result =
            multiple_layout.table.has_value()
                ? ahfl::backends::emit_core_wasm(
                      multiple,
                      *multiple_layout.table,
                      {ahfl::ir::core::CoreAgentId{0}, ahfl::backends::WasmProfileKind::Wasi})
                : ahfl::backends::CoreWasmCodegenResult{};
        check(single_result.ok() && multiple_result.ok() &&
                  single_result.artifact->bytes == multiple_result.artifact->bytes &&
                  std::holds_alternative<CoreAgentId>(multiple_result.artifact->entry),
              "explicit agent entry ignores unrelated agents without changing E1 bytes");
    }

    // Test 14 (KR6.5 E3-C1): one typed entry resolves strictly. Package metadata
    // never falls back to declaration zero or a display/local name.
    {
        using namespace ahfl;
        using namespace ahfl::ir::core;
        auto single = make_e1_core_program();
        single.agents[0].symbol_ref = {ir::SymbolRefKind::Agent,
                                       "app::Runner",
                                       "Runner",
                                       "app",
                                       301};
        const auto legacy = backends::resolve_core_wasm_entry(single, nullptr);
        check(legacy.has_value() &&
                  std::get_if<CoreAgentId>(&*legacy) != nullptr &&
                  std::get<CoreAgentId>(*legacy) == CoreAgentId{0},
              "metadata-free single-agent E1 compatibility resolves typed agent zero");

        auto multiple = make_e1_core_program();
        multiple.agents[0].symbol_ref = single.agents[0].symbol_ref;
        multiple.agents.push_back(single.agents[0]);
        multiple.agents[1].name = "Other";
        multiple.agents[1].symbol_ref.canonical_name = "app::Other";
        check(!backends::resolve_core_wasm_entry(multiple, nullptr).has_value() &&
                  backends::resolve_core_wasm_entry(multiple, nullptr).error().code ==
                      backends::core_wasm_diag::kEntryAmbiguous,
              "metadata-free multi-agent program is ambiguous, never first-selected");

        handoff::PackageMetadata agent_metadata;
        agent_metadata.entry_target =
            handoff::ExecutableRef{handoff::ExecutableKind::Agent, "app::Runner"};
        const auto explicit_agent =
            backends::resolve_core_wasm_entry(multiple, &agent_metadata);
        check(explicit_agent.has_value() &&
                  std::get_if<CoreAgentId>(&*explicit_agent) != nullptr &&
                  std::get<CoreAgentId>(*explicit_agent) == CoreAgentId{0},
              "explicit canonical agent entry resolves exactly in a multi-agent program");

        agent_metadata.entry_target->canonical_name = "Runner";
        const auto local_name_miss =
            backends::resolve_core_wasm_entry(multiple, &agent_metadata);
        check(!local_name_miss.has_value() &&
                  local_name_miss.error().code == backends::core_wasm_diag::kEntryNotFound,
              "explicit agent miss does not fall back from canonical to local name");

        agent_metadata.entry_target =
            handoff::ExecutableRef{handoff::ExecutableKind::Workflow, "app::Runner"};
        const auto wrong_kind = backends::resolve_core_wasm_entry(multiple, &agent_metadata);
        check(!wrong_kind.has_value() &&
                  wrong_kind.error().code == backends::core_wasm_diag::kEntryNotFound,
              "explicit entry kind is structural and cannot cross-resolve an agent");

        agent_metadata.entry_target->kind =
            static_cast<handoff::ExecutableKind>(std::numeric_limits<int>::max());
        const auto unknown_kind =
            backends::resolve_core_wasm_entry(multiple, &agent_metadata);
        check(!unknown_kind.has_value() &&
                  unknown_kind.error().code == backends::core_wasm_diag::kEntryNotFound,
              "unknown explicit executable kind fails closed");

        multiple.agents[1].symbol_ref.canonical_name = "app::Runner";
        agent_metadata.entry_target =
            handoff::ExecutableRef{handoff::ExecutableKind::Agent, "app::Runner"};
        const auto duplicate = backends::resolve_core_wasm_entry(multiple, &agent_metadata);
        check(!duplicate.has_value() &&
                  duplicate.error().code == backends::core_wasm_diag::kEntryNotFound,
              "duplicate explicit canonical agent entry fails closed");

        handoff::PackageMetadata empty_metadata;
        const auto missing = backends::resolve_core_wasm_entry(single, &empty_metadata);
        check(!missing.has_value() &&
                  missing.error().code == backends::core_wasm_diag::kEntryAmbiguous,
              "present package metadata without an entry never uses legacy fallback");
    }

    // Test 15: E3-C2 emits a deterministic real multi-agent workflow module.
    // Subset violations remain classified at the narrow workflow-frame seam.
    {
        using namespace ahfl;
        using namespace ahfl::ir::core;
        const auto emit_workflow = [](const CoreProgram &program) {
            const auto layouts = compute_core_layouts(program);
            return layouts.table.has_value()
                       ? backends::emit_core_wasm(
                             program,
                             *layouts.table,
                             {CoreWorkflowId{0}, backends::WasmProfileKind::Wasi})
                       : backends::CoreWasmCodegenResult{};
        };

        const auto valid = make_e3_workflow_program();
        const auto valid_layouts = compute_core_layouts(valid);
        const auto valid_layout_snapshot = valid_layouts.table;
        const auto valid_result = emit_workflow(valid);
        const auto repeated_result = emit_workflow(valid);
        const bool packaged_exactly =
            valid_result.artifact.has_value() &&
            valid_result.artifact->packaged_agent_instances ==
                std::vector<CoreInstanceId>{CoreInstanceId{0}, CoreInstanceId{1}};
        check(verify_core_program(valid).ok() && valid_layouts.ok() &&
                  valid_result.ok() && repeated_result.ok() &&
                  valid_result.artifact->bytes == repeated_result.artifact->bytes &&
                  std::holds_alternative<CoreWorkflowId>(valid_result.artifact->entry) &&
                  packaged_exactly && valid_result.artifact->imports.empty() &&
                  valid_layouts.table == valid_layout_snapshot,
              "E3-C2 emits deterministic workflow bytes without mutating layout or expanding authority");

        // RFC 0026 E4-B2-C: the no-capability E3 identity-workflow fixture
        // (import_count == 0) carries NO custom section (neither AHFLXM nor
        // AHFLWS). Assert on the section walk, not merely on imports.empty(). A
        // capability workflow does carry them (covered by the E4-B2-C case below).
        const auto e3_custom = valid_result.ok()
                                   ? wire_schema_custom_section(valid_result.artifact->bytes)
                                   : WireSchemaCustomSection{};
        check(valid_result.ok() && e3_custom.custom_count == 0 &&
                  !e3_custom.table_bytes.has_value(),
              "no-capability E3 identity workflow (import_count==0) carries no custom section");

        auto bad_workflow_layout = *valid_layouts.table;
        bad_workflow_layout.target.pointer_size = 8;
        const auto bad_workflow_layout_result = backends::emit_core_wasm(
            valid,
            bad_workflow_layout,
            {CoreWorkflowId{0}, backends::WasmProfileKind::Wasi});
        check(!bad_workflow_layout_result.artifact.has_value() &&
                  has_codegen_code(bad_workflow_layout_result,
                                   backends::core_wasm_diag::kInvalidLayout),
              "E3-C2 rejects an unverified workflow layout before byte emission");

        const auto current_body = valid_result.artifact.has_value()
                                      ? wasm_function_body(valid_result.artifact->bytes, 2)
                                      : std::nullopt;
        const auto step_body = valid_result.artifact.has_value()
                                   ? wasm_function_body(valid_result.artifact->bytes, 3)
                                   : std::nullopt;
        const auto first_runner = valid_result.artifact.has_value()
                                      ? wasm_function_body(valid_result.artifact->bytes, 4)
                                      : std::nullopt;
        const auto second_runner = valid_result.artifact.has_value()
                                       ? wasm_function_body(valid_result.artifact->bytes, 5)
                                       : std::nullopt;
        const auto workflow_run = valid_result.artifact.has_value()
                                      ? wasm_function_body(valid_result.artifact->bytes, 6)
                                      : std::nullopt;
        const auto workflow_run2 = valid_result.artifact.has_value()
                                       ? wasm_function_body(valid_result.artifact->bytes, 7)
                                       : std::nullopt;
        const bool traps_before_effect =
            current_body == std::optional<std::vector<std::uint8_t>>{{0x00, 0x00, 0x0b}} &&
            step_body == std::optional<std::vector<std::uint8_t>>{{0x00, 0x00, 0x0b}};
        const bool runners_are_frame_opaque =
            first_runner.has_value() && second_runner.has_value() &&
            std::none_of(first_runner->begin(), first_runner->end(), [](std::uint8_t byte) {
                return byte >= 0x28 && byte <= 0x3e;
            }) &&
            std::none_of(second_runner->begin(), second_runner->end(), [](std::uint8_t byte) {
                return byte >= 0x28 && byte <= 0x3e;
            }) &&
            workflow_run.has_value() && workflow_run2.has_value() &&
            std::none_of(workflow_run->begin(), workflow_run->end(), [](std::uint8_t byte) {
                return byte >= 0x28 && byte <= 0x3e;
            }) &&
            std::none_of(workflow_run2->begin(), workflow_run2->end(), [](std::uint8_t byte) {
                return byte >= 0x28 && byte <= 0x3e;
            });
        const bool schedule_calls_in_order =
            workflow_run2.has_value() &&
            small_fixture_call_indices(*workflow_run2) ==
                std::vector<std::uint32_t>{4, 5};
        check(traps_before_effect && runners_are_frame_opaque && schedule_calls_in_order,
              "E3-C2 workflow step/state trap and identity runners dispatch without frame memory operations");

        handoff::PackageMetadata workflow_metadata;
        workflow_metadata.entry_target = handoff::ExecutableRef{
            handoff::ExecutableKind::Workflow, "app::IdentityPipeline"};
        const auto workflow_entry =
            backends::resolve_core_wasm_entry(valid, &workflow_metadata);
        check(workflow_entry.has_value() &&
                  std::get_if<CoreWorkflowId>(&*workflow_entry) != nullptr &&
                  std::get<CoreWorkflowId>(*workflow_entry) == CoreWorkflowId{0} &&
                  !backends::resolve_core_wasm_entry(valid, nullptr).has_value(),
              "workflow entry resolves only through explicit package identity");

        auto duplicate_workflow = make_e3_workflow_program();
        CoreWorkflowDecl duplicate_decl;
        duplicate_decl.id = CoreWorkflowId{1};
        duplicate_decl.name = "DuplicatePipeline";
        duplicate_decl.symbol_ref = duplicate_workflow.workflows[0].symbol_ref;
        duplicate_workflow.workflows.push_back(std::move(duplicate_decl));
        const auto duplicate_workflow_entry =
            backends::resolve_core_wasm_entry(duplicate_workflow, &workflow_metadata);
        check(!duplicate_workflow_entry.has_value() &&
                  duplicate_workflow_entry.error().code ==
                      backends::core_wasm_diag::kEntryNotFound,
              "duplicate explicit canonical workflow entry fails closed");

        const auto invalid_typed_entry_layout = compute_core_layouts(valid);
        const auto invalid_typed_entry = invalid_typed_entry_layout.table.has_value()
                                             ? backends::emit_core_wasm(
                                                   valid,
                                                   *invalid_typed_entry_layout.table,
                                                   {CoreWorkflowId{9},
                                                    backends::WasmProfileKind::Wasi})
                                             : backends::CoreWasmCodegenResult{};
        check(!invalid_typed_entry.artifact.has_value() &&
                  has_codegen_code(invalid_typed_entry,
                                   backends::core_wasm_diag::kEntryNotFound),
              "out-of-range typed workflow entry fails closed without fallback");

        auto branch_join = make_e3_workflow_program();
        auto &branch_workflow = branch_join.workflows[0];
        auto &parallel_input = std::get<CorePathExpr>(branch_workflow.exprs[1].node);
        parallel_input.root = CorePathRoot::WorkflowInput;
        parallel_input.root_name = "input";
        parallel_input.workflow_node = CoreWorkflowNodeId{};
        branch_workflow.nodes[1].after.clear();
        CorePathExpr join_input;
        join_input.root = CorePathRoot::WorkflowNodeOutput;
        join_input.root_name = "first";
        join_input.root_type = CoreTypeId{0};
        join_input.workflow_node = CoreWorkflowNodeId{0};
        branch_workflow.exprs.push_back(
            CoreExpr{std::move(join_input), std::nullopt, CoreValueTypeId{0}});
        const CoreValueId join_value{branch_workflow.value_count++};
        branch_workflow.value_types.push_back(CoreValueTypeId{0});
        CoreWorkflowNode join;
        join.id = CoreWorkflowNodeId{2};
        join.node_name = "join";
        join.target_instance = CoreInstanceId{0};
        join.after = {CoreWorkflowNodeId{0}, CoreWorkflowNodeId{1}};
        join.input_region = std::make_unique<CoreRegion>();
        join.input_region->statements.push_back(
            CoreStmt{CoreLetStmt{join_value, CoreExprId{3}}, std::nullopt});
        join.input_region->statements.push_back(
            CoreStmt{CoreYieldStmt{true, join_value}, std::nullopt});
        branch_workflow.nodes.push_back(std::move(join));
        const auto branch_join_result = emit_workflow(branch_join);
        const auto branch_run2 = branch_join_result.artifact.has_value()
                                     ? wasm_function_body(branch_join_result.artifact->bytes, 7)
                                     : std::nullopt;
        check(verify_core_program(branch_join).ok() && branch_join_result.ok() &&
                  branch_join_result.artifact->packaged_agent_instances ==
                      std::vector<CoreInstanceId>{CoreInstanceId{0}, CoreInstanceId{1}} &&
                  branch_run2.has_value() &&
                  small_fixture_call_indices(*branch_run2) ==
                      std::vector<std::uint32_t>{4, 5, 4},
              "E3-C2 emits declaration-order parallel roots plus a join and dedups instance runners");

        auto wrong_type = make_e3_workflow_program();
        CoreTypeDecl other;
        other.kind = CoreTypeDecl::Kind::Struct;
        other.name = "app::OtherFrame";
        wrong_type.types.push_back(std::move(other));
        const CoreValueTypeId other_value{
            static_cast<std::uint32_t>(wrong_type.value_types.size())};
        wrong_type.value_types.push_back(
            CoreValueType{CoreVtNominal{CoreTypeId{1}, {}, std::nullopt}});
        wrong_type.agents[1].input_type = CoreTypeId{1};
        auto &second_instance =
            std::get<CoreAgentInstance>(wrong_type.instances[1].payload);
        second_instance.input_type = CoreTypeId{1};
        wrong_type.instances[1].dispatch_types[0] = other_value;
        wrong_type.flows[1].exprs[0].result_type = other_value;
        wrong_type.flows[1].value_types[0] = other_value;
        auto &agent_input = std::get<CorePathExpr>(wrong_type.flows[1].exprs[0].node);
        agent_input.root_type = CoreTypeId{1};
        const auto wrong_type_result = emit_workflow(wrong_type);
        check(verify_core_program(wrong_type).ok() &&
                  !wrong_type_result.artifact.has_value() &&
                  has_codegen_code(wrong_type_result,
                                   backends::core_wasm_diag::kUnsupportedWorkflowFrame),
              "E3 rejects a workflow frame that does not match target input type");

        // RFC 0026 E4-B2-C: a workflow node agent whose reachable final action is
        // a capability call is now a legal capability-bearing workflow (the E3
        // refusal is lifted on this lane). It emits an import section + AHFLXM
        // exec-manifest + EOF AHFLWS, and its defined/export indices shift by
        // import_count (1 here) under the same PROJECT rule.
        auto capability_agent = make_e3_workflow_program();
        CoreCapabilityDecl cap;
        cap.name = "Echo";
        cap.symbol_ref = {ir::SymbolRefKind::Capability,
                          "app::Echo",
                          "Echo",
                          "app",
                          350};
        cap.param_types = {CoreValueTypeId{0}};
        cap.return_type = CoreValueTypeId{0};
        capability_agent.capabilities.push_back(std::move(cap));
        capability_agent.agents[0].capabilities = {CoreCapabilityId{0}};
        auto &cap_flow = capability_agent.flows[0];
        cap_flow.value_count = 2;
        cap_flow.value_types = {CoreValueTypeId{0}, CoreValueTypeId{0}};
        auto &cap_statements = cap_flow.states[0].body.statements;
        cap_statements.clear();
        cap_statements.push_back(
            CoreStmt{CoreLetStmt{CoreValueId{0}, CoreExprId{0}}, std::nullopt});
        cap_statements.push_back(
            CoreStmt{CoreCapabilityCallStmt{CoreValueId{1},
                                            CoreCapabilityId{0},
                                            "Echo",
                                            {CoreValueId{0}}},
                     std::nullopt});
        cap_statements.push_back(
            CoreStmt{CoreReturnStmt{true, CoreValueId{1}}, std::nullopt});
        const auto capability_result = emit_workflow(capability_agent);
        const auto capability_custom =
            capability_result.ok()
                ? wire_schema_custom_section(capability_result.artifact->bytes)
                : WireSchemaCustomSection{};
        const bool capability_imports =
            capability_result.artifact.has_value() &&
            capability_result.artifact->imports ==
                std::vector<std::string>{"ahfl_cap.cap_350"};
        check(verify_core_program(capability_agent).ok() &&
                  capability_result.ok() && capability_imports &&
                  capability_custom.custom_count == 2 &&
                  capability_custom.table_bytes.has_value(),
              "E4-B2-C emits a capability-bearing workflow with import + AHFLXM + AHFLWS");

        // Opcode/index-level structural contract for the capability workflow.
        // `wasm_function_body` indexes Code-section defined bodies (0-based,
        // import-independent): alloc=0, run=6, run2=7 regardless of import_count.
        // The absolute import_count+1 shift is proven by the run2 call operands
        // {5,6} and by the executed Node export table.
        const auto cap_run2 = capability_result.artifact.has_value()
                                  ? wasm_function_body(capability_result.artifact->bytes, 7)
                                  : std::nullopt;
        const auto cap_run = capability_result.artifact.has_value()
                                 ? wasm_function_body(capability_result.artifact->bytes, 6)
                                 : std::nullopt;
        const auto cap_alloc = capability_result.artifact.has_value()
                                   ? wasm_function_body(capability_result.artifact->bytes, 0)
                                   : std::nullopt;
        // Latch-first: run2 begins with the locals prologue {01,05,7f} (one i32
        // local group of 5) immediately followed by the pending-latch gate
        // global.get 5 / if / unreachable / end. This proves it is the FIRST
        // instruction, not merely present somewhere in the body.
        const std::vector<std::uint8_t> latch_prefix = {
            0x01, 0x05, 0x7f, 0x23, 0x05, 0x04, 0x40, 0x00, 0x0b};
        const bool latch_first =
            cap_run2.has_value() && cap_run2->size() >= latch_prefix.size() &&
            std::equal(latch_prefix.begin(), latch_prefix.end(), cap_run2->begin());
        // Body-before-count ORDER: node0's record body ends with the reserved
        // [36..39]=0 store at absolute address 1068 (i32.const 1068 = 0x41 0xac
        // 0x08, i32.const 0, i32.store), and the event_count publish stores value
        // 1 to header address 1024 (i32.const 1024 = 0x41 0x80 0x08, i32.const 1,
        // i32.store). Assert the reserved store appears BEFORE the publish.
        const std::vector<std::uint8_t> reserved_store = {
            0x41, 0xac, 0x08, 0x41, 0x00, 0x36, 0x02, 0x00};
        const std::vector<std::uint8_t> count_publish = {
            0x41, 0x80, 0x08, 0x41, 0x01, 0x36, 0x02, 0x00};
        bool event_writes = false;
        if (cap_run2.has_value()) {
            const auto reserved_at = std::search(cap_run2->begin(), cap_run2->end(),
                                                 reserved_store.begin(), reserved_store.end());
            const auto publish_at = std::search(cap_run2->begin(), cap_run2->end(),
                                                count_publish.begin(), count_publish.end());
            event_writes = reserved_at != cap_run2->end() &&
                           publish_at != cap_run2->end() && reserved_at < publish_at;
        }
        // Legacy run on the capability lane is a bare pre-effect trap.
        const bool cap_run_traps =
            cap_run == std::optional<std::vector<std::uint8_t>>{{0x00, 0x00, 0x0b}};
        // Checked alloc returns 0 on capacity exhaustion (i32.gt_u 0x4b guard and
        // an early i32.const 0 / return 0x0f), distinct from the unchecked bump.
        const bool checked_alloc =
            cap_alloc.has_value() && contains_bytes(*cap_alloc, {0x4b}) &&
            contains_bytes(*cap_alloc, {0x41, 0x00, 0x0f});
        // The scheduler runner calls dispatch to the shifted runner indices {5,6}.
        const bool cap_schedule_calls =
            cap_run2.has_value() &&
            small_fixture_call_indices(*cap_run2) == std::vector<std::uint32_t>{5, 6};
        check(latch_first && event_writes && cap_run_traps && checked_alloc &&
                  cap_schedule_calls,
              "E4-B2-C run2 latch-first + body-before-count + checked alloc + shifted dispatch");

        // RFC 0026 E4-B2-C node-event region capacity boundary through the real
        // public emitter (scaled Core builder, single reused capability instance).
        // N=1612: heap_base = align_up(1024 + 8 + 1612*40, 8) = 65512 <= 65536, so
        // the module emits with no RESOURCE/BINARY diagnostic. N=1613: 65552 >
        // 65536, so it fails closed with EXACTLY wasm.RESOURCE_EXHAUSTED and no
        // artifact (never a misreported BINARY_OVERFLOW).
        const auto fits = make_n_node_capability_workflow(1612);
        const auto fits_layouts = compute_core_layouts(fits);
        const auto fits_result =
            (verify_core_program(fits).ok() && fits_layouts.ok() &&
             fits_layouts.table.has_value())
                ? backends::emit_core_wasm(
                      fits, *fits_layouts.table,
                      {CoreWorkflowId{0}, backends::WasmProfileKind::Wasi})
                : backends::CoreWasmCodegenResult{};
        check(fits_result.ok() &&
                  !has_codegen_code(fits_result,
                                    backends::core_wasm_diag::kResourceExhausted) &&
                  !has_codegen_code(fits_result,
                                    backends::core_wasm_diag::kBinaryOverflow),
              "E4-B2-C N=1612 node-event region fits the 64 KiB page and emits");

        const auto exceeds = make_n_node_capability_workflow(1613);
        const auto exceeds_layouts = compute_core_layouts(exceeds);
        const auto exceeds_result =
            (verify_core_program(exceeds).ok() && exceeds_layouts.ok() &&
             exceeds_layouts.table.has_value())
                ? backends::emit_core_wasm(
                      exceeds, *exceeds_layouts.table,
                      {CoreWorkflowId{0}, backends::WasmProfileKind::Wasi})
                : backends::CoreWasmCodegenResult{};
        // Exactly one diagnostic, the stable range-less RESOURCE_EXHAUSTED code,
        // and no artifact — locking both the priority (no BINARY_OVERFLOW) and the
        // range-less/no-echo property of the new stable diagnostic.
        const bool exceeds_exact =
            !exceeds_result.artifact.has_value() &&
            exceeds_result.diagnostics.size() == 1 &&
            exceeds_result.diagnostics.front().code ==
                backends::core_wasm_diag::kResourceExhausted &&
            !exceeds_result.diagnostics.front().source_range.has_value();
        check(exceeds_exact,
              "E4-B2-C N=1613 fails closed with exactly one range-less RESOURCE_EXHAUSTED");

        auto workflow_capability = make_e3_workflow_program();
        CoreCapabilityDecl workflow_cap;
        workflow_cap.name = "WorkflowEcho";
        workflow_cap.symbol_ref = {ir::SymbolRefKind::Capability,
                                   "app::WorkflowEcho",
                                   "WorkflowEcho",
                                   "app",
                                   351};
        workflow_cap.param_types = {CoreValueTypeId{0}};
        workflow_cap.return_type = CoreValueTypeId{0};
        workflow_capability.capabilities.push_back(std::move(workflow_cap));
        auto &workflow_cap_region = *workflow_capability.workflows[0].nodes[0].input_region;
        const CoreValueId workflow_cap_value{
            workflow_capability.workflows[0].value_count++};
        workflow_capability.workflows[0].value_types.push_back(CoreValueTypeId{0});
        workflow_cap_region.statements.insert(
            workflow_cap_region.statements.begin() + 1,
            CoreStmt{CoreCapabilityCallStmt{workflow_cap_value,
                                            CoreCapabilityId{0},
                                            "WorkflowEcho",
                                            {CoreValueId{0}}},
                     std::nullopt});
        const auto workflow_cap_verified = verify_core_program(workflow_capability);
        const auto workflow_cap_result = emit_workflow(workflow_capability);
        const bool has_outside_flow = std::any_of(
            workflow_cap_verified.diagnostics.begin(),
            workflow_cap_verified.diagnostics.end(),
            [](const auto &diagnostic) {
                return diagnostic.code == verify::kCapabilityOutsideFlow;
            });
        check(!workflow_cap_verified.ok() && has_outside_flow &&
                  !workflow_cap_result.artifact.has_value() &&
                  has_codegen_code(workflow_cap_result,
                                   backends::core_wasm_diag::kInvalidCore),
              "workflow-region capability remains a Core outside-Flow rejection");

        auto non_ancestor = make_e3_workflow_program();
        non_ancestor.workflows[0].nodes[1].after.clear();
        const auto non_ancestor_result = emit_workflow(non_ancestor);
        check(!verify_core_program(non_ancestor).ok() &&
                  !non_ancestor_result.artifact.has_value() &&
                  has_codegen_code(non_ancestor_result,
                                   backends::core_wasm_diag::kInvalidCore),
              "workflow non-ancestor output read remains a standalone Core rejection");
    }

    // Test 16 (RFC 0026 P6-0..P6-2 / KR6.6): computation codegen.
    //
    // The scaffold introduces a fail-closed subset gate ("P6 computation
    // handler" vs KR6.5 orchestration), a per-handler body builder with SSA
    // local allocation, and compile-time-exhaustive Expr/Stmt visitors. P6-1
    // landed the scalar stack machine compiled inline in step(); P6-2 promotes
    // each computed handler to its own `() -> i32` wasm FUNCTION and lowers
    // structured `if` to block/if/else with goto = `br` to the handler block.
    // Expression and statement kinds not yet landed still fail with
    // wasm.UNSUPPORTED_ORCHESTRATION and no artifact, while the canonical E1-E3
    // shapes remain byte-identical.
    {
        using namespace ahfl::ir::core;
        namespace backends = ahfl::backends;

        // The exact 382-byte E1 artifact blessed at the P6-0 landing. The
        // scaffold is additive; any drift here means an existing emit path moved.
        constexpr std::uint8_t kP60E1Snapshot[382] = {
            0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x1d, 0x05, 0x60,
            0x00, 0x01, 0x7f, 0x60, 0x01, 0x7f, 0x01, 0x7f, 0x60, 0x02, 0x7f, 0x7f,
            0x00, 0x60, 0x02, 0x7f, 0x7f, 0x01, 0x7f, 0x60, 0x02, 0x7f, 0x7f, 0x03,
            0x7f, 0x7f, 0x7f, 0x03, 0x08, 0x07, 0x01, 0x02, 0x00, 0x00, 0x00, 0x03,
            0x04, 0x05, 0x03, 0x01, 0x00, 0x01, 0x06, 0x1b, 0x05, 0x7f, 0x01, 0x41,
            0x01, 0x0b, 0x7f, 0x01, 0x41, 0x00, 0x0b, 0x7f, 0x00, 0x41, 0x01, 0x0b,
            0x7f, 0x01, 0x41, 0x80, 0x08, 0x0b, 0x7f, 0x01, 0x41, 0x00, 0x0b, 0x07,
            0x66, 0x09, 0x06, 0x6d, 0x65, 0x6d, 0x6f, 0x72, 0x79, 0x02, 0x00, 0x05,
            0x61, 0x6c, 0x6c, 0x6f, 0x63, 0x00, 0x00, 0x07, 0x64, 0x65, 0x61, 0x6c,
            0x6c, 0x6f, 0x63, 0x00, 0x01, 0x03, 0x72, 0x75, 0x6e, 0x00, 0x05, 0x04,
            0x72, 0x75, 0x6e, 0x32, 0x00, 0x06, 0x04, 0x73, 0x74, 0x65, 0x70, 0x00,
            0x04, 0x0d, 0x63, 0x75, 0x72, 0x72, 0x65, 0x6e, 0x74, 0x5f, 0x73, 0x74,
            0x61, 0x74, 0x65, 0x00, 0x02, 0x10, 0x74, 0x72, 0x61, 0x6e, 0x73, 0x69,
            0x74, 0x69, 0x6f, 0x6e, 0x5f, 0x63, 0x6f, 0x75, 0x6e, 0x74, 0x03, 0x01,
            0x10, 0x61, 0x68, 0x66, 0x6c, 0x5f, 0x61, 0x62, 0x69, 0x5f, 0x76, 0x65,
            0x72, 0x73, 0x69, 0x6f, 0x6e, 0x03, 0x02, 0x0a, 0xc0, 0x01, 0x07, 0x11,
            0x01, 0x01, 0x7f, 0x23, 0x03, 0x21, 0x01, 0x23, 0x03, 0x20, 0x00, 0x6a,
            0x24, 0x03, 0x20, 0x01, 0x0b, 0x03, 0x00, 0x01, 0x0b, 0x04, 0x00, 0x23,
            0x00, 0x0b, 0x0a, 0x00, 0x41, 0x00, 0x23, 0x00, 0x41, 0x00, 0x46, 0x72,
            0x0b, 0x24, 0x00, 0x23, 0x00, 0x41, 0x00, 0x46, 0x04, 0x7f, 0x41, 0x00,
            0x05, 0x23, 0x00, 0x41, 0x01, 0x46, 0x04, 0x7f, 0x41, 0x00, 0x24, 0x00,
            0x23, 0x01, 0x41, 0x01, 0x6a, 0x24, 0x01, 0x41, 0x00, 0x05, 0x00, 0x0b,
            0x0b, 0x0b, 0x2f, 0x01, 0x01, 0x7f, 0x41, 0x01, 0x24, 0x00, 0x41, 0x00,
            0x24, 0x01, 0x41, 0x03, 0x21, 0x02, 0x02, 0x40, 0x03, 0x40, 0x10, 0x03,
            0x0d, 0x01, 0x20, 0x02, 0x45, 0x04, 0x40, 0x00, 0x0b, 0x20, 0x02, 0x41,
            0x01, 0x6b, 0x21, 0x02, 0x10, 0x04, 0x1a, 0x0c, 0x00, 0x0b, 0x0b, 0x20,
            0x00, 0x0b, 0x43, 0x01, 0x04, 0x7f, 0x23, 0x04, 0x04, 0x40, 0x00, 0x0b,
            0x41, 0x01, 0x24, 0x00, 0x41, 0x00, 0x24, 0x01, 0x41, 0x03, 0x21, 0x05,
            0x02, 0x40, 0x03, 0x40, 0x10, 0x03, 0x0d, 0x01, 0x20, 0x05, 0x45, 0x04,
            0x40, 0x00, 0x0b, 0x20, 0x05, 0x41, 0x01, 0x6b, 0x21, 0x05, 0x10, 0x04,
            0x1a, 0x0c, 0x00, 0x0b, 0x0b, 0x23, 0x00, 0x41, 0x00, 0x46, 0x04, 0x40,
            0x41, 0x00, 0x20, 0x00, 0x20, 0x01, 0x0f, 0x0b, 0x00, 0x0b,
        };

        const auto e1_program = make_e1_core_program();
        const auto e1_layout = compute_core_layouts(e1_program);
        const auto e1_result = backends::emit_core_wasm(
            e1_program, *e1_layout.table,
            {CoreAgentId{0}, backends::WasmProfileKind::Wasi});
        const std::vector<std::uint8_t> e1_snapshot(
            std::begin(kP60E1Snapshot), std::end(kP60E1Snapshot));
        check(e1_result.ok() && e1_result.artifact->bytes == e1_snapshot,
              "P6-0 scaffold leaves the canonical E1 artifact byte-identical");

        constexpr std::string_view kP6ScaffoldPrefix = "RFC 0026 P6 scalar codegen cannot lower";

        // Opcode byte constants pinned here (mirror of the encoder's scalar
        // ladder); byte-pattern assertions prove the right physical instruction
        // was selected for the operand's P4-D scalar repr.
        constexpr std::uint8_t kOpI32Const = 0x41;
        constexpr std::uint8_t kOpI32Eqz = 0x45;
        constexpr std::uint8_t kOpI32LtS = 0x48;
        constexpr std::uint8_t kOpI32Add = 0x6a;
        constexpr std::uint8_t kOpI32DivS = 0x6d;
        constexpr std::uint8_t kOpCall = 0x10;
        constexpr std::uint8_t kOpI64Const = 0x42;
        constexpr std::uint8_t kOpI64LtS = 0x53;
        constexpr std::uint8_t kOpI64Add = 0x7c;

        const auto emit_agent = [&](const CoreProgram &program, const CoreLayoutTable &layout) {
            return backends::emit_core_wasm(
                program, layout, {CoreAgentId{0}, backends::WasmProfileKind::Wasi});
        };

        // Build a 3-state agent (Done final, Start computed goto, High) whose
        // Start handler branches on a scalar comparison. `wide` selects the
        // integer value type: bounded Int(0..1000) is the i32 scalar repr, an
        // unbounded Int is i64. `op` chooses Add vs Div over two i32 literals
        // so the same builder proves arithmetic + comparison lowering and the
        // signed-division trap body. Every expr/value is consumed and the
        // program is verifier-clean with a finalized layout.
        auto make_computed_goto_program = [&](bool wide, CoreBinaryOp arith) {
            auto program = make_e1_core_program();
            program.value_types.push_back(CoreValueType{
                CoreVtInt{std::make_pair<std::int64_t, std::int64_t>(0, 1000)}}); // vt1 i32
            program.value_types.push_back(CoreValueType{CoreVtBool{}});           // vt2
            if (wide) {
                // Replace vt1 with an UNBOUNDED Int -> i64 scalar repr.
                program.value_types[1] = CoreValueType{CoreVtInt{std::nullopt}};
            }
            auto &agent = program.agents[0];
            agent.states = {"Done", "Start", "High"};
            agent.finals = {CoreStateId{0}};
            agent.transitions = {{CoreStateId{1}, CoreStateId{0}},
                                 {CoreStateId{1}, CoreStateId{2}},
                                 {CoreStateId{2}, CoreStateId{0}}};
            auto &flow = program.flows[0];
            // expr0 is the existing Done identity path (vt0).
            flow.exprs.push_back(CoreExpr{CoreLiteralExpr{CoreLiteralKind::Integer, "10"},
                                          std::nullopt,
                                          CoreValueTypeId{1}}); // expr1 -> v3
            flow.exprs.push_back(CoreExpr{CoreLiteralExpr{CoreLiteralKind::Integer, "20"},
                                          std::nullopt,
                                          CoreValueTypeId{1}}); // expr2 -> v4
            flow.exprs.push_back(CoreExpr{
                CoreValueRefExpr{CoreValueId{3}}, std::nullopt, CoreValueTypeId{1}}); // expr3
            flow.exprs.push_back(CoreExpr{
                CoreValueRefExpr{CoreValueId{4}}, std::nullopt, CoreValueTypeId{1}}); // expr4
            flow.exprs.push_back(CoreExpr{CoreBinaryExpr{arith, CoreExprId{3}, CoreExprId{4}},
                                          std::nullopt,
                                          CoreValueTypeId{1}}); // expr5 add/div v1
            flow.exprs.push_back(CoreExpr{
                CoreValueRefExpr{CoreValueId{1}}, std::nullopt, CoreValueTypeId{1}}); // expr6
            flow.exprs.push_back(CoreExpr{CoreLiteralExpr{CoreLiteralKind::Integer, "25"},
                                          std::nullopt,
                                          CoreValueTypeId{1}}); // expr7 -> const v5
            flow.exprs.push_back(CoreExpr{
                CoreValueRefExpr{CoreValueId{5}}, std::nullopt, CoreValueTypeId{1}}); // expr8
            flow.exprs.push_back(
                CoreExpr{CoreBinaryExpr{CoreBinaryOp::Lt, CoreExprId{6}, CoreExprId{8}},
                         std::nullopt,
                         CoreValueTypeId{2}}); // expr9 lt -> v2
            flow.value_count = 6;
            flow.value_types = {CoreValueTypeId{0},
                                CoreValueTypeId{1},
                                CoreValueTypeId{2},
                                CoreValueTypeId{1},
                                CoreValueTypeId{1},
                                CoreValueTypeId{1}};
            auto &start = flow.states[1].body;
            start.statements.clear();
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{3}, CoreExprId{1}}, std::nullopt});
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{4}, CoreExprId{2}}, std::nullopt});
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{1}, CoreExprId{5}}, std::nullopt});
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{5}, CoreExprId{7}}, std::nullopt});
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{2}, CoreExprId{9}}, std::nullopt});
            CoreIfStmt branch;
            branch.condition = CoreValueId{2};
            branch.then_region = std::make_unique<CoreRegion>();
            branch.then_region->statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{2}, "High"}, std::nullopt});
            branch.else_region = std::make_unique<CoreRegion>();
            branch.else_region->statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{0}, "Done"}, std::nullopt});
            start.statements.push_back(CoreStmt{std::move(branch), std::nullopt});
            // High handler (state 2) needs appending after Start.
            CoreFlowState high;
            high.state = CoreStateId{2};
            high.state_name = "High";
            high.body.statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{0}, "Done"}, std::nullopt});
            flow.states.push_back(std::move(high));
            return program;
        };

        // P6-2: a scalar computation handler compiles to its OWN `() -> i32`
        // wasm FUNCTION. The seven fixed ABI functions occupy indices 0..6
        // (import_count == 0 here), so the first compiled handler is index 7;
        // `step()` becomes a thin `call`-based dispatch ladder whose body no
        // longer contains the arithmetic. `scalar_handler_body` is the single
        // accessor every P6 assertion below reads.
        constexpr std::uint32_t kFirstHandlerFunction = 7;
        const auto scalar_handler_body = [](const std::vector<std::uint8_t> &bytes) {
            return wasm_function_body(bytes, kFirstHandlerFunction);
        };

        // P6-2 fixture A (i32): bounded-Int add + signed comparison drives a
        // computed goto. The compiled handler function must contain i32.add and
        // i32.lt_s and the module must emit successfully.
        {
            auto program = make_computed_goto_program(false, CoreBinaryOp::Add);
            check(verify_core_program(program).ok() && compute_core_layouts(program).ok(),
                  "P6-2 i32 computed-goto fixture is verified Core with a layout");
            const auto layout = compute_core_layouts(program);
            const auto emitted = emit_agent(program, *layout.table);
            const auto handler_body =
                emitted.artifact ? scalar_handler_body(emitted.artifact->bytes) : std::nullopt;
            const auto contains = [&](std::uint8_t op) {
                return handler_body.has_value() &&
                       std::find(handler_body->begin(), handler_body->end(), op) !=
                           handler_body->end();
            };
            const auto step_body =
                emitted.artifact ? wasm_function_body(emitted.artifact->bytes, 4) : std::nullopt;
            check(emitted.ok() && handler_body.has_value() && contains(kOpI32Add) &&
                      contains(kOpI32LtS),
                  "P6-2 compiles a bounded-Int add/lt computed-goto handler to its own "
                  "i32 function");
            check(!contains(kOpI64Const), "P6-2 i32 handler emits no i64.const");
            // step() dispatches by CALL, not by inlining the comparison. The
            // bare-goto state still bumps transition_count with i32.add, so the
            // discriminating opcode is the handler-only i32.lt_s.
            check(step_body.has_value() &&
                      std::find(step_body->begin(), step_body->end(), kOpI32LtS) ==
                          step_body->end() &&
                      std::find(step_body->begin(), step_body->end(), kOpCall) !=
                          step_body->end(),
                  "P6-2 step() dispatches to the handler function instead of inlining it");
        }

        // P6-2 fixture A2 (i64): the same arithmetic over an UNBOUNDED Int
        // selects the i64 scalar ladder (i64.add / i64.lt_s, i64.const).
        {
            auto program = make_computed_goto_program(true, CoreBinaryOp::Add);
            check(verify_core_program(program).ok() && compute_core_layouts(program).ok(),
                  "P6-2 i64 computed-goto fixture is verified Core with a layout");
            const auto layout = compute_core_layouts(program);
            const auto emitted = emit_agent(program, *layout.table);
            const auto handler_body =
                emitted.artifact ? scalar_handler_body(emitted.artifact->bytes) : std::nullopt;
            const auto contains = [&](std::uint8_t op) {
                return handler_body.has_value() &&
                       std::find(handler_body->begin(), handler_body->end(), op) !=
                           handler_body->end();
            };
            check(emitted.ok() && handler_body.has_value() && contains(kOpI64Add) &&
                      contains(kOpI64LtS) && contains(kOpI64Const),
                  "P6-2 compiles an unbounded-Int add/lt computed-goto handler to "
                  "the i64 scalar ladder");
            // The integer comparison must NOT select the i32 signed opcode;
            // the condition Bool is still i32 but the arithmetic compare is i64.
            check(std::find(handler_body->begin(), handler_body->end(), kOpI32LtS) ==
                      handler_body->end(),
                  "P6-2 i64 integer comparison selects no i32.lt_s opcode");
        }

        // P6-2 fixture B: a signed division lowers to i32.div_s; a literal
        // zero divisor is NOT statically rejected (wasm traps at runtime).
        {
            auto program = make_computed_goto_program(false, CoreBinaryOp::Div);
            // Force the divisor literal to zero so the runtime trap path exists.
            program.flows[0].exprs[2] = CoreExpr{
                CoreLiteralExpr{CoreLiteralKind::Integer, "0"}, std::nullopt, CoreValueTypeId{1}};
            check(verify_core_program(program).ok(),
                  "P6-2 literal-divide-by-zero is a verifier-clean (runtime trap) "
                  "program");
            const auto layout = compute_core_layouts(program);
            const auto emitted = emit_agent(program, *layout.table);
            const auto handler_body =
                emitted.artifact ? scalar_handler_body(emitted.artifact->bytes) : std::nullopt;
            check(emitted.ok() && handler_body.has_value() &&
                      std::find(handler_body->begin(), handler_body->end(), kOpI32DivS) !=
                          handler_body->end(),
                  "P6-2 division-by-zero handler contains i32.div_s (runtime trap)");
        }

        // P6-2 fixture C: logical Not lowers to i32.eqz on a Bool value.
        {
            auto program = make_e1_core_program();
            program.value_types.push_back(CoreValueType{CoreVtBool{}}); // vt1
            auto &agent = program.agents[0];
            agent.states = {"Done", "Start", "High"};
            agent.transitions = {{CoreStateId{1}, CoreStateId{0}},
                                 {CoreStateId{1}, CoreStateId{2}},
                                 {CoreStateId{2}, CoreStateId{0}}};
            auto &flow = program.flows[0];
            flow.exprs.push_back(CoreExpr{CoreLiteralExpr{CoreLiteralKind::Bool, "true"},
                                          std::nullopt,
                                          CoreValueTypeId{1}}); // expr1 -> v1
            flow.exprs.push_back(CoreExpr{
                CoreValueRefExpr{CoreValueId{1}}, std::nullopt, CoreValueTypeId{1}}); // expr2
            flow.exprs.push_back(CoreExpr{CoreUnaryExpr{CoreUnaryOp::Not, CoreExprId{2}},
                                          std::nullopt,
                                          CoreValueTypeId{1}}); // expr3 -> v2
            flow.value_count = 3;
            flow.value_types = {CoreValueTypeId{0}, CoreValueTypeId{1}, CoreValueTypeId{1}};
            auto &start = flow.states[1].body;
            start.statements.clear();
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{1}, CoreExprId{1}}, std::nullopt});
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{2}, CoreExprId{3}}, std::nullopt});
            CoreIfStmt branch;
            branch.condition = CoreValueId{2};
            branch.then_region = std::make_unique<CoreRegion>();
            branch.then_region->statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{2}, "High"}, std::nullopt});
            branch.else_region = std::make_unique<CoreRegion>();
            branch.else_region->statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{0}, "Done"}, std::nullopt});
            start.statements.push_back(CoreStmt{std::move(branch), std::nullopt});
            CoreFlowState high;
            high.state = CoreStateId{2};
            high.state_name = "High";
            high.body.statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{0}, "Done"}, std::nullopt});
            flow.states.push_back(std::move(high));
            const auto layout = compute_core_layouts(program);
            const auto emitted = emit_agent(program, *layout.table);
            const auto handler_body =
                emitted.artifact ? scalar_handler_body(emitted.artifact->bytes) : std::nullopt;
            check(emitted.ok() && handler_body.has_value() &&
                      std::find(handler_body->begin(), handler_body->end(), kOpI32Eqz) !=
                          handler_body->end() &&
                      std::find(handler_body->begin(), handler_body->end(), kOpI32Const) !=
                          handler_body->end(),
                  "P6-2 compiles Bool Not to i32.eqz with an i32.const literal");
        }

        // P6-2 fail-closed: an integer literal that overflows the bounded
        // i32 scalar range must fail closed (never silently truncate).
        {
            auto program = make_computed_goto_program(false, CoreBinaryOp::Add);
            program.flows[0].exprs[7] =
                CoreExpr{CoreLiteralExpr{CoreLiteralKind::Integer, "9999999999"},
                         std::nullopt,
                         CoreValueTypeId{1}};
            const auto layout = compute_core_layouts(program);
            const auto emitted = emit_agent(program, *layout.table);
            check(
                !emitted.artifact.has_value() &&
                    has_codegen_code(emitted, backends::core_wasm_diag::kUnsupportedOrchestration),
                "P6-2 fails closed on an integer literal exceeding the i32 range");
        }

        // P6-2 fail-closed: a computed-goto branch whose target is NOT in the
        // agent's declared transition table is kInvalidCore (never silently
        // accepted). The then branch legally targets High(2); the else branch
        // illegally targets Done(0) which is NOT declared from Start(1).
        {
            auto program = make_computed_goto_program(false, CoreBinaryOp::Add);
            auto &agent = program.agents[0];
            // Drop the Start(1) -> Done(0) legal edge, leaving only -> High(2).
            agent.transitions.erase(std::remove_if(agent.transitions.begin(),
                                                   agent.transitions.end(),
                                                   [](const CoreTransition &edge) {
                                                       return edge.from == CoreStateId{1} &&
                                                              edge.to == CoreStateId{0};
                                                   }),
                                    agent.transitions.end());
            check(verify_core_program(program).ok(),
                  "computed-goto illegal-edge fixture is verifier-clean (the Core "
                  "verifier does not re-check declared transitions)");
            const auto layout = compute_core_layouts(program);
            const auto emitted = emit_agent(program, *layout.table);
            check(!emitted.artifact.has_value() &&
                      has_codegen_code(emitted, backends::core_wasm_diag::kInvalidCore),
                  "P6-2 rejects a computed goto outside the legal transition table");
        }

        // P6-3: a CoreMatchStmt in a computation region is now LOWERED, not
        // rejected. A wildcard statement match compiles to the S/B/C arm chain:
        // three nested `block` (0x02 0x40) per arm level plus the completion and
        // chain blocks, and the `i32.eqz` (0x45) + `br_if` (0x0d) inversion that
        // routes a mis-match to the next arm. The arm/fallback bodies here are
        // gotos, so the match diverges and needs no completion block.
        {
            auto program = make_e1_core_program();
            program.value_types.push_back(CoreValueType{CoreVtBool{}});
            auto &flow = program.flows[0];
            flow.exprs.push_back(CoreExpr{
                CoreLiteralExpr{CoreLiteralKind::Bool, "true"}, std::nullopt, CoreValueTypeId{1}});
            flow.value_count = 2;
            flow.value_types = {CoreValueTypeId{0}, CoreValueTypeId{1}};
            flow.patterns.push_back(CorePattern{CoreWildcardPat{}, std::nullopt});
            auto &start = flow.states[1].body;
            start.statements.clear();
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{1}, CoreExprId{1}}, std::nullopt});
            CoreMatchStmt match;
            match.scrutinee = CoreValueId{1};
            match.has_result = false;
            CoreMatchArm arm;
            arm.pattern = CorePatternId{0};
            arm.body = std::make_unique<CoreRegion>();
            arm.body->statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{0}, "Done"}, std::nullopt});
            match.arms.push_back(std::move(arm));
            match.fallback_region = std::make_unique<CoreRegion>();
            match.fallback_region->statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{0}, "Done"}, std::nullopt});
            start.statements.push_back(CoreStmt{std::move(match), std::nullopt});
            check(verify_core_program(program).ok() && compute_core_layouts(program).ok(),
                  "P6-3 match fixture is verified Core with a finalized layout");
            const auto layout = compute_core_layouts(program);
            const auto emitted = emit_agent(program, *layout.table);
            const auto handler_body =
                emitted.artifact ? scalar_handler_body(emitted.artifact->bytes) : std::nullopt;
            check(emitted.artifact.has_value() && handler_body.has_value(),
                  "P6-3 lowers a wildcard statement match to a real function");
            constexpr std::uint8_t kOpBlock = 0x02;
            constexpr std::uint8_t kOpBrIf = 0x0d;
            constexpr std::uint8_t kOpI32Eqz = 0x45;
            constexpr std::uint8_t kEmptyBlock = 0x40;
            check(handler_body.has_value() &&
                      // S, B and C_i each open a void block, in that order.
                      contains_bytes(*handler_body,
                                     {kOpBlock, kEmptyBlock, kOpBlock, kEmptyBlock,
                                      kOpBlock, kEmptyBlock}) &&
                      // A mis-match is negated then branches to the next arm.
                      contains_bytes(*handler_body, {kOpI32Eqz, kOpBrIf, 0x00}),
                  "P6-3 emits the S/B/C nested blocks + a negated br_if per arm");
            check(emitted.artifact.has_value() &&
                      !has_codegen_message(emitted, "match lowering is a later P6 slice"),
                  "P6-3 no longer rejects a CoreMatchStmt");
        }

        // P6-4: a payload-bearing enum scrutinee is now an ADDRESSED aggregate.
        // Its match loads the tag at offset 0 and a payload sub-pattern descends
        // to `payload_offset + slot_offset`, so the arm chain lowers instead of
        // falling out of the subset. (The former P6-3 fail-closed case is
        // promoted here because the dependency it named has landed.)
        {
            auto program = make_e1_core_program();
            // The payload slot is an ordinary Bool; the enum is finite.
            program.value_types.push_back(CoreValueType{CoreVtBool{}}); // vt1
            program.value_types.push_back(
                CoreValueType{CoreVtNominal{CoreTypeId{1}, {}, std::nullopt}}); // vt2
            CoreTypeDecl level;
            level.kind = CoreTypeDecl::Kind::Enum;
            level.name = "app::Level";
            level.variants = {"Low", "High"};
            level.variant_payloads = {CoreTypeDecl::VariantPayload{},
                                      CoreTypeDecl::VariantPayload{
                                          CoreTypeDecl::VariantPayload::Kind::Tuple,
                                          {CoreMemberTypeTemplateNodeId{0}},
                                          {}}};
            level.member_type_templates.push_back(
                CoreMemberTypeTemplateNode{CoreMemberTypeTemplateKind::Concrete,
                                           CoreValueTypeId{1},
                                           0,
                                           CoreTypeId{},
                                           std::nullopt,
                                           {},
                                           CoreMemberTypeTemplateNodeId{}});
            program.types.push_back(std::move(level));
            auto &flow = program.flows[0];
            flow.exprs.push_back(CoreExpr{
                CoreQualifiedExpr{"app::Level::Low", CoreTypeId{1}, CoreVariantId{0}, true},
                std::nullopt,
                CoreValueTypeId{2}});
            flow.value_count = 2;
            flow.value_types = {CoreValueTypeId{0}, CoreValueTypeId{2}};
            // Pattern 0: Low (unit). Pattern 1: High(<one payload>).
            flow.patterns.push_back(CorePattern{CoreWildcardPat{}, std::nullopt});
            flow.patterns.push_back(CorePattern{
                CoreVariantPat{CoreTypeId{1}, CoreVariantId{1}, {CorePatternId{0}}, {}, false},
                std::nullopt});
            auto &start = flow.states[1].body;
            start.statements.clear();
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{1}, CoreExprId{1}}, std::nullopt});
            CoreMatchStmt match;
            match.scrutinee = CoreValueId{1};
            match.has_result = false;
            CoreMatchArm arm;
            arm.pattern = CorePatternId{1};
            arm.body = std::make_unique<CoreRegion>();
            arm.body->statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{0}, "Done"}, std::nullopt});
            match.arms.push_back(std::move(arm));
            match.fallback_region = std::make_unique<CoreRegion>();
            match.fallback_region->statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{0}, "Done"}, std::nullopt});
            start.statements.push_back(CoreStmt{std::move(match), std::nullopt});
            check(verify_core_program(program).ok() && compute_core_layouts(program).ok(),
                  "P6-4 payload-enum match fixture is verified Core with a layout");
            const auto layout = compute_core_layouts(program);
            const auto emitted = emit_agent(program, *layout.table);
            const auto handler_body =
                emitted.artifact ? scalar_handler_body(emitted.artifact->bytes) : std::nullopt;
            // The addressed-enum tag is loaded from offset 0 (`i32.load align=2`),
            // then compared with the variant id.
            constexpr std::uint8_t kOpI32LoadP = 0x28;
            constexpr std::uint8_t kOpI32EqP = 0x46;
            check(emitted.ok() && handler_body.has_value() &&
                      contains_bytes(*handler_body, {kOpI32LoadP, 0x02, 0x00}) &&
                      contains_bytes(*handler_body, {kOpI32EqP}),
                  "P6-4 lowers a payload-enum match with a tag load at offset 0");
        }

        // P6-4: a struct constructor stores its operands at the field offsets the
        // P4-D layout DERIVES, in typed-field-id order (never source order). The
        // injected struct has fields `a: Int(0..1000)` (i32) then `b: Int`
        // (unbounded -> i64), so the layout is `a@0` (4 bytes) and `b@8` (8-byte
        // aligned). The stores must emit `i32.store align=2 offset=0` and
        // `i64.store align=3 offset=8`, proving the offset came from the layout,
        // not from guessing.
        {
            constexpr std::uint8_t kOpI32StoreP = 0x36;
            constexpr std::uint8_t kOpI64StoreP = 0x37;
            auto program = make_e1_core_program();
            program.value_types.push_back(
                CoreValueType{CoreVtInt{std::make_pair<std::int64_t, std::int64_t>(0, 1000)}});
            program.value_types.push_back(CoreValueType{CoreVtInt{std::nullopt}});
            program.value_types.push_back(
                CoreValueType{CoreVtNominal{CoreTypeId{1}, {}, std::nullopt}});
            CoreTypeDecl pair;
            pair.kind = CoreTypeDecl::Kind::Struct;
            pair.name = "app::Pair";
            pair.fields = {"a", "b"};
            pair.field_nominal_types = {CoreTypeId{}, CoreTypeId{}};
            pair.field_has_default = {false, false};
            pair.field_type_template_roots = {CoreMemberTypeTemplateNodeId{0},
                                              CoreMemberTypeTemplateNodeId{1}};
            pair.member_type_templates.push_back(
                CoreMemberTypeTemplateNode{CoreMemberTypeTemplateKind::Concrete,
                                           CoreValueTypeId{1},
                                           0,
                                           CoreTypeId{},
                                           std::nullopt,
                                           {},
                                           CoreMemberTypeTemplateNodeId{}});
            pair.member_type_templates.push_back(
                CoreMemberTypeTemplateNode{CoreMemberTypeTemplateKind::Concrete,
                                           CoreValueTypeId{2},
                                           0,
                                           CoreTypeId{},
                                           std::nullopt,
                                           {},
                                           CoreMemberTypeTemplateNodeId{}});
            program.types.push_back(std::move(pair));
            auto &flow = program.flows[0];
            flow.exprs.push_back(CoreExpr{
                CoreLiteralExpr{CoreLiteralKind::Integer, "7"}, std::nullopt, CoreValueTypeId{1}});
            flow.exprs.push_back(CoreExpr{
                CoreLiteralExpr{CoreLiteralKind::Integer, "9"}, std::nullopt, CoreValueTypeId{2}});
            // A struct literal written b-then-a proves field IDENTITY wins.
            flow.exprs.push_back(CoreExpr{
                CoreConstructExpr{"app::Pair",
                                  "",
                                  false,
                                  CoreTypeId{1},
                                  CoreVariantId{},
                                  true,
                                  {CoreConstructArg{CoreFieldId{1}, CoreValueId{2}},
                                   CoreConstructArg{CoreFieldId{0}, CoreValueId{1}}}},
                std::nullopt,
                CoreValueTypeId{3}});
            flow.exprs.push_back(CoreExpr{
                CoreLiteralExpr{CoreLiteralKind::Bool, "true"}, std::nullopt, CoreValueTypeId{4}});
            flow.value_count = 5;
            flow.value_types = {CoreValueTypeId{0}, CoreValueTypeId{1}, CoreValueTypeId{2},
                                CoreValueTypeId{3}, CoreValueTypeId{4}};
            program.value_types.push_back(CoreValueType{CoreVtBool{}}); // vt4
            auto &agent = program.agents[0];
            agent.states = {"Done", "Start", "High"};
            agent.finals = {CoreStateId{0}};
            agent.transitions = {{CoreStateId{1}, CoreStateId{0}},
                                 {CoreStateId{1}, CoreStateId{2}},
                                 {CoreStateId{2}, CoreStateId{0}}};
            auto &start = flow.states[1].body;
            start.statements.clear();
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{1}, CoreExprId{1}}, std::nullopt});
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{2}, CoreExprId{2}}, std::nullopt});
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{3}, CoreExprId{3}}, std::nullopt});
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{4}, CoreExprId{4}}, std::nullopt});
            CoreIfStmt branch;
            branch.condition = CoreValueId{4};
            branch.then_region = std::make_unique<CoreRegion>();
            branch.then_region->statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{2}, "High"}, std::nullopt});
            branch.else_region = std::make_unique<CoreRegion>();
            branch.else_region->statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{0}, "Done"}, std::nullopt});
            start.statements.push_back(CoreStmt{std::move(branch), std::nullopt});
            CoreFlowState high;
            high.state = CoreStateId{2};
            high.state_name = "High";
            high.body.statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{0}, "Done"}, std::nullopt});
            flow.states.push_back(std::move(high));
            check(compute_core_layouts(program).ok(),
                  "P6-4 struct constructor fixture has a finalized layout");
            const auto layout = compute_core_layouts(program);
            const auto emitted = emit_agent(program, *layout.table);
            const auto handler_body =
                emitted.artifact ? scalar_handler_body(emitted.artifact->bytes) : std::nullopt;
            // a (i32) at offset 0; b (i64) at offset 8.
            check(emitted.ok() && handler_body.has_value() &&
                      contains_bytes(*handler_body, {kOpI32StoreP, 0x02, 0x00}) &&
                      contains_bytes(*handler_body, {kOpI64StoreP, 0x03, 0x08}),
                  "P6-4 stores struct fields at their P4-D offsets (a@0 i32, b@8 i64)");
            // The i64 field must NOT be written at offset 0; that would be a
            // source-order or i32-layout mistake.
            check(handler_body.has_value() &&
                      !contains_bytes(*handler_body, {kOpI64StoreP, 0x03, 0x00}),
                  "P6-4 does not store the second field at offset 0 (identity order)");
        }

        // P6-4: a projection READ loads a struct field at its P4-D offset, and a
        // context STORE writes one. The context is a struct with fields
        // `n: Int(0..1000)` (i32) then `m: Int` (unbounded -> i64), so a store
        // into `ctx.m` must emit `i64.store align=3 offset=8` at the context base
        // and a read of `ctx.n` must emit `i32.load align=2 offset=0`.
        {
            constexpr std::uint8_t kOpI32LoadP = 0x28;
            constexpr std::uint8_t kOpI64StoreP = 0x37;
            auto program = make_e1_core_program();
            program.value_types.push_back(
                CoreValueType{CoreVtInt{std::make_pair<std::int64_t, std::int64_t>(0, 1000)}});
            program.value_types.push_back(CoreValueType{CoreVtInt{std::nullopt}});
            program.value_types.push_back(
                CoreValueType{CoreVtNominal{CoreTypeId{1}, {}, std::nullopt}}); // vt3 = Ctx
            CoreTypeDecl ctx;
            ctx.kind = CoreTypeDecl::Kind::Struct;
            ctx.name = "app::Ctx";
            ctx.fields = {"n", "m"};
            ctx.field_nominal_types = {CoreTypeId{}, CoreTypeId{}};
            ctx.field_has_default = {false, false};
            ctx.field_type_template_roots = {CoreMemberTypeTemplateNodeId{0},
                                             CoreMemberTypeTemplateNodeId{1}};
            ctx.member_type_templates.push_back(
                CoreMemberTypeTemplateNode{CoreMemberTypeTemplateKind::Concrete,
                                           CoreValueTypeId{1},
                                           0,
                                           CoreTypeId{},
                                           std::nullopt,
                                           {},
                                           CoreMemberTypeTemplateNodeId{}});
            ctx.member_type_templates.push_back(
                CoreMemberTypeTemplateNode{CoreMemberTypeTemplateKind::Concrete,
                                           CoreValueTypeId{2},
                                           0,
                                           CoreTypeId{},
                                           std::nullopt,
                                           {},
                                           CoreMemberTypeTemplateNodeId{}});
            program.types.push_back(std::move(ctx));
            auto &agent = program.agents[0];
            agent.context_kind = CoreAgentDecl::ContextKind::Struct;
            agent.context_type = CoreTypeId{1};
            agent.states = {"Done", "Start", "High"};
            agent.finals = {CoreStateId{0}};
            agent.transitions = {{CoreStateId{1}, CoreStateId{0}},
                                 {CoreStateId{1}, CoreStateId{2}},
                                 {CoreStateId{2}, CoreStateId{0}}};
            auto &flow = program.flows[0];
            flow.exprs.push_back(CoreExpr{
                CoreLiteralExpr{CoreLiteralKind::Integer, "5"}, std::nullopt, CoreValueTypeId{1}});
            flow.exprs.push_back(CoreExpr{
                CoreLiteralExpr{CoreLiteralKind::Integer, "5"}, std::nullopt, CoreValueTypeId{2}});
            flow.exprs.push_back(CoreExpr{
                CoreLiteralExpr{CoreLiteralKind::Bool, "true"}, std::nullopt, CoreValueTypeId{4}});
            flow.value_count = 4;
            flow.value_types = {CoreValueTypeId{0}, CoreValueTypeId{1}, CoreValueTypeId{2},
                                CoreValueTypeId{4}};
            program.value_types.push_back(CoreValueType{CoreVtBool{}}); // vt4
            auto &start = flow.states[1].body;
            start.statements.clear();
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{1}, CoreExprId{1}}, std::nullopt});
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{2}, CoreExprId{2}}, std::nullopt});
            // `ctx.m = 5` — the store writes the i64 field at offset 8.
            CorePlace place;
            place.root = CorePathRoot::Context;
            place.root_name = "ctx";
            place.root_type = CoreTypeId{1};
            place.projection = {CoreProjectionStep{CoreTypeId{1}, CoreFieldId{1}, CoreTypeId{}}};
            place.projection_resolved = true;
            place.members = {"m"};
            start.statements.push_back(
                CoreStmt{CoreStoreStmt{std::move(place), CoreValueId{2}}, std::nullopt});
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{3}, CoreExprId{3}}, std::nullopt});
            CoreIfStmt branch;
            branch.condition = CoreValueId{3};
            branch.then_region = std::make_unique<CoreRegion>();
            branch.then_region->statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{2}, "High"}, std::nullopt});
            branch.else_region = std::make_unique<CoreRegion>();
            branch.else_region->statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{0}, "Done"}, std::nullopt});
            start.statements.push_back(CoreStmt{std::move(branch), std::nullopt});
            CoreFlowState high;
            high.state = CoreStateId{2};
            high.state_name = "High";
            high.body.statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{0}, "Done"}, std::nullopt});
            flow.states.push_back(std::move(high));
            check(verify_core_program(program).ok() && compute_core_layouts(program).ok(),
                  "P6-4 context-store fixture is verified Core with a layout");
            const auto layout = compute_core_layouts(program);
            const auto emitted = emit_agent(program, *layout.table);
            const auto handler_body =
                emitted.artifact ? scalar_handler_body(emitted.artifact->bytes) : std::nullopt;
            check(emitted.ok() && handler_body.has_value() &&
                      contains_bytes(*handler_body, {kOpI64StoreP, 0x03, 0x08}),
                  "P6-4 lowers `ctx.m = v` to i64.store offset=8 at the context base");
            check(handler_body.has_value() &&
                      !contains_bytes(*handler_body, {kOpI32LoadP, 0x02, 0x08}),
                  "P6-4 does not read the i64 context field as an i32");
        }

        // P6-4 fail-closed: a project-only fixture with a STRING leaf is not a
        // value the P6 memory model carries (a 2-word PtrLen), so reading it
        // fails closed rather than truncating to one word.
        {
            auto program = make_e1_core_program();
            // Give the input struct one String field, then read it through the
            // input frame. A String layout is a 2-word PtrLen pair, which the P6
            // value model does not carry as one load.
            program.types[0].fields = {"value"};
            program.types[0].field_nominal_types = {CoreTypeId{}};
            program.types[0].field_has_default = {false};
            program.types[0].field_type_template_roots = {CoreMemberTypeTemplateNodeId{0}};
            program.types[0].member_type_templates.push_back(
                CoreMemberTypeTemplateNode{CoreMemberTypeTemplateKind::Concrete,
                                           CoreValueTypeId{1},
                                           0,
                                           CoreTypeId{},
                                           std::nullopt,
                                           {},
                                           CoreMemberTypeTemplateNodeId{}});
            program.value_types.push_back(CoreValueType{CoreVtString{}});
            auto &flow = program.flows[0];
            flow.exprs.push_back(CoreExpr{
                CorePathExpr{CorePathRoot::Input,
                             "input",
                             {"value"},
                             CoreTypeId{0},
                             {CoreProjectionStep{CoreTypeId{0}, CoreFieldId{0}, CoreTypeId{}}},
                             true,
                             {},
                             false,
                             {}},
                std::nullopt,
                CoreValueTypeId{1}});
            flow.value_count = 2;
            flow.value_types = {CoreValueTypeId{0}, CoreValueTypeId{1}};
            auto &start = flow.states[1].body;
            start.statements.clear();
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{1}, CoreExprId{1}}, std::nullopt});
            start.statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{0}, "Done"}, std::nullopt});
            const auto layout = compute_core_layouts(program);
            const auto emitted = layout.table.has_value()
                                     ? emit_agent(program, *layout.table)
                                     : backends::CoreWasmCodegenResult{};
            check(!emitted.artifact.has_value() &&
                      has_codegen_code(emitted,
                                       backends::core_wasm_diag::kUnsupportedOrchestration),
                  "P6-4 fails closed on a String field read (no single-word load)");
        }

        // P6-3 fail-closed: a tuple pattern is a later slice; it rejects with a
        // node-specific message and no artifact.
        {
            auto program = make_e1_core_program();
            program.value_types.push_back(CoreValueType{CoreVtBool{}});
            auto &flow = program.flows[0];
            flow.exprs.push_back(CoreExpr{
                CoreLiteralExpr{CoreLiteralKind::Bool, "true"}, std::nullopt, CoreValueTypeId{1}});
            flow.value_count = 2;
            flow.value_types = {CoreValueTypeId{0}, CoreValueTypeId{1}};
            flow.patterns.push_back(CorePattern{CoreWildcardPat{}, std::nullopt});
            flow.patterns.push_back(
                CorePattern{CoreTuplePat{{CorePatternId{0}}}, std::nullopt});
            auto &start = flow.states[1].body;
            start.statements.clear();
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{1}, CoreExprId{1}}, std::nullopt});
            CoreMatchStmt match;
            match.scrutinee = CoreValueId{1};
            match.has_result = false;
            CoreMatchArm arm;
            arm.pattern = CorePatternId{1};
            arm.body = std::make_unique<CoreRegion>();
            arm.body->statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{0}, "Done"}, std::nullopt});
            match.arms.push_back(std::move(arm));
            match.fallback_region = std::make_unique<CoreRegion>();
            match.fallback_region->statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{0}, "Done"}, std::nullopt});
            start.statements.push_back(CoreStmt{std::move(match), std::nullopt});
            const auto layout = compute_core_layouts(program);
            const auto emitted = layout.table.has_value()
                                     ? emit_agent(program, *layout.table)
                                     : backends::CoreWasmCodegenResult{};
            check(!emitted.artifact.has_value() &&
                      has_codegen_code(emitted,
                                       backends::core_wasm_diag::kUnsupportedOrchestration) &&
                      has_codegen_message(emitted, "tuple patterns"),
                  "P6-3 fails closed on a tuple pattern");
        }

        // P6-3: an IntRange pattern cannot reach Core through the frontend (a
        // `match` scrutinee must be an enum, and the range patterns are for
        // Int), so its two-signed-compare lowering is pinned here directly. The
        // closed interval [start, end] is `not(v < start) and not(v > end)`, so
        // an open-interval or swapped-bound lowering misroutes half the domain.
        {
            auto program = make_e1_core_program();
            program.value_types.push_back(
                CoreValueType{CoreVtInt{std::make_pair<std::int64_t, std::int64_t>(0, 1000)}});
            auto &flow = program.flows[0];
            flow.exprs.push_back(CoreExpr{
                CoreLiteralExpr{CoreLiteralKind::Integer, "5"}, std::nullopt, CoreValueTypeId{1}});
            flow.value_count = 2;
            flow.value_types = {CoreValueTypeId{0}, CoreValueTypeId{1}};
            flow.patterns.push_back(
                CorePattern{CoreIntRangePat{3, 7}, std::nullopt});
            auto &start = flow.states[1].body;
            start.statements.clear();
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{1}, CoreExprId{1}}, std::nullopt});
            CoreMatchStmt match;
            match.scrutinee = CoreValueId{1};
            match.has_result = false;
            CoreMatchArm arm;
            arm.pattern = CorePatternId{0};
            arm.body = std::make_unique<CoreRegion>();
            arm.body->statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{0}, "Done"}, std::nullopt});
            match.arms.push_back(std::move(arm));
            match.fallback_region = std::make_unique<CoreRegion>();
            match.fallback_region->statements.push_back(
                CoreStmt{CoreTrapStmt{CoreTrapKind::NonExhaustiveMatch}, std::nullopt});
            start.statements.push_back(CoreStmt{std::move(match), std::nullopt});
            check(verify_core_program(program).ok() && compute_core_layouts(program).ok(),
                  "P6-3 int-range fixture is verified Core with a layout");
            const auto layout = compute_core_layouts(program);
            const auto emitted = emit_agent(program, *layout.table);
            const auto handler_body =
                emitted.artifact ? scalar_handler_body(emitted.artifact->bytes) : std::nullopt;
            constexpr std::uint8_t kOpI32Eqz = 0x45;
            constexpr std::uint8_t kOpI32LtS = 0x48;
            constexpr std::uint8_t kOpI32GtS = 0x4a;
            constexpr std::uint8_t kOpI32And = 0x71;
            // `not(5 < 3)` -> 0x45, `not(5 > 7)` -> 0x45, folded by 0x71.
            check(emitted.artifact.has_value() && handler_body.has_value() &&
                      contains_bytes(*handler_body, {kOpI32LtS, kOpI32Eqz}) &&
                      contains_bytes(*handler_body, {kOpI32GtS, kOpI32Eqz}) &&
                      std::count(handler_body->begin(), handler_body->end(), kOpI32And) == 1,
                  "P6-3 int-range pattern emits two negated signed compares + i32.and");
        }

        // P6-3: an IntRange pattern whose bound does not fit the scrutinee's i32
        // repr can never be reached at that width, so it fails closed rather
        // than wrapping the compare operand.
        {
            auto program = make_e1_core_program();
            program.value_types.push_back(
                CoreValueType{CoreVtInt{std::make_pair<std::int64_t, std::int64_t>(0, 1000)}});
            auto &flow = program.flows[0];
            flow.exprs.push_back(CoreExpr{
                CoreLiteralExpr{CoreLiteralKind::Integer, "5"}, std::nullopt, CoreValueTypeId{1}});
            flow.value_count = 2;
            flow.value_types = {CoreValueTypeId{0}, CoreValueTypeId{1}};
            flow.patterns.push_back(
                CorePattern{CoreIntRangePat{0, 5'000'000'000LL}, std::nullopt});
            auto &start = flow.states[1].body;
            start.statements.clear();
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{1}, CoreExprId{1}}, std::nullopt});
            CoreMatchStmt match;
            match.scrutinee = CoreValueId{1};
            match.has_result = false;
            CoreMatchArm arm;
            arm.pattern = CorePatternId{0};
            arm.body = std::make_unique<CoreRegion>();
            arm.body->statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{0}, "Done"}, std::nullopt});
            match.arms.push_back(std::move(arm));
            match.fallback_region = std::make_unique<CoreRegion>();
            match.fallback_region->statements.push_back(
                CoreStmt{CoreTrapStmt{CoreTrapKind::NonExhaustiveMatch}, std::nullopt});
            start.statements.push_back(CoreStmt{std::move(match), std::nullopt});
            const auto layout = compute_core_layouts(program);
            const auto emitted = layout.table.has_value()
                                     ? emit_agent(program, *layout.table)
                                     : backends::CoreWasmCodegenResult{};
            check(!emitted.artifact.has_value() &&
                      has_codegen_code(emitted,
                                       backends::core_wasm_diag::kUnsupportedOrchestration) &&
                      has_codegen_message(emitted, "i32 scalar range"),
                  "P6-3 fails closed on an int-range bound exceeding the i32 repr");
        }

        // P6-3: a non-exhaustive fallback containing CoreTrapStmt emits the wasm
        // `unreachable` opcode (0x00), so a non-total match traps at runtime
        // instead of silently completing.
        {
            auto program = make_e1_core_program();
            program.value_types.push_back(CoreValueType{CoreVtBool{}});
            auto &flow = program.flows[0];
            flow.exprs.push_back(CoreExpr{
                CoreLiteralExpr{CoreLiteralKind::Bool, "true"}, std::nullopt, CoreValueTypeId{1}});
            flow.value_count = 2;
            flow.value_types = {CoreValueTypeId{0}, CoreValueTypeId{1}};
            // `false` pattern: the arm never matches, so the trap fallback runs.
            flow.patterns.push_back(
                CorePattern{CoreLiteralPat{CoreLiteralKind::Bool, "false"}, std::nullopt});
            auto &start = flow.states[1].body;
            start.statements.clear();
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{1}, CoreExprId{1}}, std::nullopt});
            CoreMatchStmt match;
            match.scrutinee = CoreValueId{1};
            match.has_result = false;
            CoreMatchArm arm;
            arm.pattern = CorePatternId{0};
            arm.body = std::make_unique<CoreRegion>();
            arm.body->statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{0}, "Done"}, std::nullopt});
            match.arms.push_back(std::move(arm));
            match.fallback_region = std::make_unique<CoreRegion>();
            match.fallback_region->statements.push_back(
                CoreStmt{CoreTrapStmt{CoreTrapKind::NonExhaustiveMatch}, std::nullopt});
            start.statements.push_back(CoreStmt{std::move(match), std::nullopt});
            check(verify_core_program(program).ok() && compute_core_layouts(program).ok(),
                  "P6-3 non-exhaustive-trap fixture is verified Core with a layout");
            const auto layout = compute_core_layouts(program);
            const auto emitted = emit_agent(program, *layout.table);
            const auto handler_body =
                emitted.artifact ? scalar_handler_body(emitted.artifact->bytes) : std::nullopt;
            constexpr std::uint8_t kOpLiteralFalseAndEq = 0x46; // i32.eq after a false test
            constexpr std::uint8_t kOpEndP = 0x0b;
            constexpr std::uint8_t kOpUnreachableP = 0x00;
            // The trap fallback is the LAST thing inside the arm chain, so the
            // handler's instruction stream ends with an exact, position-anchored
            // shape: the fallback's `unreachable`, then B's `end`, then S's `end`,
            // then the `unreachable`/`end`/`end` that `emit()` appends to type the
            // `block (result i32)` on its dead fall-through and close the function.
            // A raw 0x00 count is worthless here (every `i32.const 0` / `br 0` /
            // local index emits one), so pin the tail instead: dropping the trap
            // lowering leaves the region ending at S's `end` and this fails.
            const std::vector<std::uint8_t> kTrapFallbackTail = {
                kOpEndP, kOpUnreachableP, kOpEndP, kOpEndP, // region: end C, trap, end B, end S
                kOpUnreachableP, kOpEndP, kOpEndP};         // emit(): dead path, block, function
            check(emitted.artifact.has_value() && handler_body.has_value() &&
                      contains_bytes(*handler_body, {0x41, 0x00, kOpLiteralFalseAndEq}) &&
                      handler_body->size() >= kTrapFallbackTail.size() &&
                      std::equal(kTrapFallbackTail.begin(),
                                 kTrapFallbackTail.end(),
                                 handler_body->end() -
                                     static_cast<std::ptrdiff_t>(kTrapFallbackTail.size())),
                  "P6-3 lowers the trap fallback to wasm `unreachable` (0x00)");
        }

        // P6-2 scratch-pool local-index regression: an i32 match SCRATCH slot and
        // an i64 SSA let in ONE handler. The emitted function declares its locals
        // as the single wasm grouping [SSA i32][scratch i32][SSA i64], so the i64
        // group starts AFTER the whole i32 group. A `let` index computed from the
        // SSA pool alone lands inside the i32 group: the body stores an i64 into a
        // declared i32 local (`local.set` type mismatch) and the module is invalid
        // wasm. This fixture pins every local.get/local.set immediate against the
        // DECLARED layout, which the byte-pattern assertions elsewhere cannot make
        // (they only skip the immediates).
        //
        // Slot layout: v1(level)/v2(arm)/v6(cond) are SSA i32, v3 is the match
        // RESULT scratch i32, v4/v5 are SSA i64. So the declared grouping is 4
        // i32 then 2 i64, and the i32 SCRATCH group is non-empty — exactly the
        // condition under which a naive i64 base lands the i64 let on an i32 local.
        constexpr std::uint8_t kOpLocalGetP = 0x20;
        constexpr std::uint8_t kOpLocalSetP = 0x21;
        constexpr std::uint8_t kI32T = 0x7f;
        constexpr std::uint8_t kI64T = 0x7e;
        {
            auto program = make_e1_core_program();
            program.value_types.push_back(
                CoreValueType{CoreVtInt{std::make_pair<std::int64_t, std::int64_t>(0, 9)}});
            CoreTypeDecl level;
            level.kind = CoreTypeDecl::Kind::Enum;
            level.name = "app::Level";
            level.variants = {"Low", "High"};
            level.variant_payloads = {CoreTypeDecl::VariantPayload{},
                                      CoreTypeDecl::VariantPayload{}};
            program.types.push_back(std::move(level));
            program.value_types.push_back(CoreValueType{CoreVtBool{}});            // vt2 Bool
            program.value_types.push_back(CoreValueType{CoreVtInt{std::nullopt}}); // vt3 i64
            program.value_types.push_back(
                CoreValueType{CoreVtNominal{CoreTypeId{1}, {}, std::nullopt}});    // vt4 Level
            auto &agent = program.agents[0];
            agent.states = {"Done", "Start", "High"};
            agent.finals = {CoreStateId{0}};
            agent.transitions = {{CoreStateId{1}, CoreStateId{0}},
                                 {CoreStateId{1}, CoreStateId{2}},
                                 {CoreStateId{2}, CoreStateId{0}}};
            auto &flow = program.flows[0];
            flow.exprs.push_back(CoreExpr{
                CoreQualifiedExpr{"app::Level::High", CoreTypeId{1}, CoreVariantId{1}, true},
                std::nullopt,
                CoreValueTypeId{4}}); // expr1 -> v1 (Level::High, SSA i32)
            flow.exprs.push_back(CoreExpr{CoreLiteralExpr{CoreLiteralKind::Bool, "true"},
                                          std::nullopt,
                                          CoreValueTypeId{2}}); // expr2 -> v2 (arm value)
            flow.exprs.push_back(CoreExpr{CoreLiteralExpr{CoreLiteralKind::Integer, "7"},
                                          std::nullopt,
                                          CoreValueTypeId{3}}); // expr3 -> v4 (i64 let)
            flow.exprs.push_back(CoreExpr{CoreLiteralExpr{CoreLiteralKind::Integer, "3"},
                                          std::nullopt,
                                          CoreValueTypeId{3}}); // expr4 -> v5 (i64 const)
            flow.exprs.push_back(CoreExpr{
                CoreValueRefExpr{CoreValueId{4}}, std::nullopt, CoreValueTypeId{3}}); // expr5
            flow.exprs.push_back(CoreExpr{
                CoreValueRefExpr{CoreValueId{5}}, std::nullopt, CoreValueTypeId{3}}); // expr6
            flow.exprs.push_back(
                CoreExpr{CoreBinaryExpr{CoreBinaryOp::Gt, CoreExprId{5}, CoreExprId{6}},
                         std::nullopt,
                         CoreValueTypeId{2}}); // expr7 -> v6 (Bool condition)
            flow.value_count = 7;
            flow.value_types = {CoreValueTypeId{0}, // v0 input nominal
                                CoreValueTypeId{4}, // v1 Level    -> SSA i32
                                CoreValueTypeId{2}, // v2 Bool     -> SSA i32
                                CoreValueTypeId{2}, // v3 Bool     -> SCRATCH i32
                                CoreValueTypeId{3}, // v4 i64      -> SSA i64
                                CoreValueTypeId{3}, // v5 i64      -> SSA i64
                                CoreValueTypeId{2}}; // v6 Bool    -> SSA i32
            // Pattern 0: a tag-only variant pattern (High) with no payload.
            flow.patterns.push_back(
                CorePattern{CoreVariantPat{CoreTypeId{1}, CoreVariantId{1}, {}, {}, false},
                            std::nullopt});
            auto &start = flow.states[1].body;
            start.statements.clear();
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{1}, CoreExprId{1}}, std::nullopt});
            // The expression match's Bool result takes the ONE i32 SCRATCH slot.
            CoreMatchStmt match;
            match.scrutinee = CoreValueId{1};
            match.has_result = true;
            match.result = CoreValueId{3};
            CoreMatchArm arm;
            arm.pattern = CorePatternId{0};
            arm.body = std::make_unique<CoreRegion>();
            arm.body->statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{2}, CoreExprId{2}}, std::nullopt});
            arm.body->statements.push_back(
                CoreStmt{CoreYieldStmt{true, CoreValueId{2}}, std::nullopt});
            match.arms.push_back(std::move(arm));
            match.fallback_region = std::make_unique<CoreRegion>();
            match.fallback_region->statements.push_back(
                CoreStmt{CoreTrapStmt{CoreTrapKind::NonExhaustiveMatch}, std::nullopt});
            start.statements.push_back(CoreStmt{std::move(match), std::nullopt});
            // `big` is an UNBOUNDED Int, so it is an SSA i64 slot and MUST land
            // after the i32 scratch slot.
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{4}, CoreExprId{3}}, std::nullopt});
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{5}, CoreExprId{4}}, std::nullopt});
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{6}, CoreExprId{7}}, std::nullopt});
            CoreIfStmt branch;
            branch.condition = CoreValueId{6};
            branch.then_region = std::make_unique<CoreRegion>();
            branch.then_region->statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{2}, "High"}, std::nullopt});
            branch.else_region = std::make_unique<CoreRegion>();
            branch.else_region->statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{0}, "Done"}, std::nullopt});
            start.statements.push_back(CoreStmt{std::move(branch), std::nullopt});
            CoreFlowState high;
            high.state = CoreStateId{2};
            high.state_name = "High";
            high.body.statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{0}, "Done"}, std::nullopt});
            flow.states.push_back(std::move(high));
            check(verify_core_program(program).ok() && compute_core_layouts(program).ok(),
                  "P6-2 i32-scratch + i64-let fixture is verified Core with a layout");
            const auto layout = compute_core_layouts(program);
            const auto emitted = emit_agent(program, *layout.table);
            const auto handler_body =
                emitted.artifact ? scalar_handler_body(emitted.artifact->bytes) : std::nullopt;
            check(emitted.ok() && handler_body.has_value(),
                  "P6-2 i32-scratch + i64-let handler compiles to a real function");
            if (handler_body.has_value()) {
                const auto body = *handler_body;
                std::size_t at = 0;
                const auto read_leb = [&](std::size_t &pos) {
                    std::uint32_t value = 0;
                    std::uint32_t shift = 0;
                    while (pos < body.size()) {
                        const auto b = body[pos++];
                        value |= static_cast<std::uint32_t>(b & 0x7fu) << shift;
                        if ((b & 0x80u) == 0) {
                            break;
                        }
                        shift += 7;
                    }
                    return value;
                };
                // Expand the declared local groups into kind-by-index.
                std::vector<std::uint8_t> kinds;
                const auto groups = read_leb(at);
                for (std::uint32_t g = 0; g < groups; ++g) {
                    const auto count = read_leb(at);
                    const auto kind = body[at++];
                    for (std::uint32_t n = 0; n < count; ++n) {
                        kinds.push_back(kind);
                    }
                }
                // The declared grouping is uniformly "all i32 locals, then all
                // i64 locals": 4 i32 (3 SSA + the match-result scratch slot) then
                // 2 i64.
                const auto first_i64 = std::find(kinds.begin(), kinds.end(), kI64T);
                const bool grouped =
                    first_i64 != kinds.end() &&
                    std::all_of(kinds.begin(), first_i64,
                                [](std::uint8_t k) { return k == kI32T; }) &&
                    std::all_of(first_i64, kinds.end(),
                                [](std::uint8_t k) { return k == kI64T; });
                const auto i64_index =
                    static_cast<std::uint32_t>(std::distance(kinds.begin(), first_i64));
                const auto i32_count =
                    static_cast<std::uint32_t>(std::count(kinds.begin(), kinds.end(), kI32T));
                check(grouped && i32_count == 4 && kinds.size() == 6,
                      "P6-2 declares [3 SSA i32][1 scratch i32] then [2 SSA i64]");
                // Walk the stream: every local.get / local.set immediate must
                // name a local whose DECLARED kind matches the adjacent
                // i32.const / i64.const source. A `let` index short by the
                // scratch group names an i32 local here.
                std::vector<std::uint32_t> i64_sets;
                std::size_t i64_gets = 0;
                std::optional<std::uint8_t> pending_kind;
                std::vector<std::uint32_t> wrong_kind;
                while (at < body.size()) {
                    const auto op = body[at++];
                    if (op == 0x41) { // i32.const
                        (void)read_leb(at);
                        pending_kind = kI32T;
                    } else if (op == 0x42) { // i64.const
                        (void)read_leb(at);
                        pending_kind = kI64T;
                    } else if (op == kOpLocalGetP || op == kOpLocalSetP) {
                        const auto index = read_leb(at);
                        if (pending_kind.has_value() && index < kinds.size() &&
                            kinds[index] != *pending_kind) {
                            wrong_kind.push_back(index);
                        }
                        if (op == kOpLocalSetP && pending_kind == kI64T) {
                            i64_sets.push_back(index);
                        }
                        if (op == kOpLocalGetP && index >= i64_index && index < kinds.size()) {
                            ++i64_gets;
                        }
                        pending_kind.reset();
                    } else if (op == 0x02 || op == 0x04) { // block / if
                        ++at; // blocktype
                    } else if (op == 0x0c || op == 0x0d) { // br / br_if
                        (void)read_leb(at);
                    } else if (op == 0x28 || op == 0x29 || op == 0x36 || op == 0x37) {
                        (void)read_leb(at); // align
                        (void)read_leb(at); // offset
                    }
                }
                check(wrong_kind.empty(),
                      "P6-2 every local.get/set immediate matches its declared kind");
                check(i64_sets.size() == 2 &&
                          std::all_of(i64_sets.begin(), i64_sets.end(),
                                      [i64_index](std::uint32_t index) {
                                          return index >= i64_index;
                                      }),
                      "P6-2 every i64 let stores into the declared i64 group");
                check(i64_gets >= 2,
                      "P6-2 the i64 lets' locals are read back by the i64 compare");
            }
        }

        // P6-3/P6-4 arm-binding SITE regression: a payload binding (`High { v }`)
        // must latch from the PAYLOAD SLOT it was planned at, not from the
        // scrutinee root. The binding's site is keyed by its SSA value id in the
        // plan pass; a site reconstructed from the scrutinee's root kind instead
        // copies the enum's ADDRESS (an i32) into the binding's local.
        //
        // The payload field here is an unbounded Int (i64), so the binding's
        // scratch local is declared i64. Latching the address (i32) into it is a
        // `local.set` type mismatch the wasm validator rejects, which is exactly
        // what a real engine reports for the pre-fix codegen. The assertion is on
        // the emitted local index space: the latch must be an `i64.load` at the
        // payload offset, never a bare `local.get` of the scrutinee.
        constexpr std::uint8_t kOpI64LoadP = 0x29;
        {
            auto program = make_e1_core_program();
            program.value_types.push_back(CoreValueType{CoreVtInt{std::nullopt}}); // vt1 i64
            program.value_types.push_back(
                CoreValueType{CoreVtNominal{CoreTypeId{1}, {}, std::nullopt}}); // vt2 Level
            program.value_types.push_back(CoreValueType{CoreVtBool{}});         // vt3 Bool
            CoreTypeDecl level;
            level.kind = CoreTypeDecl::Kind::Enum;
            level.name = "app::Level";
            level.variants = {"Low", "High"};
            level.variant_payloads = {CoreTypeDecl::VariantPayload{},
                                      CoreTypeDecl::VariantPayload{
                                          CoreTypeDecl::VariantPayload::Kind::Tuple,
                                          {CoreMemberTypeTemplateNodeId{0}},
                                          {}}};
            level.member_type_templates.push_back(
                CoreMemberTypeTemplateNode{CoreMemberTypeTemplateKind::Concrete,
                                           CoreValueTypeId{1},
                                           0,
                                           CoreTypeId{},
                                           std::nullopt,
                                           {},
                                           CoreMemberTypeTemplateNodeId{}});
            program.types.push_back(std::move(level));
            auto &agent = program.agents[0];
            agent.states = {"Done", "Start", "High"};
            agent.finals = {CoreStateId{0}};
            agent.transitions = {{CoreStateId{1}, CoreStateId{0}},
                                 {CoreStateId{1}, CoreStateId{2}},
                                 {CoreStateId{2}, CoreStateId{0}}};
            auto &flow = program.flows[0];
            // ANF (def-before-use): value ids are defined in increasing order.
            //   v1 = the payload literal 0 (i64)
            //   v2 = Level::High{v1}, an ADDRESSED enum -> v2's P4-D layout is
            //        tag@0 + payload v@8, so the binding HAS a real memory slot
            //   v3 = the arm BINDING (i64)
            //   v4 = the match RESULT (i64)
            //   v5 = the fallback literal 7
            //   v6 = the 0 constant, v7 = the Bool condition
            flow.exprs.push_back(CoreExpr{CoreLiteralExpr{CoreLiteralKind::Integer, "0"},
                                          std::nullopt,
                                          CoreValueTypeId{1}}); // expr1 -> v1
            flow.exprs.push_back(CoreExpr{
                CoreConstructExpr{"app::Level",
                                  "",
                                  true,
                                  CoreTypeId{1},
                                  CoreVariantId{1},
                                  true,
                                  {CoreConstructArg{CoreFieldId{0}, CoreValueId{1}}}},
                std::nullopt,
                CoreValueTypeId{2}}); // expr2 -> v2 (Level)
            flow.exprs.push_back(CoreExpr{CoreLiteralExpr{CoreLiteralKind::Integer, "7"},
                                          std::nullopt,
                                          CoreValueTypeId{1}}); // expr3 -> v5
            flow.exprs.push_back(
                CoreExpr{CoreValueRefExpr{CoreValueId{4}}, std::nullopt, CoreValueTypeId{1}});
            flow.exprs.push_back(CoreExpr{CoreLiteralExpr{CoreLiteralKind::Integer, "0"},
                                          std::nullopt,
                                          CoreValueTypeId{1}}); // expr5 -> v6
            flow.exprs.push_back(
                CoreExpr{CoreBinaryExpr{CoreBinaryOp::Gt, CoreExprId{4}, CoreExprId{5}},
                         std::nullopt,
                         CoreValueTypeId{3}}); // expr6 -> v7 (Bool)
            flow.value_count = 8;
            flow.value_types = {CoreValueTypeId{0}, // v0 input
                                CoreValueTypeId{1}, // v1 the payload literal (i64)
                                CoreValueTypeId{2}, // v2 the scrutinee (addressed enum)
                                CoreValueTypeId{1}, // v3 the arm BINDING (i64)
                                CoreValueTypeId{1}, // v4 the match RESULT (i64)
                                CoreValueTypeId{1}, // v5 the fallback 7 (i64)
                                CoreValueTypeId{1}, // v6 the 0 constant (i64)
                                CoreValueTypeId{3}}; // v7 the Bool condition
            // Pattern 0: High(v) — a binding over one tuple payload slot.
            flow.patterns.push_back(CorePattern{
                CoreBindingPat{CorePatternBindingId{0}, CorePatternId{}, false}, std::nullopt});
            flow.patterns.push_back(CorePattern{
                CoreVariantPat{CoreTypeId{1},
                               CoreVariantId{1},
                               {CorePatternId{0}},
                               {},
                               false},
                std::nullopt});
            auto &start = flow.states[1].body;
            start.statements.clear();
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{1}, CoreExprId{1}}, std::nullopt});
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{2}, CoreExprId{2}}, std::nullopt});
            CoreMatchStmt match;
            match.scrutinee = CoreValueId{2};
            match.has_result = true;
            match.result = CoreValueId{4};
            CoreMatchArm arm;
            arm.pattern = CorePatternId{1};
            arm.bindings.push_back(CorePatternBinding{CoreValueId{3}});
            arm.body = std::make_unique<CoreRegion>();
            // The arm yields its BINDING (v3) as the match result, so the latched
            // value is what the result local receives.
            arm.body->statements.push_back(
                CoreStmt{CoreYieldStmt{true, CoreValueId{3}}, std::nullopt});
            match.arms.push_back(std::move(arm));
            match.fallback_region = std::make_unique<CoreRegion>();
            match.fallback_region->statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{5}, CoreExprId{3}}, std::nullopt});
            match.fallback_region->statements.push_back(
                CoreStmt{CoreYieldStmt{true, CoreValueId{5}}, std::nullopt});
            start.statements.push_back(CoreStmt{std::move(match), std::nullopt});
            // `bound > 0` drives a computed goto so the handler diverges on every
            // path (the P6 handler contract). v6 is the 0 constant, v7 the Bool.
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{6}, CoreExprId{5}}, std::nullopt});
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{7}, CoreExprId{6}}, std::nullopt});
            CoreIfStmt branch;
            branch.condition = CoreValueId{7};
            branch.then_region = std::make_unique<CoreRegion>();
            branch.then_region->statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{2}, "High"}, std::nullopt});
            branch.else_region = std::make_unique<CoreRegion>();
            branch.else_region->statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{0}, "Done"}, std::nullopt});
            start.statements.push_back(CoreStmt{std::move(branch), std::nullopt});
            CoreFlowState high;
            high.state = CoreStateId{2};
            high.state_name = "High";
            high.body.statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{0}, "Done"}, std::nullopt});
            flow.states.push_back(std::move(high));
            check(verify_core_program(program).ok() && compute_core_layouts(program).ok(),
                  "P6-3 payload-binding fixture is verified Core with a layout");
            const auto layout = compute_core_layouts(program);
            const auto emitted = emit_agent(program, *layout.table);
            const auto handler_body =
                emitted.artifact ? scalar_handler_body(emitted.artifact->bytes) : std::nullopt;
            // The latch must LOAD the field at the payload offset (a wide i64 load
            // at a nonzero offset), not copy the scrutinee local. The pre-fix
            // codegen emitted `local.get <scrutinee>; local.set <i64 binding>`,
            // which has no i64.load at all.
            check(emitted.ok() && handler_body.has_value() &&
                      contains_bytes(*handler_body, {kOpI64LoadP, 0x03}) &&
                      std::count(handler_body->begin(), handler_body->end(), kOpI64LoadP) >= 1,
                  "P6-3 latches an arm payload binding from its recorded P4-D slot");
        }

        // P6-2 opcode-pinning regression: the signed comparison ladder must
        // emit the exact wasm byte for Le/Gt/Ge at the operand's P4-D scalar
        // repr. The operands straddle zero (lhs = -1 via 0 - 1, rhs = 0), so
        // a swapped-direction or unsigned opcode miscompiles half the input
        // domain; the assertion pins the physical byte and proves the wrong
        // sibling byte is absent. `wide` selects bounded-Int i32 vs unbounded
        // i64.
        constexpr std::uint8_t kOpI32GtS = 0x4a;
        constexpr std::uint8_t kOpI32LeS = 0x4c;
        constexpr std::uint8_t kOpI32GeS = 0x4e;
        constexpr std::uint8_t kOpI64GtS = 0x55;
        constexpr std::uint8_t kOpI64LeS = 0x57;
        constexpr std::uint8_t kOpI64GeS = 0x59;
        auto make_compare_program = [&](bool wide, CoreBinaryOp op) {
            auto program = make_e1_core_program();
            program.value_types.push_back(
                CoreValueType{CoreVtInt{std::make_pair<std::int64_t, std::int64_t>(-100, 100)}});
            program.value_types.push_back(CoreValueType{CoreVtBool{}}); // vt2
            if (wide) {
                program.value_types[1] = CoreValueType{CoreVtInt{std::nullopt}};
            }
            auto &agent = program.agents[0];
            agent.states = {"Done", "Start", "High"};
            agent.finals = {CoreStateId{0}};
            agent.transitions = {{CoreStateId{1}, CoreStateId{0}},
                                 {CoreStateId{1}, CoreStateId{2}},
                                 {CoreStateId{2}, CoreStateId{0}}};
            auto &flow = program.flows[0];
            // expr0 is the existing Done identity path (vt0).
            flow.exprs.push_back(CoreExpr{CoreLiteralExpr{CoreLiteralKind::Integer, "1"},
                                          std::nullopt,
                                          CoreValueTypeId{1}}); // expr1 -> v3 (1)
            flow.exprs.push_back(CoreExpr{
                CoreValueRefExpr{CoreValueId{3}}, std::nullopt, CoreValueTypeId{1}}); // expr2
            flow.exprs.push_back(CoreExpr{CoreLiteralExpr{CoreLiteralKind::Integer, "0"},
                                          std::nullopt,
                                          CoreValueTypeId{1}}); // expr3 (0)
            flow.exprs.push_back(CoreExpr{
                CoreValueRefExpr{CoreValueId{1}}, std::nullopt, CoreValueTypeId{1}}); // expr4
            // 0 - 1 == -1 (wasm wrap), a scalar value strictly below zero.
            flow.exprs.push_back(
                CoreExpr{CoreBinaryExpr{CoreBinaryOp::Sub, CoreExprId{4}, CoreExprId{2}},
                         std::nullopt,
                         CoreValueTypeId{1}}); // expr5 -> v4
            flow.exprs.push_back(CoreExpr{
                CoreValueRefExpr{CoreValueId{4}}, std::nullopt, CoreValueTypeId{1}}); // expr6
            flow.exprs.push_back(CoreExpr{CoreLiteralExpr{CoreLiteralKind::Integer, "0"},
                                          std::nullopt,
                                          CoreValueTypeId{1}}); // expr7 -> v5
            flow.exprs.push_back(CoreExpr{
                CoreValueRefExpr{CoreValueId{5}}, std::nullopt, CoreValueTypeId{1}}); // expr8
            flow.exprs.push_back(
                CoreExpr{CoreBinaryExpr{op, CoreExprId{6}, CoreExprId{8}},
                         std::nullopt,
                         CoreValueTypeId{2}}); // expr9 cmp(-1, 0) -> v2
            flow.value_count = 6;
            flow.value_types = {CoreValueTypeId{0},
                                CoreValueTypeId{1},
                                CoreValueTypeId{2},
                                CoreValueTypeId{1},
                                CoreValueTypeId{1},
                                CoreValueTypeId{1}};
            auto &start = flow.states[1].body;
            start.statements.clear();
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{3}, CoreExprId{1}}, std::nullopt});
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{1}, CoreExprId{3}}, std::nullopt});
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{4}, CoreExprId{5}}, std::nullopt});
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{5}, CoreExprId{7}}, std::nullopt});
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{2}, CoreExprId{9}}, std::nullopt});
            CoreIfStmt branch;
            branch.condition = CoreValueId{2};
            branch.then_region = std::make_unique<CoreRegion>();
            branch.then_region->statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{2}, "High"}, std::nullopt});
            branch.else_region = std::make_unique<CoreRegion>();
            branch.else_region->statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{0}, "Done"}, std::nullopt});
            start.statements.push_back(CoreStmt{std::move(branch), std::nullopt});
            CoreFlowState high;
            high.state = CoreStateId{2};
            high.state_name = "High";
            high.body.statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{0}, "Done"}, std::nullopt});
            flow.states.push_back(std::move(high));
            return program;
        };
        struct CompareExpectation {
            CoreBinaryOp op;
            std::uint8_t correct;
            std::optional<std::uint8_t> previous_wrong;
            const char *label;
        };
        const CompareExpectation compare_cases[] = {
            {CoreBinaryOp::Le, kOpI32LeS, 0x4a, "i32 Le emits le_s (not gt_s)"},
            {CoreBinaryOp::Gt, kOpI32GtS, 0x4c, "i32 Gt emits gt_s (not le_s)"},
            {CoreBinaryOp::Ge, kOpI32GeS, 0x4d, "i32 Ge emits ge_s (not le_u)"},
        };
        for (const auto &tc : compare_cases) {
            auto program = make_compare_program(false, tc.op);
            check(verify_core_program(program).ok() && compute_core_layouts(program).ok(),
                  "P6-2 i32 signed-compare fixture is verified Core with a layout");
            const auto layout = compute_core_layouts(program);
            const auto emitted = emit_agent(program, *layout.table);
            const auto handler_body =
                emitted.artifact ? scalar_handler_body(emitted.artifact->bytes) : std::nullopt;
            const auto has = [&](std::uint8_t b) {
                return handler_body.has_value() &&
                       std::find(handler_body->begin(), handler_body->end(), b) !=
                           handler_body->end();
            };
            check(emitted.ok() && handler_body.has_value() && has(tc.correct) &&
                      (!tc.previous_wrong.has_value() || !has(*tc.previous_wrong)),
                  tc.label);
        }
        const CompareExpectation compare_wide_cases[] = {
            {CoreBinaryOp::Le, kOpI64LeS, 0x54, "i64 Le emits le_s (not lt_u)"},
            // i64 Gt was already assigned the correct byte (0x55); pin it.
            {CoreBinaryOp::Gt, kOpI64GtS, std::nullopt, "i64 Gt emits gt_s"},
            {CoreBinaryOp::Ge, kOpI64GeS, 0x56, "i64 Ge emits ge_s (not gt_u)"},
        };
        for (const auto &tc : compare_wide_cases) {
            auto program = make_compare_program(true, tc.op);
            check(verify_core_program(program).ok() && compute_core_layouts(program).ok(),
                  "P6-2 i64 signed-compare fixture is verified Core with a layout");
            const auto layout = compute_core_layouts(program);
            const auto emitted = emit_agent(program, *layout.table);
            const auto handler_body =
                emitted.artifact ? scalar_handler_body(emitted.artifact->bytes) : std::nullopt;
            const auto has = [&](std::uint8_t b) {
                return handler_body.has_value() &&
                       std::find(handler_body->begin(), handler_body->end(), b) !=
                           handler_body->end();
            };
            check(emitted.ok() && handler_body.has_value() && has(tc.correct) &&
                      (!tc.previous_wrong.has_value() || !has(*tc.previous_wrong)),
                  tc.label);
        }

        // P6-2 structured control flow: a handler compiles to a real wasm
        // function whose body is `block (result i32)` + structurally balanced
        // if/else/end. These assertions pin the byte grammar (label balance and
        // the `br` depth a nested goto must use), which a mis-emitted nesting or
        // a `br 0` that lands on an inner `if` would break — the exact failure a
        // wasm validator reports as a type mismatch or an invalid label.
        constexpr std::uint8_t kOpBlock = 0x02;
        constexpr std::uint8_t kOpIf = 0x04;
        constexpr std::uint8_t kOpElse = 0x05;
        constexpr std::uint8_t kOpEnd = 0x0b;
        constexpr std::uint8_t kOpBr = 0x0c;
        constexpr std::uint8_t kBlockResultI32 = 0x7f;
        constexpr std::uint8_t kOpUnreachable = 0x00;

        // `label_depths` counts, for each emitted `br`, how many enclosing
        // labels (function + block + if) it must skip. Walks the nesting with a
        // simple depth counter: `block`/`if` are entries, `end` is an exit, the
        // function-level `end` terminates the walk.
        const auto structure = [](const std::vector<std::uint8_t> &body) {
            struct Summary {
                std::uint32_t blocks{0};
                std::uint32_t ifs{0};
                std::uint32_t elses{0};
                std::uint32_t ends{0};
                std::vector<std::uint32_t> br_depths;
                std::uint32_t max_depth{0};
                bool saw_result_i32_block{false};
            } out;
            // Skip the local declarations (count + (count,type) pairs).
            std::size_t offset = 0;
            const auto read_leb = [&](std::size_t &at) {
                std::uint32_t value = 0;
                std::uint32_t shift = 0;
                while (at < body.size()) {
                    const auto b = body[at++];
                    value |= static_cast<std::uint32_t>(b & 0x7fu) << shift;
                    if ((b & 0x80u) == 0) {
                        break;
                    }
                    shift += 7;
                }
                return value;
            };
            const auto groups = read_leb(offset);
            for (std::uint32_t g = 0; g < groups; ++g) {
                (void)read_leb(offset); // group size
                ++offset;                // value type
            }
            std::uint32_t depth = 0; // 1 = inside the function body block
            ++depth;
            while (offset < body.size()) {
                const auto op = body[offset++];
                if (op == kOpBlock) {
                    ++out.blocks;
                    if (offset < body.size() && body[offset] == kBlockResultI32) {
                        out.saw_result_i32_block = true;
                    }
                    ++offset; // blocktype
                    ++depth;
                } else if (op == kOpIf) {
                    ++out.ifs;
                    ++offset; // blocktype
                    ++depth;
                } else if (op == kOpElse) {
                    ++out.elses;
                } else if (op == kOpEnd) {
                    ++out.ends;
                    --depth;
                    if (depth == 0) {
                        break; // function-level end
                    }
                } else if (op == kOpBr) {
                    out.br_depths.push_back(read_leb(offset));
                } else if (op == kOpUnreachable) {
                    // no immediate
                } else if (op == 0x41) { // i32.const
                    (void)read_leb(offset);
                } else if (op == 0x21 || op == 0x20) { // local.set / local.get
                    (void)read_leb(offset);
                } else if (op == 0x24 || op == 0x23) { // global.set / global.get
                    (void)read_leb(offset);
                } else if (op == kOpCall) {
                    (void)read_leb(offset);
                }
                out.max_depth = std::max(out.max_depth, depth);
            }
            return out;
        };

        // Else-less nested if: Decide -> if (x>10) { if (x>5) goto High } goto Low.
        // The inner goto is two `if`s deep, so its `br` must skip BOTH labels and
        // land on the result-i32 block (depth 2).
        {
            auto program = make_computed_goto_program(false, CoreBinaryOp::Add);
            auto &agent = program.agents[0];
            agent.states = {"Done", "Start", "High", "Low"};
            agent.finals = {CoreStateId{0}};
            agent.transitions = {{CoreStateId{1}, CoreStateId{2}},
                                 {CoreStateId{1}, CoreStateId{3}},
                                 {CoreStateId{2}, CoreStateId{0}},
                                 {CoreStateId{3}, CoreStateId{0}}};
            auto &flow = program.flows[0];
            auto &start = flow.states[1].body;
            // Reuse the planned condition (v2); wrap the outer if around an inner
            // else-less if whose branch gotos High, with a Low fallthrough after.
            auto inner = std::make_unique<CoreRegion>();
            inner->statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{2}, "High"}, std::nullopt});
            auto then_region = std::make_unique<CoreRegion>();
            CoreIfStmt inner_if;
            inner_if.condition = CoreValueId{2};
            inner_if.then_region = std::move(inner);
            then_region->statements.push_back(CoreStmt{std::move(inner_if), std::nullopt});
            CoreIfStmt outer_if;
            outer_if.condition = CoreValueId{2};
            outer_if.then_region = std::move(then_region);
            // Replace the original two-arm branch with outer_if + trailing goto.
            start.statements.pop_back();
            start.statements.push_back(CoreStmt{std::move(outer_if), std::nullopt});
            start.statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{3}, "Low"}, std::nullopt});
            CoreFlowState low;
            low.state = CoreStateId{3};
            low.state_name = "Low";
            low.body.statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{0}, "Done"}, std::nullopt});
            flow.states.push_back(std::move(low));
            check(verify_core_program(program).ok() && compute_core_layouts(program).ok(),
                  "P6-2 nested else-less fixture is verified Core with a layout");
            const auto layout = compute_core_layouts(program);
            const auto emitted = emit_agent(program, *layout.table);
            const auto handler_body =
                emitted.artifact ? scalar_handler_body(emitted.artifact->bytes) : std::nullopt;
            check(emitted.ok() && handler_body.has_value(),
                  "P6-2 nested else-less handler compiles to a real function");
            if (handler_body.has_value()) {
                const auto s = structure(*handler_body);
                check(s.saw_result_i32_block && s.blocks == 1,
                      "P6-2 handler opens exactly one result-i32 block");
                check(s.ifs == 2 && s.elses == 0 && s.ends == s.blocks + s.ifs + 1,
                      "P6-2 nested else-less if emits balanced if/end with no else");
                // Two gotos: inner (2 ifs deep -> br 2) and the trailing outer
                // fallthrough (0 ifs deep -> br 0).
                check(s.br_depths.size() == 2 &&
                          std::find(s.br_depths.begin(), s.br_depths.end(), 2u) !=
                              s.br_depths.end() &&
                          std::find(s.br_depths.begin(), s.br_depths.end(), 0u) !=
                              s.br_depths.end(),
                      "P6-2 nested goto branches to the handler block at the correct depth");
            }
        }

        // If/else so both arms diverge: the else must be emitted and a goto in
        // the else branch is only one `if` deep (br 1).
        {
            auto program = make_computed_goto_program(false, CoreBinaryOp::Add);
            const auto layout = compute_core_layouts(program);
            const auto emitted = emit_agent(program, *layout.table);
            const auto handler_body =
                emitted.artifact ? scalar_handler_body(emitted.artifact->bytes) : std::nullopt;
            check(emitted.ok() && handler_body.has_value(),
                  "P6-2 if/else handler compiles to a real function");
            if (handler_body.has_value()) {
                const auto s = structure(*handler_body);
                check(s.blocks == 1 && s.ifs == 1 && s.elses == 1 &&
                          s.ends == s.blocks + s.ifs + 1,
                      "P6-2 if/else emits one balanced if/else/end");
                check(!s.br_depths.empty() &&
                          std::all_of(s.br_depths.begin(),
                                      s.br_depths.end(),
                                      [](std::uint32_t d) { return d == 1; }),
                      "P6-2 both if/else arms branch to the handler block at depth 1");
            }
        }

        // A trap-only handler (no goto) compiles to `block (result i32)` +
        // unreachable + end, and must not claim any target states.
        {
            auto program = make_e1_core_program();
            auto &agent = program.agents[0];
            agent.states = {"Done", "Start"};
            agent.transitions = {{CoreStateId{1}, CoreStateId{0}}};
            auto &flow = program.flows[0];
            auto &start = flow.states[1].body;
            start.statements.clear();
            start.statements.push_back(
                CoreStmt{CoreTrapStmt{CoreTrapKind::NonExhaustiveMatch}, std::nullopt});
            check(verify_core_program(program).ok() && compute_core_layouts(program).ok(),
                  "P6-2 trap-only fixture is verified Core with a layout");
            const auto layout = compute_core_layouts(program);
            const auto emitted = emit_agent(program, *layout.table);
            const auto handler_body =
                emitted.artifact ? scalar_handler_body(emitted.artifact->bytes) : std::nullopt;
            check(emitted.ok() && handler_body.has_value() &&
                      std::find(handler_body->begin(), handler_body->end(), kOpUnreachable) !=
                          handler_body->end(),
                  "P6-2 trap-only handler compiles to a trapping function");
        }

        // A P6-shaped region that carries a capability EFFECT must NOT enter the
        // computation lane: effects stay on the KR6.5 orchestration classification
        // (kUnsupportedCapabilityFrame, never the P6 scaffold prefix).
        {
            auto program = make_e2_core_program();
            auto &e2_flow = program.flows[0];
            // A second unprojected input-path expr for the Start handler;
            // verifier scopes are per-handler, so Done's v0 is not visible.
            e2_flow.exprs.push_back(
                CoreExpr{CorePathExpr{CorePathRoot::Input,
                                    "input",
                                    {},
                                    CoreTypeId{0},
                                    {},
                                    true,
                                    {},
                                    false,
                                    {}},
                         std::nullopt,
                         CoreValueTypeId{0}});
            e2_flow.value_count = 4;
            e2_flow.value_types = {CoreValueTypeId{0},
                                   CoreValueTypeId{1},
                                   CoreValueTypeId{1},
                                   CoreValueTypeId{0}};
            auto &start = e2_flow.states[1].body;
            start.statements.clear();
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{3}, CoreExprId{1}}, std::nullopt});
            start.statements.push_back(
                CoreStmt{CoreCapabilityCallStmt{CoreValueId{2},
                                            CoreCapabilityId{0},
                                            "Echo",
                                            {CoreValueId{3}}},
                     std::nullopt});
            start.statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{0}, "Done"}, std::nullopt});
            check(verify_core_program(program).ok(),
                  "capability-in-non-final fixture is verified Core");
            const auto layout = compute_core_layouts(program);
            const auto emitted = backends::emit_core_wasm(
                program, *layout.table,
                {CoreAgentId{0}, backends::WasmProfileKind::Wasi});
            check(!emitted.artifact.has_value() &&
                      has_codegen_code(
                          emitted,
                          backends::core_wasm_diag::kUnsupportedCapabilityFrame) &&
                      !has_codegen_message(emitted, kP6ScaffoldPrefix),
                  "P6 gate classifies an effectful region as orchestration, not computation");
        }

        // P6-6 (RFC 0026 KR6.6): CoreCoerceExpr physical effects. The KR6.5
        // `coercion_plans` arena gate is LIFTED: a normalized coercion proof plan
        // now lowers op-by-op through the P4-D layout table — an `IntWiden` whose
        // repr grows is `i64.extend_i32_s`, a same-layout op is a physical no-op,
        // and an op with no P6 value representation fails closed with its own
        // message rather than emitting a half-correct no-op.
        constexpr std::uint8_t kOpI64ExtendI32S = 0xac;

        // Build a 3-state agent whose Start handler widens a BOUNDED i32 Int
        // (0..10) to a WIDER bounded i32 Int (0..100) through one IntWiden, then
        // compares the widened value. Both endpoints are the i32 scalar repr, so
        // the P4-D layout comparison proves the coercion is a physical no-op.
        // Every expr and SSA value is consumed exactly once (the orphan check).
        const auto make_same_repr_coercion_program = [&]() {
            auto program = make_e1_core_program();
            program.value_types.push_back(CoreValueType{
                CoreVtInt{std::make_pair<std::int64_t, std::int64_t>(0, 10)}});  // vt1 i32
            program.value_types.push_back(CoreValueType{
                CoreVtInt{std::make_pair<std::int64_t, std::int64_t>(0, 100)}}); // vt2 i32
            program.value_types.push_back(CoreValueType{CoreVtBool{}});           // vt3
            auto &agent = program.agents[0];
            agent.states = {"Done", "Start", "High"};
            agent.finals = {CoreStateId{0}};
            agent.transitions = {{CoreStateId{1}, CoreStateId{0}},
                                 {CoreStateId{1}, CoreStateId{2}},
                                 {CoreStateId{2}, CoreStateId{0}}};
            auto &flow = program.flows[0];
            // SSA values: v0 is the Done handler's own; v1..v6 are this handler's,
            // each defined exactly once (the orphan check forbids a gap).
            flow.exprs.push_back(CoreExpr{CoreLiteralExpr{CoreLiteralKind::Integer, "5"},
                                          std::nullopt,
                                          CoreValueTypeId{1}}); // expr1 -> v1 (0..10)
            flow.exprs.push_back(
                CoreExpr{CoreCoerceExpr{CoreValueId{1}, CoreCoercionPlanId{0}},
                         std::nullopt,
                         CoreValueTypeId{2}}); // expr2 -> v2 (0..100, same repr)
            flow.exprs.push_back(CoreExpr{
                CoreValueRefExpr{CoreValueId{2}}, std::nullopt, CoreValueTypeId{2}}); // expr3
            flow.exprs.push_back(CoreExpr{CoreLiteralExpr{CoreLiteralKind::Integer, "3"},
                                          std::nullopt,
                                          CoreValueTypeId{2}}); // expr4 -> v4
            flow.exprs.push_back(CoreExpr{
                CoreValueRefExpr{CoreValueId{4}}, std::nullopt, CoreValueTypeId{2}}); // expr5
            flow.exprs.push_back(CoreExpr{CoreBinaryExpr{CoreBinaryOp::Gt, CoreExprId{3},
                                                         CoreExprId{5}},
                                          std::nullopt,
                                          CoreValueTypeId{3}}); // expr6 -> v6
            flow.value_count = 7;
            flow.value_types = {CoreValueTypeId{0},
                                CoreValueTypeId{1},
                                CoreValueTypeId{2},
                                CoreValueTypeId{2},
                                CoreValueTypeId{2},
                                CoreValueTypeId{2},
                                CoreValueTypeId{3}};
            flow.coercion_plans = {CoreCoercionPlanNode{
                CoreValueTypeId{1},
                CoreValueTypeId{2},
                {CoreCoercionOp{.kind = CoreCoercionOpKind::IntWiden}}}};
            auto &start = flow.states[1].body;
            start.statements.clear();
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{1}, CoreExprId{1}}, std::nullopt});
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{2}, CoreExprId{2}}, std::nullopt});
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{3}, CoreExprId{3}}, std::nullopt});
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{4}, CoreExprId{4}}, std::nullopt});
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{5}, CoreExprId{5}}, std::nullopt});
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{6}, CoreExprId{6}}, std::nullopt});
            CoreIfStmt branch;
            branch.condition = CoreValueId{6};
            branch.then_region = std::make_unique<CoreRegion>();
            branch.then_region->statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{2}, "High"}, std::nullopt});
            branch.else_region = std::make_unique<CoreRegion>();
            branch.else_region->statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{0}, "Done"}, std::nullopt});
            start.statements.push_back(CoreStmt{std::move(branch), std::nullopt});
            CoreFlowState high;
            high.state = CoreStateId{2};
            high.state_name = "High";
            high.body.statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{0}, "Done"}, std::nullopt});
            flow.states.push_back(std::move(high));
            return program;
        };

        // P6-6 negative: an IntWiden whose two endpoints are the SAME P4-D bytes
        // is a physical no-op — the layout comparison decides, NOT the bounds — so
        // no widening instruction may be emitted and the comparison stays i32.
        {
            auto program = make_same_repr_coercion_program();
            check(verify_core_program(program).ok() && compute_core_layouts(program).ok(),
                  "P6-6 same-repr coercion fixture is verified Core with a layout");
            const auto layout = compute_core_layouts(program);
            const auto emitted = emit_agent(program, *layout.table);
            const auto handler_body =
                emitted.artifact ? scalar_handler_body(emitted.artifact->bytes) : std::nullopt;
            const auto count_of = [&](std::uint8_t op) {
                return handler_body.has_value()
                           ? static_cast<std::size_t>(std::count(handler_body->begin(),
                                                                 handler_body->end(), op))
                           : 0u;
            };
            check(emitted.ok() && handler_body.has_value(),
                  "P6-6 lowers a same-repr coercion handler to its own function");
            check(count_of(kOpI64ExtendI32S) == 0,
                  "P6-6 emits NO widening when the P4-D layouts are byte-identical");
            check(count_of(kOpI32GtS) == 1,
                  "P6-6 keeps the same-repr coercion's comparison in the i32 ladder");
        }

        // P6-6 positive: an IntWiden from a BOUNDED i32 Int to an UNBOUNDED Int
        // grows the physical repr, so the handler must carry exactly one
        // i64.extend_i32_s and the comparison moves to the i64 ladder. This is
        // the coercion the real frontend produces for `let wide: Int = narrow`.
        {
            auto program = make_same_repr_coercion_program();
            // Widen the target from the bounded i32 (0..100) to an UNBOUNDED i64.
            program.value_types[2] = CoreValueType{CoreVtInt{std::nullopt}};
            // The comparison must then be an i64 compare.
            auto &flow = program.flows[0];
            flow.exprs[4].result_type = CoreValueTypeId{2};
            check(verify_core_program(program).ok() && compute_core_layouts(program).ok(),
                  "P6-6 i32 -> i64 coercion fixture is verified Core with a layout");
            const auto layout = compute_core_layouts(program);
            const auto emitted = emit_agent(program, *layout.table);
            const auto handler_body =
                emitted.artifact ? scalar_handler_body(emitted.artifact->bytes) : std::nullopt;
            const auto count_of = [&](std::uint8_t op) {
                return handler_body.has_value()
                           ? static_cast<std::size_t>(std::count(handler_body->begin(),
                                                                 handler_body->end(), op))
                           : 0u;
            };
            check(emitted.ok() && handler_body.has_value() && count_of(kOpI64ExtendI32S) == 1,
                  "P6-6 emits exactly one i64.extend_i32_s (0xac) for the repr-growing widening");
            check(handler_body.has_value() && count_of(kOpI64GtS) == 1 &&
                      count_of(kOpI32GtS) == 0,
                  "P6-6 compares the widened value in the i64 ladder");
            check(!has_codegen_message(emitted, "coercion is outside the P6 scalar subset") &&
                      !has_codegen_message(emitted, "rejects a hidden pattern arena"),
                  "P6-6 lifts the coercion arena gate and the per-expr coercion reject");
        }

        // P6-6 fail-closed: a `StringWiden` plan is a Core-legal strict length
        // widening whose endpoints are BOTH the 2-word PtrLen String layout, but
        // the P6 value model has no 2-word value at all (a PtrLen is neither a
        // scalar local nor a single-word aggregate address), so the coercion's
        // RESULT has no P6 kind and the handler fails closed before any byte is
        // emitted — a truncated handle is never produced.
        {
            auto program = make_e1_core_program();
            program.value_types.push_back(
                CoreValueType{CoreVtString{std::make_pair<std::uint64_t, std::uint64_t>(0, 10)}});
            program.value_types.push_back(CoreValueType{CoreVtString{std::nullopt}});
            auto &agent = program.agents[0];
            agent.states = {"Done", "Start"};
            agent.finals = {CoreStateId{0}};
            agent.transitions = {{CoreStateId{1}, CoreStateId{0}}};
            auto &flow = program.flows[0];
            flow.exprs.push_back(CoreExpr{CoreLiteralExpr{CoreLiteralKind::String, "abc"},
                                          std::nullopt,
                                          CoreValueTypeId{1}}); // expr1 -> v1
            flow.exprs.push_back(
                CoreExpr{CoreCoerceExpr{CoreValueId{1}, CoreCoercionPlanId{0}},
                         std::nullopt,
                         CoreValueTypeId{2}}); // expr2 -> v2
            flow.value_count = 3;
            flow.value_types = {CoreValueTypeId{0}, CoreValueTypeId{1}, CoreValueTypeId{2}};
            flow.coercion_plans = {CoreCoercionPlanNode{
                CoreValueTypeId{1},
                CoreValueTypeId{2},
                {CoreCoercionOp{.kind = CoreCoercionOpKind::StringWiden}}}};
            auto &start = flow.states[1].body;
            start.statements.clear();
            start.statements.push_back(
                CoreStmt{CoreLetStmt{CoreValueId{1}, CoreExprId{1}}, std::nullopt});
            start.statements.push_back(
                CoreStmt{CoreGotoStmt{CoreStateId{0}, "Done"}, std::nullopt});
            check(verify_core_program(program).ok() && compute_core_layouts(program).ok(),
                  "P6-6 StringWiden fixture is verified Core with a layout");
            const auto layout = compute_core_layouts(program);
            const auto emitted = emit_agent(program, *layout.table);
            check(!emitted.artifact.has_value() &&
                      has_codegen_code(emitted,
                                       backends::core_wasm_diag::kUnsupportedOrchestration) &&
                      has_codegen_message(emitted, "non-scalar or f64"),
                  "P6-6 fails closed on a StringWiden whose result has no P6 value form");
        }
    }

    std::printf("\n%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
