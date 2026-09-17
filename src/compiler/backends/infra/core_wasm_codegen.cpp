#include "compiler/backends/infra/core_wasm_codegen.hpp"

#include "ahfl/base/support/overloaded.hpp"
#include "ahfl/compiler/ir/core_verify.hpp"
#include "ahfl/compiler/ir/core_wasm_abi_constants.hpp"
#include "ahfl/compiler/ir/core_wire_schema.hpp"
#include "ahfl/runtime/ahfl_host.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace ahfl::backends {

namespace {

using ir::core::CoreAgentDecl;
using ir::core::CoreAgentId;
using ir::core::CoreAgentInstance;
using ir::core::CoreBinaryExpr;
using ir::core::CoreBinaryOp;
using ir::core::CoreCapabilityCallStmt;
using ir::core::CoreCapabilityId;
using ir::core::CoreCoerceExpr;
using ir::core::CoreConstructExpr;
using ir::core::CoreExpr;
using ir::core::CoreExprId;
using ir::core::CoreFlowDecl;
using ir::core::CoreFlowState;
using ir::core::CoreGotoStmt;
using ir::core::CoreIfStmt;
using ir::core::CoreInstanceId;
using ir::core::CoreLayoutId;
using ir::core::CoreLetStmt;
using ir::core::CoreLiteralExpr;
using ir::core::CoreLiteralKind;
using ir::core::CoreMatchArm;
using ir::core::CoreMatchStmt;
using ir::core::CorePathExpr;
using ir::core::CoreProgram;
using ir::core::CoreQualifiedExpr;
using ir::core::CoreRegion;
using ir::core::CoreReturnStmt;
using ir::core::CoreStateId;
using ir::core::CoreStmt;
using ir::core::CoreStoreStmt;
using ir::core::CoreTrapStmt;
using ir::core::CoreUnaryExpr;
using ir::core::CoreUnaryOp;
using ir::core::CoreUnsupportedExpr;
using ir::core::CoreValueId;
using ir::core::CoreValueRefExpr;
using ir::core::CoreValueTypeId;
using ir::core::CoreValueTypeNode;
using ir::core::CoreVtBool;
using ir::core::CoreVtInt;
using ir::core::CoreVtNominal;
using ir::core::CoreWorkflowDecl;
using ir::core::CoreWorkflowId;
using ir::core::CoreWorkflowNodeId;
using ir::core::CoreYieldStmt;
using ir::core::kCoreWasmFixedLinearMemoryCapacityBytes;
using ir::core::kCoreWasmFixedLinearMemoryMinPages;

constexpr std::uint8_t kI32 = 0x7f;
constexpr std::uint8_t kI64 = 0x7e;
constexpr std::uint8_t kEmptyBlock = 0x40;
constexpr std::uint8_t kFuncType = 0x60;

constexpr std::uint8_t kSectionCustom = 0;
constexpr std::uint8_t kSectionType = 1;
constexpr std::uint8_t kSectionImport = 2;
constexpr std::uint8_t kSectionFunction = 3;
constexpr std::uint8_t kSectionMemory = 5;
constexpr std::uint8_t kSectionGlobal = 6;
constexpr std::uint8_t kSectionExport = 7;
constexpr std::uint8_t kSectionCode = 10;

// RFC 0026 E4-B1 wire-schema transport (seam doc §3.1): the deterministic
// logical wire schema for reachable capability imports rides in a single Wasm
// custom section, keyed by this canonical name, at the module's EOF. E1 no-import
// agents and E3 no-capability identity workflows never carry it; E2 agents and
// B2-C capability workflows carry the EOF AHFLWS section.
constexpr std::string_view kWireSchemaSectionName = "ahfl.wire-schema.v1";

// RFC 0026 E4-B2-C capability-workflow (seam doc §4.4): a capability-bearing
// workflow module carries a compiler-emitted execution manifest custom section
// (payload magic AHFLXM) exactly once, immediately BEFORE the EOF wire-schema
// section. The byte grammar is the exact mirror of the A2 decoder
// (src/runtime/engine/core_wasm_schema_module.cpp); no runtime code is linked
// or shared here.
constexpr std::string_view kExecManifestSectionName = "ahfl.wasm-exec-manifest.v1";
constexpr std::array<std::uint8_t, 6> kExecManifestMagic = {'A', 'H', 'F', 'L', 'X', 'M'};
constexpr std::uint8_t kExecManifestVersion = 1;
constexpr std::uint8_t kExecManifestEntryKindWorkflow = 0;

// RFC 0026 E4-B2-C node-event buffer (seam doc §4.4): a fixed linear-memory
// region observed by the host as post-run2 completion/ordering evidence. It is
// NOT a no-reinvoke proof (that is the B2-D host envelope). Header at 1024:
// event_count u32-LE at [0..3], pad[4..7]==0; records start at 1032; each record
// is a fixed 40 bytes; the region is statically sized to exactly node_count.
constexpr std::uint32_t kEventLogBase = 1024;
constexpr std::uint32_t kEventHeaderBytes = 8;
constexpr std::uint32_t kEventRecordBytes = 40;
constexpr std::uint32_t kEventRecordsBase = kEventLogBase + kEventHeaderBytes; // 1032
constexpr std::uint8_t kEventTagIdentity = 0;
constexpr std::uint8_t kEventTagCapability = 1;

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
constexpr std::uint8_t kOpLocalTee = 0x22;
constexpr std::uint8_t kOpGlobalGet = 0x23;
constexpr std::uint8_t kOpGlobalSet = 0x24;
constexpr std::uint8_t kOpI32Load = 0x28;
constexpr std::uint8_t kOpI32Store = 0x36;
constexpr std::uint8_t kOpI64Store = 0x37;
constexpr std::uint8_t kOpI32Const = 0x41;
constexpr std::uint8_t kOpI64Const = 0x42;
constexpr std::uint8_t kOpI32Eqz = 0x45;
constexpr std::uint8_t kOpI32Eq = 0x46;
// RFC 0026 P6 (KR6.6): the scalar ladder opcode set. The P6-0 scaffold emits
// unreachable from every expression/statement arm, so these are first
// referenced by P6-1; [[maybe_unused]] keeps the exhaustive opcode table
// present under clang's -Wunused-const-variable without per-reader guards.
[[maybe_unused]] constexpr std::uint8_t kOpI32Ne = 0x47;
[[maybe_unused]] constexpr std::uint8_t kOpI32LtS = 0x48;
constexpr std::uint8_t kOpI32LtU = 0x49;
[[maybe_unused]] constexpr std::uint8_t kOpI32LeS = 0x4a;
constexpr std::uint8_t kOpI32GtU = 0x4b;
[[maybe_unused]] constexpr std::uint8_t kOpI32GtS = 0x4c;
[[maybe_unused]] constexpr std::uint8_t kOpI32GeS = 0x4d;
[[maybe_unused]] constexpr std::uint8_t kOpI64Eqz = 0x50;
[[maybe_unused]] constexpr std::uint8_t kOpI64Eq = 0x51;
[[maybe_unused]] constexpr std::uint8_t kOpI64Ne = 0x52;
[[maybe_unused]] constexpr std::uint8_t kOpI64LtS = 0x53;
[[maybe_unused]] constexpr std::uint8_t kOpI64LeS = 0x54;
[[maybe_unused]] constexpr std::uint8_t kOpI64GtS = 0x55;
[[maybe_unused]] constexpr std::uint8_t kOpI64GeS = 0x56;
constexpr std::uint8_t kOpI32Add = 0x6a;
constexpr std::uint8_t kOpI32Sub = 0x6b;
constexpr std::uint8_t kOpI32Mul = 0x6c;
[[maybe_unused]] constexpr std::uint8_t kOpI32DivS = 0x6d;
[[maybe_unused]] constexpr std::uint8_t kOpI32RemS = 0x6f;
[[maybe_unused]] constexpr std::uint8_t kOpI32And = 0x71;
constexpr std::uint8_t kOpI32Or = 0x72;
[[maybe_unused]] constexpr std::uint8_t kOpI64Add = 0x7c;
[[maybe_unused]] constexpr std::uint8_t kOpI64Sub = 0x7d;
[[maybe_unused]] constexpr std::uint8_t kOpI64Mul = 0x7e;
[[maybe_unused]] constexpr std::uint8_t kOpI64DivS = 0x7f;
[[maybe_unused]] constexpr std::uint8_t kOpI64RemS = 0x81;

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
// RFC 0026 P6-1 (KR6.6): a non-final handler whose next state is decided by a
// lowered scalar computation (e.g. `if (a + b > c) goto X else goto Y`). The
// body bytes are emitted INLINE in `step()`'s dispatch arm: every path updates
// global current_state + transition_count exactly as a bare GotoAction does,
// or traps. `targets` are every state the body may branch to (the handler may
// be non-deterministic across runs), and `local_reprs` / `local_base` describe
// the step-function SSA locals the body references (i32 for Bool/narrow Int,
// i64 for wide Int — the repr is read from the P4-D layout table, never
// recomputed).
struct ComputedGotoAction {
    // Pre-lowered step()-inline body: every path updates global current_state
    // + transition_count exactly as a bare GotoAction does and returns the new
    // state id, or traps. References only the step-function SSA locals declared
    // once in make_step_body (i32 group then i64 group; indices shared across
    // all computed handlers since each step() dispatch runs one handler).
    std::vector<std::uint8_t> body;
    // Every state the body may branch to; the graph analysis treats the action
    // as nondeterministic over this successor set.
    std::vector<CoreStateId> targets;
};
struct IdentityAction {
    [[nodiscard]] friend bool operator==(IdentityAction, IdentityAction) noexcept = default;
};
struct CapabilityAction {
    CoreCapabilityId capability{};
    [[nodiscard]] friend bool operator==(CapabilityAction, CapabilityAction) noexcept = default;
};
using StateAction = std::variant<GotoAction, ComputedGotoAction, IdentityAction, CapabilityAction>;

struct AgentPlan {
    CoreAgentId agent{};
    CoreStateId initial{};
    std::vector<StateAction> actions;
    std::vector<CoreCapabilityId> imports;
    // RFC 0026 P6-1: step()-function SSA locals consumed by computed-goto
    // handler bodies. Wasm locals are FUNCTION scoped, so every computed
    // handler draws from two disjoint function-global pools (all i32 locals
    // occupy the low indices, i64 locals follow); the two counters are the
    // group sizes emitted by make_step_body's local declaration.
    std::uint32_t step_i32_locals{0};
    std::uint32_t step_i64_locals{0};
};

struct AgentPlanPolicy {
    std::string_view unsupported_code{core_wasm_diag::kUnsupportedOrchestration};
    bool allow_capability{true};
    // Workflow node packaging runs its agents' handlers through a linear,
    // single-target runner body; a P6 computed-goto handler cannot be inlined
    // there yet, so the workflow lane fails closed on it. Direct agent
    // emission accepts it.
    bool allow_computed_goto{true};
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
    // RFC 0026 E4-B2-C carriers. schedule_pos is the node's dense position in the
    // Kahn schedule (== its manifest array index). A node reachable to a single
    // capability carries that capability + its source SymbolId; identity nodes
    // leave has_capability == false.
    std::uint32_t schedule_pos{0};
    bool has_capability{false};
    CoreCapabilityId capability{};
    std::uint64_t source_symbol{0};
};

struct WorkflowPlan {
    CoreWorkflowId workflow{};
    std::vector<CoreWorkflowNodeId> schedule;
    std::vector<CoreInstanceId> packaged_instances;
    std::vector<AgentPlan> agent_plans;
    std::vector<WorkflowNodePlan> nodes;
    WorkflowFrameSource output;
    // RFC 0026 E4-B2-C: sorted-unique reachable capability ids across all node
    // agents (empty for an identity workflow). Non-empty triggers the
    // capability-workflow baseline (import section, manifest, event buffer, latch,
    // checked alloc).
    std::vector<CoreCapabilityId> imports;
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

struct WorkflowFunctionTable {
    // Imported functions occupy the low indices; every defined function index is
    // import_count + base (the same PROJECT rule the agent FunctionTable uses).
    // Identity workflows have import_count == 0, so their indices are unchanged.
    std::uint32_t import_count{0};
    std::uint32_t runner_count{0};
    [[nodiscard]] std::uint32_t alloc() const noexcept { return import_count + 0u; }
    [[nodiscard]] std::uint32_t dealloc() const noexcept { return import_count + 1u; }
    [[nodiscard]] std::uint32_t current_state() const noexcept { return import_count + 2u; }
    [[nodiscard]] std::uint32_t step() const noexcept { return import_count + 3u; }
    [[nodiscard]] std::uint32_t runner(std::uint32_t index) const noexcept {
        return import_count + 4u + index;
    }
    [[nodiscard]] std::uint32_t run() const noexcept {
        return import_count + 4u + runner_count;
    }
    [[nodiscard]] std::uint32_t run2() const noexcept {
        return import_count + 5u + runner_count;
    }
};

constexpr std::uint32_t kWorkflowGlobalTransitionCount = 0;
constexpr std::uint32_t kWorkflowGlobalAbiVersion = 1;
constexpr std::uint32_t kWorkflowGlobalHeapNext = 2;
constexpr std::uint32_t kWorkflowGlobalNodeCount = 3;
constexpr std::uint32_t kWorkflowGlobalCompletedCount = 4;
// RFC 0026 E4-B2-C: a capability-workflow module adds this private global as its
// suspend latch. It is emitted ONLY on the capability-workflow lane (see
// encode_workflow_module); identity workflows keep the 5-global section and its
// bytes unchanged. A function import never shifts global indices, so this global
// index is stable regardless of import_count.
constexpr std::uint32_t kWorkflowGlobalPendingLatched = 5;

void add_diag(CoreWasmCodegenResult &result,
              std::string_view code,
              std::string message,
              ir::SourceRangeOpt range = std::nullopt) {
    result.diagnostics.push_back(
        CoreWasmDiagnostic{std::string(code), std::move(message), std::move(range)});
}

[[nodiscard]] bool is_final_action(const StateAction &action) {
    return std::holds_alternative<IdentityAction>(action) ||
           std::holds_alternative<CapabilityAction>(action);
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

[[nodiscard]] bool region_contains_match(const ir::core::CoreRegion &region) {
    for (const auto &statement : region.statements) {
        if (std::holds_alternative<ir::core::CoreMatchStmt>(statement.node)) {
            return true;
        }
        if (const auto *branch = std::get_if<ir::core::CoreIfStmt>(&statement.node)) {
            if ((branch->then_region != nullptr &&
                 region_contains_match(*branch->then_region)) ||
                (branch->else_region != nullptr &&
                 region_contains_match(*branch->else_region))) {
                return true;
            }
        }
        if (const auto *match = std::get_if<ir::core::CoreMatchStmt>(&statement.node)) {
            for (const auto &arm : match->arms) {
                if ((arm.guard_region != nullptr &&
                     region_contains_match(*arm.guard_region)) ||
                    (arm.body != nullptr && region_contains_match(*arm.body))) {
                    return true;
                }
            }
            if (match->fallback_region != nullptr &&
                region_contains_match(*match->fallback_region)) {
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

class ByteBuffer {
  public:
    void byte(std::uint8_t value) { bytes_.push_back(value); }
    void raw(std::initializer_list<std::uint8_t> values) {
        bytes_.insert(bytes_.end(), values.begin(), values.end());
    }
    void raw_span(std::span<const std::uint8_t> values) {
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
    void u64(std::uint64_t value) {
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
    // RFC 0026 P6 (KR6.6): signed LEB128 immediates for i32.const / i64.const.
    // Logical-shift formulation of the canonical sign-extension termination
    // rule: stop when the remaining bits are all zero with a clear sign bit, or
    // all ones with a set sign bit.
    void s32(std::int32_t signed_value) {
        auto value = static_cast<std::uint32_t>(signed_value);
        while (true) {
            const auto next = static_cast<std::uint8_t>(value & 0x7fu);
            value >>= 7u;
            const bool sign_bit = (next & 0x40u) != 0;
            if ((value == 0 && !sign_bit) ||
                (value == 0x0fffffffu && sign_bit)) {
                byte(next);
                return;
            }
            byte(static_cast<std::uint8_t>(next | 0x80u));
        }
    }
    void s64(std::int64_t signed_value) {
        auto value = static_cast<std::uint64_t>(signed_value);
        while (true) {
            const auto next = static_cast<std::uint8_t>(value & 0x7fu);
            value >>= 7u;
            const bool sign_bit = (next & 0x40u) != 0;
            if ((value == 0 && !sign_bit) ||
                (value == 0x7fffffffffffffffull && sign_bit)) {
                byte(next);
                return;
            }
            byte(static_cast<std::uint8_t>(next | 0x80u));
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

// RFC 0026 P6 (KR6.6) — computation codegen scaffold.
//
// The KR6.5 backend emits one ACTION per state (goto / identity / capability)
// and validates those three canonical shapes. KR6.6 adds real per-handler
// computation lowering (scalar expressions, SSA locals, structured control
// flow). P6-0 introduces the infrastructure every later P6 slice hangs from,
// WITHOUT accepting a single new program:
//
//   * `is_p6_computation_region` is the fail-closed subset gate that separates a
//     P6 computation handler (ANF scalar lets, if/match structure, goto/trap
//     terminators; NO capability effect) from the KR6.5 orchestration shapes.
//   * `P6ComputationHandlerBuilder` is the per-handler body builder: its own
//     ByteBuffer plus a CoreValueId -> wasm-local table (handler parameters
//     bound first, then freshly allocated SSA locals). Expression and statement
//     lowering are COMPILE-TIME-EXHAUSTIVE std::variant visitors over all nine
//     CoreExprNode / CoreStmtNode arms — no catch-all, so a tenth IR node is a
//     compile error here, not a silent miscompile.
//
// Every visitor arm emits unreachable and publishes a diagnostic in P6-0, so
// try_build() always fails closed and no partial artifact is ever returned.
// P6-1 replaces the scalar arms one at a time; until then the E1-E3 byte paths
// are untouched.

[[nodiscard]] bool is_p6_computation_region(const CoreRegion &region) {
    for (const CoreStmt &statement : region.statements) {
        const bool in_subset = std::visit(
            Overloaded{
                [](const CoreLetStmt &) { return true; },
                [](const CoreGotoStmt &) { return true; },
                [](const CoreTrapStmt &) { return true; },
                [](const CoreIfStmt &s) {
                    const bool then_ok =
                        !s.then_region || is_p6_computation_region(*s.then_region);
                    const bool else_ok =
                        !s.else_region || is_p6_computation_region(*s.else_region);
                    return then_ok && else_ok;
                },
                [](const CoreMatchStmt &s) {
                    for (const CoreMatchArm &arm : s.arms) {
                        if (arm.guard_region &&
                            !is_p6_computation_region(*arm.guard_region)) {
                            return false;
                        }
                        if (!arm.body || !is_p6_computation_region(*arm.body)) {
                            return false;
                        }
                    }
                    return s.fallback_region &&
                           is_p6_computation_region(*s.fallback_region);
                },
                // Effects and non-P6 control flow stay on the KR6.5
                // orchestration lane (capability / store / return / yield).
                [](const CoreCapabilityCallStmt &) { return false; },
                [](const CoreStoreStmt &) { return false; },
                [](const CoreReturnStmt &) { return false; },
                [](const CoreYieldStmt &) { return false; },
            },
            statement.node);
        if (!in_subset) {
            return false;
        }
    }
    return true;
}

// The scalar kind of a value type for the P6 ladder, with the physical repr
// read from the P4-D layout table (design §2.1: codegen never recomputes a
// repr). Bool and narrow Int share the i32 repr but stay distinct KINDS so the
// boolean-only operators (! && ||) cannot be applied to an integer.
enum class P6ScalarKind {
    Bool,
    IntI32,
    IntI64
};

[[nodiscard]] std::optional<P6ScalarKind> p6_scalar_kind(const CoreProgram &program,
                                                         const ir::core::CoreLayoutTable &layouts,
                                                         CoreValueTypeId type) {
    if (type.value >= program.value_types.size() || type.value >= layouts.value_layouts.size()) {
        return std::nullopt;
    }
    const CoreLayoutId layout_id = layouts.value_layouts[type.value];
    if (layout_id.value >= layouts.layouts.size()) {
        return std::nullopt;
    }
    const auto *scalar =
        std::get_if<ir::core::CoreLayoutScalar>(&layouts.layouts[layout_id.value].shape);
    if (scalar == nullptr || scalar->repr == ir::core::CoreScalarRepr::F64) {
        // F64 needs the f64 opcode ladder, which is a later P6 slice; every
        // non-scalar aggregate is outside the scalar subset.
        return std::nullopt;
    }
    const CoreValueTypeNode &node = program.value_types[type.value].node;
    if (std::holds_alternative<CoreVtBool>(node)) {
        return scalar->repr == ir::core::CoreScalarRepr::I32 ? std::optional{P6ScalarKind::Bool}
                                                             : std::nullopt;
    }
    if (std::holds_alternative<CoreVtInt>(node)) {
        switch (scalar->repr) {
        case ir::core::CoreScalarRepr::I32:
            return P6ScalarKind::IntI32;
        case ir::core::CoreScalarRepr::I64:
            return P6ScalarKind::IntI64;
        case ir::core::CoreScalarRepr::F64:
            return std::nullopt;
        }
    }
    return std::nullopt;
}

// Structural all-paths termination predicate for a P6 scalar handler region.
// P6-1 accepts a computed-goto handler only when EVERY path leaves via a goto
// or trap (so `step()` always performs exactly one transition or traps and
// never falls through with an undefined next state). If/branch fallthrough and
// every other terminator keep the handler on a later slice.
[[nodiscard]] bool p6_region_always_diverges(const CoreRegion &region) {
    if (region.statements.empty()) {
        return false;
    }
    const CoreStmt &last = region.statements.back();
    return std::visit(Overloaded{
                          [](const CoreGotoStmt &) { return true; },
                          [](const CoreTrapStmt &) { return true; },
                          [](const CoreIfStmt &s) {
                              return s.then_region && s.else_region &&
                                     p6_region_always_diverges(*s.then_region) &&
                                     p6_region_always_diverges(*s.else_region);
                          },
                          [](const CoreMatchStmt &s) {
                              if (!s.fallback_region ||
                                  !p6_region_always_diverges(*s.fallback_region)) {
                                  return false;
                              }
                              return std::ranges::all_of(s.arms, [](const CoreMatchArm &arm) {
                                  return arm.body && p6_region_always_diverges(*arm.body);
                              });
                          },
                          [](const auto &) { return false; },
                      },
                      last.node);
}

class P6ComputationHandlerBuilder {
  public:
    P6ComputationHandlerBuilder(const CoreProgram &program,
                                const ir::core::CoreLayoutTable &layouts,
                                const CoreFlowDecl &flow,
                                const CoreFlowState &handler,
                                std::string_view unsupported_code,
                                std::vector<bool> &used_exprs,
                                std::vector<bool> &used_values,
                                CoreWasmCodegenResult &result)
        : program_(program), layouts_(layouts), flow_(flow), handler_(handler),
          unsupported_code_(unsupported_code), used_exprs_(used_exprs), used_values_(used_values),
          result_(result), locals_(flow.value_count, LocalInfo{}) {}

    // Validate the handler is in the P6-1 scalar subset, assign every bound
    // SSA value a per-repr pool slot, and record the goto target set. The i64
    // pool's concrete indices depend on the FUNCTION-wide i32 group size (wasm
    // locals are function scoped with one fixed type per index), so a second
    // emit() pass resolves final indices once all handlers are planned.
    [[nodiscard]] bool plan() {
        if (!p6_region_always_diverges(handler_.body)) {
            return reject("non-final scalar handler must goto or trap on every path",
                          handler_.body.statements.empty()
                              ? ir::SourceRangeOpt{}
                              : handler_.body.statements.front().source_range);
        }
        return plan_region(handler_.body);
    }

    [[nodiscard]] std::uint32_t i32_count() const noexcept {
        return i32_count_;
    }
    [[nodiscard]] std::uint32_t i64_count() const noexcept {
        return i64_count_;
    }
    [[nodiscard]] const std::vector<CoreStateId> &targets() const noexcept {
        return targets_;
    }

    // Emit the step()-inline body. `i64_base` is the function-wide count of
    // i32 SSA locals (i64 slots follow them). Every goto path performs exactly
    // the E1 transition (set current_state, bump transition_count) and returns
    // the new state; trap paths emit unreachable.
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> emit(std::uint32_t i64_base) {
        i64_base_ = i64_base;
        if (!emit_region(handler_.body)) {
            return std::nullopt;
        }
        return std::move(body_).take();
    }

  private:
    struct LocalInfo {
        P6ScalarKind kind{P6ScalarKind::IntI32};
        std::uint32_t slot{0}; // index within the kind's pool
        bool bound{false};
    };

    const CoreProgram &program_;
    const ir::core::CoreLayoutTable &layouts_;
    const CoreFlowDecl &flow_;
    const CoreFlowState &handler_;
    std::string_view unsupported_code_;
    std::vector<bool> &used_exprs_;
    std::vector<bool> &used_values_;
    CoreWasmCodegenResult &result_;

    ByteBuffer body_;
    std::vector<LocalInfo> locals_; // CoreValueId -> pool slot
    std::uint32_t i32_count_{0};
    std::uint32_t i64_count_{0};
    std::uint32_t i64_base_{0};
    std::vector<CoreStateId> targets_;

    [[nodiscard]] bool reject(std::string message, ir::SourceRangeOpt range) {
        body_.byte(kOpUnreachable);
        add_diag(result_,
                 unsupported_code_,
                 "RFC 0026 P6 scalar codegen cannot lower handler of state '" +
                     handler_.state_name + "': " + std::move(message),
                 std::move(range));
        return false;
    }

    [[nodiscard]] std::optional<P6ScalarKind> scalar_kind(CoreValueTypeId type) const {
        return p6_scalar_kind(program_, layouts_, type);
    }

    [[nodiscard]] std::optional<std::uint32_t> final_local(CoreValueId value) const {
        if (value.value >= locals_.size() || !locals_[value.value].bound) {
            return std::nullopt;
        }
        const LocalInfo &info = locals_[value.value];
        return info.kind == P6ScalarKind::IntI64 ? i64_base_ + info.slot : info.slot;
    }

    // Assign a pool slot to a let-bound value (one slot per CoreValueId; the
    // Core verifier proves flow-global single definition).
    [[nodiscard]] bool bind_value(CoreValueId value, P6ScalarKind kind) {
        if (value.value >= locals_.size() || locals_[value.value].bound) {
            return false;
        }
        LocalInfo &info = locals_[value.value];
        info.bound = true;
        info.kind = kind;
        info.slot = kind == P6ScalarKind::IntI64 ? i64_count_++ : i32_count_++;
        return true;
    }

    void record_target(CoreStateId target) {
        if (std::find(targets_.begin(), targets_.end(), target) == targets_.end()) {
            targets_.push_back(target);
        }
    }

    // --- planning pass: validate subset + assign slots + mark consumed arena ---

    [[nodiscard]] bool plan_region(const CoreRegion &region) {
        for (const CoreStmt &statement : region.statements) {
            if (!plan_statement(statement)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool plan_expr(const CoreExprId id) {
        if (id.value >= flow_.exprs.size()) {
            return false;
        }
        used_exprs_[id.value] = true;
        const CoreExpr &expr = flow_.exprs[id.value];
        if (scalar_kind(expr.result_type) == std::nullopt) {
            return reject("scalar expression has a non-scalar or f64 result type",
                          expr.source_range);
        }
        return std::visit(
            Overloaded{
                [&](const CoreLiteralExpr &) { return plan_literal(expr); },
                [&](const CoreValueRefExpr &r) {
                    used_values_[r.value.value] = true;
                    if (final_local_like(r.value) == std::nullopt) {
                        return reject("value reference is not a scalar local bound in this handler",
                                      expr.source_range);
                    }
                    return true;
                },
                [&](const CoreUnaryExpr &u) { return plan_expr(u.operand); },
                [&](const CoreBinaryExpr &b) { return plan_expr(b.lhs) && plan_expr(b.rhs); },
                [&](const CorePathExpr &) {
                    return reject("input/context field loads are a later P6 slice",
                                  expr.source_range);
                },
                [&](const CoreQualifiedExpr &) {
                    return reject("enum variant projections are a later P6 slice",
                                  expr.source_range);
                },
                [&](const CoreConstructExpr &) {
                    return reject("aggregate construction is a later P6 slice", expr.source_range);
                },
                [&](const CoreCoerceExpr &) {
                    return reject("scalar coercion is outside the P6 scalar subset",
                                  expr.source_range);
                },
                [&](const CoreUnsupportedExpr &) {
                    return reject("expression was not fully lowered to Core-IR", expr.source_range);
                },
            },
            expr.node);
    }

    // Kind lookup that does not depend on the emit-only i64 base.
    [[nodiscard]] std::optional<P6ScalarKind> final_local_like(CoreValueId value) const {
        if (value.value >= locals_.size() || !locals_[value.value].bound) {
            return std::nullopt;
        }
        return locals_[value.value].kind;
    }

    [[nodiscard]] bool plan_literal(const CoreExpr &expr) {
        const auto kind = *scalar_kind(expr.result_type);
        const auto &lit = std::get<CoreLiteralExpr>(expr.node);
        switch (lit.kind) {
        case CoreLiteralKind::Bool:
            return kind == P6ScalarKind::Bool;
        case CoreLiteralKind::Integer:
            return kind != P6ScalarKind::Bool;
        default:
            return false;
        }
    }

    [[nodiscard]] bool plan_statement(const CoreStmt &statement) {
        return std::visit(
            Overloaded{
                [&](const CoreLetStmt &s) {
                    if (s.expr.value >= flow_.exprs.size()) {
                        return reject("let references an out-of-range expression",
                                      statement.source_range);
                    }
                    const CoreExpr &bound = flow_.exprs[s.expr.value];
                    const auto kind = scalar_kind(bound.result_type);
                    if (kind == std::nullopt) {
                        return reject("let value has a non-scalar or f64 type",
                                      statement.source_range);
                    }
                    if (!bind_value(s.result, *kind)) {
                        return reject("SSA value is bound more than once", statement.source_range);
                    }
                    // Every let result is a real lowered SSA local in the
                    // emitted handler (a bound-but-unread result such as
                    // `let q = one / zero` is frontend-valid and its effect —
                    // the trapping division — must still be emitted). Orphan
                    // EXPRESSIONS remain gated by the arena consumption check.
                    used_values_[s.result.value] = true;
                    return plan_expr(s.expr);
                },
                [&](const CoreIfStmt &s) {
                    if (final_local_like(s.condition) != P6ScalarKind::Bool) {
                        return reject("if condition must be a Bool local", statement.source_range);
                    }
                    used_values_[s.condition.value] = true;
                    return (!s.then_region || plan_region(*s.then_region)) &&
                           (!s.else_region || plan_region(*s.else_region));
                },
                [&](const CoreGotoStmt &go) {
                    record_target(go.target);
                    return true;
                },
                [](const CoreTrapStmt &) { return true; },
                [&](const CoreMatchStmt &) {
                    return reject("match lowering is a later P6 slice", statement.source_range);
                },
                [&](const CoreCapabilityCallStmt &) {
                    return reject("capability effects stay on the orchestration lane",
                                  statement.source_range);
                },
                [&](const CoreStoreStmt &) {
                    return reject("context stores are a later P6 slice", statement.source_range);
                },
                [&](const CoreReturnStmt &) {
                    return reject("value-returning handlers are a later P6 slice",
                                  statement.source_range);
                },
                [&](const CoreYieldStmt &) {
                    return reject("yield is illegal in a flow handler", statement.source_range);
                },
            },
            statement.node);
    }

    // --- emit pass: scalar stack machine ---

    void emit_const_i32(std::int32_t value) {
        body_.byte(kOpI32Const);
        body_.s32(value);
    }
    void emit_const_i64(std::int64_t value) {
        body_.byte(kOpI64Const);
        body_.s64(value);
    }
    void emit_local_get(std::uint32_t local) {
        body_.byte(kOpLocalGet);
        body_.u32(local);
    }

    [[nodiscard]] bool emit_region(const CoreRegion &region) {
        for (const CoreStmt &statement : region.statements) {
            if (!emit_statement(statement)) {
                return false;
            }
        }
        return true;
    }

    // Parse the nonnegative decimal spelling an Integer literal carries
    // (INT_LITERAL is DIGIT+; source-level negation is a separate Neg node).
    [[nodiscard]] static std::optional<std::uint64_t>
    parse_unsigned_spelling(const std::string &spelling) {
        if (spelling.empty()) {
            return std::nullopt;
        }
        std::uint64_t value = 0;
        for (const char c : spelling) {
            if (c < '0' || c > '9') {
                return std::nullopt;
            }
            const std::uint64_t digit = static_cast<std::uint64_t>(c - '0');
            if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10u) {
                return std::nullopt;
            }
            value = value * 10u + digit;
        }
        return value;
    }

    [[nodiscard]] bool emit_literal(const CoreLiteralExpr &lit,
                                    P6ScalarKind kind,
                                    const CoreVtInt *int_type,
                                    ir::SourceRangeOpt range) {
        if (lit.kind == CoreLiteralKind::Bool) {
            if (kind != P6ScalarKind::Bool) {
                return reject("bool literal has a non-Bool type", std::move(range));
            }
            if (lit.spelling != "true" && lit.spelling != "false") {
                return reject("bool literal has an unrecognized spelling", std::move(range));
            }
            emit_const_i32(lit.spelling == "true" ? 1 : 0);
            return true;
        }
        if (lit.kind != CoreLiteralKind::Integer || kind == P6ScalarKind::Bool) {
            return reject("only Bool and Integer literals are in the scalar subset",
                          std::move(range));
        }
        const auto parsed = parse_unsigned_spelling(lit.spelling);
        if (!parsed.has_value()) {
            return reject("integer literal spelling does not parse or overflows uint64",
                          std::move(range));
        }
        const std::uint64_t value = *parsed;
        // Fail closed against BOTH the physical repr and any declared bounded
        // refinement; never silently truncate a constant.
        if (kind == P6ScalarKind::IntI32 &&
            value > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
            return reject("integer literal exceeds the i32 scalar range", std::move(range));
        }
        if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            return reject("integer literal exceeds the i64 scalar range", std::move(range));
        }
        if (int_type != nullptr && int_type->bounds.has_value()) {
            const auto signed_value = static_cast<std::int64_t>(value);
            if (signed_value < int_type->bounds->first || signed_value > int_type->bounds->second) {
                return reject("integer literal is outside its declared bounded-Int range",
                              std::move(range));
            }
        }
        if (kind == P6ScalarKind::IntI32) {
            emit_const_i32(static_cast<std::int32_t>(value));
        } else {
            emit_const_i64(static_cast<std::int64_t>(value));
        }
        return true;
    }

    [[nodiscard]] bool emit_expr(const CoreExprId id) {
        if (id.value >= flow_.exprs.size()) {
            return false;
        }
        const CoreExpr &expr = flow_.exprs[id.value];
        const auto result_kind = scalar_kind(expr.result_type);
        if (result_kind == std::nullopt) {
            return reject("scalar expression has a non-scalar or f64 result type",
                          expr.source_range);
        }
        bool ok = std::visit(
            Overloaded{
                [&](const CoreLiteralExpr &lit) {
                    const CoreVtInt *int_type =
                        std::get_if<CoreVtInt>(&program_.value_types[expr.result_type.value].node);
                    return emit_literal(lit, *result_kind, int_type, expr.source_range);
                },
                [&](const CoreValueRefExpr &r) {
                    const auto local = final_local(r.value);
                    if (local == std::nullopt) {
                        return reject("value reference is not a scalar local bound in this handler",
                                      expr.source_range);
                    }
                    emit_local_get(*local);
                    return true;
                },
                [&](const CoreUnaryExpr &u) { return emit_unary(u, expr.source_range); },
                [&](const CoreBinaryExpr &b) { return emit_binary(b, expr.source_range); },
                [&](const CorePathExpr &) {
                    return reject("input/context field loads are a later P6 slice",
                                  expr.source_range);
                },
                [&](const CoreQualifiedExpr &) {
                    return reject("enum variant projections are a later P6 slice",
                                  expr.source_range);
                },
                [&](const CoreConstructExpr &) {
                    return reject("aggregate construction is a later P6 slice", expr.source_range);
                },
                [&](const CoreCoerceExpr &) {
                    return reject("scalar coercion is outside the P6 scalar subset",
                                  expr.source_range);
                },
                [&](const CoreUnsupportedExpr &) {
                    return reject("expression was not fully lowered to Core-IR", expr.source_range);
                },
            },
            expr.node);
        return ok;
    }

    [[nodiscard]] bool emit_unary(const CoreUnaryExpr &u, ir::SourceRangeOpt range) {
        if (u.operand.value >= flow_.exprs.size()) {
            return false;
        }
        const P6ScalarKind operand_kind = *scalar_kind(flow_.exprs[u.operand.value].result_type);
        switch (u.op) {
        case CoreUnaryOp::Not:
            if (operand_kind != P6ScalarKind::Bool) {
                return reject("logical not requires a Bool operand", std::move(range));
            }
            if (!emit_expr(u.operand)) {
                return false;
            }
            body_.byte(kOpI32Eqz);
            return true;
        case CoreUnaryOp::Neg:
            if (operand_kind == P6ScalarKind::Bool) {
                return reject("arithmetic negation requires an Int operand", std::move(range));
            }
            // 0 - x; wasm wrap semantics (negating INT_MIN wraps), matching the
            // documented P6 integer contract.
            if (operand_kind == P6ScalarKind::IntI32) {
                emit_const_i32(0);
            } else {
                emit_const_i64(0);
            }
            if (!emit_expr(u.operand)) {
                return false;
            }
            body_.byte(operand_kind == P6ScalarKind::IntI32 ? kOpI32Sub : kOpI64Sub);
            return true;
        }
        return false;
    }

    [[nodiscard]] bool emit_binary(const CoreBinaryExpr &b, ir::SourceRangeOpt range) {
        if (b.lhs.value >= flow_.exprs.size() || b.rhs.value >= flow_.exprs.size()) {
            return false;
        }
        const auto lhs_kind = scalar_kind(flow_.exprs[b.lhs.value].result_type);
        const auto rhs_kind = scalar_kind(flow_.exprs[b.rhs.value].result_type);
        if (lhs_kind == std::nullopt || rhs_kind == std::nullopt || *lhs_kind != *rhs_kind) {
            return reject("binary operands must share one scalar type", std::move(range));
        }
        const P6ScalarKind kind = *lhs_kind;
        const bool wide = kind == P6ScalarKind::IntI64;
        const bool bool_operands = kind == P6ScalarKind::Bool;

        struct Opcode {
            std::uint8_t i32;
            std::uint8_t i64;
        };
        std::optional<Opcode> opcode;
        bool is_comparison = false;
        switch (b.op) {
        case CoreBinaryOp::Add:
            opcode = {kOpI32Add, kOpI64Add};
            break;
        case CoreBinaryOp::Sub:
            opcode = {kOpI32Sub, kOpI64Sub};
            break;
        case CoreBinaryOp::Mul:
            opcode = {kOpI32Mul, kOpI64Mul};
            break;
        // Signed division / remainder: a zero divisor traps at runtime, which
        // is the deliberately documented P6 integer semantics.
        case CoreBinaryOp::Div:
            opcode = {kOpI32DivS, kOpI64DivS};
            break;
        case CoreBinaryOp::Mod:
            opcode = {kOpI32RemS, kOpI64RemS};
            break;
        case CoreBinaryOp::Eq:
            opcode = {kOpI32Eq, kOpI64Eq};
            is_comparison = true;
            break;
        case CoreBinaryOp::Ne:
            opcode = {kOpI32Ne, kOpI64Ne};
            is_comparison = true;
            break;
        case CoreBinaryOp::Lt:
            opcode = {kOpI32LtS, kOpI64LtS};
            is_comparison = true;
            break;
        case CoreBinaryOp::Le:
            opcode = {kOpI32LeS, kOpI64LeS};
            is_comparison = true;
            break;
        case CoreBinaryOp::Gt:
            opcode = {kOpI32GtS, kOpI64GtS};
            is_comparison = true;
            break;
        case CoreBinaryOp::Ge:
            opcode = {kOpI32GeS, kOpI64GeS};
            is_comparison = true;
            break;
        case CoreBinaryOp::And:
            if (!bool_operands) {
                return reject("logical and requires Bool operands", std::move(range));
            }
            opcode = {kOpI32And, kOpI32And};
            break;
        case CoreBinaryOp::Or:
            if (!bool_operands) {
                return reject("logical or requires Bool operands", std::move(range));
            }
            opcode = {kOpI32Or, kOpI32Or};
            break;
        }
        if (!opcode.has_value()) {
            return reject("binary operator is outside the P6 scalar subset", std::move(range));
        }
        if (bool_operands && !is_comparison && b.op != CoreBinaryOp::And &&
            b.op != CoreBinaryOp::Or) {
            return reject("arithmetic operator requires Int operands", std::move(range));
        }
        if (!bool_operands && (b.op == CoreBinaryOp::And || b.op == CoreBinaryOp::Or)) {
            return reject("logical and/or requires Bool operands", std::move(range));
        }
        if (!emit_expr(b.lhs) || !emit_expr(b.rhs)) {
            return false;
        }
        body_.byte(wide ? opcode->i64 : opcode->i32);
        return true;
    }

    void emit_goto_transition(const CoreGotoStmt &go) {
        // Mirrors the E1 bare-goto step tail exactly: latch current_state,
        // bump transition_count once, return the new state id. `return` works
        // from any nested if/block depth.
        emit_const_i32(static_cast<std::int32_t>(go.target.value));
        body_.byte(kOpGlobalSet);
        body_.u32(kGlobalCurrentState);
        body_.byte(kOpGlobalGet);
        body_.u32(kGlobalTransitionCount);
        emit_const_i32(1);
        body_.byte(kOpI32Add);
        body_.byte(kOpGlobalSet);
        body_.u32(kGlobalTransitionCount);
        emit_const_i32(static_cast<std::int32_t>(go.target.value));
        body_.byte(kOpReturn);
    }

    [[nodiscard]] bool emit_statement(const CoreStmt &statement) {
        return std::visit(
            Overloaded{
                [&](const CoreLetStmt &s) {
                    if (!emit_expr(s.expr)) {
                        return false;
                    }
                    const auto local = final_local(s.result);
                    if (local == std::nullopt) {
                        return reject("let result has no SSA local", statement.source_range);
                    }
                    body_.byte(kOpLocalSet);
                    body_.u32(*local);
                    return true;
                },
                [&](const CoreIfStmt &s) {
                    const auto cond = final_local(s.condition);
                    if (cond == std::nullopt) {
                        return reject("if condition has no Bool local", statement.source_range);
                    }
                    emit_local_get(*cond);
                    body_.byte(kOpIf);
                    // A fully-diverging if (both branches goto/trap on every
                    // path) is the handler's terminator and is typed i32: every
                    // branch returns polymorphically, and the produced i32 also
                    // satisfies the enclosing step()-dispatch arm. An if that
                    // can fall through is a plain void control block; the
                    // verifier proves a fully-diverging if is always terminal.
                    const bool diverges = s.then_region && s.else_region &&
                                          p6_region_always_diverges(*s.then_region) &&
                                          p6_region_always_diverges(*s.else_region);
                    body_.byte(diverges ? kI32 : kEmptyBlock);
                    if (s.then_region && !emit_region(*s.then_region)) {
                        return false;
                    }
                    if (s.else_region) {
                        body_.byte(kOpElse);
                        if (!emit_region(*s.else_region)) {
                            return false;
                        }
                    }
                    body_.byte(kOpEnd);
                    return true;
                },
                [&](const CoreGotoStmt &go) {
                    record_target(go.target);
                    emit_goto_transition(go);
                    return true;
                },
                [&](const CoreTrapStmt &) {
                    body_.byte(kOpUnreachable);
                    return true;
                },
                [&](const CoreMatchStmt &) {
                    return reject("match lowering is a later P6 slice", statement.source_range);
                },
                [&](const CoreCapabilityCallStmt &) {
                    return reject("capability effects stay on the orchestration lane",
                                  statement.source_range);
                },
                [&](const CoreStoreStmt &) {
                    return reject("context stores are a later P6 slice", statement.source_range);
                },
                [&](const CoreReturnStmt &) {
                    return reject("value-returning handlers are a later P6 slice",
                                  statement.source_range);
                },
                [&](const CoreYieldStmt &) {
                    return reject("yield is illegal in a flow handler", statement.source_range);
                },
            },
            statement.node);
    }
};

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
    // RFC 0026 P6 (KR6.6): coercion plans stay on the KR6.5 reject path
    // (scalar coercion lowering is not part of the P6 ladder). A non-empty
    // PATTERN arena is admitted past this gate only when some handler is a pure
    // P6 computation region actually containing a match — otherwise canonical
    // E1-E3 shapes that merely carry leftover pattern artifacts keep the exact
    // legacy rejection. The admitted flow still fails closed per-handler below,
    // because P6-0 lowers no arm.
    if (!flow->coercion_plans.empty() ||
        (!flow->patterns.empty() &&
         !std::any_of(flow->states.begin(),
                      flow->states.end(),
                      [](const ir::core::CoreFlowState &state) {
                          return region_contains_match(state.body) &&
                                 is_p6_computation_region(state.body);
                      }))) {
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

    // RFC 0026 P6-1: computed-goto handlers planned in the per-state loop and
    // emitted in a second pass once the function-wide maximum per-repr local
    // group sizes are known (wasm locals are function scoped; disjoint handler
    // invocations reuse the same local indices).
    struct PendingComputed {
        std::uint32_t state{0};
        std::unique_ptr<P6ComputationHandlerBuilder> builder;
    };
    std::vector<PendingComputed> pending_computed;

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

        // RFC 0026 P6 (KR6.6): the canonical KR6.5 non-final handler is a
        // single bare CoreGotoStmt. Anything that is NOT that shape but IS a
        // pure computation region (ANF scalar lets + structured if + goto/trap
        // terminators, never a capability effect) enters the per-handler scalar
        // builder (P6-1: Literal/ValueRef/Unary/Binary + computed goto). All
        // effect-bearing or otherwise out-of-subset regions keep the legacy
        // rejections below.
        const bool single_goto =
            statements.size() == 1 &&
            std::holds_alternative<CoreGotoStmt>(statements.front().node);
        if (!single_goto) {
            if (!handler->body.statements.empty() &&
                is_p6_computation_region(handler->body)) {
                if (!policy.allow_computed_goto) {
                    add_diag(result,
                             policy.unsupported_code,
                             "KR6.5 " + std::string(policy.slice) +
                                 " does not yet package a P6 computed-goto handler",
                             statements.front().source_range);
                    return std::nullopt;
                }
                auto builder =
                    std::make_unique<P6ComputationHandlerBuilder>(program,
                                                                  layouts,
                                                                  *flow,
                                                                  *handler,
                                                                  policy.unsupported_code,
                                                                  used_exprs,
                                                                  used_values,
                                                                  result);
                if (!builder->plan()) {
                    return std::nullopt;
                }
                // Every dynamically reachable target must be a declared legal
                // edge of THIS state.
                for (const CoreStateId destination : builder->targets()) {
                    const bool legal =
                        std::any_of(agent.transitions.begin(),
                                    agent.transitions.end(),
                                    [state, destination](const auto &edge) {
                                        return edge.from.value == state && edge.to == destination;
                                    });
                    if (!legal) {
                        add_diag(result,
                                 core_wasm_diag::kInvalidCore,
                                 "computed goto target is not present in the agent's legal "
                                 "transition table",
                                 statements.front().source_range);
                        return std::nullopt;
                    }
                }
                plan.step_i32_locals = std::max(plan.step_i32_locals, builder->i32_count());
                plan.step_i64_locals = std::max(plan.step_i64_locals, builder->i64_count());
                pending_computed.push_back(PendingComputed{state, std::move(builder)});
                continue;
            }
            const bool contains_capability = region_contains_capability(handler->body);
            add_diag(result,
                     contains_capability
                         ? (policy.allow_capability ? core_wasm_diag::kUnsupportedCapabilityFrame
                                                    : policy.unsupported_code)
                         : policy.unsupported_code,
                     statements.size() != 1
                         ? "KR6.5 " + std::string(policy.slice) +
                               " requires a non-final handler to contain exactly one goto"
                         : "KR6.5 " + std::string(policy.slice) +
                               " supports only CoreGotoStmt in a non-final handler",
                     statements.empty() ? ir::SourceRangeOpt{}
                                        : statements.front().source_range);
            return std::nullopt;
        }
        const auto &go = std::get<CoreGotoStmt>(statements.front().node);
        const bool legal = std::any_of(
            agent.transitions.begin(), agent.transitions.end(), [state, &go](const auto &edge) {
                return edge.from.value == state && edge.to == go.target;
            });
        if (!legal) {
            add_diag(result,
                     core_wasm_diag::kInvalidCore,
                     "goto is not present in the agent's legal transition table",
                     statements.front().source_range);
            return std::nullopt;
        }
        plan.actions[state] = GotoAction{go.target};
    }

    // Second pass: emit every planned computed-goto body now that the shared
    // i32 group size (the i64 local base) is final.
    for (auto &pending : pending_computed) {
        auto body = pending.builder->emit(plan.step_i32_locals);
        if (!body.has_value()) {
            return std::nullopt;
        }
        plan.actions[pending.state] =
            ComputedGotoAction{std::move(*body), pending.builder->targets()};
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

    // The goto graph must be acyclic and every state must reach a final action.
    // A computed-goto state is NONDETERMINISTIC: ALL of its targets are
    // possible next states, so the analysis is a three-color DFS over the
    // successor SET (a reachable back edge on any path is rejected), not the
    // E1 single-successor walk.
    const auto successors = [&](std::uint32_t state) -> std::vector<std::uint32_t> {
        return std::visit(Overloaded{
                              [](const GotoAction &a) { return std::vector{a.target.value}; },
                              [](const ComputedGotoAction &a) {
                                  std::vector<std::uint32_t> out;
                                  out.reserve(a.targets.size());
                                  for (const CoreStateId target : a.targets) {
                                      out.push_back(target.value);
                                  }
                                  return out;
                              },
                              [](const IdentityAction &) { return std::vector<std::uint32_t>{}; },
                              [](const CapabilityAction &) { return std::vector<std::uint32_t>{}; },
                          },
                          plan.actions[state]);
    };
    enum class Color : std::uint8_t {
        White,
        Gray,
        Black
    };
    std::vector<Color> colors(plan.actions.size(), Color::White);
    std::function<bool(std::uint32_t)> acyclic_to_final = [&](std::uint32_t state) -> bool {
        if (colors[state] == Color::Black) {
            return true;
        }
        if (colors[state] == Color::Gray) {
            return false; // back edge: cycle
        }
        colors[state] = Color::Gray;
        for (const std::uint32_t next : successors(state)) {
            if (!acyclic_to_final(next)) {
                return false;
            }
        }
        colors[state] = Color::Black;
        return true;
    };
    for (std::uint32_t state = 0; state < plan.actions.size(); ++state) {
        if (!acyclic_to_final(state)) {
            add_diag(result,
                     core_wasm_diag::kNonterminatingE1Run,
                     "KR6.5 " + std::string(policy.slice) +
                         " goto graph contains a reachable cycle");
            return std::nullopt;
        }
    }

    // Import only the capability finals reachable from the declared initial
    // state across EVERY possible computed-goto path. The E2 least-privilege
    // contract permits at most one distinct reachable capability import.
    std::vector<std::uint8_t> reachable_state(plan.actions.size(), 0);
    {
        std::vector<std::uint32_t> worklist{plan.initial.value};
        reachable_state[plan.initial.value] = 1;
        while (!worklist.empty()) {
            const std::uint32_t current = worklist.back();
            worklist.pop_back();
            for (const std::uint32_t next : successors(current)) {
                if (!reachable_state[next]) {
                    reachable_state[next] = 1;
                    worklist.push_back(next);
                }
            }
        }
    }
    for (std::uint32_t state = 0; state < plan.actions.size(); ++state) {
        if (!reachable_state[state]) {
            continue;
        }
        if (const auto *capability = std::get_if<CapabilityAction>(&plan.actions[state])) {
            plan.imports.push_back(capability->capability);
        }
    }
    std::sort(plan.imports.begin(), plan.imports.end(), [](auto lhs, auto rhs) {
        return lhs.value < rhs.value;
    });
    plan.imports.erase(std::unique(plan.imports.begin(), plan.imports.end()),
                       plan.imports.end());
    if (plan.imports.size() > 1) {
        add_diag(result,
                 core_wasm_diag::kUnsupportedCapabilityFrame,
                 "KR6.5 " + std::string(policy.slice) +
                     " computed-goto graph reaches more than one capability final");
        return std::nullopt;
    }
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

[[nodiscard]] std::optional<std::uint32_t>
workflow_runner_index(const WorkflowPlan &plan, CoreInstanceId instance);

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
        // RFC 0026 E4-B2-C: a capability-bearing agent instance is now a legal
        // workflow node. build_agent_plan validates the E2 capability contract
        // (functional goto graph reaching exactly one terminal action, least-
        // privilege single reachable import). Non-goto/identity/capability
        // actions still fail closed inside build_agent_plan.
        auto agent_plan =
            build_agent_plan(program,
                             layouts,
                             payload->base,
                             result,
                             AgentPlanPolicy{core_wasm_diag::kUnsupportedWorkflowFrame,
                                             /*allow_capability=*/true,
                                             /*allow_computed_goto=*/false,
                                             "E3"});
        if (!agent_plan.has_value()) {
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
        plan.nodes[id].schedule_pos = schedule_position[id];
    }

    // RFC 0026 E4-B2-C: derive each node's capability identity (if any) from its
    // packaged agent plan and aggregate the sorted-unique reachable capability
    // set. build_agent_plan already reduced each agent to at most one reachable
    // import (least privilege); a node carries that capability + its source
    // SymbolId so the manifest and node-event records can name it. An identity
    // node leaves has_capability == false. A non-empty plan.imports selects the
    // capability-workflow baseline.
    for (std::uint32_t id = 0; id < plan.nodes.size(); ++id) {
        const auto runner = workflow_runner_index(plan, plan.nodes[id].target_instance);
        if (!runner.has_value() || *runner >= plan.agent_plans.size()) {
            add_diag(result,
                     core_wasm_diag::kInvalidCore,
                     "workflow node target has no packaged runner plan");
            return std::nullopt;
        }
        const auto &agent_plan = plan.agent_plans[*runner];
        if (!agent_plan.imports.empty()) {
            const auto capability = agent_plan.imports.front();
            if (capability.value >= program.capabilities.size()) {
                add_diag(result,
                         core_wasm_diag::kInvalidCore,
                         "workflow node capability id is out of range");
                return std::nullopt;
            }
            const auto &symbol = program.capabilities[capability.value].symbol_ref;
            if (!symbol.id.has_value()) {
                add_diag(result,
                         core_wasm_diag::kInvalidCapabilityAbi,
                         "workflow node capability has no source SymbolId");
                return std::nullopt;
            }
            plan.nodes[id].has_capability = true;
            plan.nodes[id].capability = capability;
            plan.nodes[id].source_symbol = *symbol.id;
            plan.imports.push_back(capability);
        }
    }
    std::sort(plan.imports.begin(), plan.imports.end(), [](auto lhs, auto rhs) {
        return lhs.value < rhs.value;
    });
    plan.imports.erase(std::unique(plan.imports.begin(), plan.imports.end()),
                       plan.imports.end());
    if (plan.imports.size() >
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max() - 6u)) {
        add_diag(result,
                 core_wasm_diag::kInvalidCapabilityAbi,
                 "reachable capability import table exceeds the wasm32 index domain");
        return std::nullopt;
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

[[nodiscard]] ByteBuffer make_alloc_body(std::uint32_t heap_global = kGlobalHeapNext) {
    ByteBuffer body;
    body.u32(1);
    body.u32(1);
    body.byte(kI32);
    append_indexed_op(body, kOpGlobalGet, heap_global);
    append_indexed_op(body, kOpLocalSet, 1);
    append_indexed_op(body, kOpGlobalGet, heap_global);
    append_indexed_op(body, kOpLocalGet, 0);
    body.byte(kOpI32Add);
    append_indexed_op(body, kOpGlobalSet, heap_global);
    append_indexed_op(body, kOpLocalGet, 1);
    body.byte(kOpEnd);
    return body;
}
// RFC 0026 E4-B2-C: capability-workflow-private checked bump allocator. Keeps the
// existing (i32)->i32 signature. Computes new = heap_next + len; if new exceeds
// the fixed 64 KiB page (65536), returns the reserved null pointer 0 WITHOUT
// advancing heap_next. Otherwise advances and returns the old heap_next. The
// shared unchecked make_alloc_body used by E1/E2/E3 is left untouched.
[[nodiscard]] ByteBuffer make_checked_alloc_body(std::uint32_t heap_global) {
    ByteBuffer body;
    body.u32(1);
    body.u32(2);
    body.byte(kI32); // locals 1 = old heap_next, 2 = new heap_next
    append_indexed_op(body, kOpGlobalGet, heap_global);
    append_indexed_op(body, kOpLocalSet, 1);
    append_indexed_op(body, kOpLocalGet, 1);
    append_indexed_op(body, kOpLocalGet, 0);
    body.byte(kOpI32Add);
    append_indexed_op(body, kOpLocalSet, 2);
    // Capacity check: new > fixed single-page capacity OR new < old (wrap) ->
    // return 0, no advance.
    append_indexed_op(body, kOpLocalGet, 2);
    append_const(body, kCoreWasmFixedLinearMemoryCapacityBytes);
    body.byte(kOpI32GtU);
    append_indexed_op(body, kOpLocalGet, 2);
    append_indexed_op(body, kOpLocalGet, 1);
    body.byte(kOpI32LtU);
    body.byte(kOpI32Or);
    body.byte(kOpIf);
    body.byte(kEmptyBlock);
    append_const(body, 0);
    body.byte(kOpReturn);
    body.byte(kOpEnd);
    append_indexed_op(body, kOpLocalGet, 2);
    append_indexed_op(body, kOpGlobalSet, heap_global);
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
    // RFC 0026 P6-1: step()-function SSA locals for computed-goto handlers.
    // All i32 temporaries occupy the low group; i64 temporaries follow. Pure
    // E1-E2 agents declare none, so their bytes are unchanged.
    std::uint32_t local_groups = 0;
    if (plan.step_i32_locals != 0) {
        ++local_groups;
    }
    if (plan.step_i64_locals != 0) {
        ++local_groups;
    }
    body.u32(local_groups);
    if (plan.step_i32_locals != 0) {
        body.u32(plan.step_i32_locals);
        body.byte(kI32);
    }
    if (plan.step_i64_locals != 0) {
        body.u32(plan.step_i64_locals);
        body.byte(kI64);
    }
    for (std::uint32_t state = 0; state < plan.actions.size(); ++state) {
        append_indexed_op(body, kOpGlobalGet, kGlobalCurrentState);
        append_const(body, state);
        body.byte(kOpI32Eq);
        body.byte(kOpIf);
        body.byte(kI32);
        const auto &action = plan.actions[state];
        if (const auto *go = std::get_if<GotoAction>(&action)) {
            append_const(body, go->target.value);
            append_indexed_op(body, kOpGlobalSet, kGlobalCurrentState);
            append_indexed_op(body, kOpGlobalGet, kGlobalTransitionCount);
            append_const(body, 1);
            body.byte(kOpI32Add);
            append_indexed_op(body, kOpGlobalSet, kGlobalTransitionCount);
            append_const(body, go->target.value);
        } else if (const auto *computed = std::get_if<ComputedGotoAction>(&action)) {
            // The pre-lowered handler body runs the scalar stack machine and
            // returns the next state id itself (bumping globals on every goto
            // path), or traps. It is emitted inline inside this dispatch arm.
            body.raw_span(computed->body);
        } else {
            // Identity / capability final: the state is stable and reports
            // itself without incrementing the transition counter.
            append_const(body, state);
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
encode_module(const CoreProgram &program,
              const AgentPlan &plan,
              std::span<const std::uint8_t> wire_schema_payload) {
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
    memories.u32(1); // one memory
    memories.byte(0); // limits flags: no declared maximum
    memories.u32(kCoreWasmFixedLinearMemoryMinPages);
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

    // RFC 0026 E4-B1: exactly one wire-schema custom section, fixed after the
    // Code section, present iff the agent has reachable capability imports (the
    // caller passes an empty payload otherwise). The custom payload is the
    // canonical Wasm custom-section framing (name-length LEB + name) followed by
    // the encoded table bytes (`AHFLWS...`) verbatim; no re-projection here.
    if (!wire_schema_payload.empty()) {
        ByteBuffer custom;
        if (!custom.name(kWireSchemaSectionName)) {
            return std::nullopt;
        }
        custom.raw_span(wire_schema_payload);
        if (!append_section(module, kSectionCustom, custom)) {
            return std::nullopt;
        }
    }
    return std::move(module).take();
}

[[nodiscard]] ByteBuffer make_trapping_i32_body() {
    ByteBuffer body;
    body.u32(0);
    body.byte(kOpUnreachable);
    body.byte(kOpEnd);
    return body;
}

// RFC 0026 E4-B2-C: two-phase capability-workflow memory sizing (seam §4.4/§5.2).
// PHASE 1 (checked wasm32 arithmetic): node_count * 40, + header, + align, all
// verified against the wasm32 domain; any overflow is a BINARY_OVERFLOW. PHASE 2
// (capacity): only once a legal heap_base exists, heap_base > 65536 is a
// RESOURCE_EXHAUSTED. Returns the computed heap_base, or a diagnostic code in
// `overflow_is_binary` selecting which failure to raise. node_count = 1612 fits
// (heap_base 65512); node_count = 1613 is rejected (65552).
struct EventLayout {
    std::uint32_t heap_base{0};
};
[[nodiscard]] std::optional<EventLayout>
compute_event_layout(std::size_t node_count, bool &overflow_is_binary) {
    overflow_is_binary = false;
    // Phase 1: checked wasm32 arithmetic.
    if (node_count > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        overflow_is_binary = true;
        return std::nullopt;
    }
    const auto n = static_cast<std::uint32_t>(node_count);
    if (n != 0 && n > (std::numeric_limits<std::uint32_t>::max() - kEventHeaderBytes) /
                          kEventRecordBytes) {
        overflow_is_binary = true;
        return std::nullopt;
    }
    const std::uint32_t event_bytes = kEventHeaderBytes + n * kEventRecordBytes;
    if (event_bytes > std::numeric_limits<std::uint32_t>::max() - kEventLogBase) {
        overflow_is_binary = true;
        return std::nullopt;
    }
    std::uint32_t unaligned = kEventLogBase + event_bytes;
    if (unaligned > std::numeric_limits<std::uint32_t>::max() - 7u) {
        overflow_is_binary = true;
        return std::nullopt;
    }
    const std::uint32_t heap_base = (unaligned + 7u) & ~static_cast<std::uint32_t>(7u);
    // Phase 2: capacity against the fixed single page.
    if (heap_base > kCoreWasmFixedLinearMemoryCapacityBytes) {
        overflow_is_binary = false;
        return std::nullopt;
    }
    return EventLayout{heap_base};
}

// Store a 32-bit little-endian immediate at a fixed absolute address (i32.store,
// align=2, offset=0). The address and value are pushed by the caller lambdas.
void append_i32_store_const(ByteBuffer &body, std::uint32_t addr, std::uint32_t value) {
    append_const(body, addr);
    append_const(body, value);
    body.byte(kOpI32Store);
    body.u32(2u); // alignment (log2(4))
    body.u32(0u); // offset
}

// Store a 64-bit zero at a fixed absolute address (i64.store, align=3, offset=0).
void append_i64_store_zero(ByteBuffer &body, std::uint32_t addr) {
    append_const(body, addr);
    body.byte(kOpI64Const);
    body.byte(0x00); // SLEB128 for 0
    body.byte(kOpI64Store);
    body.u32(3u); // alignment (log2(8))
    body.u32(0u); // offset
}

// RFC 0026 E4-B2-C: emit the AHFLXM execution-manifest payload, byte-identical to
// the A2 decoder's canonical grammar. Private to this TU; no runtime code shared.
// Nodes are emitted in schedule order so schedule_pos == array index.
[[nodiscard]] std::optional<std::vector<std::uint8_t>>
encode_exec_manifest(const WorkflowPlan &plan) {
    if (plan.schedule.size() >
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return std::nullopt;
    }
    if (plan.workflow.value == CoreWorkflowId::kInvalid) {
        return std::nullopt;
    }
    ByteBuffer out;
    out.raw_span(
        std::span<const std::uint8_t>(kExecManifestMagic.data(), kExecManifestMagic.size()));
    out.byte(kExecManifestVersion);
    out.byte(kExecManifestEntryKindWorkflow);
    out.u32(plan.workflow.value);
    out.u32(static_cast<std::uint32_t>(plan.schedule.size()));
    for (std::uint32_t index = 0; index < plan.schedule.size(); ++index) {
        const auto node_id = plan.schedule[index];
        if (node_id.value >= plan.nodes.size()) {
            return std::nullopt;
        }
        const auto &node = plan.nodes[node_id.value];
        if (node.node.value == CoreWorkflowNodeId::kInvalid) {
            return std::nullopt;
        }
        out.u32(node.node.value);
        out.u32(index); // schedule_pos == array index
        out.byte(node.has_capability ? std::uint8_t{1} : std::uint8_t{0});
        if (node.has_capability) {
            if (node.capability.value == CoreCapabilityId::kInvalid) {
                return std::nullopt;
            }
            out.u32(node.capability.value);
            out.u64(node.source_symbol);
        }
    }
    return std::move(out).take();
}

[[nodiscard]] std::optional<std::uint32_t>
workflow_runner_index(const WorkflowPlan &plan, CoreInstanceId instance) {
    const auto it = std::lower_bound(plan.packaged_instances.begin(),
                                     plan.packaged_instances.end(),
                                     instance,
                                     [](auto lhs, auto rhs) { return lhs.value < rhs.value; });
    if (it == plan.packaged_instances.end() || *it != instance) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(it - plan.packaged_instances.begin());
}

struct WorkflowRunnerWalk {
    std::vector<std::pair<CoreStateId, CoreStateId>> transitions;
    CoreStateId terminal{};
};

[[nodiscard]] std::optional<WorkflowRunnerWalk>
workflow_initial_transitions(const AgentPlan &plan) {
    if (plan.initial.value >= plan.actions.size()) {
        return std::nullopt;
    }

    std::vector<bool> visited(plan.actions.size(), false);
    WorkflowRunnerWalk walk;
    auto state = plan.initial;
    while (true) {
        if (state.value >= plan.actions.size() || visited[state.value]) {
            return std::nullopt;
        }
        visited[state.value] = true;
        const auto &action = plan.actions[state.value];
        // Both IdentityAction and CapabilityAction are legal terminals; only
        // GotoAction continues the deterministic walk.
        if (!std::holds_alternative<GotoAction>(action)) {
            walk.terminal = state;
            break;
        }
        const auto &go = std::get<GotoAction>(action);
        if (go.target.value >= plan.actions.size()) {
            return std::nullopt;
        }
        walk.transitions.emplace_back(state, go.target);
        state = go.target;
    }
    return walk;
}

[[nodiscard]] std::optional<std::uint32_t>
workflow_import_function_index(std::span<const CoreCapabilityId> imports,
                               CoreCapabilityId capability) {
    const auto it = std::lower_bound(imports.begin(),
                                     imports.end(),
                                     capability,
                                     [](auto lhs, auto rhs) { return lhs.value < rhs.value; });
    if (it == imports.end() || *it != capability) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(it - imports.begin());
}

[[nodiscard]] std::optional<ByteBuffer>
make_workflow_runner_body(const AgentPlan &plan,
                          std::span<const CoreCapabilityId> workflow_imports) {
    auto walk = workflow_initial_transitions(plan);
    if (!walk.has_value()) {
        return std::nullopt;
    }
    if (walk->terminal.value >= plan.actions.size()) {
        return std::nullopt;
    }
    const auto &terminal_action = plan.actions[walk->terminal.value];
    const bool is_capability = std::holds_alternative<CapabilityAction>(terminal_action);

    ByteBuffer body;
    body.u32(1);
    body.u32(1);
    body.byte(kI32); // local 2 = private current state
    append_const(body, plan.initial.value);
    append_indexed_op(body, kOpLocalSet, 2);

    for (const auto &[source, target] : walk->transitions) {
        append_indexed_op(body, kOpLocalGet, 2);
        append_const(body, source.value);
        body.byte(kOpI32Eq);
        body.byte(kOpI32Eqz);
        body.byte(kOpIf);
        body.byte(kEmptyBlock);
        body.byte(kOpUnreachable);
        body.byte(kOpEnd);

        append_const(body, target.value);
        append_indexed_op(body, kOpLocalSet, 2);
        append_indexed_op(body, kOpGlobalGet, kWorkflowGlobalTransitionCount);
        append_const(body, 1);
        body.byte(kOpI32Add);
        append_indexed_op(body, kOpGlobalSet, kWorkflowGlobalTransitionCount);
    }

    append_indexed_op(body, kOpLocalGet, 2);
    append_const(body, walk->terminal.value);
    body.byte(kOpI32Eq);
    body.byte(kOpI32Eqz);
    body.byte(kOpIf);
    body.byte(kEmptyBlock);
    body.byte(kOpUnreachable);
    body.byte(kOpEnd);

    if (!is_capability) {
        // Identity terminal: return (OK, input ptr, input len). The runner never
        // touches the event buffer; the scheduler is the sole event writer.
        append_const(body, AHFL_CAP_OK);
        append_indexed_op(body, kOpLocalGet, 0);
        append_indexed_op(body, kOpLocalGet, 1);
        body.byte(kOpEnd);
        return body;
    }

    // Capability terminal: call the ahfl_cap import and forward its raw
    // (status, ptr, len) to the scheduler WITHOUT interpreting or writing events.
    // The module function index is the capability's ordinal in the workflow-level
    // sorted-unique import table (imports occupy the low function indices).
    const auto &capability = std::get<CapabilityAction>(terminal_action).capability;
    const auto import_index = workflow_import_function_index(workflow_imports, capability);
    if (!import_index.has_value()) {
        return std::nullopt;
    }
    append_indexed_op(body, kOpLocalGet, 0);
    append_indexed_op(body, kOpLocalGet, 1);
    append_indexed_op(body, kOpCall, *import_index);
    // Multi-value results (status, ptr, len) are already on the stack in order;
    // return them verbatim to the scheduler.
    body.byte(kOpEnd);
    return body;
}

[[nodiscard]] std::optional<std::uint32_t>
workflow_node_ptr_local(CoreWorkflowNodeId node) {
    if (node.value > (std::numeric_limits<std::uint32_t>::max() - 2u) / 2u) {
        return std::nullopt;
    }
    return 2u + node.value * 2u;
}

[[nodiscard]] std::optional<std::uint32_t>
workflow_node_len_local(CoreWorkflowNodeId node) {
    const auto ptr = workflow_node_ptr_local(node);
    if (!ptr.has_value() || *ptr == std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    return *ptr + 1u;
}

[[nodiscard]] bool append_workflow_source(ByteBuffer &body,
                                          const WorkflowFrameSource &source) {
    if (source.kind == WorkflowFrameSourceKind::Input) {
        append_indexed_op(body, kOpLocalGet, 0);
        append_indexed_op(body, kOpLocalGet, 1);
        return true;
    }
    const auto ptr = workflow_node_ptr_local(source.node);
    const auto len = workflow_node_len_local(source.node);
    if (!ptr.has_value() || !len.has_value()) {
        return false;
    }
    append_indexed_op(body, kOpLocalGet, *ptr);
    append_indexed_op(body, kOpLocalGet, *len);
    return true;
}

// Emit the SOLE node-event write for one completed node (P0-1: runners never
// write events). Writes the full 40-byte record body FIRST, then the caller
// publishes the incremented event_count. `record_addr` = records_base +
// schedule_pos*40. tag 0 (identity) zeroes capability/source_symbol/
// invocation_ordinal and sets status = OK; tag 1 (capability) carries the node's
// capability identity. All pad and reserved bytes are explicitly zeroed.
void append_event_record_write(ByteBuffer &body,
                               const WorkflowNodePlan &node,
                               std::uint32_t status_local) {
    const std::uint32_t record_addr = kEventRecordsBase + node.schedule_pos * kEventRecordBytes;
    const std::uint8_t tag = node.has_capability ? kEventTagCapability : kEventTagIdentity;
    // [0..3]: tag u8 in byte 0, pad[1..3] == 0 (one aligned 4-byte store).
    append_i32_store_const(body, record_addr + 0u, static_cast<std::uint32_t>(tag));
    // [4..7]: workflow_node_id.
    append_i32_store_const(body, record_addr + 4u, node.node.value);
    // [8..11]: schedule_pos.
    append_i32_store_const(body, record_addr + 8u, node.schedule_pos);
    // [12..15]: capability (0 for identity).
    append_i32_store_const(body, record_addr + 12u,
                           node.has_capability ? node.capability.value : 0u);
    // [16..23]: source_symbol u64 (0 for identity). A capability source SymbolId
    // is a non-negative value bounded by the existing E2 host-ABI contract
    // (build_agent_plan rejects SymbolId > UINT32_MAX), so it is always < 2^63
    // and the canonical ByteBuffer::s64 signed-LEB128 emitter needs no special
    // sign extension here.
    if (node.has_capability) {
        append_const(body, record_addr + 16u);
        body.byte(kOpI64Const);
        body.s64(static_cast<std::int64_t>(node.source_symbol));
        body.byte(kOpI64Store);
        body.u32(3u);
        body.u32(0u);
    } else {
        append_i64_store_zero(body, record_addr + 16u);
    }
    // [24..31]: invocation_ordinal (statically 0 today; the manifest carries none).
    append_i64_store_zero(body, record_addr + 24u);
    // [32..35]: status (address first, then the node's completion status local).
    append_const(body, record_addr + 32u);
    append_indexed_op(body, kOpLocalGet, status_local);
    body.byte(kOpI32Store);
    body.u32(2u);
    body.u32(0u);
    // [36..39]: reserved == 0.
    append_i32_store_const(body, record_addr + 36u, 0u);
}

[[nodiscard]] bool append_workflow_schedule(ByteBuffer &body,
                                            const WorkflowPlan &plan,
                                            const WorkflowFunctionTable &functions,
                                            std::uint32_t status_local) {
    const bool capability_workflow = !plan.imports.empty();
    append_const(body, 0);
    append_indexed_op(body, kOpGlobalSet, kWorkflowGlobalTransitionCount);
    append_const(body, 0);
    append_indexed_op(body, kOpGlobalSet, kWorkflowGlobalCompletedCount);

    if (capability_workflow) {
        // Reset the node-event header: event_count = 0 and pad[4..7] = 0. The
        // latch first-instruction gate (in run2) has already run before this.
        append_i32_store_const(body, kEventLogBase + 0u, 0u);
        append_i32_store_const(body, kEventLogBase + 4u, 0u);
    }

    for (const auto node_id : plan.schedule) {
        if (node_id.value >= plan.nodes.size()) {
            return false;
        }
        const auto &node = plan.nodes[node_id.value];
        const auto runner = workflow_runner_index(plan, node.target_instance);
        const auto ptr_local = workflow_node_ptr_local(node_id);
        const auto len_local = workflow_node_len_local(node_id);
        if (!runner.has_value() || !ptr_local.has_value() || !len_local.has_value() ||
            !append_workflow_source(body, node.input)) {
            return false;
        }
        append_indexed_op(body, kOpCall, functions.runner(*runner));
        // Multi-value results are (status, ptr, len); pop in reverse order.
        append_indexed_op(body, kOpLocalSet, *len_local);
        append_indexed_op(body, kOpLocalSet, *ptr_local);
        append_indexed_op(body, kOpLocalSet, status_local);

        if (!capability_workflow) {
            // Identity workflow (unchanged bytes): any non-OK traps.
            append_indexed_op(body, kOpLocalGet, status_local);
            append_const(body, AHFL_CAP_OK);
            body.byte(kOpI32Eq);
            body.byte(kOpI32Eqz);
            body.byte(kOpIf);
            body.byte(kEmptyBlock);
            body.byte(kOpUnreachable);
            body.byte(kOpEnd);
            append_indexed_op(body, kOpGlobalGet, kWorkflowGlobalCompletedCount);
            append_const(body, 1);
            body.byte(kOpI32Add);
            append_indexed_op(body, kOpGlobalSet, kWorkflowGlobalCompletedCount);
            continue;
        }

        // Capability-workflow status dispatch, normalized to the E2 `ahfl_cap`
        // postcondition (P0-1 / L2 / L3 / L4 + scheduler P0s).
        // PENDING: only a null result_ptr is a legal suspend -> set the latch and
        // return (PENDING,0,0). A PENDING with a non-null ptr is malformed ->
        // ERROR and does NOT latch.
        append_indexed_op(body, kOpLocalGet, status_local);
        append_const(body, AHFL_CAP_PENDING);
        body.byte(kOpI32Eq);
        body.byte(kOpIf);
        body.byte(kEmptyBlock);
        append_indexed_op(body, kOpLocalGet, *ptr_local);
        body.byte(kOpI32Eqz);
        body.byte(kOpIf);
        body.byte(kEmptyBlock);
        append_const(body, 1);
        append_indexed_op(body, kOpGlobalSet, kWorkflowGlobalPendingLatched);
        append_const(body, AHFL_CAP_PENDING);
        append_const(body, 0);
        append_const(body, 0);
        body.byte(kOpReturn);
        body.byte(kOpEnd);
        // PENDING with a non-null ptr is malformed.
        append_error_return(body);
        body.byte(kOpEnd);
        // Not PENDING. A completion is OK ONLY when status==OK AND ptr!=0 AND
        // len!=0; every other case (ERROR, unknown, OK-empty) is (ERROR,0,0).
        append_indexed_op(body, kOpLocalGet, status_local);
        append_const(body, AHFL_CAP_OK);
        body.byte(kOpI32Eq);
        body.byte(kOpI32Eqz);
        body.byte(kOpIf);
        body.byte(kEmptyBlock);
        append_error_return(body);
        body.byte(kOpEnd);
        append_indexed_op(body, kOpLocalGet, *ptr_local);
        body.byte(kOpI32Eqz);
        append_indexed_op(body, kOpLocalGet, *len_local);
        body.byte(kOpI32Eqz);
        body.byte(kOpI32Or);
        body.byte(kOpIf);
        body.byte(kEmptyBlock);
        append_error_return(body); // OK-empty (null ptr or zero len) is ERROR.
        body.byte(kOpEnd);
        // Defensive event_count bound (§4.4): before writing the record verify the
        // runtime header event_count == this node's schedule_pos. `completed_count`
        // (a mutable export the host could tamper) is NOT trusted as the
        // coordinate. schedule_pos < node_count holds by construction (the region
        // is statically sized to node_count). A mismatch returns (ERROR,0,0) with
        // no body/count write.
        append_const(body, kEventLogBase + 0u);
        body.byte(kOpI32Load);
        body.u32(2u);
        body.u32(0u);
        append_const(body, node.schedule_pos);
        body.byte(kOpI32Eq);
        body.byte(kOpI32Eqz);
        body.byte(kOpIf);
        body.byte(kEmptyBlock);
        append_error_return(body);
        body.byte(kOpEnd);
        // OK: write the single node-event record (full body first), then publish
        // event_count = schedule_pos + 1 AFTER the full 40-byte body store (so the
        // host never reads a partial record), and bump completed_count.
        append_event_record_write(body, node, status_local);
        append_const(body, kEventLogBase + 0u);
        append_const(body, node.schedule_pos + 1u);
        body.byte(kOpI32Store);
        body.u32(2u);
        body.u32(0u);
        append_indexed_op(body, kOpGlobalGet, kWorkflowGlobalCompletedCount);
        append_const(body, 1);
        body.byte(kOpI32Add);
        append_indexed_op(body, kOpGlobalSet, kWorkflowGlobalCompletedCount);
    }
    return true;
}

[[nodiscard]] std::optional<ByteBuffer>
make_workflow_run2_body(const WorkflowPlan &plan,
                        const WorkflowFunctionTable &functions) {
    if (plan.nodes.size() >
        (static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) - 1u) / 2u) {
        return std::nullopt;
    }
    const auto node_locals = static_cast<std::uint32_t>(plan.nodes.size()) * 2u;
    const auto status_local = 2u + node_locals;

    ByteBuffer body;
    body.u32(1);
    body.u32(node_locals + 1u);
    body.byte(kI32); // node (ptr,len) pairs followed by one status scratch

    if (!plan.imports.empty()) {
        // L4: a suspended capability-workflow instance cannot accept another
        // input frame. This latch check is the FIRST instruction of run2, before
        // the schedule resets the event header or re-runs any import.
        append_indexed_op(body, kOpGlobalGet, kWorkflowGlobalPendingLatched);
        body.byte(kOpIf);
        body.byte(kEmptyBlock);
        body.byte(kOpUnreachable);
        body.byte(kOpEnd);
    }

    if (!append_workflow_schedule(body, plan, functions, status_local)) {
        return std::nullopt;
    }
    append_const(body, AHFL_CAP_OK);
    if (!append_workflow_source(body, plan.output)) {
        return std::nullopt;
    }
    body.byte(kOpEnd);
    return body;
}

[[nodiscard]] ByteBuffer make_workflow_run_body(const WorkflowPlan &plan,
                                                const WorkflowFunctionTable &functions) {
    ByteBuffer body;
    if (!plan.imports.empty()) {
        // Legacy run is pointer-only; the v1 (ptr,len) ABI cannot carry a status,
        // so a capability-workflow's run traps before any state mutation, input
        // read, or capability effect (a PENDING must never leak through run).
        body.u32(0);
        body.byte(kOpUnreachable);
        body.byte(kOpEnd);
        return body;
    }
    body.u32(1);
    body.u32(3);
    body.byte(kI32); // locals 2=status, 3=ptr, 4=len
    append_indexed_op(body, kOpLocalGet, 0);
    append_indexed_op(body, kOpLocalGet, 1);
    append_indexed_op(body, kOpCall, functions.run2());
    append_indexed_op(body, kOpLocalSet, 4);
    append_indexed_op(body, kOpLocalSet, 3);
    append_indexed_op(body, kOpLocalSet, 2);
    append_indexed_op(body, kOpLocalGet, 2);
    append_const(body, AHFL_CAP_OK);
    body.byte(kOpI32Eq);
    body.byte(kOpI32Eqz);
    body.byte(kOpIf);
    body.byte(kEmptyBlock);
    body.byte(kOpUnreachable);
    body.byte(kOpEnd);
    append_indexed_op(body, kOpLocalGet, 3);
    body.byte(kOpEnd);
    return body;
}

[[nodiscard]] std::optional<std::vector<std::uint8_t>>
encode_workflow_module(const CoreProgram &program,
                       const WorkflowPlan &plan,
                       std::span<const std::uint8_t> wire_schema_payload,
                       CoreWasmCodegenResult &result) {
    if (plan.packaged_instances.size() >
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max() - 6u) ||
        plan.agent_plans.size() != plan.packaged_instances.size() ||
        plan.nodes.size() >=
            static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        add_diag(result,
                 core_wasm_diag::kBinaryOverflow,
                 "workflow WASM function/index domain exceeds the wasm32 limit");
        return std::nullopt;
    }
    std::uint64_t aggregate_transitions = 0;
    for (const auto node_id : plan.schedule) {
        if (node_id.value >= plan.nodes.size()) {
            return std::nullopt;
        }
        const auto runner = workflow_runner_index(
            plan, plan.nodes[node_id.value].target_instance);
        if (!runner.has_value() || *runner >= plan.agent_plans.size()) {
            return std::nullopt;
        }
        const auto transitions = workflow_initial_transitions(plan.agent_plans[*runner]);
        if (!transitions.has_value()) {
            return std::nullopt;
        }
        aggregate_transitions += transitions->transitions.size();
        if (aggregate_transitions >
            static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
            add_diag(result,
                     core_wasm_diag::kBinaryOverflow,
                     "workflow aggregate transition count exceeds the wasm32 domain");
            return std::nullopt;
        }
    }

    const bool capability_workflow = !plan.imports.empty();
    const auto import_count =
        capability_workflow ? static_cast<std::uint32_t>(plan.imports.size()) : 0u;

    // RFC 0026 E4-B2-C two-phase memory sizing (seam §4.4/§5.2). PHASE 1 checked
    // wasm32 arithmetic -> BINARY_OVERFLOW; PHASE 2 capacity vs the fixed 64 KiB
    // page -> RESOURCE_EXHAUSTED. Identity workflows keep heap_base = 1024.
    std::uint32_t heap_base = kEventLogBase;
    if (capability_workflow) {
        bool overflow_is_binary = false;
        auto layout = compute_event_layout(plan.nodes.size(), overflow_is_binary);
        if (!layout.has_value()) {
            if (overflow_is_binary) {
                add_diag(result,
                         core_wasm_diag::kBinaryOverflow,
                         "capability-workflow node-event region size overflows the "
                         "wasm32 arithmetic domain");
            } else {
                add_diag(result,
                         core_wasm_diag::kResourceExhausted,
                         "capability-workflow node-event region and heap exceed the "
                         "fixed 64 KiB linear-memory page");
            }
            return std::nullopt;
        }
        heap_base = layout->heap_base;
    }

    const WorkflowFunctionTable functions{
        import_count, static_cast<std::uint32_t>(plan.packaged_instances.size())};
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

    // RFC 0026 E4-B2-C: capability imports occupy the low function indices. The
    // import module name / field spelling mirror the E2 agent contract exactly.
    if (capability_workflow) {
        ByteBuffer imports;
        imports.u32(import_count);
        for (const auto id : plan.imports) {
            if (id.value >= program.capabilities.size()) {
                return std::nullopt;
            }
            const auto &symbol = program.capabilities[id.value].symbol_ref;
            // build_agent_plan already enforces the existing E2 host-ABI gate on
            // the capability SymbolId (<= UINT32_MAX); the encoder only defends
            // presence + import-name length here.
            if (!symbol.id.has_value()) {
                return std::nullopt;
            }
            if (!imports.name("ahfl_cap") ||
                !imports.name("cap_" + std::to_string(*symbol.id))) {
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
    functions_section.u32(functions.runner_count + 6u);
    functions_section.u32(kTypeI32ToI32);
    functions_section.u32(kTypeTwoI32ToVoid);
    functions_section.u32(kTypeNoArgsI32);
    functions_section.u32(kTypeNoArgsI32);
    for (std::uint32_t i = 0; i < functions.runner_count; ++i) {
        functions_section.u32(kTypeCapabilityTuple);
    }
    functions_section.u32(kTypeTwoI32ToI32);
    functions_section.u32(kTypeCapabilityTuple);
    if (!append_section(module, kSectionFunction, functions_section)) {
        return std::nullopt;
    }

    ByteBuffer memories;
    memories.u32(1); // one memory
    memories.byte(0); // limits flags: no declared maximum
    memories.u32(kCoreWasmFixedLinearMemoryMinPages);
    if (!append_section(module, kSectionMemory, memories)) {
        return std::nullopt;
    }

    // Globals: a capability workflow adds the private pending_latched flag as a
    // 6th global (cap-lane only). Identity workflows keep the 5-global section.
    ByteBuffer globals;
    globals.u32(capability_workflow ? 6u : 5u);
    append_global(globals, true, 0);
    append_global(globals, false, 1);
    append_global(globals, true, heap_base);
    append_global(globals, false, static_cast<std::uint32_t>(plan.nodes.size()));
    append_global(globals, true, 0);
    if (capability_workflow) {
        append_global(globals, true, 0); // kWorkflowGlobalPendingLatched
    }
    if (!append_section(module, kSectionGlobal, globals)) {
        return std::nullopt;
    }

    ByteBuffer exports;
    exports.u32(11);
    const bool exports_ok =
        append_export(exports, "memory", kExportMemory, 0) &&
        append_export(exports, "alloc", kExportFunction, functions.alloc()) &&
        append_export(exports, "dealloc", kExportFunction, functions.dealloc()) &&
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
                      kWorkflowGlobalTransitionCount) &&
        append_export(exports,
                      "ahfl_abi_version",
                      kExportGlobal,
                      kWorkflowGlobalAbiVersion) &&
        append_export(exports,
                      "workflow_node_count",
                      kExportGlobal,
                      kWorkflowGlobalNodeCount) &&
        append_export(exports,
                      "workflow_completed_count",
                      kExportGlobal,
                      kWorkflowGlobalCompletedCount);
    if (!exports_ok || !append_section(module, kSectionExport, exports)) {
        return std::nullopt;
    }

    ByteBuffer code;
    code.u32(functions.runner_count + 6u);
    // A capability workflow uses the checked-alloc body (returns 0 on capacity
    // exhaustion, never advancing heap_next); identity workflows keep the shared
    // unchecked bump body byte-for-byte.
    const auto alloc = capability_workflow
                           ? make_checked_alloc_body(kWorkflowGlobalHeapNext)
                           : make_alloc_body(kWorkflowGlobalHeapNext);
    const auto dealloc = make_dealloc_body();
    const auto current = make_trapping_i32_body();
    const auto step = make_trapping_i32_body();
    if (!code.sized(alloc) || !code.sized(dealloc) || !code.sized(current) ||
        !code.sized(step)) {
        return std::nullopt;
    }
    for (const auto &agent_plan : plan.agent_plans) {
        auto runner = make_workflow_runner_body(agent_plan, plan.imports);
        if (!runner.has_value() || !code.sized(*runner)) {
            return std::nullopt;
        }
    }
    const auto run = make_workflow_run_body(plan, functions);
    auto run2 = make_workflow_run2_body(plan, functions);
    if (!run2.has_value() || !code.sized(run) || !code.sized(*run2) ||
        !append_section(module, kSectionCode, code)) {
        return std::nullopt;
    }

    // RFC 0026 E4-B2-C: a capability workflow ends with the exec-manifest custom
    // section (AHFLXM) EXACTLY ONCE, IMMEDIATELY BEFORE the wire-schema custom
    // section (AHFLWS), which remains the module's FINAL section at EOF.
    if (capability_workflow) {
        auto manifest = encode_exec_manifest(plan);
        if (!manifest.has_value()) {
            return std::nullopt;
        }
        ByteBuffer manifest_custom;
        if (!manifest_custom.name(kExecManifestSectionName)) {
            return std::nullopt;
        }
        manifest_custom.raw_span(*manifest);
        if (!append_section(module, kSectionCustom, manifest_custom)) {
            return std::nullopt;
        }
        if (wire_schema_payload.empty()) {
            return std::nullopt;
        }
        ByteBuffer schema_custom;
        if (!schema_custom.name(kWireSchemaSectionName)) {
            return std::nullopt;
        }
        schema_custom.raw_span(wire_schema_payload);
        if (!append_section(module, kSectionCustom, schema_custom)) {
            return std::nullopt;
        }
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
        // RFC 0026 E4-B2-C: a capability workflow projects the deterministic wire
        // schema for exactly its reachable capability imports (same authority the
        // E2 agent path uses). Identity workflows have no imports and skip it.
        std::vector<std::uint8_t> wire_schema_payload;
        if (!plan->imports.empty()) {
            auto projection = ir::core::project_core_wire_schema(program, plan->imports);
            if (!projection.ok()) {
                std::string message =
                    "reachable capability import ABI is not wire-transportable";
                ir::SourceRangeOpt range;
                if (!projection.diagnostics.empty()) {
                    const auto &first = projection.diagnostics.front();
                    message += " (" + first.code + ")";
                    range = first.source_range;
                }
                add_diag(result, core_wasm_diag::kInvalidCapabilityAbi, std::move(message),
                         range);
                return result;
            }
            auto encoded = ir::core::encode_core_wire_schema_table(*projection.table);
            if (!encoded.ok()) {
                std::string message =
                    "wire-schema section payload exceeds the encoding domain";
                ir::SourceRangeOpt range;
                if (!encoded.diagnostics.empty()) {
                    const auto &first = encoded.diagnostics.front();
                    message += " (" + first.code + ")";
                    range = first.source_range;
                }
                add_diag(result, core_wasm_diag::kBinaryOverflow, std::move(message), range);
                return result;
            }
            wire_schema_payload = std::move(*encoded.bytes);
        }
        const auto diag_count_before = result.diagnostics.size();
        auto bytes = encode_workflow_module(program, *plan, wire_schema_payload, result);
        if (!bytes.has_value()) {
            // encode_workflow_module raises its own precise diagnostic (BINARY_
            // OVERFLOW / RESOURCE_EXHAUSTED); only add the generic fallback if it
            // failed silently.
            if (result.diagnostics.size() == diag_count_before) {
                add_diag(result,
                         core_wasm_diag::kBinaryOverflow,
                         "workflow WASM section, local, function, or index exceeds the "
                         "wasm32 encoding domain");
            }
            return result;
        }
        CoreWasmArtifact artifact;
        artifact.bytes = std::move(*bytes);
        artifact.entry = target.entry;
        artifact.packaged_agent_instances = plan->packaged_instances;
        for (const auto id : plan->imports) {
            const auto &symbol = program.capabilities[id.value].symbol_ref;
            if (symbol.id.has_value()) {
                artifact.imports.push_back("ahfl_cap.cap_" + std::to_string(*symbol.id));
            }
        }
        artifact.exports = {"memory",
                            "alloc",
                            "dealloc",
                            "run",
                            "run2",
                            "step",
                            "current_state",
                            "transition_count",
                            "ahfl_abi_version",
                            "workflow_node_count",
                            "workflow_completed_count"};
        result.artifact = std::move(artifact);
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

    // RFC 0026 E4-B1: project the deterministic logical wire schema for exactly
    // the reachable capability imports (already sorted/unique in the plan), then
    // encode it to its canonical section payload. E1 no-import agents skip this
    // entirely and stay byte-identical. Any projection or encode failure fails
    // closed: no partial artifact, no silent section drop.
    std::vector<std::uint8_t> wire_schema_payload;
    if (!plan->imports.empty()) {
        auto projection = ir::core::project_core_wire_schema(program, plan->imports);
        if (!projection.ok()) {
            // Fail-closed seam: never assume a diagnostic is present. A future or
            // defensive empty-diagnostics result must still reject with a fixed
            // code, not deref an empty vector.
            std::string message =
                "reachable capability import ABI is not wire-transportable";
            ir::SourceRangeOpt range;
            if (!projection.diagnostics.empty()) {
                const auto &first = projection.diagnostics.front();
                message += " (" + first.code + ")";
                range = first.source_range;
            }
            add_diag(result,
                     core_wasm_diag::kInvalidCapabilityAbi,
                     std::move(message),
                     range);
            return result;
        }
        auto encoded = ir::core::encode_core_wire_schema_table(*projection.table);
        if (!encoded.ok()) {
            std::string message =
                "wire-schema section payload exceeds the encoding domain";
            ir::SourceRangeOpt range;
            if (!encoded.diagnostics.empty()) {
                const auto &first = encoded.diagnostics.front();
                message += " (" + first.code + ")";
                range = first.source_range;
            }
            add_diag(result,
                     core_wasm_diag::kBinaryOverflow,
                     std::move(message),
                     range);
            return result;
        }
        wire_schema_payload = std::move(*encoded.bytes);
    }

    auto bytes = encode_module(program, *plan, wire_schema_payload);
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
