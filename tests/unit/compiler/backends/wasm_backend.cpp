#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdint>
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

static bool rejects_as_unsupported(const ahfl::ir::core::CoreProgram &program) {
    const auto verified = ahfl::ir::core::verify_core_program(program);
    const auto layout = ahfl::ir::core::compute_core_layouts(program);
    if (!verified.ok() || !layout.ok() || !layout.table.has_value()) {
        return false;
    }
    const auto emitted = ahfl::backends::emit_core_wasm(
        program,
        *layout.table,
        {{0}, ahfl::backends::WasmProfileKind::Wasi});
    return !emitted.artifact.has_value() &&
           has_codegen_code(emitted,
                            ahfl::backends::core_wasm_diag::kUnsupportedOrchestration);
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
                program, *layout.table, {{0}, ahfl::backends::WasmProfileKind::Wasi});
            const auto second = ahfl::backends::emit_core_wasm(
                program, *layout.table, {{0}, ahfl::backends::WasmProfileKind::Wasi});
            const auto browser = ahfl::backends::emit_core_wasm(
                program, *layout.table, {{0}, ahfl::backends::WasmProfileKind::Browser});
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

            auto tampered = *layout.table;
            tampered.target.pointer_size = 8;
            const auto bad_layout = ahfl::backends::emit_core_wasm(
                program, tampered, {{0}, ahfl::backends::WasmProfileKind::Wasi});
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

    // Test 11: entry selection and deterministic termination fail closed.
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
                      {{0}, ahfl::backends::WasmProfileKind::Wasi})
                : ahfl::backends::CoreWasmCodegenResult{};
        check(!cycle_result.artifact.has_value() &&
                  has_codegen_code(cycle_result,
                                   ahfl::backends::core_wasm_diag::kNonterminatingE1Run),
              "E1 rejects a deterministic goto cycle with no artifact");

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
                      {{0}, ahfl::backends::WasmProfileKind::Wasi})
                : ahfl::backends::CoreWasmCodegenResult{};
        check(!multiple_result.artifact.has_value() &&
                  has_codegen_code(multiple_result,
                                   ahfl::backends::core_wasm_diag::kEntryAmbiguous),
              "E1 rejects multiple agents instead of selecting/concatenating");
    }

    std::printf("\n%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
