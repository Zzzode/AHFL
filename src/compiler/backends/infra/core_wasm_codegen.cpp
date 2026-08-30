#include "compiler/backends/infra/core_wasm_codegen.hpp"

#include "ahfl/compiler/ir/core_verify.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <variant>

namespace ahfl::backends {

namespace {

using ir::core::CoreAgentDecl;
using ir::core::CoreAgentId;
using ir::core::CoreFlowDecl;
using ir::core::CoreGotoStmt;
using ir::core::CoreLetStmt;
using ir::core::CorePathExpr;
using ir::core::CoreProgram;
using ir::core::CoreReturnStmt;
using ir::core::CoreStateId;
using ir::core::CoreVtNominal;

constexpr std::uint8_t kI32 = 0x7f;
constexpr std::uint8_t kEmptyBlock = 0x40;
constexpr std::uint8_t kFuncType = 0x60;

constexpr std::uint8_t kSectionType = 1;
constexpr std::uint8_t kSectionFunction = 3;
constexpr std::uint8_t kSectionMemory = 5;
constexpr std::uint8_t kSectionGlobal = 6;
constexpr std::uint8_t kSectionExport = 7;
constexpr std::uint8_t kSectionCode = 10;

constexpr std::uint8_t kExportFunction = 0;
constexpr std::uint8_t kExportMemory = 2;
constexpr std::uint8_t kExportGlobal = 3;

constexpr std::uint8_t kOpUnreachable = 0x00;
constexpr std::uint8_t kOpNop = 0x01;
constexpr std::uint8_t kOpBlock = 0x02;
constexpr std::uint8_t kOpLoop = 0x03;
constexpr std::uint8_t kOpIf = 0x04;
constexpr std::uint8_t kOpElse = 0x05;
constexpr std::uint8_t kOpEnd = 0x0b;
constexpr std::uint8_t kOpBr = 0x0c;
constexpr std::uint8_t kOpBrIf = 0x0d;
constexpr std::uint8_t kOpCall = 0x10;
constexpr std::uint8_t kOpDrop = 0x1a;
constexpr std::uint8_t kOpLocalGet = 0x20;
constexpr std::uint8_t kOpLocalSet = 0x21;
constexpr std::uint8_t kOpGlobalGet = 0x23;
constexpr std::uint8_t kOpGlobalSet = 0x24;
constexpr std::uint8_t kOpI32Const = 0x41;
constexpr std::uint8_t kOpI32Eqz = 0x45;
constexpr std::uint8_t kOpI32Eq = 0x46;
constexpr std::uint8_t kOpI32Add = 0x6a;
constexpr std::uint8_t kOpI32Sub = 0x6b;
constexpr std::uint8_t kOpI32Or = 0x72;

constexpr std::uint32_t kGlobalCurrentState = 0;
constexpr std::uint32_t kGlobalTransitionCount = 1;
constexpr std::uint32_t kGlobalAbiVersion = 2;
constexpr std::uint32_t kGlobalHeapNext = 3;

constexpr std::uint32_t kFuncAlloc = 0;
constexpr std::uint32_t kFuncDealloc = 1;
constexpr std::uint32_t kFuncCurrentState = 2;
constexpr std::uint32_t kFuncIsFinal = 3;
constexpr std::uint32_t kFuncStep = 4;
constexpr std::uint32_t kFuncRun = 5;

struct StateAction {
    bool is_final{false};
    CoreStateId target{};
};

struct E1Plan {
    CoreAgentId agent{};
    CoreStateId initial{};
    std::vector<StateAction> actions;
};

void add_diag(CoreWasmCodegenResult &result,
              std::string_view code,
              std::string message,
              ir::SourceRangeOpt range = std::nullopt) {
    result.diagnostics.push_back(
        CoreWasmDiagnostic{std::string(code), std::move(message), std::move(range)});
}

[[nodiscard]] bool is_final_state(const CoreAgentDecl &agent, std::uint32_t state) {
    return std::any_of(agent.finals.begin(), agent.finals.end(), [state](CoreStateId id) {
        return id.value == state;
    });
}

[[nodiscard]] const CoreFlowDecl *unique_target_flow(const CoreProgram &program,
                                                     CoreAgentId target) {
    const CoreFlowDecl *found = nullptr;
    for (const auto &flow : program.flows) {
        if (flow.target == target) {
            if (found != nullptr) {
                return nullptr;
            }
            found = &flow;
        }
    }
    return found;
}

[[nodiscard]] bool validate_identity_final(const CoreProgram &program,
                                           const CoreAgentDecl &agent,
                                           const CoreFlowDecl &flow,
                                           const ir::core::CoreFlowState &handler,
                                           std::vector<bool> &used_exprs,
                                           std::vector<bool> &used_values,
                                           CoreWasmCodegenResult &result) {
    const auto &statements = handler.body.statements;
    if (statements.size() != 2) {
        const auto range = statements.empty() ? ir::SourceRangeOpt{}
                                              : statements.front().source_range;
        add_diag(result,
                 core_wasm_diag::kUnsupportedOrchestration,
                 "KR6.5 E1 requires a final handler to be the canonical input identity let "
                 "followed by return",
                 range);
        return false;
    }

    const auto *let = std::get_if<CoreLetStmt>(&statements[0].node);
    const auto *ret = std::get_if<CoreReturnStmt>(&statements[1].node);
    if (let == nullptr || ret == nullptr || !ret->has_value || ret->value != let->result) {
        add_diag(result,
                 core_wasm_diag::kUnsupportedOrchestration,
                 "KR6.5 E1 final handler is not the canonical input identity ANF shape",
                 statements[0].source_range);
        return false;
    }
    if (let->expr.value >= flow.exprs.size() || let->result.value >= flow.value_types.size() ||
        let->result.value >= used_values.size()) {
        add_diag(result,
                 core_wasm_diag::kInvalidCore,
                 "canonical identity references an out-of-range expression or SSA value",
                 statements[0].source_range);
        return false;
    }
    if (used_exprs[let->expr.value] || used_values[let->result.value]) {
        add_diag(result,
                 core_wasm_diag::kUnsupportedOrchestration,
                 "KR6.5 E1 requires each canonical identity expression and value to be unique",
                 statements[0].source_range);
        return false;
    }

    const auto &expr = flow.exprs[let->expr.value];
    const auto *path = std::get_if<CorePathExpr>(&expr.node);
    if (path == nullptr || path->root != ir::core::CorePathRoot::Input ||
        path->root_type != agent.input_type || !path->members.empty() ||
        !path->projection.empty() || !path->projection_resolved || path->has_local) {
        add_diag(result,
                 core_wasm_diag::kUnsupportedOrchestration,
                 "KR6.5 E1 accepts only an unprojected canonical input identity path",
                 expr.source_range);
        return false;
    }
    if (expr.result_type.value >= program.value_types.size() ||
        flow.value_types[let->result.value] != expr.result_type) {
        add_diag(result,
                 core_wasm_diag::kInvalidCore,
                 "canonical identity expression and SSA value types disagree",
                 expr.source_range);
        return false;
    }
    const auto *nominal =
        std::get_if<CoreVtNominal>(&program.value_types[expr.result_type.value].node);
    if (nominal == nullptr || nominal->base != agent.input_type) {
        add_diag(result,
                 core_wasm_diag::kUnsupportedOrchestration,
                 "KR6.5 E1 identity result is not the agent input nominal value type",
                 expr.source_range);
        return false;
    }

    used_exprs[let->expr.value] = true;
    used_values[let->result.value] = true;
    return true;
}

[[nodiscard]] std::optional<E1Plan> build_e1_plan(const CoreProgram &program,
                                                  CoreAgentId target,
                                                  CoreWasmCodegenResult &result) {
    if (!program.workflows.empty()) {
        add_diag(result,
                 core_wasm_diag::kUnsupportedOrchestration,
                 "KR6.5 E1 does not support workflow declarations");
        return std::nullopt;
    }
    if (program.agents.size() != 1 || target.value >= program.agents.size()) {
        add_diag(result,
                 core_wasm_diag::kEntryAmbiguous,
                 "KR6.5 E1 requires exactly one agent and an in-range explicit target");
        return std::nullopt;
    }
    if (program.flows.size() != 1) {
        add_diag(result,
                 core_wasm_diag::kEntryAmbiguous,
                 "KR6.5 E1 requires exactly one flow for the target agent");
        return std::nullopt;
    }

    const auto &agent = program.agents[target.value];
    const auto *flow = unique_target_flow(program, target);
    if (flow == nullptr || flow != &program.flows.front()) {
        add_diag(result,
                 core_wasm_diag::kEntryAmbiguous,
                 "KR6.5 E1 could not resolve one unique flow for the target agent");
        return std::nullopt;
    }
    if (agent.input_type != agent.output_type) {
        add_diag(result,
                 core_wasm_diag::kUnsupportedOrchestration,
                 "KR6.5 E1 identity passthrough requires identical agent input/output types");
        return std::nullopt;
    }
    if (!flow->patterns.empty() || !flow->coercion_plans.empty()) {
        add_diag(result,
                 core_wasm_diag::kUnsupportedOrchestration,
                 "KR6.5 E1 rejects hidden pattern or coercion arenas");
        return std::nullopt;
    }

    if (agent.states.size() >=
        static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        add_diag(result,
                 core_wasm_diag::kBinaryOverflow,
                 "agent state count exceeds the E1 wasm32 signed-immediate domain");
        return std::nullopt;
    }

    std::vector<const ir::core::CoreFlowState *> handlers(agent.states.size(), nullptr);
    for (const auto &handler : flow->states) {
        if (handler.state.value >= handlers.size() || handlers[handler.state.value] != nullptr) {
            add_diag(result,
                     core_wasm_diag::kInvalidCore,
                     "flow handler state identity is duplicate or out of range");
            return std::nullopt;
        }
        handlers[handler.state.value] = &handler;
    }

    E1Plan plan;
    plan.agent = target;
    plan.initial = agent.initial;
    plan.actions.resize(agent.states.size());
    std::vector<bool> used_exprs(flow->exprs.size(), false);
    std::vector<bool> used_values(flow->value_count, false);

    for (std::uint32_t state = 0; state < plan.actions.size(); ++state) {
        const auto *handler = handlers[state];
        if (handler == nullptr) {
            add_diag(result,
                     core_wasm_diag::kInvalidCore,
                     "target flow is missing a state handler");
            return std::nullopt;
        }

        auto &action = plan.actions[state];
        action.is_final = is_final_state(agent, state);
        const auto &statements = handler->body.statements;
        if (action.is_final) {
            if (!validate_identity_final(
                    program, agent, *flow, *handler, used_exprs, used_values, result)) {
                return std::nullopt;
            }
            continue;
        }

        if (statements.size() != 1) {
            const auto range = statements.empty() ? ir::SourceRangeOpt{}
                                                  : statements.front().source_range;
            add_diag(result,
                     core_wasm_diag::kUnsupportedOrchestration,
                     "KR6.5 E1 requires a non-final handler to contain exactly one goto",
                     range);
            return std::nullopt;
        }
        const auto *go = std::get_if<CoreGotoStmt>(&statements.front().node);
        if (go == nullptr) {
            add_diag(result,
                     core_wasm_diag::kUnsupportedOrchestration,
                     "KR6.5 E1 supports only CoreGotoStmt in a non-final handler",
                     statements.front().source_range);
            return std::nullopt;
        }
        action.target = go->target;

        const bool legal = std::any_of(
            agent.transitions.begin(), agent.transitions.end(), [state, go](const auto &edge) {
                return edge.from.value == state && edge.to == go->target;
            });
        if (!legal) {
            add_diag(result,
                     core_wasm_diag::kInvalidCore,
                     "goto is not present in the agent's legal transition table",
                     statements.front().source_range);
            return std::nullopt;
        }
    }

    if (std::any_of(used_exprs.begin(), used_exprs.end(), [](bool used) { return !used; }) ||
        std::any_of(used_values.begin(), used_values.end(), [](bool used) { return !used; })) {
        add_diag(result,
                 core_wasm_diag::kUnsupportedOrchestration,
                 "KR6.5 E1 rejects hidden/orphan expressions or SSA values");
        return std::nullopt;
    }

    // The action graph is functional. Finalize each path to prove that every
    // state reaches a final state and no E1 run can spin forever.
    std::vector<std::uint8_t> color(plan.actions.size(), 0);
    for (std::uint32_t start = 0; start < plan.actions.size(); ++start) {
        if (color[start] == 2) {
            continue;
        }
        std::vector<std::uint32_t> path;
        std::uint32_t current = start;
        while (color[current] == 0) {
            color[current] = 1;
            path.push_back(current);
            if (plan.actions[current].is_final) {
                break;
            }
            current = plan.actions[current].target.value;
        }
        if (!plan.actions[current].is_final && color[current] == 1) {
            add_diag(result,
                     core_wasm_diag::kNonterminatingE1Run,
                     "KR6.5 E1 deterministic goto graph contains a cycle");
            return std::nullopt;
        }
        for (const auto state : path) {
            color[state] = 2;
        }
    }
    return plan;
}

class ByteBuffer {
  public:
    void byte(std::uint8_t value) { bytes_.push_back(value); }

