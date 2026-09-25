#include "compiler/backends/infra/core_wasm_codegen.hpp"

#include "ahfl/base/support/overloaded.hpp"
#include "ahfl/compiler/ir/core_recursion.hpp"
#include "ahfl/compiler/ir/core_verify.hpp"
#include "ahfl/compiler/ir/core_wasm_abi_constants.hpp"
#include "ahfl/compiler/ir/core_wire_schema.hpp"
#include "ahfl/runtime/ahfl_host.h"
#include "compiler/backends/infra/detail/wasm_byte_buffer.hpp"

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
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace ahfl::backends {

namespace {

using detail::ByteBuffer;
using ir::core::CoreAgentDecl;
using ir::core::CoreAgentId;
using ir::core::CoreAgentInstance;
using ir::core::CoreBinaryExpr;
using ir::core::CoreBinaryOp;
using ir::core::CoreBodyStorage;
using ir::core::CoreCallExpr;
using ir::core::CoreCallClosureExpr;
using ir::core::CoreClosureExpr;
using ir::core::CoreFnId;
using ir::core::CoreFnDecl;
using ir::core::CoreFnInstance;
using ir::core::CoreBindingPat;
using ir::core::CoreCapabilityCallStmt;
using ir::core::CoreCallStmt;
using ir::core::CoreCapabilityDecl;
using ir::core::CoreCapabilityId;
using ir::core::CoreCoerceExpr;
using ir::core::CoreCoercionOp;
using ir::core::CoreCoercionOpKind;
using ir::core::CoreCoercionPlanId;
using ir::core::CoreCoercionPlanNode;
using ir::core::CoreCollectionExpr;
using ir::core::CoreCollectionOpKind;
using ir::core::CoreConstructArg;
using ir::core::CoreConstructExpr;
using ir::core::CoreExpr;
using ir::core::CoreExprId;
using ir::core::CoreFlowDecl;
using ir::core::CoreFlowState;
using ir::core::CoreGotoStmt;
using ir::core::CoreIfStmt;
using ir::core::CoreInstanceId;
using ir::core::CoreIntRangePat;
using ir::core::CoreLayoutId;
using ir::core::CoreLetStmt;
using ir::core::CoreLiteralExpr;
using ir::core::CoreLiteralKind;
using ir::core::CoreLiteralPat;
using ir::core::CoreMatchArm;
using ir::core::CoreMatchStmt;
using ir::core::CoreOrPat;
using ir::core::CorePathExpr;
using ir::core::CorePattern;
using ir::core::CorePatternBinding;
using ir::core::CorePatternId;
using ir::core::CoreProgram;
using ir::core::CoreQualifiedExpr;
using ir::core::CoreRegion;
using ir::core::CoreReturnStmt;
using ir::core::CoreStateId;
using ir::core::CoreStmt;
using ir::core::CoreStoreStmt;
using ir::core::CoreTrapStmt;
using ir::core::CoreTuplePat;
using ir::core::CoreTypeDecl;
using ir::core::CoreTypeId;
using ir::core::CoreUnaryExpr;
using ir::core::CoreUnaryOp;
using ir::core::CoreUnsupportedExpr;
using ir::core::CoreValueId;
using ir::core::CoreValueRefExpr;
using ir::core::CoreValueTypeId;
using ir::core::CoreValueTypeNode;
using ir::core::CoreVariantId;
using ir::core::CoreVariantPat;
using ir::core::CoreVariantPatField;
using ir::core::CoreVtBool;
using ir::core::CoreVtInt;
using ir::core::CoreVtNominal;
using ir::core::CoreWildcardPat;
using ir::core::CoreWorkflowDecl;
using ir::core::CoreWorkflowId;
using ir::core::CoreWorkflowNodeId;
using ir::core::CoreYieldStmt;
using ir::core::kCoreWasmFixedLinearMemoryCapacityBytes;
using ir::core::kCoreWasmFixedLinearMemoryMinPages;
using ir::core::kNodeEventHeaderBytes;
using ir::core::kNodeEventLogBase;
using ir::core::kNodeEventRecordBytes;
using ir::core::kNodeEventRecordsBase;
using ir::core::kP6AggregateContextBase;
using ir::core::kP6AggregateContextCapacity;
using ir::core::kP6AggregateInputBase;
using ir::core::kP6AggregateInputCapacity;
using ir::core::kP6AggregateScratchBase;
using ir::core::kP6AggregateScratchCapacity;
using ir::core::kP6CollectionBackingCapacity;

constexpr std::uint8_t kI32 = 0x7f;
constexpr std::uint8_t kI64 = 0x7e;
constexpr std::uint8_t kEmptyBlock = 0x40;
constexpr std::uint8_t kFuncType = 0x60;

constexpr std::uint8_t kSectionCustom = 0;
constexpr std::uint8_t kSectionType = 1;
constexpr std::uint8_t kSectionImport = 2;
constexpr std::uint8_t kSectionFunction = 3;
// RFC 0026 FB-3b (CORE-FNBODY-DESIGN §6.2): exactly one funcref table is
// declared when (and only when) the module constructs a first-class closure.
// The wasm section order is by non-decreasing section id, so Table(4) sits
// uniquely between Function(3) and Memory(5); there is no "either side"
// freedom.
constexpr std::uint8_t kSectionTable = 4;
constexpr std::uint8_t kSectionMemory = 5;
constexpr std::uint8_t kSectionGlobal = 6;
constexpr std::uint8_t kSectionExport = 7;
// Element(9) sits uniquely between Export(7) and Code(10). It carries the
// active funcref initializers table[0..N) -> wasm funcidx.
constexpr std::uint8_t kSectionElement = 9;
constexpr std::uint8_t kSectionCode = 10;
// The funcref reftype encoding (wasm spec reftype space).
constexpr std::uint8_t kFuncRefType = 0x70;

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

// RFC 0026 E4-B2-C node-event record tags. The buffer base / header / record
// size constants live in the public ABI SSOT
// (ahfl/compiler/ir/core_wasm_abi_constants.hpp); only the tag spellings are
// local to the emitter.
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
// RFC 0026 FB-3b (§5.2 / §6.2): an indirect call through the funcref table.
// Encoding is the opcode, the expected typeidx (ULEB), then the tableidx (0;
// the module declares exactly one funcref table).
constexpr std::uint8_t kOpCallIndirect = 0x11;
constexpr std::uint8_t kOpDrop = 0x1a;
constexpr std::uint8_t kOpLocalGet = 0x20;
constexpr std::uint8_t kOpLocalSet = 0x21;
constexpr std::uint8_t kOpLocalTee = 0x22;
constexpr std::uint8_t kOpGlobalGet = 0x23;
constexpr std::uint8_t kOpGlobalSet = 0x24;
constexpr std::uint8_t kOpI32Load = 0x28;
constexpr std::uint8_t kOpI64Load = 0x29;
constexpr std::uint8_t kOpI32Store = 0x36;
constexpr std::uint8_t kOpI64Store = 0x37;
// RFC 0026 P6-4: the natural-alignment exponents a `memarg` carries for the two
// scalar widths P6 loads/stores. Wasm takes log2(alignment); a value at its
// natural alignment lets the engine use the widest access.
constexpr std::uint32_t kAlignI32 = 2;
constexpr std::uint32_t kAlignI64 = 3;
constexpr std::uint8_t kOpI32Const = 0x41;
constexpr std::uint8_t kOpI64Const = 0x42;
constexpr std::uint8_t kOpI32Eqz = 0x45;
constexpr std::uint8_t kOpI32Eq = 0x46;
// RFC 0026 P6 (KR6.6): the scalar ladder opcode set. The wasm numeric
// opcode table orders the integer comparisons as lt_s, lt_u, gt_s, gt_u,
// le_s, le_u, ge_s, ge_u per bit width (i32: 0x48..0x4f, i64: 0x53..0x5a);
// the constants below pin that full table, and emit_binary selects the
// exact signed byte for the operand's P4-D scalar repr.
constexpr std::uint8_t kOpI32Ne = 0x47;
constexpr std::uint8_t kOpI32LtS = 0x48;
constexpr std::uint8_t kOpI32LtU = 0x49;
constexpr std::uint8_t kOpI32GtS = 0x4a;
constexpr std::uint8_t kOpI32GtU = 0x4b;
constexpr std::uint8_t kOpI32LeS = 0x4c;
[[maybe_unused]] constexpr std::uint8_t kOpI32LeU = 0x4d;
constexpr std::uint8_t kOpI32GeS = 0x4e;
[[maybe_unused]] constexpr std::uint8_t kOpI32GeU = 0x4f;
[[maybe_unused]] constexpr std::uint8_t kOpI64Eqz = 0x50;
constexpr std::uint8_t kOpI64Eq = 0x51;
constexpr std::uint8_t kOpI64Ne = 0x52;
constexpr std::uint8_t kOpI64LtS = 0x53;
[[maybe_unused]] constexpr std::uint8_t kOpI64LtU = 0x54;
constexpr std::uint8_t kOpI64GtS = 0x55;
[[maybe_unused]] constexpr std::uint8_t kOpI64GtU = 0x56;
constexpr std::uint8_t kOpI64LeS = 0x57;
[[maybe_unused]] constexpr std::uint8_t kOpI64LeU = 0x58;
constexpr std::uint8_t kOpI64GeS = 0x59;
[[maybe_unused]] constexpr std::uint8_t kOpI64GeU = 0x5a;
constexpr std::uint8_t kOpI32Add = 0x6a;
constexpr std::uint8_t kOpI32Sub = 0x6b;
constexpr std::uint8_t kOpI32Mul = 0x6c;
constexpr std::uint8_t kOpI32DivS = 0x6d;
constexpr std::uint8_t kOpI32RemS = 0x6f;
constexpr std::uint8_t kOpI32And = 0x71;
constexpr std::uint8_t kOpI32Or = 0x72;
constexpr std::uint8_t kOpI64Add = 0x7c;
constexpr std::uint8_t kOpI64Sub = 0x7d;
constexpr std::uint8_t kOpI64Mul = 0x7e;
constexpr std::uint8_t kOpI64DivS = 0x7f;
constexpr std::uint8_t kOpI64RemS = 0x81;
// RFC 0026 P6-6: the integer-width widening. i64.extend_i32_s decodes an i32 in
// the low lane and SIGN-extends it to i64, which is the exact physical effect of
// a `BoundedInt <: Int` widening whose bounded side is the i32 scalar repr.
constexpr std::uint8_t kOpI64ExtendI32S = 0xac;
// RFC 0026 P6-5: a WIDE element index is confined to the wasm32 address domain
// by wrapping to i32. The P4-D container CAPACITY is what proves this is lossless
// (the plan proved `capacity` fits the i32 ladder, and the bounds compare rejects
// any index outside `[0, capacity)` — so no live index bit is discarded).
constexpr std::uint8_t kOpI32WrapI64 = 0xa7;

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
// RFC 0026 P6-2 (KR6.6): compiled per-state handler FUNCTIONS follow the seven
// fixed ABI functions. An agent with no computed handler declares none, so the
// E1-E3 artifacts keep byte-identical function/code section counts.
constexpr std::uint32_t kDefinedHandlerBase = 7;

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
// RFC 0026 P6-2 (KR6.6): a non-final handler whose next state is decided by a
// lowered scalar computation (e.g. `if (a + b > c) goto X else goto Y`). P6-1
// emitted such a body INLINE in `step()`'s dispatch arm; P6-2 compiles it to a
// REAL wasm function of type `() -> i32` (no parameters, the new state id as
// the result). The function body runs the lowered statement region inside a
// `block (result i32)`, updates global current_state + transition_count on
// every goto path and exits the block with `br`, or traps. `targets` are every
// state the body may branch to (the handler may be non-deterministic across
// runs); the graph analysis treats the action as nondeterministic over this
// successor set.
struct ComputedGotoAction {
    // Import-indexed function index of the compiled handler (see FunctionTable).
    std::uint32_t function{0};
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

// RFC 0026 P6-2 (KR6.6): one compiled non-final handler FUNCTION. The body is
// the complete wasm function body (local declarations + a `block (result i32)`
// wrapping the lowered statement region + the terminating `end`), ready for the
// Code section's `sized()` framing. The handler's successor set is NOT stored
// here: it already lives on the `ComputedGotoAction` the state maps to, which is
// the single source the goto-graph analysis reads.
struct CompiledHandler {
    std::vector<std::uint8_t> body;
};

// RFC 0026 FB-1 (CORE-FNBODY-DESIGN §6): forward-declared here so AgentPlan
// can hold a vector of them; the full definition follows the scalar body
// builder (which owns the planning/emission that fills one).
struct CompiledFn;

// FB-3b: one call_indirect expected functype, expressed as flat wasm value-type
// bytes. `params` already includes the leading env i32 and expands every
// closure argument to its two i32 words; `result` is a single word (a closure
// result needs multi-value return and is fail-closed in this slice). Structural
// equality (never name identity) is all wasm call_indirect checks, so the
// per-call-site descriptor and the callee fn's own functype match byte shape.
struct ClosureCallType {
    std::vector<std::uint8_t> params;
    std::uint8_t result{kI32};
    [[nodiscard]] friend bool operator==(const ClosureCallType &,
                                         const ClosureCallType &) noexcept = default;
};

// Stable structural key for a call_indirect functype: the flat param type bytes
// (env + args, closures expanded to two i32) followed by the result byte.
[[nodiscard]] inline std::string
closure_call_type_key(const ClosureCallType &t) {
    std::string key;
    key.reserve(t.params.size() + 1);
    key.append(reinterpret_cast<const char *>(t.params.data()), t.params.size());
    key.push_back(static_cast<char>(t.result));
    return key;
}

struct AgentPlan {
    CoreAgentId agent{};
    CoreStateId initial{};
    std::vector<StateAction> actions;
    std::vector<CoreCapabilityId> imports;
    // KR6.7 (RFC 0026 P7): true when a compiled handler projects raw bytes out
    // of the P4-D input frame (or an input-reached bounded collection), so the
    // run2 boundary does not carry canonical wire-JSON output and canonical
    // observation conformance awaits the P6-7 frame decision.
    bool reads_raw_input_frame{false};
    // RFC 0026 P6-2: compiled handler functions in ascending function-index
    // order. Empty for a pure E1-E3 agent, so its function/code sections keep
    // their byte-identical 7-entry shape.
    std::vector<CompiledHandler> handlers;
    // RFC 0026 FB-1: outlined pure fn bodies reachable from the entry handlers
    // (and from other reachable fns) via direct CoreCallExpr, in the fixed
    // point's deterministic CoreFnId order. Empty for a module with no direct
    // calls, so a zero-fn module is byte-identical to its E1-E3/P6 shape.
    std::vector<CompiledFn> fns;
    // FB-1 fix-forward: when true, an aggregate-returning reachable fn bumps the
    // runtime heap per activation; kGlobalHeapNext is initialized to
    // `construct_heap_base` (relocated above every reserved frame/backing
    // region) and every computed handler resets it on entry. False keeps the
    // legacy initial heap (kNodeEventLogBase) and byte-identical modules.
    bool construct_heap_enabled{false};
    std::uint32_t construct_heap_base{0};
    // RFC 0026 FB-3b (CORE-FNBODY-DESIGN §6.2): the funcref table content, in
    // DENSE table-slot order (slot == vector index). Every entry is the
    // CoreFnId of a reachable fn whose address is taken by a CoreClosureExpr
    // (a lifted lambda or a zero-capture static-fn reference). Empty for a
    // closure-free module, which then omits BOTH the Table(4) and Element(9)
    // sections and stays byte-identical.
    std::vector<CoreFnId> closure_table;
    // FB-3b: the deduplicated call_indirect expected functypes beyond the five
    // fixed ABI types and the per-fn types (which occupy type indices
    // 5 .. 5+fns-1). Index i here is type index (5 + fns.size() + i). Each
    // descriptor is the full functype (params already prefixed by the env i32
    // and with closure args expanded to two i32 words).
    std::vector<ClosureCallType> closure_signatures;
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

enum class WorkflowFrameSourceKind {
    Input,
    NodeOutput
};

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
    // RFC 0026 P6-2: number of compiled per-state handler functions following
    // the seven fixed ABI functions. Zero for a pure E1-E3 agent, so its
    // function/code sections keep their byte-identical shape.
    std::uint32_t handler_count{0};
    [[nodiscard]] std::uint32_t alloc() const noexcept {
        return import_count + kDefinedAlloc;
    }
    [[nodiscard]] std::uint32_t dealloc() const noexcept {
        return import_count + kDefinedDealloc;
    }
    [[nodiscard]] std::uint32_t current_state() const noexcept {
        return import_count + kDefinedCurrentState;
    }
    [[nodiscard]] std::uint32_t is_final() const noexcept {
        return import_count + kDefinedIsFinal;
    }
    [[nodiscard]] std::uint32_t step() const noexcept {
        return import_count + kDefinedStep;
    }
    [[nodiscard]] std::uint32_t run() const noexcept {
        return import_count + kDefinedRun;
    }
    [[nodiscard]] std::uint32_t run2() const noexcept {
        return import_count + kDefinedRun2;
    }
    [[nodiscard]] std::uint32_t handler(std::uint32_t index) const noexcept {
        return import_count + kDefinedHandlerBase + index;
    }
    // RFC 0026 FB-1: outlined fn bodies follow the computed handlers
    // (§6.1: [import+7 .. +7+H) handlers, [import+7+H .. +7+H+F) fns).
    [[nodiscard]] std::uint32_t fn_base(std::uint32_t handler_count) const noexcept {
        return import_count + kDefinedHandlerBase + handler_count;
    }
    [[nodiscard]] std::uint32_t defined_count() const noexcept {
        return kDefinedHandlerBase + handler_count;
    }
};

struct WorkflowFunctionTable {
    // Imported functions occupy the low indices; every defined function index is
    // import_count + base (the same PROJECT rule the agent FunctionTable uses).
    // Identity workflows have import_count == 0, so their indices are unchanged.
    std::uint32_t import_count{0};
    std::uint32_t runner_count{0};
    // RFC 0026 FB-1: outlined fn bodies follow runner/run2 (§6.1 agent ordering
    // discipline). In FB-1 a workflow module's node-input/return regions accept
    // only opaque workflow frames and its packaged agents disallow computed
    // handlers, so no outlined fn is ever reachable from a workflow MODULE
    // (a packaged agent's direct calls are compiled into that agent's own
    // module). The field stays 0 and a workflow module is byte-identical to its
    // E1-E3/E4 shape; the projection exists for index-space symmetry with the
    // agent FunctionTable and the FB-3 closure/indirect slice.
    std::uint32_t fn_count{0};
    [[nodiscard]] std::uint32_t alloc() const noexcept {
        return import_count + 0u;
    }
    [[nodiscard]] std::uint32_t dealloc() const noexcept {
        return import_count + 1u;
    }
    [[nodiscard]] std::uint32_t current_state() const noexcept {
        return import_count + 2u;
    }
    [[nodiscard]] std::uint32_t step() const noexcept {
        return import_count + 3u;
    }
    [[nodiscard]] std::uint32_t runner(std::uint32_t index) const noexcept {
        return import_count + 4u + index;
    }
    [[nodiscard]] std::uint32_t run() const noexcept {
        return import_count + 4u + runner_count;
    }
    [[nodiscard]] std::uint32_t run2() const noexcept {
        return import_count + 5u + runner_count;
    }
    // RFC 0026 FB-1: outlined fn ordinal -> absolute function index. Fn bodies
    // follow runner/run2; fn_count is 0 for a workflow module in FB-1 (see the
    // field note), keeping the module byte-identical.
    [[nodiscard]] std::uint32_t fn(std::uint32_t ordinal) const noexcept {
        return import_count + 6u + runner_count + ordinal;
    }
    [[nodiscard]] std::uint32_t defined_count() const noexcept {
        return 6u + runner_count + fn_count;
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

// RFC 0026 FB-4: invoke `visit` for every STATEMENT in a region and its nested
// if / match regions (iterative, explicit stack). Shared by the effectful-fn
// reachability walk, the in-fn call enumeration, and the import pre-pass.
template <class Fn>
void for_each_region_statement(const ir::core::CoreRegion &root, Fn &&visit) {
    std::vector<const ir::core::CoreRegion *> pending{&root};
    while (!pending.empty()) {
        const ir::core::CoreRegion *region = pending.back();
        pending.pop_back();
        for (const ir::core::CoreStmt &stmt : region->statements) {
            visit(stmt);
            if (const auto *branch = std::get_if<ir::core::CoreIfStmt>(&stmt.node)) {
                if (branch->then_region != nullptr) {
                    pending.push_back(branch->then_region.get());
                }
                if (branch->else_region != nullptr) {
                    pending.push_back(branch->else_region.get());
                }
            } else if (const auto *match =
                           std::get_if<ir::core::CoreMatchStmt>(&stmt.node)) {
                for (const ir::core::CoreMatchArm &arm : match->arms) {
                    if (arm.guard_region != nullptr) {
                        pending.push_back(arm.guard_region.get());
                    }
                    if (arm.body != nullptr) {
                        pending.push_back(arm.body.get());
                    }
                }
                if (match->fallback_region != nullptr) {
                    pending.push_back(match->fallback_region.get());
                }
            }
        }
    }
}

// Flow-gate predicate: does the region contain any match statement? A
// top-level match is caught directly; a match nested under an if is reached by
// branch recursion. Match arm traversal is unnecessary (any arm containing a
// match would itself hold a CoreMatchStmt somewhere already covered).
[[nodiscard]] bool region_contains_match(const ir::core::CoreRegion &region) {
    for (const auto &statement : region.statements) {
        if (std::holds_alternative<ir::core::CoreMatchStmt>(statement.node)) {
            return true;
        }
        if (const auto *branch = std::get_if<ir::core::CoreIfStmt>(&statement.node)) {
            if ((branch->then_region != nullptr && region_contains_match(*branch->then_region)) ||
                (branch->else_region != nullptr && region_contains_match(*branch->else_region))) {
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
    if (let == nullptr || let->expr.value >= flow.storage.exprs.size() ||
        let->result.value >= flow.storage.value_types.size() || let->result.value >= used_values.size()) {
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

    const auto &expr = flow.storage.exprs[let->expr.value];
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
        flow.storage.value_types[let->result.value] != expr.result_type) {
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
                 statements.empty() ? ir::SourceRangeOpt{} : statements.front().source_range);
        return false;
    }
    const auto input = validate_canonical_input_let(
        program, agent, flow, statements[0], used_exprs, used_values, unsupported_code, result);
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
    const auto type = flow.storage.value_types[input->value];
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
    const auto input = validate_canonical_input_let(program,
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
    if (call == nullptr || ret == nullptr || !ret->has_value || ret->value != call->result ||
        call->args.size() != 1 || call->args[0] != *input) {
        add_diag(result,
                 core_wasm_diag::kUnsupportedCapabilityFrame,
                 "KR6.5 E2 capability final is not the canonical opaque forwarding shape",
                 statements[1].source_range);
        return std::nullopt;
    }
    if (call->capability.value >= program.capabilities.size() ||
        call->result.value >= flow.storage.value_types.size() || call->result.value >= used_values.size() ||
        used_values[call->result.value]) {
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
    const auto input_type = flow.storage.value_types[input->value];
    const auto result_type = flow.storage.value_types[call->result.value];
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
    if (!has_finalized_layout(layouts, input_type) || !has_finalized_layout(layouts, result_type)) {
        add_diag(result,
                 core_wasm_diag::kInvalidLayout,
                 "capability boundary type has no finalized P4-D layout",
                 statements[1].source_range);
        return std::nullopt;
    }

    used_values[call->result.value] = true;
    return CapabilityAction{call->capability};
}

// RFC 0026 P6 (KR6.6) — computation codegen scaffold.
//
// The KR6.5 backend emits one ACTION per state (goto / identity / capability)
// and validates those three canonical shapes. KR6.6 adds real per-handler
// computation lowering (scalar expressions, SSA locals, structured control
// flow). The scaffold every P6 slice hangs from:
//
//   * `is_p6_computation_region` is the fail-closed subset gate that separates a
//     P6 computation handler (ANF scalar lets, if/match structure, goto/trap
//     terminators; NO capability effect) from the KR6.5 orchestration shapes.
//   * `P6ComputationHandlerBuilder` is the per-handler body builder: its own
//     ByteBuffer plus a CoreValueId -> wasm-local table. Expression and statement
//     lowering are COMPILE-TIME-EXHAUSTIVE std::variant visitors over all nine
//     CoreExprNode / CoreStmtNode arms — no catch-all, so a tenth IR node is a
//     compile error here, not a silent miscompile.
//
// P6-1 landed the scalar stack machine (Literal/ValueRef/Unary/Binary + computed
// goto), compiled INLINE in step(). P6-2 promotes each computed handler to its
// own `() -> i32` wasm FUNCTION and lowers structured `if` to wasm block/if/else
// with a goto = `br` to the handler block. P6-3 (this slice) lowers `match`:
// arms become a nested block+br_if chain, guards are ANDed Bools gated by br_if,
// and the fallback is the instruction after the last arm. Expression kinds not
// yet landed (path/qualified/construct/coerce) still fail closed per-arm, so no
// partial artifact is ever returned and the E1-E3 byte paths stay untouched.

// Does this region belong to the P6 subset? ANF scalar lets, structured if, and
// goto/trap terminators, with no capability effect. A `CoreYieldStmt` here means
// the region is an ordinary flow region — a yield outside a match arm is illegal
// (`core_verify.cpp` kYieldOutsideMatchArm), so it is NOT in the subset.
//
// A match is admitted STRUCTURALLY (see the CoreMatchStmt arm): the gate does not
// descend into its arms / guards / fallback, because those regions are consumed
// by the enclosing match and legitimately END in a yield (the guard's Bool, an
// expression arm's value, a statement arm's unit). Their role is validated by
// `plan_match` -> `plan_match_region`, which is the single authority for the
// match-consumed role: it owns the "yield must be the final statement" rule and
// rejects anything else by delegating to `plan_statement`. So the fail-closed
// property is preserved — a match whose consumed region holds an out-of-subset
// statement is rejected by the planner, one region deeper than this cheap
// pre-filter, and never becomes a partial artifact.
[[nodiscard]] bool is_p6_subset_region(const CoreRegion &region) {
    for (const CoreStmt &statement : region.statements) {
        const bool in_subset = std::visit(
            Overloaded{
                [](const CoreLetStmt &) { return true; },
                [](const CoreGotoStmt &) { return true; },
                [](const CoreTrapStmt &) { return true; },
                [](const CoreYieldStmt &) { return false; },
                [](const CoreIfStmt &s) {
                    return (s.then_region == nullptr || is_p6_subset_region(*s.then_region)) &&
                           (s.else_region == nullptr || is_p6_subset_region(*s.else_region));
                },
                // A match is structurally in-subset; its consumed regions are
                // validated by `plan_match_region` (see the header comment).
                [](const CoreMatchStmt &) { return true; },
                // RFC 0026 P6-4: a context store (`ctx.field = v`) is now a
                // computation statement lowered to a memory store. Capability and
                // return stay on the KR6.5 orchestration lane.
                [](const CoreStoreStmt &) { return true; },
                [](const CoreCapabilityCallStmt &) { return false; },
                [](const CoreReturnStmt &) { return false; },
                // RFC 0026 FB-4: an ordered effectful fn call is a straight-line
                // statement (like a let); it is in the scalar subset and is
                // emitted as a `call` to an outlined effect fn.
                [](const CoreCallStmt &) { return true; },
            },
            statement.node);
        if (!in_subset) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool is_p6_computation_region(const CoreRegion &region) {
    return is_p6_subset_region(region);
}

// The scalar kind of a value type for the P6 ladder, with the physical repr
// read from the P4-D layout table (design §2.1: codegen never recomputes a
// repr). Bool and narrow Int share the i32 repr but stay distinct KINDS so the
// boolean-only operators (! && ||) cannot be applied to an integer. `Index` is a
// TAG-ONLY enum's value kind (design `CoreLayoutEnum` with every variant
// payload size 0): its scratch representation is the i32 discriminant, exactly
// like a bounded Int. It is distinct from IntI32 so an arithmetic operator can
// never be applied to a discriminant — only `match` consumes it.
enum class P6ScalarKind {
    Bool,
    IntI32,
    IntI64,
    Index,
    // RFC 0026 P6-4: an AGGREGATE value (a struct, or an enum with a payload)
    // whose whole runtime representation is an i32 linear-memory ADDRESS of its
    // P4-D shaped bytes. Distinct from IntI32 so no arithmetic/compare operator
    // can consume an address; only a field projection, a store, or a payload
    // pattern touches one.
    Ptr,
    // RFC 0026 P6-5: a BOUNDED COLLECTION value. Its whole runtime
    // representation is the i32 linear-memory ADDRESS of its INLINE `(ptr,len)`
    // header (P4-D `CoreLayoutContainer`, size 8/align 4), exactly analogous to
    // an aggregate's address — the backing elements live at `ptr`, one `stride`
    // apart. Distinct from BOTH IntI32 and Ptr so no arithmetic / aggregate op
    // can consume a handle: only a collection op (len / element read / element
    // write) touches one, and every backing fact (element layout, stride,
    // capacity, Map value offset, checked backing size) comes from that
    // `CoreLayoutContainer`, never re-derived.
    Collection,
    // RFC 0026 FB-3b (CORE-FNBODY-DESIGN §3.1.1 D-FNREP): a first-class CALLABLE
    // value. Its whole runtime representation is the eight-byte
    // `(func_index:i32 @0, env_ptr:i32 @4)` word pair (P4-D
    // `CoreLayoutClosure`): an i32 dense funcref-table slot plus the i32 address
    // of the captured environment aggregate (0 for a zero-capture closure). It
    // is TWO i32 words, so it can never flow through a one-word local or a
    // scalar/aggregate slot; only `CoreClosureExpr` (constructor),
    // `CoreCallClosureExpr` (indirect call), and a callable parameter / binding
    // touch it.
    Closure
};

// The location in the scrutinee a pattern is tested at (RFC 0026 P6-4). The root
// site is the scrutinee itself — a scalar in a local (`in_memory == false`), or
// address 0 of an aggregate's address (`in_memory == true`, kind `Ptr`). A
// payload sub-pattern descends to `base + payload_offset + slot_offset`, so ONE
// test/latch path covers any nesting depth; only the accumulated offset differs.
struct P6PatternSite {
    P6ScalarKind kind{P6ScalarKind::IntI32};
    bool in_memory{false};
    std::uint64_t offset{0};
};

// The P4-D layout of `type`, or null when the value type / layout id is out of
// range. One shared bounds-checked accessor for every P6 kind query.
[[nodiscard]] const ir::core::CoreLayout *p6_value_layout(const CoreProgram &program,
                                                          const ir::core::CoreLayoutTable &layouts,
                                                          CoreValueTypeId type) {
    if (type.value >= program.value_types.size() || type.value >= layouts.value_layouts.size()) {
        return nullptr;
    }
    const CoreLayoutId layout_id = layouts.value_layouts[type.value];
    if (layout_id.value >= layouts.layouts.size()) {
        return nullptr;
    }
    return &layouts.layouts[layout_id.value];
}

// FB-3b: the P4-D environment aggregate LAYOUT of a lifted fn's closure type —
// the indirect edge off its CoreLayoutClosure — located through the hash-consed
// CoreVtClosure whose ordered capture types equal `fn.captures`. The returned
// layout's shape is a CoreLayoutStruct (field offsets/sizes) and `size` gives
// the aggregate's byte extent. Null for an ordinary / zero-capture fn.
[[nodiscard]] const ir::core::CoreLayout *
p6_closure_environment_layout(const CoreProgram &program,
                              const ir::core::CoreLayoutTable &layouts,
                              const CoreFnDecl &fn) {
    if (fn.captures.empty()) {
        return nullptr;
    }
    for (std::uint32_t i = 0; i < program.value_types.size(); ++i) {
        const auto *closure =
            std::get_if<ir::core::CoreVtClosure>(&program.value_types[i].node);
        if (closure == nullptr || closure->captures.size() != fn.captures.size()) {
            continue;
        }
        bool matches = true;
        for (std::uint32_t s = 0; s < fn.captures.size(); ++s) {
            if (closure->captures[s].value_type != fn.captures[s]) {
                matches = false;
                break;
            }
        }
        if (!matches || i >= layouts.value_layouts.size()) {
            continue;
        }
        const CoreLayoutId closure_layout_id = layouts.value_layouts[i];
        if (closure_layout_id.value >= layouts.layouts.size()) {
            continue;
        }
        const auto *shape = std::get_if<ir::core::CoreLayoutClosure>(
            &layouts.layouts[closure_layout_id.value].shape);
        if (shape == nullptr || !shape->environment.has_value()) {
            continue;
        }
        const CoreLayoutId env_id = *shape->environment;
        if (env_id.value >= layouts.layouts.size()) {
            continue;
        }
        const ir::core::CoreLayout &env_layout = layouts.layouts[env_id.value];
        if (std::holds_alternative<ir::core::CoreLayoutStruct>(env_layout.shape)) {
            return &env_layout;
        }
    }
    return nullptr;
}

// Struct-shape accessor over the env layout above.
[[nodiscard]] const ir::core::CoreLayoutStruct *
p6_closure_environment_struct(const ir::core::CoreLayout *env_layout) {
    return env_layout == nullptr
               ? nullptr
               : std::get_if<ir::core::CoreLayoutStruct>(&env_layout->shape);
}

// The P4-D CONTAINER layout of a BOUNDED collection value type, or null. A
// bounded collection is a `CoreVtNominal` over a List / Set / Map declaration
// whose `capacity` is present; its final layout shape is `CoreLayoutContainer`
// (the one authority for element layout, stride, capacity, Map value offset and
// checked backing size). An UNBOUNDED collection has no such layout (P4-D fails
// `core.layout.UNBOUNDED`), so this returns null and every consumer fails
// closed — the boundedness gate is the layout table's, never a second copy.
[[nodiscard]] const ir::core::CoreLayoutContainer *p6_container_layout(
    const CoreProgram &program, const ir::core::CoreLayoutTable &layouts, CoreValueTypeId type) {
    if (type.value >= program.value_types.size()) {
        return nullptr;
    }
    const auto *nominal = std::get_if<CoreVtNominal>(&program.value_types[type.value].node);
    if (nominal == nullptr || !nominal->capacity.has_value() ||
        nominal->base.value >= program.types.size()) {
        return nullptr;
    }
    const CoreTypeDecl &decl = program.types[nominal->base.value];
    if (!ir::core::capacity_allowed(decl.role)) {
        return nullptr;
    }
    const ir::core::CoreLayout *layout = p6_value_layout(program, layouts, type);
    if (layout == nullptr) {
        return nullptr;
    }
    return std::get_if<ir::core::CoreLayoutContainer>(&layout->shape);
}

// Whether `type` is a tag-only enum (an enum whose every variant payload is
// zero-sized): its whole runtime representation is the i32 discriminant, so a
// `match` on it compiles to tag compares with no payload projection. This is
// the unit-variant pattern + qualified-variant-constructor lane; a payload-
// bearing enum needs struct layout and lands with P6-4.
[[nodiscard]] bool p6_is_tag_only_enum(const CoreProgram &program,
                                       const ir::core::CoreLayoutTable &layouts,
                                       CoreValueTypeId type) {
    const ir::core::CoreLayout *layout = p6_value_layout(program, layouts, type);
    if (layout == nullptr) {
        return false;
    }
    const auto *tagged = std::get_if<ir::core::CoreLayoutEnum>(&layout->shape);
    if (tagged == nullptr || tagged->tag_size != 4) {
        return false;
    }
    return std::ranges::all_of(tagged->variant_payload_sizes,
                               [](std::uint64_t size) { return size == 0; });
}

// Whether `type` is an AGGREGATE whose runtime representation is an i32 linear
// memory ADDRESS of its P4-D shaped bytes (RFC 0026 P6-4): a struct nominal or
// a payload-bearing enum. A tag-only enum is NOT one — its whole value is the
// i32 discriminant and it projects no payload. A zero-sized struct is rejected:
// it has no fields to load or store, so admitting it would be a meaningless
// address with no observable effect (fail closed rather than emit a dummy).
[[nodiscard]] bool p6_is_aggregate(const CoreProgram &program,
                                   const ir::core::CoreLayoutTable &layouts,
                                   CoreValueTypeId type) {
    const ir::core::CoreLayout *layout = p6_value_layout(program, layouts, type);
    if (layout == nullptr || layout->is_zero_sized) {
        return false;
    }
    if (std::holds_alternative<ir::core::CoreLayoutStruct>(layout->shape)) {
        return true;
    }
    if (std::holds_alternative<ir::core::CoreLayoutEnum>(layout->shape)) {
        return !p6_is_tag_only_enum(program, layouts, type);
    }
    return false;
}

// The single logical value type that names the argument-less nominal `type`
// (index identity, never a name — Principle 2). A generic instantiation carries
// a non-empty argument list and is deliberately NOT matched, so a parameterized
// aggregate fails closed rather than silently picking the wrong instantiation.
[[nodiscard]] std::optional<CoreValueTypeId> p6_nominal_value_type(const CoreProgram &program,
                                                                   CoreTypeId type) {
    if (type.value >= program.types.size()) {
        return std::nullopt;
    }
    for (std::uint32_t i = 0; i < program.value_types.size(); ++i) {
        const auto *nominal = std::get_if<CoreVtNominal>(&program.value_types[i].node);
        if (nominal != nullptr && nominal->base == type && nominal->args.empty()) {
            return CoreValueTypeId{i};
        }
    }
    return std::nullopt;
}

// The P4-D struct layout of a (non-generic) nominal struct type, or null.
[[nodiscard]] const ir::core::CoreLayoutStruct *p6_nominal_struct_layout(
    const CoreProgram &program, const ir::core::CoreLayoutTable &layouts, CoreTypeId type) {
    const auto value_type = p6_nominal_value_type(program, type);
    if (!value_type.has_value()) {
        return nullptr;
    }
    const ir::core::CoreLayout *layout = p6_value_layout(program, layouts, *value_type);
    if (layout == nullptr) {
        return nullptr;
    }
    return std::get_if<ir::core::CoreLayoutStruct>(&layout->shape);
}

// The P4-D enum layout of a (non-generic) nominal enum type, or null.
[[nodiscard]] const ir::core::CoreLayoutEnum *p6_nominal_enum_layout(
    const CoreProgram &program, const ir::core::CoreLayoutTable &layouts, CoreTypeId type) {
    const auto value_type = p6_nominal_value_type(program, type);
    if (!value_type.has_value()) {
        return nullptr;
    }
    const ir::core::CoreLayout *layout = p6_value_layout(program, layouts, *value_type);
    if (layout == nullptr) {
        return nullptr;
    }
    return std::get_if<ir::core::CoreLayoutEnum>(&layout->shape);
}

// The P4-D enum layout of a CONCRETE enum value type (a generic instantiation
// such as `Option<Int>` / `Result<Int, String>`), or null. Generic enums have
// no args-empty representative that carries the instantiated payload slot
// layout, so a variant constructor / pattern must resolve the layout through
// the concrete scrutinee / result value type rather than the bare nominal
// declaration. The finalized layout for every used instantiation lives in
// `value_layouts` (P4-D finalizes all interned value types).
[[nodiscard]] const ir::core::CoreLayoutEnum *p6_enum_value_layout(
    const CoreProgram &program, const ir::core::CoreLayoutTable &layouts,
    CoreValueTypeId value_type, CoreTypeId expected_base) {
    const ir::core::CoreLayout *layout = p6_value_layout(program, layouts, value_type);
    if (layout == nullptr) {
        return nullptr;
    }
    const auto *shape = std::get_if<ir::core::CoreLayoutEnum>(&layout->shape);
    if (shape == nullptr) {
        return nullptr;
    }
    if (value_type.value >= program.value_types.size()) {
        return nullptr;
    }
    const auto *nominal = std::get_if<CoreVtNominal>(&program.value_types[value_type.value].node);
    if (nominal == nullptr || nominal->base != expected_base) {
        return nullptr;
    }
    return shape;
}

// The P4-D byte size of a (non-generic) nominal type, or nullopt when the type
// has no interned value type / finalized layout. ONE size query, shared by the
// constructor scratch allocator and the fixed-frame capacity gate.
[[nodiscard]] std::optional<std::uint64_t> p6_nominal_size(const CoreProgram &program,
                                                           const ir::core::CoreLayoutTable &layouts,
                                                           CoreTypeId type) {
    const auto value_type = p6_nominal_value_type(program, type);
    if (!value_type.has_value()) {
        return std::nullopt;
    }
    const ir::core::CoreLayout *layout = p6_value_layout(program, layouts, *value_type);
    if (layout == nullptr) {
        return std::nullopt;
    }
    return layout->size;
}

// Whether a fixed aggregate frame (`input` / `context`) can hold the agent's
// struct. The struct's P4-D size must fit the region's byte capacity, and the
// last field's offset must stay inside it: a size-only check would still admit a
// struct whose tail padding crosses the boundary. One RESOURCE-class rejection
// names the frame and both sizes, so the failure is actionable.
[[nodiscard]] bool fits_frame_region(const CoreProgram &program,
                                     const ir::core::CoreLayoutTable &layouts,
                                     CoreTypeId type,
                                     std::uint32_t capacity,
                                     std::string_view frame,
                                     CoreWasmCodegenResult &result) {
    const auto size = p6_nominal_size(program, layouts, type);
    if (!size.has_value() || *size > capacity) {
        add_diag(result,
                 core_wasm_diag::kResourceExhausted,
                 "agent " + std::string(frame) + " frame is " +
                     (size.has_value() ? std::to_string(*size) : std::string("unknown")) +
                     " bytes but its reserved region holds only " + std::to_string(capacity) +
                     " bytes in the fixed 64 KiB linear-memory page");
        return false;
    }
    return true;
}

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
    // RFC 0026 FB-3b (D-FNREP): BOTH the signature type `CoreVtFn` (a
    // callable parameter / binding slot) and `CoreVtClosure` (a construction
    // site) share the eight-byte `CoreLayoutClosure` word pair. It is a
    // first-class P6 value but a TWO-word one, so it gets its own kind and can
    // never be mistaken for a scalar or an aggregate address.
    if (std::holds_alternative<ir::core::CoreLayoutClosure>(
            layouts.layouts[layout_id.value].shape)) {
        return P6ScalarKind::Closure;
    }
    const auto *scalar =
        std::get_if<ir::core::CoreLayoutScalar>(&layouts.layouts[layout_id.value].shape);
    if (scalar == nullptr || scalar->repr == ir::core::CoreScalarRepr::F64) {
        // F64 needs the f64 opcode ladder, which is a later P6 slice; every
        // non-scalar aggregate is outside the scalar subset.
        // A tag-only enum is one non-scalar shape in the subset: it is an i32
        // discriminant consumed only by `match` (never by an arithmetic operator,
        // which the distinct kind enforces). A STRUCT or a PAYLOAD-BEARING enum
        // (RFC 0026 P6-4) is the other: its whole representation is the i32
        // address of its P4-D shaped bytes, consumed only by a field projection,
        // a store, or a payload pattern — never by an operator.
        if (p6_is_tag_only_enum(program, layouts, type)) {
            return P6ScalarKind::Index;
        }
        if (p6_is_aggregate(program, layouts, type)) {
            return P6ScalarKind::Ptr;
        }
        // RFC 0026 P6-5: a BOUNDED collection is a handle-shaped value like an
        // aggregate (the address of its inline header). An UNBOUNDED collection
        // has no P4-D container layout, so `p6_container_layout` returns null and
        // this stays fail-closed — the exact `core.layout.UNBOUNDED` boundary.
        if (p6_container_layout(program, layouts, type) != nullptr) {
            return P6ScalarKind::Collection;
        }
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

// --- P6-6 coercion physical effects (RFC 0026 Q4) ---
//
// A `CoreCoerceExpr` is executable exactly when EVERY op in its proof plan has a
// known PHYSICAL action under the P6 value model. The action is LAYOUT-DERIVED,
// never re-derived from the op's spelling: the P4-D `CoreLayoutTable` decides
// whether the two endpoints are the same bytes (no code), a scalar width growth
// (one extend instruction), or a shape P6 cannot carry (fail closed).
//
//   IntWiden     : `BoundedInt <: Int`. A bounds refinement at the SAME physical
//                  repr is a physical no-op; a repr growth i32 -> i64 is
//                  `i64.extend_i32_s`. Anything else (e.g. a bounded value wider
//                  than the target) is not a widening P6 can perform.
//   TypeArg / FnParam / FnReturn : a type-level proof whose physical meaning is
//                  "the same representation". Realized ONLY when the endpoints
//                  are the same bytes (a covariant projection through an
//                  unchanged layout, e.g. an unbounded phantom argument); a
//                  projection that changes the shape fails closed, because
//                  performing it would need the child plan's own effects.
//   StringWiden  : no runtime code in principle — a `CoreVtString` is a 2-word
//                  PtrLen pair whose width is layout-identical at both endpoints,
//                  so the words would move unchanged. But the P6 value model has
//                  NO 2-word value at all (a `PtrLen` is neither a scalar local
//                  nor a single-word aggregate address), so no P6 local can hold
//                  the result: it fails closed rather than truncating.
//   CapacityWiden: likewise outside the P6 value model. A bounded collection is
//                  a `CoreLayoutContainer` keyed by `capacity` -> `backing_size`,
//                  and P6 has no collection handle, element store, or length
//                  word, so there is no layout-derived action to emit.
//
// The classifier is total over `CoreCoercionOpKind` and returns the exact
// physical action, so the caller never has to re-inspect the kind.
enum class P6CoercionEffect {
    None,      // no runtime code: the operand's word IS the result's word
    ExtendI32, // i32 -> i64 sign extension (`BoundedInt <: Int` with repr growth)
};

/// Stable display name of a coercion op kind for a P6 fail-closed diagnostic
/// (never used as identity — the enum is).
[[nodiscard]] constexpr std::string_view core_coercion_op_name(CoreCoercionOpKind kind) noexcept {
    switch (kind) {
    case CoreCoercionOpKind::IntWiden:
        return "IntWiden";
    case CoreCoercionOpKind::StringWiden:
        return "StringWiden";
    case CoreCoercionOpKind::CapacityWiden:
        return "CapacityWiden";
    case CoreCoercionOpKind::TypeArg:
        return "TypeArg";
    case CoreCoercionOpKind::FnParam:
        return "FnParam";
    case CoreCoercionOpKind::FnReturn:
        return "FnReturn";
    }
    return "unknown";
}

// The single physical-effect decision for one coercion op kind. `source`/`result`
// are the plan node's endpoints; the widths come from the same `p6_scalar_kind`
// the rest of the scalar ladder uses and the byte-shape equality from the P4-D
// `value_layouts_equivalent` SSOT — codegen never re-derives a repr.
[[nodiscard]] std::optional<P6CoercionEffect>
p6_coercion_effect(const CoreProgram &program,
                   const ir::core::CoreLayoutTable &layouts,
                   CoreCoercionOpKind kind,
                   CoreValueTypeId source,
                   CoreValueTypeId result,
                   std::string &why) {
    const bool same_bytes = ir::core::value_layouts_equivalent(layouts, source, result);
    const auto source_kind = p6_scalar_kind(program, layouts, source);
    const auto result_kind = p6_scalar_kind(program, layouts, result);

    switch (kind) {
    case CoreCoercionOpKind::CapacityWiden:
        why = "capacity widening has no P6 collection value representation";
        return std::nullopt;
    case CoreCoercionOpKind::IntWiden:
        if (same_bytes) {
            return P6CoercionEffect::None; // same physical width: a bounds refinement
        }
        if (source_kind == P6ScalarKind::IntI32 && result_kind == P6ScalarKind::IntI64) {
            return P6CoercionEffect::ExtendI32;
        }
        why = "IntWiden is neither a same-width bounds refinement nor an i32 -> i64 widening";
        return std::nullopt;
    case CoreCoercionOpKind::StringWiden:
    case CoreCoercionOpKind::TypeArg:
    case CoreCoercionOpKind::FnParam:
    case CoreCoercionOpKind::FnReturn:
        if (same_bytes && source_kind != std::nullopt && result_kind != std::nullopt) {
            // The operand's own P6 word IS a valid P6 word of the result type;
            // a projection that lands on a shape P6 has no local for (Index /
            // aggregate / f64 / a 2-word String) is rejected by the caller's
            // result-kind check, so this branch stays a true no-op.
            return P6CoercionEffect::None;
        }
        why = "a " + std::string(core_coercion_op_name(kind)) +
              " projection is not a same-layout transformation of a P6 value";
        return std::nullopt;
    }
    why = "unknown coercion operation kind";
    return std::nullopt;
}

// Structural all-paths termination predicate for a P6 scalar handler region.
// A computed-goto handler is accepted only when EVERY path leaves via a goto or
// trap (so `step()` always performs exactly one transition or traps and never
// falls through with an undefined next state). It examines the region's LAST
// statement because everything after a terminator is unreachable: a trailing
// if must diverge on BOTH branches, and a trailing goto/trap diverges outright.
// A region ending in let / match / fall-through keeps the handler on a later
// slice.
[[nodiscard]] bool p6_region_always_diverges(const CoreRegion &region) {
    if (region.statements.empty()) {
        return false;
    }
    const CoreStmt &last = region.statements.back();
    // RFC 0027 P6/P7/P8 (KR6.13-P7): one handler per CoreStmtNode alternative,
    // generated from core_stmt_nodes.def. Only control-transfer statements can
    // make a region "always diverge"; the rest name
    // P6_REGION_DIVERGE_NEVER (explicit, named no-op). No generic catch-all: a
    // new statement alternative is a COMPILE ERROR here until classified.
#define P6_REGION_DIVERGE_NEVER(Name) [](const Name &) { return false; },
#define P6_REGION_DIVERGE_CoreGotoStmt(Name) [](const Name &) { return true; },
#define P6_REGION_DIVERGE_CoreTrapStmt(Name) [](const Name &) { return true; },
#define P6_REGION_DIVERGE_CoreIfStmt(Name)                                                         \
    [](const Name &s) {                                                                            \
        return s.then_region && s.else_region && p6_region_always_diverges(*s.then_region) &&      \
               p6_region_always_diverges(*s.else_region);                                          \
    },
#define P6_REGION_DIVERGE_CoreMatchStmt(Name)                                                      \
    [](const Name &s) {                                                                            \
        if (!s.fallback_region || !p6_region_always_diverges(*s.fallback_region)) {                \
            return false;                                                                          \
        }                                                                                          \
        return std::ranges::all_of(s.arms, [](const CoreMatchArm &arm) {                           \
            return arm.body && p6_region_always_diverges(*arm.body);                               \
        });                                                                                        \
    },
#define P6_REGION_DIVERGE_CoreLetStmt(Name) P6_REGION_DIVERGE_NEVER(Name)
#define P6_REGION_DIVERGE_CoreCapabilityCallStmt(Name) P6_REGION_DIVERGE_NEVER(Name)
#define P6_REGION_DIVERGE_CoreCallStmt(Name) P6_REGION_DIVERGE_NEVER(Name)
#define P6_REGION_DIVERGE_CoreStoreStmt(Name) P6_REGION_DIVERGE_NEVER(Name)
#define P6_REGION_DIVERGE_CoreReturnStmt(Name) P6_REGION_DIVERGE_NEVER(Name)
#define P6_REGION_DIVERGE_CoreYieldStmt(Name) P6_REGION_DIVERGE_NEVER(Name)
#define HANDLE_CORE_STMT_NODE(Name, Wire) P6_REGION_DIVERGE_##Name(Name)
    return std::visit(
        Overloaded{
#include "ahfl/compiler/ir/core_stmt_nodes.def"
        },
        last.node);
#undef HANDLE_CORE_STMT_NODE
#undef P6_REGION_DIVERGE_NEVER
#undef P6_REGION_DIVERGE_CoreGotoStmt
#undef P6_REGION_DIVERGE_CoreTrapStmt
#undef P6_REGION_DIVERGE_CoreIfStmt
#undef P6_REGION_DIVERGE_CoreMatchStmt
#undef P6_REGION_DIVERGE_CoreLetStmt
#undef P6_REGION_DIVERGE_CoreCapabilityCallStmt
#undef P6_REGION_DIVERGE_CoreCallStmt
#undef P6_REGION_DIVERGE_CoreStoreStmt
#undef P6_REGION_DIVERGE_CoreReturnStmt
#undef P6_REGION_DIVERGE_CoreYieldStmt
}

// --- P6-3 match lowering: the arm-chain shape (RFC 0026 Q2) ---
//
// A `match` compiles to three nested wasm blocks per arm, no relooper:
//
//     block                      <- S: "this match is DONE" (skips the fallback)
//       block                    <- B: the arm chain + the fallback
//         block                  <- C_i: THIS arm (its mismatch/guard-false exit)
//           <bindings latched>   <- the whole scrutinee, one scratch local each
//           <pattern test>       <- leaves i32
//           br_if 0              <- not matched  -> end of C_i -> arm i+1
//           <guard>; br_if 0     <- guard false  -> end of C_i -> arm i+1
//           <body>               <- completes -> `br` to S; may diverge instead
//         end
//         ... remaining arms ...
//         <fallback region>      <- runs when no arm matched
//       end                      <- B
//     end                        <- S
//
// Why S exists: a statement arm whose body COMPLETES (the lowered `if let` then
// block) must skip the fallback, which sits after every arm inside B — only a
// label outside B can express that in structured control flow. A mismatched
// test and a false guard both continue to the NEXT ARM, which is exactly the
// position after C_i's `end`, so they are `br 0`.
//
// `label_depth_` is "labels between here and the handler block", so inside C_i
// it counts S, B and C_i; S is therefore at `label_depth_ - 1` and the handler
// block at `label_depth_` (unchanged from `emit_goto_transition`). Every
// reachable goto target is recorded by the ordinary statement emitter as it
// walks the regions, so `targets()` is exact without a separate region walk.

// Maps a CoreFnDecl's own body storage into the scalar builder. The builder is
// shared by flow handlers and outlined fn bodies (design §6.1: ONE expression
// compiler, fn bodies are the third body owner); this view is the fn-side twin
// of (flow, handler).
struct FnBodyView {
    const CoreBodyStorage *body_storage{nullptr};
    const CoreRegion *body{nullptr};
    std::string_view name;
};

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
        : program_(program), layouts_(layouts), storage_(flow.storage),
          region_(handler.body), state_name_(handler.state_name),
          unsupported_code_(unsupported_code), used_exprs_(used_exprs), used_values_(used_values),
          result_(result), locals_(flow.storage.value_count, LocalInfo{}),
          binding_locals_(flow.storage.value_count, LocalInfo{}),
          binding_sites_(flow.storage.value_count, std::nullopt),
          match_result_locals_(flow.storage.value_count, LocalInfo{}),
          construct_addrs_(flow.storage.exprs.size(), std::nullopt),
          dynamic_constructs_(flow.storage.exprs.size(), false) {}

    // FB-1 fn-body twin. `fn_ordinals` resolves a callee CoreInstanceId to the
    // wasm ordinal its module placement assigned (CoreFnId ordinal, not the
    // absolute function index — the builder adds import+7+handler itself at
    // emit time). The resolver is null in handler mode, where a direct call
    // stays fail-closed.
    P6ComputationHandlerBuilder(const CoreProgram &program,
                                const ir::core::CoreLayoutTable &layouts,
                                FnBodyView fn,
                                std::vector<bool> &used_exprs,
                                std::vector<bool> &used_values,
                                CoreWasmCodegenResult &result,
                                const std::vector<std::uint32_t> *instance_to_fn_ordinal,
                                const std::uint32_t *fn_function_base)
        : program_(program), layouts_(layouts), storage_(*fn.body_storage), region_(*fn.body),
          state_name_(fn.name), fn_mode_(true),
          unsupported_code_(core_wasm_diag::kUnsupportedOrchestration),
          used_exprs_(used_exprs), used_values_(used_values), result_(result),
          locals_(fn.body_storage->value_count, LocalInfo{}),
          binding_locals_(fn.body_storage->value_count, LocalInfo{}),
          binding_sites_(fn.body_storage->value_count, std::nullopt),
          match_result_locals_(fn.body_storage->value_count, LocalInfo{}),
          construct_addrs_(fn.body_storage->exprs.size(), std::nullopt),
          dynamic_constructs_(fn.body_storage->exprs.size(), false),
          instance_to_fn_ordinal_(instance_to_fn_ordinal),
          fn_function_base_(fn_function_base) {}

    // Validate the handler is in the scalar subset and assign every bound SSA
    // value a per-repr pool slot (i32 group first, then i64 — a real function
    // needs one fixed type per local index), recording the goto target set.
    [[nodiscard]] bool plan() {
        if (!fn_mode_ && !p6_region_always_diverges(region_)) {
            return reject("non-final scalar handler must goto or trap on every path",
                          region_.statements.empty()
                              ? ir::SourceRangeOpt{}
                              : region_.statements.front().source_range);
        }
        return plan_region(region_);
    }

    // FB-1: after a successful plan, the concrete P6 word each SSA value uses.
    // The caller maps the fn's pre-bound params and the return value through
    // this to materialize the wasm functype.
    [[nodiscard]] std::optional<P6ScalarKind> word_of(CoreValueId value) const {
        if (value.value >= storage_.value_types.size()) {
            return std::nullopt;
        }
        return p6_scalar_kind(program_, layouts_, storage_.value_types[value.value]);
    }

    [[nodiscard]] const std::vector<CoreInstanceId> &fn_callees() const noexcept {
        return fn_callees_;
    }

    // FB-1: mark a pre-bound fn parameter before plan() walks the body. Public
    // so the outlined-fn compilation pass can bind the functype parameters.
    [[nodiscard]] bool bind_param(CoreValueId value, std::uint32_t ordinal,
                                  P6ScalarKind kind);

    // FB-1: install the direct-call tables AFTER construction (ordinals are
    // assigned by the reachability fixed point, which runs after every entry
    // body is planned). Required before emit() when the body contains a
    // CoreCallExpr; plan() validates callees without these.
    void set_fn_call_tables(const std::vector<std::uint32_t> *instance_to_fn_ordinal,
                            const std::uint32_t *fn_function_base) {
        instance_to_fn_ordinal_ = instance_to_fn_ordinal;
        fn_function_base_ = fn_function_base;
    }

    // FB-1 fix-forward (handler mode): emit a `heap_next = heap_base` reset as
    // this entry handler's first instruction so every step() starts a fresh
    // per-activation aggregate heap.
    void reset_construct_heap_on_entry(std::uint32_t heap_base) {
        reset_construct_heap_ = true;
        reset_heap_base_ = heap_base;
    }

    // FB-1 fix-forward: aligned per-activation bytes the planned fn body bumps
    // the aggregate heap for its dynamic constructs (0 for handlers).
    [[nodiscard]] std::uint32_t dynamic_construct_bytes() const noexcept {
        return dynamic_construct_bytes_;
    }

    // FB-3b: bind ONE pre-bound environment slot of a lifted fn. It is an
    // ordinary bound local of `kind` (a pair for a nested-closure capture),
    // populated at fn entry by an i32.load/i64.load from the env pointer
    // (wasm local 0) at `offset`. Env slots never appear in the logical
    // functype parameters, so they consume declared-local pool slots rather
    // than parameter ordinals; the entry loads run before the region.
    [[nodiscard]] bool
    bind_env_binding(CoreValueId value, std::uint32_t offset, P6ScalarKind kind) {
        if (value.value >= locals_.size() || locals_[value.value].bound) {
            return false;
        }
        LocalInfo &info = locals_[value.value];
        info.bound = true;
        info.kind = kind;
        if (kind == P6ScalarKind::IntI64) {
            info.slot = i64_count_++;
        } else {
            info.slot = i32_count_++;
            if (kind == P6ScalarKind::Closure) {
                info.is_word_pair = true;
                ++i32_count_;
            }
        }
        EnvBindingInit init;
        init.value = value;
        init.offset = offset;
        init.wide = kind == P6ScalarKind::IntI64;
        init.pair = kind == P6ScalarKind::Closure;
        env_binding_inits_.push_back(init);
        used_values_[value.value] = true;
        return true;
    }

    // FB-3b: funcref-table facts the module driver unions across bodies.
    [[nodiscard]] const std::vector<CoreFnId> &closure_targets() const noexcept {
        return closure_targets_;
    }
    [[nodiscard]] const std::vector<ClosureCallType> &closure_call_types() const noexcept {
        return closure_call_types_;
    }
    [[nodiscard]] std::uint32_t closure_env_bytes() const noexcept {
        return closure_env_bytes_;
    }

    // FB-3b: install the finalized funcref-table slots and call_indirect type
    // indices. Called after every reachable body was planned and before
    // emit().
    void install_closure_tables(
        const std::unordered_map<std::uint32_t, std::uint32_t> &fn_to_table_slot,
        std::unordered_map<std::string, std::uint32_t> closure_type_index) {
        fn_to_table_slot_ = &fn_to_table_slot;
        closure_type_index_ = std::move(closure_type_index);
        closure_tables_installed_ = true;
    }

    // RFC 0026 FB-4: install the module's sorted capability import table so an
    // outlined effect fn can emit its in-body ahfl_cap `call` at the SAME
    // import ordinal a handler capability statement would use. Called after
    // import planning and before fn bodies are emitted.
    void install_import_table(const std::vector<CoreCapabilityId> *imports) {
        imports_ = imports;
    }

    [[nodiscard]] const std::vector<CoreStateId> &targets() const noexcept {
        return targets_;
    }

    // KR6.7 (RFC 0026 P7): true when this handler projects a field out of the
    // raw P4-D INPUT frame (or reads a bounded collection reached through the
    // input frame) using the fixed reserved-region addressing. Such a handler
    // consumes RAW frame bytes, not the opaque canonical wire-JSON the run2
    // boundary otherwise forwards, so a canonical output observation awaits the
    // P6-7 frame decision. Context-only stores and construct/local-rooted
    // projections do not set this: the output boundary stays the borrowed wire
    // frame.
    [[nodiscard]] bool reads_raw_input_frame() const noexcept {
        return reads_raw_input_frame_;
    }

    // Emit the complete body.
    //
    // Handler mode: the body of this handler's own `() -> i32` wasm function:
    // the local declarations (its OWN i32 group then i64 group), a
    // `block (result i32)` that every goto path leaves with `br`, the lowered
    // region, and the closing `end`s. The block is the structured early-exit
    // target RFC 0026 Q2 mandates: no relooper, no arbitrary jump.
    //
    // Fn mode (FB-1 §6.3): an `(i32 env, args...) -> ret` function. The leading
    // `env` parameter is wasm local 0; the concrete args occupy locals
    // 1..param_count in DECLARATION order (their words may mix i32/i64 — the
    // functype carries the mixed order, so params do not go through the
    // i32-first SSA pool). Non-param SSA + scratch locals follow, grouped
    // i32-word then i64-word. The body completes with an explicit value
    // `return`; there is no result-i32 block.
    [[nodiscard]] std::optional<std::vector<std::uint8_t>>
    emit(std::span<const P6ScalarKind> param_words) {
        // The pool base EVERY non-param local lookup adds is 1 (env) + the
        // total flat PARAM WORD count: a Closure parameter occupies two i32
        // argument locals (func_index, env_ptr). Set it before walking the
        // region (which emits local.get/set).
        fn_param_count_ = 0;
        for (const P6ScalarKind word : param_words) {
            fn_param_count_ += boundary_word_count(word);
        }
        // Runtime checked-bump temporaries (fresh base + advanced heap pointer).
        // They are needed by fn-mode aggregate constructs AND by capturing
        // closure construction in EITHER mode (closure envs are module-arena
        // memory that must outlive the creating activation). They are the LAST
        // two i32 locals so the SSA/scratch pool indices are unchanged.
        const bool dynamic =
            (fn_mode_ && has_dynamic_construct()) || closure_env_bytes_ > 0;
        // FB-4: an outlined fn body with an in-fn capability call needs three
        // more trailing i32 scratch locals (status, ptr, len) for the
        // multi-value import result. They share the second i32 local group
        // after the bump temporaries.
        const std::uint32_t cap_scratch = (fn_mode_ && capability_scratch_needed_) ? 3u : 0u;
        const std::uint32_t temp_count = (dynamic ? 2u : 0u) + cap_scratch;
        // CORE-GAPS: the three Map-KeyGet scan scratch i32s, appended after the
        // bump/capability temporaries (absent when no KeyGet is planned).
        const std::uint32_t keyget_count = keyget_scratch_needed_ ? 3u : 0u;
        if (temp_count != 0 || keyget_count != 0) {
            // The bump temporaries are placed AFTER the i64 group (a second
            // i32 local group) so they do not shift the SSA/scratch i64 pool
            // indices pool_local derives from i32_group_size(). Fn bodies have
            // the leading env local plus the flat param words; handlers have no
            // parameters, so their pool begins at local 0.
            const std::uint32_t pool_base = fn_mode_ ? 1u + fn_param_count_ : 0u;
            const std::uint32_t after_groups =
                pool_base + i32_count_ + scratch_i32_count_ +
                i64_count_ + scratch_i64_count_;
            if (dynamic) {
                alloc_temp_local_ = after_groups;
                alloc_new_local_ = after_groups + 1u;
            }
            if (cap_scratch != 0) {
                const std::uint32_t base = after_groups + (dynamic ? 2u : 0u);
                cap_status_local_ = base;
                cap_ptr_local_ = base + 1u;
                cap_len_local_ = base + 2u;
            }
            if (keyget_count != 0) {
                const std::uint32_t base = after_groups + temp_count;
                keyget_cursor_local_ = base;
                keyget_found_local_ = base + 1u;
                keyget_addr_local_ = base + 2u;
            }
        }
        // Handler mode, aggregate/closure heap enabled: reset the per-activation
        // bump heap before the handler region so every step() starts with a
        // fresh arena (the prior step's aggregate / closure-env bytes are
        // abandoned — ByValue snapshots never alias across activations).
        if (!fn_mode_ && reset_construct_heap_) {
            emit_const_i32(static_cast<std::int32_t>(reset_heap_base_));
            body_.byte(kOpGlobalSet);
            body_.u32(kGlobalHeapNext);
        }
        // Fn mode (lifted fn with captures): populate every pre-bound
        // environment slot by loading from the env pointer (wasm local 0) at
        // its P4-D slot offset. This is the ONLY body the env pointer has;
        // captured names then read as ordinary bound locals.
        if (fn_mode_) {
            for (const EnvBindingInit &init : env_binding_inits_) {
                body_.byte(kOpLocalGet);
                body_.u32(0); // env pointer
                if (init.pair) {
                    const auto first = pool_local(locals_[init.value.value], false);
                    body_.byte(kOpI32Load);
                    body_.u32(kAlignI32);
                    body_.u32(init.offset);
                    body_.byte(kOpLocalSet);
                    body_.u32(first);
                    body_.byte(kOpLocalGet);
                    body_.u32(0);
                    body_.byte(kOpI32Load);
                    body_.u32(kAlignI32);
                    body_.u32(init.offset + 4u);
                    body_.byte(kOpLocalSet);
                    body_.u32(first + 1u);
                } else {
                    body_.byte(init.wide ? kOpI64Load : kOpI32Load);
                    body_.u32(init.wide ? kAlignI64 : kAlignI32);
                    body_.u32(init.offset);
                    body_.byte(kOpLocalSet);
                    body_.u32(pool_local(locals_[init.value.value], false));
                }
            }
        }
        if (!emit_region(region_)) {
            return std::nullopt;
        }
        ByteBuffer function;
        // Declared (non-parameter) locals. Params are implicit locals and must
        // not appear in the local-declaration group.
        const std::uint32_t pool_i32 = i32_count_ + scratch_i32_count_;
        const std::uint32_t pool_i64 = i64_count_ + scratch_i64_count_;
        std::uint32_t local_groups = 0;
        if (pool_i32 != 0) {
            ++local_groups;
        }
        if (pool_i64 != 0) {
            ++local_groups;
        }
        // The fn-mode bump temporaries form a SECOND i32 group after the i64
        // group, keeping the pool index spaces stable. The KeyGet scan scratch
        // joins that trailing i32 group (it exists in either body mode).
        const std::uint32_t temp_i32 = temp_count + keyget_count;
        if (temp_i32 != 0) {
            ++local_groups;
        }
        function.u32(local_groups);
        if (pool_i32 != 0) {
            function.u32(pool_i32);
            function.byte(kI32);
        }
        if (pool_i64 != 0) {
            function.u32(pool_i64);
            function.byte(kI64);
        }
        if (temp_i32 != 0) {
            function.u32(temp_i32);
            function.byte(kI32);
        }
        if (fn_mode_) {
            function.raw_span(body_.span());
            // plan() proved the region returns a value on every path; this
            // unreachable only types the (dead) fallthrough for a value fn.
            function.byte(kOpUnreachable);
            function.byte(kOpEnd);
            return std::move(function).take();
        }
        function.byte(kOpBlock);        function.byte(kI32); // block (result i32): the new state id
        function.raw_span(body_.span());
        // plan() proved the region diverges on every path, so no real
        // fallthrough reaches here; the trailing unreachable exists only to type
        // the block's result-i32 requirement on the (dead) fallthrough path.
        function.byte(kOpUnreachable);
        function.byte(kOpEnd);
        function.byte(kOpEnd);
        return std::move(function).take();
    }

  private:
    struct LocalInfo {
        P6ScalarKind kind{P6ScalarKind::IntI32};
        std::uint32_t slot{0}; // index within the kind's pool (or param ordinal)
        bool bound{false};
        // FB-1: a pre-bound fn PARAMETER. It is wasm local (1 + slot), where
        // slot is its declaration ordinal, and it is NOT part of the declared
        // i32/i64 SSA pools (parameters are implicit locals whose word order is
        // fixed by the functype, which may mix i32 and i64).
        bool is_param{false};
        // FB-3b: a Closure value occupies TWO consecutive i32 parameter / pool
        // slots (func_index word at `slot`, env_ptr word at `slot + 1`). Set
        // only for kind == P6ScalarKind::Closure.
        bool is_word_pair{false};
    };

    const CoreProgram &program_;
    const ir::core::CoreLayoutTable &layouts_;
    const CoreBodyStorage &storage_;
    const CoreRegion &region_;
    std::string_view state_name_;
    bool fn_mode_{false};
    std::string_view unsupported_code_;
    std::vector<bool> &used_exprs_;
    std::vector<bool> &used_values_;
    CoreWasmCodegenResult &result_;

    ByteBuffer body_;
    std::vector<LocalInfo> locals_; // CoreValueId -> pool slot
    std::uint32_t i32_count_{0};
    std::uint32_t i64_count_{0};
    // FB-1 fn mode: number of pre-bound parameters (== functype arg count, env
    // excluded). Set in emit(); pool_local adds the 1 + this offset.
    std::uint32_t fn_param_count_{0};
    // FB-1 fn mode: the direct callee instances this body invokes, in first-use
    // order (deduped), the edges of the reachability fixed point.
    std::vector<CoreInstanceId> fn_callees_;
    // Statement-level `if` opens one wasm label per nesting level, so a goto must
    // `br` past all of them to reach the handler's `block (result i32)`.
    std::uint32_t label_depth_{0};
    std::vector<CoreStateId> targets_;

    // --- P6-3 match lowering state ---
    //
    // A match arm binds pattern variables and, in expression position, produces a
    // value. Both need a scratch local, and both must be known before the body is
    // emitted (a wasm function declares all its locals up front, but wasm has no
    // way to free a local at a point in the instruction stream). This ONE scratch
    // pool therefore grows MONOTONICALLY in lexical order — the standard stack
    // slot allocator a compiler frame builder uses: every match statement takes
    // fresh slots from the current cursor, so a later match (a sibling statement,
    // or a nested match deeper in the stream) can never alias an earlier one's.
    // The cost is a few unused slots for a match whose scope ended early;
    // correctness is unaffected.
    //
    // `binding_locals_`      : CoreValueId -> slot, for a `CoreBindingPat`'s arm
    //                          binding (the value a `ValueRef` inside the arm reads).
    // `binding_sites_`       : CoreValueId -> the `P6PatternSite` that binding
    //                          latches from — the full site (kind + in-memory
    //                          flag + byte offset), so a payload binding reads
    //                          its own P4-D slot instead of the scrutinee root.
    // `match_result_locals_` : CoreValueId -> slot, for a match's `result` id; an
    //                          expression arm's yielded value is stored there.
    //
    // Slots are indexed within a kind's pool (one pool per physical repr).
    // `pool_local` derives every pool's base from the single
    // `[ SSA i32 ][ scratch i32 ][ SSA i64 ][ scratch i64 ]` grouping, so a slot
    // index is never constructed from one pool's size alone — the emitted local
    // declaration order and the index space cannot diverge.
    //
    // All three tables are keyed by CoreValueId, the SAME domain the emit pass
    // reads: the plan pass resolves a pattern's `CorePatternBindingId` (arm-local
    // index) through the arm's binding list before recording, so the pattern
    // index space and the value index space can never be confused.
    std::vector<LocalInfo> binding_locals_;
    std::vector<std::optional<P6PatternSite>> binding_sites_;
    std::vector<LocalInfo> match_result_locals_;
    std::uint32_t scratch_i32_count_{0};
    std::uint32_t scratch_i64_count_{0};

    // CORE-GAPS: set when a Map KeyGet scan is planned. The keyed-scan codegen
    // needs three trailing module-level i32 scratch locals (cursor, found flag,
    // current-entry address), appended after every SSA/scratch pool so the pool
    // index spaces are unchanged. There is NO key-width dummy and NO result
    // local: the matched value word is selected by the value_wide immediate
    // load and left on the operand stack (the enclosing let consumes it). The
    // loop bounds the cursor by the container's header len clamped to capacity,
    // so the scan is always finite.
    bool keyget_scratch_needed_{false};
    std::uint32_t keyget_cursor_local_{std::numeric_limits<std::uint32_t>::max()};
    std::uint32_t keyget_found_local_{std::numeric_limits<std::uint32_t>::max()};
    std::uint32_t keyget_addr_local_{std::numeric_limits<std::uint32_t>::max()};

    // --- P6-4 aggregate memory state ---
    //
    // `construct_addrs_`: CoreExprId -> the scratch linear-memory address a
    // CoreConstructExpr materializes its aggregate at. Assigned during the plan
    // pass from ONE monotone cursor (`scratch_addr_cursor_`) exactly like the
    // scratch locals above, so two construct sites can never alias and emit only
    // reads the address. `scratch_addr_cursor_` is a byte offset RELATIVE to the
    // scratch arena base (an absolute wasm32 address).
    std::vector<std::optional<std::uint32_t>> construct_addrs_;
    std::uint32_t scratch_addr_cursor_{0};
    // fn-mode only: aligned bytes this body bumps the per-activation arena.
    std::uint32_t dynamic_construct_bytes_{0};

    // FB-1 fix-forward: outlined fn bodies do NOT use the compile-time static
    // scratch arena for CoreConstructExpr storage. Every builder used to start
    // its own `scratch_addr_cursor_` at zero in the MODULE-SHARED scratch
    // region, so two native `call` activations (repeated, nested, or a callee
    // clobbering an aggregate argument) aliased one another's live aggregate —
    // a silent wrong-code defect (design §6.3 prescribes per-activation runtime
    // checked bump-heap storage for exactly this hazard). In fn mode every
    // construct is therefore materialized at run time with `call alloc`;
    // `dynamic_constructs_` marks those plan sites, and `dynamic_constructs_`
    // being non-empty adds one i32 temp local (the fresh allocation address).
    // Handler mode never sets these and stays byte-identical.
    std::vector<bool> dynamic_constructs_;

    // FB-1 fn mode: callee CoreInstanceId -> wasm fn ordinal assigned by the
    // module encoder. Null in handler mode.
    const std::vector<std::uint32_t> *instance_to_fn_ordinal_{nullptr};
    // FB-1 fn mode: absolute wasm function index of fn ordinal 0
    // (import_count+7+handler_count). Null in handler mode; set before emit.
    const std::uint32_t *fn_function_base_{nullptr};
    // FB-1 fix-forward (fn mode): aggregate constructs are bumped from the
    // module heap (fn_mode_ alone is the gate — an outlined fn ALWAYS uses the
    // per-activation heap; scalar fns simply construct nothing). Handler mode
    // only carries the entry-reset flag below.
    // Handler mode only (FB-1 fix-forward): when set, emit
    // `global.set heap_next <reset_heap_base_>` as the first body instruction
    // so each step() starts a fresh per-activation aggregate heap. Fn mode
    // bumps the SAME global (kGlobalHeapNext) inline for its dynamic aggregate
    // constructs. Unset on every builder of a module with no
    // aggregate-returning fn, which then stays byte-identical.
    bool reset_construct_heap_{false};
    std::uint32_t reset_heap_base_{0};
    // Emit-time absolute local indices for the fn-mode inline checked bump:
    // A = the fresh base (the construct's result address), B = cursor + size.
    // Set in emit() when the fn body contains at least one dynamic construct.
    std::uint32_t alloc_temp_local_{std::numeric_limits<std::uint32_t>::max()};
    std::uint32_t alloc_new_local_{std::numeric_limits<std::uint32_t>::max()};
    // FB-4: set when an outlined fn body contains at least one in-fn capability
    // call; emit() reserves three scratch i32 locals (status, ptr, len) for the
    // multi-value import result.
    bool capability_scratch_needed_{false};
    std::uint32_t cap_status_local_{std::numeric_limits<std::uint32_t>::max()};
    std::uint32_t cap_ptr_local_{std::numeric_limits<std::uint32_t>::max()};
    std::uint32_t cap_len_local_{std::numeric_limits<std::uint32_t>::max()};

    // --- RFC 0026 FB-3b closures ---
    //
    // A closure value is the eight-byte (func_index, env_ptr) pair; every
    // closure SSA value occupies TWO consecutive i32 locals / stack words
    // (func_index first, env_ptr second). The module driver finalizes the
    // dense funcref-table slots and call_indirect type indices after every
    // reachable body is planned, then installs the two maps before emit();
    // plan() records the targets / descriptors / env bytes the driver unions.
    const std::unordered_map<std::uint32_t, std::uint32_t> *fn_to_table_slot_{nullptr};
    // RFC 0026 FB-4: the module's sorted capability import table (imports
    // occupy the low function indices). Null in handler mode; set on every
    // builder before an effectful fn body is emitted.
    const std::vector<CoreCapabilityId> *imports_{nullptr};
    std::unordered_map<std::string, std::uint32_t> closure_type_index_{};
    bool closure_tables_installed_{false};
    // Every fn whose address a CoreClosureExpr takes in THIS body (unique,
    // first-seen order); the driver seeds funcref-table reachability.
    std::vector<CoreFnId> closure_targets_;
    // Every distinct call_indirect functype this body emits (plan time).
    std::vector<ClosureCallType> closure_call_types_;
    // Aligned heap bytes this body bumps for closure ENVIRONMENTS (the
    // captured-word aggregate each capturing closure allocates). Fn bodies
    // fold this into the per-activation recursion budget; a handler bumps it
    // once per step. Zero-capture closures allocate nothing.
    std::uint32_t closure_env_bytes_{0};
    // A lifted fn's pre-bound environment slots, installed by the driver via
    // bind_env_binding() BEFORE plan(). At fn entry the builder emits one load
    // per slot from the env pointer (wasm local 0) into the ordinary bound
    // local; a pair slot loads two words.
    struct EnvBindingInit {
        CoreValueId value{};
        std::uint32_t offset{0};
        bool wide{false}; // i64 slot
        bool pair{false}; // nested-closure (two i32 words)
    };
    std::vector<EnvBindingInit> env_binding_inits_;

    // KR6.7: set when a projection's root is the raw P4-D input frame. See
    // reads_raw_input_frame().
    bool reads_raw_input_frame_{false};

    [[nodiscard]] bool reject(std::string message, ir::SourceRangeOpt range) {
        body_.byte(kOpUnreachable);
        add_diag(result_,
                 unsupported_code_,
                 std::string("RFC 0026 P6 scalar codegen cannot lower body '") +
                     std::string(state_name_) + "': " + std::move(message),
                 std::move(range));
        return false;
    }

    // Fail closed with an EXPLICIT code (used for nodes whose blocker is a
    // distinct, later slice rather than the generic P6 subset): an unreachable
    // trap is still emitted so no partial artifact can run, while the diagnostic
    // names the real gate.
    [[nodiscard]] bool reject_with_code(std::string_view code, std::string message,
                                        ir::SourceRangeOpt range) {
        body_.byte(kOpUnreachable);
        add_diag(result_,
                 code,
                 std::string("RFC 0026 P6 scalar codegen cannot lower body '") +
                     std::string(state_name_) + "': " + std::move(message),
                 std::move(range));
        return false;
    }

    [[nodiscard]] std::optional<P6ScalarKind> scalar_kind(CoreValueTypeId type) const {
        return p6_scalar_kind(program_, layouts_, type);
    }

    // The declared size of the i32 group in the emitted local index space: every
    // SSA i32 local followed by every scratch i32 local. The handler function has
    // no parameters, so local 0 begins the i32 group, and a real wasm function
    // declares its locals as one i32 group followed by one i64 group (wider types
    // cannot share an index). EVERY kind's pool is carved from that single
    // grouping:
    //
    //     [ SSA i32 ][ scratch i32 ][ SSA i64 ][ scratch i64 ]
    //
    // So the i64 group begins at THIS offset. This is the ONE definition of that
    // boundary; `pool_local` derives both groups from it, which is what keeps the
    // SSA pool and the match scratch pool from disagreeing on where i64 starts.
    [[nodiscard]] std::uint32_t i32_group_size() const {
        return i32_count_ + scratch_i32_count_;
    }

    // The wasm local index of a pool slot. `scratch_pool` selects the monotone
    // match scratch pool (arm bindings / expression-match results) over the
    // ordinary SSA let pool; within either pool the slot is already offset by
    // its kind, so the base is the only arithmetic left.
    [[nodiscard]] std::uint32_t pool_local(const LocalInfo &info, bool scratch_pool) const {
        if (info.is_param) {
            // local 0 is env; parameter ordinal i (declaration order, mixed
            // i32/i64 words) is wasm local 1 + i.
            return 1u + info.slot;
        }
        // Non-param SSA + scratch declared locals begin at local
        // 1 + param_count (fn mode) or local 0 (handler mode). They are grouped
        // i32-word then i64-word; the i64 pool is offset by the i32 group size.
        const std::uint32_t base = fn_mode_ ? 1u + fn_param_count_ : 0u;
        if (info.kind == P6ScalarKind::IntI64) {
            return base + i32_group_size() + (scratch_pool ? i64_count_ : 0) + info.slot;
        }
        return base + (scratch_pool ? i32_count_ : 0) + info.slot;
    }

    [[nodiscard]] std::optional<std::uint32_t> final_local(CoreValueId value) const {
        if (value.value >= locals_.size() || !locals_[value.value].bound) {
            return std::nullopt;
        }
        return pool_local(locals_[value.value], /*scratch_pool=*/false);
    }

    // The wasm local index of a match scratch slot (a Bool / narrow Int /
    // tag-only enum discriminant is i32, an unbounded Int is i64); the scratch
    // group follows its kind's SSA group inside the declared grouping.
    [[nodiscard]] std::uint32_t match_pool_local(const LocalInfo &info) const {
        return pool_local(info, /*scratch_pool=*/true);
    }

    [[nodiscard]] std::optional<std::uint32_t> binding_local(CoreValueId value) const {
        if (value.value >= binding_locals_.size() || !binding_locals_[value.value].bound) {
            return std::nullopt;
        }
        return match_pool_local(binding_locals_[value.value]);
    }

    // The SITE an arm binding was planned at. The plan pass recorded the FULL
    // site (kind + in-memory flag + byte offset), so the kind here is the
    // binding's own P4-D repr — a struct-payload binding is a `Ptr` (an address),
    // an i64 payload field is `IntI64` — never the scrutinee's root kind. The
    // previous reconstruction guessed the root kind and would latch a payload
    // field's value as if it were the whole scrutinee.
    [[nodiscard]] std::optional<P6PatternSite> binding_site_of(CoreValueId value) const {
        if (value.value >= binding_sites_.size()) {
            return std::nullopt;
        }
        return binding_sites_[value.value];
    }

    // Latch one arm binding's value into its scratch local from its SITE: a
    // scalar is loaded, and an aggregate (a `Ptr` site) has its ADDRESS copied
    // (the binding then reads fields through that address later).
    [[nodiscard]] bool emit_binding_latch(std::uint32_t scrutinee_local,
                                          std::uint32_t dest,
                                          const P6PatternSite &site,
                                          ir::SourceRangeOpt range) {
        if (!emit_site_value(scrutinee_local, site, range)) {
            return false;
        }
        body_.byte(kOpLocalSet);
        body_.u32(dest);
        return true;
    }

    [[nodiscard]] std::optional<std::uint32_t> match_result_local(CoreValueId value) const {
        if (value.value >= match_result_locals_.size() ||
            !match_result_locals_[value.value].bound) {
            return std::nullopt;
        }
        return match_pool_local(match_result_locals_[value.value]);
    }

    // The wasm local a value id is readable from at this point in the stream: an
    // SSA let local, an arm binding scratch slot, or a match result slot, in that
    // order (the three are disjoint by construction — an id is produced by
    // exactly one statement kind). This is the ONE place `ValueRef` resolution
    // happens, so a match binding and an ordinary let read identically.
    [[nodiscard]] std::optional<std::uint32_t> readable_local(CoreValueId value) const {
        if (const auto local = final_local(value)) {
            return local;
        }
        if (const auto local = binding_local(value)) {
            return local;
        }
        return match_result_local(value);
    }

    // Plan-side twin of `readable_local` (no emit-only i64 base in the way).
    [[nodiscard]] std::optional<P6ScalarKind> readable_kind(CoreValueId value) const {
        if (const auto kind = final_local_like(value)) {
            return kind;
        }
        if (value.value < binding_locals_.size() && binding_locals_[value.value].bound) {
            return binding_locals_[value.value].kind;
        }
        if (value.value < match_result_locals_.size() && match_result_locals_[value.value].bound) {
            return match_result_locals_[value.value].kind;
        }
        return std::nullopt;
    }

    [[nodiscard]] bool emit_value_read(CoreValueId value, ir::SourceRangeOpt range) {
        const auto local = readable_local(value);
        if (local == std::nullopt) {
            return reject("value is not a readable local at this point in the handler",
                          std::move(range));
        }
        emit_local_get(*local);
        return true;
    }

    // Push ONE boundary argument's words onto the operand stack in declared
    // (func_index, env_ptr) order. A one-word value emits one local.get; a
    // Closure pair emits the function-index word then the env-pointer word,
    // matching the flat two-i32 functype parameter layout.
    [[nodiscard]] bool
    emit_boundary_value_read(CoreValueId value, ir::SourceRangeOpt range) {
        const auto kind = value.value < storage_.value_types.size()
                              ? scalar_kind(storage_.value_types[value.value])
                              : std::nullopt;
        const auto local = readable_local(value);
        if (kind == std::nullopt || local == std::nullopt) {
            return reject("boundary argument is not a readable representable value",
                          std::move(range));
        }
        if (*kind == P6ScalarKind::Closure) {
            emit_local_get(*local);         // func_index word
            emit_local_get(*local + 1u);    // env_ptr word
            return true;
        }
        emit_local_get(*local);
        return true;
    }

    // Store ONE captured environment value (already known to be P6-representable)
    // into the env aggregate at `base` + slot `offset`.
    [[nodiscard]] bool emit_env_slot_store(CoreValueId value,
                                           std::uint32_t base_local,
                                           std::uint64_t offset,
                                           ir::SourceRangeOpt range) {
        if (offset > std::numeric_limits<std::uint32_t>::max()) {
            return reject("environment slot offset exceeds the wasm32 domain", std::move(range));
        }
        const auto kind = scalar_kind(storage_.value_types[value.value]);
        const auto local = readable_local(value);
        if (kind == std::nullopt || local == std::nullopt) {
            return reject("captured value is not a readable representable value", std::move(range));
        }
        if (*kind == P6ScalarKind::Closure) {
            // Two words at offset (func_index) and offset+4 (env_ptr).
            emit_local_get(base_local);
            emit_local_get(*local);
            body_.byte(kOpI32Store);
            body_.u32(kAlignI32);
            body_.u32(static_cast<std::uint32_t>(offset));
            emit_local_get(base_local);
            emit_local_get(*local + 1u);
            body_.byte(kOpI32Store);
            body_.u32(kAlignI32);
            body_.u32(static_cast<std::uint32_t>(offset + 4u));
            return true;
        }
        const bool wide = *kind == P6ScalarKind::IntI64;
        emit_local_get(base_local);
        if (!emit_value_read(value, range)) {
            return false;
        }
        body_.byte(wide ? kOpI64Store : kOpI32Store);
        body_.u32(wide ? kAlignI64 : kAlignI32);
        body_.u32(static_cast<std::uint32_t>(offset));
        return true;
    }

    // --- P6-4 aggregate places ---
    //
    // AGGREGATE FIELD REPRESENTATION (the ONE rule both directions obey): a
    // struct / payload-bearing-enum FIELD slot holds a bare i32 ADDRESS of the
    // child aggregate's bytes, NEVER the child inlined. `emit_construct_store`'s
    // `place_is_aggregate_leaf` branch and `emit_store`'s aggregate leaf both
    // write it that way. So walking INTO such a field is a DEREFERENCE: the
    // projection walk loads the i32 at the field's offset and continues from THAT
    // address, and a final aggregate field's VALUE is the address its slot holds
    // (one load), not the slot's own address.
    //
    // `ProjectionRoot` is where a chain starts: a `Ptr`-kind SSA local / arm
    // binding holding the aggregate's address, or a reserved frame base
    // (`input` / `context`), which IS the aggregate's address.
    struct ProjectionRoot {
        bool is_local{false};
        std::uint32_t local{0};
        std::uint32_t base{0};
    };

    // An optional-returning companion to `reject`, for the helpers below that
    // must report a layout/edge failure while building an address.
    template <class T>
    [[nodiscard]] std::optional<T> reject_as(std::string message, ir::SourceRangeOpt range) {
        static_cast<void>(reject(std::move(message), std::move(range)));
        return std::nullopt;
    }

    // The final slot a projection chain lands on: its P4-D layout edge (which
    // selects the load/store width), the byte offset of the slot within the
    // address the chain leaves on the stack, and the P4-D offsets of the
    // intermediate aggregate fields the address walk dereferences.
    struct ProjectionSlot {
        CoreLayoutId edge{};
        std::uint32_t offset{0};
        std::vector<std::uint32_t> deref_offsets;
    };

    // RESOLVE a projection chain without emitting anything: validate every step's
    // owner/field against the P4-D layout and return the final slot plus the
    // intermediate dereference offsets. The plan pass uses this to decide the
    // leaf's shape (an aggregate leaf cannot persist into the durable context
    // frame); the emit pass replays `deref_offsets` to build the address. ONE
    // walk decides the shape and the encoding, so plan and emit cannot disagree.
    [[nodiscard]] std::optional<ProjectionSlot>
    resolve_projection_slot(const std::vector<ir::core::CoreProjectionStep> &projection,
                            CoreTypeId root_type,
                            ir::SourceRangeOpt range) {
        const auto *structure = p6_nominal_struct_layout(program_, layouts_, root_type);
        if (structure == nullptr) {
            return reject_as<ProjectionSlot>("projection root type has no finalized struct layout",
                                             range);
        }
        if (projection.empty()) {
            return reject_as<ProjectionSlot>("a projection chain requires a member step", range);
        }
        ProjectionSlot slot;
        for (std::size_t i = 0; i < projection.size(); ++i) {
            const ir::core::CoreProjectionStep &step = projection[i];
            if (step.field.value >= structure->field_offsets.size() ||
                step.field.value >= structure->field_layouts.size()) {
                return reject_as<ProjectionSlot>(
                    "projection step field id is out of range for its owner", range);
            }
            const std::uint64_t offset = structure->field_offsets[step.field.value];
            if (offset > std::numeric_limits<std::uint32_t>::max()) {
                return reject_as<ProjectionSlot>(
                    "aggregate field offset exceeds the wasm32 address domain", range);
            }
            const CoreLayoutId edge = structure->field_layouts[step.field.value];
            if (i + 1 == projection.size()) {
                slot.edge = edge;
                slot.offset = static_cast<std::uint32_t>(offset);
                return slot;
            }
            // A non-final step continues INTO a nested struct whose slot holds the
            // child aggregate's address.
            const auto *nested = p6_nominal_struct_layout(program_, layouts_, step.result_type);
            if (!place_is_aggregate_leaf(edge) || nested == nullptr) {
                return reject_as<ProjectionSlot>(
                    "projection step does not continue through a struct", range);
            }
            slot.deref_offsets.push_back(static_cast<std::uint32_t>(offset));
            structure = nested;
        }
        return reject_as<ProjectionSlot>("projection path has no step", range);
    }

    // Walk a projection chain, emitting the base ADDRESS of its final field slot:
    // push the root, then for every intermediate step advance by the field offset
    // and DEREFERENCE the slot (an aggregate field holds the child's address, the
    // ONE representation rule `emit_construct_store` writes). The caller emits
    // exactly ONE load (read) or ONE store (write) at the returned offset, so the
    // read and the write direction share one walk and cannot disagree about
    // whether an aggregate field is an inline value or an inline pointer.
    [[nodiscard]] std::optional<ProjectionSlot>
    emit_projection_slot(const std::vector<ir::core::CoreProjectionStep> &projection,
                         CoreTypeId root_type,
                         const ProjectionRoot &root,
                         ir::SourceRangeOpt range) {
        const auto slot = resolve_projection_slot(projection, root_type, range);
        if (slot == std::nullopt) {
            return std::nullopt;
        }
        if (root.is_local) {
            emit_local_get(root.local);
        } else {
            emit_const_i32(static_cast<std::int32_t>(root.base));
        }
        for (const std::uint32_t offset : slot->deref_offsets) {
            body_.byte(kOpI32Load);
            body_.u32(kAlignI32);
            body_.u32(offset);
        }
        return slot;
    }

    // The P6 kind of the object at a P4-D layout edge. A scalar keeps its width;
    // a tag-only enum edge is `Index` (its whole representation is the i32 tag at
    // offset 0, loaded/stored like a narrow Int); a STRUCT or a payload-bearing
    // enum is `Ptr` — its whole value IS its address; a bounded-collection edge
    // (RFC 0026 P6-5) is `Collection` — its whole value is the address of its
    // inline `(ptr,len)` header, the SAME one-word handle rule. Every other shape
    // (a PtrLen pair, bytes, f64) is NOT a single-word P6 value and maps to `Ptr`
    // only so the CALLER's leaf-repr check rejects it: a field read/store of such
    // a leaf must fail closed rather than truncate.
    //
    // RFC 0026 P6-8a: a CLOSURE has a finalized P4-D layout (the D2
    // `(func_index, env_ptr)` word pair), but NO P6 value model — P6 has no
    // function-table index value, no env field walk, and no `call_indirect`, so a
    // closure is NOT a single-word P6 value. It falls into the same "not a P6
    // value" arm as a PtrLen / bytes / f64 leaf: `place_is_scalar_leaf` and
    // `place_is_aggregate_leaf` both return false for it, so every consumer fails
    // closed rather than reading its first word. The two-word size is deliberately
    // NOT papered over by a scalar mapping — that would let a projection read half
    // a closure.
    // Whether a VALUE kind and a P4-D SLOT kind agree on PHYSICAL WIDTH. A slot
    // edge never carries its own Bool kind (every CoreScalarRepr::I32 edge maps
    // to IntI32 in `place_kind_of_layout`), while a Bool VALUE is P6ScalarKind::
    // Bool: the two share one i32 word with identical compare/load/store
    // opcodes, so Bool is width-compatible with an I32 slot. Every other pair
    // matches kind-for-kind — an i64 word never matches an i32 slot, and a Ptr
    // / Collection / Closure handle never matches a scalar (and vice versa).
    // This is the single width predicate the collection gates use, so the
    // typed verifier (which admits Bool keys/values) and the emitter can never
    // disagree about what the bounded scan can realize.
    [[nodiscard]] static bool same_word_width(P6ScalarKind value, P6ScalarKind slot) {
        if (value == slot) {
            return true;
        }
        const bool value_is_i32 =
            value == P6ScalarKind::Bool || value == P6ScalarKind::IntI32;
        return value_is_i32 && slot == P6ScalarKind::IntI32;
    }

    [[nodiscard]] P6ScalarKind place_kind_of_layout(CoreLayoutId layout_id) const {
        if (layout_id.value < layouts_.layouts.size()) {
            const ir::core::CoreLayout &layout = layouts_.layouts[layout_id.value];
            // FB-3b: the eight-byte (func_index, env_ptr) callable word pair.
            // It is deliberately NOT mapped to Ptr: it is two words and only
            // the closure constructor / call_indirect paths may touch it.
            if (std::holds_alternative<ir::core::CoreLayoutClosure>(layout.shape)) {
                return P6ScalarKind::Closure;
            }
            if (const auto *scalar = std::get_if<ir::core::CoreLayoutScalar>(&layout.shape)) {
                switch (scalar->repr) {
                case ir::core::CoreScalarRepr::I32:
                    return P6ScalarKind::IntI32;
                case ir::core::CoreScalarRepr::I64:
                    return P6ScalarKind::IntI64;
                case ir::core::CoreScalarRepr::F64:
                    break;
                }
            }
            if (const auto *tagged = std::get_if<ir::core::CoreLayoutEnum>(&layout.shape)) {
                if (std::ranges::all_of(tagged->variant_payload_sizes,
                                        [](std::uint64_t size) { return size == 0; })) {
                    return P6ScalarKind::Index;
                }
                return P6ScalarKind::Ptr;
            }
            if (std::holds_alternative<ir::core::CoreLayoutStruct>(layout.shape)) {
                return P6ScalarKind::Ptr;
            }
            if (std::holds_alternative<ir::core::CoreLayoutContainer>(layout.shape)) {
                return P6ScalarKind::Collection;
            }
        }
        return P6ScalarKind::Ptr;
    }

    // Whether a P4-D layout edge is a leaf the P6 memory model can load/store as
    // ONE word: an i32 / i64 scalar, or a tag-only enum (an i32 discriminant).
    // Everything else — a PtrLen String, bytes, f64, or a nested aggregate /
    // collection — is not a single-word field, so a leaf read/store of it
    // fails closed.
    [[nodiscard]] bool place_is_scalar_leaf(CoreLayoutId layout_id) const {
        const P6ScalarKind kind = place_kind_of_layout(layout_id);
        return kind == P6ScalarKind::IntI32 || kind == P6ScalarKind::IntI64 ||
               kind == P6ScalarKind::Index;
    }

    // Whether a P4-D layout edge is an ADDRESS-SHAPED leaf: a struct, a
    // payload-bearing enum, or a bounded collection (RFC 0026 P6-5), whose whole
    // P6 representation is an i32 address (of the aggregate's bytes, or of the
    // collection's inline header). A PtrLen String / bytes / f64 is NOT one — it
    // has no address the P6 model owns — so this is the predicate a store
    // destination uses to decide "one i32 slot" vs "fail closed".
    [[nodiscard]] bool place_is_aggregate_leaf(CoreLayoutId layout_id) const {
        if (layout_id.value >= layouts_.layouts.size()) {
            return false;
        }
        const ir::core::CoreLayout &layout = layouts_.layouts[layout_id.value];
        if (std::holds_alternative<ir::core::CoreLayoutStruct>(layout.shape) ||
            std::holds_alternative<ir::core::CoreLayoutContainer>(layout.shape)) {
            return true;
        }
        const auto *tagged = std::get_if<ir::core::CoreLayoutEnum>(&layout.shape);
        return tagged != nullptr &&
               !std::ranges::all_of(tagged->variant_payload_sizes,
                                    [](std::uint64_t size) { return size == 0; });
    }

    // Whether a P4-D layout edge is a single-word P6 value at all: a scalar, a
    // tag-only enum discriminant, or an address-shaped leaf.
    [[nodiscard]] bool place_is_p6_value(CoreLayoutId layout_id) const {
        return place_is_scalar_leaf(layout_id) || place_is_aggregate_leaf(layout_id);
    }

    // The sub-site a PAYLOAD slot of a variant pattern descends to: the P4-D
    // offset `payload_base + slot_offset`, with the slot's own P6 kind. This is
    // the ONE constructor of a payload sub-site, shared by the plan pass
    // (`plan_variant_pattern_site`) and the emit pass (`emit_variant_test`), so
    // the two can never disagree about which slots are addressable. A slot whose
    // P4-D edge is NOT a single-word P6 value — a PtrLen String / bytes / f64, or
    // a D2 closure (RFC 0026 P6-8a; two words with no P6 representation) — fails
    // closed here rather than latching / loading only its first word. The
    // projection / store / collection leaf paths already reject exactly these
    // edges through the same `place_is_p6_value` predicate; routing the
    // pattern-site construction through it makes the P6 boundary uniform instead
    // of relying on "no producer exists yet".
    [[nodiscard]] std::optional<P6PatternSite>
    payload_sub_site(const ir::core::CoreLayoutStruct &payload,
                     std::uint32_t slot,
                     std::uint64_t payload_base,
                     ir::SourceRangeOpt range) {
        if (slot >= payload.field_offsets.size() || slot >= payload.field_layouts.size()) {
            static_cast<void>(
                reject("variant payload sub-pattern slot is out of range", std::move(range)));
            return std::nullopt;
        }
        const CoreLayoutId edge = payload.field_layouts[slot];
        if (!place_is_p6_value(edge)) {
            static_cast<void>(
                reject("variant payload sub-pattern slot is not a single-word P6 value",
                       std::move(range)));
            return std::nullopt;
        }
        return P6PatternSite{
            place_kind_of_layout(edge), true, payload_base + payload.field_offsets[slot]};
    }

    // The root of a projection chain as a `ProjectionRoot`. A local-rooted chain
    // starts from the aggregate's own `Ptr` local; `input` / `context` start from
    // the reserved frame base, which IS the frame struct's address.
    [[nodiscard]] std::optional<ProjectionRoot> projection_root_of(const CorePathExpr &path) const {
        if (path.has_local) {
            const auto local = readable_local(path.local);
            if (local == std::nullopt || readable_kind(path.local) != P6ScalarKind::Ptr) {
                return std::nullopt;
            }
            return ProjectionRoot{true, *local, 0};
        }
        switch (path.root) {
        case ir::core::CorePathRoot::Input:
            return ProjectionRoot{false, 0, kP6AggregateInputBase};
        case ir::core::CorePathRoot::Context:
            return ProjectionRoot{false, 0, kP6AggregateContextBase};
        default:
            return std::nullopt;
        }
    }

    // The same root for a STORE place (`ctx.field = v`): only the context frame
    // is a legal destination in the P6 subset, so this is the single place that
    // decision lives.
    [[nodiscard]] std::optional<ProjectionRoot>
    store_root_of(const ir::core::CorePlace &place) const {
        if (place.root != ir::core::CorePathRoot::Context) {
            return std::nullopt;
        }
        return ProjectionRoot{false, 0, kP6AggregateContextBase};
    }

    // Allocate a fresh scratch slot for `value` in whichever pool the physical
    // repr needs. `pool` selects binding-vs-result for the diagnostics-free local
    // table; the slot itself comes from the shared cursor.
    [[nodiscard]] bool
    bind_scratch(CoreValueId value, P6ScalarKind kind, std::vector<LocalInfo> &pool) {
        if (value.value >= pool.size() || pool[value.value].bound) {
            return false;
        }
        LocalInfo &info = pool[value.value];
        info.bound = true;
        info.kind = kind;
        // A Closure is a TWO-word (func_index, env_ptr) pair; only one-word
        // kinds reach the monotone match-scratch pool in this slice, so pairs
        // are never allocated here (defensive guard).
        if (kind == P6ScalarKind::Closure) {
            return false;
        }
        info.slot = kind == P6ScalarKind::IntI64 ? scratch_i64_count_++ : scratch_i32_count_++;
        return true;
    }

    // Assign a pool slot to a let-bound value (one slot per CoreValueId; the
    // Core verifier proves flow-global single definition). A Closure value
    // occupies TWO consecutive i32 pool slots (func_index then env_ptr).
    [[nodiscard]] bool bind_value(CoreValueId value, P6ScalarKind kind) {
        if (value.value >= locals_.size() || locals_[value.value].bound) {
            return false;
        }
        LocalInfo &info = locals_[value.value];
        info.bound = true;
        info.kind = kind;
        if (kind == P6ScalarKind::IntI64) {
            info.slot = i64_count_++;
        } else {
            info.slot = i32_count_++;
            if (kind == P6ScalarKind::Closure) {
                info.is_word_pair = true;
                ++i32_count_; // env_ptr word
            }
        }
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
        if (id.value >= storage_.exprs.size()) {
            return reject("expression id is out of range for this flow", ir::SourceRangeOpt{});
        }
        used_exprs_[id.value] = true;
        const CoreExpr &expr = storage_.exprs[id.value];
        // FB-3b: closures construct / indirect-call through the funcref table.
        // They are TWO-word P6 values, so they are handled before the single
        // scalar-result gate below.
        if (const auto *closure = std::get_if<CoreClosureExpr>(&expr.node)) {
            return plan_closure_expr(*closure, expr);
        }
        if (const auto *call = std::get_if<CoreCallClosureExpr>(&expr.node)) {
            return plan_call_closure_expr(*call, expr);
        }
        if (scalar_kind(expr.result_type) == std::nullopt) {
            return reject("scalar expression has a non-scalar or f64 result type",
                          expr.source_range);
        }
        return std::visit(
            Overloaded{
                [&](const CoreLiteralExpr &) { return plan_literal(expr); },
                [&](const CoreValueRefExpr &r) {
                    used_values_[r.value.value] = true;
                    if (readable_kind(r.value) == std::nullopt) {
                        return reject("value reference is not a readable local in this handler",
                                      expr.source_range);
                    }
                    return true;
                },
                [&](const CoreUnaryExpr &u) { return plan_expr(u.operand); },
                [&](const CoreBinaryExpr &b) { return plan_expr(b.lhs) && plan_expr(b.rhs); },
                [&](const CorePathExpr &p) { return plan_path(p, expr.source_range); },
                [&](const CoreQualifiedExpr &q) {
                    return plan_qualified(id, q, expr.source_range);
                },
                [&](const CoreConstructExpr &c) {
                    return plan_construct(id, c, expr.source_range);
                },
                [&](const CoreCoerceExpr &c) { return plan_coerce(id, c, expr.source_range); },
                [&](const CoreCollectionExpr &c) {
                    return plan_collection(id, c, expr.source_range);
                },
                [&](const CoreUnsupportedExpr &) {
                    return reject("expression was not fully lowered to Core-IR", expr.source_range);
                },
                [&](const CoreCallExpr &c) { return plan_direct_call(c, expr); },
                // Defensive: the two closure nodes are handled above.
                [&](const CoreClosureExpr &) { return false; },
                [&](const CoreCallClosureExpr &) { return false; },
            },
            expr.node);
    }

    // FB-1 §6.1/§6.3: plan a statically-resolved direct call. The callee must
    // resolve to a body-bearing Fn instance whose wasm ordinal the reachability
    // pass assigned; every argument and the result must be a single P6 word
    // (i32/i64 scalar, aggregate i32 address, bounded-collection i32 handle —
    // String/PtrLen/f64 are exactly the words p6_scalar_kind rejects, so the
    // multi-word cross-boundary FN_CROSS_BOUNDARY_TYPE gate is shared with the
    // P6 scalar gate rather than reimplemented).
    [[nodiscard]] bool plan_direct_call(const CoreCallExpr &c, const CoreExpr &expr) {
        if (c.callee.value >= program_.instances.size()) {
            return reject("direct call target is out of range", expr.source_range);
        }
        const auto *payload =
            std::get_if<CoreFnInstance>(&program_.instances[c.callee.value].payload);
        if (payload == nullptr || payload->body.value == CoreFnId::kInvalid ||
            payload->body.value >= program_.fns.size()) {
            return reject("direct call target has no outlined fn body for the wasm computation lane",
                          expr.source_range);
        }
        if (!plan_call_operands(c.callee, c.args, expr.result_type, expr.source_range)) {
            return false;
        }
        return true;
    }

    // FB-4: the ordered-statement twin of plan_direct_call. Same callee-link +
    // single-word boundary validation; the result type comes from the owning
    // body's dense value-type table (the statement is not in the expr arena).
    [[nodiscard]] bool plan_call_stmt(const CoreCallStmt &c, ir::SourceRangeOpt range) {
        if (c.callee.value >= program_.instances.size()) {
            return reject("ordered call target is out of range", range);
        }
        const auto *payload =
            std::get_if<CoreFnInstance>(&program_.instances[c.callee.value].payload);
        if (payload == nullptr || payload->body.value == CoreFnId::kInvalid ||
            payload->body.value >= program_.fns.size()) {
            return reject("ordered call target has no outlined fn body for the wasm effect lane",
                          range);
        }
        const CoreValueTypeId result_ty =
            c.result.value < storage_.value_types.size()
                ? storage_.value_types[c.result.value]
                : CoreValueTypeId{};
        if (!plan_call_operands(c.callee, c.args, result_ty, range)) {
            return false;
        }
        // The call result is a bound SSA local (like a let result).
        const auto kind = scalar_kind(result_ty);
        if (kind == std::nullopt) {
            return reject("ordered call result has a non-scalar type", range);
        }
        if (!bind_value(c.result, *kind)) {
            return reject("ordered call result SSA value is bound more than once", range);
        }
        used_values_[c.result.value] = true;
        return true;
    }

    // FB-4: ensure the effect fn owns the three scratch locals an in-fn
    // capability call's multi-value (status,ptr,len) result uses. They live in
    // the SECOND i32 local group (after the i64 group) so the SSA/scratch pool
    // indices are unchanged, exactly like the bump temporaries.
    void ensure_capability_scratch() { capability_scratch_needed_ = true; }

    // Whether one P6 scalar kind is a single i32 word that matches the FIXED
    // capability tuple functype (i32,i32)->(i32,i32,i32). An aggregate
    // ADDRESS (Ptr), bounded-collection handle (Collection), Bool, bounded
    // Int (IntI32) and tag-only enum (Index) all cross as one i32 word. An
    // unbounded Int (IntI64) and f64 do NOT (64-bit), and a closure is a
    // two-word pair — those fail the ABI rather than emitting an invalid
    // module. A non-P6 type (nullopt: String PtrLen / bytes / f64) fails too.
    [[nodiscard]] static bool
    capability_abi_word_kind(std::optional<P6ScalarKind> kind) {
        if (kind == std::nullopt) {
            return false;
        }
        switch (*kind) {
        case P6ScalarKind::Bool:
        case P6ScalarKind::IntI32:
        case P6ScalarKind::Index:
        case P6ScalarKind::Ptr:
        case P6ScalarKind::Collection:
            return true;
        case P6ScalarKind::IntI64:
        case P6ScalarKind::Closure:
            return false;
        }
        return false;
    }

    // RFC 0026 FB-4 (design §5.3): plan a capability invocation INSIDE an
    // outlined fn body. Only fn mode admits it (a handler capability call keeps
    // its KR6.5 canonical-final shape). The opaque capability ABI is
    // (ptr,len) -> (status,ptr,len), so both the single argument and the result
    // must be single-word P6 values (an aggregate/collection i32 address /
    // handle). The capability must be in the module's planned import table.
    [[nodiscard]] bool plan_capability_call(const CoreCapabilityCallStmt &s,
                                            ir::SourceRangeOpt range) {
        if (!fn_mode_) {
            return reject("a direct capability invocation is legal only at an agent capability "
                          "final (or inside an outlined effect fn body)",
                          range);
        }
        if (s.capability.value >= program_.capabilities.size()) {
            return reject("in-fn capability call references an out-of-range capability", range);
        }
        // The import table is finalized AFTER every reachable fn is planned
        // (Phase B installs it), so import membership is enforced at emit time
        // (and by the Core verifier effect-authorization pass); plan validates
        // only the capability id and the opaque single-word boundary.
        if (s.args.size() != 1) {
            return reject("the capability ABI takes exactly one opaque (ptr,len) frame; an in-fn "
                          "capability call passes " +
                              std::to_string(s.args.size()) + " argument(s)",
                          range);
        }
        const CoreValueId arg = s.args[0];
        used_values_[arg.value] = true;
        if (arg.value >= storage_.value_types.size()) {
            return reject("in-fn capability argument id is out of range", range);
        }
        // The capability import has the FIXED functype
        // kTypeCapabilityTuple = (i32,i32) -> (i32,i32,i32): an opaque frame
        // address + length in, status + opaque frame ptr + length out. Every
        // crossing word is therefore a single i32 word. An Int64 scalar
        // (unbounded Int) / f64 would be read or bound as a 64-bit local and
        // produce an invalid module (local.get i64 where the import expects
        // i32), and a closure is a two-word pair — reject all of those here
        // with a diagnostic rather than emitting a module that fails wasm
        // validation.
        const auto arg_kind =
            p6_scalar_kind(program_, layouts_, storage_.value_types[arg.value]);
        if (!capability_abi_word_kind(arg_kind)) {
            return reject_with_code(
                core_wasm_diag::kUnsupportedCapabilityFrame,
                "in-fn capability argument is not a single i32-word opaque frame "
                "(an unbounded Int / Int64, f64, String or multi-word scalar cannot match the "
                "fixed (i32,i32) capability ABI; route the value through the agent's opaque "
                "frame type instead)",
                range);
        }
        const auto result_kind =
            p6_scalar_kind(program_, layouts_,
                           s.result.value < storage_.value_types.size()
                               ? storage_.value_types[s.result.value]
                               : CoreValueTypeId{});
        if (s.result.value >= storage_.value_types.size() ||
            !capability_abi_word_kind(result_kind)) {
            return reject_with_code(
                core_wasm_diag::kUnsupportedCapabilityFrame,
                "in-fn capability result is not a single i32-word opaque frame "
                "(an unbounded Int / Int64 or f64 result cannot match the fixed "
                "(i32,i32,i32) capability ABI)",
                range);
        }
        if (!bind_value(s.result, *result_kind)) {
            return reject("in-fn capability result SSA value is bound more than once", range);
        }
        used_values_[s.result.value] = true;
        // Reserve the (status, ptr, len) scratch locals for the multi-value
        // import result.
        ensure_capability_scratch();
        return true;
    }

    // Shared callee-link / single-word boundary validation for a direct fn call
    // (CoreCallExpr or CoreCallStmt): records the callee instance for the
    // reachability fixed point and rejects String/f64/multi-word
    // arguments/results and a closure RESULT (one-word functype limit).
    [[nodiscard]] bool plan_call_operands(CoreInstanceId callee,
                                          const std::vector<CoreValueId> &args,
                                          CoreValueTypeId result_type,
                                          ir::SourceRangeOpt range) {
        if (std::find(fn_callees_.begin(), fn_callees_.end(), callee) == fn_callees_.end()) {
            fn_callees_.push_back(callee);
        }
        for (const CoreValueId arg : args) {
            used_values_[arg.value] = true;
            if (arg.value >= storage_.value_types.size()) {
                return reject("call argument id is out of range", range);
            }
            const auto arg_kind =
                p6_scalar_kind(program_, layouts_, storage_.value_types[arg.value]);
            // A closure argument is legal: the unified functype expands it to
            // its two (func_index, env_ptr) words. Only genuinely non-P6 types
            // (String PtrLen / bytes / f64) cross no boundary in this slice.
            if (arg_kind == std::nullopt) {
                return reject("call argument is not a representable P6 boundary value "
                              "(String / f64 / multi-word types cannot cross an fn boundary)",
                              range);
            }
        }
        const auto result_kind = p6_scalar_kind(program_, layouts_, result_type);
        if (result_kind == std::nullopt || *result_kind == P6ScalarKind::Closure) {
            return reject("call result is not a single-word P6 value "
                          "(a closure / String / f64 / multi-word result cannot cross an fn "
                          "boundary in this slice)",
                          range);
        }
        return true;
    }

    // FB-3b: number of flat wasm parameter WORDS a boundary value occupies, and
    // the corresponding type bytes. A scalar / aggregate-address / collection
    // handle is one i32 (or i64) word; a Closure callable is the two-word
    // (func_index, env_ptr) pair. A non-P6 type (String PtrLen / bytes / f64)
    // has no representation and rejects.
    [[nodiscard]] static std::uint32_t boundary_word_count(P6ScalarKind kind) {
        return kind == P6ScalarKind::Closure ? 2u : 1u;
    }
    static void append_boundary_param_bytes(P6ScalarKind kind, std::vector<std::uint8_t> &out) {
        if (kind == P6ScalarKind::Closure) {
            out.push_back(kI32);
            out.push_back(kI32);
        } else {
            out.push_back(kind == P6ScalarKind::IntI64 ? kI64 : kI32);
        }
    }
    // A single RESULT word's type byte; nullopt for a closure (two-word) or
    // otherwise non-P6 result, which this slice cannot return.
    [[nodiscard]] static std::optional<std::uint8_t>
    boundary_result_byte(const std::optional<P6ScalarKind> &kind) {
        if (kind == std::nullopt || *kind == P6ScalarKind::Closure) {
            return std::nullopt;
        }
        return *kind == P6ScalarKind::IntI64 ? kI64 : kI32;
    }

    // Resolve the CoreVtFn signature behind a callable logical value: a
    // signature-site CoreVtFn (a parameter / binding) or a construction-site
    // CoreVtClosure's referenced signature. Index identity, never a name.
    [[nodiscard]] const ir::core::CoreVtFn *
    callable_signature_of(CoreValueTypeId type) const {
        if (type.value >= program_.value_types.size()) {
            return nullptr;
        }
        const CoreValueTypeNode &node = program_.value_types[type.value].node;
        if (const auto *fn = std::get_if<ir::core::CoreVtFn>(&node)) {
            return fn;
        }
        if (const auto *closure = std::get_if<ir::core::CoreVtClosure>(&node)) {
            if (closure->signature.value >= program_.value_types.size()) {
                return nullptr;
            }
            return std::get_if<ir::core::CoreVtFn>(
                &program_.value_types[closure->signature.value].node);
        }
        return nullptr;
    }

    // The P4-D environment aggregate of a lifted fn's closure type (the
    // indirect edge off its CoreLayoutClosure), located through the hash-consed
    // CoreVtClosure whose captures equal `fn.captures`. Null for an
    // ordinary / zero-capture fn. Codegen reads field offsets/size from THIS
    // layout; it never re-derives them.
    [[nodiscard]] const ir::core::CoreLayout *
    fn_environment_layout(const CoreFnDecl &fn) const {
        return p6_closure_environment_layout(program_, layouts_, fn);
    }
    [[nodiscard]] static const ir::core::CoreLayoutStruct *
    fn_environment_struct(const ir::core::CoreLayout *env_layout) {
        return p6_closure_environment_struct(env_layout);
    }

    // FB-3b: plan a closure construction. Validates the target fn, the
    // capture-arity/types against the fn's declared env signature, and meters
    // the runtime env bump (only a capturing closure allocates).
    [[nodiscard]] bool
    plan_closure_expr(const CoreClosureExpr &c, const CoreExpr &expr) {
        if (scalar_kind(expr.result_type) != P6ScalarKind::Closure) {
            return reject("closure construction result has a non-closure type", expr.source_range);
        }
        if (c.fn.value >= program_.fns.size()) {
            return reject("closure construction references an out-of-range fn", expr.source_range);
        }
        const CoreFnDecl &fn = program_.fns[c.fn.value];
        if (std::find(closure_targets_.begin(), closure_targets_.end(), fn.id) ==
            closure_targets_.end()) {
            closure_targets_.push_back(fn.id);
        }
        if (c.env.size() != fn.captures.size()) {
            return reject("closure capture count does not match the lifted fn's environment "
                          "signature",
                          expr.source_range);
        }
        for (std::uint32_t i = 0; i < c.env.size(); ++i) {
            const CoreValueId captured = c.env[i];
            used_values_[captured.value] = true;
            if (captured.value >= storage_.value_types.size() ||
                p6_scalar_kind(program_, layouts_,
                               storage_.value_types[captured.value]) == std::nullopt) {
                return reject("a captured closure value is not a representable P6 value "
                              "(String / f64 captures cross no boundary in this slice)",
                              expr.source_range);
            }
            if (storage_.value_types[captured.value] != fn.captures[i]) {
                return reject("closure capture value type does not match its environment slot",
                              expr.source_range);
            }
        }
        // Meter the env bump for capturing closures. The env aggregate is
        // module-arena memory that must outlive the creating activation, so it
        // is bumped at run time (never static scratch). The driver folds fn-body
        // bytes into the per-activation recursion budget; a handler bumps once
        // per step (its entry resets the heap).
        if (!fn.captures.empty()) {
            const ir::core::CoreLayout *env = fn_environment_layout(fn);
            if (env == nullptr || env->size == 0 ||
                env->size > std::numeric_limits<std::uint32_t>::max()) {
                return reject("capturing closure has no valid finalized environment layout",
                              expr.source_range);
            }
            closure_env_bytes_ +=
                static_cast<std::uint32_t>(align_up(env->size, 8));
        }
        return true;
    }

    // FB-3b: plan an indirect call through a closure value. Resolves the
    // callable signature, checks arity and every boundary word, and records the
    // expected call_indirect functype (deduplicated by the driver).
    [[nodiscard]] bool
    plan_call_closure_expr(const CoreCallClosureExpr &c, const CoreExpr &expr) {
        used_values_[c.callee.value] = true;
        if (c.callee.value >= storage_.value_types.size()) {
            return reject("indirect call callee id is out of range", expr.source_range);
        }
        const CoreValueTypeId callee_type = storage_.value_types[c.callee.value];
        if (p6_scalar_kind(program_, layouts_, callee_type) != P6ScalarKind::Closure) {
            return reject("indirect call callee is not a callable closure value",
                          expr.source_range);
        }
        const ir::core::CoreVtFn *signature = callable_signature_of(callee_type);
        if (signature == nullptr) {
            return reject("indirect call callee has no resolvable Fn signature",
                          expr.source_range);
        }
        if (c.args.size() != signature->params.size()) {
            return reject("indirect call argument arity does not match the closure signature",
                          expr.source_range);
        }
        ClosureCallType descriptor;
        descriptor.params.push_back(kI32); // leading env pointer
        for (std::uint32_t i = 0; i < c.args.size(); ++i) {
            const CoreValueId arg = c.args[i];
            used_values_[arg.value] = true;
            if (arg.value >= storage_.value_types.size()) {
                return reject("indirect call argument id is out of range", expr.source_range);
            }
            const auto arg_kind =
                p6_scalar_kind(program_, layouts_, storage_.value_types[arg.value]);
            if (arg_kind == std::nullopt) {
                return reject("indirect call argument is not a representable P6 boundary value "
                              "(String / f64 cross no boundary in this slice)",
                              expr.source_range);
            }
            const auto param_kind =
                p6_scalar_kind(program_, layouts_, signature->params[i]);
            if (param_kind == std::nullopt || *arg_kind != *param_kind) {
                return reject("indirect call argument kind does not match the closure signature",
                              expr.source_range);
            }
            append_boundary_param_bytes(*arg_kind, descriptor.params);
        }
        const auto result_kind =
            p6_scalar_kind(program_, layouts_, expr.result_type);
        const auto result_byte = boundary_result_byte(result_kind);
        if (result_byte == std::nullopt) {
            return reject("indirect call result is not a single-word P6 value "
                          "(a closure / String / f64 result cannot cross in this slice)",
                          expr.source_range);
        }
        descriptor.result = *result_byte;
        if (std::find(closure_call_types_.begin(), closure_call_types_.end(), descriptor) ==
            closure_call_types_.end()) {
            closure_call_types_.push_back(descriptor);
        }
        return true;
    }

    // Kind lookup that does not depend on the emit-only i64 base.
    [[nodiscard]] std::optional<P6ScalarKind> final_local_like(CoreValueId value) const {
        if (value.value >= locals_.size() || !locals_[value.value].bound) {
            return std::nullopt;
        }
        return locals_[value.value].kind;
    }

    [[nodiscard]] bool plan_literal(const CoreExpr &expr) {
        const auto kind = scalar_kind(expr.result_type);
        if (kind == std::nullopt) {
            return reject("scalar literal has a non-scalar or f64 result type", expr.source_range);
        }
        const auto &lit = std::get<CoreLiteralExpr>(expr.node);
        switch (lit.kind) {
        case CoreLiteralKind::Bool:
            if (*kind != P6ScalarKind::Bool) {
                return reject("bool literal has a non-Bool scalar type", expr.source_range);
            }
            return true;
        case CoreLiteralKind::Integer:
            if (*kind != P6ScalarKind::IntI32 && *kind != P6ScalarKind::IntI64) {
                return reject("integer literal has a non-Int scalar type", expr.source_range);
            }
            return true;
        case CoreLiteralKind::Unit:
            // FB-1: a Unit literal is the zero-word value; it has no runtime
            // representation and is legal only where a Unit result is expected
            // (an fn with no return value — not yet in the slice). Reject here to
            // keep the single-word boundary, matching the multi-word gate.
            return reject("a Unit literal has no single-word wasm representation in this slice",
                          expr.source_range);
        default:
            return reject("only Bool and Integer literals are in the scalar subset",
                          expr.source_range);
        }
    }

    // --- P6-4 aggregate memory planning ---
    //
    // A projection path is a READ: a chain of member steps from a root struct.
    // Planning meters the result kind (the last step's layout edge) and hands the
    // emit pass the typed step chain it must walk. The buffers differ by ROOT:
    //   * `Input`   -> the host-written input frame at `kP6AggregateInputBase`
    //   * `Context` -> the zero-initialised context frame at `kP6AggregateContextBase`
    //   * `Local`   -> an aggregate SSA value's address (its `Ptr` local)
    // A workflow-rooted / identifier / free-root path is outside the flow
    // computation subset, so it fails closed rather than reading garbage.
    [[nodiscard]] bool plan_path(const CorePathExpr &path, ir::SourceRangeOpt range) {
        if (!path.projection_resolved) {
            return reject("projection path is unresolved in an executable program", range);
        }
        if (!path.has_local && path.root != ir::core::CorePathRoot::Input &&
            path.root != ir::core::CorePathRoot::Context) {
            return reject("only input / context / local path roots are in the P6 subset", range);
        }
        for (std::uint32_t i = 0; i < path.projection.size(); ++i) {
            const ir::core::CoreProjectionStep &step = path.projection[i];
            const CoreTypeId expected =
                (i == 0) ? path.root_type : path.projection[i - 1].result_type;
            if (!plan_projection_owner(step.owner_type, expected, range)) {
                return false;
            }
        }
        // No member chain (a bare `input` / `ctx` / aggregate local) reads the
        // whole frame: that is legal only for an aggregate, and only when the
        // frame really lives in a buffer the module owns. A bare scalar root is
        // NOT a memory read — reject so a malformed IR never reads raw bytes.
        if (path.projection.empty()) {
            if (path.has_local) {
                if (readable_kind(path.local) != P6ScalarKind::Ptr) {
                    return reject("a bare local aggregate read requires a struct value", range);
                }
                return true;
            }
            return reject("a bare input/context root is not a memory read in the P6 subset", range);
        }
        return true;
    }

    // Whether `owner` is the struct a projection step walks and continues from the
    // expected type (the root type for the first step, the previous step's
    // declared result otherwise). The emit pass re-reads the exact edge from the
    // P4-D layout, so this is a fail-closed shape check, not a second authority.
    [[nodiscard]] bool
    plan_projection_owner(CoreTypeId owner, CoreTypeId expected, ir::SourceRangeOpt range) {
        if (owner.value >= program_.types.size() ||
            program_.types[owner.value].kind != CoreTypeDecl::Kind::Struct) {
            return reject("projection owner is not a struct type", range);
        }
        if (owner != expected) {
            return reject("projection step does not continue from the expected owner type", range);
        }
        if (p6_nominal_struct_layout(program_, layouts_, owner) == nullptr) {
            return reject("projection owner has no finalized struct layout", range);
        }
        return true;
    }

    // A constructor materializes an aggregate at a fresh scratch address. Planning
    // meters the address and validates every operand against its DECLARED field /
    // payload slot (identity, never source order — Principle 2), so emit only
    // stores the operands at the offsets the field identities name.
    [[nodiscard]] bool
    plan_construct(CoreExprId id, const CoreConstructExpr &construct, ir::SourceRangeOpt range) {
        if (id.value >= construct_addrs_.size() || construct_addrs_[id.value].has_value() ||
            dynamic_constructs_[id.value]) {
            return reject("constructor is planned more than once", range);
        }
        if (!construct.resolved) {
            return reject("constructor is unresolved in an executable program", range);
        }
        const auto result_kind = scalar_kind(storage_.exprs[id.value].result_type);
        if (result_kind != P6ScalarKind::Ptr) {
            return reject("constructor result is not an aggregate type", range);
        }
        if (!construct.is_enum_variant) {
            const auto *structure = p6_nominal_struct_layout(program_, layouts_, construct.type_id);
            if (structure == nullptr || structure->field_offsets.size() != construct.args.size()) {
                return reject("struct constructor does not match its declared field layout", range);
            }
            for (const CoreConstructArg &arg : construct.args) {
                if (arg.field.value >= structure->field_offsets.size()) {
                    return reject("struct constructor field id is out of range", range);
                }
                if (!plan_construct_operand(
                        arg.value, structure->field_layouts[arg.field.value], range)) {
                    return false;
                }
            }
        } else {
            if (construct.variant.value >=
                program_.types[construct.type_id.value].variants.size()) {
                return reject("enum variant constructor id is out of range", range);
            }
            const int kind = enum_payload_kind(construct.type_id, construct.variant);
            if (kind < 0) {
                return reject("enum variant constructor owner has no payload metadata", range);
            }
            if (kind == 0 && !construct.args.empty()) {
                return reject("unit enum variant constructor must have no payload args", range);
            }
            if (kind != 0) {
                if (construct.args.size() !=
                    enum_payload_arity(construct.type_id, construct.variant)) {
                    return reject("enum variant constructor payload arity does not match its "
                                  "declaration",
                                  range);
                }
            }
            const CoreValueTypeId construct_vt = storage_.exprs[id.value].result_type;
            const auto *tagged =
                p6_enum_value_layout(program_, layouts_, construct_vt, construct.type_id);
            if (tagged == nullptr) {
                return reject("enum variant constructor has no finalized instantiated layout",
                              range);
            }
            const CoreLayoutId payload_layout =
                construct.variant.value < tagged->variant_payload_layouts.size()
                    ? tagged->variant_payload_layouts[construct.variant.value]
                    : CoreLayoutId{};
            const ir::core::CoreLayoutStruct *payload = nullptr;
            if (payload_layout.value < layouts_.layouts.size()) {
                payload = std::get_if<ir::core::CoreLayoutStruct>(
                    &layouts_.layouts[payload_layout.value].shape);
            }
            for (const CoreConstructArg &arg : construct.args) {
                if (payload == nullptr) {
                    return reject("enum payload constructor has no struct payload layout", range);
                }
                if (arg.field.value >= payload->field_layouts.size()) {
                    return reject("enum payload constructor slot id is out of range", range);
                }
                if (!plan_construct_operand(
                        arg.value, payload->field_layouts[arg.field.value], range)) {
                    return false;
                }
            }
        }
        // Meter the construct storage LAST so a rejected constructor never
        // consumes capacity (plans stay deterministic regardless of reject
        // order). Outlined fn bodies allocate PER ACTIVATION at run time from
        // the checked bump heap (design §6.3): static scratch would alias
        // across repeated/nested native calls; entry handlers keep the
        // compile-time static arena.
        const std::uint64_t size = aggregate_size(construct.type_id);
        if (fn_mode_) {
            if (size == 0 || size > std::numeric_limits<std::uint32_t>::max()) {
                return reject("an fn aggregate constructor has an invalid size", range);
            }
            dynamic_constructs_[id.value] = true;
            dynamic_construct_bytes_ +=
                static_cast<std::uint32_t>(align_up(size, 8));
            return true;
        }
        const std::uint64_t start = align_up(scratch_addr_cursor_, 8);
        if (start + size > kP6AggregateScratchCapacity ||
            start + size > std::numeric_limits<std::uint32_t>::max()) {
            return reject("constructor scratch arena is exhausted", range);
        }
        construct_addrs_[id.value] = static_cast<std::uint32_t>(kP6AggregateScratchBase + start);
        scratch_addr_cursor_ = static_cast<std::uint32_t>(start + size);
        return true;
    }

    // Validate ONE constructor operand against its P4-D SLOT edge: a scalar slot
    // takes an i32/i64 operand of the same width, an addressable-aggregate slot
    // takes a `Ptr` operand, and any other slot (a String / collection / f64, or
    // an inline tag-only enum the P6 model cannot address) fails closed.
    [[nodiscard]] bool
    plan_construct_operand(CoreValueId value, CoreLayoutId slot, ir::SourceRangeOpt range) {
        if (value.value >= storage_.value_types.size()) {
            return reject("constructor operand value id is out of range", range);
        }
        const auto kind = scalar_kind(storage_.value_types[value.value]);
        if (kind == std::nullopt) {
            return reject("constructor operand has a non-aggregate, non-scalar type", range);
        }
        if (place_is_aggregate_leaf(slot)) {
            if (*kind != P6ScalarKind::Ptr) {
                return reject("constructor operand is not an aggregate for its aggregate slot",
                              range);
            }
            return true;
        }
        if (place_is_scalar_leaf(slot)) {
            // A scalar slot takes a scalar of the same PHYSICAL width. A tag-only
            // enum discriminant is an i32 and matches a narrow-Int slot; a Bool
            // and a narrow Int share the i32 repr. A `Ptr` operand into a scalar
            // slot (or vice versa) is a shape error.
            const bool slot_wide = place_kind_of_layout(slot) == P6ScalarKind::IntI64;
            const bool operand_wide = *kind == P6ScalarKind::IntI64;
            if (*kind == P6ScalarKind::Ptr || slot_wide != operand_wide) {
                return reject("constructor operand width does not match its slot", range);
            }
            return true;
        }
        return reject("constructor slot is not a single-word P6 value", range);
    }

    // --- P6-6 coercion planning (RFC 0026 P4 proof plans, physical effects) ---
    //
    // A `CoreCoerceExpr` consumes an existing SSA value and produces a fresh one.
    // The result is a NEW SSA value (the Core lowerer binds it via `bind_pure`),
    // so it takes an ordinary SSA local of the RESULT's kind, exactly like a let
    // whose expression happens to be a coercion. Planning:
    //   * proves the operand is readable and its recorded type is the plan root's
    //     SOURCE (the Core verifier also proves this — this is the fail-closed
    //     backend-side restatement that never trusts a partial arena),
    //   * walks every op of the root plan node through `p6_coercion_effect`, so an
    //     op with no P6 physical action rejects HERE with its own message.
    //
    // The RESULT's own P6 kind is not re-checked here: the `plan_expr` entry guard
    // already rejected a result with no `p6_scalar_kind` (a 2-word String, a
    // collection handle, an f64, a closure), so a coercion whose result P6 cannot
    // hold has failed closed one frame up. The enclosing `let` binds the result
    // id to a local of that same kind (`bind_value` reads the result type), so a
    // repr-growing widening lands in an i64 local.
    [[nodiscard]] bool
    plan_coerce(CoreExprId id, const CoreCoerceExpr &coerce, ir::SourceRangeOpt range) {
        if (id.value >= storage_.exprs.size()) {
            return reject("coercion expression id is out of range for this flow", range);
        }
        if (coerce.operand.value >= storage_.value_types.size()) {
            return reject("coercion operand value id is out of range", range);
        }
        if (coerce.plan.value >= storage_.coercion_plans.size()) {
            return reject("coercion plan id is out of range for this flow", range);
        }
        const CoreCoercionPlanNode &root = storage_.coercion_plans[coerce.plan.value];
        if (storage_.value_types[coerce.operand.value] != root.source) {
            return reject("coercion operand type does not equal its plan root source", range);
        }
        // Every op must name a physical action the P6 value model can perform.
        // (The result's own P6 kind is already checked by the `plan_expr` entry
        // guard before this visitor runs, so a 2-word String / collection / f64
        // coercion result has failed closed one frame up.)
        for (const CoreCoercionOp &op : root.ops) {
            std::string why;
            if (p6_coercion_effect(program_, layouts_, op.kind, root.source, root.result, why) ==
                std::nullopt) {
                return reject("coercion plan contains an unsupported op: " + why, range);
            }
        }
        return true;
    }

    [[nodiscard]] bool emit_coerce(const CoreCoerceExpr &coerce, ir::SourceRangeOpt range) {
        if (coerce.operand.value >= storage_.value_types.size() ||
            coerce.plan.value >= storage_.coercion_plans.size()) {
            return reject("coercion expression references an out-of-range id", std::move(range));
        }
        const CoreCoercionPlanNode &root = storage_.coercion_plans[coerce.plan.value];
        const auto source_kind = readable_kind(coerce.operand);
        if (source_kind == std::nullopt) {
            return reject("coercion operand is not a readable local in this handler",
                          std::move(range));
        }
        if (!emit_value_read(coerce.operand, range)) {
            return false;
        }
        // Apply each op's physical effect in canonical plan order. A `None` op is
        // a no-op on the stack (the same word is the result word); `ExtendI32` is
        // the one real instruction.
        for (const CoreCoercionOp &op : root.ops) {
            std::string why;
            const auto effect =
                p6_coercion_effect(program_, layouts_, op.kind, root.source, root.result, why);
            if (effect == std::nullopt) {
                return reject("coercion plan contains an unsupported op: " + why, std::move(range));
            }
            switch (*effect) {
            case P6CoercionEffect::None:
                break;
            case P6CoercionEffect::ExtendI32:
                if (*source_kind != P6ScalarKind::IntI32) {
                    return reject("i32 -> i64 widening requires an i32 operand", std::move(range));
                }
                body_.byte(kOpI64ExtendI32S);
                break;
            }
        }
        return true;
    }

    // Emit a bounded-collection operation. The handle in `base` is the address
    // of the inline `(ptr,len)` header; `stride` / `value_offset` / `capacity`
    // come from the P4-D `CoreLayoutContainer` (plan already proved it exists and
    // the element slot is one P6 word). A bounds failure traps via `unreachable`
    // — never a wild load/store.
    //
    //   Len        : base -> [len @4]                        (i32)
    //   ElementGet : base -> [ptr @0] + idx*stride + voffset -> load
    //   ElementSet : base -> [ptr @0] + idx*stride + voffset -> store(value);
    //                the result is the base handle (chainable, like a `+=`)
    //
    // `stride` is a power-of-two multiple of the element alignment (P4-D aligns
    // it up), so the index scaling is a `mul` and not a shift (a shift would
    // silently be wrong for a non-power-of-two stride such as a 3-byte element
    // padded to 4 — and the layout is the authority, never an assumption).
    [[nodiscard]] bool
    emit_collection(CoreExprId id, const CoreCollectionExpr &collection, ir::SourceRangeOpt range) {
        const auto base_kind = readable_kind(collection.base);
        if (base_kind != P6ScalarKind::Collection) {
            return reject("collection operation base is not a bounded collection value",
                          std::move(range));
        }
        const CoreValueTypeId base_type = storage_.value_types[collection.base.value];
        const ir::core::CoreLayoutContainer *container =
            p6_container_layout(program_, layouts_, base_type);
        if (container == nullptr) {
            return reject("collection operation base has no finalized container layout",
                          std::move(range));
        }
        if (collection.op == CoreCollectionOpKind::Len) {
            // The header length word is HOST-WRITTEN. Element access clamps its
            // address with index<capacity below, but a LENGTH is otherwise an
            // untrusted i32 the recursion lattice sealed against the STATIC
            // container capacity (core_recursion: a Len term bounds the rank by
            // the P4-D capacity). In an outlined fn body that length can drive a
            // recursion guard/entry, so clamp it to the sealed capacity here —
            // min(header_len, capacity) — using an UNSIGNED compare so a hostile
            // huge (or sign-negative) word is pinned to capacity instead of
            // driving unbounded native recursion. The clamp is restricted to fn
            // bodies: handler-mode bytes (E1-E3/P6) must stay byte-identical.
            const auto emit_len_load = [&]() -> bool {
                if (!emit_value_read(collection.base, range)) {
                    return false;
                }
                body_.byte(kOpI32Load); // len @ 4 (pointer @ 0 is the other word)
                body_.u32(kAlignI32);
                body_.u32(ir::core::kP6CollectionHeaderLenOffset);
                return true;
            };
            if (fn_mode_ &&
                container->capacity <=
                    static_cast<std::uint64_t>(
                        std::numeric_limits<std::int32_t>::max())) {
                // block (result i32): (len >u cap) ? cap : len
                body_.byte(kOpBlock);
                body_.byte(kI32);
                if (!emit_len_load()) {
                    return false;
                }
                emit_const_i32(static_cast<std::int32_t>(container->capacity));
                body_.byte(kOpI32GtU); // len >u cap
                body_.byte(kOpIf);
                body_.byte(kI32);
                emit_const_i32(static_cast<std::int32_t>(container->capacity));
                body_.byte(kOpElse);
                if (!emit_len_load()) {
                    return false;
                }
                body_.byte(kOpEnd); // if
                body_.byte(kOpEnd); // block
            } else if (!emit_len_load()) {
                return false;
            }
            // The header word is an i32 by construction; the LENGTH result is an
            // `Int`, which may be the wider i64 repr, so widen it. `i64.extend_
            // i32_s` is correct for a length (always >= 0 in a well-formed
            // container, so signed vs unsigned is indistinguishable, but the
            // signed form keeps ONE extension opcode in the module).
            const auto result_kind = scalar_kind(storage_.exprs[id.value].result_type);
            if (result_kind == P6ScalarKind::IntI64) {
                body_.byte(kOpI64ExtendI32S);
            }
            return true;
        }
        if (collection.op == CoreCollectionOpKind::KeyGet ||
            collection.op == CoreCollectionOpKind::Contains) {
            return emit_collection_key_get(collection, container, range);
        }
        const auto index_kind = readable_kind(collection.index);
        const auto element_slot = container->element;
        const bool element_wide = place_kind_of_layout(element_slot) == P6ScalarKind::IntI64;
        const std::uint32_t element_offset = static_cast<std::uint32_t>(container->value_offset);
        // The index must be a scalar Int. The bounds test itself is emitted
        // BEFORE the address arithmetic so a bad index cannot compute a wild
        // address even transiently; `stride` / `value_offset` are the P4-D facts.
        if (index_kind == std::nullopt ||
            (*index_kind != P6ScalarKind::IntI32 && *index_kind != P6ScalarKind::IntI64)) {
            return reject("collection element index is not a scalar Int", std::move(range));
        }
        const bool index_wide = *index_kind == P6ScalarKind::IntI64;
        // [in-bounds?] : (index >= 0) && (index < capacity) && (index < len).
        // `len` (the header's live count) gives the native semantics; `capacity`
        // (the layout count) is the region-safety net. A FALSE result TRAPS: the
        // test is negated (`i32.eqz`) so `if` takes the `unreachable` arm exactly
        // when the index is out of range (the same "negate the condition, trap in
        // the then-arm" discipline the match arm chain uses).
        if (!emit_element_bounds(collection, container->capacity, range)) {
            return false;
        }
        body_.byte(kOpI32Eqz);
        body_.byte(kOpIf);
        body_.byte(kEmptyBlock);
        body_.byte(kOpUnreachable);
        body_.byte(kOpEnd);
        // [address] : ptr @0 + index*stride + value_offset. The stack already
        // holds the `ptr` word from the header load; the index term is emitted
        // next and combined with ONE `i32.add`.
        if (!emit_value_read(collection.base, range)) {
            return false;
        }
        body_.byte(kOpI32Load);
        body_.u32(kAlignI32);
        body_.u32(ir::core::kP6CollectionHeaderPtrOffset);
        if (!emit_value_read(collection.index, range)) {
            return false;
        }
        // Widen a narrow index to the address arithmetic's domain. A wide index is
        // rejected below if it cannot be confined to wasm32; an i64 index is
        // wrapped to i32 only when the layout proves capacity fits i32 (the same
        // fact the bounds immediates rely on). A narrow index scales in i32.
        if (index_wide) {
            body_.byte(kOpI32WrapI64);
        }
        // Scale the index by the layout-derived stride, then add the Map value
        // offset. BOTH steps are `i32` arithmetic on the raw index: the P4-D
        // `stride` is an align-up multiple of the entry alignment, never assumed
        // to be a power of two, so an explicit `mul` is used rather than a shift
        // (a 3-byte element padded to a 4-byte stride would be silently wrong).
        const bool stride_one = container->stride == 1;
        if (!stride_one) {
            emit_const_i32(static_cast<std::int32_t>(container->stride));
            body_.byte(kOpI32Mul);
        }
        if (element_offset != 0) {
            emit_const_i32(static_cast<std::int32_t>(element_offset));
            body_.byte(kOpI32Add);
        }
        // Combine the header's `ptr` word with the scaled index term into the
        // final element address. This add is ALWAYS required: the stack holds two
        // independent values (the header pointer and the offset term), and the
        // single-word load/store that follows consumes exactly one address.
        body_.byte(kOpI32Add);
        if (collection.op == CoreCollectionOpKind::ElementGet) {
            body_.byte(element_wide ? kOpI64Load : kOpI32Load);
            body_.u32(element_wide ? kAlignI64 : kAlignI32);
            body_.u32(0);
            return true;
        }
        // Store: [address][value] so the address is already on the stack; the
        // operand is read next. An address-shaped element stores its child's
        // address (the ONE representation rule the plan already proved).
        if (!emit_value_read(collection.value, range)) {
            return false;
        }
        if (place_is_aggregate_leaf(element_slot)) {
            body_.byte(kOpI32Store);
            body_.u32(kAlignI32);
            body_.u32(0);
        } else {
            body_.byte(element_wide ? kOpI64Store : kOpI32Store);
            body_.u32(element_wide ? kAlignI64 : kAlignI32);
            body_.u32(0);
        }
        // The result is the base handle itself, so an element write chains.
        return emit_value_read(collection.base, std::move(range));
    }

    // CORE-GAPS: emit a bounded Map KEYED lookup. Leaves the matched VALUE word
    // on the stack; no match is a runtime trap (the wasm counterpart of the
    // evaluator's "key not found"). A linear scan walks the live entries:
    //
    //   cursor in [0, min(header_len, capacity)); entry i is at
    //   [ptr@0] + cursor*stride; its key is the first word and its value is at
    //   the P4-D value_offset.
    //
    // Three i32 scratch locals (cursor / found / current-entry address) are
    // shared by every KeyGet in a body: each scan completes and its result word
    // is consumed by the enclosing let before another can begin, so sequential
    // reuse never aliases a still-live scan. All facts come from the
    // CoreLayoutContainer; the loop is therefore structurally finite.
    [[nodiscard]] bool
    emit_collection_key_get(const CoreCollectionExpr &collection,
                            const ir::core::CoreLayoutContainer *container,
                            ir::SourceRangeOpt range) {
        const bool is_contains = collection.op == CoreCollectionOpKind::Contains;
        const bool key_wide = place_kind_of_layout(container->element) == P6ScalarKind::IntI64;
        // A membership test never reads the value slot; a Set container has no
        // value edge at all. A KeyGet on a Map reads the value at value_offset.
        const bool value_wide =
            container->value.has_value() &&
            place_kind_of_layout(*container->value) == P6ScalarKind::IntI64;

        const auto emit_header_ptr = [&]() -> bool {
            if (!emit_value_read(collection.base, range)) {
                return false;
            }
            body_.byte(kOpI32Load);
            body_.u32(kAlignI32);
            body_.u32(ir::core::kP6CollectionHeaderPtrOffset);
            return true;
        };
        const auto emit_header_len = [&]() -> bool {
            if (!emit_value_read(collection.base, range)) {
                return false;
            }
            body_.byte(kOpI32Load);
            body_.u32(kAlignI32);
            body_.u32(ir::core::kP6CollectionHeaderLenOffset);
            return true;
        };

        // Initialize the scan scratch: found=0, cursor=0.
        emit_const_i32(0);
        body_.byte(kOpLocalSet);
        body_.u32(keyget_found_local_);
        emit_const_i32(0);
        body_.byte(kOpLocalSet);
        body_.u32(keyget_cursor_local_);

        // B: the exit block. L: the scan loop. `br 1` from inside L leaves B.
        body_.byte(kOpBlock);
        body_.byte(kEmptyBlock);
        ++label_depth_;
        body_.byte(kOpLoop);
        body_.byte(kEmptyBlock);
        ++label_depth_;

        // if (found || cursor >=u min(len, capacity)) br 1
        body_.byte(kOpLocalGet);
        body_.u32(keyget_found_local_);
        body_.byte(kOpLocalGet);
        body_.u32(keyget_cursor_local_);
        // bound = min(header_len, capacity), one block (result i32). The clamp
        // is the same unsigned ladder Len uses in fn mode; it keeps a hostile
        // length word from driving the scan past the reserved backing region.
        body_.byte(kOpBlock);
        body_.byte(kI32);
        if (!emit_header_len()) {
            return false;
        }
        emit_const_i32(static_cast<std::int32_t>(container->capacity));
        body_.byte(kOpI32GtU);
        body_.byte(kOpIf);
        body_.byte(kI32);
        emit_const_i32(static_cast<std::int32_t>(container->capacity));
        body_.byte(kOpElse);
        if (!emit_header_len()) {
            return false;
        }
        body_.byte(kOpEnd);
        body_.byte(kOpEnd); // bound block
        body_.byte(kOpI32GeU);
        body_.byte(kOpI32Or);
        body_.byte(kOpBrIf);
        body_.u32(1); // leave B

        // entry = ptr + cursor*stride (held in the addr scratch; also the match
        // address when the key compares equal).
        if (!emit_header_ptr()) {
            return false;
        }
        body_.byte(kOpLocalGet);
        body_.u32(keyget_cursor_local_);
        if (container->stride != 1) {
            emit_const_i32(static_cast<std::int32_t>(container->stride));
            body_.byte(kOpI32Mul);
        }
        body_.byte(kOpI32Add);
        body_.byte(kOpLocalTee);
        body_.u32(keyget_addr_local_);

        // Compare the key word at entry+0 with the search key operand.
        body_.byte(kOpLocalGet);
        body_.u32(keyget_addr_local_);
        body_.byte(key_wide ? kOpI64Load : kOpI32Load);
        body_.u32(key_wide ? kAlignI64 : kAlignI32);
        body_.u32(0);
        if (!emit_value_read(collection.index, range)) {
            return false;
        }
        body_.byte(key_wide ? kOpI64Eq : kOpI32Eq);
        body_.byte(kOpIf);
        body_.byte(kEmptyBlock);
        ++label_depth_;
        emit_const_i32(1);
        body_.byte(kOpLocalSet);
        body_.u32(keyget_found_local_);
        body_.byte(kOpEnd);
        --label_depth_;

        // cursor += 1; continue L.
        body_.byte(kOpLocalGet);
        body_.u32(keyget_cursor_local_);
        emit_const_i32(1);
        body_.byte(kOpI32Add);
        body_.byte(kOpLocalSet);
        body_.u32(keyget_cursor_local_);
        body_.byte(kOpBr);
        body_.u32(0); // continue L
        body_.byte(kOpEnd); // L
        --label_depth_;
        body_.byte(kOpEnd); // B
        --label_depth_;

        // No match -> KeyGet TRAPS (the evaluator returns "key not found"); a
        // membership test is total and falls through to the found flag below.
        if (!is_contains) {
            body_.byte(kOpLocalGet);
            body_.u32(keyget_found_local_);
            body_.byte(kOpI32Eqz);
            body_.byte(kOpIf);
            body_.byte(kEmptyBlock);
            ++label_depth_;
            body_.byte(kOpUnreachable);
            body_.byte(kOpEnd);
            --label_depth_;

            // Matched value word at entry + value_offset; leave it on the stack.
            body_.byte(kOpLocalGet);
            body_.u32(keyget_addr_local_);
            body_.byte(value_wide ? kOpI64Load : kOpI32Load);
            body_.u32(value_wide ? kAlignI64 : kAlignI32);
            body_.u32(static_cast<std::uint32_t>(container->value_offset));
        } else {
            // Membership: leave the found flag (i32 0/1) on the stack.
            body_.byte(kOpLocalGet);
            body_.u32(keyget_found_local_);
        }
        return true;
    }

    // Emit the element-index bounds test as a single i32 flag, leaving the `if`
    // body to trap. The predicate is
    //
    //     (index >= 0) && (index < capacity) && (index < len)
    //
    // where `len` is the container header's OWN logical-count word
    // (`kP6CollectionHeaderLenOffset`), read at RUNTIME, and `capacity` is the
    // P4-D layout count the plan already proved fits the reserved region.
    //
    //   * `index < len` gives the NATIVE semantics (`builtin_list_raw_get` /
    //     `list_raw_set`: `idx >= items.size()` -> error; ABI doc "the current
    //     logical element count (<= capacity)"). For a WELL-FORMED header
    //     (`len <= capacity`, the documented invariant) it is the ONLY effective
    //     bound, so a read of a slot in `[len, capacity)` traps exactly as it
    //     fails natively.
    //   * `index < capacity` is the SAFETY net the plan's region proof needs: the
    //     TCB is the host-written frame, so a hostile `len > capacity` must not
    //     let `index * stride` scale past the reserved region and wrap. Since the
    //     plan proved `(capacity-1)*stride + element_size + value_offset` fits,
    //     this bound guarantees every formed address is in-region.
    //
    // The index width selects the comparison ladder. The plan pass already proved
    // the index is a scalar Int and the base is a bounded collection whose header
    // exists, so this reads the locals and the header word and emits only the
    // compares — it never re-derives a backing fact.
    [[nodiscard]] bool emit_element_bounds(const CoreCollectionExpr &collection,
                                           std::uint64_t capacity,
                                           ir::SourceRangeOpt range) {
        const auto index_kind = readable_kind(collection.index);
        if (index_kind == std::nullopt) {
            return reject("collection element index is not a readable value", std::move(range));
        }
        const bool wide = *index_kind == P6ScalarKind::IntI64;
        // (index >= 0) : not(index < 0)
        if (!emit_value_read(collection.index, range)) {
            return false;
        }
        if (wide) {
            emit_const_i64(0);
            body_.byte(kOpI64LtS);
        } else {
            emit_const_i32(0);
            body_.byte(kOpI32LtS);
        }
        body_.byte(kOpI32Eqz);
        const auto compare_index_less_than = [&](std::uint64_t bound) -> bool {
            if (!emit_value_read(collection.index, range)) {
                return false;
            }
            if (wide) {
                emit_const_i64(static_cast<std::int64_t>(bound));
                body_.byte(kOpI64LtS);
            } else {
                emit_const_i32(static_cast<std::int32_t>(bound));
                body_.byte(kOpI32LtS);
            }
            body_.byte(kOpI32And);
            return true;
        };
        // (index < capacity): the region-safety bound.
        if (!compare_index_less_than(capacity)) {
            return false;
        }
        // (index < len): the native-semantics bound, read from the header at
        // RUNTIME. The header word is an i32 by construction, so a wide index
        // compares in i64 — the word is extended with the same signed form
        // `emit_collection`'s Len result uses (a well-formed length is >= 0, so
        // signed vs unsigned is indistinguishable, but the module keeps ONE
        // extension opcode).
        if (!emit_value_read(collection.index, range)) {
            return false;
        }
        if (!emit_value_read(collection.base, range)) {
            return false;
        }
        body_.byte(kOpI32Load);
        body_.u32(kAlignI32);
        body_.u32(ir::core::kP6CollectionHeaderLenOffset);
        if (wide) {
            body_.byte(kOpI64ExtendI32S);
            body_.byte(kOpI64LtS);
        } else {
            body_.byte(kOpI32LtS);
        }
        body_.byte(kOpI32And);
        return true;
    }

    // --- P6-5 bounded-collection planning (RFC 0026 P6-5) ---
    //
    // A collection value's whole P6 representation is the i32 ADDRESS of its
    // inline `(ptr,len)` header. The plan pass:
    //   * proves the BASE is a readable `Collection` local whose P4-D layout is a
    //     `CoreLayoutContainer` (element layout, stride, capacity, value_offset,
    //     checked backing_size — the ONE authority; an UNBOUNDED collection has
    //     no container layout, so this fails closed exactly at `core.layout.
    //     UNBOUNDED`);
    //   * proves each extra operand exists, is readable, and matches the op's
    //     required type (an element index must be a scalar Int, an element value
    //     must match the element slot's kind);
    //   * rejects an element op whose element slot is NOT a single-word P6 value
    //     (a PtrLen String / collection / f64 element), so no load/store can
    //     truncate a wider element.
    // `Len` needs no further check beyond the base layout (its header word is an
    // i32 by construction).
    [[nodiscard]] bool
    plan_collection(CoreExprId id, const CoreCollectionExpr &collection, ir::SourceRangeOpt range) {
        if (id.value >= storage_.exprs.size()) {
            return reject("collection expression id is out of range for this flow", range);
        }
        const auto base_kind = readable_kind(collection.base);
        if (base_kind != P6ScalarKind::Collection) {
            return reject("collection operation base is not a bounded collection value", range);
        }
        const CoreValueTypeId base_type = storage_.value_types[collection.base.value];
        const ir::core::CoreLayoutContainer *container =
            p6_container_layout(program_, layouts_, base_type);
        if (container == nullptr) {
            return reject("collection operation base has no finalized container layout", range);
        }
        used_values_[collection.base.value] = true;
        if (collection.op == CoreCollectionOpKind::Len) {
            return true;
        }
        // The P4-D backing facts must be representable in the wasm32 address
        // domain. The emit path forms THREE i32 addresses from them and every one
        // must stay inside the reserved backing region
        // [kP6CollectionBackingBase, + kP6CollectionBackingCapacity):
        //
        //   ptr + index * stride + value_offset     (the element address)
        //   ptr + (capacity - 1) * stride + element_size + value_offset
        //                                           (one past the last slot)
        //
        // A per-field `<= INT32_MAX` test is NOT sufficient: it bounds the
        // factors, never the PRODUCTS. `index * stride` is emitted as `i32.mul`,
        // so a `stride * capacity` that crosses 2^32 wraps and a logical index
        // that passed the `[0, capacity)` compare — which compares the INDEX to
        // capacity, not to a scaled byte offset — would resolve to an in-page
        // address it has no right to. Compute both products with CHECKED 64-bit
        // arithmetic and require the whole scaled region to fit the ONE region
        // budget (a single SSOT constant, never a second copy of 65536). A
        // zero-capacity container is empty and passes trivially.
        const std::uint64_t element_size = container->element.value < layouts_.layouts.size()
                                               ? layouts_.layouts[container->element.value].size
                                               : 0;
        const std::uint64_t region = kP6CollectionBackingCapacity;
        const std::uint64_t capacity = container->capacity;
        const std::uint64_t stride = container->stride;
        const std::uint64_t value_offset = container->value_offset;
        if (stride > std::numeric_limits<std::uint32_t>::max() ||
            value_offset > std::numeric_limits<std::uint32_t>::max()) {
            return reject("collection backing facts exceed the wasm32 address domain", range);
        }
        if (capacity != 0) {
            // The largest address the emit path can form is the last slot's end:
            // (capacity - 1) * stride + element_size + value_offset. Checked in
            // uint64 so the product can never wrap before the region compare.
            const std::uint64_t max_index = capacity - 1;
            if (stride != 0 && max_index > std::numeric_limits<std::uint64_t>::max() / stride) {
                return reject("collection backing stride * capacity overflows the wasm32 region",
                              range);
            }
            const std::uint64_t scaled = max_index * stride;
            if (scaled > std::numeric_limits<std::uint64_t>::max() - element_size ||
                scaled + element_size > std::numeric_limits<std::uint64_t>::max() - value_offset) {
                return reject("collection backing stride * capacity overflows the wasm32 region",
                              range);
            }
            if (scaled + element_size + value_offset > region) {
                return reject("collection backing region exceeds the reserved linear-memory budget",
                              range);
            }
        }
        if (collection.op == CoreCollectionOpKind::KeyGet ||
            collection.op == CoreCollectionOpKind::Contains) {
            // Bounded KEYED scan over a Map (KeyGet / Contains) or a Set
            // (Contains only). The second operand is a SEARCH KEY; the KEY slot
            // must be a single-word scalar P6 value (Int/Bool/tag-only enum,
            // i32 or i64). A KeyGet additionally requires the VALUE slot to be
            // one word and checks its result width. Structural equality on
            // String / aggregate / collection keys has no single-word wasm
            // compare and stays fail-closed (P6-7 PtrLen frame work). The scan
            // is emitted as a bounded loop whose cursor is clamped by
            // min(header_len, capacity), so it is structurally finite.
            const CoreTypeDecl *base_decl = nullptr;
            if (base_type.value < program_.value_types.size()) {
                if (const auto *nom =
                        std::get_if<CoreVtNominal>(&program_.value_types[base_type.value].node)) {
                    if (nom->base.value < program_.types.size()) {
                        base_decl = &program_.types[nom->base.value];
                    }
                }
            }
            const bool is_map = base_decl != nullptr &&
                                base_decl->role == ir::core::CoreNominalRole::Map;
            if (base_decl == nullptr ||
                (collection.op == CoreCollectionOpKind::KeyGet && !is_map) ||
                !(base_decl->role == ir::core::CoreNominalRole::Map ||
                  base_decl->role == ir::core::CoreNominalRole::Set)) {
                return reject(
                    "keyed scan requires a Map (KeyGet) or Set/Map (Contains) base", range);
            }
            // A Set carries no value edge; a KeyGet requires one.
            if (collection.op == CoreCollectionOpKind::KeyGet &&
                !container->value.has_value()) {
                return reject("Map container layout has no value edge", range);
            }
            if (!place_is_p6_value(container->element)) {
                return reject("keyed scan key must be a single-word scalar P6 value", range);
            }
            if (collection.op == CoreCollectionOpKind::KeyGet &&
                !place_is_p6_value(*container->value)) {
                return reject(
                    "Map keyed lookup value must be a single-word scalar P6 value", range);
            }
            const auto key_kind = readable_kind(collection.index);
            if (key_kind == std::nullopt || *key_kind == P6ScalarKind::Ptr ||
                *key_kind == P6ScalarKind::Collection || *key_kind == P6ScalarKind::Closure) {
                return reject("keyed scan key is not a scalar Int/Bool/enum value", range);
            }
            if (!same_word_width(*key_kind, place_kind_of_layout(container->element))) {
                return reject("keyed scan key width does not match the key slot layout", range);
            }
            if (collection.op == CoreCollectionOpKind::KeyGet) {
                const auto value_kind_opt = scalar_kind(storage_.exprs[id.value].result_type);
                if (value_kind_opt == std::nullopt || *value_kind_opt == P6ScalarKind::Ptr ||
                    *value_kind_opt == P6ScalarKind::Collection) {
                    return reject("Map keyed lookup result is not a scalar P6 value", range);
                }
                if (!same_word_width(*value_kind_opt,
                                     place_kind_of_layout(*container->value))) {
                    return reject(
                        "Map keyed lookup result width does not match the value slot layout",
                        range);
                }
            }
            // The scan walks the full entry span; it must fit the reserved
            // backing region for both container shapes.
            if (capacity != 0) {
                if (stride == 0 ||
                    capacity > std::numeric_limits<std::uint64_t>::max() / stride ||
                    capacity * stride > region) {
                    return reject("container backing region exceeds the reserved memory budget",
                                  range);
                }
            }
            used_values_[collection.index.value] = true;
            keyget_scratch_needed_ = true;
            return true;
        }
        // An element slot must be ONE word the P6 memory model can load/store: a
        // scalar, a tag-only enum, or an address-shaped leaf (the element's own
        // layout edge decides which, exactly like an aggregate field). Without
        // this the single-word `load` / `store` below would silently truncate a
        // wider element (a PtrLen String / bytes / f64, a nested aggregate, or a
        // collection handle). The typed entry point already gates the element's
        // LOGICAL value type upstream, so for a lowered program this is the
        // layout-edge restatement — but the POSITIVE forms it decides (an
        // address-shaped aggregate element, whose result kind IS a defined P6
        // value) are reached through it, so it stays the single place the
        // one-word rule is enforced for the store path too.
        if (!place_is_p6_value(container->element)) {
            return reject("collection element is not a single-word P6 value", range);
        }
        const auto index_kind = readable_kind(collection.index);
        if (index_kind == std::nullopt ||
            (*index_kind != P6ScalarKind::IntI32 && *index_kind != P6ScalarKind::IntI64)) {
            return reject("collection element index is not a scalar Int", range);
        }
        used_values_[collection.index.value] = true;
        if (collection.op == CoreCollectionOpKind::ElementGet) {
            return true;
        } // An element WRITE must match the element slot's kind. An address-shaped
        // element takes the child's address (the ONE representation rule); a
        // scalar slot takes a scalar of the same physical width.
        const auto value_kind = readable_kind(collection.value);
        if (value_kind == std::nullopt) {
            return reject("collection element write value is not a readable value", range);
        }
        if (place_is_aggregate_leaf(container->element)) {
            if (*value_kind != place_kind_of_layout(container->element)) {
                return reject("collection element write value is not the element's address", range);
            }
        } else if (!same_word_width(*value_kind,
                                     place_kind_of_layout(container->element))) {
            return reject("collection element write value kind does not match the element slot",
                          range);
        }
        used_values_[collection.value.value] = true;
        return true;
    }

    // The P4-D size of an aggregate nominal (0 when the layout is missing).
    [[nodiscard]] std::uint64_t aggregate_size(CoreTypeId type) const {
        return p6_nominal_size(program_, layouts_, type).value_or(0);
    }

    // 0: unit payload, 1: tuple payload, 2: struct payload, -1: no metadata.
    [[nodiscard]] int enum_payload_kind(CoreTypeId type, CoreVariantId variant) const {
        if (type.value >= program_.types.size()) {
            return -1;
        }
        const CoreTypeDecl &decl = program_.types[type.value];
        if (variant.value >= decl.variant_payloads.size()) {
            return -1;
        }
        switch (decl.variant_payloads[variant.value].kind) {
        case CoreTypeDecl::VariantPayload::Kind::Unit:
            return 0;
        case CoreTypeDecl::VariantPayload::Kind::Tuple:
            return 1;
        case CoreTypeDecl::VariantPayload::Kind::Struct:
            return 2;
        }
        return -1;
    }

    [[nodiscard]] std::uint32_t enum_payload_arity(CoreTypeId type, CoreVariantId variant) const {
        if (type.value >= program_.types.size()) {
            return 0;
        }
        const CoreTypeDecl &decl = program_.types[type.value];
        if (variant.value >= decl.variant_payloads.size()) {
            return 0;
        }
        return static_cast<std::uint32_t>(
            decl.variant_payloads[variant.value].slot_type_template_roots.size());
    }

    [[nodiscard]] static std::uint64_t align_up(std::uint64_t value, std::uint64_t align) {
        return (value + align - 1) & ~(align - 1);
    }

    // A qualified unit variant (`Level::High`) is a constructor-like value: for a
    // TAG-ONLY enum its whole value is the i32 discriminant (a plain constant),
    // and for a PAYLOAD-BEARING enum it is an ADDRESSED aggregate whose tag is at
    // offset 0 and whose payload slots are zero (P6 has no default materializer
    // yet, so a unit variant of a payload enum must have an all-unit payload to be
    // lowerable). Planning meters the scratch address and records it, so emit only
    // stores the tag.
    [[nodiscard]] bool
    plan_qualified(CoreExprId id, const CoreQualifiedExpr &q, ir::SourceRangeOpt range) {
        if (!q.resolved) {
            return reject("qualified variant is not resolved to a typed enum", range);
        }
        if (q.type_id.value >= program_.types.size() ||
            q.variant.value >= program_.types[q.type_id.value].variants.size()) {
            return reject("qualified variant identity is out of range", range);
        }
        if (p6_is_tag_only_enum(program_, layouts_, storage_.exprs[id.value].result_type)) {
            return true; // a plain i32 discriminant constant
        }
        if (!p6_is_aggregate(program_, layouts_, storage_.exprs[id.value].result_type)) {
            return reject("qualified variant requires an enum type", range);
        }
        const int kind = enum_payload_kind(q.type_id, q.variant);
        if (kind != 0) {
            return reject("a payload-bearing variant cannot be a unit qualified value", range);
        }
        if (id.value >= construct_addrs_.size() || construct_addrs_[id.value].has_value() ||
            dynamic_constructs_[id.value]) {
            return reject("qualified variant is planned more than once", range);
        }
        const std::uint64_t size = aggregate_size(q.type_id);
        if (fn_mode_) {
            if (size == 0 || size > std::numeric_limits<std::uint32_t>::max()) {
                return reject("an fn aggregate qualified variant has an invalid size", range);
            }
            dynamic_constructs_[id.value] = true;
            dynamic_construct_bytes_ += static_cast<std::uint32_t>(size);
            return true;
        }
        const std::uint64_t start = align_up(scratch_addr_cursor_, 8);
        if (start + size > kP6AggregateScratchCapacity ||
            start + size > std::numeric_limits<std::uint32_t>::max()) {
            return reject("constructor scratch arena is exhausted", range);
        }
        construct_addrs_[id.value] = static_cast<std::uint32_t>(kP6AggregateScratchBase + start);
        scratch_addr_cursor_ = static_cast<std::uint32_t>(start + size);
        return true;
    }

    //
    // Planning a match carves every arm binding and (for an expression match) the
    // match result a scratch local, validates each arm's pattern against the
    // scrutinee's kind, and recurses into the guard / body / fallback regions so
    // their own lets and nested matches are planned too. Emission only reads the
    // slots, so the whole scratch-local layout is fixed before the first byte of
    // the function body is written — a wasm function declares its locals up front.

    [[nodiscard]] bool plan_match(const CoreMatchStmt &match, ir::SourceRangeOpt range) {
        const auto scrutinee_kind = readable_kind(match.scrutinee);
        if (scrutinee_kind == std::nullopt) {
            return reject("match scrutinee is not a readable local in this handler", range);
        }
        used_values_[match.scrutinee.value] = true;
        // A scrutinee is a scalar or a tag-only enum: a literal / Bool pattern
        // tests the value itself, a variant pattern tests the i32 tag. Each
        // pattern node re-checks which of the two it may consume.

        if (match.has_result) {
            if (match.result.value >= match_result_locals_.size() ||
                match_result_locals_[match.result.value].bound) {
                return reject("match result is bound more than once", range);
            }
            const auto result_kind = scalar_kind(storage_.value_types[match.result.value]);
            if (result_kind == std::nullopt || *result_kind == P6ScalarKind::Index) {
                return reject("expression match result must have a scalar or aggregate type",
                              range);
            }
            if (!bind_scratch(match.result, *result_kind, match_result_locals_)) {
                return reject("match result scratch local could not be allocated", range);
            }
            used_values_[match.result.value] = true;
        }

        // The scrutinee's ROOT site: a scalar sits in its local; an aggregate is
        // addressed (offset 0 of its own address).
        const P6PatternSite root_site{*scrutinee_kind, *scrutinee_kind == P6ScalarKind::Ptr, 0};
        // The scrutinee's CONCRETE value type (e.g. `Option<Int>`): a generic
        // enum's instantiated payload layout is reachable only through it.
        const CoreValueTypeId scrutinee_vt = storage_.value_types[match.scrutinee.value];

        for (const CoreMatchArm &arm : match.arms) {
            for (const CorePatternBinding &binding : arm.bindings) {
                if (binding.value.value >= storage_.value_types.size()) {
                    return reject("match arm binding value id is out of range", range);
                }
                const auto binding_kind = scalar_kind(storage_.value_types[binding.value.value]);
                if (binding_kind == std::nullopt || *binding_kind == P6ScalarKind::Index) {
                    return reject("match arm binding has a non-scalar or f64 type", range);
                }
                if (!bind_scratch(binding.value, *binding_kind, binding_locals_)) {
                    return reject("match arm binding scratch local could not be allocated", range);
                }
                used_values_[binding.value.value] = true;
            }
            if (!plan_arm_pattern(arm.pattern,
                                  root_site,
                                  arm.bindings,
                                  /*allow_payload_bindings=*/true,
                                  range,
                                  scrutinee_vt)) {
                return false;
            }
            // A guard region is present iff the source arm wrote `if <guard>`.
            if (arm.guard_region && !plan_match_region(*arm.guard_region, range)) {
                return false;
            }
            if (!arm.body) {
                return reject("match arm has no body region", range);
            }
            if (!plan_match_region(*arm.body, range)) {
                return false;
            }
        }
        if (!match.fallback_region) {
            return reject("match has no fallback region", range);
        }
        return plan_match_region(*match.fallback_region, range);
    }

    // Plan a match-consumed region: a guard, an arm body, or the fallback. Its
    // statements are ordinary P6 statements EXCEPT a trailing `CoreYieldStmt` —
    // that is the region's completion hand-off to the enclosing match (the
    // guard's Bool, the expression arm's value, or a statement arm's unit), which
    // the emitter consumes, so it must not be planned as a flow-region yield.
    [[nodiscard]] bool plan_match_region(const CoreRegion &region, ir::SourceRangeOpt range) {
        for (std::size_t index = 0; index < region.statements.size(); ++index) {
            const CoreStmt &statement = region.statements[index];
            if (const auto *yield = std::get_if<CoreYieldStmt>(&statement.node)) {
                if (index + 1 != region.statements.size()) {
                    return reject("a match region's yield must be its final statement",
                                  statement.source_range);
                }
                if (yield->has_value) {
                    used_values_[yield->value.value] = true;
                    if (readable_kind(yield->value) == std::nullopt) {
                        return reject("match region yields a value that is not a readable local",
                                      statement.source_range);
                    }
                }
                continue;
            }
            if (!plan_statement(statement)) {
                return false;
            }
        }
        (void)range;
        return true;
    }

    // A pattern site: WHERE in the scrutinee a pattern is tested. The root site
    // is the scrutinee itself (a scalar local, or address 0 of an aggregate's
    // address). A payload sub-pattern descends to `base + payload_offset +
    // field_offset`, so the same test/latch code covers a level-1 payload field
    // and a level-N nested one — only the accumulated offset differs.

    // Validate a pattern tree against the subset and the scrutinee's kind, and
    // record every binding's SITE. Every alternative is COMPILE-TIME-EXHAUSTIVE
    // over CorePatternNode's seven arms, so a pattern kind that is not yet
    // lowerable rejects with a specific message rather than silently matching
    // nothing. `allow_payload_bindings` is false inside an or-pattern: two
    // alternatives may not bind one name at two different payload offsets, so a
    // payload binding under an or fails closed rather than aliasing.
    //
    // `bindings` is the enclosing ARM's binding list: a `CoreBindingPat` names
    // its binding by `CorePatternBindingId`, an index into that list, and this
    // function resolves it to the binding's CoreValueId before recording the
    // site. That is the ONE place the two index spaces meet; everything the emit
    // pass reads is keyed by value id.
    [[nodiscard]] bool plan_arm_pattern(CorePatternId id,
                                        P6PatternSite site,
                                        const std::vector<CorePatternBinding> &bindings,
                                        bool allow_payload_bindings,
                                        ir::SourceRangeOpt range,
                                        std::optional<CoreValueTypeId> concrete_enum =
                                            std::nullopt) {
        if (id.value >= storage_.patterns.size()) {
            return reject("pattern id is out of range for this flow", range);
        }
        const CorePattern &pattern = storage_.patterns[id.value];
        return std::visit(
            Overloaded{
                [&](const CoreWildcardPat &) { return true; },
                [&](const CoreBindingPat &b) {
                    // `x` and `x @ nested`: the binding is latched from its SITE
                    // before the test, so a nested pattern is tested after it, on
                    // the same site.
                    if (!allow_payload_bindings && (site.in_memory || site.offset != 0)) {
                        return reject("or-pattern alternatives may not bind a payload slot", range);
                    }
                    // An AGGREGATE PAYLOAD SLOT (a struct / payload-bearing
                    // enum slot inside an addressed enum's payload, necessarily
                    // at a nonzero in-memory offset) holds the CHILD aggregate's
                    // ADDRESS as one stored i32 word (the ONE-representation
                    // rule `emit_construct_store` writes). The Ptr latch
                    // (`emit_site_value`) computes the SLOT's own address
                    // instead of loading that word, so the binding aliases the
                    // payload slot and a later field projection reads the wrong
                    // scratch — a silently wrong branch with no trap (the
                    // expression-match aggregate arm-copy gap). A Collection
                    // slot is different: its latch LOADS the stored one-word
                    // handle, so it is not gated here. The ROOT enum address
                    // (offset 0) is also sound: it IS the scrutinee value, so a
                    // whole-enum binding stays allowed. Fail closed until the
                    // aggregate payload arm-copy lands; the lowerer rejects this
                    // same shape for unwrap and this is the typed backstop for a
                    // hand-built match.
                    if (site.in_memory && site.offset != 0 &&
                        site.kind == P6ScalarKind::Ptr) {
                        return reject(
                            "binding an aggregate enum payload slot is not in the P6 match "
                            "subset (expression-match aggregate arm-copy is not yet supported)",
                            range);
                    }
                    if (b.binding.value >= bindings.size()) {
                        return reject("pattern binding id is out of range for this arm", range);
                    }
                    const CoreValueId value = bindings[b.binding.value].value;
                    if (value.value >= binding_sites_.size()) {
                        return reject("arm binding value id is out of range", range);
                    }
                    binding_sites_[value.value] = site;
                    return !b.has_nested ||
                           plan_arm_pattern(
                               b.nested, site, bindings, allow_payload_bindings, range,
                               std::nullopt);
                },
                [&](const CoreLiteralPat &lit) {
                    if (site.in_memory) {
                        return reject("literal pattern requires a scalar scrutinee", range);
                    }
                    return plan_literal_pattern(lit, site.kind, range);
                },
                [&](const CoreIntRangePat &r) {
                    if (site.in_memory) {
                        return reject("int-range pattern requires a scalar scrutinee", range);
                    }
                    if (site.kind == P6ScalarKind::Bool || site.kind == P6ScalarKind::Index) {
                        return reject("int-range pattern requires an Int scrutinee", range);
                    }
                    if (r.start > r.end) {
                        return reject("int-range pattern has start greater than end", range);
                    }
                    return true;
                },
                [&](const CoreVariantPat &v) {
                    return plan_variant_pattern_site(
                        v, site, bindings, allow_payload_bindings, range, concrete_enum);
                },
                [&](const CoreOrPat &o) {
                    if (o.alternatives.size() < 2) {
                        return reject("or-pattern must have at least two alternatives", range);
                    }
                    return std::ranges::all_of(o.alternatives, [&](CorePatternId alt) {
                        return plan_arm_pattern(alt,
                                                site,
                                                bindings,
                                                /*allow_payload_bindings=*/false,
                                                range,
                                                concrete_enum);
                    });
                },
                [&](const CoreTuplePat &t) { return plan_tuple_pattern_site(t, site, range); },
            },
            pattern.node);
    }

    // A variant pattern: the scrutinee must be a tag-only enum (an `Index` value)
    // or an ADDRESSED enum. With a payload, the tag is at the address and each
    // sub-pattern descends to `payload_offset + slot_offset`.
    [[nodiscard]] bool plan_variant_pattern_site(const CoreVariantPat &v,
                                                 P6PatternSite site,
                                                 const std::vector<CorePatternBinding> &bindings,
                                                 bool allow_payload_bindings,
                                                 ir::SourceRangeOpt range,
                                                 std::optional<CoreValueTypeId> concrete_enum) {
        if (v.owner_enum.value >= program_.types.size()) {
            return reject("variant pattern owner type id is out of range", range);
        }
        const CoreTypeDecl &decl = program_.types[v.owner_enum.value];
        if (v.variant.value >= decl.variants.size()) {
            return reject("variant pattern variant id is out of range", range);
        }
        const bool has_subpatterns = !v.tuple_subpatterns.empty() || !v.struct_fields.empty();
        // A tag-only enum as a VALUE carries no payload, so a payload sub-pattern
        // there is a shape error, never a silent no-op.
        if (!site.in_memory) {
            if (site.kind != P6ScalarKind::Index) {
                return reject("variant pattern requires an enum scrutinee", range);
            }
            if (has_subpatterns) {
                return reject("variant payload pattern requires an addressed enum scrutinee",
                              range);
            }
            return true;
        }
        if (site.kind != P6ScalarKind::Ptr) {
            return reject("variant pattern requires an enum scrutinee", range);
        }
        const auto *tagged = concrete_enum.has_value()
                                 ? p6_enum_value_layout(program_, layouts_, *concrete_enum,
                                                        v.owner_enum)
                                 : p6_nominal_enum_layout(program_, layouts_, v.owner_enum);
        if (tagged == nullptr || v.variant.value >= tagged->variant_payload_layouts.size()) {
            return reject("variant pattern owner has no finalized enum layout", range);
        }
        if (!has_subpatterns) {
            return true; // a unit variant matches on the tag alone
        }
        const CoreLayoutId payload_layout = tagged->variant_payload_layouts[v.variant.value];
        const ir::core::CoreLayoutStruct *payload = nullptr;
        if (payload_layout.value < layouts_.layouts.size()) {
            payload = std::get_if<ir::core::CoreLayoutStruct>(
                &layouts_.layouts[payload_layout.value].shape);
        }
        if (payload == nullptr) {
            return reject("variant payload pattern has no struct payload layout", range);
        }
        const std::uint64_t payload_base = site.offset + tagged->payload_offset;
        for (std::uint32_t i = 0; i < v.tuple_subpatterns.size(); ++i) {
            const auto sub = payload_sub_site(*payload, i, payload_base, range);
            if (sub == std::nullopt) {
                return false;
            }
            if (!plan_arm_pattern(
                    v.tuple_subpatterns[i], *sub, bindings, allow_payload_bindings, range,
                    std::nullopt)) {
                return false;
            }
        }
        for (const CoreVariantPatField &field : v.struct_fields) {
            const auto sub = payload_sub_site(*payload, field.slot.value, payload_base, range);
            if (sub == std::nullopt) {
                return false;
            }
            if (!plan_arm_pattern(field.pattern, *sub, bindings, allow_payload_bindings, range,
                                  std::nullopt)) {
                return false;
            }
        }
        return true;
    }

    // An anonymous tuple pattern is not in the P6 aggregate subset: no Core path
    // / construct in this slice materializes a tuple value (Sema's match scrutinee
    // is an enum), so a tuple pattern fails closed rather than matching nothing.
    [[nodiscard]] bool
    plan_tuple_pattern_site(const CoreTuplePat &, P6PatternSite, ir::SourceRangeOpt range) {
        return reject("tuple patterns are not in the P6 aggregate subset", std::move(range));
    }

    [[nodiscard]] bool plan_literal_pattern(const CoreLiteralPat &lit,
                                            P6ScalarKind scrutinee_kind,
                                            ir::SourceRangeOpt range) {
        switch (lit.kind) {
        case CoreLiteralKind::Bool:
            return scrutinee_kind == P6ScalarKind::Bool ||
                   reject("bool pattern requires a Bool scrutinee", range);
        case CoreLiteralKind::Integer:
            if (scrutinee_kind == P6ScalarKind::Bool || scrutinee_kind == P6ScalarKind::Index) {
                return reject("integer pattern requires an Int scrutinee", range);
            }
            return parse_unsigned_spelling(lit.spelling).has_value() ||
                   reject("integer pattern spelling does not parse", range);
        default:
            return reject("only Bool and Integer patterns are in the scalar subset", range);
        }
    }

    [[nodiscard]] bool plan_statement(const CoreStmt &statement) {
        return std::visit(
            Overloaded{
                [&](const CoreLetStmt &s) {
                    if (s.expr.value >= storage_.exprs.size()) {
                        return reject("let references an out-of-range expression",
                                      statement.source_range);
                    }
                    const CoreExpr &bound = storage_.exprs[s.expr.value];
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
                [&](const CoreMatchStmt &s) { return plan_match(s, statement.source_range); },
                [&](const CoreCapabilityCallStmt &s) {
                    return plan_capability_call(s, statement.source_range);
                },
                // RFC 0026 FB-4: ordered effectful fn call.
                [&](const CoreCallStmt &s) {
                    return plan_call_stmt(s, statement.source_range);
                },
                [&](const CoreStoreStmt &s) { return plan_store(s, statement.source_range); },
                [&](const CoreReturnStmt &s) {
                    if (fn_mode_) {
                        if (!s.has_value) {
                            return reject("an fn body must return a value", statement.source_range);
                        }
                        used_values_[s.value.value] = true;
                        if (s.value.value >= storage_.value_types.size() ||
                            p6_scalar_kind(program_, layouts_,
                                           storage_.value_types[s.value.value]) == std::nullopt) {
                            return reject("fn return value is not a single-word P6 value",
                                          statement.source_range);
                        }
                        return true;
                    }
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
        if (lit.kind != CoreLiteralKind::Integer ||
            (kind != P6ScalarKind::IntI32 && kind != P6ScalarKind::IntI64)) {
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
        if (id.value >= storage_.exprs.size()) {
            return reject("expression id is out of range for this flow", ir::SourceRangeOpt{});
        }
        const CoreExpr &expr = storage_.exprs[id.value];
        // FB-3b: closure construction / indirect call produce or consume the
        // two-word callable pair and are handled before the single-word scalar
        // result gate.
        if (const auto *closure = std::get_if<CoreClosureExpr>(&expr.node)) {
            return emit_closure_expr(*closure, expr);
        }
        if (const auto *call = std::get_if<CoreCallClosureExpr>(&expr.node)) {
            return emit_call_closure_expr(*call, expr);
        }
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
                    return emit_value_read(r.value, expr.source_range);
                },
                [&](const CoreUnaryExpr &u) { return emit_unary(u, expr.source_range); },
                [&](const CoreBinaryExpr &b) { return emit_binary(b, expr.source_range); },
                [&](const CorePathExpr &p) { return emit_path(p, expr.source_range); },
                [&](const CoreQualifiedExpr &q) {
                    return emit_qualified(id, q, expr.source_range);
                },
                [&](const CoreConstructExpr &c) {
                    return emit_construct(id, c, expr.source_range);
                },
                [&](const CoreCoerceExpr &c) { return emit_coerce(c, expr.source_range); },
                [&](const CoreCollectionExpr &c) {
                    return emit_collection(id, c, expr.source_range);
                },
                [&](const CoreUnsupportedExpr &) {
                    return reject("expression was not fully lowered to Core-IR", expr.source_range);
                },
                [&](const CoreCallExpr &c) { return emit_direct_call(c, expr); },
                // Defensive: the two closure nodes are dispatched above.
                [&](const CoreClosureExpr &) { return false; },
                [&](const CoreCallClosureExpr &) { return false; },
            },
            expr.node);
        return ok;
    }

    // FB-3b §6.2/§6.3: emit a closure construction, leaving the eight-byte pair
    // on the stack as (func_index, env_ptr): the dense funcref-table slot
    // constant, then the runtime-bumped environment address (0 for a
    // zero-capture closure). A capturing closure bumps the module arena for its
    // env aggregate and stores each captured value at its P4-D slot offset.
    [[nodiscard]] bool emit_closure_expr(const CoreClosureExpr &c, const CoreExpr &expr) {
        if (fn_to_table_slot_ == nullptr) {
            return reject("closure construction reached emit without a funcref table",
                          expr.source_range);
        }
        const auto slot_it = fn_to_table_slot_->find(c.fn.value);
        if (slot_it == fn_to_table_slot_->end()) {
            return reject("closure target fn was not assigned a funcref-table slot",
                          expr.source_range);
        }
        const CoreFnDecl &fn = program_.fns[c.fn.value];
        const ir::core::CoreLayoutStruct *env_struct = nullptr;
        if (!fn.captures.empty()) {
            const ir::core::CoreLayout *env_layout = fn_environment_layout(fn);
            if (env_layout == nullptr) {
                return reject("capturing closure has no finalized environment layout",
                              expr.source_range);
            }
            env_struct = fn_environment_struct(env_layout);
            if (env_struct == nullptr) {
                return reject("capturing closure environment is not an aggregate layout",
                              expr.source_range);
            }
            if (alloc_temp_local_ == std::numeric_limits<std::uint32_t>::max()) {
                return reject("closure env allocation temp local was not reserved",
                              expr.source_range);
            }
            const std::uint32_t env_size =
                static_cast<std::uint32_t>(align_up(env_layout->size, 8));
            // Bump the module arena; emit_dynamic_construct_alloc leaves
            // NOTHING on the stack and holds the fresh base in alloc_temp_local_.
            emit_dynamic_construct_alloc(env_size);
            for (std::uint32_t i = 0; i < c.env.size(); ++i) {
                if (env_struct->field_layouts.size() <= i) {
                    return reject("environment layout has too few slots", expr.source_range);
                }
                if (!emit_env_slot_store(c.env[i], alloc_temp_local_,
                                         env_struct->field_offsets[i], expr.source_range)) {
                    return false;
                }
            }
        }
        emit_const_i32(static_cast<std::int32_t>(slot_it->second)); // func_index
        if (fn.captures.empty()) {
            emit_const_i32(0); // env_ptr: zero-capture static reference
        } else {
            emit_local_get(alloc_temp_local_); // env_ptr: runtime env base
        }
        return true;
    }

    // FB-3b §5.2: emit call_indirect through a closure value. Push the
    // closure's env_ptr word first, then each argument word left to right, then
    // the func_index word as the table slot, and execute call_indirect with the
    // expected functype (type trap unreachable in a well-formed program).
    [[nodiscard]] bool emit_call_closure_expr(const CoreCallClosureExpr &c,
                                              const CoreExpr &expr) {
        if (c.callee.value >= storage_.value_types.size()) {
            return reject("indirect call callee id is out of range", expr.source_range);
        }
        const auto callee_kind = scalar_kind(storage_.value_types[c.callee.value]);
        if (callee_kind != P6ScalarKind::Closure) {
            return reject("indirect call callee is not a closure pair", expr.source_range);
        }
        const auto callee_local = readable_local(c.callee);
        if (callee_local == std::nullopt) {
            return reject("indirect call callee is not a readable closure local",
                          expr.source_range);
        }
        ClosureCallType descriptor;
        descriptor.params.push_back(kI32); // leading env pointer
        for (const CoreValueId arg : c.args) {
            if (arg.value >= storage_.value_types.size()) {
                return reject("indirect call argument id is out of range", expr.source_range);
            }
            const auto arg_kind = scalar_kind(storage_.value_types[arg.value]);
            if (arg_kind == std::nullopt) {
                return reject("indirect call argument is not a representable boundary value",
                              expr.source_range);
            }
            append_boundary_param_bytes(*arg_kind, descriptor.params);
        }
        const auto result_byte = boundary_result_byte(scalar_kind(expr.result_type));
        if (result_byte == std::nullopt) {
            return reject("indirect call result is not a single-word P6 value",
                          expr.source_range);
        }
        descriptor.result = *result_byte;
        const auto type_it = closure_type_index_.find(closure_call_type_key(descriptor));
        if (type_it == closure_type_index_.end()) {
            return reject("indirect call functype was not assigned a type index",
                          expr.source_range);
        }
        emit_local_get(*callee_local + 1u); // env_ptr first
        for (const CoreValueId arg : c.args) {
            if (!emit_boundary_value_read(arg, expr.source_range)) {
                return false;
            }
        }
        emit_local_get(*callee_local); // func_index -> table slot
        body_.byte(kOpCallIndirect);
        body_.u32(type_it->second);
        body_.u32(0); // the single declared funcref table
        return true;
    }

    // FB-1 §6.3 / FB-3b: emit a static direct call. Wasm operand order pushes
    // the FIRST parameter (env) FIRST (i32.const 0), then each argument word
    // left to right — a closure argument expands to its (func_index, env_ptr)
    // pair — then a plain `call` to fn_function_base_ + ordinal. The module
    // encoder computes the base after the reachability fixed point.
    [[nodiscard]] bool emit_direct_call(const CoreCallExpr &c, const CoreExpr &expr) {
        if (instance_to_fn_ordinal_ == nullptr || fn_function_base_ == nullptr) {
            return reject("direct call reached emit without an fn function table",
                          expr.source_range);
        }
        if (c.callee.value >= instance_to_fn_ordinal_->size()) {
            return reject("direct call callee is out of range", expr.source_range);
        }
        const std::uint32_t ordinal = (*instance_to_fn_ordinal_)[c.callee.value];
        if (ordinal == std::numeric_limits<std::uint32_t>::max()) {
            return reject("direct call callee was not reached by the fn reachability pass",
                          expr.source_range);
        }
        if (!emit_call_words(c.callee, ordinal, c.args, expr.source_range)) {
            return false;
        }
        return true;
    }

    // FB-4: emit an ordered effectful fn call. Identical `call` lowering to the
    // pure direct call (env=0, args left-to-right, call fn base + ordinal); the
    // single result word is left on the stack for the caller to set into the
    // statement's result local.
    [[nodiscard]] bool emit_call_stmt(const CoreCallStmt &s, ir::SourceRangeOpt range) {
        if (instance_to_fn_ordinal_ == nullptr || fn_function_base_ == nullptr) {
            return reject("ordered call reached emit without an fn function table", range);
        }
        if (s.callee.value >= instance_to_fn_ordinal_->size()) {
            return reject("ordered call callee is out of range", range);
        }
        const std::uint32_t ordinal = (*instance_to_fn_ordinal_)[s.callee.value];
        if (ordinal == std::numeric_limits<std::uint32_t>::max()) {
            return reject("ordered call callee was not reached by the fn reachability pass", range);
        }
        if (!emit_call_words(s.callee, ordinal, s.args, range)) {
            return false;
        }
        const auto local = final_local(s.result);
        if (local == std::nullopt) {
            return reject("ordered call result has no SSA local", range);
        }
        body_.byte(kOpLocalSet);
        body_.u32(*local);
        return true;
    }

    // Push env=0, every argument word, and the plain `call`. Shared by the pure
    // CoreCallExpr (whose let-local set is done by emit_statement) and the
    // ordered CoreCallStmt.
    [[nodiscard]] bool emit_call_words(CoreInstanceId callee, std::uint32_t ordinal,
                                       const std::vector<CoreValueId> &args,
                                       ir::SourceRangeOpt range) {
        static_cast<void>(callee);
        emit_const_i32(0); // env: a static call never passes captures
        for (const CoreValueId arg : args) {
            if (!emit_boundary_value_read(arg, range)) {
                return false;
            }
        }
        body_.byte(kOpCall);
        body_.u32(*fn_function_base_ + ordinal);
        return true;
    }

    // RFC 0026 FB-4 (design §5.3): emit a capability invocation INSIDE an
    // outlined effect fn. The import occupies the SAME low function index a
    // handler capability statement uses (its ordinal in the sorted import
    // table), so the in-fn call routes through the identical ahfl_cap import
    // sequence. The opaque (status,ptr,len) tuple is popped into scratch
    // locals; a non-OK status is a single-run failure and traps (durable
    // replay / pending-latch for in-fn effects is the later wire-checkpoint
    // slice — explicitly NOT implemented here), and on OK the result ptr word
    // becomes the call's SSA value (the opaque frame address).
    [[nodiscard]] bool emit_capability_call(const CoreCapabilityCallStmt &s,
                                            ir::SourceRangeOpt range) {
        if (imports_ == nullptr) {
            return reject("effect fn capability call reached emit without an import table", range);
        }
        const auto it = std::find(imports_->begin(), imports_->end(), s.capability);
        if (it == imports_->end()) {
            return reject("effect fn capability was not planned into the import table", range);
        }
        const std::uint32_t import_ordinal =
            static_cast<std::uint32_t>(std::distance(imports_->begin(), it));
        // Push the single opaque (ptr,len) argument. The capability tuple
        // functype takes exactly two i32 words: the frame ADDRESS followed by
        // its P4-D byte length (an aggregate / collection address), or a scalar
        // value followed by an unspecified length (0).
        if (s.args.size() != 1) {
            return reject("in-fn capability call must pass one opaque frame", range);
        }
        const CoreValueId arg = s.args[0];
        if (arg.value >= storage_.value_types.size()) {
            return reject("in-fn capability argument id is out of range", range);
        }
        const auto arg_kind =
            p6_scalar_kind(program_, layouts_, storage_.value_types[arg.value]);
        if (arg_kind == std::nullopt) {
            return reject("in-fn capability argument is not a representable opaque frame", range);
        }
        if (!emit_boundary_value_read(arg, range)) {
            return false;
        }
        if (*arg_kind == P6ScalarKind::Ptr || *arg_kind == P6ScalarKind::Collection) {
            const ir::core::CoreLayout *layout =
                p6_value_layout(program_, layouts_, storage_.value_types[arg.value]);
            if (layout == nullptr || layout->size >
                                         static_cast<std::uint64_t>(
                                             std::numeric_limits<std::uint32_t>::max())) {
                return reject("in-fn capability argument frame has no valid P4-D byte length",
                              range);
            }
            emit_const_i32(static_cast<std::int32_t>(layout->size));
        } else {
            emit_const_i32(0); // a scalar opaque value carries no frame length
        }
        body_.byte(kOpCall);
        body_.u32(import_ordinal); // imports are the low function indices
        // Pop (status, ptr, len) in reverse push order.
        body_.byte(kOpLocalSet);
        body_.u32(cap_len_local_);
        body_.byte(kOpLocalSet);
        body_.u32(cap_ptr_local_);
        body_.byte(kOpLocalSet);
        body_.u32(cap_status_local_);
        // Single-run contract: a non-OK status (ERROR / PENDING / unknown) is a
        // hard trap. This is the honest FB-4 boundary — pending suspend + resume
        // / no-reinvoke through an fn body is the wire-checkpoint slice.
        body_.byte(kOpLocalGet);
        body_.u32(cap_status_local_);
        body_.byte(kOpIf);
        body_.byte(kEmptyBlock);
        body_.byte(kOpUnreachable);
        body_.byte(kOpEnd);
        // OK: the result is the opaque frame pointer word.
        body_.byte(kOpLocalGet);
        body_.u32(cap_ptr_local_);
        const auto local = final_local(s.result);
        if (local == std::nullopt) {
            return reject("in-fn capability result has no SSA local", range);
        }
        body_.byte(kOpLocalSet);
        body_.u32(*local);
        return true;
    }

    // Emit a projection path READ. The plan pass proved the root is input /
    // context / a local aggregate and every step owner is a struct, so this walks
    // the chain accumulating each step's P4-D field offset and leaves the last
    // place's value (a scalar load, a `Ptr` address, or a `Index` tag) on the
    // stack.
    [[nodiscard]] bool emit_path(const CorePathExpr &path, ir::SourceRangeOpt range) {
        if (path.projection.empty()) {
            if (!path.has_local) {
                return reject("a bare input/context root is not a memory read in the P6 subset",
                              std::move(range));
            }
            // A bare local aggregate read is just its address.
            return emit_value_read(path.local, std::move(range));
        }
        const auto root = projection_root_of(path);
        if (root == std::nullopt) {
            return reject("projection root is not input, context, or an aggregate local",
                          std::move(range));
        }
        // KR6.7: a non-empty projection rooted at the input frame reads raw P4-D
        // input bytes. A context projection or an aggregate-LOCAL projection
        // (constructor / binding scratch) leaves the run2 wire-frame boundary
        // intact.
        if (!path.has_local && path.root == ir::core::CorePathRoot::Input) {
            reads_raw_input_frame_ = true;
        }
        // The walk leaves the final slot's ADDRESS on the stack, dereferencing
        // every intermediate aggregate field exactly as `emit_construct_store`
        // wrote it.
        const auto slot =
            emit_projection_slot(path.projection, path.root_type, *root, std::move(range));
        if (slot == std::nullopt) {
            return false;
        }
        // A projected LEAF must be a single-word P6 value: a scalar, a tag-only
        // enum discriminant, an addressable aggregate, or a bounded collection.
        // A PtrLen String / bytes / f64 has no single-word P6 representation,
        // so fail closed rather than load half of it.
        if (!place_is_p6_value(slot->edge)) {
            return reject("projection leaf is not a single-word P6 value", std::move(range));
        }
        const P6ScalarKind kind = place_kind_of_layout(slot->edge);
        if (kind == P6ScalarKind::Collection) {
            // RFC 0026 P6-5: a bounded collection's P4-D layout is the INLINE
            // `(ptr,len)` header itself, so its VALUE is the address of the field
            // slot — NOT a load (the header is not a pointer to a header; the two
            // words ARE the handle). `emit_projection_slot` left the OWNING
            // struct's address on the stack, so advance to the slot. This is the
            // exact counterpart of the aggregate-address rule, and header word
            // loads happen inside `emit_collection`.
            if (slot->offset != 0) {
                emit_const_i32(static_cast<std::int32_t>(slot->offset));
                body_.byte(kOpI32Add);
            }
            return true;
        }
        // An aggregate field's slot HOLDS the child's address (the ONE
        // representation rule), so both forms are read as one i32, then the
        // aggregate form is left as the pointer it read. A scalar field is
        // loaded at its own physical width.
        const bool wide = kind == P6ScalarKind::IntI64;
        body_.byte(wide ? kOpI64Load : kOpI32Load);
        body_.u32(wide ? kAlignI64 : kAlignI32);
        body_.u32(slot->offset);
        return true;
    }

    // Emit a qualified unit variant: a tag-only enum's discriminant constant, or
    // an addressed aggregate with its tag stored at offset 0 and no payload stores
    // (a unit variant of a payload enum has all-unit slots in P6).
    [[nodiscard]] bool
    emit_qualified(CoreExprId id, const CoreQualifiedExpr &q, ir::SourceRangeOpt range) {
        if (p6_is_tag_only_enum(program_, layouts_, storage_.exprs[id.value].result_type)) {
            emit_const_i32(static_cast<std::int32_t>(q.variant.value));
            return true;
        }
        const bool dynamic = id.value < dynamic_constructs_.size() && dynamic_constructs_[id.value];
        if (!dynamic &&
            (id.value >= construct_addrs_.size() || !construct_addrs_[id.value].has_value())) {
            return reject("qualified variant has no planned scratch address", std::move(range));
        }
        const std::uint64_t size = aggregate_size(q.type_id);
        const std::uint32_t address =
            dynamic ? 0u : *construct_addrs_[id.value];
        if (dynamic) {
            emit_dynamic_construct_alloc(static_cast<std::uint32_t>(size));
            emit_local_get(alloc_temp_local_);
            emit_const_i32(static_cast<std::int32_t>(q.variant.value));
            body_.byte(kOpI32Store);
            body_.u32(kAlignI32);
            body_.u32(0);
            emit_local_get(alloc_temp_local_);
            return true;
        }
        emit_const_i32(static_cast<std::int32_t>(address));
        emit_const_i32(static_cast<std::int32_t>(q.variant.value));
        body_.byte(kOpI32Store);
        body_.u32(kAlignI32);
        body_.u32(0);
        emit_const_i32(static_cast<std::int32_t>(address));
        return true;
    }

    // Whether this planned body allocates at least one aggregate at run time
    // (fn mode only; handlers keep static scratch).
    [[nodiscard]] bool has_dynamic_construct() const {
        for (bool d : dynamic_constructs_) {
            if (d) {
                return true;
            }
        }
        return false;
    }

    // Emit one per-activation bump from the module aggregate arena for an
    // fn-mode aggregate construct of `size` bytes. Leaves NOTHING on the
    // operand stack: the fresh base is held in alloc_temp_local_ and the
    // advanced cursor in alloc_new_local_. The cursor global is module-level
    // and monotonically increases across nested/repeated native calls, so two
    // live results never alias. Bumping past `arena_limit_` traps (RESOURCE
    // fail-closed); a compile-time depth-aware budget guarantees the worst-case
    // chain fits, making the trap a defensive guard only.
    void emit_dynamic_construct_alloc(std::uint32_t size) {
        const auto global_get = [&](std::uint32_t g) {
            body_.byte(kOpGlobalGet);
            body_.u32(g);
        };
        const auto global_set = [&](std::uint32_t g) {
            body_.byte(kOpGlobalSet);
            body_.u32(g);
        };
        const auto local_get = [&](std::uint32_t l) {
            body_.byte(kOpLocalGet);
            body_.u32(l);
        };
        const auto local_set = [&](std::uint32_t l) {
            body_.byte(kOpLocalSet);
            body_.u32(l);
        };
        const auto local_tee = [&](std::uint32_t l) {
            body_.byte(kOpLocalTee);
            body_.u32(l);
        };
        global_get(kGlobalHeapNext);
        local_tee(alloc_temp_local_);
        emit_const_i32(static_cast<std::int32_t>(size));
        body_.byte(kOpI32Add);
        local_set(alloc_new_local_);
        local_get(alloc_new_local_);
        emit_const_i32(static_cast<std::int32_t>(
            kCoreWasmFixedLinearMemoryCapacityBytes));
        body_.byte(kOpI32GtU);
        body_.byte(kOpIf);
        body_.byte(kEmptyBlock);
        body_.byte(kOpUnreachable);
        body_.byte(kOpEnd);
        local_get(alloc_new_local_);
        global_set(kGlobalHeapNext);
    }

    // Emit a constructor: allocate its scratch bytes and store every operand at
    // its DECLARED slot offset, then leave the address on the stack. Field
    // IDENTITY (never source write order) selects the destination, so
    // `Pair { b: 2, a: 1 }` cannot be mis-assigned (Principle 2). An enum stores
    // its i32 tag at offset 0 and its payload slots at `payload_offset`.
    [[nodiscard]] bool
    emit_construct(CoreExprId id, const CoreConstructExpr &construct, ir::SourceRangeOpt range) {
        const bool dynamic =
            id.value < dynamic_constructs_.size() && dynamic_constructs_[id.value];
        if (!dynamic &&
            (id.value >= construct_addrs_.size() || !construct_addrs_[id.value].has_value())) {
            return reject("constructor has no planned scratch address", std::move(range));
        }
        const std::uint64_t dyn_size = aggregate_size(construct.type_id);
        const std::uint32_t static_address =
            dynamic ? 0u : *construct_addrs_[id.value];
        if (dynamic) {
            // Allocate a fresh per-activation aggregate; its base is held in the
            // allocation temp local while the fields are written.
            emit_dynamic_construct_alloc(static_cast<std::uint32_t>(dyn_size));
        }
        if (!construct.is_enum_variant) {
            const auto *structure = p6_nominal_struct_layout(program_, layouts_, construct.type_id);
            if (structure == nullptr) {
                return reject("struct constructor has no finalized layout", std::move(range));
            }
            for (const CoreConstructArg &arg : construct.args) {
                if (!emit_construct_store(arg,
                                          static_address,
                                          structure->field_layouts[arg.field.value],
                                          structure->field_offsets[arg.field.value],
                                          0,
                                          dynamic,
                                          std::move(range))) {
                    return false;
                }
            }
        } else {
            const CoreValueTypeId construct_vt = storage_.exprs[id.value].result_type;
            const auto *tagged =
                p6_enum_value_layout(program_, layouts_, construct_vt, construct.type_id);
            if (tagged == nullptr ||
                construct.variant.value >= tagged->variant_payload_sizes.size()) {
                return reject("enum constructor has no finalized layout", std::move(range));
            }
            const CoreLayoutId payload_layout =
                tagged->variant_payload_layouts[construct.variant.value];
            const ir::core::CoreLayoutStruct *payload = nullptr;
            if (payload_layout.value < layouts_.layouts.size()) {
                payload = std::get_if<ir::core::CoreLayoutStruct>(
                    &layouts_.layouts[payload_layout.value].shape);
            }
            if (!construct.args.empty() && payload == nullptr) {
                return reject("enum payload constructor has no struct payload layout",
                              std::move(range));
            }
            for (const CoreConstructArg &arg : construct.args) {
                if (payload == nullptr) {
                    return reject("enum unit variant constructor has an unexpected payload arg",
                                  std::move(range));
                }
                if (!emit_construct_store(arg,
                                          static_address,
                                          payload->field_layouts[arg.field.value],
                                          payload->field_offsets[arg.field.value],
                                          tagged->payload_offset,
                                          dynamic,
                                          std::move(range))) {
                    return false;
                }
            }
            // The discriminant is written after the payload so the tag is the LAST
            // store; the value is the variant id (its declaration-order index).
            if (dynamic) {
                emit_local_get(alloc_temp_local_);
            } else {
                emit_const_i32(static_cast<std::int32_t>(static_address));
            }
            emit_const_i32(static_cast<std::int32_t>(construct.variant.value));
            body_.byte(kOpI32Store);
            body_.u32(kAlignI32);
            body_.u32(0);
        }
        if (dynamic) {
            emit_local_get(alloc_temp_local_);
        } else {
            emit_const_i32(static_cast<std::int32_t>(static_address));
        }
        return true;
    }

    // Store ONE constructor operand at its declared slot offset (plus an optional
    // enum payload base). A scalar operand is loaded from its local; an aggregate
    // operand is copied by its address. The SLOT's layout edge selects the width,
    // and the plan pass already proved the operand matches it. When
    // `dynamic_address` is set the destination base is the fn-mode allocation
    // temp local (a fresh per-activation heap slot) rather than a static scratch
    // constant.
    [[nodiscard]] bool emit_construct_store(const CoreConstructArg &arg,
                                            std::uint32_t static_address,
                                            const CoreLayoutId slot_layout,
                                            std::uint64_t slot_offset,
                                            std::uint64_t payload_base,
                                            bool dynamic_address,
                                            ir::SourceRangeOpt range) {
        const std::uint64_t offset = payload_base + slot_offset;
        const auto kind = readable_kind(arg.value);
        const auto local = readable_local(arg.value);
        if (kind == std::nullopt || local == std::nullopt) {
            return reject("constructor operand is not a readable value", std::move(range));
        }
        if (place_is_aggregate_leaf(slot_layout)) {
            // An aggregate operand materializes as a 32-bit ADDRESS; it is one i32
            // slot. A P6 aggregate is never flattened inline.
            if (dynamic_address) {
                emit_local_get(alloc_temp_local_);
            } else {
                emit_const_i32(static_cast<std::int32_t>(static_address));
            }
            emit_local_get(*local);
            body_.byte(kOpI32Store);
            body_.u32(kAlignI32);
            body_.u32(static_cast<std::uint32_t>(offset));
            return true;
        }
        const bool wide = place_kind_of_layout(slot_layout) == P6ScalarKind::IntI64;
        // Stack order: [address][value] for a store, so push the address first.
        if (dynamic_address) {
            emit_local_get(alloc_temp_local_);
        } else {
            emit_const_i32(static_cast<std::int32_t>(static_address));
        }
        emit_local_get(*local);
        body_.byte(wide ? kOpI64Store : kOpI32Store);
        body_.u32(wide ? kAlignI64 : kAlignI32);
        body_.u32(static_cast<std::uint32_t>(offset));
        return true;
    }

    // A store into a place (`ctx.field = v`, `ctx.nested.field = v`). The plan
    // pass bounds-checks the projection; emit walks the same typed step chain to
    // the scalar leaf and stores the value at the P4-D field offset. A store into
    // a bare `ctx.field` is the common case; a deeper chain accumulates offsets.
    [[nodiscard]] bool plan_store(const CoreStoreStmt &store, ir::SourceRangeOpt range) {
        if (!store.place.projection_resolved) {
            return reject("store place projection is unresolved in an executable program", range);
        }
        if (store.place.root != ir::core::CorePathRoot::Context) {
            return reject("only a context store is in the P6 subset", range);
        }
        if (store.place.projection.empty()) {
            return reject("a context store requires a member projection", range);
        }
        for (std::uint32_t i = 0; i < store.place.projection.size(); ++i) {
            const ir::core::CoreProjectionStep &step = store.place.projection[i];
            const CoreTypeId expected =
                (i == 0) ? store.place.root_type : store.place.projection[i - 1].result_type;
            if (!plan_projection_owner(step.owner_type, expected, range)) {
                return false;
            }
        }
        if (readable_kind(store.value) == std::nullopt) {
            return reject("store value is not a readable value", range);
        }
        // An aggregate-valued context store would write a SCRATCH ADDRESS into the
        // durable context frame. The scratch arena is per-handler (its cursor
        // resets to zero in every handler), so a later handler's first constructor
        // reuses that byte range and silently clobbers the value the context slot
        // still points at. P6 has no lifetime rule for a value that outlives its
        // handler yet, so this fails closed exactly like the PtrLen / String leaf
        // rather than emit a dangling pointer.
        const auto slot =
            resolve_projection_slot(store.place.projection, store.place.root_type, range);
        if (slot == std::nullopt) {
            return false;
        }
        if (place_is_aggregate_leaf(slot->edge)) {
            return reject("a context store may not persist an aggregate value in the P6 subset",
                          range);
        }
        used_values_[store.value.value] = true;
        return true;
    }

    [[nodiscard]] bool emit_store(const CoreStoreStmt &store, ir::SourceRangeOpt range) {
        const auto root = store_root_of(store.place);
        if (root == std::nullopt) {
            return reject("only a context store is in the P6 subset", std::move(range));
        }
        // Stack order for a store is [address][value], so the shared walk pushes
        // the destination slot's address (dereferencing every intermediate
        // aggregate field) before the operand is read.
        const auto slot = emit_projection_slot(
            store.place.projection, store.place.root_type, *root, std::move(range));
        if (slot == std::nullopt) {
            return false;
        }
        // A store leaf must be a single-word P6 value: a scalar or an addressable
        // aggregate. A PtrLen / bytes / collection / f64 leaf has no single-word
        // P6 representation, so fail closed.
        if (!place_is_p6_value(slot->edge)) {
            return reject("store destination is not a single-word P6 value", std::move(range));
        }
        const auto place_kind = place_kind_of_layout(slot->edge);
        const auto local = readable_local(store.value);
        if (local == std::nullopt) {
            return reject("store value is not a readable value", std::move(range));
        }
        if (place_kind == P6ScalarKind::Ptr) {
            // An aggregate destination holds the operand's ADDRESS (one i32 slot),
            // the ONE representation rule the projection walk above obeys.
            emit_local_get(*local);
            body_.byte(kOpI32Store);
            body_.u32(kAlignI32);
            body_.u32(slot->offset);
            return true;
        }
        if (place_kind == P6ScalarKind::Index || readable_kind(store.value) != place_kind) {
            return reject("store value kind does not match its destination field",
                          std::move(range));
        }
        const bool wide = place_kind == P6ScalarKind::IntI64;
        emit_local_get(*local);
        body_.byte(wide ? kOpI64Store : kOpI32Store);
        body_.u32(wide ? kAlignI64 : kAlignI32);
        body_.u32(slot->offset);
        return true;
    }

    [[nodiscard]] bool emit_unary(const CoreUnaryExpr &u, ir::SourceRangeOpt range) {
        if (u.operand.value >= storage_.exprs.size()) {
            return reject("unary operand id is out of range for this flow", std::move(range));
        }
        const auto operand_kind = scalar_kind(storage_.exprs[u.operand.value].result_type);
        if (operand_kind == std::nullopt) {
            return reject("unary operand has a non-scalar or f64 result type", std::move(range));
        }
        switch (u.op) {
        case CoreUnaryOp::Not:
            if (*operand_kind != P6ScalarKind::Bool) {
                return reject("logical not requires a Bool operand", std::move(range));
            }
            if (!emit_expr(u.operand)) {
                return false;
            }
            body_.byte(kOpI32Eqz);
            return true;
        case CoreUnaryOp::Neg:
            if (*operand_kind == P6ScalarKind::Bool) {
                return reject("arithmetic negation requires an Int operand", std::move(range));
            }
            // 0 - x; wasm wrap semantics (negating INT_MIN wraps), matching the
            // documented P6 integer contract.
            if (*operand_kind == P6ScalarKind::IntI32) {
                emit_const_i32(0);
            } else {
                emit_const_i64(0);
            }
            if (!emit_expr(u.operand)) {
                return false;
            }
            body_.byte(*operand_kind == P6ScalarKind::IntI32 ? kOpI32Sub : kOpI64Sub);
            return true;
        }
        return reject("unary operator is outside the P6 scalar subset", std::move(range));
    }

    [[nodiscard]] bool emit_binary(const CoreBinaryExpr &b, ir::SourceRangeOpt range) {
        if (b.lhs.value >= storage_.exprs.size() || b.rhs.value >= storage_.exprs.size()) {
            return reject("binary operand id is out of range for this flow", std::move(range));
        }
        const auto lhs_kind = scalar_kind(storage_.exprs[b.lhs.value].result_type);
        const auto rhs_kind = scalar_kind(storage_.exprs[b.rhs.value].result_type);
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

    // A goto ends THIS handler: latch current_state, bump transition_count
    // exactly once, then leave the enclosing `block (result i32)` with the new
    // state id on the stack. The `br` skips every label opened by the enclosing
    // statement-level ifs, so a goto exits structurally from any nesting depth
    // (RFC 0026 Q2: structured early-exit, no relooper).
    void emit_goto_transition(const CoreGotoStmt &go) {
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
        body_.byte(kOpBr);
        // The handler function wraps the region in ONE `block (result i32)`; each
        // enclosing statement-level `if` adds one label. `br label_depth_` leaves
        // every enclosing `if` and lands on that block with the state id.
        body_.u32(label_depth_);
    }

    void emit_br(std::uint32_t depth) {
        body_.byte(kOpBr);
        body_.u32(depth);
    }

    // Emit one arm's PATTERN TEST, leaving a single i32 on the stack: nonzero iff
    // the value at `site` matches. `scrutinee_local` holds the scrutinee (a scalar
    // value, or an aggregate's i32 address); a payload sub-pattern descends by
    // adding the plan-recorded P4-D offset, so one emitter covers every depth. No
    // branch instruction is emitted: an or-pattern combines its alternatives with
    // `i32.or`, correct because a pattern test is pure and all alternatives read
    // the same value.
    [[nodiscard]] bool emit_pattern_test(CorePatternId id,
                                         std::uint32_t scrutinee_local,
                                         P6PatternSite site,
                                         ir::SourceRangeOpt range,
                                         std::optional<CoreValueTypeId> concrete_enum =
                                             std::nullopt) {
        if (id.value >= storage_.patterns.size()) {
            return reject("pattern id is out of range for this flow", std::move(range));
        }
        const CorePattern &pattern = storage_.patterns[id.value];
        return std::visit(
            Overloaded{
                // `_` and `x`: irrefutable — a constant true, so the enclosing
                // `br_if 0` never fires. A binding's own latch already happened.
                [&](const CoreWildcardPat &) {
                    emit_const_i32(1);
                    return true;
                },
                [&](const CoreBindingPat &b) {
                    if (b.has_nested) {
                        return emit_pattern_test(b.nested, scrutinee_local, site, std::move(range),
                                                 std::nullopt);
                    }
                    emit_const_i32(1);
                    return true;
                },
                [&](const CoreLiteralPat &lit) {
                    return emit_literal_test(lit, scrutinee_local, site, std::move(range));
                },
                [&](const CoreIntRangePat &r) {
                    return emit_int_range_test(r, scrutinee_local, site, std::move(range));
                },
                [&](const CoreVariantPat &v) {
                    return emit_variant_test(
                        v, scrutinee_local, site, std::move(range), concrete_enum);
                },
                [&](const CoreOrPat &o) {
                    // Any alternative matching => the pattern matches. Each
                    // alternative leaves its own i32; `i32.or` folds them. The
                    // chain must start from a pushed 0 so the first `or` has two
                    // operands.
                    emit_const_i32(0);
                    for (const CorePatternId alt : o.alternatives) {
                        if (!emit_pattern_test(
                                alt, scrutinee_local, site, range, concrete_enum)) {
                            return false;
                        }
                        body_.byte(kOpI32Or);
                    }
                    return true;
                },
                [&](const CoreTuplePat &) {
                    return reject("tuple patterns are not in the P6 aggregate subset",
                                  std::move(range));
                },
            },
            pattern.node);
    }

    // Push the ADDRESS a pattern site names: the scrutinee local (an aggregate's
    // address) advanced by the site's accumulated field offset.
    void emit_site_address(std::uint32_t scrutinee_local, const P6PatternSite &site) {
        emit_local_get(scrutinee_local);
        if (site.offset != 0) {
            emit_const_i32(static_cast<std::int32_t>(site.offset));
            body_.byte(kOpI32Add);
        }
    }

    // Push the VALUE at a pattern site: a scalar in a local (`local.get`), or a
    // scalar at a memory offset (`local.get; i32.load offset=`). A `Ptr` site's
    // "value" is its address, which is what a nested payload test needs.
    [[nodiscard]] bool emit_site_value(std::uint32_t scrutinee_local,
                                       const P6PatternSite &site,
                                       ir::SourceRangeOpt range) {
        if (site.kind == P6ScalarKind::Ptr) {
            emit_site_address(scrutinee_local, site);
            return true;
        }
        if (!site.in_memory) {
            emit_local_get(scrutinee_local);
            return true;
        }
        if (site.offset > std::numeric_limits<std::uint32_t>::max()) {
            return reject("pattern site offset exceeds the wasm32 address domain",
                          std::move(range));
        }
        emit_local_get(scrutinee_local);
        const bool wide = site.kind == P6ScalarKind::IntI64;
        body_.byte(wide ? kOpI64Load : kOpI32Load);
        body_.u32(wide ? kAlignI64 : kAlignI32);
        body_.u32(static_cast<std::uint32_t>(site.offset));
        return true;
    }

    // A variant pattern: compare the tag. A tag-only enum value test compares the
    // i32 discriminant in the scrutinee local; an ADDRESSED enum loads the tag
    // from offset 0 of the site, then (with a payload) each sub-pattern is tested
    // at `site + payload_offset + slot_offset`.
    [[nodiscard]] bool emit_variant_test(const CoreVariantPat &v,
                                         std::uint32_t scrutinee_local,
                                         const P6PatternSite &site,
                                         ir::SourceRangeOpt range,
                                         std::optional<CoreValueTypeId> concrete_enum) {
        if (v.owner_enum.value >= program_.types.size() ||
            v.variant.value >= program_.types[v.owner_enum.value].variants.size()) {
            return reject("variant pattern identity is out of range", std::move(range));
        }
        if (!site.in_memory) {
            if (site.kind != P6ScalarKind::Index) {
                return reject("variant pattern requires an enum scrutinee", std::move(range));
            }
            emit_local_get(scrutinee_local);
        } else {
            if (site.kind != P6ScalarKind::Ptr) {
                return reject("variant pattern requires an enum scrutinee", std::move(range));
            }
            emit_site_address(scrutinee_local, site);
            body_.byte(kOpI32Load);
            body_.u32(kAlignI32);
            body_.u32(0); // tag at offset 0
        }
        emit_const_i32(static_cast<std::int32_t>(v.variant.value));
        body_.byte(kOpI32Eq);
        const bool has_subpatterns = !v.tuple_subpatterns.empty() || !v.struct_fields.empty();
        if (!has_subpatterns) {
            return true;
        }
        // Tag AND every payload sub-pattern. The tag test is already on the stack;
        // each sub-pattern leaves its own i32 and `i32.and` folds them.
        const auto *tagged = concrete_enum.has_value()
                                 ? p6_enum_value_layout(program_, layouts_, *concrete_enum,
                                                        v.owner_enum)
                                 : p6_nominal_enum_layout(program_, layouts_, v.owner_enum);
        if (tagged == nullptr || v.variant.value >= tagged->variant_payload_layouts.size()) {
            return reject("variant pattern owner has no finalized enum layout", std::move(range));
        }
        const CoreLayoutId payload_layout = tagged->variant_payload_layouts[v.variant.value];
        const ir::core::CoreLayoutStruct *payload = nullptr;
        if (payload_layout.value < layouts_.layouts.size()) {
            payload = std::get_if<ir::core::CoreLayoutStruct>(
                &layouts_.layouts[payload_layout.value].shape);
        }
        if (payload == nullptr) {
            return reject("variant payload pattern has no struct payload layout", std::move(range));
        }
        const std::uint64_t payload_base = site.offset + tagged->payload_offset;
        for (std::uint32_t i = 0; i < v.tuple_subpatterns.size(); ++i) {
            const auto sub = payload_sub_site(*payload, i, payload_base, range);
            if (sub == std::nullopt) {
                return false;
            }
            if (!emit_pattern_test(
                    v.tuple_subpatterns[i], scrutinee_local, *sub, std::move(range),
                    std::nullopt)) {
                return false;
            }
            body_.byte(kOpI32And);
        }
        for (const CoreVariantPatField &field : v.struct_fields) {
            const auto sub = payload_sub_site(*payload, field.slot.value, payload_base, range);
            if (sub == std::nullopt) {
                return false;
            }
            if (!emit_pattern_test(field.pattern, scrutinee_local, *sub, std::move(range),
                                   std::nullopt)) {
                return false;
            }
            body_.byte(kOpI32And);
        }
        return true;
    }

    [[nodiscard]] bool emit_literal_test(const CoreLiteralPat &lit,
                                         std::uint32_t scrutinee_local,
                                         const P6PatternSite &site,
                                         ir::SourceRangeOpt range) {
        if (site.in_memory ||
            (site.kind != P6ScalarKind::Bool && site.kind != P6ScalarKind::IntI32 &&
             site.kind != P6ScalarKind::IntI64)) {
            return reject("literal pattern requires a scalar scrutinee", std::move(range));
        }
        const bool wide = site.kind == P6ScalarKind::IntI64;
        if (lit.kind == CoreLiteralKind::Bool) {
            if (site.kind != P6ScalarKind::Bool) {
                return reject("bool pattern requires a Bool scrutinee", std::move(range));
            }
            if (lit.spelling != "true" && lit.spelling != "false") {
                return reject("bool pattern has an unrecognized spelling", std::move(range));
            }
            emit_local_get(scrutinee_local);
            emit_const_i32(lit.spelling == "true" ? 1 : 0);
            body_.byte(kOpI32Eq);
            return true;
        }
        if (lit.kind != CoreLiteralKind::Integer ||
            (site.kind != P6ScalarKind::IntI32 && site.kind != P6ScalarKind::IntI64)) {
            return reject("only Bool and Integer patterns are in the scalar subset",
                          std::move(range));
        }
        const auto parsed = parse_unsigned_spelling(lit.spelling);
        if (!parsed.has_value() ||
            *parsed > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            return reject("integer pattern spelling does not parse or overflows", std::move(range));
        }
        emit_local_get(scrutinee_local);
        if (wide) {
            emit_const_i64(static_cast<std::int64_t>(*parsed));
        } else {
            if (*parsed > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
                return reject("integer pattern exceeds the i32 scalar range", std::move(range));
            }
            emit_const_i32(static_cast<std::int32_t>(*parsed));
        }
        body_.byte(wide ? kOpI64Eq : kOpI32Eq);
        return true;
    }

    // `start..end` is a CLOSED interval (`core_ir.hpp`): matches iff
    // start <= v <= end. The IR stores both bounds as typed i64, so lowering
    // needs the scrutinee's repr to pick the compare width, plus a bound range
    // check against it (a range bound that does not fit the scrutinee's repr
    // could never be reached / could wrap, so it fails closed).
    [[nodiscard]] bool emit_int_range_test(const CoreIntRangePat &r,
                                           std::uint32_t scrutinee_local,
                                           const P6PatternSite &site,
                                           ir::SourceRangeOpt range) {
        if (r.start > r.end) {
            return reject("int-range pattern has start greater than end", std::move(range));
        }
        if (site.in_memory ||
            (site.kind != P6ScalarKind::IntI32 && site.kind != P6ScalarKind::IntI64)) {
            return reject("int-range pattern requires an Int scrutinee", std::move(range));
        }
        const bool wide = site.kind == P6ScalarKind::IntI64;
        if (!wide && (r.start < std::numeric_limits<std::int32_t>::min() ||
                      r.end > std::numeric_limits<std::int32_t>::max())) {
            return reject("int-range pattern bound exceeds the i32 scalar range", std::move(range));
        }
        // `not (v < start)` — the first operand of the `and`.
        emit_local_get(scrutinee_local);
        if (wide) {
            emit_const_i64(r.start);
        } else {
            emit_const_i32(static_cast<std::int32_t>(r.start));
        }
        body_.byte(wide ? kOpI64LtS : kOpI32LtS);
        body_.byte(kOpI32Eqz);
        // `and not (v > end)` — same width.
        emit_local_get(scrutinee_local);
        if (wide) {
            emit_const_i64(r.end);
        } else {
            emit_const_i32(static_cast<std::int32_t>(r.end));
        }
        body_.byte(wide ? kOpI64GtS : kOpI32GtS);
        body_.byte(kOpI32Eqz);
        body_.byte(kOpI32And);
        return true;
    }

    // Emit an arm body (or the fallback region) and then leave the arm's
    // completion: a trailing `CoreYieldStmt` is the region's hand-off to this
    // match, so a value yield stores the match result and a unit yield is a
    // no-op; both then `br` to the match-completion block S (skipping every
    // remaining arm and the fallback). A region whose paths diverge (goto /
    // trap / a fully-diverging if or nested match) emits its own exits and needs
    // no completion here — the same "completes vs diverges" split the verifier's
    // `core_region_exit` draws.
    //
    // `completion_offset` is S's label depth measured from the START of this
    // region's instruction stream: 2 for an arm body (S is outside C_i and B) and
    // 1 for the fallback (only B is between it and S). A statement-level `if`
    // opened inside the region adds one label per nesting level, so the emitted
    // depth grows with `label_depth_` past the region's own base.
    [[nodiscard]] bool emit_match_region(const CoreRegion &region,
                                         std::optional<std::uint32_t> result_local,
                                         std::uint32_t completion_offset,
                                         ir::SourceRangeOpt default_range) {
        const std::uint32_t region_base = label_depth_;
        for (const CoreStmt &statement : region.statements) {
            if (const auto *yield = std::get_if<CoreYieldStmt>(&statement.node)) {
                if (result_local.has_value()) {
                    if (!yield->has_value) {
                        return reject("expression match arm must yield a value", default_range);
                    }
                    if (!emit_value_read(yield->value, default_range)) {
                        return false;
                    }
                    body_.byte(kOpLocalSet);
                    body_.u32(*result_local);
                }
                emit_br(completion_offset + (label_depth_ - region_base));
                return true;
            }
            if (!emit_statement(statement)) {
                return false;
            }
        }
        if (!p6_region_always_diverges(region)) {
            return reject("match region must yield or diverge on every path", default_range);
        }
        return true;
    }

    // Emit a guard region, leaving its Bool on the stack. The region ends in
    // exactly one value yield (so no completion block is involved), and every
    // other statement is an ordinary P6 statement.
    [[nodiscard]] bool emit_guard_region(const CoreRegion &region, ir::SourceRangeOpt range) {
        if (region.statements.empty()) {
            return reject("guard region must yield a value", std::move(range));
        }
        const auto *yield = std::get_if<CoreYieldStmt>(&region.statements.back().node);
        if (yield == nullptr || !yield->has_value) {
            return reject("guard region must end in a value yield", std::move(range));
        }
        for (std::size_t index = 0; index + 1 < region.statements.size(); ++index) {
            if (!emit_statement(region.statements[index])) {
                return false;
            }
        }
        return emit_value_read(yield->value, std::move(range));
    }

    // The match arm chain described above. `S` and `B` are opened once, then one
    // `C_i` per arm; the fallback runs at B's tail.
    [[nodiscard]] bool emit_match(const CoreMatchStmt &match, ir::SourceRangeOpt range) {
        const auto scrutinee_local = readable_local(match.scrutinee);
        if (scrutinee_local == std::nullopt) {
            return reject("match scrutinee has no readable local", range);
        }
        const auto scrutinee_kind = readable_kind(match.scrutinee);
        if (scrutinee_kind == std::nullopt) {
            return reject("match scrutinee has no readable kind", range);
        }
        const P6PatternSite root_site{*scrutinee_kind, *scrutinee_kind == P6ScalarKind::Ptr, 0};
        std::optional<std::uint32_t> result_local;
        if (match.has_result) {
            result_local = match_result_local(match.result);
            if (result_local == std::nullopt) {
                return reject("match result has no scratch local", range);
            }
        }

        // S: the completion target, typeless — a completing arm leaves through
        // it, skipping the remaining arms and the fallback below.
        body_.byte(kOpBlock);
        body_.byte(kEmptyBlock);
        ++label_depth_;
        // B: the arm chain. Its own block so an arm can reach S (br past B) while
        // a mismatched test / false guard only reaches the next arm (br 0).
        body_.byte(kOpBlock);
        body_.byte(kEmptyBlock);
        ++label_depth_;

        for (const CoreMatchArm &arm : match.arms) {
            // C_i: this arm. A test/guard failure `br 0` lands right after the
            // `end` below, i.e. at the next arm.
            body_.byte(kOpBlock);
            body_.byte(kEmptyBlock);
            ++label_depth_;

            // Latch every arm binding from its SITE before the test. The IR gives
            // a binding no independent value, so a root binding copies the whole
            // scrutinee and a payload binding loads (or addresses) its P4-D slot.
            // The plan pass recorded the FULL site for each binding, so the kind
            // here is the binding's own repr (a struct payload is an address, an
            // i64 field is i64) and the latch needs no scrutinee-root guess.
            for (const CorePatternBinding &binding : arm.bindings) {
                const auto dest = binding_local(binding.value);
                if (dest == std::nullopt) {
                    return reject("arm binding has no scratch local", range);
                }
                const auto site = binding_site_of(binding.value);
                if (site == std::nullopt) {
                    return reject("arm binding has no planned pattern site", range);
                }
                if (!emit_binding_latch(*scrutinee_local, *dest, *site, range)) {
                    return false;
                }
            }
            if (!emit_pattern_test(arm.pattern, *scrutinee_local, root_site, range,
                                   storage_.value_types[match.scrutinee.value])) {
                return false;
            }
            // A pattern test leaves "matched" as a nonzero i32; `br_if` branches
            // on NONZERO, but a mis-match must continue to the NEXT arm, so the
            // test is negated first: branch exactly when it did not match.
            body_.byte(kOpI32Eqz);
            body_.byte(kOpBrIf);
            body_.u32(0); // not matched -> next arm
            if (arm.guard_region) {
                if (!emit_guard_region(*arm.guard_region, range)) {
                    return false;
                }
                // Same inversion: skip the arm when the guard yields false.
                body_.byte(kOpI32Eqz);
                body_.byte(kOpBrIf);
                body_.u32(0); // guard false -> next arm
            }
            if (arm.body &&
                !emit_match_region(*arm.body, result_local, /*completion_offset=*/2, range)) {
                return false;
            }
            --label_depth_;
            body_.byte(kOpEnd);
        }

        // The fallback runs when no arm matched. It is a separate region (never a
        // synthetic arm), so it shares the arm-body shape: a completing fallback
        // leaves through S, a diverging one (a non-exhaustive match's trap, or a
        // goto) leaves on its own.
        if (match.fallback_region &&
            !emit_match_region(
                *match.fallback_region, result_local, /*completion_offset=*/1, range)) {
            return false;
        }

        --label_depth_;
        body_.byte(kOpEnd); // B
        --label_depth_;
        body_.byte(kOpEnd); // S
        return true;
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
                    // FB-3b: a closure result is the two-word (func_index,
                    // env_ptr) pair, popped in REVERSE push order: env_ptr into
                    // slot+1 first, then func_index into slot.
                    const auto result_kind =
                        s.result.value < storage_.value_types.size()
                            ? scalar_kind(storage_.value_types[s.result.value])
                            : std::nullopt;
                    if (result_kind == P6ScalarKind::Closure) {
                        body_.byte(kOpLocalSet);
                        body_.u32(*local + 1u);
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
                    // Every statement-level if is emitted as a VOID block. Each
                    // goto path leaves the WHOLE handler via `br` (carrying the
                    // new state id to the enclosing result-i32 block), so a fully
                    // diverging if never needs to yield a value itself. Typing an
                    // if i32 merely because *it* always diverges is wrong when it
                    // is nested inside a void/fall-through branch: the branching
                    // goto path would then leave a stray i32 ('expected 0
                    // elements on the stack for fallthru, found 1').
                    body_.byte(kEmptyBlock);
                    ++label_depth_;
                    const bool then_ok = !s.then_region || emit_region(*s.then_region);
                    bool else_ok = true;
                    if (then_ok && s.else_region) {
                        body_.byte(kOpElse);
                        else_ok = emit_region(*s.else_region);
                    }
                    --label_depth_;
                    if (!then_ok || !else_ok) {
                        return false;
                    }
                    body_.byte(kOpEnd);
                    return true;
                },
                [&](const CoreGotoStmt &go) {
                    if (fn_mode_) {
                        return reject("a goto is illegal inside an fn body",
                                      statement.source_range);
                    }
                    record_target(go.target);
                    emit_goto_transition(go);
                    return true;
                },
                [&](const CoreTrapStmt &) {
                    body_.byte(kOpUnreachable);
                    return true;
                },
                [&](const CoreMatchStmt &s) { return emit_match(s, statement.source_range); },
                // RFC 0026 FB-4: a capability invocation INSIDE an outlined
                // effect fn routes through the same ahfl_cap import sequence.
                // (A handler capability call keeps its canonical KR6.5 final
                // shape and is rejected here in handler mode.)
                [&](const CoreCapabilityCallStmt &s) {
                    return emit_capability_call(s, statement.source_range);
                },
                // RFC 0026 FB-4: ordered effectful fn call (a plain `call` to
                // an outlined effect fn; its result is set into a local).
                [&](const CoreCallStmt &s) {
                    return emit_call_stmt(s, statement.source_range);
                },
                [&](const CoreStoreStmt &s) { return emit_store(s, statement.source_range); },
                [&](const CoreReturnStmt &s) {
                    if (fn_mode_) {
                        if (!s.has_value) {
                            return reject("an fn body must return a value",
                                          statement.source_range);
                        }
                        if (!emit_value_read(s.value, statement.source_range)) {
                            return false;
                        }
                        body_.byte(kOpReturn);
                        return true;
                    }
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

// Out-of-line: a pre-bound fn parameter occupies wasm local 1 + `ordinal`,
// where `ordinal` is the parameter's FLAT WORD index among the functype
// arguments (local 0 is env). A single-word parameter takes one local; a
// Closure parameter takes TWO consecutive i32 locals (func_index at 1+ordinal,
// env_ptr at 1+ordinal+1). Parameters never consume SSA-pool slots; the
// non-param pool begins at 1 + total param words.
[[nodiscard]] inline bool P6ComputationHandlerBuilder::bind_param(
    CoreValueId value, std::uint32_t ordinal, P6ScalarKind kind) {
    if (value.value >= locals_.size() || locals_[value.value].bound) {
        return false;
    }
    LocalInfo &info = locals_[value.value];
    info.bound = true;
    info.is_param = true;
    info.kind = kind;
    info.slot = ordinal;
    info.is_word_pair = kind == P6ScalarKind::Closure;
    used_values_[value.value] = true;
    return true;
}

// Full definition of the FB-1 outlined-fn compilation record (forward-declared
// above so AgentPlan can hold a vector of them).
struct CompiledFn {
    CoreFnId id{};
    std::vector<std::uint8_t> body;
    std::vector<P6ScalarKind> param_words;
    P6ScalarKind result_word{P6ScalarKind::IntI32};
    std::vector<CoreInstanceId> callees;
};

// The single-word P6 result every value-bearing return of `fn` carries, or a
// reason string. The Core verifier proves the body completes via consistent
// value returns; this derives the physical result word for the wasm functype.
struct FnReturnTermination {
    bool ok{false};
    P6ScalarKind result_word{P6ScalarKind::IntI32};
    std::string reason;
};

[[nodiscard]] FnReturnTermination
fn_return_termination(const CoreProgram &program,
                      const ir::core::CoreLayoutTable &layouts,
                      const CoreFnDecl &fn) {
    std::optional<P6ScalarKind> word;
    bool found = false;
    bool mismatch = false;
    bool closure_result = false;
    const auto scan = [&](auto &&self, const CoreRegion &region) -> void {
        for (const CoreStmt &stmt : region.statements) {
            if (const auto *ret = std::get_if<CoreReturnStmt>(&stmt.node);
                ret != nullptr && ret->has_value) {
                if (ret->value.value >= fn.storage.value_types.size()) {
                    mismatch = true;
                    continue;
                }
                found = true;
                const auto k =
                    p6_scalar_kind(program, layouts, fn.storage.value_types[ret->value.value]);
                if (k == std::nullopt) {
                    mismatch = true;
                    continue;
                }
                // FB-3b fail-closed boundary: a closure is a TWO-word
                // (func_index, env_ptr) value, so a closure-returning fn cannot
                // have the one-word functype this encoder emits (it would
                // declare one i32 result while the body pushes two). Reject at
                // this functype decision rather than relying on the upstream
                // Core verifier / call-site gates, so a partially-validated or
                // hand-built artifact can never carry an arity-inconsistent
                // functype or silently drop the env word. Multi-value results
                // are a later slice.
                if (*k == P6ScalarKind::Closure) {
                    closure_result = true;
                    mismatch = true;
                    continue;
                }
                if (word.has_value() && *word != *k) {
                    mismatch = true;
                }
                word = k;
            }
            if (const auto *branch = std::get_if<CoreIfStmt>(&stmt.node)) {
                if (branch->then_region) {
                    self(self, *branch->then_region);
                }
                if (branch->else_region) {
                    self(self, *branch->else_region);
                }
            }
            if (const auto *match = std::get_if<CoreMatchStmt>(&stmt.node)) {
                for (const CoreMatchArm &arm : match->arms) {
                    if (arm.body) {
                        self(self, *arm.body);
                    }
                }
                if (match->fallback_region) {
                    self(self, *match->fallback_region);
                }
            }
        }
    };
    scan(scan, fn.body);
    if (closure_result) {
        return FnReturnTermination{false, P6ScalarKind::IntI32,
                                   "returns a closure value, which needs multi-value "
                                   "(func_index + env_ptr) and is fail-closed in FB-3b; "
                                   "a closure result cannot cross the single-word functype"};
    }
    if (mismatch) {
        // Two value-bearing returns disagree on their physical word (or one is
        // not a single-word P6 value). The verifier rejects this at the fn
        // definition; codegen must never silently keep the first word and emit a
        // functype that contradicts the other returns.
        return FnReturnTermination{false, P6ScalarKind::IntI32,
                                   "has value-bearing returns with disagreeing result words; "
                                   "every return must carry the same single-word type"};
    }
    if (!found || !word.has_value()) {
        return FnReturnTermination{false, P6ScalarKind::IntI32,
                                   "has no single-word value-bearing return on every path"};
    }
    return FnReturnTermination{true, *word, {}};
}

// FB-1 fix-forward: worst-case aggregate-heap bytes one computed-handler
// activation (one step) can bump. The bump heap never reclaims within a step,
// so EVERY reachable activation's constructs are counted, including results on
// branches that do not both execute (a deliberate over-approximation) and
// bounded-recursion activations (multiplied by the sealed SCC depth). Tree
// recursion (>=2 internal call sites in one member) fans out geometrically and
// is rejected through the fixed-page budget rather than under-counted. Returns
// UINT64_MAX after emitting a RESOURCE diagnostic on saturation/overflow.
[[nodiscard]] std::uint64_t
compute_fn_construct_heap_budget(const CoreProgram &program,
                                 const std::vector<std::uint32_t> &dynamic_bytes_by_fn,
                                 std::span<const CoreBodyStorage *const> entry_storages,
                                 const std::vector<bool> &reachable_fn,
                                 const ir::core::FnRecursionAnalysis &recursion,
                                 CoreWasmCodegenResult &result) {
    constexpr std::uint64_t kCap = ir::core::kCoreWasmFixedLinearMemoryCapacityBytes;
    const auto sat_mul = [](std::uint64_t a, std::uint64_t b) -> std::uint64_t {
        if (a != 0 && b > std::numeric_limits<std::uint64_t>::max() / a) {
            return std::numeric_limits<std::uint64_t>::max();
        }
        return a * b;
    };
    const auto sat_add = [](std::uint64_t a, std::uint64_t b) -> std::uint64_t {
        if (a > std::numeric_limits<std::uint64_t>::max() - b) {
            return std::numeric_limits<std::uint64_t>::max();
        }
        return a + b;
    };

    const std::size_t cc = recursion.components.size();
    const auto &component_of = recursion.component_of;

    // Resolve an instance to its fn's component, or -1.
    const auto instance_component = [&](CoreInstanceId id) -> std::int64_t {
        if (id.value >= program.instances.size()) {
            return -1;
        }
        const auto *payload =
            std::get_if<CoreFnInstance>(&program.instances[id.value].payload);
        if (payload == nullptr || payload->body.value == CoreFnId::kInvalid ||
            payload->body.value >= program.fns.size()) {
            return -1;
        }
        const std::uint32_t f = payload->body.value;
        if (f < reachable_fn.size() && !reachable_fn[f]) {
            return -1;
        }
        return static_cast<std::int64_t>(component_of[f]);
    };

    // FB-3b: every invocation expr's possible target COMPONENTS. A direct
    // CoreCallExpr contributes one (its resolved callee); a
    // CoreCallClosureExpr contributes one per closure its callee slot can hold
    // (the flow-sensitive call_indirect points-to set — the SAME conservative
    // set the recursion lattice sealed), so the activation/heap multiplicity
    // counts an indirect cycle exactly like a direct one. Each occurrence is
    // one possible runtime activation.
    const ir::core::ClosurePointsTo points_to =
        ir::core::ClosurePointsTo::analyze(program);
    const auto expr_target_components =
        [&](const CoreBodyStorage &storage, const CoreExpr &expr) {
            std::vector<std::int64_t> out;
            if (const auto *call = std::get_if<CoreCallExpr>(&expr.node)) {
                const std::int64_t c = instance_component(call->callee);
                if (c >= 0) {
                    out.push_back(c);
                }
                return out;
            }
            if (const auto *indirect_call =
                    std::get_if<CoreCallClosureExpr>(&expr.node)) {
                for (const std::uint32_t target :
                     points_to.targets_of(storage, *indirect_call)) {
                    if (target < reachable_fn.size() && reachable_fn[target]) {
                        out.push_back(static_cast<std::int64_t>(component_of[target]));
                    }
                }
            }
            return out;
        };

    // Per-component facts.
    std::vector<std::uint32_t> depth(cc, 1);
    std::vector<std::uint64_t> bytes_max(cc, 0);
    std::vector<std::uint64_t> internal_branch(cc, 0); // max internal sites/member
    for (std::uint32_t ci = 0; ci < cc; ++ci) {
        const auto &members = recursion.components[ci];
        // A component is nontrivial iff it was sealed as a recursion group
        // (fn_depth_bound >= 0 for its members); a trivial acyclic component
        // stays at depth 1.
        const bool nontrivial =
            std::any_of(members.begin(), members.end(), [&](std::uint32_t f) {
                return f < recursion.fn_depth_bound.size() &&
                       recursion.fn_depth_bound[f] >= 0;
            });
        std::uint64_t sites_max = 0;
        for (const std::uint32_t f : members) {
            if (f < reachable_fn.size() && reachable_fn[f]) {
                bytes_max[ci] =
                    std::max<std::uint64_t>(bytes_max[ci], dynamic_bytes_by_fn[f]);
            }
            if (nontrivial) {
                const std::int64_t bound = recursion.fn_depth_bound[f];
                if (bound > 0) {
                    depth[ci] = std::max<std::uint32_t>(
                        depth[ci], static_cast<std::uint32_t>(bound));
                }
            }
            // Count this member's internal invocation SITES (one direct
            // CoreCallExpr = 1, one CoreCallClosureExpr = the number of its
            // possible targets inside C), so branching is preserved for both
            // direct and call_indirect cycles.
            std::uint64_t sites = 0;
            for (const CoreExpr &expr : program.fns[f].storage.exprs) {
                for (const std::int64_t target_c :
                     expr_target_components(program.fns[f].storage, expr)) {
                    if (target_c == static_cast<std::int64_t>(ci)) {
                        ++sites;
                    }
                }
            }
            sites_max = std::max(sites_max, sites);
        }
        internal_branch[ci] = nontrivial ? sites_max : 0;
    }

    // G(C): total activations inside C per external entry. b==1 -> depth;
    // b>=2 -> geometric sum 1+b+...+b^(d-1).
    std::vector<std::uint64_t> activations_per_entry(cc, 1);
    for (std::uint32_t ci = 0; ci < cc; ++ci) {
        const std::uint64_t b = internal_branch[ci];
        if (b <= 1) {
            activations_per_entry[ci] = depth[ci];
            continue;
        }
        std::uint64_t total = 0;
        std::uint64_t power = 1;
        for (std::uint32_t k = 0; k < depth[ci]; ++k) {
            total = sat_add(total, power);
            power = sat_mul(power, b);
            if (total == std::numeric_limits<std::uint64_t>::max()) {
                break;
            }
        }
        activations_per_entry[ci] = total;
    }

    // Root sites: one invocation in an entry (handler/workflow) storage per
    // possible target component (a closure call contributes one per matching
    // construction).
    std::vector<std::uint64_t> entries(cc, 0);
    for (const CoreBodyStorage *storage : entry_storages) {
        if (storage == nullptr) {
            continue;
        }
        for (const CoreExpr &expr : storage->exprs) {
            for (const std::int64_t c : expr_target_components(*storage, expr)) {
                entries[c] = sat_add(entries[c], 1);
            }
        }
    }

    // External site counts D -> C (one per possible target per invocation
    // expr; direct = 1, closure call = 1..N).
    std::vector<std::vector<std::pair<std::uint32_t, std::uint64_t>>> ext(cc);
    std::vector<std::uint32_t> indegree(cc, 0);
    for (std::uint32_t d = 0; d < cc; ++d) {
        std::unordered_map<std::uint32_t, std::uint64_t> per_target;
        for (const std::uint32_t f : recursion.components[d]) {
            for (const CoreExpr &expr : program.fns[f].storage.exprs) {
                for (const std::int64_t c :
                     expr_target_components(program.fns[f].storage, expr)) {
                    if (c >= 0 && static_cast<std::uint32_t>(c) != d) {
                        per_target[static_cast<std::uint32_t>(c)] =
                            sat_add(per_target[static_cast<std::uint32_t>(c)], 1);
                    }
                }
            }
        }
        for (const auto &[target, count] : per_target) {
            ext[d].push_back({target, count});
            ++indegree[target];
        }
    }

    // Propagation over the condensation DAG (Kahn): mult[C] = root sites into C
    // plus every (total activation of D) * external D->C sites.
    std::vector<std::uint64_t> mult = entries;
    std::vector<std::uint32_t> queue;
    for (std::uint32_t ci = 0; ci < cc; ++ci) {
        if (indegree[ci] == 0) {
            queue.push_back(ci);
        }
    }
    std::uint32_t visited = 0;
    while (!queue.empty()) {
        const std::uint32_t d = queue.back();
        queue.pop_back();
        ++visited;
        const std::uint64_t total_activ_d = sat_mul(mult[d], activations_per_entry[d]);
        for (const auto &[target, count] : ext[d]) {
            mult[target] =
                sat_add(mult[target], sat_mul(total_activ_d, count));
            if (--indegree[target] == 0) {
                queue.push_back(target);
            }
        }
    }
    if (visited != cc) {
        add_diag(result,
                 core_wasm_diag::kInvalidCore,
                 "internal error: the fn-call condensation is not acyclic while "
                 "computing the aggregate heap budget");
        return std::numeric_limits<std::uint64_t>::max();
    }

    std::uint64_t total = 0;
    for (std::uint32_t ci = 0; ci < cc; ++ci) {
        const std::uint64_t component_bytes =
            sat_mul(sat_mul(mult[ci], activations_per_entry[ci]), bytes_max[ci]);
        total = sat_add(total, component_bytes);
    }
    if (total > kCap) {
        add_diag(result,
                 core_wasm_diag::kResourceExhausted,
                 "the outlined-fn aggregate activation tree can bump " +
                     std::to_string(total) +
                     " bytes (branching recursion counts geometrically); bound the "
                     "recursion or reduce aggregate sizes to fit the 64 KiB page");
        return std::numeric_limits<std::uint64_t>::max();
    }
    return total;
}

// Compile the outlined pure fn bodies reachable from the module's ENTRY
// storages (an agent module's one shared flow storage; a workflow module's
// workflow storage plus its packaged agent flows). Only fns the direct
// CoreCallExpr closure reaches are emitted; the closure is a deterministic
// worklist fixed point over the static fn call graph (its recursion groups are
// SCCs sealed with a finite static depth by analyze_fn_recursion), and
// ordinals are assigned in ascending CoreFnId order so the module is
// reproducible. A module with no direct calls emits zero fn bodies and stays
// byte-identical to its E1-E3/P6 shape.
// RFC 0026 FB-4: compute ONLY the reachable fn ids (no emission), seeded from
// the entry storages' direct calls / closures and the planned computed
// handlers' recorded callees. Used to size the capability import table BEFORE
// the function base is fixed, so an effectful fn's capability is imported at a
// stable low ordinal (the same import sequence the handler path uses).
[[nodiscard]] std::vector<CoreFnId>
compute_reachable_fn_ids(const CoreProgram &program,
                         std::span<const CoreBodyStorage *const> entry_storages,
                         std::span<const std::vector<CoreInstanceId>> planned_entry_callees,
                         const CoreFlowDecl *target_flow = nullptr) {
    std::vector<CoreInstanceId> worklist;
    std::vector<bool> queued(program.instances.size(), false);
    const auto enqueue = [&](CoreInstanceId id) {
        if (id.value < queued.size() && !queued[id.value]) {
            queued[id.value] = true;
            worklist.push_back(id);
        }
    };
    const auto enqueue_fn_id = [&](CoreFnId fn_id) {
        if (fn_id.value < program.fns.size()) {
            enqueue(program.fns[fn_id.value].instance);
        }
    };
    const auto enqueue_region = [&](const CoreRegion &region) {
        for_each_region_statement(region, [&](const CoreStmt &stmt) {
            if (const auto *call = std::get_if<CoreCallStmt>(&stmt.node)) {
                enqueue(call->callee);
            }
        });
    };
    for (const CoreBodyStorage *storage : entry_storages) {
        if (storage == nullptr) {
            continue;
        }
        for (const CoreExpr &expr : storage->exprs) {
            if (const auto *call = std::get_if<CoreCallExpr>(&expr.node)) {
                enqueue(call->callee);
            }
            if (const auto *closure = std::get_if<CoreClosureExpr>(&expr.node)) {
                enqueue_fn_id(closure->fn);
            }
        }
    }
    // Seed ordered CoreCallStmt roots from ONLY the compiled target flow's
    // handler regions. Iterating every flow here pulled effect fns reachable
    // from a DIFFERENT agent's handlers into this agent's import plan,
    // over-declaring capabilities the target agent never reaches (least
    // privilege / E2 manifest drift). entry_storages already scopes the pure
    // arena roots to the single compiled flow; this keeps statement roots in
    // the same scope. A null target_flow (workflow / non-agent entries) has no
    // per-flow handler statements to seed from.
    if (target_flow != nullptr) {
        for (const CoreFlowState &state : target_flow->states) {
            enqueue_region(state.body);
        }
    }
    for (const auto &callees : planned_entry_callees) {
        for (const CoreInstanceId id : callees) {
            enqueue(id);
        }
    }
    std::vector<bool> seen_fn(program.fns.size(), false);
    std::vector<CoreFnId> reachable;
    while (!worklist.empty()) {
        const CoreInstanceId id = worklist.back();
        worklist.pop_back();
        if (id.value >= program.instances.size()) {
            continue;
        }
        const auto *payload = std::get_if<CoreFnInstance>(&program.instances[id.value].payload);
        if (payload == nullptr || payload->body.value == CoreFnId::kInvalid ||
            payload->body.value >= program.fns.size()) {
            continue;
        }
        const CoreFnId fn_id = payload->body;
        if (seen_fn[fn_id.value]) {
            continue;
        }
        seen_fn[fn_id.value] = true;
        reachable.push_back(fn_id);
        const CoreFnDecl &fn = program.fns[fn_id.value];
        for (const CoreExpr &expr : fn.storage.exprs) {
            if (const auto *call = std::get_if<CoreCallExpr>(&expr.node)) {
                enqueue(call->callee);
            }
            if (const auto *closure = std::get_if<CoreClosureExpr>(&expr.node)) {
                enqueue_fn_id(closure->fn);
            }
        }
        enqueue_region(fn.body);
    }
    std::sort(reachable.begin(), reachable.end(),
              [](CoreFnId a, CoreFnId b) { return a.value < b.value; });
    return reachable;
}

// The sorted, de-duplicated capabilities reached by a set of reachable fns.
[[nodiscard]] std::vector<CoreCapabilityId>
capabilities_of_reachable_fns(const CoreProgram &program,
                              const std::vector<CoreFnId> &reachable) {
    std::vector<CoreCapabilityId> out;
    if (reachable.empty()) {
        return out;
    }
    const ir::core::FnEffectAnalysis effects = ir::core::analyze_fn_effects(program);
    for (const CoreFnId fn_id : reachable) {
        if (fn_id.value >= effects.capabilities.size()) {
            continue;
        }
        for (const CoreCapabilityId cap : effects.capabilities[fn_id.value]) {
            out.push_back(cap);
        }
    }
    std::sort(out.begin(), out.end(), [](CoreCapabilityId a, CoreCapabilityId b) {
        return a.value < b.value;
    });
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

[[nodiscard]] bool compile_reachable_fn_bodies(
    const CoreProgram &program,
    const ir::core::CoreLayoutTable &layouts,
    std::vector<CompiledFn> &out,
    std::span<const CoreBodyStorage *const> entry_storages,
    std::span<const std::vector<CoreInstanceId>> planned_entry_callees,
    std::span<P6ComputationHandlerBuilder *const> entry_builders,
    std::uint32_t fn_function_base,
    CoreWasmCodegenResult &result,
    bool &construct_heap_enabled_out,
    std::uint32_t &construct_heap_base_out,
    std::vector<CoreFnId> &closure_table_out,
    std::vector<ClosureCallType> &closure_signatures_out,
    std::uint32_t &entry_closure_env_bytes_out,
    const std::vector<CoreCapabilityId> *imports = nullptr) {
    construct_heap_enabled_out = false;
    construct_heap_base_out = ir::core::kNodeEventLogBase;
    closure_table_out.clear();
    closure_signatures_out.clear();
    entry_closure_env_bytes_out = 0;
    std::vector<CoreInstanceId> worklist;
    std::vector<bool> queued(program.instances.size(), false);
    const auto enqueue = [&](CoreInstanceId id) {
        if (id.value >= queued.size() || queued[id.value]) {
            return;
        }
        queued[id.value] = true;
        worklist.push_back(id);
    };
    // Roots from the raw entry storages (covers identity/capability finals
    // whose bodies are validated, not built with the P6 builder) ...
    const auto enqueue_fn_id = [&](CoreFnId fn_id) {
        if (fn_id.value >= program.fns.size()) {
            return;
        }
        enqueue(program.fns[fn_id.value].instance);
    };
    for (const CoreBodyStorage *storage : entry_storages) {
        if (storage == nullptr) {
            continue;
        }
        for (const CoreExpr &expr : storage->exprs) {
            if (const auto *call = std::get_if<CoreCallExpr>(&expr.node)) {
                enqueue(call->callee);
            }
            // FB-3b: a lifted fn / static-fn reference whose closure is
            // constructed in an entry handler must itself be emitted (its
            // funcidx goes into the funcref table).
            if (const auto *closure = std::get_if<CoreClosureExpr>(&expr.node)) {
                enqueue_fn_id(closure->fn);
            }
        }
    }
    // ... plus the callees the planned computed handlers recorded while
    // planning (their builder walked the body and validated each call).
    for (const auto &callees : planned_entry_callees) {
        for (const CoreInstanceId id : callees) {
            enqueue(id);
        }
    }

    std::vector<CoreFnId> reachable;
    std::vector<bool> seen_fn(program.fns.size(), false);
    const auto resolve = [&](CoreInstanceId id) -> const CoreFnDecl * {
        if (id.value >= program.instances.size()) {
            add_diag(result,
                     core_wasm_diag::kInvalidCore,
                     "direct call references an out-of-range fn instance during wasm planning");
            return nullptr;
        }
        const auto *payload = std::get_if<CoreFnInstance>(&program.instances[id.value].payload);
        if (payload == nullptr || payload->body.value == CoreFnId::kInvalid ||
            payload->body.value >= program.fns.size()) {
            add_diag(result,
                     core_wasm_diag::kUnsupportedOrchestration,
                     "direct call target has no outlined fn body for the wasm computation lane");
            return nullptr;
        }
        return &program.fns[payload->body.value];
    };
    while (!worklist.empty()) {
        const CoreInstanceId id = worklist.back();
        worklist.pop_back();
        const CoreFnDecl *fn = resolve(id);
        if (fn == nullptr) {
            return false;
        }
        if (seen_fn[fn->id.value]) {
            continue;
        }
        seen_fn[fn->id.value] = true;
        reachable.push_back(fn->id);
        for (const CoreExpr &expr : fn->storage.exprs) {
            if (const auto *call = std::get_if<CoreCallExpr>(&expr.node)) {
                enqueue(call->callee);
            }
            // FB-3b: a fn may construct a closure over another lifted fn (or a
            // zero-capture static fn); its body must be emitted and tabled too.
            if (const auto *closure = std::get_if<CoreClosureExpr>(&expr.node)) {
                enqueue_fn_id(closure->fn);
            }
        }
        // RFC 0026 FB-4: an outlined fn's ordered effectful calls are reachable
        // fn bodies too (including a fn that only wraps another effectful fn).
        for_each_region_statement(fn->body, [&](const CoreStmt &stmt) {
            if (const auto *call = std::get_if<CoreCallStmt>(&stmt.node)) {
                enqueue(call->callee);
            }
        });
    }
    std::sort(reachable.begin(), reachable.end(), [](CoreFnId a, CoreFnId b) {
        return a.value < b.value;
    });
    if (reachable.empty()) {
        return true;
    }

    // RFC 0026 FB-2 (design §8.1-6c / §5.4): the recursion depth lattice sealed
    // the static call depth of every recursive fn group from bounded-container
    // capacities / literals. Recursion runs as ordinary native `call`s, so the
    // reachable direct-fn graph's worst-case call depth must fit the engine-safe
    // native stack budget; an over-deep plan is RESOURCE-fail-closed here rather
    // than overflowing the engine stack at runtime. This consumes the SAME
    // analysis the verifier ran (never a second derivation). The FB-3 slice
    // extends this budget with the closure-env linear-memory accounting.
    ir::core::FnRecursionAnalysis recursion_analysis;
    {
        recursion_analysis = ir::core::analyze_fn_recursion(program);
        if (!recursion_analysis.unbounded_issues.empty() ||
            !recursion_analysis.overflow_sccs.empty()) {
            add_diag(result,
                     core_wasm_diag::kInvalidCore,
                     "an outlined fn recursion group reached by this module is not structurally "
                     "bounded; the Core verifier must seal the depth lattice before codegen");
            return false;
        }
        const std::uint64_t native_depth =
            ir::core::max_native_fn_call_depth(program, recursion_analysis, &seen_fn);
        if (native_depth > ir::core::kFnRecursionNativeStackDepthMax) {
            add_diag(result,
                     core_wasm_diag::kResourceExhausted,
                     "the bounded-recursion plan reaches a native call depth of " +
                         std::to_string(native_depth) + " which exceeds the engine-safe limit of " +
                         std::to_string(ir::core::kFnRecursionNativeStackDepthMax) +
                         "; use a smaller bounded collection capacity");
            return false;
        }
    }

    // callee CoreInstanceId -> fn ordinal (CoreFnId order), for the builder's
    // `call` emission. The absolute function base is injected at section emit;
    // emit_direct_call adds it, so plan against a zero base here.
    std::vector<std::uint32_t> instance_to_ordinal(
        program.instances.size(), std::numeric_limits<std::uint32_t>::max());
    for (std::uint32_t ordinal = 0; ordinal < reachable.size(); ++ordinal) {
        instance_to_ordinal[program.fns[reachable[ordinal].value].instance.value] = ordinal;
    }

    // Phase A: construct + plan every fn builder and derive its functype /
    // per-activation construct bytes BEFORE any body is emitted, so the
    // per-activation aggregate heap budget (FB-1 fix-forward) can be checked
    // module-wide once. Builders are retained and emitted in Phase B.
    struct PlannedFn {
        CoreFnId id{};
        std::unique_ptr<P6ComputationHandlerBuilder> builder;
        std::vector<P6ScalarKind> param_words;
        P6ScalarKind result_word{P6ScalarKind::IntI32};
        std::vector<CoreInstanceId> callees;
        std::uint32_t dynamic_bytes{0};
        std::uint32_t closure_env_bytes{0};
        std::vector<CoreFnId> closure_targets;
        std::vector<ClosureCallType> closure_call_types;
    };
    std::vector<PlannedFn> planned_fns;
    planned_fns.reserve(reachable.size());
    for (std::uint32_t ordinal = 0; ordinal < reachable.size(); ++ordinal) {
        const CoreFnDecl &fn = program.fns[reachable[ordinal].value];
        std::vector<bool> used_exprs(fn.storage.exprs.size(), false);
        std::vector<bool> used_values(fn.storage.value_count, false);
        const FnBodyView view{&fn.storage, &fn.body, fn.name};
        auto builder = std::make_unique<P6ComputationHandlerBuilder>(program,
                                           layouts,
                                           view,
                                           used_exprs,
                                           used_values,
                                           result,
                                           &instance_to_ordinal,
                                           &fn_function_base);
        std::vector<P6ScalarKind> param_words;
        param_words.reserve(fn.params.size());
        // Flat functype ARGUMENT word ordinal (env local 0 excluded). A closure
        // parameter occupies two i32 words, so its ordinal advances by two.
        std::uint32_t param_word_ordinal = 0;
        for (std::uint32_t i = 0; i < fn.params.size(); ++i) {
            const CoreValueId param = fn.params[i];
            if (param.value >= fn.storage.value_types.size()) {
                add_diag(result,
                         core_wasm_diag::kInvalidCore,
                         "fn parameter value is out of range for its body storage");
                return false;
            }
            const auto word =
                p6_scalar_kind(program, layouts, fn.storage.value_types[param.value]);
            if (word == std::nullopt) {
                add_diag(result,
                         core_wasm_diag::kUnsupportedOrchestration,
                         "fn '" + fn.name +
                             "' crosses a boundary with a non-representable parameter "
                             "(String / f64 / multi-word types are rejected)");
                return false;
            }
            param_words.push_back(*word);
            if (!builder->bind_param(param, param_word_ordinal, *word)) {
                add_diag(result,
                         core_wasm_diag::kInvalidCore,
                         "fn parameter could not be bound into the wasm local table");
                return false;
            }
            param_word_ordinal += (*word == P6ScalarKind::Closure) ? 2u : 1u;
        }
        // FB-3b: bind a lifted fn's pre-bound environment slots (parallel to
        // fn.env_bindings / fn.captures) from the P4-D env aggregate offsets,
        // BEFORE plan() walks the body. These consume declared-local pool
        // slots, never logical functype parameters.
        if (!fn.env_bindings.empty()) {
            const ir::core::CoreLayout *env_layout =
                p6_closure_environment_layout(program, layouts, fn);
            const ir::core::CoreLayoutStruct *env_struct =
                p6_closure_environment_struct(env_layout);
            if (env_struct == nullptr ||
                env_struct->field_offsets.size() != fn.env_bindings.size()) {
                add_diag(result,
                         core_wasm_diag::kInvalidCore,
                         "lifted fn '" + fn.name +
                             "' has env bindings but no matching finalized environment layout");
                return false;
            }
            for (std::uint32_t i = 0; i < fn.env_bindings.size(); ++i) {
                const CoreValueId slot = fn.env_bindings[i];
                if (slot.value >= fn.storage.value_types.size()) {
                    add_diag(result,
                             core_wasm_diag::kInvalidCore,
                             "lifted fn env binding is out of range for its body storage");
                    return false;
                }
                const auto slot_word =
                    p6_scalar_kind(program, layouts, fn.storage.value_types[slot.value]);
                if (slot_word == std::nullopt) {
                    add_diag(result,
                             core_wasm_diag::kUnsupportedOrchestration,
                             "lifted fn '" + fn.name +
                                 "' captures a non-representable value "
                                 "(String / f64 captures cross no boundary in this slice)");
                    return false;
                }
                const std::uint64_t field_offset = env_struct->field_offsets[i];
                if (field_offset > std::numeric_limits<std::uint32_t>::max()) {
                    add_diag(result,
                             core_wasm_diag::kInvalidCore,
                             "lifted fn environment slot offset exceeds the wasm32 domain");
                    return false;
                }
                if (!builder->bind_env_binding(
                        slot, static_cast<std::uint32_t>(field_offset), *slot_word)) {
                    add_diag(result,
                             core_wasm_diag::kInvalidCore,
                             "lifted fn env binding could not be bound into the wasm local table");
                    return false;
                }
            }
        }
        if (!builder->plan()) {
            return false;
        }
        const FnReturnTermination termination = fn_return_termination(program, layouts, fn);
        if (!termination.ok) {
            add_diag(result,
                     core_wasm_diag::kUnsupportedOrchestration,
                     "fn '" + fn.name + "' " + termination.reason);
            return false;
        }
        PlannedFn planned;
        planned.id = reachable[ordinal];
        planned.param_words = std::move(param_words);
        planned.result_word = termination.result_word;
        planned.callees = builder->fn_callees();
        planned.dynamic_bytes = builder->dynamic_construct_bytes();
        planned.closure_env_bytes = builder->closure_env_bytes();
        planned.closure_targets = builder->closure_targets();
        planned.closure_call_types = builder->closure_call_types();
        planned.builder = std::move(builder);
        planned_fns.push_back(std::move(planned));
    }

    // FB-3b: finalize the funcref table and the call_indirect functype set
    // across EVERY reachable body (entry handlers + outlined fns). The table
    // holds the reachable fns whose address a CoreClosureExpr takes; dense
    // slots are assigned in ascending CoreFnId order (deterministic and
    // independent of internal fn-index placement). The call_indirect functypes
    // are deduplicated and indexed in deterministic body-visit order (entry
    // handlers first, then outlined fns in ordinal order).
    std::unordered_map<std::uint32_t, std::uint32_t> fn_to_table_slot;
    {
        std::vector<CoreFnId> targets;
        const auto add_target = [&](CoreFnId id) {
            if (id.value < seen_fn.size() && seen_fn[id.value]) {
                targets.push_back(id);
            }
        };
        for (const P6ComputationHandlerBuilder *entry_builder : entry_builders) {
            if (entry_builder != nullptr) {
                for (const CoreFnId id : entry_builder->closure_targets()) {
                    add_target(id);
                }
            }
        }
        for (const PlannedFn &planned : planned_fns) {
            for (const CoreFnId id : planned.closure_targets) {
                add_target(id);
            }
        }
        std::sort(targets.begin(), targets.end(), [](CoreFnId a, CoreFnId b) {
            return a.value < b.value;
        });
        targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
        closure_table_out = targets;
        for (std::uint32_t slot = 0; slot < targets.size(); ++slot) {
            fn_to_table_slot.emplace(targets[slot].value, slot);
        }
    }
    std::unordered_map<std::string, std::uint32_t> closure_type_index_by_key;
    {
        std::vector<ClosureCallType> signatures;
        const auto add_signatures = [&](const std::vector<ClosureCallType> &types) {
            for (const ClosureCallType &type : types) {
                std::string key = closure_call_type_key(type);
                if (closure_type_index_by_key.try_emplace(key, 0).second) {
                    signatures.push_back(type);
                }
            }
        };
        for (const P6ComputationHandlerBuilder *entry_builder : entry_builders) {
            if (entry_builder != nullptr) {
                add_signatures(entry_builder->closure_call_types());
            }
        }
        for (const PlannedFn &planned : planned_fns) {
            add_signatures(planned.closure_call_types);
        }
        for (std::uint32_t i = 0; i < signatures.size(); ++i) {
            closure_type_index_by_key[
                closure_call_type_key(signatures[i])] = i;
        }
        closure_signatures_out = std::move(signatures);
    }
    // Once-per-step closure-env bytes the ENTRY handlers bump (fn-body envs
    // are already folded into the per-activation recursion budget below).
    for (const P6ComputationHandlerBuilder *entry_builder : entry_builders) {
        if (entry_builder != nullptr) {
            entry_closure_env_bytes_out += entry_builder->closure_env_bytes();
        }
    }

    // FB-1 fix-forward: the per-activation aggregate heap. When at least one
    // reachable fn constructs an aggregate, its constructs are bumped at run
    // time (never static scratch, which aliases across native call
    // activations). The heap base must sit above every reserved frame AND the
    // bounded-collection backing high-water; the worst-case bump of one
    // handler activation tree (sealed recursion depths included) must fit the
    // fixed page. Both are checked here against the ONE page budget.
    bool construct_heap_enabled = false;
    for (const PlannedFn &planned : planned_fns) {
        if (planned.dynamic_bytes != 0 || planned.closure_env_bytes != 0) {
            construct_heap_enabled = true;
            break;
        }
    }
    if (entry_closure_env_bytes_out != 0) {
        construct_heap_enabled = true;
    }
    std::uint32_t construct_heap_base = ir::core::kNodeEventLogBase;
    if (construct_heap_enabled) {
        std::uint64_t backing_high = ir::core::kP6CollectionBackingBase;
        const auto account_storage = [&](const CoreBodyStorage &storage) {
            for (const CoreValueTypeId vt : storage.value_types) {
                const auto *container = p6_container_layout(program, layouts, vt);
                if (container == nullptr) {
                    continue;
                }
                backing_high = std::max<std::uint64_t>(
                    backing_high,
                    ir::core::kP6CollectionBackingBase + container->backing_size);
            }
        };
        for (const CoreBodyStorage *storage : entry_storages) {
            if (storage != nullptr) {
                account_storage(*storage);
            }
        }
        for (const PlannedFn &planned : planned_fns) {
            account_storage(program.fns[planned.id.value].storage);
        }
        construct_heap_base =
            static_cast<std::uint32_t>((backing_high + 7u) & ~static_cast<std::uint64_t>(7u));

        std::vector<std::uint32_t> dynamic_bytes_by_fn(program.fns.size(), 0);
        for (const PlannedFn &planned : planned_fns) {
            // Per-activation bump of one fn activation = its dynamic aggregates
            // PLUS the closure environments it constructs. Both share the
            // module arena and the recursion-depth multiplicity below, so a
            // recursive lifted fn allocating fresh envs is bounded by the same
            // single-page gate (design §6.3 / FB-2 depth product).
            dynamic_bytes_by_fn[planned.id.value] =
                planned.dynamic_bytes + planned.closure_env_bytes;
        }
        // Worst-case aggregate/env bytes one computed handler can bump in a
        // single step (one whole activation tree). Per-fn activation
        // multiplicity is derived over the SAME sealed recursion analysis
        // (handler call sites are roots; intra-SCC edges multiply by the sealed
        // depth). This is a deliberate over-approximation: the heap never
        // reclaims within a step, so every (possibly dead) slot is counted.
        std::uint64_t budget = compute_fn_construct_heap_budget(
            program, dynamic_bytes_by_fn, entry_storages, seen_fn,
            recursion_analysis, result);
        if (budget == std::numeric_limits<std::uint64_t>::max()) {
            return false;
        }
        // Entry-handler closure envs bump once per step OUTSIDE any fn
        // activation, so they are added on top of the fn activation tree.
        budget += entry_closure_env_bytes_out;
        const std::uint64_t capacity =
            ir::core::kCoreWasmFixedLinearMemoryCapacityBytes;
        if (static_cast<std::uint64_t>(construct_heap_base) > capacity ||
            budget > capacity - static_cast<std::uint64_t>(construct_heap_base)) {
            add_diag(result,
                     core_wasm_diag::kResourceExhausted,
                     "the aggregate / closure-environment plan needs " + std::to_string(budget) +
                         " heap bytes above base " + std::to_string(construct_heap_base) +
                         " but the fixed 64 KiB linear-memory page has only " +
                         std::to_string(capacity - construct_heap_base) +
                         "; use smaller aggregates / captures or bounded collections");
            return false;
        }
    }

    // Phase B: install the finalized funcref / call_indirect tables (with
    // ABSOLUTE type indices beyond the per-fn types), then emit every planned
    // fn body in ordinal order.
    const std::uint32_t closure_type_base = 5u + static_cast<std::uint32_t>(reachable.size());
    std::unordered_map<std::string, std::uint32_t> absolute_closure_type_index;
    absolute_closure_type_index.reserve(closure_signatures_out.size());
    for (std::uint32_t i = 0; i < closure_signatures_out.size(); ++i) {
        absolute_closure_type_index.emplace(
            closure_call_type_key(closure_signatures_out[i]), closure_type_base + i);
    }
    for (PlannedFn &planned : planned_fns) {
        planned.builder->install_closure_tables(fn_to_table_slot, absolute_closure_type_index);
        // FB-4: give each outlined fn builder the module import table so an
        // in-body capability call emits at the correct ahfl_cap ordinal.
        planned.builder->install_import_table(imports);
    }
    for (std::uint32_t ordinal = 0; ordinal < planned_fns.size(); ++ordinal) {
        PlannedFn &planned = planned_fns[ordinal];
        auto body = planned.builder->emit(planned.param_words);
        if (!body.has_value()) {
            return false;
        }
        CompiledFn compiled;
        compiled.id = planned.id;
        compiled.body = std::move(*body);
        compiled.param_words = std::move(planned.param_words);
        compiled.result_word = planned.result_word;
        compiled.callees = std::move(planned.callees);
        out.push_back(std::move(compiled));
    }
    construct_heap_enabled_out = construct_heap_enabled;
    construct_heap_base_out = construct_heap_base;
    return true;
}

[[nodiscard]] std::optional<AgentPlan> build_agent_plan(const CoreProgram &program,
                                                        const ir::core::CoreLayoutTable &layouts,
                                                        CoreAgentId target,
                                                        CoreWasmCodegenResult &result,
                                                        AgentPlanPolicy policy = {}) {
    if (target.value >= program.agents.size()) {
        add_diag(
            result, core_wasm_diag::kEntryNotFound, "explicit Core agent entry is out of range");
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
    // RFC 0026 P6 (KR6.6): the coercion proof arena is executable from P6-6
    // onward (a normalized coercion lowers to a real physical effect per
    // CoreCoercionPlanNode op, or fails closed per-handler in the scalar
    // builder). A non-empty PATTERN arena is admitted past this gate only when
    // some handler is a pure P6 computation region actually containing a match —
    // otherwise canonical E1-E3 shapes that merely carry leftover pattern
    // artifacts keep the exact legacy rejection. The admitted flow still fails
    // closed per-handler below for any pattern kind or expression node outside
    // the landed subset.
    if (!flow->storage.patterns.empty() &&
        !std::any_of(
            flow->states.begin(), flow->states.end(), [](const ir::core::CoreFlowState &state) {
                return region_contains_match(state.body) && is_p6_computation_region(state.body);
            })) {
        const bool contains_capability = std::any_of(
            flow->states.begin(), flow->states.end(), [](const ir::core::CoreFlowState &state) {
                return region_contains_capability(state.body);
            });
        add_diag(result,
                 contains_capability
                     ? (policy.allow_capability ? core_wasm_diag::kUnsupportedCapabilityFrame
                                                : policy.unsupported_code)
                     : policy.unsupported_code,
                 "KR6.5 " + std::string(policy.slice) +
                     " rejects a hidden pattern arena with no matched computation handler");
        return std::nullopt;
    }
    if (agent.states.size() >= static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
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

    // RFC 0026 P6-4 (KR6.6): the input and context frames are FIXED regions of
    // the single 64 KiB page (`core_wasm_abi_constants.hpp`). A projection path
    // emits a load/store at `frame_base + field_offset`, so an agent whose
    // input / context struct does not FIT its region would silently read or
    // write into the neighbouring region (or past the page) with no diagnostic.
    // Fail closed with the same RESOURCE-class rejection the constructor scratch
    // arena uses, BEFORE any handler byte is emitted.
    if (!fits_frame_region(
            program, layouts, agent.input_type, kP6AggregateInputCapacity, "input", result)) {
        return std::nullopt;
    }
    if (agent.context_kind == CoreAgentDecl::ContextKind::Struct &&
        !fits_frame_region(
            program, layouts, agent.context_type, kP6AggregateContextCapacity, "context", result)) {
        return std::nullopt;
    }

    AgentPlan plan;
    plan.agent = target;
    plan.initial = agent.initial;
    plan.actions.resize(agent.states.size(), IdentityAction{});
    std::vector<bool> used_exprs(flow->storage.exprs.size(), false);
    std::vector<bool> used_values(flow->storage.value_count, false);
    // FB-1: a computed handler is PLANNED in the state loop but its body is
    // EMITTED only after the fn reachability fixed point assigned ordinals (a
    // handler can directly call an outlined fn, so its emitted `call` indices
    // depend on that table). Keep one planned builder per computed handler.
    struct PlannedComputedHandler {
        std::uint32_t state{0};
        std::unique_ptr<P6ComputationHandlerBuilder> builder;
        std::vector<CoreStateId> targets;
        bool reads_raw_input_frame{false};
    };
    std::vector<PlannedComputedHandler> planned_handlers;

    // RFC 0026 P6-2: a computed-goto handler compiles to its OWN `() -> i32`
    // function, so its locals are private and every handler can be emitted
    // immediately (no function-wide local-pool base to resolve first).
    for (std::uint32_t state = 0; state < plan.actions.size(); ++state) {
        const auto *handler = handlers[state];
        if (handler == nullptr) {
            add_diag(
                result, core_wasm_diag::kInvalidCore, "target flow is missing a state handler");
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
                auto action = validate_capability_final(
                    program, layouts, agent, *flow, *handler, used_exprs, used_values, result);
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
        // builder and compiles to its own function (P6-2: Literal/ValueRef/
        // Unary/Binary + structured if + goto/trap). All effect-bearing or
        // otherwise out-of-subset regions keep the legacy rejections below.
        const bool single_goto =
            statements.size() == 1 && std::holds_alternative<CoreGotoStmt>(statements.front().node);
        if (!single_goto) {
            if (!handler->body.statements.empty() && is_p6_computation_region(handler->body)) {
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
                // Targets are recorded during planning (match-arm gotos), so
                // validate the successor set now; the body itself is emitted
                // after the fn fixed point (a handler may directly call an fn).
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
                // Read the target/raw-frame facts BEFORE moving the builder
                // (an aggregate field move must not be followed by a read of
                // the moved-from unique_ptr).
                const std::vector<CoreStateId> handler_targets = builder->targets();
                const bool reads_raw = builder->reads_raw_input_frame();
                planned_handlers.push_back(PlannedComputedHandler{
                    state, std::move(builder), handler_targets, reads_raw});
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
                     statements.empty() ? ir::SourceRangeOpt{} : statements.front().source_range);
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

    if (std::any_of(used_exprs.begin(), used_exprs.end(), [](bool used) { return !used; }) ||
        std::any_of(used_values.begin(), used_values.end(), [](bool used) { return !used; })) {
        const bool contains_capability =
            std::any_of(plan.actions.begin(), plan.actions.end(), [](const StateAction &action) {
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
    plan.imports.erase(std::unique(plan.imports.begin(), plan.imports.end()), plan.imports.end());
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

    // RFC 0026 FB-1 §6.1: compile the outlined pure fn bodies reachable from
    // the entry handler regions via direct CoreCallExpr. The reachability set
    // is a deterministic fixed point over the static fn call graph (recursion
    // groups are SCCs sealed by analyze_fn_recursion); ordinals are assigned in
    // CoreFnId order so the module is reproducible. A zero-call module emits no
    // fn bodies. The handler count is fixed by the planned computed handlers,
    // so the fn base is known now.
    plan.handlers.resize(planned_handlers.size());
    std::vector<std::vector<CoreInstanceId>> planned_entry_callees;
    planned_entry_callees.reserve(planned_handlers.size());
    for (const PlannedComputedHandler &planned : planned_handlers) {
        planned_entry_callees.push_back(planned.builder->fn_callees());
    }

    // RFC 0026 FB-4 (design §5.3): the capability imports an effectful outlined
    // fn reaches transitively must be planned BEFORE the function base is
    // fixed, so they occupy the same low import ordinals the handler
    // capability statements use (import ordinal == position in the sorted
    // table). Merge them with the capability-final imports.
    {
        const std::vector<CoreFnId> reachable_fn_ids = compute_reachable_fn_ids(
            program,
            std::vector<const CoreBodyStorage *>{&flow->storage},
            planned_entry_callees,
            flow);
        for (const CoreCapabilityId cap : capabilities_of_reachable_fns(program, reachable_fn_ids)) {
            plan.imports.push_back(cap);
        }
        std::sort(plan.imports.begin(), plan.imports.end(),
                  [](auto lhs, auto rhs) { return lhs.value < rhs.value; });
        plan.imports.erase(std::unique(plan.imports.begin(), plan.imports.end()),
                           plan.imports.end());
        if (plan.imports.size() >
            static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
            add_diag(result,
                     core_wasm_diag::kInvalidCapabilityAbi,
                     "reachable capability import table (handler + effectful-fn) exceeds the "
                     "wasm32 index domain");
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
    }

    const std::uint32_t agent_fn_base =
        static_cast<std::uint32_t>(plan.imports.size()) + kDefinedHandlerBase +
        static_cast<std::uint32_t>(planned_handlers.size());
    // Keep raw pointers to the planned entry-handler builders so the fn
    // reachability / closure-table driver can read their closure facts and
    // install the finalized tables before the handlers are emitted below. The
    // unique_ptrs stay alive in `planned_handlers` for this whole window.
    std::vector<P6ComputationHandlerBuilder *> entry_builder_ptrs;
    entry_builder_ptrs.reserve(planned_handlers.size());
    for (const PlannedComputedHandler &planned : planned_handlers) {
        entry_builder_ptrs.push_back(planned.builder.get());
    }
    std::uint32_t entry_closure_env_bytes = 0;
    if (!compile_reachable_fn_bodies(program,
                                     layouts,
                                     plan.fns,
                                     std::vector<const CoreBodyStorage *>{&flow->storage},
                                     planned_entry_callees,
                                     entry_builder_ptrs,
                                     agent_fn_base,
                                     result,
                                     plan.construct_heap_enabled,
                                     plan.construct_heap_base,
                                     plan.closure_table,
                                     plan.closure_signatures,
                                     entry_closure_env_bytes,
                                     &plan.imports)) {
        return std::nullopt;
    }

    // Build the callee-instance -> ordinal table the HANDLER emitters use (a
    // handler can directly call any reachable fn).
    std::vector<std::uint32_t> instance_to_ordinal(
        program.instances.size(), std::numeric_limits<std::uint32_t>::max());
    for (std::uint32_t ordinal = 0; ordinal < plan.fns.size(); ++ordinal) {
        instance_to_ordinal[program.fns[plan.fns[ordinal].id.value].instance.value] = ordinal;
    }
    const std::uint32_t handler_fn_base = agent_fn_base;

    // FB-3b: absolute call_indirect type indices sit beyond the five fixed ABI
    // types and the per-fn types (5 + fns.size()). Install the finalized
    // funcref-slot and call_indirect type maps on each entry handler.
    const std::uint32_t agent_closure_type_base =
        5u + static_cast<std::uint32_t>(plan.fns.size());
    std::unordered_map<std::uint32_t, std::uint32_t> fn_to_table_slot;
    for (std::uint32_t slot = 0; slot < plan.closure_table.size(); ++slot) {
        fn_to_table_slot.emplace(plan.closure_table[slot].value, slot);
    }
    std::unordered_map<std::string, std::uint32_t> entry_closure_type_index;
    for (std::uint32_t i = 0; i < plan.closure_signatures.size(); ++i) {
        entry_closure_type_index.emplace(
            closure_call_type_key(plan.closure_signatures[i]), agent_closure_type_base + i);
    }

    // Now that ordinals are fixed, emit the planned computed handlers in
    // state-loop order and publish their actions (the function index is the
    // handler's position in plan.handlers). A builder owns state via a
    // unique_ptr whose stored object is stable, so releasing the pointers into
    // the vector explicitly (no reallocation here) is safe; move the unique_ptr
    // objects rather than copying them.
    for (std::uint32_t index = 0; index < planned_handlers.size(); ++index) {
        PlannedComputedHandler &planned = planned_handlers[index];
        const std::uint32_t state = planned.state;
        std::vector<CoreStateId> targets = planned.targets;
        std::unique_ptr<P6ComputationHandlerBuilder> builder = std::move(planned.builder);
        builder->set_fn_call_tables(&instance_to_ordinal, &handler_fn_base);
        builder->install_closure_tables(fn_to_table_slot, entry_closure_type_index);
        if (plan.construct_heap_enabled) {
            builder->reset_construct_heap_on_entry(plan.construct_heap_base);
        }
        auto body = builder->emit({});
        if (!body.has_value()) {
            return std::nullopt;
        }
        // reads_raw_input_frame_ is latched during EMIT (an input-frame
        // projection emits a fixed-region load), so read it after emit().
        const bool reads_raw = builder->reads_raw_input_frame();
        plan.handlers[index] = CompiledHandler{std::move(*body)};
        plan.actions[state] = ComputedGotoAction{index, std::move(targets)};
        if (reads_raw) {
            plan.reads_raw_input_frame = true;
        }
    }
    return plan;
}

[[nodiscard]] const ir::core::CoreInstanceDecl *agent_instance(const CoreProgram &program,
                                                               CoreInstanceId id) {
    if (id.value >= program.instances.size()) {
        return nullptr;
    }
    return std::get_if<CoreAgentInstance>(&program.instances[id.value].payload) != nullptr
               ? &program.instances[id.value]
               : nullptr;
}

[[nodiscard]] const CoreAgentInstance *agent_instance_payload(const CoreProgram &program,
                                                              CoreInstanceId id) {
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
        let->expr.value >= workflow.storage.exprs.size() ||
        let->result.value >= workflow.storage.value_types.size() ||
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

    const auto &expr = workflow.storage.exprs[let->expr.value];
    const auto *path = std::get_if<CorePathExpr>(&expr.node);
    if (path == nullptr || !path->members.empty() || !path->projection.empty() ||
        !path->projection_resolved || path->has_local ||
        expr.result_type.value >= program.value_types.size() ||
        workflow.storage.value_types[let->result.value] != expr.result_type ||
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
        const auto *nominal =
            std::get_if<CoreVtNominal>(&program.value_types[expr.result_type.value].node);
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
        const auto *source_payload = agent_instance_payload(program, source_node.target_instance);
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

[[nodiscard]] std::optional<std::uint32_t> workflow_runner_index(const WorkflowPlan &plan,
                                                                 CoreInstanceId instance);

[[nodiscard]] std::optional<WorkflowPlan>
build_workflow_plan(const CoreProgram &program,
                    const ir::core::CoreLayoutTable &layouts,
                    CoreWorkflowId target,
                    CoreWasmCodegenResult &result) {
    if (target.value >= program.workflows.size()) {
        add_diag(
            result, core_wasm_diag::kEntryNotFound, "explicit Core workflow entry is out of range");
        return std::nullopt;
    }
    const auto &workflow = program.workflows[target.value];
    if (!workflow.storage.patterns.empty() || !workflow.storage.coercion_plans.empty()) {
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

    std::vector<bool> used_exprs(workflow.storage.exprs.size(), false);
    std::vector<bool> used_values(workflow.storage.value_count, false);
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
    plan.imports.erase(std::unique(plan.imports.begin(), plan.imports.end()), plan.imports.end());
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
        add_diag(
            result, core_wasm_diag::kUnsupportedWorkflowFrame, "workflow return region is absent");
        return std::nullopt;
    }
    const auto *return_let =
        std::get_if<CoreLetStmt>(&workflow.return_region->statements.front().node);
    if (return_let == nullptr || return_let->expr.value >= workflow.storage.exprs.size()) {
        add_diag(result,
                 core_wasm_diag::kUnsupportedWorkflowFrame,
                 "workflow return is not a canonical path let");
        return std::nullopt;
    }
    const auto output_type = workflow.storage.exprs[return_let->expr.value].result_type;
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
[[nodiscard]] bool
append_section(ByteBuffer &module, std::uint8_t section_id, const ByteBuffer &payload) {
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
// FB-1: vector-word overload for the heterogeneous fn functypes (an i64
// parameter makes the parameter word sequence non-constant).
void append_func_type(ByteBuffer &section,
                      const std::vector<std::uint8_t> &params,
                      std::initializer_list<std::uint8_t> results) {
    section.byte(kFuncType);
    section.u32(static_cast<std::uint32_t>(params.size()));
    section.raw_span(params);
    section.u32(static_cast<std::uint32_t>(results.size()));
    section.raw(results);
}
void append_global(ByteBuffer &section, bool is_mutable, std::uint32_t initial) {
    section.byte(kI32);
    section.byte(is_mutable ? 1 : 0);
    append_const(section, initial);
    section.byte(kOpEnd);
}
[[nodiscard]] bool
append_export(ByteBuffer &section, std::string_view name, std::uint8_t kind, std::uint32_t index) {
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
[[nodiscard]] ByteBuffer make_step_body(const AgentPlan &plan, const FunctionTable &functions) {
    ByteBuffer body;
    // The dispatch ladder is stateless: every handler is a real function now
    // (RFC 0026 P6-2), so step() declares no locals of its own.
    body.u32(0);
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
            // The compiled handler function owns the whole transition: it
            // latches current_state, bumps transition_count exactly once on the
            // path it takes, and yields the new state id. step() only calls it.
            append_indexed_op(body, kOpCall, functions.handler(computed->function));
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

[[nodiscard]] ByteBuffer make_run_body(const AgentPlan &plan, const FunctionTable &functions) {
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

[[nodiscard]] std::optional<std::uint32_t> import_function_index(const AgentPlan &plan,
                                                                 CoreCapabilityId capability) {
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

[[nodiscard]] ByteBuffer make_run2_body(const AgentPlan &plan, const FunctionTable &functions) {
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
    const FunctionTable functions{static_cast<std::uint32_t>(plan.imports.size()),
                                  static_cast<std::uint32_t>(plan.handlers.size())};
    ByteBuffer module;
    module.raw({0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00});

    ByteBuffer types;
    types.u32(5 + static_cast<std::uint32_t>(plan.fns.size()) +
                  static_cast<std::uint32_t>(plan.closure_signatures.size()));
    append_func_type(types, {}, {kI32});
    append_func_type(types, {kI32}, {kI32});
    append_func_type(types, {kI32, kI32}, {});
    append_func_type(types, {kI32, kI32}, {kI32});
    append_func_type(types, {kI32, kI32}, {kI32, kI32, kI32});
    // RFC 0026 FB-1: one functype per outlined fn, in ordinal order, appended
    // beyond the five fixed types. Wasm permits structurally duplicate
    // functypes, so each fn gets its own type index (5 + ordinal) even when
    // signatures coincide. Every signature is (i32 env, args...) -> ret; a
    // closure ARGUMENT is two i32 words (func_index, env_ptr), an i64 argument
    // is one i64 word.
    for (const CompiledFn &fn : plan.fns) {
        std::vector<std::uint8_t> params;
        params.reserve(fn.param_words.size() + 1);
        params.push_back(kI32); // env
        for (const P6ScalarKind word : fn.param_words) {
            if (word == P6ScalarKind::Closure) {
                params.push_back(kI32);
                params.push_back(kI32);
            } else {
                params.push_back(word == P6ScalarKind::IntI64 ? kI64 : kI32);
            }
        }
        const std::uint8_t result_word =
            fn.result_word == P6ScalarKind::IntI64 ? kI64 : kI32;
        append_func_type(types, params, {result_word});
    }
    // RFC 0026 FB-3b: the call_indirect expected functypes follow the per-fn
    // types at index (5 + fns.size() + i). Their bytes are already complete
    // (leading env + closure-arg expansion + single-word result).
    for (const ClosureCallType &signature : plan.closure_signatures) {
        types.byte(kFuncType);
        types.u32(static_cast<std::uint32_t>(signature.params.size()));
        types.raw_span(signature.params);
        types.u32(1);
        types.byte(signature.result);
    }
    if (!append_section(module, kSectionType, types)) {
        return std::nullopt;
    }

    if (!plan.imports.empty()) {
        ByteBuffer imports;
        imports.u32(static_cast<std::uint32_t>(plan.imports.size()));
        for (const auto id : plan.imports) {
            const auto symbol = *program.capabilities[id.value].symbol_ref.id;
            if (!imports.name("ahfl_cap") || !imports.name("cap_" + std::to_string(symbol))) {
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
    functions_section.u32(functions.defined_count() +
                          static_cast<std::uint32_t>(plan.fns.size()));
    functions_section.u32(kTypeI32ToI32);
    functions_section.u32(kTypeTwoI32ToVoid);
    functions_section.u32(kTypeNoArgsI32);
    functions_section.u32(kTypeNoArgsI32);
    functions_section.u32(kTypeNoArgsI32);
    functions_section.u32(kTypeTwoI32ToI32);
    functions_section.u32(kTypeCapabilityTuple);
    // RFC 0026 P6-2: each compiled handler is a `() -> i32` function (same type
    // as alloc/current_state/is_final). Absent handlers contribute no entry, so
    // a pure E1-E3 agent keeps its canonical 7-function section bytes.
    for (std::uint32_t index = 0; index < plan.handlers.size(); ++index) {
        functions_section.u32(kTypeNoArgsI32);
    }
    // RFC 0026 FB-1: outlined fn bodies follow the handlers, each with its own
    // functype (5 + ordinal).
    for (std::uint32_t index = 0; index < plan.fns.size(); ++index) {
        functions_section.u32(5u + index);
    }
    if (!append_section(module, kSectionFunction, functions_section)) {
        return std::nullopt;
    }

    // RFC 0026 FB-3b §6.2: exactly ONE funcref table, declared ONLY when the
    // module constructs a closure. Table(4) is emitted in its fixed position
    // between Function(3) and Memory(5); a closure-free module omits the whole
    // section (and Element(9)) and stays byte-identical.
    if (!plan.closure_table.empty()) {
        ByteBuffer tables;
        tables.u32(1);                 // one table
        tables.byte(kFuncRefType);     // element type funcref
        tables.byte(0);                // limits: minimum only, no maximum
        tables.u32(static_cast<std::uint32_t>(plan.closure_table.size()));
        if (!append_section(module, kSectionTable, tables)) {
            return std::nullopt;
        }
    }

    ByteBuffer memories;
    memories.u32(1);  // one memory
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
    // The agent lane's bump heap starts at the identity-workflow node-event log
    // base. When the module uses per-activation outlined-fn aggregate constructs
    // (FB-1 fix-forward) it is RELOCATED above every reserved frame and the
    // bounded-collection backing high-water, and each computed handler resets it
    // on entry; the legacy host alloc()/run() path simply begins at that higher
    // base.
    append_global(globals, true,
                  plan.construct_heap_enabled ? plan.construct_heap_base
                                              : kNodeEventLogBase);
    append_global(globals, true, 0);
    if (!append_section(module, kSectionGlobal, globals)) {
        return std::nullopt;
    }

    ByteBuffer exports;
    exports.u32(9);
    const bool exports_ok =
        append_export(exports, "memory", kExportMemory, 0) &&
        append_export(exports, "alloc", kExportFunction, functions.alloc()) &&
        append_export(exports, "dealloc", kExportFunction, functions.dealloc()) &&
        append_export(exports, "run", kExportFunction, functions.run()) &&
        append_export(exports, "run2", kExportFunction, functions.run2()) &&
        append_export(exports, "step", kExportFunction, functions.step()) &&
        append_export(exports, "current_state", kExportFunction, functions.current_state()) &&
        append_export(exports, "transition_count", kExportGlobal, kGlobalTransitionCount) &&
        append_export(exports, "ahfl_abi_version", kExportGlobal, kGlobalAbiVersion);
    if (!exports_ok || !append_section(module, kSectionExport, exports)) {
        return std::nullopt;
    }

    // RFC 0026 FB-3b §6.2: Element(9) in its fixed position between Export(7)
    // and Code(10). One ACTIVE segment initializes table[0..N) with the wasm
    // function index of each closure target in DENSE table-slot order. The
    // dense slot is what a constructed closure's func_index word holds; this
    // segment maps slot -> absolute funcidx. A closure-free module omits it.
    if (!plan.closure_table.empty()) {
        std::vector<std::uint32_t> fn_ordinal_by_id(program.fns.size(),
                                                    std::numeric_limits<std::uint32_t>::max());
        for (std::uint32_t ordinal = 0; ordinal < plan.fns.size(); ++ordinal) {
            fn_ordinal_by_id[plan.fns[ordinal].id.value] = ordinal;
        }
        ByteBuffer elements;
        elements.u32(1);                 // one segment
        elements.byte(0);               // active, implicit table 0, offset expr
        elements.byte(kOpI32Const);
        elements.s32(0);                // offset = 0
        elements.byte(kOpEnd);
        elements.u32(static_cast<std::uint32_t>(plan.closure_table.size()));
        for (const CoreFnId fn_id : plan.closure_table) {
            const std::uint32_t ordinal = fn_ordinal_by_id[fn_id.value];
            if (ordinal == std::numeric_limits<std::uint32_t>::max()) {
                return std::nullopt;
            }
            elements.u32(functions.fn_base(functions.handler_count) + ordinal);
        }
        if (!append_section(module, kSectionElement, elements)) {
            return std::nullopt;
        }
    }

    ByteBuffer code;
    code.u32(functions.defined_count() +
             static_cast<std::uint32_t>(plan.fns.size()));
    const auto alloc = make_alloc_body();
    const auto dealloc = make_dealloc_body();
    const auto current = make_current_state_body();
    const auto final = make_is_final_body(plan);
    const auto step = make_step_body(plan, functions);
    const auto run = make_run_body(plan, functions);
    const auto run2 = make_run2_body(plan, functions);
    if (!code.sized(alloc) || !code.sized(dealloc) || !code.sized(current) || !code.sized(final) ||
        !code.sized(step) || !code.sized(run) || !code.sized(run2)) {
        return std::nullopt;
    }
    // RFC 0026 P6-2: the compiled handler function bodies follow run2, in the
    // same order their indices were assigned (ascending function index).
    for (const auto &handler : plan.handlers) {
        if (!code.sized(handler.body)) {
            return std::nullopt;
        }
    }
    // RFC 0026 FB-1: the outlined fn bodies follow the handlers in ordinal
    // order (the function-index order the type/function sections declared).
    for (const auto &fn : plan.fns) {
        if (!code.sized(fn.body)) {
            return std::nullopt;
        }
    }
    if (!append_section(module, kSectionCode, code)) {
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
[[nodiscard]] std::optional<EventLayout> compute_event_layout(std::size_t node_count,
                                                              bool &overflow_is_binary) {
    overflow_is_binary = false;
    // Phase 1: checked wasm32 arithmetic.
    if (node_count > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        overflow_is_binary = true;
        return std::nullopt;
    }
    const auto n = static_cast<std::uint32_t>(node_count);
    if (n != 0 && n > (std::numeric_limits<std::uint32_t>::max() - kNodeEventHeaderBytes) /
                          kNodeEventRecordBytes) {
        overflow_is_binary = true;
        return std::nullopt;
    }
    const std::uint32_t event_bytes = kNodeEventHeaderBytes + n * kNodeEventRecordBytes;
    if (event_bytes > std::numeric_limits<std::uint32_t>::max() - kNodeEventLogBase) {
        overflow_is_binary = true;
        return std::nullopt;
    }
    std::uint32_t unaligned = kNodeEventLogBase + event_bytes;
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

[[nodiscard]] std::optional<std::uint32_t> workflow_runner_index(const WorkflowPlan &plan,
                                                                 CoreInstanceId instance) {
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
    const auto it =
        std::lower_bound(imports.begin(), imports.end(), capability, [](auto lhs, auto rhs) {
            return lhs.value < rhs.value;
        });
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

[[nodiscard]] std::optional<std::uint32_t> workflow_node_ptr_local(CoreWorkflowNodeId node) {
    if (node.value > (std::numeric_limits<std::uint32_t>::max() - 2u) / 2u) {
        return std::nullopt;
    }
    return 2u + node.value * 2u;
}

[[nodiscard]] std::optional<std::uint32_t> workflow_node_len_local(CoreWorkflowNodeId node) {
    const auto ptr = workflow_node_ptr_local(node);
    if (!ptr.has_value() || *ptr == std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    return *ptr + 1u;
}

[[nodiscard]] bool append_workflow_source(ByteBuffer &body, const WorkflowFrameSource &source) {
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
    const std::uint32_t record_addr =
        kNodeEventRecordsBase + node.schedule_pos * kNodeEventRecordBytes;
    const std::uint8_t tag = node.has_capability ? kEventTagCapability : kEventTagIdentity;
    // [0..3]: tag u8 in byte 0, pad[1..3] == 0 (one aligned 4-byte store).
    append_i32_store_const(body, record_addr + 0u, static_cast<std::uint32_t>(tag));
    // [4..7]: workflow_node_id.
    append_i32_store_const(body, record_addr + 4u, node.node.value);
    // [8..11]: schedule_pos.
    append_i32_store_const(body, record_addr + 8u, node.schedule_pos);
    // [12..15]: capability (0 for identity).
    append_i32_store_const(
        body, record_addr + 12u, node.has_capability ? node.capability.value : 0u);
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
        append_i32_store_const(body, kNodeEventLogBase + 0u, 0u);
        append_i32_store_const(body, kNodeEventLogBase + 4u, 0u);
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
        append_const(body, kNodeEventLogBase + 0u);
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
        append_const(body, kNodeEventLogBase + 0u);
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
make_workflow_run2_body(const WorkflowPlan &plan, const WorkflowFunctionTable &functions) {
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
        plan.nodes.size() >= static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
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
        const auto runner = workflow_runner_index(plan, plan.nodes[node_id.value].target_instance);
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
    std::uint32_t heap_base = kNodeEventLogBase;
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
            if (!imports.name("ahfl_cap") || !imports.name("cap_" + std::to_string(*symbol.id))) {
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
    // RFC 0026 FB-1: defined_count() is runner_count+6+fn_count. A workflow
    // module carries zero outlined fns in FB-1 (fn_count==0), so this is
    // byte-identical to the pre-FB-1 runner_count+6 shape.
    functions_section.u32(functions.defined_count());
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
    memories.u32(1);  // one memory
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
        append_export(exports, "current_state", kExportFunction, functions.current_state()) &&
        append_export(exports, "transition_count", kExportGlobal, kWorkflowGlobalTransitionCount) &&
        append_export(exports, "ahfl_abi_version", kExportGlobal, kWorkflowGlobalAbiVersion) &&
        append_export(exports, "workflow_node_count", kExportGlobal, kWorkflowGlobalNodeCount) &&
        append_export(
            exports, "workflow_completed_count", kExportGlobal, kWorkflowGlobalCompletedCount);
    if (!exports_ok || !append_section(module, kSectionExport, exports)) {
        return std::nullopt;
    }

    ByteBuffer code;
    code.u32(functions.defined_count());
    // A capability workflow uses the checked-alloc body (returns 0 on capacity
    // exhaustion, never advancing heap_next); identity workflows keep the shared
    // unchecked bump body byte-for-byte.
    const auto alloc = capability_workflow ? make_checked_alloc_body(kWorkflowGlobalHeapNext)
                                           : make_alloc_body(kWorkflowGlobalHeapNext);
    const auto dealloc = make_dealloc_body();
    const auto current = make_trapping_i32_body();
    const auto step = make_trapping_i32_body();
    if (!code.sized(alloc) || !code.sized(dealloc) || !code.sized(current) || !code.sized(step)) {
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

namespace {

// KR6.7 (RFC 0026 P7): descriptor builders. Each is derived ONLY from the
// already-built internal plan (the identical plan the module bytes were
// emitted from), so the descriptor can never disagree with the artifact.

[[nodiscard]] std::vector<CoreWasmCapabilityImport>
build_import_descriptors(const CoreProgram &program,
                         const std::vector<CoreCapabilityId> &plan_imports) {
    std::vector<CoreWasmCapabilityImport> imports;
    imports.reserve(plan_imports.size());
    for (std::uint32_t ordinal = 0; ordinal < plan_imports.size(); ++ordinal) {
        const auto id = plan_imports[ordinal];
        const auto &symbol = program.capabilities[id.value].symbol_ref;
        CoreWasmCapabilityImport import_descriptor;
        import_descriptor.ordinal = ordinal;
        if (symbol.id.has_value()) {
            import_descriptor.field = "cap_" + std::to_string(*symbol.id);
        }
        import_descriptor.canonical_name = symbol.canonical_name;
        imports.push_back(std::move(import_descriptor));
    }
    return imports;
}

// The ordered state names one agent runner enters on a single invocation: the
// initial state, then every deterministic goto target, ending at the terminal.
// This is exactly the state walk `make_workflow_runner_body` emits, derived from
// the same AgentPlan rather than re-walking the graph.
[[nodiscard]] std::vector<std::string> runner_walk_names(const CoreProgram &program,
                                                         const AgentPlan &agent_plan) {
    std::vector<std::string> walk;
    const auto &states = program.agents[agent_plan.agent.value].states;
    auto state = agent_plan.initial;
    std::vector<bool> visited(agent_plan.actions.size(), false);
    while (true) {
        if (state.value >= agent_plan.actions.size() || visited[state.value]) {
            break; // workflow_initial_transitions already proved this terminates
        }
        visited[state.value] = true;
        if (state.value < states.size()) {
            walk.push_back(states[state.value]);
        }
        const auto *go = std::get_if<GotoAction>(&agent_plan.actions[state.value]);
        if (go == nullptr) {
            break; // terminal (identity / capability)
        }
        state = go->target;
    }
    return walk;
}

[[nodiscard]] CoreWasmExecutionDescriptor build_agent_descriptor(const CoreProgram &program,
                                                                 const AgentPlan &plan) {
    CoreWasmExecutionDescriptor descriptor;
    descriptor.is_workflow = false;
    descriptor.frame_contract = plan.reads_raw_input_frame ? CoreWasmFrameContract::RawP6Frame
                                                           : CoreWasmFrameContract::WireJson;
    descriptor.agent_name = program.agents[plan.agent.value].symbol_ref.canonical_name;
    descriptor.states = program.agents[plan.agent.value].states;
    descriptor.initial_state = plan.initial.value;
    descriptor.imports = build_import_descriptors(program, plan.imports);
    descriptor.event_log_base = ir::core::kNodeEventLogBase;
    descriptor.event_header_bytes = ir::core::kNodeEventHeaderBytes;
    descriptor.event_record_bytes = ir::core::kNodeEventRecordBytes;
    descriptor.event_records_base = ir::core::kNodeEventRecordsBase;
    descriptor.workflow_node_count = 0;
    descriptor.heap_base = ir::core::kNodeEventLogBase;
    return descriptor;
}

[[nodiscard]] CoreWasmExecutionDescriptor build_workflow_descriptor(const CoreProgram &program,
                                                                    const WorkflowPlan &plan) {
    CoreWasmExecutionDescriptor descriptor;
    descriptor.is_workflow = true;
    // A workflow node runner never carries a P6 computed handler
    // (build_workflow_plan sets allow_computed_goto=false), so the scheduler
    // forwards opaque canonical frames end to end; the output boundary stays
    // wire-JSON.
    descriptor.frame_contract = CoreWasmFrameContract::WireJson;
    descriptor.imports = build_import_descriptors(program, plan.imports);
    descriptor.event_log_base = ir::core::kNodeEventLogBase;
    descriptor.event_header_bytes = ir::core::kNodeEventHeaderBytes;
    descriptor.event_record_bytes = ir::core::kNodeEventRecordBytes;
    descriptor.event_records_base = ir::core::kNodeEventRecordsBase;
    descriptor.workflow_node_count = static_cast<std::uint32_t>(plan.nodes.size());

    // The bump heap starts above the node-event region only for a capability
    // workflow (the same two-phase layout encode_workflow_module performs); an
    // identity workflow's heap starts at the log base.
    descriptor.heap_base = ir::core::kNodeEventLogBase;
    if (!plan.imports.empty()) {
        bool overflow_is_binary = false;
        if (auto layout = compute_event_layout(plan.nodes.size(), overflow_is_binary);
            layout.has_value()) {
            descriptor.heap_base = layout->heap_base;
        }
    }

    // Runner table: one entry per sorted-unique packaged agent instance.
    descriptor.agents.reserve(plan.packaged_instances.size());
    for (std::uint32_t runner = 0; runner < plan.packaged_instances.size(); ++runner) {
        const auto &agent_plan = plan.agent_plans[runner];
        CoreWasmStateWalk walk;
        walk.agent = program.agents[agent_plan.agent.value].symbol_ref.canonical_name;
        walk.walk = runner_walk_names(program, agent_plan);
        descriptor.agents.push_back(std::move(walk));
    }

    // Node schedule in Kahn execution order.
    descriptor.nodes.reserve(plan.schedule.size());
    for (std::uint32_t position = 0; position < plan.schedule.size(); ++position) {
        const auto node_id = plan.schedule[position];
        const auto &node = plan.nodes[node_id.value];
        CoreWasmNodeDescriptor node_descriptor;
        node_descriptor.node_id = node.node.value;
        node_descriptor.schedule_pos = node.schedule_pos;
        node_descriptor.runner = workflow_runner_index(plan, node.target_instance).value_or(0);
        node_descriptor.has_capability = node.has_capability;
        if (node.has_capability) {
            node_descriptor.capability_ordinal =
                workflow_import_function_index(plan.imports, node.capability).value_or(0);
            node_descriptor.source_symbol = node.source_symbol;
        }
        descriptor.nodes.push_back(std::move(node_descriptor));
    }
    return descriptor;
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
                 "P4-D verifier rejected the layout table (" + first.code + "): " + first.message,
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
                std::string message = "reachable capability import ABI is not wire-transportable";
                ir::SourceRangeOpt range;
                if (!projection.diagnostics.empty()) {
                    const auto &first = projection.diagnostics.front();
                    message += " (" + first.code + ")";
                    range = first.source_range;
                }
                add_diag(result, core_wasm_diag::kInvalidCapabilityAbi, std::move(message), range);
                return result;
            }
            auto encoded = ir::core::encode_core_wire_schema_table(*projection.table);
            if (!encoded.ok()) {
                std::string message = "wire-schema section payload exceeds the encoding domain";
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
        result.descriptor = build_workflow_descriptor(program, *plan);
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
            std::string message = "reachable capability import ABI is not wire-transportable";
            ir::SourceRangeOpt range;
            if (!projection.diagnostics.empty()) {
                const auto &first = projection.diagnostics.front();
                message += " (" + first.code + ")";
                range = first.source_range;
            }
            add_diag(result, core_wasm_diag::kInvalidCapabilityAbi, std::move(message), range);
            return result;
        }
        auto encoded = ir::core::encode_core_wire_schema_table(*projection.table);
        if (!encoded.ok()) {
            std::string message = "wire-schema section payload exceeds the encoding domain";
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
        artifact.imports.push_back("ahfl_cap.cap_" +
                                   std::to_string(*program.capabilities[id.value].symbol_ref.id));
    }
    result.descriptor = build_agent_descriptor(program, *plan);
    result.artifact = std::move(artifact);
    return result;
}

} // namespace ahfl::backends
