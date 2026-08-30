#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "ahfl/compiler/ir/core_layout.hpp"
#include "ahfl/compiler/ir/core_verify.hpp"
#include "compiler/backends/infra/core_wasm_codegen.hpp"
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

static bool has_codegen_code(const ahfl::backends::CoreWasmCodegenResult &result,
                             std::string_view code) {
    for (const auto &diagnostic : result.diagnostics) {
        if (diagnostic.code == code) {
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

static bool contains_bytes(const std::vector<std::uint8_t> &bytes,
                           std::initializer_list<std::uint8_t> needle) {
    return std::search(bytes.begin(), bytes.end(), needle.begin(), needle.end()) != bytes.end();
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

int main() {
    std::printf("=== WASM Backend Tests ===\n\n");

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
        }
    }

    // Test 12: capability ABI and frame-subset failures publish no artifact.
    {
        using namespace ahfl::ir::core;
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

    // Test 15: E3-C1 validates a real multi-agent workflow plan but publishes no
    // workflow bytes until C2. Subset violations are classified at the narrow
    // workflow-frame seam.
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
        const auto valid_result = emit_workflow(valid);
        check(verify_core_program(valid).ok() && !valid_result.artifact.has_value() &&
                  has_codegen_code(valid_result,
                                   backends::core_wasm_diag::kUnsupportedOrchestration) &&
                  !has_codegen_code(valid_result,
                                    backends::core_wasm_diag::kUnsupportedWorkflowFrame),
              "E3-C1 accepts the complete identity DAG plan but does not emit workflow bytes");

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
        check(verify_core_program(branch_join).ok() &&
                  !branch_join_result.artifact.has_value() &&
                  has_codegen_code(branch_join_result,
                                   backends::core_wasm_diag::kUnsupportedOrchestration) &&
                  !has_codegen_code(branch_join_result,
                                    backends::core_wasm_diag::kUnsupportedWorkflowFrame),
              "E3-C1 validates declaration-order parallel roots plus a join and dedups instances");

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
              "E3-C1 rejects a workflow frame that does not match target input type");

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
        check(verify_core_program(capability_agent).ok() &&
                  !capability_result.artifact.has_value() &&
                  has_codegen_code(capability_result,
                                   backends::core_wasm_diag::kUnsupportedWorkflowFrame),
              "E3-C1 rejects a reachable capability-bearing agent before byte emission");

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

    std::printf("\n%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
