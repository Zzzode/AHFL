#include "compiler/backends/infra/core_wasm_codegen.hpp"

#include "ahfl/compiler/ir/core_verify.hpp"
#include "ahfl/runtime/ahfl_host.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace ahfl::backends {

namespace {

using ir::core::CoreAgentDecl;
using ir::core::CoreAgentId;
using ir::core::CoreAgentInstance;
using ir::core::CoreCapabilityCallStmt;
using ir::core::CoreCapabilityId;
using ir::core::CoreFlowDecl;
using ir::core::CoreGotoStmt;
using ir::core::CoreInstanceId;
using ir::core::CoreLetStmt;
using ir::core::CorePathExpr;
using ir::core::CoreProgram;
using ir::core::CoreReturnStmt;
using ir::core::CoreStateId;
using ir::core::CoreValueId;
using ir::core::CoreValueTypeId;
using ir::core::CoreVtNominal;
using ir::core::CoreWorkflowDecl;
using ir::core::CoreWorkflowId;
using ir::core::CoreWorkflowNodeId;
using ir::core::CoreYieldStmt;

constexpr std::uint8_t kI32 = 0x7f;
constexpr std::uint8_t kEmptyBlock = 0x40;
constexpr std::uint8_t kFuncType = 0x60;

constexpr std::uint8_t kSectionType = 1;
constexpr std::uint8_t kSectionImport = 2;
constexpr std::uint8_t kSectionFunction = 3;
constexpr std::uint8_t kSectionMemory = 5;
constexpr std::uint8_t kSectionGlobal = 6;
constexpr std::uint8_t kSectionExport = 7;
constexpr std::uint8_t kSectionCode = 10;

constexpr std::uint8_t kImportFunction = 0;
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
constexpr std::uint8_t kOpReturn = 0x0f;
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
constexpr std::uint32_t kGlobalPendingLatched = 4;

constexpr std::uint32_t kDefinedAlloc = 0;
constexpr std::uint32_t kDefinedDealloc = 1;
constexpr std::uint32_t kDefinedCurrentState = 2;
constexpr std::uint32_t kDefinedIsFinal = 3;
constexpr std::uint32_t kDefinedStep = 4;
constexpr std::uint32_t kDefinedRun = 5;
constexpr std::uint32_t kDefinedRun2 = 6;

constexpr std::uint32_t kTypeNoArgsI32 = 0;
constexpr std::uint32_t kTypeI32ToI32 = 1;
constexpr std::uint32_t kTypeTwoI32ToVoid = 2;
constexpr std::uint32_t kTypeTwoI32ToI32 = 3;
constexpr std::uint32_t kTypeCapabilityTuple = 4;

static_assert(AHFL_CAP_OK == 0u);
static_assert(AHFL_CAP_ERROR == 1u);
static_assert(AHFL_CAP_PENDING == 2u);

struct GotoAction {
    CoreStateId target{};
};
struct IdentityAction {
    [[nodiscard]] friend bool operator==(IdentityAction, IdentityAction) noexcept = default;
};
struct CapabilityAction {
    CoreCapabilityId capability{};
    [[nodiscard]] friend bool operator==(CapabilityAction, CapabilityAction) noexcept = default;
};
using StateAction = std::variant<GotoAction, IdentityAction, CapabilityAction>;

struct AgentPlan {
    CoreAgentId agent{};
    CoreStateId initial{};
    std::vector<StateAction> actions;
    std::vector<CoreCapabilityId> imports;
};

struct AgentPlanPolicy {
    std::string_view unsupported_code{core_wasm_diag::kUnsupportedOrchestration};
    bool allow_capability{true};
    std::string_view slice{"E2"};
};

enum class WorkflowFrameSourceKind { Input, NodeOutput };

struct WorkflowFrameSource {
    WorkflowFrameSourceKind kind{WorkflowFrameSourceKind::Input};
    CoreWorkflowNodeId node{};
    CoreValueTypeId type{};
};

struct WorkflowNodePlan {
    CoreWorkflowNodeId node{};
    CoreInstanceId target_instance{};
    WorkflowFrameSource input;
};

struct WorkflowPlan {
    CoreWorkflowId workflow{};
    std::vector<CoreWorkflowNodeId> schedule;
    std::vector<CoreInstanceId> packaged_instances;
    std::vector<AgentPlan> agent_plans;
    std::vector<WorkflowNodePlan> nodes;
    WorkflowFrameSource output;
};

struct FunctionTable {
    std::uint32_t import_count{0};
    [[nodiscard]] std::uint32_t alloc() const noexcept { return import_count + kDefinedAlloc; }
    [[nodiscard]] std::uint32_t dealloc() const noexcept { return import_count + kDefinedDealloc; }
    [[nodiscard]] std::uint32_t current_state() const noexcept {
        return import_count + kDefinedCurrentState;
    }
    [[nodiscard]] std::uint32_t is_final() const noexcept {
        return import_count + kDefinedIsFinal;
    }
    [[nodiscard]] std::uint32_t step() const noexcept { return import_count + kDefinedStep; }
    [[nodiscard]] std::uint32_t run() const noexcept { return import_count + kDefinedRun; }
    [[nodiscard]] std::uint32_t run2() const noexcept { return import_count + kDefinedRun2; }
};

void add_diag(CoreWasmCodegenResult &result,
              std::string_view code,
              std::string message,
              ir::SourceRangeOpt range = std::nullopt) {
    result.diagnostics.push_back(
        CoreWasmDiagnostic{std::string(code), std::move(message), std::move(range)});
}

[[nodiscard]] bool is_final_action(const StateAction &action) {
    return !std::holds_alternative<GotoAction>(action);
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

[[nodiscard]] bool region_contains_capability(const ir::core::CoreRegion &region) {
    for (const auto &statement : region.statements) {
        if (std::holds_alternative<CoreCapabilityCallStmt>(statement.node)) {
            return true;
        }
        if (const auto *branch = std::get_if<ir::core::CoreIfStmt>(&statement.node)) {
            if ((branch->then_region != nullptr &&
                 region_contains_capability(*branch->then_region)) ||
                (branch->else_region != nullptr &&
                 region_contains_capability(*branch->else_region))) {
                return true;
            }
        }
        if (const auto *match = std::get_if<ir::core::CoreMatchStmt>(&statement.node)) {
            for (const auto &arm : match->arms) {
                if ((arm.guard_region != nullptr &&
                     region_contains_capability(*arm.guard_region)) ||
                    (arm.body != nullptr && region_contains_capability(*arm.body))) {
                    return true;
                }
            }
            if (match->fallback_region != nullptr &&
                region_contains_capability(*match->fallback_region)) {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] bool has_finalized_layout(const ir::core::CoreLayoutTable &layouts,
                                        CoreValueTypeId type) {
    if (type.value >= layouts.value_layouts.size()) {
        return false;
    }
    const auto layout = layouts.value_layouts[type.value];
    return layout.value < layouts.layouts.size() &&
           !std::holds_alternative<ir::core::CoreLayoutPending>(
               layouts.layouts[layout.value].shape);
}

[[nodiscard]] std::optional<CoreValueId>
validate_canonical_input_let(const CoreProgram &program,
                             const CoreAgentDecl &agent,
                             const CoreFlowDecl &flow,
                             const ir::core::CoreStmt &statement,
                             std::vector<bool> &used_exprs,
                             std::vector<bool> &used_values,
                             std::string_view unsupported_code,
                             CoreWasmCodegenResult &result) {
    const auto *let = std::get_if<CoreLetStmt>(&statement.node);
    if (let == nullptr || let->expr.value >= flow.exprs.size() ||
        let->result.value >= flow.value_types.size() ||
        let->result.value >= used_values.size()) {
        add_diag(result,
                 unsupported_code,
                 "KR6.5 E2 requires a canonical input let as the first final statement",
                 statement.source_range);
        return std::nullopt;
    }
    if (used_exprs[let->expr.value] || used_values[let->result.value]) {
        add_diag(result,
                 unsupported_code,
                 "KR6.5 E2 requires each canonical input expression and value to be unique",
                 statement.source_range);
        return std::nullopt;
    }

    const auto &expr = flow.exprs[let->expr.value];
    const auto *path = std::get_if<CorePathExpr>(&expr.node);
    if (path == nullptr || path->root != ir::core::CorePathRoot::Input ||
        path->root_type != agent.input_type || !path->members.empty() ||
        !path->projection.empty() || !path->projection_resolved || path->has_local) {
        add_diag(result,
                 unsupported_code,
                 "KR6.5 E2 accepts only an unprojected canonical input frame",
                 expr.source_range);
        return std::nullopt;
    }
    if (expr.result_type.value >= program.value_types.size() ||
        flow.value_types[let->result.value] != expr.result_type) {
        add_diag(result,
                 core_wasm_diag::kInvalidCore,
                 "canonical input expression and SSA value types disagree",
                 expr.source_range);
        return std::nullopt;
    }
    const auto *nominal =
        std::get_if<CoreVtNominal>(&program.value_types[expr.result_type.value].node);
    if (nominal == nullptr || nominal->base != agent.input_type) {
        add_diag(result,
                 unsupported_code,
                 "canonical input frame is not the agent input nominal value type",
                 expr.source_range);
        return std::nullopt;
    }

    used_exprs[let->expr.value] = true;
    used_values[let->result.value] = true;
    return let->result;
}

[[nodiscard]] bool validate_identity_final(const CoreProgram &program,
                                           const ir::core::CoreLayoutTable &layouts,
                                           const CoreAgentDecl &agent,
                                           const CoreFlowDecl &flow,
                                           const ir::core::CoreFlowState &handler,
                                           std::vector<bool> &used_exprs,
                                           std::vector<bool> &used_values,
                                           std::string_view unsupported_code,
                                           CoreWasmCodegenResult &result) {
    const auto &statements = handler.body.statements;
    if (statements.size() != 2) {
        add_diag(result,
                 unsupported_code,
                 "KR6.5 identity final must contain canonical input and return",
                 statements.empty() ? ir::SourceRangeOpt{}
                                    : statements.front().source_range);
        return false;
    }
    const auto input = validate_canonical_input_let(
        program,
        agent,
        flow,
        statements[0],
        used_exprs,
        used_values,
        unsupported_code,
        result);
    if (!input.has_value()) {
        return false;
    }
    const auto *ret = std::get_if<CoreReturnStmt>(&statements[1].node);
    if (ret == nullptr || !ret->has_value || ret->value != *input ||
        agent.input_type != agent.output_type) {
        add_diag(result,
                 unsupported_code,
                 "KR6.5 identity final is not the canonical input passthrough",
                 statements[1].source_range);
        return false;
    }
    const auto type = flow.value_types[input->value];
    if (!has_finalized_layout(layouts, type)) {
        add_diag(result,
                 core_wasm_diag::kInvalidLayout,
                 "canonical identity type has no finalized P4-D layout",
                 statements[0].source_range);
        return false;
    }
    return true;
}

[[nodiscard]] std::optional<CapabilityAction>
validate_capability_final(const CoreProgram &program,
                          const ir::core::CoreLayoutTable &layouts,
                          const CoreAgentDecl &agent,
                          const CoreFlowDecl &flow,
                          const ir::core::CoreFlowState &handler,
                          std::vector<bool> &used_exprs,
                          std::vector<bool> &used_values,
                          CoreWasmCodegenResult &result) {
    const auto &statements = handler.body.statements;
    if (statements.size() != 3) {
        add_diag(result,
                 core_wasm_diag::kUnsupportedCapabilityFrame,
                 "KR6.5 E2 capability final must contain canonical input, one call, and return",
                 statements.empty() ? ir::SourceRangeOpt{} : statements.front().source_range);
        return std::nullopt;
    }
    const auto input = validate_canonical_input_let(
        program,
        agent,
        flow,
        statements[0],
        used_exprs,
        used_values,
        core_wasm_diag::kUnsupportedCapabilityFrame,
        result);
    if (!input.has_value()) {
        return std::nullopt;
    }
    const auto *call = std::get_if<CoreCapabilityCallStmt>(&statements[1].node);
    const auto *ret = std::get_if<CoreReturnStmt>(&statements[2].node);
    if (call == nullptr || ret == nullptr || !ret->has_value ||
        ret->value != call->result || call->args.size() != 1 || call->args[0] != *input) {
        add_diag(result,
                 core_wasm_diag::kUnsupportedCapabilityFrame,
                 "KR6.5 E2 capability final is not the canonical opaque forwarding shape",
                 statements[1].source_range);
        return std::nullopt;
    }
    if (call->capability.value >= program.capabilities.size() ||
        call->result.value >= flow.value_types.size() ||
        call->result.value >= used_values.size() || used_values[call->result.value]) {
        add_diag(result,
                 core_wasm_diag::kInvalidCore,
                 "canonical capability call references an invalid or reused identity",
                 statements[1].source_range);
        return std::nullopt;
    }
    if (std::find(agent.capabilities.begin(), agent.capabilities.end(), call->capability) ==
        agent.capabilities.end()) {
        add_diag(result,
                 core_wasm_diag::kUnsupportedCapabilityFrame,
                 "canonical capability call is not in the target agent whitelist",
                 statements[1].source_range);
        return std::nullopt;
    }

    const auto &capability = program.capabilities[call->capability.value];
    const auto input_type = flow.value_types[input->value];
    const auto result_type = flow.value_types[call->result.value];
    if (capability.param_types.size() != 1 || capability.param_types[0] != input_type ||
        capability.return_type != result_type) {
        add_diag(result,
                 core_wasm_diag::kUnsupportedCapabilityFrame,
                 "canonical capability frame types do not exactly match the Core signature",
                 statements[1].source_range);
        return std::nullopt;
    }
    const auto *input_nominal =
        std::get_if<CoreVtNominal>(&program.value_types[input_type.value].node);
    const auto *result_nominal =
        std::get_if<CoreVtNominal>(&program.value_types[result_type.value].node);
    if (input_nominal == nullptr || input_nominal->base != agent.input_type ||
        result_nominal == nullptr || result_nominal->base != agent.output_type) {
        add_diag(result,
                 core_wasm_diag::kUnsupportedCapabilityFrame,
                 "capability input/output are not the exact agent nominal boundary types",
                 statements[1].source_range);
        return std::nullopt;
    }
    if (!has_finalized_layout(layouts, input_type) ||
        !has_finalized_layout(layouts, result_type)) {
        add_diag(result,
                 core_wasm_diag::kInvalidLayout,
                 "capability boundary type has no finalized P4-D layout",
                 statements[1].source_range);
        return std::nullopt;
    }

    used_values[call->result.value] = true;
    return CapabilityAction{call->capability};
}

[[nodiscard]] std::optional<AgentPlan>
build_agent_plan(const CoreProgram &program,
                 const ir::core::CoreLayoutTable &layouts,
                 CoreAgentId target,
                 CoreWasmCodegenResult &result,
                 AgentPlanPolicy policy = {}) {
    if (target.value >= program.agents.size()) {
        add_diag(result,
                 core_wasm_diag::kEntryNotFound,
                 "explicit Core agent entry is out of range");
        return std::nullopt;
    }

    const auto &agent = program.agents[target.value];
    const auto *flow = unique_target_flow(program, target);
    if (flow == nullptr) {
        add_diag(result,
                 core_wasm_diag::kEntryAmbiguous,
                 "explicit Core agent entry does not have one unique target flow");
        return std::nullopt;
    }
    if (!flow->patterns.empty() || !flow->coercion_plans.empty()) {
        const bool contains_capability =
            std::any_of(flow->states.begin(),
                        flow->states.end(),
                        [](const ir::core::CoreFlowState &state) {
                            return region_contains_capability(state.body);
                        });
        add_diag(result,
                 contains_capability
                     ? (policy.allow_capability ? core_wasm_diag::kUnsupportedCapabilityFrame
                                                : policy.unsupported_code)
                     : policy.unsupported_code,
                 "KR6.5 " + std::string(policy.slice) +
                     " rejects hidden pattern or coercion arenas");
        return std::nullopt;
    }
    if (agent.states.size() >=
        static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        add_diag(result,
                 core_wasm_diag::kBinaryOverflow,
                 "agent state count exceeds the wasm32 signed-immediate domain");
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

    AgentPlan plan;
    plan.agent = target;
    plan.initial = agent.initial;
    plan.actions.resize(agent.states.size(), IdentityAction{});
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
        const auto &statements = handler->body.statements;
        if (is_final_state(agent, state)) {
            const bool contains_capability = region_contains_capability(handler->body);
            if (!contains_capability) {
                if (!validate_identity_final(program,
                                             layouts,
                                             agent,
                                             *flow,
                                             *handler,
                                             used_exprs,
                                             used_values,
                                             policy.unsupported_code,
                                             result)) {
                    return std::nullopt;
                }
                plan.actions[state] = IdentityAction{};
            } else {
                if (!policy.allow_capability) {
                    add_diag(result,
                             policy.unsupported_code,
                             "KR6.5 " + std::string(policy.slice) +
                                 " does not compose a capability-bearing agent in a workflow",
                             handler->body.statements.empty()
                                 ? ir::SourceRangeOpt{}
                                 : handler->body.statements.front().source_range);
                    return std::nullopt;
                }
                auto action = validate_capability_final(program,
                                                        layouts,
                                                        agent,
                                                        *flow,
                                                        *handler,
                                                        used_exprs,
                                                        used_values,
                                                        result);
                if (!action.has_value()) {
                    return std::nullopt;
                }
                plan.actions[state] = *action;
            }
            continue;
        }

        if (statements.size() != 1) {
            const bool contains_capability = region_contains_capability(handler->body);
            add_diag(result,
                     contains_capability
                         ? (policy.allow_capability ? core_wasm_diag::kUnsupportedCapabilityFrame
                                                    : policy.unsupported_code)
                         : policy.unsupported_code,
                     "KR6.5 " + std::string(policy.slice) +
                         " requires a non-final handler to contain exactly one goto",
                     statements.empty() ? ir::SourceRangeOpt{}
                                        : statements.front().source_range);
            return std::nullopt;
        }
        const auto *go = std::get_if<CoreGotoStmt>(&statements.front().node);
        if (go == nullptr) {
            add_diag(result,
                     region_contains_capability(handler->body)
                         ? (policy.allow_capability ? core_wasm_diag::kUnsupportedCapabilityFrame
                                                    : policy.unsupported_code)
                         : policy.unsupported_code,
                     "KR6.5 " + std::string(policy.slice) +
                         " supports only CoreGotoStmt in a non-final handler",
                     statements.front().source_range);
            return std::nullopt;
        }
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
        plan.actions[state] = GotoAction{go->target};
    }

    if (std::any_of(used_exprs.begin(), used_exprs.end(), [](bool used) { return !used; }) ||
        std::any_of(used_values.begin(), used_values.end(), [](bool used) { return !used; })) {
        const bool contains_capability = std::any_of(
            plan.actions.begin(), plan.actions.end(), [](const StateAction &action) {
                return std::holds_alternative<CapabilityAction>(action);
            });
        add_diag(result,
                 contains_capability
                     ? (policy.allow_capability ? core_wasm_diag::kUnsupportedCapabilityFrame
                                                : policy.unsupported_code)
                     : policy.unsupported_code,
                 "KR6.5 " + std::string(policy.slice) +
                     " rejects hidden/orphan expressions or SSA values");
        return std::nullopt;
    }

    // The goto graph is functional. Every state must reach a final action.
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
            if (is_final_action(plan.actions[current])) {
                break;
            }
            current = std::get<GotoAction>(plan.actions[current]).target.value;
        }
        if (!is_final_action(plan.actions[current]) && color[current] == 1) {
            add_diag(result,
                     core_wasm_diag::kNonterminatingE1Run,
                     "KR6.5 " + std::string(policy.slice) +
                         " deterministic goto graph contains a cycle");
            return std::nullopt;
        }
        for (const auto state : path) {
            color[state] = 2;
        }
    }

    // Import only the action reachable from the declared initial state. The
    // E2 graph is functional, so this walk has exactly one terminal action;
    // unreachable declared finals must not expand host authority.
    std::uint32_t reachable = plan.initial.value;
    while (std::holds_alternative<GotoAction>(plan.actions[reachable])) {
        reachable = std::get<GotoAction>(plan.actions[reachable]).target.value;
    }
    if (const auto *capability =
            std::get_if<CapabilityAction>(&plan.actions[reachable])) {
        plan.imports.push_back(capability->capability);
    }
    std::sort(plan.imports.begin(), plan.imports.end(), [](auto lhs, auto rhs) {
        return lhs.value < rhs.value;
    });
    plan.imports.erase(std::unique(plan.imports.begin(), plan.imports.end()),
                       plan.imports.end());
    if (plan.imports.size() >
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max() - 7u)) {
        add_diag(result,
                 core_wasm_diag::kInvalidCapabilityAbi,
                 "reachable capability import table exceeds the wasm32 index domain");
        return std::nullopt;
    }
    for (const auto id : plan.imports) {
        const auto &capability = program.capabilities[id.value];
        if (!capability.symbol_ref.id.has_value() ||
            *capability.symbol_ref.id > std::numeric_limits<std::uint32_t>::max()) {
            add_diag(result,
                     core_wasm_diag::kInvalidCapabilityAbi,
                     "capability SymbolId is absent or exceeds the uint32 host ABI domain",
                     capability.source_range);
            return std::nullopt;
        }
    }
    return plan;
}

[[nodiscard]] const ir::core::CoreInstanceDecl *
agent_instance(const CoreProgram &program, CoreInstanceId id) {
    if (id.value >= program.instances.size()) {
        return nullptr;
    }
    return std::get_if<CoreAgentInstance>(&program.instances[id.value].payload) != nullptr
               ? &program.instances[id.value]
               : nullptr;
}

[[nodiscard]] const CoreAgentInstance *
agent_instance_payload(const CoreProgram &program, CoreInstanceId id) {
    const auto *instance = agent_instance(program, id);
    return instance == nullptr ? nullptr : std::get_if<CoreAgentInstance>(&instance->payload);
}

[[nodiscard]] bool workflow_node_is_ancestor(const CoreWorkflowDecl &workflow,
                                             CoreWorkflowNodeId node,
                                             CoreWorkflowNodeId possible_ancestor) {
    if (node.value >= workflow.nodes.size() || possible_ancestor.value >= workflow.nodes.size()) {
        return false;
    }
    std::vector<bool> seen(workflow.nodes.size(), false);
    std::vector<CoreWorkflowNodeId> pending = workflow.nodes[node.value].after;
    while (!pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        if (current.value >= workflow.nodes.size() || seen[current.value]) {
            continue;
        }
        if (current == possible_ancestor) {
            return true;
        }
        seen[current.value] = true;
        pending.insert(pending.end(),
                       workflow.nodes[current.value].after.begin(),
                       workflow.nodes[current.value].after.end());
    }
    return false;
}

[[nodiscard]] std::optional<WorkflowFrameSource>
validate_workflow_frame_region(const CoreProgram &program,
                               const CoreWorkflowDecl &workflow,
                               const ir::core::CoreRegion *region,
                               CoreValueTypeId expected_type,
                               std::optional<CoreWorkflowNodeId> owner_node,
                               const std::vector<std::uint32_t> &schedule_position,
                               std::vector<bool> &used_exprs,
                               std::vector<bool> &used_values,
                               CoreWasmCodegenResult &result) {
    if (region == nullptr || region->statements.size() != 2) {
        add_diag(result,
                 core_wasm_diag::kUnsupportedWorkflowFrame,
                 "KR6.5 E3 workflow frame region must contain one path let and one yield");
        return std::nullopt;
    }
    const auto &let_statement = region->statements[0];
    const auto &yield_statement = region->statements[1];
    const auto *let = std::get_if<CoreLetStmt>(&let_statement.node);
    const auto *yield = std::get_if<CoreYieldStmt>(&yield_statement.node);
    if (let == nullptr || yield == nullptr || !yield->has_value || yield->value != let->result ||
        let->expr.value >= workflow.exprs.size() ||
        let->result.value >= workflow.value_types.size() ||
        let->result.value >= used_values.size()) {
        add_diag(result,
                 core_wasm_diag::kUnsupportedWorkflowFrame,
                 "KR6.5 E3 workflow frame region is not canonical path-let/yield ANF",
                 let_statement.source_range);
        return std::nullopt;
    }
    if (used_exprs[let->expr.value] || used_values[let->result.value]) {
        add_diag(result,
                 core_wasm_diag::kUnsupportedWorkflowFrame,
                 "KR6.5 E3 workflow frame expression/value is reused across regions",
                 let_statement.source_range);
        return std::nullopt;
    }

    const auto &expr = workflow.exprs[let->expr.value];
    const auto *path = std::get_if<CorePathExpr>(&expr.node);
    if (path == nullptr || !path->members.empty() || !path->projection.empty() ||
        !path->projection_resolved || path->has_local ||
        expr.result_type.value >= program.value_types.size() ||
        workflow.value_types[let->result.value] != expr.result_type ||
        expr.result_type != expected_type) {
        add_diag(result,
                 core_wasm_diag::kUnsupportedWorkflowFrame,
                 "KR6.5 E3 accepts only an exact unprojected opaque workflow frame",
                 expr.source_range);
        return std::nullopt;
    }

    WorkflowFrameSource source;
    source.type = expr.result_type;
    if (path->root == ir::core::CorePathRoot::WorkflowInput) {
        if (path->root_type != workflow.input_type) {
            add_diag(result,
                     core_wasm_diag::kUnsupportedWorkflowFrame,
                     "workflow input path does not identify the declared workflow input shell",
                     expr.source_range);
            return std::nullopt;
        }
        const auto *nominal = std::get_if<CoreVtNominal>(
            &program.value_types[expr.result_type.value].node);
        if (nominal == nullptr || nominal->base != workflow.input_type) {
            add_diag(result,
                     core_wasm_diag::kUnsupportedWorkflowFrame,
                     "workflow input frame is not the exact input nominal value type",
                     expr.source_range);
            return std::nullopt;
        }
        source.kind = WorkflowFrameSourceKind::Input;
    } else if (path->root == ir::core::CorePathRoot::WorkflowNodeOutput) {
        if (path->workflow_node.value >= workflow.nodes.size()) {
            add_diag(result,
                     core_wasm_diag::kInvalidCore,
                     "workflow frame references an out-of-range node output",
                     expr.source_range);
            return std::nullopt;
        }
        const auto &source_node = workflow.nodes[path->workflow_node.value];
        const auto *source_instance = agent_instance(program, source_node.target_instance);
        const auto *source_payload =
            agent_instance_payload(program, source_node.target_instance);
        if (source_instance == nullptr || source_payload == nullptr ||
            source_instance->dispatch_types.size() != 3 ||
            path->root_type != source_payload->output_type ||
            expr.result_type != source_instance->dispatch_types[2]) {
            add_diag(result,
                     core_wasm_diag::kUnsupportedWorkflowFrame,
                     "workflow node-output frame does not match its target instance output",
                     expr.source_range);
            return std::nullopt;
        }
        if (owner_node.has_value() &&
            (!workflow_node_is_ancestor(workflow, *owner_node, path->workflow_node) ||
             schedule_position[path->workflow_node.value] >=
                 schedule_position[owner_node->value])) {
            add_diag(result,
                     core_wasm_diag::kUnsupportedWorkflowFrame,
                     "workflow node input reads an output that is not a scheduled ancestor",
                     expr.source_range);
            return std::nullopt;
        }
        source.kind = WorkflowFrameSourceKind::NodeOutput;
        source.node = path->workflow_node;
    } else {
        add_diag(result,
                 core_wasm_diag::kUnsupportedWorkflowFrame,
                 "workflow frame root is neither workflow input nor node output",
                 expr.source_range);
        return std::nullopt;
    }

    used_exprs[let->expr.value] = true;
    used_values[let->result.value] = true;
    return source;
}

[[nodiscard]] std::optional<WorkflowPlan>
build_workflow_plan(const CoreProgram &program,
                    const ir::core::CoreLayoutTable &layouts,
                    CoreWorkflowId target,
                    CoreWasmCodegenResult &result) {
    if (target.value >= program.workflows.size()) {
        add_diag(result,
                 core_wasm_diag::kEntryNotFound,
                 "explicit Core workflow entry is out of range");
        return std::nullopt;
    }
    const auto &workflow = program.workflows[target.value];
    if (!workflow.patterns.empty() || !workflow.coercion_plans.empty()) {
        add_diag(result,
                 core_wasm_diag::kUnsupportedWorkflowFrame,
                 "KR6.5 E3 rejects hidden workflow pattern or coercion arenas");
        return std::nullopt;
    }
    if (workflow.nodes.size() >=
        static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        add_diag(result,
                 core_wasm_diag::kBinaryOverflow,
                 "workflow node count exceeds the wasm32 signed-immediate domain");
        return std::nullopt;
    }

    WorkflowPlan plan;
    plan.workflow = target;
    plan.nodes.resize(workflow.nodes.size());

    std::vector<std::uint32_t> remaining(workflow.nodes.size(), 0);
    std::vector<std::vector<CoreWorkflowNodeId>> successors(workflow.nodes.size());
    for (std::uint32_t id = 0; id < workflow.nodes.size(); ++id) {
        remaining[id] = static_cast<std::uint32_t>(workflow.nodes[id].after.size());
        for (const auto dependency : workflow.nodes[id].after) {
            if (dependency.value >= workflow.nodes.size()) {
                add_diag(result,
                         core_wasm_diag::kInvalidCore,
                         "verified workflow contains an out-of-range dependency edge");
                return std::nullopt;
            }
            successors[dependency.value].push_back(CoreWorkflowNodeId{id});
        }
    }
    std::vector<CoreWorkflowNodeId> ready;
    ready.reserve(workflow.nodes.size());
    for (std::uint32_t id = 0; id < workflow.nodes.size(); ++id) {
        if (remaining[id] == 0) {
            ready.push_back(CoreWorkflowNodeId{id});
        }
    }
    for (std::size_t cursor = 0; cursor < ready.size(); ++cursor) {
        const auto id = ready[cursor];
        plan.schedule.push_back(id);
        for (const auto successor : successors[id.value]) {
            auto &count = remaining[successor.value];
            if (count == 0) {
                add_diag(result,
                         core_wasm_diag::kInvalidCore,
                         "verified workflow dependency count underflowed during scheduling");
                return std::nullopt;
            }
            --count;
            if (count == 0) {
                ready.push_back(successor);
            }
        }
    }
    if (plan.schedule.size() != workflow.nodes.size()) {
        add_diag(result,
                 core_wasm_diag::kInvalidCore,
                 "verified workflow did not produce a complete deterministic DAG schedule");
        return std::nullopt;
    }
    std::vector<std::uint32_t> schedule_position(workflow.nodes.size(), 0);
    for (std::uint32_t position = 0; position < plan.schedule.size(); ++position) {
        schedule_position[plan.schedule[position].value] = position;
    }

    for (const auto &node : workflow.nodes) {
        if (agent_instance(program, node.target_instance) == nullptr) {
            add_diag(result,
                     core_wasm_diag::kInvalidCore,
                     "workflow node target is not a materialized agent instance");
            return std::nullopt;
        }
        plan.packaged_instances.push_back(node.target_instance);
    }
    std::sort(plan.packaged_instances.begin(),
              plan.packaged_instances.end(),
              [](auto lhs, auto rhs) { return lhs.value < rhs.value; });
    plan.packaged_instances.erase(
        std::unique(plan.packaged_instances.begin(), plan.packaged_instances.end()),
        plan.packaged_instances.end());

    for (const auto instance_id : plan.packaged_instances) {
        const auto *payload = agent_instance_payload(program, instance_id);
        if (payload == nullptr) {
            add_diag(result,
                     core_wasm_diag::kInvalidCore,
                     "packaged workflow target is not an agent instance");
            return std::nullopt;
        }
        auto agent_plan = build_agent_plan(
            program,
            layouts,
            payload->base,
            result,
            AgentPlanPolicy{core_wasm_diag::kUnsupportedWorkflowFrame, false, "E3"});
        if (!agent_plan.has_value()) {
            return std::nullopt;
        }
        if (std::any_of(agent_plan->actions.begin(),
                        agent_plan->actions.end(),
                        [](const StateAction &action) {
                            return !std::holds_alternative<GotoAction>(action) &&
                                   !std::holds_alternative<IdentityAction>(action);
                        })) {
            add_diag(result,
                     core_wasm_diag::kUnsupportedWorkflowFrame,
                     "workflow packages only identity-returning agent instances");
            return std::nullopt;
        }
        plan.agent_plans.push_back(std::move(*agent_plan));
    }

    std::vector<bool> used_exprs(workflow.exprs.size(), false);
    std::vector<bool> used_values(workflow.value_count, false);
    for (std::uint32_t id = 0; id < workflow.nodes.size(); ++id) {
        const auto &node = workflow.nodes[id];
        const auto *instance = agent_instance(program, node.target_instance);
        if (instance == nullptr || instance->dispatch_types.size() != 3) {
            add_diag(result,
                     core_wasm_diag::kInvalidCore,
                     "workflow target instance has no exact input/context/output descriptor");
            return std::nullopt;
        }
        auto source = validate_workflow_frame_region(program,
                                                     workflow,
                                                     node.input_region.get(),
                                                     instance->dispatch_types[0],
                                                     CoreWorkflowNodeId{id},
                                                     schedule_position,
                                                     used_exprs,
                                                     used_values,
                                                     result);
        if (!source.has_value() || !has_finalized_layout(layouts, source->type) ||
            !has_finalized_layout(layouts, instance->dispatch_types[2])) {
            if (source.has_value()) {
                add_diag(result,
                         core_wasm_diag::kInvalidLayout,
                         "workflow target boundary has no finalized P4-D layout");
            }
            return std::nullopt;
        }
        plan.nodes[id] = WorkflowNodePlan{CoreWorkflowNodeId{id}, node.target_instance, *source};
    }

    // The return region's exact logical type is its source type. Its nominal base
    // must be the declared workflow output shell; no second type reconstruction is
    // permitted in codegen.
    if (workflow.return_region == nullptr || workflow.return_region->statements.empty()) {
        add_diag(result,
                 core_wasm_diag::kUnsupportedWorkflowFrame,
                 "workflow return region is absent");
        return std::nullopt;
    }
    const auto *return_let =
        std::get_if<CoreLetStmt>(&workflow.return_region->statements.front().node);
    if (return_let == nullptr || return_let->expr.value >= workflow.exprs.size()) {
        add_diag(result,
                 core_wasm_diag::kUnsupportedWorkflowFrame,
                 "workflow return is not a canonical path let");
        return std::nullopt;
    }
    const auto output_type = workflow.exprs[return_let->expr.value].result_type;
    if (output_type.value >= program.value_types.size()) {
        add_diag(result, core_wasm_diag::kInvalidCore, "workflow return type is out of range");
        return std::nullopt;
    }
    const auto *output_nominal =
        std::get_if<CoreVtNominal>(&program.value_types[output_type.value].node);
    if (output_nominal == nullptr || output_nominal->base != workflow.output_type) {
        add_diag(result,
                 core_wasm_diag::kUnsupportedWorkflowFrame,
                 "workflow return frame is not the exact declared output nominal type");
        return std::nullopt;
    }
    auto output = validate_workflow_frame_region(program,
                                                 workflow,
                                                 workflow.return_region.get(),
                                                 output_type,
                                                 std::nullopt,
                                                 schedule_position,
                                                 used_exprs,
                                                 used_values,
                                                 result);
    if (!output.has_value() || !has_finalized_layout(layouts, output->type)) {
        if (output.has_value()) {
            add_diag(result,
                     core_wasm_diag::kInvalidLayout,
                     "workflow output has no finalized P4-D layout");
        }
        return std::nullopt;
    }
    plan.output = *output;

    if (std::any_of(used_exprs.begin(), used_exprs.end(), [](bool used) { return !used; }) ||
        std::any_of(used_values.begin(), used_values.end(), [](bool used) { return !used; })) {
        add_diag(result,
                 core_wasm_diag::kUnsupportedWorkflowFrame,
                 "KR6.5 E3 rejects hidden/orphan workflow expressions or SSA values");
        return std::nullopt;
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
    body.u32(1);
    body.u32(1);
    body.byte(kI32);
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
    body.u32(0);
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
[[nodiscard]] ByteBuffer make_is_final_body(const AgentPlan &plan) {
    ByteBuffer body;
    body.u32(0);
    append_const(body, 0);
    for (std::uint32_t state = 0; state < plan.actions.size(); ++state) {
        if (!is_final_action(plan.actions[state])) {
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
[[nodiscard]] ByteBuffer make_step_body(const AgentPlan &plan) {
    ByteBuffer body;
    body.u32(0);
    for (std::uint32_t state = 0; state < plan.actions.size(); ++state) {
        append_indexed_op(body, kOpGlobalGet, kGlobalCurrentState);
        append_const(body, state);
        body.byte(kOpI32Eq);
        body.byte(kOpIf);
        body.byte(kI32);
        const auto &action = plan.actions[state];
        if (is_final_action(action)) {
            append_const(body, state);
        } else {
            const auto target = std::get<GotoAction>(action).target;
            append_const(body, target.value);
            append_indexed_op(body, kOpGlobalSet, kGlobalCurrentState);
            append_indexed_op(body, kOpGlobalGet, kGlobalTransitionCount);
            append_const(body, 1);
            body.byte(kOpI32Add);
            append_indexed_op(body, kOpGlobalSet, kGlobalTransitionCount);
            append_const(body, target.value);
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

void append_run_to_final(ByteBuffer &body,
                         const AgentPlan &plan,
                         const FunctionTable &functions,
                         std::uint32_t fuel_local) {
    append_const(body, plan.initial.value);
    append_indexed_op(body, kOpGlobalSet, kGlobalCurrentState);
    append_const(body, 0);
    append_indexed_op(body, kOpGlobalSet, kGlobalTransitionCount);
    append_const(body, static_cast<std::uint32_t>(plan.actions.size()) + 1u);
    append_indexed_op(body, kOpLocalSet, fuel_local);

    body.byte(kOpBlock);
    body.byte(kEmptyBlock);
    body.byte(kOpLoop);
    body.byte(kEmptyBlock);
    append_indexed_op(body, kOpCall, functions.is_final());
    append_indexed_op(body, kOpBrIf, 1);
    append_indexed_op(body, kOpLocalGet, fuel_local);
    body.byte(kOpI32Eqz);
    body.byte(kOpIf);
    body.byte(kEmptyBlock);
    body.byte(kOpUnreachable);
    body.byte(kOpEnd);
    append_indexed_op(body, kOpLocalGet, fuel_local);
    append_const(body, 1);
    body.byte(kOpI32Sub);
    append_indexed_op(body, kOpLocalSet, fuel_local);
    append_indexed_op(body, kOpCall, functions.step());
    body.byte(kOpDrop);
    append_indexed_op(body, kOpBr, 0);
    body.byte(kOpEnd);
    body.byte(kOpEnd);
}

[[nodiscard]] bool has_capability_action(const AgentPlan &plan) {
    return !plan.imports.empty();
}

[[nodiscard]] ByteBuffer make_run_body(const AgentPlan &plan,
                                       const FunctionTable &functions) {
    ByteBuffer body;
    if (has_capability_action(plan)) {
        // The pointer-only v1 ABI cannot represent status or length. Trap before
        // state mutation, input inspection, or any effect instead of discarding them.
        body.u32(0);
        body.byte(kOpUnreachable);
        body.byte(kOpEnd);
        return body;
    }
    body.u32(1);
    body.u32(1);
    body.byte(kI32); // local 2 = fuel
    append_run_to_final(body, plan, functions, 2);
    append_indexed_op(body, kOpLocalGet, 0);
    body.byte(kOpEnd);
    return body;
}

void append_error_return(ByteBuffer &body) {
    append_const(body, AHFL_CAP_ERROR);
    append_const(body, 0);
    append_const(body, 0);
    body.byte(kOpReturn);
}

[[nodiscard]] std::optional<std::uint32_t>
import_function_index(const AgentPlan &plan, CoreCapabilityId capability) {
    const auto it = std::lower_bound(plan.imports.begin(),
                                     plan.imports.end(),
                                     capability,
                                     [](auto lhs, auto rhs) { return lhs.value < rhs.value; });
    if (it == plan.imports.end() || *it != capability) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(it - plan.imports.begin());
}

void append_capability_return(ByteBuffer &body,
                              const AgentPlan &plan,
                              const CapabilityAction &action) {
    const auto function = import_function_index(plan, action.capability);
    if (!function.has_value()) {
        body.byte(kOpUnreachable);
        return;
    }
    append_indexed_op(body, kOpLocalGet, 0);
    append_indexed_op(body, kOpLocalGet, 1);
    append_indexed_op(body, kOpCall, *function);
    // Multi-value results are (status, ptr, len); pop in reverse order.
    append_indexed_op(body, kOpLocalSet, 4);
    append_indexed_op(body, kOpLocalSet, 3);
    append_indexed_op(body, kOpLocalSet, 2);

    append_indexed_op(body, kOpLocalGet, 2);
    append_const(body, AHFL_CAP_OK);
    body.byte(kOpI32Eq);
    body.byte(kOpIf);
    body.byte(kEmptyBlock);
    append_indexed_op(body, kOpLocalGet, 3);
    body.byte(kOpI32Eqz);
    append_indexed_op(body, kOpLocalGet, 4);
    body.byte(kOpI32Eqz);
    body.byte(kOpI32Or);
    body.byte(kOpIf);
    body.byte(kEmptyBlock);
    append_error_return(body);
    body.byte(kOpEnd);
    append_const(body, AHFL_CAP_OK);
    append_indexed_op(body, kOpLocalGet, 3);
    append_indexed_op(body, kOpLocalGet, 4);
    body.byte(kOpReturn);
    body.byte(kOpEnd);

    append_indexed_op(body, kOpLocalGet, 2);
    append_const(body, AHFL_CAP_PENDING);
    body.byte(kOpI32Eq);
    body.byte(kOpIf);
    body.byte(kEmptyBlock);
    append_indexed_op(body, kOpLocalGet, 3);
    body.byte(kOpI32Eqz);
    body.byte(kOpIf);
    body.byte(kEmptyBlock);
    append_const(body, 1);
    append_indexed_op(body, kOpGlobalSet, kGlobalPendingLatched);
    append_const(body, AHFL_CAP_PENDING);
    append_const(body, 0);
    append_const(body, 0);
    body.byte(kOpReturn);
    body.byte(kOpEnd);
    append_error_return(body);
    body.byte(kOpEnd);

    // AHFL_CAP_ERROR and every unknown status collapse to ERROR with no frame.
    append_error_return(body);
}

[[nodiscard]] ByteBuffer make_run2_body(const AgentPlan &plan,
                                        const FunctionTable &functions) {
    ByteBuffer body;
    body.u32(1);
    body.u32(4);
    body.byte(kI32); // locals 2=status, 3=ptr, 4=len, 5=fuel

    // A suspended instance cannot accept ownership of another input frame.
    // This check precedes state reset, input reads, and capability invocation.
    append_indexed_op(body, kOpGlobalGet, kGlobalPendingLatched);
    body.byte(kOpIf);
    body.byte(kEmptyBlock);
    body.byte(kOpUnreachable);
    body.byte(kOpEnd);

    append_run_to_final(body, plan, functions, 5);
    for (std::uint32_t state = 0; state < plan.actions.size(); ++state) {
        if (!is_final_action(plan.actions[state])) {
            continue;
        }
        append_indexed_op(body, kOpGlobalGet, kGlobalCurrentState);
        append_const(body, state);
        body.byte(kOpI32Eq);
        body.byte(kOpIf);
        body.byte(kEmptyBlock);
        if (std::holds_alternative<IdentityAction>(plan.actions[state])) {
            append_const(body, AHFL_CAP_OK);
            append_indexed_op(body, kOpLocalGet, 0);
            append_indexed_op(body, kOpLocalGet, 1);
            body.byte(kOpReturn);
        } else {
            append_capability_return(body, plan, std::get<CapabilityAction>(plan.actions[state]));
        }
        body.byte(kOpEnd);
    }
    body.byte(kOpUnreachable);
    body.byte(kOpEnd);
    return body;
}

[[nodiscard]] std::optional<std::vector<std::uint8_t>>
encode_module(const CoreProgram &program, const AgentPlan &plan) {
    const FunctionTable functions{static_cast<std::uint32_t>(plan.imports.size())};
    ByteBuffer module;
    module.raw({0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00});

    ByteBuffer types;
    types.u32(5);
    append_func_type(types, {}, {kI32});
    append_func_type(types, {kI32}, {kI32});
    append_func_type(types, {kI32, kI32}, {});
    append_func_type(types, {kI32, kI32}, {kI32});
    append_func_type(types, {kI32, kI32}, {kI32, kI32, kI32});
    if (!append_section(module, kSectionType, types)) {
        return std::nullopt;
    }

    if (!plan.imports.empty()) {
        ByteBuffer imports;
        imports.u32(static_cast<std::uint32_t>(plan.imports.size()));
        for (const auto id : plan.imports) {
            const auto symbol = *program.capabilities[id.value].symbol_ref.id;
            if (!imports.name("ahfl_cap") ||
                !imports.name("cap_" + std::to_string(symbol))) {
                return std::nullopt;
            }
            imports.byte(kImportFunction);
            imports.u32(kTypeCapabilityTuple);
        }
        if (!append_section(module, kSectionImport, imports)) {
            return std::nullopt;
        }
    }

    ByteBuffer functions_section;
    functions_section.u32(7);
    functions_section.u32(kTypeI32ToI32);
    functions_section.u32(kTypeTwoI32ToVoid);
    functions_section.u32(kTypeNoArgsI32);
    functions_section.u32(kTypeNoArgsI32);
    functions_section.u32(kTypeNoArgsI32);
    functions_section.u32(kTypeTwoI32ToI32);
    functions_section.u32(kTypeCapabilityTuple);
    if (!append_section(module, kSectionFunction, functions_section)) {
        return std::nullopt;
    }

    ByteBuffer memories;
    memories.u32(1);
    memories.byte(0);
    memories.u32(1);
    if (!append_section(module, kSectionMemory, memories)) {
        return std::nullopt;
    }

    ByteBuffer globals;
    globals.u32(5);
    append_global(globals, true, plan.initial.value);
    append_global(globals, true, 0);
    append_global(globals, false, 1);
    append_global(globals, true, 1024);
    append_global(globals, true, 0);
    if (!append_section(module, kSectionGlobal, globals)) {
        return std::nullopt;
    }

    ByteBuffer exports;
    exports.u32(9);
    const bool exports_ok = append_export(exports, "memory", kExportMemory, 0) &&
                            append_export(exports, "alloc", kExportFunction, functions.alloc()) &&
                            append_export(exports,
                                          "dealloc",
                                          kExportFunction,
                                          functions.dealloc()) &&
                            append_export(exports, "run", kExportFunction, functions.run()) &&
                            append_export(exports, "run2", kExportFunction, functions.run2()) &&
                            append_export(exports, "step", kExportFunction, functions.step()) &&
                            append_export(exports,
                                          "current_state",
                                          kExportFunction,
                                          functions.current_state()) &&
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
    code.u32(7);
    const auto alloc = make_alloc_body();
    const auto dealloc = make_dealloc_body();
    const auto current = make_current_state_body();
    const auto final = make_is_final_body(plan);
    const auto step = make_step_body(plan);
    const auto run = make_run_body(plan, functions);
    const auto run2 = make_run2_body(plan, functions);
    if (!code.sized(alloc) || !code.sized(dealloc) || !code.sized(current) ||
        !code.sized(final) || !code.sized(step) || !code.sized(run) ||
        !code.sized(run2) || !append_section(module, kSectionCode, code)) {
        return std::nullopt;
    }
    return std::move(module).take();
}

} // namespace

std::expected<CoreWasmEntry, CoreWasmDiagnostic>
resolve_core_wasm_entry(const CoreProgram &program,
                        const handoff::PackageMetadata *package_metadata) {
    const auto fail = [](std::string_view code, std::string message) {
        return std::unexpected<CoreWasmDiagnostic>(
            CoreWasmDiagnostic{std::string(code), std::move(message), std::nullopt});
    };
    if (package_metadata == nullptr) {
        if (program.agents.size() == 1 && program.workflows.empty()) {
            return CoreWasmEntry{CoreAgentId{0}};
        }
        return fail(core_wasm_diag::kEntryAmbiguous,
                    "emit wasm requires an explicit package entry for workflow or "
                    "multi-agent programs");
    }
    if (!package_metadata->entry_target.has_value()) {
        return fail(core_wasm_diag::kEntryAmbiguous,
                    "package metadata has no executable entry target");
    }

    const auto &entry = *package_metadata->entry_target;
    if (entry.kind != handoff::ExecutableKind::Agent &&
        entry.kind != handoff::ExecutableKind::Workflow) {
        return fail(core_wasm_diag::kEntryNotFound,
                    "explicit package entry has an unknown executable kind");
    }
    if (entry.kind == handoff::ExecutableKind::Agent) {
        std::optional<CoreAgentId> found;
        for (std::uint32_t i = 0; i < program.agents.size(); ++i) {
            if (program.agents[i].symbol_ref.canonical_name != entry.canonical_name) {
                continue;
            }
            if (found.has_value()) {
                return fail(core_wasm_diag::kEntryNotFound,
                            "explicit agent entry resolves more than once");
            }
            found = CoreAgentId{i};
        }
        if (!found.has_value()) {
            return fail(core_wasm_diag::kEntryNotFound,
                        "explicit agent entry did not resolve by exact canonical name");
        }
        return CoreWasmEntry{*found};
    }

    std::optional<CoreWorkflowId> found;
    for (std::uint32_t i = 0; i < program.workflows.size(); ++i) {
        if (program.workflows[i].symbol_ref.canonical_name != entry.canonical_name) {
            continue;
        }
        if (found.has_value()) {
            return fail(core_wasm_diag::kEntryNotFound,
                        "explicit workflow entry resolves more than once");
        }
        found = CoreWorkflowId{i};
    }
    if (!found.has_value()) {
        return fail(core_wasm_diag::kEntryNotFound,
                    "explicit workflow entry did not resolve by exact canonical name");
    }
    return CoreWasmEntry{*found};
}

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
                 "KR6.5 E2 supports only the exact canonical wasm32 target layout");
        return result;
    }
    switch (target.profile) {
    case WasmProfileKind::Wasi:
    case WasmProfileKind::Browser:
        break;
    default:
        add_diag(result, core_wasm_diag::kUnsupportedTarget, "unknown WASM deployment profile");
        return result;
    }

    if (const auto *workflow = std::get_if<CoreWorkflowId>(&target.entry)) {
        auto plan = build_workflow_plan(program, layouts, *workflow, result);
        if (!plan.has_value()) {
            return result;
        }
        add_diag(result,
                 core_wasm_diag::kUnsupportedOrchestration,
                 "KR6.5 E3-C1 validated the workflow plan; workflow byte emission lands in E3-C2");
        return result;
    }
    const auto *agent = std::get_if<CoreAgentId>(&target.entry);
    if (agent == nullptr) {
        add_diag(result, core_wasm_diag::kEntryNotFound, "unknown Core WASM entry variant");
        return result;
    }
    auto plan = build_agent_plan(program, layouts, *agent, result);
    if (!plan.has_value()) {
        return result;
    }
    auto bytes = encode_module(program, *plan);
    if (!bytes.has_value()) {
        add_diag(result,
                 core_wasm_diag::kBinaryOverflow,
                 "WASM binary section, name, or index exceeds the wasm32 encoding domain");
        return result;
    }

    CoreWasmArtifact artifact;
    artifact.bytes = std::move(*bytes);
    artifact.entry = target.entry;
    artifact.exports = {"memory",
                        "alloc",
                        "dealloc",
                        "run",
                        "run2",
                        "step",
                        "current_state",
                        "transition_count",
                        "ahfl_abi_version"};
    for (const auto id : plan->imports) {
        artifact.imports.push_back(
            "ahfl_cap.cap_" +
            std::to_string(*program.capabilities[id.value].symbol_ref.id));
    }
    result.artifact = std::move(artifact);
    return result;
}

} // namespace ahfl::backends