    void raw(std::initializer_list<std::uint8_t> values) {
        bytes_.insert(bytes_.end(), values.begin(), values.end());
    }

    void u32(std::uint32_t value) {
        do {
            std::uint8_t next = static_cast<std::uint8_t>(value & 0x7fu);
            value >>= 7u;
            if (value != 0) {
                next = static_cast<std::uint8_t>(next | 0x80u);
            }
            byte(next);
        } while (value != 0);
    }

    // E1 constants are non-negative and bounded by INT32_MAX. Signed LEB128
    // still needs an extra zero group when bit 6 of the last byte is set.
    void s32_nonnegative(std::uint32_t value) {
        bool more = true;
        while (more) {
            std::uint8_t next = static_cast<std::uint8_t>(value & 0x7fu);
            value >>= 7u;
            more = value != 0 || (next & 0x40u) != 0;
            if (more) {
                next = static_cast<std::uint8_t>(next | 0x80u);
            }
            byte(next);
        }
    }

    [[nodiscard]] bool name(std::string_view value) {
        if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        u32(static_cast<std::uint32_t>(value.size()));
        bytes_.insert(bytes_.end(), value.begin(), value.end());
        return true;
    }

    [[nodiscard]] bool sized(const ByteBuffer &payload) {
        if (payload.bytes_.size() > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        u32(static_cast<std::uint32_t>(payload.bytes_.size()));
        bytes_.insert(bytes_.end(), payload.bytes_.begin(), payload.bytes_.end());
        return true;
    }

    [[nodiscard]] std::vector<std::uint8_t> take() && { return std::move(bytes_); }

  private:
    std::vector<std::uint8_t> bytes_;
};

void append_const(ByteBuffer &body, std::uint32_t value) {
    body.byte(kOpI32Const);
    body.s32_nonnegative(value);
}

void append_indexed_op(ByteBuffer &body, std::uint8_t op, std::uint32_t index) {
    body.byte(op);
    body.u32(index);
}

[[nodiscard]] bool append_section(ByteBuffer &module,
                                  std::uint8_t section_id,
                                  const ByteBuffer &payload) {
    module.byte(section_id);
    return module.sized(payload);
}

void append_func_type(ByteBuffer &section,
                      std::initializer_list<std::uint8_t> params,
                      std::initializer_list<std::uint8_t> results) {
    section.byte(kFuncType);
    section.u32(static_cast<std::uint32_t>(params.size()));
    section.raw(params);
    section.u32(static_cast<std::uint32_t>(results.size()));
    section.raw(results);
}

void append_global(ByteBuffer &section, bool is_mutable, std::uint32_t initial) {
    section.byte(kI32);
    section.byte(is_mutable ? 1 : 0);
    append_const(section, initial);
    section.byte(kOpEnd);
}

[[nodiscard]] bool append_export(ByteBuffer &section,
                                 std::string_view name,
                                 std::uint8_t kind,
                                 std::uint32_t index) {
    if (!section.name(name)) {
        return false;
    }
    section.byte(kind);
    section.u32(index);
    return true;
}

[[nodiscard]] ByteBuffer make_alloc_body() {
    ByteBuffer body;
    body.u32(1); // one local group
    body.u32(1);
    body.byte(kI32); // local 1 = previous heap pointer
    append_indexed_op(body, kOpGlobalGet, kGlobalHeapNext);
    append_indexed_op(body, kOpLocalSet, 1);
    append_indexed_op(body, kOpGlobalGet, kGlobalHeapNext);
    append_indexed_op(body, kOpLocalGet, 0);
    body.byte(kOpI32Add);
    append_indexed_op(body, kOpGlobalSet, kGlobalHeapNext);
    append_indexed_op(body, kOpLocalGet, 1);
    body.byte(kOpEnd);
    return body;
}

[[nodiscard]] ByteBuffer make_dealloc_body() {
    ByteBuffer body;
    body.u32(0); // locals
    body.byte(kOpNop);
    body.byte(kOpEnd);
    return body;
}

[[nodiscard]] ByteBuffer make_current_state_body() {
    ByteBuffer body;
    body.u32(0);
    append_indexed_op(body, kOpGlobalGet, kGlobalCurrentState);
    body.byte(kOpEnd);
    return body;
}

[[nodiscard]] ByteBuffer make_is_final_body(const E1Plan &plan) {
    ByteBuffer body;
    body.u32(0);
    append_const(body, 0);
    for (std::uint32_t state = 0; state < plan.actions.size(); ++state) {
        if (!plan.actions[state].is_final) {
            continue;
        }
        append_indexed_op(body, kOpGlobalGet, kGlobalCurrentState);
        append_const(body, state);
        body.byte(kOpI32Eq);
        body.byte(kOpI32Or);
    }
    body.byte(kOpEnd);
    return body;
}

[[nodiscard]] ByteBuffer make_step_body(const E1Plan &plan) {
    ByteBuffer body;
    body.u32(0);
    for (std::uint32_t state = 0; state < plan.actions.size(); ++state) {
        append_indexed_op(body, kOpGlobalGet, kGlobalCurrentState);
        append_const(body, state);
        body.byte(kOpI32Eq);
        body.byte(kOpIf);
        body.byte(kI32); // result i32
        const auto &action = plan.actions[state];
        if (action.is_final) {
            append_const(body, state);
        } else {
            append_const(body, action.target.value);
            append_indexed_op(body, kOpGlobalSet, kGlobalCurrentState);
            append_indexed_op(body, kOpGlobalGet, kGlobalTransitionCount);
            append_const(body, 1);
            body.byte(kOpI32Add);
            append_indexed_op(body, kOpGlobalSet, kGlobalTransitionCount);
            append_const(body, action.target.value);
        }
        body.byte(kOpElse);
    }
    body.byte(kOpUnreachable);
    for (std::size_t state = 0; state < plan.actions.size(); ++state) {
        body.byte(kOpEnd);
    }
    body.byte(kOpEnd);
    return body;
}

[[nodiscard]] ByteBuffer make_run_body(const E1Plan &plan) {
    ByteBuffer body;
    body.u32(1); // one local group
    body.u32(1);
    body.byte(kI32); // local 2 = fuel

    append_const(body, plan.initial.value);
    append_indexed_op(body, kOpGlobalSet, kGlobalCurrentState);
    append_const(body, 0);
    append_indexed_op(body, kOpGlobalSet, kGlobalTransitionCount);
    append_const(body, static_cast<std::uint32_t>(plan.actions.size()) + 1u);
    append_indexed_op(body, kOpLocalSet, 2);

    body.byte(kOpBlock);
    body.byte(kEmptyBlock);
    body.byte(kOpLoop);
    body.byte(kEmptyBlock);
    append_indexed_op(body, kOpCall, kFuncIsFinal);
    append_indexed_op(body, kOpBrIf, 1); // break outer block
    append_indexed_op(body, kOpLocalGet, 2);
    body.byte(kOpI32Eqz);
    body.byte(kOpIf);
    body.byte(kEmptyBlock);
    body.byte(kOpUnreachable);
    body.byte(kOpEnd);
    append_indexed_op(body, kOpLocalGet, 2);
    append_const(body, 1);
    body.byte(kOpI32Sub);
    append_indexed_op(body, kOpLocalSet, 2);
    append_indexed_op(body, kOpCall, kFuncStep);
    body.byte(kOpDrop);
    append_indexed_op(body, kOpBr, 0); // loop
    body.byte(kOpEnd);
    body.byte(kOpEnd);
    // E1's sole value boundary is the validated opaque identity: return the
    // input pointer without reading, writing, copying, or allocating its frame.
    append_indexed_op(body, kOpLocalGet, 0);
    body.byte(kOpEnd);
    return body;
}

[[nodiscard]] std::optional<std::vector<std::uint8_t>> encode_module(const E1Plan &plan) {
    ByteBuffer module;
    module.raw({0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00});

    ByteBuffer types;
    types.u32(4);
    append_func_type(types, {}, {kI32});
    append_func_type(types, {kI32}, {kI32});
    append_func_type(types, {kI32, kI32}, {});
    append_func_type(types, {kI32, kI32}, {kI32});
    if (!append_section(module, kSectionType, types)) {
        return std::nullopt;
    }

    ByteBuffer functions;
    functions.u32(6);
    functions.u32(1); // alloc
    functions.u32(2); // dealloc
    functions.u32(0); // current_state
    functions.u32(0); // private is_final
    functions.u32(0); // step
    functions.u32(3); // run
    if (!append_section(module, kSectionFunction, functions)) {
        return std::nullopt;
    }

    ByteBuffer memories;
    memories.u32(1);
    memories.byte(0); // min only
    memories.u32(1);  // one wasm page
    if (!append_section(module, kSectionMemory, memories)) {
        return std::nullopt;
    }

    ByteBuffer globals;
    globals.u32(4);
    append_global(globals, true, plan.initial.value);
    append_global(globals, true, 0);
    append_global(globals, false, 1);
    append_global(globals, true, 1024);
    if (!append_section(module, kSectionGlobal, globals)) {
        return std::nullopt;
    }

    ByteBuffer exports;
    exports.u32(8);
    const bool exports_ok = append_export(exports, "memory", kExportMemory, 0) &&
                            append_export(exports, "alloc", kExportFunction, kFuncAlloc) &&
                            append_export(exports, "dealloc", kExportFunction, kFuncDealloc) &&
                            append_export(exports, "run", kExportFunction, kFuncRun) &&
                            append_export(exports, "step", kExportFunction, kFuncStep) &&
                            append_export(exports,
                                          "current_state",
                                          kExportFunction,
                                          kFuncCurrentState) &&
                            append_export(exports,
                                          "transition_count",
                                          kExportGlobal,
                                          kGlobalTransitionCount) &&
                            append_export(exports,
                                          "ahfl_abi_version",
                                          kExportGlobal,
                                          kGlobalAbiVersion);
    if (!exports_ok || !append_section(module, kSectionExport, exports)) {
        return std::nullopt;
    }

    ByteBuffer code;
    code.u32(6);
    const auto alloc = make_alloc_body();
    const auto dealloc = make_dealloc_body();
    const auto current = make_current_state_body();
    const auto final = make_is_final_body(plan);
    const auto step = make_step_body(plan);
    const auto run = make_run_body(plan);
    if (!code.sized(alloc) || !code.sized(dealloc) || !code.sized(current) ||
        !code.sized(final) || !code.sized(step) || !code.sized(run) ||
        !append_section(module, kSectionCode, code)) {
        return std::nullopt;
    }

    return std::move(module).take();
}

} // namespace

CoreWasmCodegenResult emit_core_wasm(const CoreProgram &program,
                                     const ir::core::CoreLayoutTable &layouts,
                                     CoreWasmTarget target) {
    CoreWasmCodegenResult result;

    const auto core_verification = ir::core::verify_core_program(program);
    if (!core_verification.ok()) {
        const auto &first = core_verification.diagnostics.front();
        add_diag(result,
                 core_wasm_diag::kInvalidCore,
                 "Core verifier rejected the program (" + first.code + "): " + first.message,
                 first.source_range);
        return result;
    }

    const auto layout_diagnostics = ir::core::verify_core_layout_table(program, layouts);
    if (!layout_diagnostics.empty()) {
        const auto &first = layout_diagnostics.front();
        add_diag(result,
                 core_wasm_diag::kInvalidLayout,
                 "P4-D verifier rejected the layout table (" + first.code + "): " +
                     first.message,
                 first.source_range);
        return result;
    }

    if (!(layouts.target == ir::core::TargetDataLayout{})) {
        add_diag(result,
                 core_wasm_diag::kUnsupportedTarget,
                 "KR6.5 E1 supports only the exact canonical wasm32 target layout");
        return result;
    }
    switch (target.profile) {
    case WasmProfileKind::Wasi:
    case WasmProfileKind::Browser:
        break;
    default:
        add_diag(result,
                 core_wasm_diag::kUnsupportedTarget,
                 "unknown WASM deployment profile");
        return result;
    }

    auto plan = build_e1_plan(program, target.agent, result);
    if (!plan.has_value()) {
        return result;
    }
    auto bytes = encode_module(*plan);
    if (!bytes.has_value()) {
        add_diag(result,
                 core_wasm_diag::kBinaryOverflow,
                 "WASM binary section or name exceeds the wasm32 encoding domain");
        return result;
    }

    CoreWasmArtifact artifact;
    artifact.bytes = std::move(*bytes);
    artifact.agent = target.agent;
    artifact.exports = {"memory",
                        "alloc",
                        "dealloc",
                        "run",
                        "step",
                        "current_state",
                        "transition_count",
                        "ahfl_abi_version"};
    result.artifact = std::move(artifact);
    return result;
}

} // namespace ahfl::backends
