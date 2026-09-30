#include "compiler/backends/wasm/core_wasm_codegen.hpp"

#include "ahfl/base/support/const_literal.hpp"
#include "ahfl/base/support/overloaded.hpp"
#include "ahfl/base/support/string_literal.hpp"
#include "ahfl/compiler/ir/core_frame_layout.hpp"
#include "ahfl/compiler/ir/core_recursion.hpp"
#include "ahfl/compiler/ir/core_verify.hpp"
#include "ahfl/compiler/ir/core_wasm_abi_constants.hpp"
#include "ahfl/compiler/ir/core_wire_schema.hpp"
#include "ahfl/runtime/ahfl_host.h"
#include "compiler/backends/wasm/detail/wasm_byte_buffer.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
using ir::core::CoreVtDecimal;
using ir::core::CoreVtDuration;
using ir::core::CoreVtInt;
using ir::core::CoreVtNominal;
using ir::core::CoreVtString;
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
using ir::core::kP6AggregateOutputBase;
using ir::core::kP6AggregateOutputCapacity;
using ir::core::kP6AggregateScratchBase;
using ir::core::kP6AggregateScratchCapacity;
using ir::core::kP6CollectionBackingCapacity;
using ir::core::kP6FrameStringPoolBytes;
using ir::core::kP6RodataBase;
using ir::core::kP6RodataCapacity;

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
// RFC 0026 P6-7 frame-bridge v2 D1 (rung V2-B): the ONE active rodata segment.
constexpr std::uint8_t kSectionData = 11;
// The funcref reftype encoding (wasm spec reftype space).
constexpr std::uint8_t kFuncRefType = 0x70;

// RFC 0026 E4-B1 wire-schema transport (seam doc §3.1): the deterministic
// logical wire schema for reachable capability imports rides in a single Wasm
// custom section, keyed by this canonical name, at the module's EOF. E1 no-import
// agents and E3 no-capability identity workflows never carry it; E2 agents and
// B2-C capability workflows carry the EOF AHFLWS section.
constexpr std::string_view kWireSchemaSectionName = "ahfl.wire-schema.v1";

// RFC 0026 P6-7 rung A: a P6-frame module (one whose handler projects the raw
// P4-D input frame) carries, immediately BEFORE the EOF wire-schema section, the
// deterministic P4-D layout table plus the boundary roots and the disjoint
// backing placements / payload-arena span. The host physical-layout authority.
constexpr std::string_view kCoreLayoutSectionName = "ahfl.core-layout.v1";

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
// RFC 0026 KR6.8 WH-4 fix-forward D-C: the agent entry kind. A capability
// AGENT module carries a flat capability-list manifest (no workflow schedule /
// nodes), mirroring the A2 decoder's agent arm.
constexpr std::uint8_t kExecManifestEntryKindAgent = 1;

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
// RFC 0026 P6-7 frame-bridge v2 D3 (rung V2-C): the additive capability
// BRIDGE functype `(block_ptr:i32) -> (status:i32, result_root_ptr:i32)`. It is
// appended at the very end of the agent module's type table (after every fixed
// type, per-fn type, closure type, and the runv type), so no existing index
// moves on a non-bridge module. The host derives its index from the descriptor;
// an old host that meets the unknown functype fails closed at instantiation.

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
// RFC 0026 P6-7 frame-bridge v2 rung V2-A: a COMPUTED FINAL. The state's
// handler is a real `() -> (i32,i32)` wasm function (status, value_ptr) in the
// P6 subset; it materializes its result into the fixed output frame
// (kP6AggregateOutputBase) and runv returns (AHFL_CAP_OK, output_base). Like
// ComputedGotoAction, `function` is the handler ordinal in the module's
// compiled-handler table (FunctionTable::handler).
struct ComputedReturnAction {
    std::uint32_t function{0};
    [[nodiscard]] friend bool operator==(ComputedReturnAction,
                                         ComputedReturnAction) noexcept = default;
};
using StateAction =
    std::variant<GotoAction, ComputedGotoAction, IdentityAction, CapabilityAction,
                 ComputedReturnAction>;

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

// RFC 0026 P6-7 frame-bridge v2 D1 (rung V2-B): the module-wide String literal
// pool. Every reachable String literal's DECODED UTF-8 bytes are interned here
// during planning (Principle 3: hash-cons by byte content), then frozen once
// into the deterministic rodata image: unique byte sequences sorted by their
// UNSIGNED byte order, concatenated with 8-alignment zero padding. A
// constructed String is the PtrLen immediate pair
// (kP6RodataBase + frozen_offset, byte_length), and the frozen image is the ONE
// active Data(11) segment's payload. Determinism comes from the byte content
// itself, never the source appearance or interner allocation order.
class RodataLiteralPool {
  public:
    struct Entry {
        std::string bytes;
        std::uint32_t offset{0};
    };

    // Comparator on UNSIGNED bytes so the ordering is platform-independent.
    struct ByteLess {
        using is_transparent = void;
        [[nodiscard]] bool operator()(std::string_view lhs,
                                      std::string_view rhs) const noexcept {
            const auto *a = reinterpret_cast<const unsigned char *>(lhs.data());
            const auto *b = reinterpret_cast<const unsigned char *>(rhs.data());
            const std::size_t common = std::min(lhs.size(), rhs.size());
            for (std::size_t i = 0; i < common; ++i) {
                if (a[i] != b[i]) {
                    return a[i] < b[i];
                }
            }
            return lhs.size() < rhs.size();
        }
    };

    // Intern one literal's decoded bytes. `upper_length_bound` is the PtrLen
    // slot wire node's declared length upper bound (nullopt for unbounded); a
    // literal over the bound fails the RESOURCE/frame family. The pool is
    // fail-closed itself with no diagnostic channel, so the planner converts
    // the false result into the named diagnostic.
    [[nodiscard]] bool intern(std::string bytes,
                              std::optional<std::int64_t> upper_length_bound) {
        if (upper_length_bound.has_value() &&
            static_cast<std::int64_t>(bytes.size()) > *upper_length_bound) {
            return false;
        }
        unique_.insert(std::move(bytes));
        // Meter the aligned extent eagerly so a fourth over-capacity literal is
        // rejected at its own plan site with an actionable diagnostic.
        std::uint64_t extent = 0;
        for (const std::string &entry : unique_) {
            extent = (extent + 7u) & ~std::uint64_t{7u};
            extent += static_cast<std::uint64_t>(entry.size());
        }
        if (extent > kP6RodataCapacity) {
            return false;
        }
        return true;
    }

    [[nodiscard]] bool empty() const noexcept { return unique_.empty(); }

    // V2-C fix-forward: replace the pool's internee set with the reachable
    // builders' literals (reachability compaction dropped the dead handlers
    // that interned the old contents). The supplied bytes are a subset of what
    // a prior successful planning round interned, so every length bound and
    // the rodata capacity already held; the bool only re-checks capacity.
    [[nodiscard]] bool rebuild_from_literals(std::span<const std::string> literals) {
        unique_.clear();
        for (const std::string &bytes : literals) {
            unique_.insert(bytes);
        }
        std::uint64_t extent = 0;
        for (const std::string &entry : unique_) {
            extent = (extent + 7u) & ~std::uint64_t{7u};
            extent += static_cast<std::uint64_t>(entry.size());
        }
        return extent <= kP6RodataCapacity;
    }

    // Freeze the pool: sort by byte content, assign 8-aligned offsets, and
    // build the zero-padded image plus the offset lookup.
    void freeze() {
        entries_.clear();
        entries_.reserve(unique_.size());
        image_.clear();
        std::uint32_t cursor = 0;
        for (const std::string &bytes : unique_) {
            Entry entry;
            entry.bytes = bytes;
            entry.offset = cursor;
            image_.append(bytes);
            cursor += static_cast<std::uint32_t>(bytes.size());
            while ((image_.size() & 7u) != 0u) {
                image_.push_back('\0');
                ++cursor;
            }
            entries_.push_back(std::move(entry));
        }
        extent_ = cursor;
    }

    [[nodiscard]] std::uint32_t extent() const noexcept { return extent_; }
    [[nodiscard]] const std::string &image() const noexcept { return image_; }

    // V2-D: the frozen literal byte strings, for merging several per-agent
    // pools into one workflow-module pool.
    [[nodiscard]] std::vector<std::string> frozen_literals() const {
        std::vector<std::string> literals;
        literals.reserve(entries_.size());
        for (const Entry &entry : entries_) {
            literals.push_back(entry.bytes);
        }
        return literals;
    }

    // The frozen rodata offset of `bytes`, or nullopt for a literal that was
    // never interned (a planner/emitter disagreement that must fail closed).
    [[nodiscard]] std::optional<std::uint32_t>
    offset_of(std::string_view bytes) const {
        for (const Entry &entry : entries_) {
            if (entry.bytes == bytes) {
                return entry.offset;
            }
        }
        return std::nullopt;
    }

  private:
    std::set<std::string, ByteLess> unique_;
    std::vector<Entry> entries_;
    std::string image_;
    std::uint32_t extent_{0};
};

// Forward-declared: the physical P6-frame section plan is defined after the
// builder/planning helpers, but AgentPlan must carry an owned instance of it
// (the single physical planning pass runs before handler emission).
struct FrameSectionPlan;
[[nodiscard]] ir::core::CoreFrameLayoutSection
frame_section_to_layout_section(const FrameSectionPlan &plan);

// RFC 0026 P6-7 frame-bridge v2 D3/D4 (rung V2-C): one planned capability
// bridge call site. `param_vt` / `result_vt` are the argument/result value
// types in the flow-storage arena; the physical planner resolves their P4-D
// roots. `spill_bytes` is the site's private scalar/PtrLen spill slot size in
// the control page frame. The remaining fields are installed by the physical
// frame-region planner before handler emission.
struct BridgeCallPlan {
    std::uint32_t call_site_id{0};
    CoreCapabilityId capability{};
    /// The handler state this bridge call belongs to (reachability filtering).
    CoreStateId state{};
    /// V2-D: the packaged workflow runner that owns this site. kInvalid on the
    /// direct-agent lane, where the module has one registry with no runner tag.
    std::uint32_t runner{std::numeric_limits<std::uint32_t>::max()};
    std::vector<CoreValueTypeId> param_vt;
    CoreValueTypeId result_vt{};
    std::uint32_t spill_bytes{0};
    std::uint32_t block_offset{0};
    std::uint32_t spill_base{0};
    std::uint32_t result_base{0};
};

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
    // RFC 0026 P6-7 frame-bridge v2 rung V2-A: true when at least one final
    // state is a ComputedReturnAction. Such an agent is a P6-frame module whose
    // runv returns the fixed output base; mixing it with capability finals (or a
    // second final kind in one agent) is rejected by build_agent_plan.
    bool has_computed_final{false};
    // RFC 0026 P6-7 frame-bridge v2 rung V2-D fix-forward: true when at least
    // one NON-FINAL state is a ComputedGotoAction (a scalar computed-goto
    // preamble, with or without a capability final behind it). The opaque
    // workflow runner is a static GotoAction walk that treats any other action
    // as its terminal, so a workflow packaging such an agent must take the
    // (pending) V2-D computed-runner lane and is fail-closed rejected until
    // that emission lands.
    bool has_computed_goto{false};
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
    // RFC 0026 P6-7 frame-bridge v2 D1 (rung V2-B): the frozen String literal
    // pool. Non-empty iff at least one reachable handler constructs a String
    // literal; then the module emits exactly ONE active Data(11) section
    // initializing [kP6RodataBase, +rodata_extent). Empty modules gain no Data
    // section and stay byte-identical.
    RodataLiteralPool rodata;
    std::uint32_t rodata_extent{0};
    // RFC 0026 P6-7 frame-bridge v2 D5/D6 (rung V2-D): maximum construct-scratch
    // high-water across this agent's planned handlers (relative bytes). A
    // workflow packager sizes the per-instance scratch node sub-block from it.
    std::uint32_t p6_scratch_high{0};
    // RFC 0026 P6-7 frame-bridge v2 D3/D4 (rung V2-C): the dense, ANF-ordered
    // capability bridge call sites planned from non-final handlers. Empty on
    // every non-bridge module, whose bytes stay identical. The physical
    // frame-region planner fills each site's block/result coordinates BEFORE
    // the handler bodies are emitted.
    std::vector<BridgeCallPlan> bridge_calls;
    // The physical P6-frame section plan (boundary table, placements, payload
    // arena, rodata span, V2-C bridge page frame) populated when this agent
    // emits on the frame lane; null on the opaque wire-JSON lane. Owned by
    // the plan so the single physical planning pass runs before handler
    // emission (the bridge emit needs its coordinates) and the section emitter
    // / descriptor consume the identical plan. unique_ptr because
    // FrameSectionPlan is defined later in this translation unit.
    std::unique_ptr<FrameSectionPlan> frame_section;
    // The projected wire-schema table paired with `frame_section` (capability
    // roots + agent boundary roots), needed by the descriptor renderer.
    std::optional<ir::core::CoreWireSchemaTable> frame_wire_table;
};

// Module-wide recorder every frame-lane entry-handler builder registers its
// bridge calls into while PLANNING (dense ids in handler planning order == ANF
// order). The physical frame-region planner installs the control-block / spill
// coordinates (which depend on the whole dense layout sum) before handler
// bodies are EMITTED.
class BridgeCallRegistry {
  public:
    [[nodiscard]] std::uint32_t reserve(CoreCapabilityId capability,
                                        std::vector<CoreValueTypeId> param_vt,
                                        CoreValueTypeId result_vt,
                                        std::uint32_t spill_bytes,
                                        CoreStateId state) {
        const auto id = static_cast<std::uint32_t>(sites_.size());
        BridgeCallPlan site;
        site.call_site_id = id;
        site.capability = capability;
        site.state = state;
        site.runner = workflow_runner_;
        site.param_vt = std::move(param_vt);
        site.result_vt = result_vt;
        site.spill_bytes = spill_bytes;
        sites_.push_back(std::move(site));
        return id;
    }

    // V2-D: install the workflow-packaging context before a relocated runner's
    // handlers plan: every site reserved until the matching finish call is
    // tagged with `runner`. kInvalid runner restores direct-agent mode.
    void begin_workflow_runner(std::uint32_t runner) noexcept {
        workflow_runner_ = runner;
    }

    // V2-D: after one relocated runner's REACHABILITY compaction inputs are
    // known, filter this runner's sites (a contiguous tail appended after every
    // earlier runner) to its reachable handlers, renumber the survivors to
    // GLOBAL dense ids (the site-id base of prior runners plus the local dense
    // index), and install the global control-block / spill coordinates the
    // handler EMIT pass reads. Returns the old-local-id -> global-id remap the
    // caller forwards to every surviving builder. Reachability is indexed by
    // the runner agent's own state id space.
    [[nodiscard]] std::optional<std::unordered_map<std::uint32_t, std::uint32_t>>
    compact_workflow_runner(std::uint32_t runner,
                            std::span<const std::uint8_t> reachable_state,
                            std::uint32_t global_site_base,
                            std::uint32_t runner_spill_base) {
        std::size_t begin = sites_.size();
        while (begin > 0 && sites_[begin - 1].runner == runner) {
            --begin;
        }
        std::vector<BridgeCallPlan> kept;
        kept.reserve(sites_.size() - begin);
        std::unordered_map<std::uint32_t, std::uint32_t> remap;
        std::uint32_t running_spill = 0;
        for (std::size_t i = begin; i < sites_.size(); ++i) {
            if (sites_[i].runner != runner ||
                sites_[i].state.value >= reachable_state.size() ||
                !reachable_state[sites_[i].state.value]) {
                continue;
            }
            BridgeCallPlan site = std::move(sites_[i]);
            const std::uint32_t local_id =
                static_cast<std::uint32_t>(kept.size());
            const std::uint32_t global_id = global_site_base + local_id;
            remap.emplace(site.call_site_id, global_id);
            site.call_site_id = global_id;
            site.block_offset = global_id * block_stride_;
            site.spill_base = runner_spill_base + running_spill;
            running_spill += (site.spill_bytes + 7u) & ~std::uint32_t{7u};
            kept.push_back(std::move(site));
        }
        sites_.erase(sites_.begin() + static_cast<std::ptrdiff_t>(begin), sites_.end());
        sites_.insert(sites_.end(),
                      std::make_move_iterator(kept.begin()),
                      std::make_move_iterator(kept.end()));
        return std::optional{std::move(remap)};
    }

    [[nodiscard]] bool empty() const noexcept { return sites_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return sites_.size(); }
    [[nodiscard]] const std::vector<BridgeCallPlan> &sites() const noexcept { return sites_; }
    [[nodiscard]] std::vector<BridgeCallPlan> &sites() noexcept { return sites_; }

    [[nodiscard]] bool uses_capability(CoreCapabilityId capability) const noexcept {
        return std::any_of(sites_.begin(), sites_.end(),
                           [capability](const BridgeCallPlan &site) {
                               return site.capability == capability;
                           });
    }

    // V2-D: whether ONE packaged runner bridges a capability (the global table
    // is shared across runners, so the per-runner predicates scope by runner).
    [[nodiscard]] bool uses_capability_for_runner(CoreCapabilityId capability,
                                                  std::uint32_t runner) const noexcept {
        return std::any_of(sites_.begin(), sites_.end(),
                           [&](const BridgeCallPlan &site) {
                               return site.runner == runner &&
                                      site.capability == capability;
                           });
    }

    [[nodiscard]] std::size_t site_count_for_runner(std::uint32_t runner) const noexcept {
        return static_cast<std::size_t>(
            std::count_if(sites_.begin(), sites_.end(),
                          [&](const BridgeCallPlan &site) { return site.runner == runner; }));
    }

    // V2-D: the ACTUAL (reachability-compacted) spill window extent one runner
    // occupies in the merged page frame: the top of its last site's spill
    // window relative to `runner_spill_base`. Zero when the runner has no
    // surviving site. The D6 capacity family reserves a conservative bound per
    // runner; the exact section records the tight value.
    [[nodiscard]] std::uint32_t runner_spill_extent(std::uint32_t runner,
                                                    std::uint32_t runner_spill_base) const
        noexcept {
        std::uint32_t top = runner_spill_base;
        for (const BridgeCallPlan &site : sites_) {
            if (site.runner != runner) {
                continue;
            }
            top = std::max(top, site.spill_base +
                                    ((site.spill_bytes + 7u) & ~std::uint32_t{7u}));
        }
        return top - runner_spill_base;
    }

    void install_coordinates(std::uint32_t control_base,
                             std::uint32_t block_stride,
                             std::uint32_t spill_base) noexcept {
        control_base_ = control_base;
        block_stride_ = block_stride;
        spill_base_ = spill_base;
    }

    [[nodiscard]] std::uint32_t control_base() const noexcept { return control_base_; }
    [[nodiscard]] std::uint32_t block_stride() const noexcept { return block_stride_; }
    [[nodiscard]] std::uint32_t spill_base() const noexcept { return spill_base_; }

  private:
    std::vector<BridgeCallPlan> sites_;
    // V2-D: runner tag installed for one relocated workflow build at a time.
    std::uint32_t workflow_runner_{std::numeric_limits<std::uint32_t>::max()};
    std::uint32_t control_base_{0};
    std::uint32_t block_stride_{0};
    std::uint32_t spill_base_{0};
};

struct AgentPlanPolicy {
    std::string_view unsupported_code{core_wasm_diag::kUnsupportedOrchestration};
    bool allow_capability{true};
    // Workflow node packaging (frame-bridge v2 D5, rung V2-D) flips both gates
    // to true: a packaged agent passes the SAME per-handler region/SSA/safety
    // gates a direct agent passes, with no weakened second subset.
    bool allow_computed_goto{true};
    // RFC 0026 P6-7 frame-bridge v2 D3 (rung V2-C): direct-agent emission
    // admits an ORDERED capability bridge statement in a non-final frame
    // computation region.
    bool allow_bridge{true};
    std::string_view slice{"E2"};
    // V2-D RETURN: true ONLY for the single packaged runner whose bare
    // host-packed entry frame the workflow scheduler rewrites in place into
    // module pointer-tree form before that runner executes (the first
    // scheduled node, with a non-constructed Input-sourced input region). The
    // computed-final input-inline provenance gate then admits aggregate
    // projections / payload-enum leaves off `input` for that runner. Every
    // other workflow runner receives an inline scheduler-materialized I_k and
    // leaves this false, and direct-agent builds leave it false: no scheduler
    // normalizes those frames, so the input-inline rejection stands.
    bool admit_normalized_entry_frame{false};
    // V2-D: true for a packaged agent: do not build the agent-level frame
    // section (the workflow packager owns the module-level section) and gather
    // the per-agent facts (scratch high-water, rodata extent, bridge sites) the
    // workflow D6 capacity family needs.
    bool skip_frame_section{false};
    // V2-D fix-forward: true on BOTH workflow packaging builds (fact gathering
    // and relocated emit). It lets the input-inline provenance gate fire on a
    // non-final computed-goto preamble too: only the single entry runner's
    // host-packed I_k is pointer-tree normalized, so every other packaged
    // handler receives an inline scheduler-materialized frame. Direct-agent
    // builds leave this false and keep the historic final-only predicate.
    bool workflow_packaging_lane{false};
    // V2-D emission: when non-null, every frame handler is emitted with its
    // fixed frame regions and state globals relocated onto a packaged instance's
    // node block inside a workflow module. Null on the direct-agent lane.
    const struct P6FrameRelocation *frame_relocation{nullptr};
    // V2-D emission: when non-null, every frame handler interns its String
    // literals into the workflow module's shared rodata pool instead of a
    // private per-agent pool. The caller freezes it after every packaged agent
    // is emitted.
    RodataLiteralPool *shared_rodata_pool{nullptr};
    // V2-D emission half 2: when non-null, bridge statements planned by this
    // build record into the WORKFLOW-module shared dense registry (instead of a
    // private agent registry). `wf_runner` is this build's packaged-runner
    // index; `wf_site_id_base` is the global dense-id base of its first site;
    // `wf_runner_spill_base` is the global spill-window base for this runner.
    BridgeCallRegistry *shared_bridge_registry{nullptr};
    std::uint32_t wf_runner{0};
    std::uint32_t wf_site_id_base{0};
    std::uint32_t wf_runner_spill_base{0};
    // V2-D: module-global bridge control-page coordinates the relocated
    // handlers' bridge emit reads (block pointer = control_base + global site
    // id * block_stride).
    std::uint32_t wf_control_base{0};
    std::uint32_t wf_block_stride{0};
    // V2-D: the workflow module's global sorted-unique capability import table
    // (a relocated build emits bridge calls at these module ordinals).
    const std::vector<CoreCapabilityId> *wf_imports{nullptr};
};

// V2-D: relocation of a packaged agent's frame-lane handler bytes from the
// direct-agent fixed regions onto one packaged instance's node-frame block in
// a workflow module. Installed on each P6ComputationHandlerBuilder before emit.
struct P6FrameRelocation {
    std::uint32_t input_base{0};
    std::uint32_t input_capacity{0};
    std::uint32_t context_base{0};
    std::uint32_t context_capacity{0};
    std::uint32_t scratch_base{0};
    std::uint32_t scratch_capacity{0};
    std::uint32_t output_base{0};
    std::uint32_t output_capacity{0};
    // Workflow global indices the relocated handler's state latch, per-goto
    // counter and dynamic-construct heap land in.
    std::uint32_t current_state_global{0};
    std::uint32_t transition_count_global{0};
    std::uint32_t heap_next_global{0};
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
    // Kahn schedule (== its manifest array index). A node reachable to one or
    // more capabilities carries them + their source SymbolIds; identity nodes
    // leave capabilities empty. A P6 bridge node can call multiple capabilities
    // across different branches.
    std::uint32_t schedule_pos{0};
    std::vector<std::pair<CoreCapabilityId, std::uint64_t>> capabilities;
};

// One value-binding let of a workflow frame region in the form the in-module
// scheduler materializes it. Either a frame PATH read (bare or projected,
// rooted in the workflow input frame or an upstream node's inline output block)
// or a flat aggregate CONSTRUCT whose operands are earlier lets.
struct WorkflowFrameLet {
    CoreValueId result{};
    CoreValueTypeId value_type{};
    bool is_construct{false};
    // Path lets:
    WorkflowFrameSource source;
    const CorePathExpr *path{nullptr};
    // Construct lets:
    const CoreConstructExpr *construct{nullptr};
};

struct WorkflowRegionPlan {
    // The frame source the region yields (workflow input or an upstream node
    // output).
    WorkflowFrameSource source;
    // True for a V2-D projected/constructed P4-D region the scheduler must
    // materialize with in-module word copies (a field projection, or an
    // aggregate construct such as a two-upstream node input). False for the
    // legacy E3 exact bare-frame forwarding shape (one let + one yield of an
    // unprojected input/node-output path), which forwards the source verbatim.
    bool constructed{false};
    // The yield value (the region's final frame root).
    CoreValueId yield_value{};
    // The materialized lets in ANF order; empty for a bare forward.
    std::vector<WorkflowFrameLet> lets;
    // Compile-time construct-scratch bytes the scheduler reserves for this
    // region's aggregate constructs (aligned), 0 when the region has none.
    std::uint32_t construct_scratch{0};
};

// V2-D fact-gathering facts per packaged instance (agent-lane coordinates;
// the workflow relocation is applied during module emission).
struct WorkflowP6GatherFacts {
    bool p6{false};
    std::uint32_t scratch_high{0};
    std::uint32_t rodata_extent{0};
    std::uint32_t bridge_site_count{0};
    std::uint32_t bridge_max_arity{0};
    std::uint32_t bridge_spill_extent{0};
    std::uint32_t bridge_result_extent{0};
};

// V2-D: one packaged instance's fixed node-frame block coordinates.
struct WorkflowNodeBlock {
    CoreInstanceId instance{};
    std::uint32_t input_layout{0};
    std::uint32_t context_layout{0};
    std::uint32_t output_layout{0};
    std::uint32_t input_size{0};
    std::uint32_t context_size{0};
    std::uint32_t output_size{0};
    std::uint32_t input_base{0};
    std::uint32_t context_base{0};
    std::uint32_t scratch_base{0};
    std::uint32_t scratch_size{0};
    std::uint32_t output_base{0};
};

// WH-5b.1: sentinel for p6_block_by_runner when a packaged-instance runner is
// opaque (no fixed node-frame block).
constexpr std::uint32_t kInvalidP6Block =
    std::numeric_limits<std::uint32_t>::max();

struct WorkflowPlan {
    CoreWorkflowId workflow{};
    std::vector<CoreWorkflowNodeId> schedule;
    std::vector<CoreInstanceId> packaged_instances;
    std::vector<AgentPlan> agent_plans;
    std::vector<WorkflowNodePlan> nodes;
    WorkflowFrameSource output;
    // V2-D: the validated input region per node (indexed by node id) and the
    // validated return region; only consulted on the P6 frame lane.
    std::vector<WorkflowRegionPlan> node_regions;
    WorkflowRegionPlan return_region;
    // RFC 0026 E4-B2-C: sorted-unique reachable capability ids across all node
    // agents (empty for an identity workflow). Non-empty triggers the
    // capability-workflow baseline (import section, manifest, event buffer, latch,
    // checked alloc).
    std::vector<CoreCapabilityId> imports;
    // V2-D: true when at least one scheduled node runs on the P6 frame lane.
    // Gates the computed runners, node blocks, frame sections and the Data
    // section; an all-opaque workflow stays byte-identical.
    bool has_p6_nodes{false};
    // WH-5b.1: node blocks are P6-RUNNER-ONLY dense order (packaged-instance
    // order with opaque runners skipped). An opaque capability-final runner
    // keeps its heap tuple and owns no fixed node-frame block, so the frame
    // section's node_blocks array carries only nonzero P6 spans (the
    // transport verifier's honesty guarantee needs no special-casing).
    std::vector<WorkflowNodeBlock> node_blocks;
    // Maps every packaged-instance runner index to its P6-only node_blocks
    // ordinal, or kInvalidP6Block when the runner is opaque.
    std::vector<std::uint32_t> p6_block_by_runner;
    // V2-D module-level physical coordinates (all compile-time constants).
    std::uint32_t bridge_control_base{0};
    std::uint32_t bridge_block_stride{0};
    std::uint32_t bridge_control_extent{0};
    std::uint32_t bridge_spill_base{0};
    std::uint32_t bridge_spill_extent{0};
    std::uint32_t node_blocks_base{0};
    std::uint32_t node_blocks_extent{0};
    // V2-D: the in-module scheduler's construct-scratch region for projected /
    // constructed node inputs and the workflow return (zero on a bare-forward
    // workflow).
    std::uint32_t region_scratch_base{0};
    std::uint32_t region_scratch_extent{0};
    // V2-D: fixed scratch the scheduler uses to turn the host-packed INLINE
    // entry frame into the module pointer-tree form the packaged handlers read
    // (every inline aggregate child gets its own addressed window). Zero on a
    // bare-forward workflow whose handlers read no input aggregate.
    std::uint32_t entry_normalize_base{0};
    std::uint32_t entry_normalize_extent{0};
    std::uint32_t entry_payload_base{0};
    std::uint32_t entry_payload_capacity{0};
    std::uint32_t wf_output_base{0};
    std::uint32_t wf_output_size{0};
    std::uint32_t wf_heap_base{0};
    // V2-D emission half 2: the fixed state-entry trace ring the packaged
    // runners append real (runner, state) evidence to (8-byte count header +
    // one 8-byte record per agent state, sized for the maximum possible single
    // run: every declared state of every packaged agent exactly once).
    std::uint32_t state_trace_base{0};
    std::uint32_t state_trace_capacity{0};
    // V2-D workflow output boundary value type (sizes the workflow output
    // slot in the D6 capacity family).
    CoreValueTypeId wf_output_vt{};
    // V2-D emission: relocated frame-handler bodies per packaged runner
    // (parallel to agent_plans), produced by the second, relocated
    // build_agent_plan pass in the emit driver. Empty for an opaque runner.
    std::vector<std::vector<CompiledHandler>> relocated_handlers;
    // V2-D emission: the merged, frozen module-wide rodata pool (one Data
    // section image across every packaged agent).
    RodataLiteralPool workflow_rodata;
    std::uint32_t workflow_rodata_extent{0};
    // V2-D emission: dense frame-layout table roots per runner (indices into
    // the workflow core-layout section's dense table) and the workflow output
    // root, plus the owned section/wire tables the descriptor mirrors.
    std::vector<ir::core::CoreLayoutId> dense_node_input_layouts;
    std::vector<ir::core::CoreLayoutId> dense_node_context_layouts;
    std::vector<ir::core::CoreLayoutId> dense_node_output_layouts;
    ir::core::CoreLayoutId dense_wf_output_layout{};
    std::optional<ir::core::CoreFrameLayoutSection> frame_section;
    std::optional<ir::core::CoreWireSchemaTable> frame_wire_table;
    // V2-D emission half 2: the flattened, runner-grouped dense bridge call
    // sites across every packaged runner (global call_site_id order == control
    // block dense order). Empty on an all-opaque / computed-final-only workflow.
    std::vector<BridgeCallPlan> workflow_bridge_sites;
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
    // V2-D: total relocated frame-handler functions across every packaged
    // runner, appended AFTER run2 (each is a `() -> i32` handler). Zero on an
    // all-opaque workflow, so the fixed 6+runner_count shape is byte-identical.
    std::uint32_t handler_count{0};
    // RFC 0026 FB-1: outlined fn bodies follow runner/run2/handlers (§6.1 agent
    // ordering discipline). In FB-1 a workflow module's node-input/return
    // regions accept only opaque workflow frames and its packaged agents
    // disallow computed handlers, so no outlined fn is ever reachable from a
    // workflow MODULE (a packaged agent's direct calls are compiled into that
    // agent's own module). The field stays 0 and an opaque workflow module is
    // byte-identical to its E1-E3/E4 shape; the projection exists for
    // index-space symmetry with the agent FunctionTable and the FB-3
    // closure/indirect slice.
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
    // V2-D: absolute index of relocated handler `handler_index` (the sum of the
    // earlier runner handler counts plus this one's local index).
    [[nodiscard]] std::uint32_t handler(std::uint32_t handler_index) const noexcept {
        return import_count + 6u + runner_count + handler_index;
    }
    // RFC 0026 FB-1: outlined fn ordinal -> absolute function index. Fn bodies
    // follow runner/run2/handlers; fn_count is 0 for a workflow module in FB-1
    // (see the field note), keeping the module byte-identical.
    [[nodiscard]] std::uint32_t fn(std::uint32_t ordinal) const noexcept {
        return import_count + 6u + runner_count + handler_count + ordinal;
    }
    [[nodiscard]] std::uint32_t defined_count() const noexcept {
        return 6u + runner_count + handler_count + fn_count;
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
           std::holds_alternative<CapabilityAction>(action) ||
           std::holds_alternative<ComputedReturnAction>(action);
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

// Pure, side-effect-free twin of the identity-final shape check: is this final the
// canonical `let in = input; return in;` passthrough (with matching input/output
// nominals)? Used to route a capability-free final between the v1 identity lane
// and the V2-A computed-final lane WITHOUT emitting diagnostics or marking the
// used-expr/value arenas (those belong to validate_identity_final on whichever
// path actually runs).
[[nodiscard]] bool is_identity_final_shape(const CoreAgentDecl &agent,
                                           const CoreFlowDecl &flow,
                                           const ir::core::CoreRegion &region) {
    if (region.statements.size() != 2) {
        return false;
    }
    const auto *let = std::get_if<CoreLetStmt>(&region.statements[0].node);
    const auto *ret = std::get_if<CoreReturnStmt>(&region.statements[1].node);
    if (let == nullptr || ret == nullptr || !ret->has_value ||
        ret->value != let->result ||
        let->expr.value >= flow.storage.exprs.size() ||
        let->result.value >= flow.storage.value_types.size()) {
        return false;
    }
    const CoreExpr &expr = flow.storage.exprs[let->expr.value];
    const auto *path = std::get_if<CorePathExpr>(&expr.node);
    // The identity final is `let in = input; return in;`: a bare input
    // frame read (root == Input, no members/projection, not a local).
    if (path == nullptr || path->root != ir::core::CorePathRoot::Input ||
        path->has_local || !path->projection.empty() || !path->members.empty() ||
        !path->projection_resolved || path->root_type != agent.input_type) {
        return false;
    }
    return agent.input_type == agent.output_type;
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
[[nodiscard]] bool is_p6_subset_region(const CoreRegion &region,
                                       bool allow_bridge_calls = false) {
    for (const CoreStmt &statement : region.statements) {
        const bool in_subset = std::visit(
            Overloaded{
                [](const CoreLetStmt &) { return true; },
                [](const CoreGotoStmt &) { return true; },
                [](const CoreTrapStmt &) { return true; },
                [](const CoreYieldStmt &) { return false; },
                [allow_bridge_calls](const CoreIfStmt &s) {
                    return (s.then_region == nullptr ||
                            is_p6_subset_region(*s.then_region, allow_bridge_calls)) &&
                           (s.else_region == nullptr ||
                            is_p6_subset_region(*s.else_region, allow_bridge_calls));
                },
                // A match is structurally in-subset; its consumed regions are
                // validated by `plan_match_region` (see the header comment).
                [](const CoreMatchStmt &) { return true; },
                // RFC 0026 P6-4: a context store (`ctx.field = v`) is now a
                // computation statement lowered to a memory store. Capability and
                // return stay on the KR6.5 orchestration lane.
                [](const CoreStoreStmt &) { return true; },
                [allow_bridge_calls](const CoreCapabilityCallStmt &) {
                    // RFC 0026 P6-7 frame-bridge v2 D3 (rung V2-C): an ORDERED
                    // bridge capability call is admitted in a non-final frame
                    // computation region; an opaque / final capability call keeps
                    // its canonical KR6.5 E2 shape and stays false.
                    return allow_bridge_calls;
                },
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

[[nodiscard]] bool is_p6_computation_region(const CoreRegion &region,
                                            bool allow_bridge_calls = false) {
    return is_p6_subset_region(region, allow_bridge_calls);
}

// RFC 0026 P6-7 frame-bridge v2 rung V2-A: a COMPUTED FINAL region is the P6
// subset (scalar lets, structured if, match, ctx store, ordered pure-fn calls)
// whose terminator is a value-bearing CoreReturnStmt instead of a goto/trap.
// A match stays structurally admitted; its arms/fallback consume returns the
// same way the computed-goto lane consumes gotos (per-arm fail-closed planning
// in the builder). A capability effect keeps the region off this lane (the
// capability final has its own opaque E2 shape; in-handler bridging is V2-C).
[[nodiscard]] bool is_p6_computed_final_region(const CoreRegion &region) {
    for (const CoreStmt &statement : region.statements) {
        const bool in_subset = std::visit(
            Overloaded{
                [](const CoreLetStmt &) { return true; },
                [](const CoreGotoStmt &) { return false; },
                [](const CoreTrapStmt &) { return true; },
                [](const CoreYieldStmt &) { return false; },
                [](const CoreIfStmt &s) {
                    return (s.then_region == nullptr ||
                            is_p6_computed_final_region(*s.then_region)) &&
                           (s.else_region == nullptr ||
                            is_p6_computed_final_region(*s.else_region));
                },
                [](const CoreMatchStmt &) { return true; },
                [](const CoreStoreStmt &) { return true; },
                [](const CoreCapabilityCallStmt &) { return false; },
                [](const CoreReturnStmt &) { return true; },
                [](const CoreCallStmt &) { return true; },
            },
            statement.node);
        if (!in_subset) {
            return false;
        }
    }
    return true;
}

// Structural all-paths termination predicate for a computed-final region:
// EVERY path must end in a value-bearing CoreReturnStmt (a final handler
// materializes an output on every path). Mirrors p6_region_always_diverges:
// only the last statement matters (everything after a terminator is
// unreachable), a trailing if requires both branches to return, and a trailing
// match requires every arm and the fallback to return.
[[nodiscard]] bool p6_region_always_returns(const CoreRegion &region) {
    if (region.statements.empty()) {
        return false;
    }
    const CoreStmt &last = region.statements.back();
    if (const auto *ret = std::get_if<CoreReturnStmt>(&last.node)) {
        return ret->has_value;
    }
    if (const auto *branch = std::get_if<CoreIfStmt>(&last.node)) {
        return branch->then_region && branch->else_region &&
               p6_region_always_returns(*branch->then_region) &&
               p6_region_always_returns(*branch->else_region);
    }
    if (const auto *match = std::get_if<CoreMatchStmt>(&last.node)) {
        if (!match->fallback_region || !p6_region_always_returns(*match->fallback_region)) {
            return false;
        }
        return std::ranges::all_of(match->arms, [](const CoreMatchArm &arm) {
            return arm.body && p6_region_always_returns(*arm.body);
        });
    }
    return false;
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
    // RFC 0026 P6-7 frame-bridge v2 D1 (rung V2-B): a STRING value. Its whole
    // runtime representation is the eight-byte PtrLen word pair
    // `(payload_ptr:i32, byte_len:i32)` (P4-D `CoreLayoutPtrLen`, the only
    // PtrLen family). Like a Closure it occupies TWO consecutive i32 locals /
    // stack words, but unlike a Closure it is inline 8-byte data in every
    // struct/enum frame slot (no pointer-tree expansion, no funcref table), it
    // never crosses the capability/fn ABI, and its two words are compile-time
    // immediates for a literal: (kP6RodataBase + pool_offset, utf8_byte_len).
    String,
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
    // RFC 0026 P6-7 frame-bridge v2 D1 (rung V2-B): a String's whole physical
    // representation is the eight-byte CoreLayoutPtrLen word pair
    // (payload_ptr, byte_len). PtrLen is the String-only layout family, so this
    // mapping is total. It is a TWO-word inline value: never an aggregate
    // address, never an operand of a scalar/collection op.
    if (std::holds_alternative<ir::core::CoreLayoutPtrLen>(
            layouts.layouts[layout_id.value].shape)) {
        return P6ScalarKind::String;
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
    // RFC 0026 P6-7 frame-bridge v2 D1 (rung V2-B): a Decimal / Duration literal
    // is embedded as its compile-time i64 word (mantissa / bare milliseconds),
    // so on the physical P6 word model both ride the IntI64 kind — a constant
    // i64.const, never an arithmetic operand (plan_binary/unary require a real
    // CoreVtInt). Such a final DOES emit a frame section: there is deliberately
    // NO output-frame gate rejecting Decimal/Duration leaves (frame-bridge v2
    // section 0 non-goals and section 10 V2-B — "i64 字面量, 无语差分 fixture,
    // 仅词法/行走单测"). The certified embedded host keeps their wire nodes
    // outside the rung-E walk subset (node_embedded_host.mjs packValue/readValue
    // reject 'decimal'/'duration'), so the i64 word is produced but intentionally
    // UNREACHABLE through a conforming host observation until a later rung
    // renders the builtin s<scale>:<mantissa> / bare-millis spelling. The
    // emit-but-unobservable decision is pinned by a structural probe so this
    // comment and the code cannot drift.
    if ((std::holds_alternative<CoreVtDecimal>(node) ||
         std::holds_alternative<CoreVtDuration>(node)) &&
        scalar->repr == ir::core::CoreScalarRepr::I64) {
        return P6ScalarKind::IntI64;
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
//   StringWiden  : since frame-bridge v2 V2-B a `CoreVtString` IS a first-class
//                  two-word PtrLen P6 value, so a strict length widening whose
//                  endpoints share the layout-identical PtrLen shape is a
//                  same-bytes no-op: the two inline words move unchanged. It is
//                  realizable only on the frame computed-final lane (the only
//                  builder that constructs / carries a String pair); a String
//                  literal anywhere else is rejected earlier by the
//                  rodata-pool gate, not by this classifier.
//   CapacityWiden: outside the P6 value model. A bounded collection is a
//                  `CoreLayoutContainer` keyed by `capacity` -> `backing_size`,
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

    // RFC 0026 P6-7 frame-bridge v2 rung V2-A: put this HANDLER builder in
    // computed-final mode. A computed-final handler is a `() -> i32` function
    // like a computed-goto handler (step() calls it and it yields the stable
    // final state id), but every path ends in a value-bearing CoreReturnStmt
    // that MATERIALIZES the result into the fixed output frame
    // (kP6AggregateOutputBase). The output value type drives the materializer's
    // shape walk. Two scratch i32 locals are reserved here (source-address
    // holder for an aggregate/enum root, copy-loop cursor for a collection
    // header), before plan() assigns the SSA pools.
    void enable_computed_final(CoreValueTypeId output_value_type, std::uint32_t final_state_id) {
        final_return_mode_ = true;
        final_return_type_ = output_value_type;
        final_state_id_ = final_state_id;
    }

    // RFC 0026 P6-7 frame-bridge v2 D1 (rung V2-B): install the module-wide
    // String literal pool a frame handler's literals are interned into during
    // planning and read from at emit time. Null outside a P6-frame agent, where
    // String literals keep failing closed (no rodata region is planned there).
    void install_rodata_pool(RodataLiteralPool *pool) { rodata_pool_ = pool; }

    // V2-C fix-forward: the String literal bytes THIS builder interned while
    // planning. Reachability compaction rebuilds the module-wide pool from the
    // surviving builders so dead-handler literals cannot pin rodata space.
    [[nodiscard]] const std::vector<std::string> &rodata_literals() const noexcept {
        return rodata_literals_;
    }

    // V2-D: the construct-scratch high-water this builder planned (relative
    // bytes; the workflow packager sizes the per-instance scratch sub-block
    // from the maximum across an agent's handlers).
    [[nodiscard]] std::uint32_t scratch_high_water() const noexcept {
        return scratch_addr_cursor_;
    }

    // RFC 0026 P6-7 frame-bridge v2 D3/D4 (rung V2-C): install the module-wide
    // bridge call-site registry. Null on every non-frame builder, where an
    // in-handler capability call keeps its canonical KR6.5 final shape and is
    // rejected. When installed, a non-final handler may plan ORDERED bridge
    // calls that the physical frame planner serves with disjoint per-call-site
    // control blocks and result placements.
    void install_bridge_registry(BridgeCallRegistry *registry, CoreStateId state) {
        bridge_registry_ = registry;
        bridge_state_ = state;
    }

    // V2-C fix-forward: renumber this builder's bridge statement ids after
    // unreachable call sites were filtered out of the dense module table.
    // Returns false (a compiler-internal inconsistency the caller must reject
    // on) when a retained statement names a filtered-away id: an emitted bridge
    // statement whose dense site no longer exists would index sites()[id] out of
    // bounds and the host would reject the module. No-op for a builder without
    // bridge statements.
    [[nodiscard]] bool remap_bridge_call_ids(
        const std::unordered_map<std::uint32_t, std::uint32_t> &remap,
        std::size_t dense_site_count) {
        for (auto &[statement, id] : bridge_call_ids_) {
            const auto it = remap.find(id);
            if (it == remap.end() || it->second >= dense_site_count) {
                return false;
            }
            id = it->second;
        }
        return true;
    }

    // Validate the handler is in the scalar subset and assign every bound SSA
    // value a per-repr pool slot (i32 group first, then i64 — a real function
    // needs one fixed type per local index), recording the goto target set.
    [[nodiscard]] bool plan() {
        if (fn_mode_) {
            // FB-1 outlined fn: the Core verifier proves every path value-returns;
            // no structural divergence/return gate here.
        } else if (final_return_mode_) {
            if (!p6_region_always_returns(region_)) {
                return reject("a computed final must return a value on every path",
                              region_.statements.empty()
                                  ? ir::SourceRangeOpt{}
                                  : region_.statements.front().source_range);
            }
        } else if (!p6_region_always_diverges(region_)) {
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
        // V2-A: the computed-final materializer's one source-address scratch
        // i32, appended after the KeyGet temps (absent outside a computed
        // final).
        const std::uint32_t final_count = final_return_mode_ ? final_src_count_ : 0u;
        // V2-B: one temp holding a String ctx-store slot address for its two
        // PtrLen stores (absent without such a store).
        const std::uint32_t ctx_store_count = ptrlen_ctx_store_needed_ ? 1u : 0u;
        // V2-B: one temp holding a projection slot address while reading the
        // String PtrLen leaf's two words (absent without such a read).
        const std::uint32_t ptrlen_read_count = ptrlen_read_needed_ ? 1u : 0u;
        // V2-C: a HANDLER bridge call needs two trailing i32 scratch locals
        // (status, result_root_ptr) for the (i32)->(i32,i32) import result.
        const std::uint32_t bridge_count =
            (!fn_mode_ && bridge_scratch_needed_) ? 2u : 0u;
        if (temp_count != 0 || keyget_count != 0 || final_count != 0 ||
            ctx_store_count != 0 || ptrlen_read_count != 0 || bridge_count != 0) {
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
            if (final_count != 0) {
                final_src_local_ = after_groups + temp_count + keyget_count;
            }
            if (ctx_store_count != 0) {
                ctx_store_addr_local_ =
                    after_groups + temp_count + keyget_count + final_count;
            }
            if (ptrlen_read_count != 0) {
                ptrlen_read_addr_local_ =
                    after_groups + temp_count + keyget_count + final_count +
                    ctx_store_count;
            }
            if (bridge_count != 0) {
                const std::uint32_t base =
                    after_groups + temp_count + keyget_count + final_count +
                    ctx_store_count + ptrlen_read_count;
                bridge_status_local_ = base;
                bridge_ptr_local_ = base + 1u;
            }
        }
        // Handler mode, aggregate/closure heap enabled: reset the per-activation
        // bump heap before the handler region so every step() starts with a
        // fresh arena (the prior step's aggregate / closure-env bytes are
        // abandoned — ByValue snapshots never alias across activations).
        if (!fn_mode_ && reset_construct_heap_) {
            emit_const_i32(static_cast<std::int32_t>(reset_heap_base_));
            body_.byte(kOpGlobalSet);
            body_.u32(handler_heap_next_global());
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
        const std::uint32_t temp_i32 =
            temp_count + keyget_count + final_count + ctx_store_count +
            ptrlen_read_count + bridge_count;
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
    // RFC 0026 P6-7 frame-bridge v2 rung V2-A: handler-mode computed final.
    // Every path ends in a value-bearing CoreReturnStmt materialized into the
    // fixed output frame; `final_return_type_` is the agent's output value type.
    bool final_return_mode_{false};
    CoreValueTypeId final_return_type_{};
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

    // RFC 0026 P6-7 frame-bridge v2 D1 (rung V2-B): a String ctx store needs the
    // walked slot address twice (the PtrLen is two i32 stores), so it is teed
    // into ONE trailing i32 temp local. Allocated only when such a store is
    // planned; lives in the second i32 group after the i64 group.
    bool ptrlen_ctx_store_needed_{false};
    std::uint32_t ctx_store_addr_local_{std::numeric_limits<std::uint32_t>::max()};
    // V2-B: one temp holding the walked slot address while reading a String
    // PtrLen leaf's two words through a projection. Allocated only when such a
    // read is planned.
    bool ptrlen_read_needed_{false};
    std::uint32_t ptrlen_read_addr_local_{std::numeric_limits<std::uint32_t>::max()};

    // RFC 0026 P6-7 frame-bridge v2 rung V2-A: a computed-final aggregate /
    // collection materialization needs ONE trailing i32 scratch local holding
    // the root value's source address (a scratch-constructed aggregate or an
    // input-reached collection header). Scalar roots materialize straight from
    // their SSA local and need no scratch slot. Like the bump/keyget
    // temporaries it lives in the SECOND i32 local group after the i64 group so
    // every SSA/scratch pool index is unchanged.
    // Emit-time absolute local index of the FIRST source-address scratch
    // local; nested aggregate dereferences use final_src_local_ + level.
    std::uint32_t final_src_local_{std::numeric_limits<std::uint32_t>::max()};
    // Number of source-address scratch locals this computed final needs:
    // one for the root aggregate/collection plus one per nested aggregate
    // dereference depth. 0 for a scalar root.
    std::uint32_t final_src_count_{0};
    // The final state's own dense id; the materializing terminator yields it
    // through the handler's result-i32 block.
    std::uint32_t final_state_id_{0};

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

    // RFC 0026 P6-7 frame-bridge v2 D3/D4 (rung V2-C): set when a HANDLER plans
    // at least one capability bridge call. emit() reserves two trailing i32
    // scratch locals (status, result_root_ptr) for the (i32)->(i32,i32) bridge
    // import result. They share the second i32 local group after the i64 group
    // and are independent of the FB-4 fn-mode capability scratch.
    bool bridge_scratch_needed_{false};
    std::uint32_t bridge_status_local_{std::numeric_limits<std::uint32_t>::max()};
    std::uint32_t bridge_ptr_local_{std::numeric_limits<std::uint32_t>::max()};

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

    // V2-D RETURN: true when the workflow scheduler normalizes the host-packed
    // INLINE entry frame into module pointer-tree form before this handler's
    // runner executes. The computed-final input-inline provenance gate then
    // admits aggregate projections/payload-enum leaves off `input` (they are
    // dereferenceable after normalization). Direct-agent builders leave this
    // false.
    bool normalized_entry_frame_admitted_{false};

    // V2-D fix-forward: true on BOTH workflow packaging builds (fact gathering
    // and relocated emit). It lets the input-inline provenance gate fire on a
    // non-final computed-goto preamble too: only the single entry runner's
    // host-packed I_k is pointer-tree normalized, so every OTHER packaged
    // handler receives an inline scheduler-materialized frame and must not
    // dereference aggregate edges off INPUT. Direct-agent builders leave this
    // false and keep the historic final-only predicate.
    bool workflow_packaging_lane_{false};

    // RFC 0026 P6-7 frame-bridge v2 rung V2-B: module-wide String literal pool
    // (null outside a P6-frame agent). A String literal SSA value is the PtrLen
    // immediate pair naming this pool; the pool is frozen between plan and emit.
    RodataLiteralPool *rodata_pool_{nullptr};
    // Byte content of every literal this builder interned (duplicates kept;
    // the pool hash-conses on rebuild).
    std::vector<std::string> rodata_literals_;

    // Fixed P6 frame regions (single named authority for every emit/plan site).
    // The V2-D workflow packager RELOCATES these onto one packaged instance's
    // fixed node-frame block (I_k/C_k/scratch_k/O_k) and rebases the two state
    // globals a handler touches onto the workflow module's global indices via
    // install_frame_relocation(); until then the canonical agent-lane constants
    // and global indices apply, so a direct agent emission is unchanged.
  public:
    using FrameRelocation = P6FrameRelocation;

    // V2-D: relocate every fixed frame region this builder emits onto one
    // packaged instance's node-frame block and rebase the state globals onto
    // the workflow module's global section. Called once, after plan() and
    // before emit(), by the workflow packager.
    void install_frame_relocation(FrameRelocation relocation) {
        relocation_ = std::move(relocation);
    }

    // V2-D RETURN: the workflow scheduler rewrites the INLINE entry frame into
    // pointer-tree form before this handler runs, so input-frame aggregate
    // projections are dereferenceable here.
    void admit_normalized_entry_frame() {
        normalized_entry_frame_admitted_ = true;
    }

    // V2-D fix-forward: mark this builder as compiled on the workflow
    // packaging lane (fact gathering or relocated emit), so the inline-input
    // provenance gate applies to non-final handlers as well.
    void mark_workflow_packaging_lane() {
        workflow_packaging_lane_ = true;
    }

    [[nodiscard]] std::uint32_t input_base() const noexcept {
        return relocation_ ? relocation_->input_base : kP6AggregateInputBase;
    }
    [[nodiscard]] std::uint32_t input_capacity() const noexcept {
        return relocation_ ? relocation_->input_capacity : kP6AggregateInputCapacity;
    }
    [[nodiscard]] std::uint32_t context_base() const noexcept {
        return relocation_ ? relocation_->context_base : kP6AggregateContextBase;
    }
    [[nodiscard]] std::uint32_t context_capacity() const noexcept {
        return relocation_ ? relocation_->context_capacity : kP6AggregateContextCapacity;
    }
    [[nodiscard]] std::uint32_t scratch_base() const noexcept {
        return relocation_ ? relocation_->scratch_base : kP6AggregateScratchBase;
    }
    [[nodiscard]] std::uint32_t scratch_capacity() const noexcept {
        return relocation_ ? relocation_->scratch_capacity : kP6AggregateScratchCapacity;
    }
    [[nodiscard]] std::uint32_t output_base() const noexcept {
        return relocation_ ? relocation_->output_base : kP6AggregateOutputBase;
    }
    [[nodiscard]] std::uint32_t output_capacity() const noexcept {
        return relocation_ ? relocation_->output_capacity : kP6AggregateOutputCapacity;
    }
    [[nodiscard]] std::uint32_t rodata_base() const noexcept { return kP6RodataBase; };

    // The workflow module's global indices for private current state and the
    // per-node transition counter (== the agent indices on the direct lane).
    [[nodiscard]] std::uint32_t handler_current_state_global() const noexcept {
        return relocation_ ? relocation_->current_state_global : kGlobalCurrentState;
    }
    [[nodiscard]] std::uint32_t handler_transition_count_global() const noexcept {
        return relocation_ ? relocation_->transition_count_global : kGlobalTransitionCount;
    }
    [[nodiscard]] std::uint32_t handler_heap_next_global() const noexcept {
        return relocation_ ? relocation_->heap_next_global : kGlobalHeapNext;
    }

    // V2-D: null on the direct-agent lane (canonical fixed regions); installed
    // by the workflow packager for a relocated per-node handler emission.
  private:
    std::optional<FrameRelocation> relocation_;

    // RFC 0026 P6-7 frame-bridge v2 D3/D4 (rung V2-C): the module-wide bridge
    // call-site registry (null outside a frame-lane handler). A planned bridge
    // statement reserves its dense id here; statement identity is the stable
    // address of its variant node, so emit maps the same node back to its id.
    BridgeCallRegistry *bridge_registry_{nullptr};
    CoreStateId bridge_state_{};
    std::unordered_map<const CoreCapabilityCallStmt *, std::uint32_t> bridge_call_ids_;
    // Result SSA value ids produced by bridge calls in this builder (provenance
    // for the all-run-stable String ctx-store gate).
    std::vector<CoreValueId> bridge_result_values_;
    // Flow-global SSA value space and the pure-expression arena are NUMBERED
    // SEPARATELY: a statement-produced value (e.g. a capability call result) has
    // no expr at all, and a `let` binds a result value to an arbitrary expr id.
    // plan_let records that binding here so provenance walks resolve a value to
    // the expr that defines it.
    std::unordered_map<std::uint32_t, std::uint32_t> let_value_exprs_;

    // RFC 0026 P6-7 frame-bridge v2 V2-A fix-forward: the computed-final
    // materializer expands the MODULE aggregate pointer tree, so it must never
    // be handed an aggregate that names the host-packed INLINE input frame
    // (whose fields are in place with no child-address slots). The birth-site
    // gate in `plan_path` rejects such reads when they are planned; the
    // constructor-operand and materializer-root checks below re-assert the same
    // invariant at the exact dereference boundary (defense in depth at a
    // host-trust boundary).
    //
    // Whether an aggregate-typed (`Ptr`) value lives in the host-packed INLINE
    // input frame rather than in module pointer-tree scratch. ANF SSA locals
    // form a DAG of let/value-ref/coerce chains rooted at one path, so the walk
    // terminates; `seen` guards defensively. A scalar or bounded collection is
    // never held by an aggregate field as a child ADDRESS and returns false.
    [[nodiscard]] bool
    value_names_input_inline_aggregate(CoreValueId value,
                                       std::vector<std::uint32_t> &seen) const {
        if (std::find(seen.begin(), seen.end(), value.value) != seen.end()) {
            return false;
        }
        seen.push_back(value.value);
        if (value.value >= storage_.value_types.size()) {
            return false;
        }
        if (scalar_kind(storage_.value_types[value.value]) != P6ScalarKind::Ptr) {
            return false;
        }
        const auto def = let_value_exprs_.find(value.value);
        if (def == let_value_exprs_.end() || def->second >= storage_.exprs.size()) {
            // A statement-produced aggregate (call result) is always
            // module-side; only a let-bound path can name the input frame.
            return false;
        }
        const CoreExpr &expr = storage_.exprs[def->second];
        if (const auto *ref = std::get_if<CoreValueRefExpr>(&expr.node)) {
            return value_names_input_inline_aggregate(ref->value, seen);
        }
        if (const auto *coerce = std::get_if<CoreCoerceExpr>(&expr.node)) {
            return value_names_input_inline_aggregate(coerce->operand, seen);
        }
        if (const auto *path = std::get_if<CorePathExpr>(&expr.node)) {
            return !path->has_local &&
                   path->root == ir::core::CorePathRoot::Input;
        }
        return false;
    }

    [[nodiscard]] bool value_names_input_inline_aggregate(CoreValueId value) const {
        std::vector<std::uint32_t> seen;
        return value_names_input_inline_aggregate(value, seen);
    }

    // V2-C provenance: true when `root` is itself a bridge result, or a path
    // projection whose root local (transitively) is one. A String PtrLen read
    // out of such an aggregate names a host-packed, disjoint result placement
    // that never aliases and outlives the goto graph, so it is all-run-stable
    // enough to persist into a context slot. ANF locals form a DAG, so the walk
    // terminates; `seen` guards defensively.
    [[nodiscard]] bool value_derives_from_bridge_result(
        CoreValueId root, std::vector<std::uint32_t> &seen) const {
        if (bridge_registry_ == nullptr) {
            return false;
        }
        if (std::find(seen.begin(), seen.end(), root.value) != seen.end()) {
            return false;
        }
        seen.push_back(root.value);
        if (std::any_of(bridge_result_values_.begin(), bridge_result_values_.end(),
                        [&](CoreValueId value) { return value == root; })) {
            return true;
        }
        if (root.value >= storage_.value_types.size()) {
            return false;
        }
        // A statement-produced value (capability call result) is matched by the
        // bridge-results check above; only a let-bound value has a defining
        // expr. A value with neither is not derived from a bridge result.
        const auto def = let_value_exprs_.find(root.value);
        if (def == let_value_exprs_.end()) {
            return false;
        }
        if (def->second >= storage_.exprs.size()) {
            return false;
        }
        const CoreExpr &expr = storage_.exprs[def->second];
        // ANF `let decision = %call_result` introduces a value-ref alias; follow
        // it so the provenance reaches the capability call's own result value.
        if (const auto *ref = std::get_if<CoreValueRefExpr>(&expr.node)) {
            return value_derives_from_bridge_result(ref->value, seen);
        }
        const auto *path = std::get_if<CorePathExpr>(&expr.node);
        if (path == nullptr || !path->has_local ||
            path->root != ir::core::CorePathRoot::Local) {
            return false;
        }
        return value_derives_from_bridge_result(path->local, seen);
    }

    [[nodiscard]] bool value_derives_from_bridge_result(CoreValueId root) const {
        std::vector<std::uint32_t> seen;
        return value_derives_from_bridge_result(root, seen);
    }

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
    // (the binding then reads fields through that address later). A String site
    // (V2-B) latches the TWO inline PtrLen words: from the two local slots for a
    // whole-String scrutinee, or by two i32 loads from the payload slot address.
    [[nodiscard]] bool emit_binding_latch(std::uint32_t scrutinee_local,
                                          std::uint32_t dest,
                                          const P6PatternSite &site,
                                          ir::SourceRangeOpt range) {
        if (site.kind == P6ScalarKind::String) {
            if (!site.in_memory) {
                emit_local_get(scrutinee_local);
                body_.byte(kOpLocalSet);
                body_.u32(dest);
                emit_local_get(scrutinee_local + 1u);
                body_.byte(kOpLocalSet);
                body_.u32(dest + 1u);
                return true;
            }
            // Inline PtrLen slot at scrutinee(+offset). The address walk is all
            // compile-time immediates, so recompute it for each of the two
            // words instead of borrowing a temp local.
            const auto load_slot_word = [&](std::uint32_t word_offset,
                                            std::uint32_t target) {
                emit_local_get(scrutinee_local);
                if (site.offset != 0) {
                    emit_const_i32(static_cast<std::int32_t>(site.offset));
                    body_.byte(kOpI32Add);
                }
                body_.byte(kOpI32Load);
                body_.u32(kAlignI32);
                body_.u32(word_offset);
                body_.byte(kOpLocalSet);
                body_.u32(target);
            };
            load_slot_word(0, dest);
            load_slot_word(4, dest + 1u);
            return true;
        }
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
        const auto kind = readable_kind(value);
        if (kind == P6ScalarKind::String) {
            // The two-word PtrLen (payload ptr, byte len), ptr first.
            emit_local_get(*local);
            emit_local_get(*local + 1u);
            return true;
        }
        emit_local_get(*local);
        return true;
    }

    // Pop the words `emit_value_read` pushed into one SSA value's own local
    // pair. A two-word value (Closure func_index/env_ptr, String ptr/len)
    // leaves TWO words; the second word (env_ptr / len) is the stack top and
    // lands in slot+1 first, then the first word lands in slot. Every other
    // kind leaves one.
    void emit_set_value_words(std::optional<P6ScalarKind> kind, std::uint32_t local) {
        if (kind.has_value() && is_two_word_kind(*kind)) {
            body_.byte(kOpLocalSet);
            body_.u32(local + 1u);
        }
        body_.byte(kOpLocalSet);
        body_.u32(local);
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
            // RFC 0026 P6-7 frame-bridge v2 D1 (rung V2-B): the eight-byte
            // inline (ptr,len) String word pair. It is NOT an aggregate address
            // (the words ARE the value, inline in its struct slot) and not a
            // scalar; only literal construction, frame-slot copy, ctx store and
            // the final materializer touch it.
            if (std::holds_alternative<ir::core::CoreLayoutPtrLen>(layout.shape)) {
                return P6ScalarKind::String;
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

    // Whether a P4-D layout edge is the inline eight-byte PtrLen String slot
    // (RFC 0026 P6-7 frame-bridge v2 D1). The ONE predicate every String-slot
    // path (constructor copy, ctx store, final materialization, enum payload
    // binding) consults.
    [[nodiscard]] bool edge_is_ptr_len(CoreLayoutId id) const {
        return id.value < layouts_.layouts.size() &&
               std::holds_alternative<ir::core::CoreLayoutPtrLen>(
                   layouts_.layouts[id.value].shape);
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
        if (edge.value >= layouts_.layouts.size()) {
            static_cast<void>(
                reject("variant payload sub-pattern slot is out of range", std::move(range)));
            return std::nullopt;
        }
        if (std::holds_alternative<ir::core::CoreLayoutPtrLen>(
                layouts_.layouts[edge.value].shape)) {
            // RFC 0026 P6-7 frame-bridge v2 D1 (rung V2-B): a String payload
            // slot is the inline two-word PtrLen; a binding latches both words
            // from the payload address.
            return P6PatternSite{
                P6ScalarKind::String, true, payload_base + payload.field_offsets[slot]};
        }
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
            return ProjectionRoot{false, 0, input_base()};
        case ir::core::CorePathRoot::Context:
            return ProjectionRoot{false, 0, context_base()};
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
        return ProjectionRoot{false, 0, context_base()};
    }

    [[nodiscard]] static bool is_two_word_kind(P6ScalarKind kind) {
        return kind == P6ScalarKind::Closure || kind == P6ScalarKind::String;
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
        // A Closure is a TWO-word (func_index, env_ptr) pair and a String is
        // the TWO-word (ptr,len) PtrLen pair; only one-word kinds reach the
        // monotone match-scratch pool in this slice, so pairs are allocated the
        // two consecutive slots they need.
        if (kind == P6ScalarKind::Closure) {
            return false;
        }
        if (kind == P6ScalarKind::String) {
            info.is_word_pair = true;
            info.slot = scratch_i32_count_++;
            ++scratch_i32_count_; // byte_len word
            return true;
        }
        info.slot = kind == P6ScalarKind::IntI64 ? scratch_i64_count_++ : scratch_i32_count_++;
        return true;
    }

    // Assign a pool slot to a let-bound value (one slot per CoreValueId; the
    // Core verifier proves flow-global single definition). A Closure value
    // occupies TWO consecutive i32 pool slots (func_index then env_ptr); a
    // String literal value occupies TWO consecutive slots (payload ptr then
    // byte_len).
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
            if (is_two_word_kind(kind)) {
                info.is_word_pair = true;
                ++i32_count_; // second word (env_ptr / byte_len)
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
        case P6ScalarKind::String:
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

    // RFC 0026 P6-7 frame-bridge v2 D3/D4 (rung V2-C): plan an ORDERED
    // capability BRIDGE statement inside a non-final frame computation region.
    // The module passes a control-block pointer and receives
    // (status, result_root_ptr); the host walks P4-D -> wire JSON, invokes the
    // capability, validates, and packs the result into this call site's
    // disjoint placement. Arity is the declared param_types.size() (multi-arg is
    // the whole point of D4); every argument/result must be a frame-walkable P6
    // value. The wire-shape subset (no map/f64/decimal/duration/timestamp/uuid)
    // is enforced AGAIN by the physical frame planner against the projected
    // wire schema; here only the physical representability is gated.
    [[nodiscard]] bool plan_bridge_call(const CoreCapabilityCallStmt &s,
                                        ir::SourceRangeOpt range) {
        if (fn_mode_ || bridge_registry_ == nullptr) {
            return reject("a direct capability invocation is legal only at an agent capability "
                          "final (or inside an outlined effect fn body)",
                          range);
        }
        if (s.capability.value >= program_.capabilities.size()) {
            return reject("bridge capability call references an out-of-range capability", range);
        }
        const CoreCapabilityDecl &capability = program_.capabilities[s.capability.value];
        if (s.args.size() != capability.param_types.size()) {
            return reject_with_code(
                core_wasm_diag::kInvalidCore,
                "bridge capability call arity disagrees with its declared parameter count", range);
        }
        std::vector<CoreValueTypeId> param_vt;
        param_vt.reserve(s.args.size());
        std::uint32_t spill_bytes = 0;
        for (std::uint32_t i = 0; i < s.args.size(); ++i) {
            const CoreValueId arg = s.args[i];
            used_values_[arg.value] = true;
            if (arg.value >= storage_.value_types.size()) {
                return reject("bridge capability argument id is out of range", range);
            }
            const CoreValueTypeId arg_type = storage_.value_types[arg.value];
            if (arg_type != capability.param_types[i]) {
                return reject_with_code(
                    core_wasm_diag::kInvalidCore,
                    "bridge capability argument type disagrees with the declared parameter type",
                    range);
            }
            const auto arg_kind = p6_scalar_kind(program_, layouts_, arg_type);
            if (arg_kind == std::nullopt || *arg_kind == P6ScalarKind::Closure ||
                *arg_kind == P6ScalarKind::Collection) {
                return reject_with_code(
                    core_wasm_diag::kUnsupportedCapabilityFrame,
                    "a bridge capability argument is not a frame-walkable P6 value in this rung "
                    "(a bounded collection crosses the frame bridge only with its own backing "
                    "placement on a later ladder; an unbounded Int, f64, bytes or closure is "
                    "rejected outright)",
                    range);
            }
            param_vt.push_back(arg_type);
            // Scalar / PtrLen arguments are spilled into the call site's
            // private 8-aligned slot in the control page frame; aggregate and
            // collection arguments stay at their existing stable address and
            // need no spill.
            if (*arg_kind == P6ScalarKind::Ptr || *arg_kind == P6ScalarKind::Collection) {
                continue;
            }
            spill_bytes += 8;
        }
        if (s.result.value >= storage_.value_types.size()) {
            return reject("bridge capability result id is out of range", range);
        }
        const CoreValueTypeId result_type = storage_.value_types[s.result.value];
        if (result_type != capability.return_type) {
            return reject_with_code(
                core_wasm_diag::kInvalidCore,
                "bridge capability result type disagrees with the declared return type", range);
        }
        const auto result_kind = p6_scalar_kind(program_, layouts_, result_type);
        if (result_kind == std::nullopt || *result_kind == P6ScalarKind::Closure ||
            *result_kind == P6ScalarKind::Collection) {
            return reject_with_code(
                core_wasm_diag::kUnsupportedCapabilityFrame,
                "a bridge capability result is not a frame-walkable P6 value in this rung (a "
                "bounded collection result needs its own backing placement on a later ladder; an "
                "unbounded Int, f64, bytes or closure is rejected outright)",
                range);
        }
        if (!bind_value(s.result, *result_kind)) {
            return reject("bridge capability result SSA value is bound more than once", range);
        }
        used_values_[s.result.value] = true;
        const std::uint32_t call_site_id =
            bridge_registry_->reserve(s.capability, std::move(param_vt), result_type,
                                      spill_bytes, bridge_state_);
        bridge_call_ids_.emplace(&s, call_site_id);
        bridge_result_values_.push_back(s.result);
        bridge_scratch_needed_ = true;
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
            // its two (func_index, env_ptr) words. A String is also two words
            // but is NOT part of the fn-boundary calling convention in any
            // rung (its by-address PtrLen marshaling arrives with the frame
            // bridge), so it crosses no boundary; f64/bytes stay non-P6.
            if (arg_kind == std::nullopt || *arg_kind == P6ScalarKind::String) {
                return reject("call argument is not a representable P6 boundary value "
                              "(String / f64 / multi-word types cannot cross an fn boundary)",
                              range);
            }
        }
        const auto result_kind = p6_scalar_kind(program_, layouts_, result_type);
        if (result_kind == std::nullopt || *result_kind == P6ScalarKind::Closure ||
            *result_kind == P6ScalarKind::String) {
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
    // A single RESULT word's type byte; nullopt for a closure / String
    // (two-word) or otherwise non-P6 result, which this slice cannot return.
    [[nodiscard]] static std::optional<std::uint8_t>
    boundary_result_byte(const std::optional<P6ScalarKind> &kind) {
        if (kind == std::nullopt || *kind == P6ScalarKind::Closure ||
            *kind == P6ScalarKind::String) {
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
        case CoreLiteralKind::String: {
            // RFC 0026 P6-7 frame-bridge v2 D1 (rung V2-B): a String literal is
            // the two-word PtrLen (rodata ptr, byte len), constructible ONLY on
            // the frame lane where the module owns a rodata Data section.
            if (rodata_pool_ == nullptr) {
                return reject_with_code(
                    core_wasm_diag::kUnsupportedCapabilityFrame,
                    "a String literal is constructible only inside a P6-7 frame-bridge v2 "
                    "computed final: this builder owns no in-module rodata region (the "
                    "E1-E3/FB lanes never build one, and V2-B does not construct String "
                    "literals in a non-final frame handler)",
                    expr.source_range);
            }
            if (*kind != P6ScalarKind::String) {
                return reject("string literal has a non-String result type", expr.source_range);
            }
            if (expr.result_type.value >= program_.value_types.size()) {
                return reject("string literal references an out-of-range result type",
                              expr.source_range);
            }
            const auto *string_type =
                std::get_if<CoreVtString>(&program_.value_types[expr.result_type.value].node);
            if (string_type == nullptr) {
                return reject("string literal result type is not a String", expr.source_range);
            }
            const std::optional<std::int64_t> upper_length =
                string_type->length_bounds.has_value()
                    ? std::optional{string_type->length_bounds->second}
                    : std::nullopt;
            std::string bytes =
                ahfl::support::decode_string_literal_bytes(lit.spelling);
            // Record the literal this builder contributed BEFORE moving the
            // bytes into the shared pool: the reachability compaction rebuilds
            // the module pool from the surviving (reachable) builders only, so
            // a dead handler can never pin rodata bytes a live byte never names.
            rodata_literals_.push_back(bytes);
            if (!rodata_pool_->intern(std::move(bytes), upper_length)) {
                if (upper_length.has_value()) {
                    return reject_with_code(
                        core_wasm_diag::kUnsupportedCapabilityFrame,
                        "a string literal is longer than its slot's declared String length "
                        "upper bound",
                        expr.source_range);
                }
                return reject_with_code(
                    core_wasm_diag::kResourceExhausted,
                    "the deduplicated String literal pool exceeds its reserved rodata region "
                    "of " + std::to_string(kP6RodataCapacity) +
                    " bytes in the fixed 64 KiB linear-memory page; shorten the literals",
                    expr.source_range);
            }
            return true;
        }
        case CoreLiteralKind::Decimal: {
            // V2-B: the compile-time i64 mantissa word; the host renders the
            // builtin `s<scale>:<mantissa>` spelling at encode. Literal only —
            // no Decimal arithmetic crosses the P6 lane.
            if (*kind != P6ScalarKind::IntI64) {
                return reject("decimal literal has a non-i64 scalar type", expr.source_range);
            }
            const auto parsed = ahfl::support::parse_decimal_literal(lit.spelling);
            if (!parsed.has_value()) {
                return reject("decimal literal spelling does not parse or its mantissa overflows "
                              "i64",
                              expr.source_range);
            }
            return true;
        }
        case CoreLiteralKind::Duration: {
            if (*kind != P6ScalarKind::IntI64) {
                return reject("duration literal has a non-i64 scalar type", expr.source_range);
            }
            const auto milliseconds =
                ahfl::support::parse_duration_literal_milliseconds(lit.spelling);
            if (!milliseconds.has_value()) {
                return reject("duration literal spelling does not parse or its milliseconds "
                              "overflow i64",
                              expr.source_range);
            }
            return true;
        }
        case CoreLiteralKind::Unit:
            // FB-1: a Unit literal is the zero-word value; it has no runtime
            // representation and is legal only where a Unit result is expected
            // (an fn with no return value — not yet in the slice). Reject here to
            // keep the single-word boundary, matching the multi-word gate.
            return reject("a Unit literal has no single-word wasm representation in this slice",
                          expr.source_range);
        case CoreLiteralKind::Float:
            return reject("float literals need the f64 opcode ladder, a later P6 slice",
                          expr.source_range);
        }
        return reject("literal kind is outside the P6 scalar subset", expr.source_range);
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
            // V2-C: a bare INPUT frame root in a frame handler is the whole
            // aggregate's fixed base address (1024) — a legal stable P4-D span
            // for a capability bridge argument. Only an aggregate root is
            // admitted (a scalar bare root is never a memory address), and the
            // raw-frame fact is latched at plan time so the physical frame
            // section is built.
            if (path.root == ir::core::CorePathRoot::Input &&
                bridge_registry_ != nullptr) {
                const auto root_vt =
                    p6_nominal_value_type(program_, path.root_type);
                if (root_vt.has_value()) {
                    const auto kind =
                        p6_scalar_kind(program_, layouts_, *root_vt);
                    if (kind == P6ScalarKind::Ptr) {
                        reads_raw_input_frame_ = true;
                        return true;
                    }
                }
            }
            return reject("a bare input/context root is not a memory read in the P6 subset", range);
        }
        // Resolve the final slot once; the edge and the intermediate
        // dereference chain decide both the V2-B PtrLen gate and the
        // computed-final input-inline provenance gate below.
        const auto leaf =
            resolve_projection_slot(path.projection, path.root_type, range);
        if (leaf == std::nullopt) {
            return false;
        }
        // V2-B: a leaf that lands on a String PtrLen slot is read as the two
        // inline words. A two-word String pair is realizable inside a computed
        // final materializer (the output-frame lane that carries rodata) or,
        // from V2-C, inside a non-final HANDLER that serves a capability
        // bridge (the pair is spilled into the call site's control-block slot
        // and walked by the host). A pure goto handler without either lane
        // still fails closed here rather than emitting an orphaned two-word
        // load.
        if (edge_is_ptr_len(leaf->edge)) {
            if (!final_return_mode_ && bridge_registry_ == nullptr) {
                return reject(
                    "a String (PtrLen) field is readable only inside a P6-7 frame-bridge v2 "
                    "computed final or a capability bridge handler; a non-final goto handler "
                    "without either lane cannot carry the two-word String pair (it constructs "
                    "no rodata, crosses no String boundary, and materializes no output frame)",
                    range);
            }
            ptrlen_read_needed_ = true;
        }
        // V2-A fix-forward: the projection address walk DEREFERENCES every
        // intermediate aggregate field (an i32.load chain) and reads an
        // aggregate LEAF as the child address its slot stores. That is the
        // MODULE runtime representation, but the host packs the INPUT frame
        // with the P4-D INLINE graph (every field in place, no child-address
        // slots). A projection off `input` therefore diverges from the frame as
        // soon as it crosses an aggregate edge (the load reads an inline value
        // word as an address) or lands on a struct/payload-enum leaf (the slot
        // words ARE the inline value, not a pointer).
        //
        // V2-D RETURN: on the workflow packaging lane the scheduler rewrites
        // the host-packed INLINE entry frame into module POINTER-TREE form
        // before the entry runner executes, so both shapes are dereferenceable
        // and the two rejections below are lifted for a relocated entry
        // handler. A top-level scalar / tag-enum / inline-String /
        // collection-header field needs no child dereference on every lane.
        //
        // The admission is FRAME-LEVEL (one builder per packaged runner) but
        // the rewrite is per-runner: only the scheduler's entry runner gets a
        // pointer-tree I_k. Every OTHER packaged runner receives an INLINE
        // scheduler-materialized frame, so on the relocated workflow lane the
        // gate applies to ALL its handlers — including a non-final
        // computed-goto preamble, whose intermediate aggregate dereference
        // would otherwise read inline words as a child address. The direct
        // agent lane (no relocation) keeps the historic final-only predicate.
        const bool workflow_inline_input_lane = workflow_packaging_lane_;
        if (!normalized_entry_frame_admitted_ &&
            (final_return_mode_ || workflow_inline_input_lane) &&
            !path.has_local && path.root == ir::core::CorePathRoot::Input) {
            if (workflow_packaging_lane_) {
                if (!leaf->deref_offsets.empty()) {
                    return reject(
                        "a workflow handler projects through a nested aggregate of an inline "
                        "scheduler-materialized INPUT frame; only the host-packed entry frame is "
                        "rewritten into pointer-tree form this rung, so a nested input projection "
                        "cannot be read on a non-entry node yet",
                        range);
                }
                if (place_is_aggregate_leaf(leaf->edge) &&
                    place_kind_of_layout(leaf->edge) == P6ScalarKind::Ptr) {
                    return reject(
                        "a workflow handler reads a struct/enum field directly off an inline "
                        "scheduler-materialized INPUT frame, whose aggregate bytes are expanded "
                        "in place rather than stored as a child address; only the host-packed "
                        "entry frame is pointer-tree normalized this rung (construct the "
                        "aggregate in-module instead)",
                        range);
                }
            } else {
                if (!leaf->deref_offsets.empty()) {
                    return reject(
                        "a computed final projects through a nested aggregate of the host-packed "
                        "INPUT frame; the inline-input-frame walk (frame-base + P4-D offset, no "
                        "child-address dereference) is a later frame-bridge rung, so a nested "
                        "input projection cannot be read on this lane yet",
                        range);
                }
                if (place_is_aggregate_leaf(leaf->edge) &&
                    place_kind_of_layout(leaf->edge) == P6ScalarKind::Ptr) {
                    return reject(
                        "a computed final reads a struct/enum field directly off the host-packed "
                        "INPUT frame, whose aggregate bytes are packed inline rather than stored "
                        "as a child address; materializing that value awaits the deferred "
                        "inline-input-frame expansion of the frame-bridge v2 (construct the "
                        "aggregate in-module on this rung instead)",
                        range);
                }
            }
        }
        // V2-C: latch the raw-frame fact at PLAN time too, so the frame section
        // (which must be built before handler emission for the bridge
        // coordinates) knows this handler projects the raw P4-D input frame.
        // The emit pass re-latches the identical fact from the load bytes.
        if (path.root == ir::core::CorePathRoot::Input) {
            reads_raw_input_frame_ = true;
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
        if (start + size > scratch_capacity() ||
            start + size > std::numeric_limits<std::uint32_t>::max()) {
            return reject("constructor scratch arena is exhausted", range);
        }
        construct_addrs_[id.value] = static_cast<std::uint32_t>(start);
        scratch_addr_cursor_ = static_cast<std::uint32_t>(start + size);
        return true;
    }

    // Validate ONE constructor operand against its P4-D SLOT edge: a scalar slot
    // takes an i32/i64 operand of the same width, an addressable-aggregate slot
    // takes a `Ptr` operand, a String PtrLen slot (V2-B) takes the two-word
    // String operand, and any other slot (bytes / f64 / an inline tag-only enum
    // the P6 model cannot address) fails closed.
    [[nodiscard]] bool
    plan_construct_operand(CoreValueId value, CoreLayoutId slot, ir::SourceRangeOpt range) {
        if (value.value >= storage_.value_types.size()) {
            return reject("constructor operand value id is out of range", range);
        }
        const auto kind = scalar_kind(storage_.value_types[value.value]);
        if (kind == std::nullopt) {
            return reject("constructor operand has a non-aggregate, non-scalar type", range);
        }
        if (slot.value < layouts_.layouts.size() &&
            std::holds_alternative<ir::core::CoreLayoutPtrLen>(
                layouts_.layouts[slot.value].shape)) {
            if (*kind != P6ScalarKind::String) {
                return reject("constructor operand for a String slot is not a String PtrLen value",
                              range);
            }
            return true;
        }
        if (place_is_aggregate_leaf(slot)) {
            if (*kind != P6ScalarKind::Ptr) {
                return reject("constructor operand is not an aggregate for its aggregate slot",
                              range);
            }
            // V2-A fix-forward: an aggregate field is stored as the child's
            // i32 ADDRESS in the module representation, but an operand sourced
            // from the host-packed INPUT frame names inline bytes with no such
            // address. Fail closed until the inline-input-frame expansion lands
            // (the materializer would otherwise store an inline value word as a
            // handle and later dereference it).
            if (final_return_mode_ && value_names_input_inline_aggregate(value)) {
                return reject(
                    "a computed final stores an aggregate/enum field sourced from the host-packed "
                    "INPUT frame; input aggregates are packed inline and carry no module child "
                    "address, so the inline-input-frame expansion (a later frame-bridge rung) "
                    "must land before such a field can be copied into a constructed output",
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

    // V2-B: true iff an expression is a Decimal/Duration literal (its i64 word
    // is a compile-time constant, never an integer-arithmetic operand).
    [[nodiscard]] bool is_decimal_duration_expr(const CoreExpr &expr) const {
        const auto *literal = std::get_if<CoreLiteralExpr>(&expr.node);
        return literal != nullptr && (literal->kind == CoreLiteralKind::Decimal ||
                                      literal->kind == CoreLiteralKind::Duration);
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
                *key_kind == P6ScalarKind::Collection || *key_kind == P6ScalarKind::Closure ||
                *key_kind == P6ScalarKind::String) {
                return reject("keyed scan key is not a scalar Int/Bool/enum value", range);
            }
            if (!same_word_width(*key_kind, place_kind_of_layout(container->element))) {
                return reject("keyed scan key width does not match the key slot layout", range);
            }
            if (collection.op == CoreCollectionOpKind::KeyGet) {
                const auto value_kind_opt = scalar_kind(storage_.exprs[id.value].result_type);
                if (value_kind_opt == std::nullopt || *value_kind_opt == P6ScalarKind::Ptr ||
                    *value_kind_opt == P6ScalarKind::Collection ||
                    *value_kind_opt == P6ScalarKind::String) {
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
        if (start + size > scratch_capacity() ||
            start + size > std::numeric_limits<std::uint32_t>::max()) {
            return reject("constructor scratch arena is exhausted", range);
        }
        construct_addrs_[id.value] = static_cast<std::uint32_t>(start);
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
        // A String PtrLen scrutinee would need byte-wise literal comparison;
        // String values on the frame lane move only as enum-payload bindings
        // and constructor operands, never as a compared scrutinee.
        if (*scrutinee_kind == P6ScalarKind::String) {
            return reject("matching directly on a String value is outside the P6 frame subset "
                          "(bind a String enum payload instead)",
                          range);
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
                        // V2-C: on the frame-bridge lane an unrepresentable let
                        // value is a capability-FRAME rejection (the expression
                        // cannot name a bridge argument/result), not the generic
                        // P6-scalar scaffold classification.
                        if (bridge_registry_ != nullptr) {
                            return reject_with_code(
                                core_wasm_diag::kUnsupportedCapabilityFrame,
                                "a frame-bridge let value is not a frame-walkable P6 value "
                                "(f64, bytes or a zero-sized aggregate cannot cross the bridge)",
                                statement.source_range);
                        }
                        return reject("let value has a non-scalar or f64 type",
                                      statement.source_range);
                    }
                    if (!bind_value(s.result, *kind)) {
                        return reject("SSA value is bound more than once", statement.source_range);
                    }
                    let_value_exprs_[s.result.value] = s.expr.value;
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
                    // V2-C: a non-final frame-lane HANDLER statement is a
                    // capability bridge; an outlined fn body keeps the opaque
                    // tuple call (and a final handler never reaches this
                    // dispatch).
                    if (!fn_mode_ && bridge_registry_ != nullptr) {
                        return plan_bridge_call(s, statement.source_range);
                    }
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
                        const auto return_kind =
                            s.value.value < storage_.value_types.size()
                                ? p6_scalar_kind(program_, layouts_,
                                                 storage_.value_types[s.value.value])
                                : std::nullopt;
                        // A String PtrLen is two words and never crosses the
                        // single-word fn calling convention (any rung).
                        if (return_kind == std::nullopt ||
                            *return_kind == P6ScalarKind::String) {
                            return reject("fn return value is not a single-word P6 value",
                                          statement.source_range);
                        }
                        return true;
                    }
                    if (final_return_mode_) {
                        return plan_final_return(s, statement.source_range);
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
        if (lit.kind == CoreLiteralKind::String) {
            // RFC 0026 P6-7 frame-bridge v2 D1 (rung V2-B): leave the PtrLen
            // pair (rodata_ptr:i32, byte_len:i32) on the operand stack. The
            // planner interned the bytes into the frozen pool, so both words
            // are compile-time immediates.
            if (kind != P6ScalarKind::String) {
                return reject("string literal has a non-String type", std::move(range));
            }
            if (rodata_pool_ == nullptr) {
                return reject("string literal reached emit without a rodata pool",
                              std::move(range));
            }
            const std::string bytes =
                ahfl::support::decode_string_literal_bytes(lit.spelling);
            const auto offset = rodata_pool_->offset_of(bytes);
            if (!offset.has_value()) {
                return reject("string literal was not interned into the rodata pool",
                              std::move(range));
            }
            emit_const_i32(static_cast<std::int32_t>(rodata_base() + *offset));
            emit_const_i32(static_cast<std::int32_t>(bytes.size()));
            return true;
        }
        if (lit.kind == CoreLiteralKind::Decimal) {
            // V2-B: the i64 mantissa word the compile-time parser derived.
            if (kind != P6ScalarKind::IntI64) {
                return reject("decimal literal has a non-i64 type", std::move(range));
            }
            const auto parsed = ahfl::support::parse_decimal_literal(lit.spelling);
            if (!parsed.has_value()) {
                return reject("decimal literal spelling does not parse or its mantissa overflows "
                              "i64",
                              std::move(range));
            }
            emit_const_i64(parsed->units);
            return true;
        }
        if (lit.kind == CoreLiteralKind::Duration) {
            if (kind != P6ScalarKind::IntI64) {
                return reject("duration literal has a non-i64 type", std::move(range));
            }
            const auto milliseconds =
                ahfl::support::parse_duration_literal_milliseconds(lit.spelling);
            if (!milliseconds.has_value()) {
                return reject("duration literal spelling does not parse or its milliseconds "
                              "overflow i64",
                              std::move(range));
            }
            emit_const_i64(*milliseconds);
            return true;
        }
        if (lit.kind != CoreLiteralKind::Integer ||
            (kind != P6ScalarKind::IntI32 && kind != P6ScalarKind::IntI64)) {
            return reject("literal kind is outside the P6 scalar subset",
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

    // RFC 0026 P6-7 frame-bridge v2 D3/D4 (rung V2-C): emit one ORDERED
    // capability bridge statement in a non-final handler. The module (1) writes
    // the fixed control block `{call_site_id, arg_count, (ptr,len)[*]}` at its
    // dense block address, spilling scalar / PtrLen arguments into the call
    // site's private spill slots; (2) calls the additive
    // `kTypeCapabilityBridge = (i32)->(i32,i32)` import; (3) traps on any
    // non-OK status (single-run: no pending arm); (4) binds the result SSA from
    // the returned result root — a load for inline scalars / PtrLen, the root
    // address itself for an aggregate / collection.
    [[nodiscard]] bool emit_bridge_call(const CoreCapabilityCallStmt &s,
                                        ir::SourceRangeOpt range) {
        if (imports_ == nullptr || bridge_registry_ == nullptr) {
            return reject("bridge capability call reached emit without the import table / frame "
                          "plan",
                          range);
        }
        const auto id_it = bridge_call_ids_.find(&s);
        if (id_it == bridge_call_ids_.end()) {
            return reject("bridge capability call has no planned call-site id", range);
        }
        // V2-C fix-forward: never index the dense site table without proving
        // the remapped id is in range. The plan-time reachability compaction
        // renumbers retained statements and rejects when a retained id is
        // absent, so a hit here is defense in depth, not a normal path.
        if (id_it->second >= bridge_registry_->size()) {
            return reject("bridge capability call names a dense call site outside the planned "
                          "call-site table",
                          range);
        }
        const BridgeCallPlan &site = bridge_registry_->sites()[id_it->second];
        const auto import_it = std::find(imports_->begin(), imports_->end(), s.capability);
        if (import_it == imports_->end()) {
            return reject("bridge capability was not planned into the import table", range);
        }
        const std::uint32_t import_ordinal =
            static_cast<std::uint32_t>(std::distance(imports_->begin(), import_it));
        const std::uint32_t block_ptr =
            bridge_registry_->control_base() + site.block_offset;
        std::uint32_t spill_cursor = site.spill_base;

        // Write the 8-byte block header (call_site_id, arg_count).
        const auto store_i32_word = [&](std::uint32_t address, std::int32_t value) {
            emit_const_i32(static_cast<std::int32_t>(address));
            emit_const_i32(value);
            body_.byte(kOpI32Store);
            body_.u32(kAlignI32);
            body_.u32(0);
        };
        // Store one SSA i32 word (pushed by the caller) into `address`.
        const auto store_i32_stack_word = [&](std::uint32_t address) {
            emit_const_i32(static_cast<std::int32_t>(address));
            body_.byte(kOpI32Store);
            body_.u32(kAlignI32);
            body_.u32(0);
        };
        store_i32_word(block_ptr, static_cast<std::int32_t>(site.call_site_id));
        store_i32_word(block_ptr + 4u, static_cast<std::int32_t>(s.args.size()));

        // Write the (ptr,len) argument descriptors and perform any scalar /
        // PtrLen spill. Aggregate / collection arguments stay at their existing
        // stable address, so only their address is named.
        for (std::uint32_t i = 0; i < s.args.size(); ++i) {
            const CoreValueId arg = s.args[i];
            const auto kind =
                p6_scalar_kind(program_, layouts_, storage_.value_types[arg.value]);
            if (kind == std::nullopt) {
                return reject("bridge capability argument has no representable P6 kind", range);
            }
            const std::uint32_t desc_addr = block_ptr + 8u + 8u * i;
            if (*kind == P6ScalarKind::Ptr || *kind == P6ScalarKind::Collection) {
                const ir::core::CoreLayout *layout = p6_value_layout(
                    program_, layouts_, storage_.value_types[arg.value]);
                if (layout == nullptr) {
                    return reject("bridge aggregate argument has no finalized P4-D layout", range);
                }
                // The value word IS its root address; the span length is the
                // layout root size (8 for a collection header).
                if (!emit_value_read(arg, range)) {
                    return false;
                }
                store_i32_stack_word(desc_addr);
                store_i32_word(desc_addr + 4u,
                               static_cast<std::int32_t>(layout->size));
                continue;
            }
            // Scalar / PtrLen: spill the physical word(s) into the private
            // slot, then name (spill_address, physical_width).
            const std::uint32_t spill_addr = spill_cursor;
            if (*kind == P6ScalarKind::String) {
                // A String value binds TWO adjacent i32 locals (payload ptr,
                // byte len). Copy both into the spill slot: [addr,value] stack
                // order, len at +4 and ptr at +0. The payload itself is never
                // copied — the spilled PtrLen keeps naming its original
                // all-run-stable region (input arena / rodata / another bridge
                // result placement), which the host re-authorizes.
                const auto string_local = readable_local(arg);
                if (string_local == std::nullopt) {
                    return reject("bridge String argument has no bound PtrLen local", range);
                }
                emit_const_i32(static_cast<std::int32_t>(spill_addr + 4u));
                body_.byte(kOpLocalGet);
                body_.u32(*string_local + 1u);
                body_.byte(kOpI32Store);
                body_.u32(kAlignI32);
                body_.u32(0);
                emit_const_i32(static_cast<std::int32_t>(spill_addr));
                body_.byte(kOpLocalGet);
                body_.u32(*string_local);
                body_.byte(kOpI32Store);
                body_.u32(kAlignI32);
                body_.u32(0);
            } else if (*kind == P6ScalarKind::IntI64) {
                emit_const_i32(static_cast<std::int32_t>(spill_addr));
                if (!emit_value_read(arg, range)) {
                    return false;
                }
                body_.byte(kOpI64Store);
                body_.u32(kAlignI64);
                body_.u32(0);
            } else {
                emit_const_i32(static_cast<std::int32_t>(spill_addr));
                if (!emit_value_read(arg, range)) {
                    return false;
                }
                body_.byte(kOpI32Store);
                body_.u32(kAlignI32);
                body_.u32(0);
            }
            store_i32_word(desc_addr, static_cast<std::int32_t>(spill_addr));
            store_i32_word(desc_addr + 4u,
                           *kind == P6ScalarKind::IntI64 ? 8 : 8);
            spill_cursor += 8;
        }

        // call ahfl_cap[ordinal] with the single block pointer.
        emit_const_i32(static_cast<std::int32_t>(block_ptr));
        body_.byte(kOpCall);
        body_.u32(import_ordinal);
        // Pop (status, result_root_ptr).
        body_.byte(kOpLocalSet);
        body_.u32(bridge_ptr_local_);
        body_.byte(kOpLocalSet);
        body_.u32(bridge_status_local_);
        // Single-run contract: any non-OK status traps (durable replay / pending
        // arm stays the D2b authority).
        body_.byte(kOpLocalGet);
        body_.u32(bridge_status_local_);
        body_.byte(kOpIf);
        body_.byte(kEmptyBlock);
        body_.byte(kOpUnreachable);
        body_.byte(kOpEnd);

        // Bind the result SSA. An aggregate / collection result word is the
        // packed root address itself; an inline scalar / PtrLen is loaded from
        // the root.
        const auto result_kind =
            p6_scalar_kind(program_, layouts_, storage_.value_types[s.result.value]);
        if (result_kind == std::nullopt) {
            return reject("bridge capability result has no representable P6 kind", range);
        }
        const auto local = final_local(s.result);
        if (local == std::nullopt) {
            return reject("bridge capability result has no SSA local", range);
        }
        if (*result_kind == P6ScalarKind::Ptr || *result_kind == P6ScalarKind::Collection) {
            body_.byte(kOpLocalGet);
            body_.u32(bridge_ptr_local_);
            body_.byte(kOpLocalSet);
            body_.u32(*local);
            return true;
        }
        if (*result_kind == P6ScalarKind::String) {
            body_.byte(kOpLocalGet);
            body_.u32(bridge_ptr_local_);
            body_.byte(kOpI32Load);
            body_.u32(kAlignI32);
            body_.u32(0);
            body_.byte(kOpLocalSet);
            body_.u32(*local);
            body_.byte(kOpLocalGet);
            body_.u32(bridge_ptr_local_);
            body_.byte(kOpI32Load);
            body_.u32(kAlignI32);
            body_.u32(4);
            body_.byte(kOpLocalSet);
            body_.u32(*local + 1u);
            return true;
        }
        body_.byte(kOpLocalGet);
        body_.u32(bridge_ptr_local_);
        if (*result_kind == P6ScalarKind::IntI64) {
            body_.byte(kOpI64Load);
            body_.u32(kAlignI64);
            body_.u32(0);
        } else {
            body_.byte(kOpI32Load);
            body_.u32(kAlignI32);
            body_.u32(0);
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
                // V2-C: a bare INPUT aggregate root in a frame handler is the
                // fixed input-frame base address (a legal bridge argument
                // span). A context root or a scalar root stays fail-closed.
                if (path.root == ir::core::CorePathRoot::Input &&
                    bridge_registry_ != nullptr) {
                    const auto root_vt =
                        p6_nominal_value_type(program_, path.root_type);
                    if (root_vt.has_value() &&
                        p6_scalar_kind(program_, layouts_, *root_vt) ==
                            P6ScalarKind::Ptr) {
                        reads_raw_input_frame_ = true;
                        emit_const_i32(
                            static_cast<std::int32_t>(input_base()));
                        return true;
                    }
                }
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
        // A projected LEAF must be a single-word P6 value (a scalar, a tag-only
        // enum discriminant, an addressable aggregate, a bounded collection) OR
        // the two-word String PtrLen pair (V2-B). Bytes / f64 leaves fail closed.
        const bool slot_ptr_len = edge_is_ptr_len(slot->edge);
        if (!place_is_p6_value(slot->edge) && !slot_ptr_len) {
            return reject("projection leaf is not a representable P6 frame value",
                          std::move(range));
        }
        if (slot_ptr_len) {
            // V2-B: the two inline PtrLen words. The walked slot address is
            // teed into the reserved read temp so both loads address the same
            // slot; the stack ends with (payload_ptr, byte_len).
            body_.byte(kOpLocalTee);
            body_.u32(ptrlen_read_addr_local_);
            body_.byte(kOpI32Load);
            body_.u32(kAlignI32);
            body_.u32(slot->offset);
            emit_local_get(ptrlen_read_addr_local_);
            body_.byte(kOpI32Load);
            body_.u32(kAlignI32);
            body_.u32(slot->offset + 4u);
            return true;
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
            dynamic ? 0u : scratch_base() + *construct_addrs_[id.value];
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
        global_get(handler_heap_next_global());
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
        global_set(handler_heap_next_global());
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
            dynamic ? 0u : scratch_base() + *construct_addrs_[id.value];
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
        // RFC 0026 P6-7 frame-bridge v2 D1 (rung V2-B): a String slot holds the
        // PtrLen INLINE (8 bytes at the slot offset). Copy both words straight
        // from the operand's two i32 locals (a literal's two immediates, a
        // projection's, or a match binding's). The payload pointer names a
        // stable region (rodata / input arena / a bridge result), so no
        // lifetime rule is involved.
        if (slot_layout.value < layouts_.layouts.size() &&
            std::holds_alternative<ir::core::CoreLayoutPtrLen>(
                layouts_.layouts[slot_layout.value].shape)) {
            if (*kind != P6ScalarKind::String) {
                return reject("constructor String slot operand is not a String",
                              std::move(range));
            }
            const auto store_word = [&](std::uint32_t word_offset, std::uint32_t src) {
                if (dynamic_address) {
                    emit_local_get(alloc_temp_local_);
                } else {
                    emit_const_i32(static_cast<std::int32_t>(static_address));
                }
                emit_local_get(src);
                body_.byte(kOpI32Store);
                body_.u32(kAlignI32);
                body_.u32(static_cast<std::uint32_t>(offset + word_offset));
            };
            store_word(0, *local);
            store_word(4, *local + 1u);
            return true;
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
        // handler yet, so this fails closed exactly like the bytes / f64 leaf
        // rather than emit a dangling pointer.
        const auto slot =
            resolve_projection_slot(store.place.projection, store.place.root_type, range);
        if (slot == std::nullopt) {
            return false;
        }
        // RFC 0026 P6-7 frame-bridge v2 §2.3 (rung V2-B): a PtrLen-valued ctx
        // store IS admitted, but only when the payload pointer names an
        // all-run-stable region (rodata / input frame arena / a bridge result).
        // Of those, V2-B can prove only rodata — a String LITERAL — so a String
        // value produced any other way stays fail-closed here until later rungs
        // carry its provenance. An aggregate slot keeps its dangling-address
        // rejection below.
        if (edge_is_ptr_len(slot->edge)) {
            if (readable_kind(store.value) != P6ScalarKind::String) {
                return reject("a context String slot requires a String PtrLen value", range);
            }
            if (store.value.value >= storage_.value_types.size()) {
                return reject("a context String store references an out-of-range value", range);
            }
            // The value space and expr space are numbered separately: resolve a
            // let-bound value to the expr that defines it before testing for a
            // rodata literal.
            bool is_rodata_literal = false;
            if (const auto def = let_value_exprs_.find(store.value.value);
                def != let_value_exprs_.end() && def->second < storage_.exprs.size()) {
                const CoreExpr &source = storage_.exprs[def->second];
                const auto *literal = std::get_if<CoreLiteralExpr>(&source.node);
                is_rodata_literal =
                    literal != nullptr && literal->kind == CoreLiteralKind::String;
            }
            // V2-C: a String projected out of a CAPABILITY BRIDGE result is
            // also all-run-stable: the host packed it into that call site's
            // disjoint result placement, which never aliases and outlives the
            // goto graph. Prove provenance by following the store value's path
            // chain to the bound result of a planned bridge call in THIS
            // builder.
            if (!is_rodata_literal && !value_derives_from_bridge_result(store.value)) {
                return reject_with_code(
                    core_wasm_diag::kUnsupportedCapabilityFrame,
                    "a context String store requires an all-run-stable payload: a rodata "
                    "String literal or a String projected from a capability bridge result "
                    "(a computed or input-borrowed String ctx store without that provenance "
                    "arrives with a later frame-bridge rung)",
                    range);
            }
            used_values_[store.value.value] = true;
            ptrlen_ctx_store_needed_ = true;
            return true;
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
        // A store leaf must be a single-word P6 value (a scalar or an
        // addressable aggregate) OR the eight-byte inline String PtrLen pair
        // (V2-B). A bytes / f64 leaf still fails closed.
        const bool slot_ptr_len = edge_is_ptr_len(slot->edge);
        if (!place_is_p6_value(slot->edge) && !slot_ptr_len) {
            return reject("store destination is not a P6 frame value", std::move(range));
        }
        const auto place_kind = place_kind_of_layout(slot->edge);
        const auto local = readable_local(store.value);
        if (local == std::nullopt) {
            return reject("store value is not a readable value", std::move(range));
        }
        if (slot_ptr_len) {
            // The projection walk left exactly ONE destination address on the
            // stack; tee it into the reserved temp so the two PtrLen stores can
            // each address the slot (the second i32 word sits at offset + 4).
            if (readable_kind(store.value) != P6ScalarKind::String) {
                return reject("store value kind does not match its String destination slot",
                              std::move(range));
            }
            body_.byte(kOpLocalTee);
            body_.u32(ctx_store_addr_local_);
            const auto store_word = [&](std::uint32_t word_offset, std::uint32_t src) {
                emit_local_get(ctx_store_addr_local_);
                emit_local_get(src);
                body_.byte(kOpI32Store);
                body_.u32(kAlignI32);
                body_.u32(slot->offset + word_offset);
            };
            store_word(0, *local);
            store_word(4, *local + 1u);
            return true;
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
        // RFC 0026 P6-7 frame-bridge v2 rung V2-D (emission half 2): scalar
        // ctx-store kind agreement. A tag-only-enum (Index) destination is a
        // single inline i32 discriminant word, so an Index VALUE stores with
        // the width-exact i32.store the P6-4 rule reserved for Int slots; that
        // rule banned the slot categorically only because no readable value
        // could produce an Index word yet, and a projected capability-result
        // enum field now can. A Bool VALUE shares one i32 word with an I32
        // SLOT edge (slot edges never carry their own Bool kind), exactly the
        // `same_word_width` agreement the collection gates use. The predicate
        // stays type-strict: an IntI32/Bool word never lands in an Index slot
        // and an Index discriminant never lands in an Int/Bool slot, even
        // though all three are physically i32.
        if (const auto value_kind = readable_kind(store.value);
            value_kind == std::nullopt || !same_word_width(*value_kind, place_kind)) {
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

    // --- RFC 0026 P6-7 frame-bridge v2 rung V2-A: computed-final materialization
    //
    // A final handler ends in `return v`; the planner validates v against the
    // agent output nominal and the emitter materializes it into the fixed output
    // frame (kP6AggregateOutputBase) before yielding the final state id:
    //
    //   * a scalar root (Bool / i32 / i64 / tag-only enum) -> one width-exact
    //     store at the frame base;
    //   * an aggregate root (struct / payload-bearing enum) -> the whole output
    //     region is zeroed first (padding words are provably zero), then every
    //     inline slot is copied from the scratch/frame aggregate the value names;
    //   * a bounded-collection root -> its 8-byte inline (ptr,len) header is
    //     copied; the backing elements stay in the stable input-backed placement.
    //
    // V2-B: a String (PtrLen) slot is copied as its two inline words (the
    // payload pointer stays a rodata / input-arena pointer — the payload is
    // never copied). Bytes16 (Uuid), f64, closures, and uninhabited slots still
    // fail closed. Decimal/Duration i64 literal words pass the scalar gate but
    // have no canonical differential observation (the host spelling render is a
    // later rung).

    // Validate one P4-D layout edge is entirely in the v2 output-frame walk
    // subset (scalars i32/i64, inline structs/enums, bounded containers, and the
    // String PtrLen pair).
    [[nodiscard]] bool
    validate_final_layout_shape(CoreLayoutId id, ir::SourceRangeOpt range) {
        if (id.value >= layouts_.layouts.size()) {
            return reject("computed final references an out-of-range P4-D layout", range);
        }
        const ir::core::CoreLayout &layout = layouts_.layouts[id.value];
        const bool ok = std::visit(
            Overloaded{
                [](const ir::core::CoreLayoutPending &) { return false; },
                [](const ir::core::CoreLayoutScalar &s) {
                    return s.repr != ir::core::CoreScalarRepr::F64;
                },
                [](const ir::core::CoreLayoutBytes &) { return false; },
                // V2-B: the String PtrLen inline pair.
                [](const ir::core::CoreLayoutPtrLen &) { return true; },
                [](const ir::core::CoreLayoutFnRef &) { return false; },
                [](const ir::core::CoreLayoutClosure &) { return false; },
                [&](const ir::core::CoreLayoutStruct &s) {
                    return std::ranges::all_of(s.field_layouts, [&](CoreLayoutId field) {
                        return validate_final_layout_shape(field, range);
                    });
                },
                [&](const ir::core::CoreLayoutEnum &e) {
                    return std::ranges::all_of(e.variant_payload_layouts,
                                              [&](CoreLayoutId payload) {
                                                  return validate_final_layout_shape(
                                                      payload, range);
                                              });
                },
                [&](const ir::core::CoreLayoutContainer &c) {
                    if (!validate_final_layout_shape(c.element, range)) {
                        return false;
                    }
                    return !c.value.has_value() ||
                           validate_final_layout_shape(*c.value, range);
                },
                [](const ir::core::CoreLayoutUninhabited &) { return false; },
            },
            layout.shape);
        if (!ok) {
            return reject(
                "computed final carries a shape the P6-7 output frame cannot represent "
                "(only Bool/Int scalars, tag enums, inline structs/enums, bounded "
                "collections, and String PtrLen slots; f64/Uuid/closure slots stay fail-closed)",
                range);
        }
        return true;
    }

    // Plan a computed-final return: the value must match the agent output
    // nominal and every physical word it materializes must be in the v1 subset.
    // Reserves the one source-address scratch local for an aggregate/collection
    // root.
    [[nodiscard]] bool plan_final_return(const CoreReturnStmt &s, ir::SourceRangeOpt range) {
        if (!s.has_value) {
            return reject("a computed final must return a value", range);
        }
        if (s.value.value >= storage_.value_types.size()) {
            return reject("final return value id is out of range for this flow", range);
        }
        const CoreValueTypeId value_type = storage_.value_types[s.value.value];
        if (value_type != final_return_type_) {
            return reject(
                "final return value type does not match the agent output nominal",
                range);
        }
        used_values_[s.value.value] = true;
        const auto kind = p6_scalar_kind(program_, layouts_, value_type);
        if (kind == std::nullopt) {
            return reject(
                "computed final value is not a single-word P6 value on the frame lane",
                range);
        }
        switch (*kind) {
        case P6ScalarKind::Bool:
        case P6ScalarKind::IntI32:
        case P6ScalarKind::IntI64:
        case P6ScalarKind::Index:
            return true;
        case P6ScalarKind::Closure:
        case P6ScalarKind::String:
            // A closure never crosses; a bare-String ROOT output is also not
            // materialized by V2-B (String rides inline inside an aggregate
            // output — the five V2-E cases all return structs).
            return reject("a closure or bare String value cannot be the computed-final frame root",
                          range);
        case P6ScalarKind::Collection:
            final_src_count_ = std::max(final_src_count_, 1u);
            return true;
        case P6ScalarKind::Ptr: {
            // Boundary of the materializer: an aggregate root that names the
            // host-packed INLINE input frame has no module pointer tree to
            // expand (the birth-site gate in plan_path normally catches this
            // earlier; this keeps the boundary fail-closed on its own).
            if (value_names_input_inline_aggregate(s.value)) {
                return reject(
                    "a computed final cannot materialize an aggregate rooted in the host-packed "
                    "INPUT frame: its fields are packed inline with no module child-address "
                    "tree; the inline-input-frame expansion is a later frame-bridge rung",
                    range);
            }
            if (value_type.value >= layouts_.value_layouts.size()) {
                return reject("computed final aggregate has no finalized layout", range);
            }
            const CoreLayoutId root = layouts_.value_layouts[value_type.value];
            if (!validate_final_layout_shape(root, range)) {
                return false;
            }
            // One address-stack local for the root, plus one per nested
            // aggregate-field dereference.
            final_src_count_ =
                std::max(final_src_count_, final_copy_depth(root) + 1u);
            return true;
        }
        }
        return reject("computed final value has an unclassified P6 kind", range);
    }

    // Emit one width-exact word copy: [dst_base + dst_off] := *(src_local +
    // src_off). All offsets are compile-time constants of a frame whose size is
    // page-gated.
    void emit_copy_word(bool wide,
                        std::uint32_t src_local,
                        std::uint64_t src_off,
                        std::uint32_t dst_base,
                        std::uint64_t dst_off) {
        emit_const_i32(static_cast<std::int32_t>(dst_base + dst_off));
        emit_local_get(src_local);
        if (src_off != 0) {
            emit_const_i32(static_cast<std::int32_t>(src_off));
            body_.byte(kOpI32Add);
        }
        body_.byte(wide ? kOpI64Load : kOpI32Load);
        body_.u32(wide ? kAlignI64 : kAlignI32);
        body_.u32(0);
        body_.byte(wide ? kOpI64Store : kOpI32Store);
        body_.u32(wide ? kAlignI64 : kAlignI32);
        body_.u32(0);
    }

    // Copy every named INLINE slot of one struct (or an enum variant's payload
    // struct) from the aggregate named by scratch-local `level` into the output
    // frame at `dst_base + dst_off`.
    //
    // The module's runtime representation stores an aggregate-typed FIELD as the
    // child's ADDRESS (the ONE-representation rule `emit_construct_store` and
    // `emit_projection_slot` obey): scalar children live inline at
    // field_offsets, a struct/enum child is reached by dereferencing the i32
    // word stored at the field slot, and a bounded-collection field names its
    // inline (ptr,len) header through the same one-word handle. The P4-D frame
    // layout INLINES those children instead, so the materializer EXPANDS the
    // pointer tree into the inline frame: a nested child is copied from the
    // address its parent slot holds into the frame at the child's inline offset,
    // using one scratch address local per nesting depth. Padding is never touched
    // (the caller zeroed the whole region first).
    // Whether a layout edge is copied as one inline word (a scalar, or a
    // TAG-ONLY enum whose whole representation is the i32 discriminant), vs an
    // aggregate child reached through the i32 address its parent field stores.
    [[nodiscard]] bool final_leaf_is_word(const ir::core::CoreLayout &layout) const {
        if (const auto *scalar = std::get_if<ir::core::CoreLayoutScalar>(&layout.shape)) {
            return scalar->repr != ir::core::CoreScalarRepr::F64;
        }
        if (const auto *tagged = std::get_if<ir::core::CoreLayoutEnum>(&layout.shape)) {
            return std::ranges::all_of(tagged->variant_payload_sizes,
                                      [](std::uint64_t size) { return size == 0; });
        }
        return false;
    }

    [[nodiscard]] bool emit_copy_struct_fields(const ir::core::CoreLayoutStruct &structure,
                                              std::uint32_t level,
                                              std::uint64_t src_off,
                                              std::uint32_t dst_base,
                                              std::uint64_t dst_off,
                                              ir::SourceRangeOpt range) {
        for (std::uint32_t i = 0; i < structure.field_layouts.size(); ++i) {
            const CoreLayoutId edge = structure.field_layouts[i];
            const std::uint64_t off = structure.field_offsets[i];
            if (edge.value >= layouts_.layouts.size()) {
                return reject("computed final copy walks an out-of-range field layout", range);
            }
            const ir::core::CoreLayout &field = layouts_.layouts[edge.value];
            if (final_leaf_is_word(field)) {
                const bool wide =
                    std::holds_alternative<ir::core::CoreLayoutScalar>(field.shape) &&
                    std::get_if<ir::core::CoreLayoutScalar>(&field.shape)->repr ==
                        ir::core::CoreScalarRepr::I64;
                emit_copy_word(wide, final_src_local_ + level, src_off + off,
                              dst_base, dst_off + off);
                continue;
            }
            if (std::holds_alternative<ir::core::CoreLayoutPtrLen>(field.shape)) {
                // V2-B: copy the String PtrLen's two inline words from the
                // source aggregate slot straight into the output frame slot.
                // The payload pointer is preserved verbatim (rodata / input
                // arena); the payload bytes themselves are never copied.
                emit_copy_word(false, final_src_local_ + level, src_off + off,
                              dst_base, dst_off + off);
                emit_copy_word(false, final_src_local_ + level, src_off + off + 4u,
                              dst_base, dst_off + off + 4u);
                continue;
            }
            if (std::holds_alternative<ir::core::CoreLayoutBytes>(field.shape) ||
                std::holds_alternative<ir::core::CoreLayoutClosure>(field.shape)) {
                return reject("computed final copies a Uuid/closure field", range);
            }
            if (std::holds_alternative<ir::core::CoreLayoutStruct>(field.shape) ||
                std::holds_alternative<ir::core::CoreLayoutContainer>(field.shape) ||
                std::holds_alternative<ir::core::CoreLayoutEnum>(field.shape)) {
                if (level + 1u >= final_src_count_) {
                    return reject("computed final aggregate nesting exceeded its scratch stack",
                                  range);
                }
                // Latch the child handle from the parent's field slot into the
                // next address-stack local, then expand that child inline.
                emit_local_get(final_src_local_ + level);
                if (src_off + off != 0) {
                    emit_const_i32(static_cast<std::int32_t>(src_off + off));
                    body_.byte(kOpI32Add);
                }
                body_.byte(kOpI32Load);
                body_.u32(kAlignI32);
                body_.u32(0);
                body_.byte(kOpLocalSet);
                body_.u32(final_src_local_ + level + 1u);
                if (!emit_copy_aggregate_at(edge, level + 1u,
                                           dst_base, dst_off + off, range)) {
                    return false;
                }
                continue;
            }
            if (std::holds_alternative<ir::core::CoreLayoutUninhabited>(field.shape) ||
                std::holds_alternative<ir::core::CoreLayoutPending>(field.shape)) {
                return reject("computed final copies a pending/uninhabited field", range);
            }
            return reject("computed final copies a non-v1 frame field", range);
        }
        return true;
    }

    // Copy one child AGGREGATE named by scratch-local `level` into the frame at
    // `dst_base + dst_off`.
    [[nodiscard]] bool emit_copy_aggregate_at(CoreLayoutId id,
                                             std::uint32_t level,
                                             std::uint32_t dst_base,
                                             std::uint64_t dst_off,
                                             ir::SourceRangeOpt range) {
        if (id.value >= layouts_.layouts.size()) {
            return reject("computed final copy walks an out-of-range aggregate layout", range);
        }
        const ir::core::CoreLayout &layout = layouts_.layouts[id.value];
        if (const auto *structure = std::get_if<ir::core::CoreLayoutStruct>(&layout.shape)) {
            return emit_copy_struct_fields(*structure, level, 0, dst_base, dst_off, range);
        }
        if (const auto *tagged = std::get_if<ir::core::CoreLayoutEnum>(&layout.shape)) {
            // Discriminant at offset 0 of the enum.
            emit_copy_word(false, final_src_local_ + level, 0, dst_base, dst_off);
            // Copy ONLY the ACTIVE variant's payload. Every variant shares the
            // one payload union at `payload_offset`, and the variants' layouts
            // may place different field KINDS at the same union offset (an
            // aggregate child's address slot vs an inline scalar word), so a
            // straight-line copy of EVERY payload overwrote the active
            // variant's already-expanded inline words with an inactive
            // variant's module-representation pointer (and leaked a scratch
            // address into the host-visible frame). Read the source
            // discriminant at run time and copy just the matching payload; an
            // out-of-range tag traps instead of leaving stale union bytes.
            const std::uint32_t src = final_src_local_ + level;
            for (std::uint32_t ordinal = 0;
                 ordinal < tagged->variant_payload_layouts.size(); ++ordinal) {
                const CoreLayoutId payload = tagged->variant_payload_layouts[ordinal];
                if (payload.value >= layouts_.layouts.size()) {
                    return reject("computed final enum payload layout is out of range", range);
                }
                const auto *payload_struct =
                    std::get_if<ir::core::CoreLayoutStruct>(
                        &layouts_.layouts[payload.value].shape);
                if (payload_struct == nullptr) {
                    return reject("computed final enum payload is not a struct layout", range);
                }
                // A unit variant stores no payload words; it still participates
                // in the discriminant range check below.
                if (payload_struct->field_layouts.empty()) {
                    continue;
                }
                emit_local_get(src);
                body_.byte(kOpI32Load);
                body_.u32(kAlignI32);
                body_.u32(0);
                emit_const_i32(static_cast<std::int32_t>(ordinal));
                body_.byte(kOpI32Eq);
                body_.byte(kOpIf);
                body_.byte(kEmptyBlock);
                ++label_depth_;
                const bool payload_ok =
                    emit_copy_struct_fields(*payload_struct, level, tagged->payload_offset,
                                            dst_base, dst_off + tagged->payload_offset, range);
                --label_depth_;
                body_.byte(kOpEnd);
                if (!payload_ok) {
                    return false;
                }
            }
            // Defensive fail-closed: a corrupt discriminant names no payload.
            emit_local_get(src);
            body_.byte(kOpI32Load);
            body_.u32(kAlignI32);
            body_.u32(0);
            emit_const_i32(static_cast<std::int32_t>(tagged->variant_payload_layouts.size()));
            body_.byte(kOpI32GeU);
            body_.byte(kOpIf);
            body_.byte(kEmptyBlock);
            ++label_depth_;
            body_.byte(kOpUnreachable);
            --label_depth_;
            body_.byte(kOpEnd);
            return true;
        }
        if (std::holds_alternative<ir::core::CoreLayoutContainer>(layout.shape)) {
            // The inline (ptr,len) header only; the backing placement is shared
            // and its elements are never copied.
            emit_copy_word(false, final_src_local_ + level, 0, dst_base, dst_off);
            emit_copy_word(false, final_src_local_ + level, 4, dst_base, dst_off + 4);
            return true;
        }
        return reject("computed final copy reaches a non-aggregate frame shape", range);
    }

    // Deepest aggregate nesting of a frame layout (the address-stack size the
    // materializer needs): one level per aggregate/container edge reached from
    // the root.
    [[nodiscard]] std::uint32_t final_copy_depth(CoreLayoutId id) const {
        if (id.value >= layouts_.layouts.size()) {
            return 0;
        }
        const ir::core::CoreLayout &layout = layouts_.layouts[id.value];
        std::uint32_t deepest = 0;
        if (const auto *s = std::get_if<ir::core::CoreLayoutStruct>(&layout.shape)) {
            for (const CoreLayoutId child : s->field_layouts) {
                if (child.value >= layouts_.layouts.size()) {
                    continue;
                }
                const ir::core::CoreLayout &child_layout =
                    layouts_.layouts[child.value];
                // A scalar / tag-only-enum leaf is an inline word on THIS
                // level; a payload-bearing enum / struct / container edge
                // descends one address level.
                if (!final_leaf_is_word(child_layout) &&
                    (std::holds_alternative<ir::core::CoreLayoutStruct>(child_layout.shape) ||
                     std::holds_alternative<ir::core::CoreLayoutContainer>(child_layout.shape) ||
                     std::holds_alternative<ir::core::CoreLayoutEnum>(child_layout.shape))) {
                    deepest = std::max(deepest, 1u + final_copy_depth(child));
                }
            }
        } else if (const auto *e = std::get_if<ir::core::CoreLayoutEnum>(&layout.shape)) {
            for (const CoreLayoutId payload : e->variant_payload_layouts) {
                // Payload slots are relative to the enum base (same level),
                // but a nested aggregate field inside one descends.
                deepest = std::max(deepest, final_copy_depth(payload));
            }
        }
        return deepest;
    }

    // Emit the computed-final terminator: materialize the result into the output
    // frame, then leave the handler's result-i32 block yielding the final state
    // id (runv invokes this handler explicitly and discards the word).
    [[nodiscard]] bool emit_final_return(const CoreReturnStmt &s, ir::SourceRangeOpt range) {
        const auto kind = s.value.value < storage_.value_types.size()
                              ? p6_scalar_kind(program_, layouts_,
                                               storage_.value_types[s.value.value])
                              : std::nullopt;
        if (kind == std::nullopt) {
            return reject("computed final value has no P6 kind at emit time", range);
        }
        switch (*kind) {
        case P6ScalarKind::Bool:
        case P6ScalarKind::IntI32:
        case P6ScalarKind::Index:
            emit_const_i32(static_cast<std::int32_t>(output_base()));
            if (!emit_value_read(s.value, range)) {
                return false;
            }
            body_.byte(kOpI32Store);
            body_.u32(kAlignI32);
            body_.u32(0);
            break;
        case P6ScalarKind::IntI64:
            emit_const_i32(static_cast<std::int32_t>(output_base()));
            if (!emit_value_read(s.value, range)) {
                return false;
            }
            body_.byte(kOpI64Store);
            body_.u32(kAlignI64);
            body_.u32(0);
            break;
        case P6ScalarKind::Closure:
        case P6ScalarKind::String:
            return reject("a closure or bare String value cannot be the computed-final frame root",
                          range);
        case P6ScalarKind::Collection: {
            // Copy the 8-byte inline header; the backing placement is shared.
            if (!emit_value_read(s.value, range)) {
                return false;
            }
            body_.byte(kOpLocalSet);
            body_.u32(final_src_local_);
            emit_copy_word(false, final_src_local_, 0, output_base(), 0);
            emit_copy_word(false, final_src_local_, 4, output_base(), 4);
            break;
        }
        case P6ScalarKind::Ptr: {
            const CoreValueTypeId value_type = storage_.value_types[s.value.value];
            if (value_type.value >= layouts_.value_layouts.size()) {
                return reject("computed final aggregate has no finalized layout", range);
            }
            const CoreLayoutId root = layouts_.value_layouts[value_type.value];
            if (root.value >= layouts_.layouts.size()) {
                return reject("computed final aggregate root layout is out of range", range);
            }
            const std::uint64_t size = layouts_.layouts[root.value].size;
            if (size == 0 || size > output_capacity() ||
                size % 4 != 0) {
                return reject("computed final aggregate has an invalid output-frame size", range);
            }
            // 1) Zero the whole output region with a bounded cursor loop. The
            // cursor reuses the source scratch local (the source address is
            // captured only after zeroing completes).
            emit_const_i32(0);
            body_.byte(kOpLocalSet);
            body_.u32(final_src_local_);
            body_.byte(kOpBlock);
            body_.byte(kEmptyBlock);
            ++label_depth_;
            body_.byte(kOpLoop);
            body_.byte(kEmptyBlock);
            ++label_depth_;
            emit_local_get(final_src_local_);
            emit_const_i32(static_cast<std::int32_t>(size));
            body_.byte(kOpI32GeU);
            body_.byte(kOpBrIf);
            body_.u32(1); // cursor >=u size -> leave loop
            emit_const_i32(static_cast<std::int32_t>(output_base()));
            emit_local_get(final_src_local_);
            body_.byte(kOpI32Add);
            emit_const_i32(0);
            body_.byte(kOpI32Store);
            body_.u32(kAlignI32);
            body_.u32(0);
            emit_local_get(final_src_local_);
            emit_const_i32(4);
            body_.byte(kOpI32Add);
            body_.byte(kOpLocalSet);
            body_.u32(final_src_local_);
            body_.byte(kOpBr);
            body_.u32(0);
            body_.byte(kOpEnd); // loop
            --label_depth_;
            body_.byte(kOpEnd); // block
            --label_depth_;
            // 2) Capture the source aggregate address.
            if (!emit_value_read(s.value, range)) {
                return false;
            }
            body_.byte(kOpLocalSet);
            body_.u32(final_src_local_);
            // 3) Copy every named slot (padding stays zero), expanding the
            //    module's aggregate-pointer tree into the inline frame layout.
            if (!emit_copy_aggregate_at(root, /*level=*/0,
                                       output_base(), 0, range)) {
                return false;
            }
            break;
        }
        }
        // The handler was invoked explicitly by runv; yield the stable final
        // state id through its result-i32 block (no global latch, no transition
        // bump: current_state already names this final state).
        emit_const_i32(static_cast<std::int32_t>(final_state_id_));
        body_.byte(kOpBr);
        body_.u32(label_depth_);
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
        // A Decimal/Duration i64 word rides the constant lane only; it has no
        // integer arithmetic opcode on the P6 frame model.
        if (*operand_kind == P6ScalarKind::String ||
            is_decimal_duration_expr(storage_.exprs[u.operand.value])) {
            return reject("unary arithmetic is defined for Int and Bool only on the P6 frame lane",
                          std::move(range));
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
        // A String PtrLen pair never participates in integer/comparison ops
        // (its equality/order is byte semantics on the payload, a later rung),
        // and a Decimal/Duration i64 word is a literal constant, not an
        // arithmetic operand on the P6 frame model.
        if (*lhs_kind == P6ScalarKind::String ||
            is_decimal_duration_expr(storage_.exprs[b.lhs.value]) ||
            is_decimal_duration_expr(storage_.exprs[b.rhs.value])) {
            return reject("binary arithmetic/comparison is defined for Int/Bool only on the P6 "
                          "frame lane",
                          std::move(range));
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
        body_.u32(handler_current_state_global());
        body_.byte(kOpGlobalGet);
        body_.u32(handler_transition_count_global());
        emit_const_i32(1);
        body_.byte(kOpI32Add);
        body_.byte(kOpGlobalSet);
        body_.u32(handler_transition_count_global());
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
        if (site.kind == P6ScalarKind::String) {
            // A String site latches TWO words through emit_binding_latch; a
            // one-word site read would truncate the PtrLen pair.
            return reject("a String pattern site is copied as a PtrLen pair, not read as one word",
                          std::move(range));
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
                    const auto yielded_kind =
                        yield->value.value < storage_.value_types.size()
                            ? scalar_kind(storage_.value_types[yield->value.value])
                            : std::nullopt;
                    emit_set_value_words(yielded_kind, *result_local);
                }
                emit_br(completion_offset + (label_depth_ - region_base));
                return true;
            }
            if (!emit_statement(statement)) {
                return false;
            }
        }
        // A computed-final arm/fallback ends in a value return; its terminator
        // branches past the match to the handler's result block, so it diverges
        // from this region the same way a goto/trap arm does. Use the SAME
        // structural all-paths predicate the handler-level plan gate uses
        // (`p6_region_always_returns`): a trailing return OR a trailing if/match
        // whose every branch returns — a bare `statements.back()` test wrongly
        // rejected an if-let arm whose final `if` returned on both branches
        // even though the whole region provably returns.
        const bool final_return =
            final_return_mode_ && p6_region_always_returns(region);
        if (!final_return && !p6_region_always_diverges(region)) {
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
                    // A two-word result is popped in REVERSE push order: the
                    // second word (closure env_ptr / String byte_len) into
                    // slot+1 first, then the first word (func_index / ptr).
                    const auto result_kind =
                        s.result.value < storage_.value_types.size()
                            ? scalar_kind(storage_.value_types[s.result.value])
                            : std::nullopt;
                    emit_set_value_words(result_kind, *local);
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
                    // V2-C: a planned HANDLER bridge statement invokes the
                    // additive bridge functype; an outlined fn body keeps the
                    // opaque tuple call.
                    if (!fn_mode_ && bridge_registry_ != nullptr) {
                        return emit_bridge_call(s, statement.source_range);
                    }
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
                    if (final_return_mode_) {
                        return emit_final_return(s, statement.source_range);
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

// The boundary table is a FIXED-EDGE TREE of value slots: a struct field or an
// enum payload slot is a distinct, simultaneously-live position even when two
// such positions hash-cons to one source layout id. A bounded CONTAINER reached
// at one slot owns an 8-byte inline header and its own disjoint backing
// placement; collapsing two same-typed container slots onto one placement would
// alias their elements (design section 6.2: "Two simultaneously-live input
// containers therefore never overlap"). We therefore:
//   * unfold every FIXED aggregate slot that bears a container descendant into
//     its own dense occurrence node (two `List<Int>(4)` fields become two dense
//     container nodes, each with its own placement);
//   * intern pure-data fixed subtrees (no container descendant) and every
//     BACKING subtree (a container's element/map-value edges) by source id,
//     since those nodes name no per-slot placement;
//   * reject a container nested inside BACKING storage (a collection whose
//     element/value is itself a container, or a backing aggregate bearing a
//     container field): such headers would need a placement per capacity slot,
//     which is outside the P6-7 rung-E frame lane and must never alias.
struct FrameContainerOccurrence {
    ir::core::CoreLayoutId dense_layout;
    bool from_input{false};
};

class BoundaryTableBuilder {
  public:
    explicit BoundaryTableBuilder(const ir::core::CoreLayoutTable &source) : source_(source) {
        shared_.assign(source_.layouts.size(), ir::core::CoreLayoutId{});
        backing_.assign(source_.layouts.size(), ir::core::CoreLayoutId{});
        shared_seen_.assign(source_.layouts.size(), false);
        shared_pending_.assign(source_.layouts.size(), false);
        backing_seen_.assign(source_.layouts.size(), false);
        backing_pending_.assign(source_.layouts.size(), false);
        dense_.target = source_.target;
    }

    // True when a FIXED-edge descent from `id` (struct fields, enum payload
    // slots) reaches a bounded container. Container backing edges do not count:
    // they open the separate backing realm.
    [[nodiscard]] bool bears_fixed_container(ir::core::CoreLayoutId id) {
        if (id.value >= source_.layouts.size()) {
            return false;
        }
        const auto cached = bears_.find(id.value);
        if (cached != bears_.end()) {
            return cached->second;
        }
        if (bear_active_.contains(id.value)) {
            return false; // defensive: fixed-edge graphs are acyclic
        }
        bear_active_.insert(id.value);
        bool bears = false;
        std::visit(
            Overloaded{
                [&](const ir::core::CoreLayoutStruct &shape) {
                    for (const auto child : shape.field_layouts) {
                        if (bears_fixed_container(child)) {
                            bears = true;
                        }
                    }
                },
                [&](const ir::core::CoreLayoutEnum &shape) {
                    for (const auto payload : shape.variant_payload_layouts) {
                        if (bears_fixed_container(payload)) {
                            bears = true;
                        }
                    }
                },
                [&](const ir::core::CoreLayoutContainer &) { bears = true; },
                [](const auto &) {},
            },
            source_.layouts[id.value].shape);
        bear_active_.erase(id.value);
        bears_[id.value] = bears;
        return bears;
    }

    // Emit a node reached through a FIXED frame slot (struct field / enum
    // payload slot / a boundary root). A container here is a new per-SLOT
    // occurrence with its own backing placement: two fixed slots that hash-cons
    // to one source aggregate/container id are deliberately UNFOLDED into
    // distinct dense nodes (design section 6.2 disjoint-placement rule).
    // Children are emitted FIRST and the node appended last (post-order), so
    // the dense vector never reallocates under a live reference to an entry.
    ir::core::CoreLayoutId emit_fixed(ir::core::CoreLayoutId src, bool from_input) {
        if (src.value >= source_.layouts.size()) {
            failed_ = true;
            return ir::core::CoreLayoutId{};
        }
        if (std::holds_alternative<ir::core::CoreLayoutContainer>(
                source_.layouts[src.value].shape)) {
            return emit_container_occurrence(src, from_input);
        }
        if (!bears_fixed_container(src)) {
            return emit_shared(src);
        }
        // Fixed aggregate inline graphs are acyclic (the producer layout
        // verifier rejects inline-only cycles); an active edge nonetheless
        // means a layout table we should never have received.
        if (!fixed_active_.insert(src.value).second) {
            failed_ = true;
            return ir::core::CoreLayoutId{};
        }
        ir::core::CoreLayout copy = source_.layouts[src.value];
        const bool local_ok = rewrite_fixed(copy, src, from_input);
        fixed_active_.erase(src.value);
        if (!local_ok || failed_) {
            failed_ = true;
            return ir::core::CoreLayoutId{};
        }
        const ir::core::CoreLayoutId dense{static_cast<std::uint32_t>(dense_.layouts.size())};
        dense_.layouts.push_back(std::move(copy));
        return dense;
    }

    // Emit a node reached through a container's BACKING storage (element or
    // map value). This realm never carries a live container header in the
    // rung-E frame lane, so it is interned by source id and fails closed if a
    // container (or a container-bearing aggregate) appears.
    ir::core::CoreLayoutId emit_backing(ir::core::CoreLayoutId src) {
        if (src.value >= source_.layouts.size()) {
            failed_ = true;
            return ir::core::CoreLayoutId{};
        }
        if (std::holds_alternative<ir::core::CoreLayoutContainer>(
                source_.layouts[src.value].shape) ||
            bears_fixed_container(src)) {
            nested_container_ = true;
            failed_ = true;
            return ir::core::CoreLayoutId{};
        }
        if (backing_seen_[src.value]) {
            return backing_[src.value];
        }
        if (backing_pending_[src.value]) {
            failed_ = true;
            return ir::core::CoreLayoutId{};
        }
        backing_pending_[src.value] = true;
        ir::core::CoreLayout copy = source_.layouts[src.value];
        if (!rewrite_backing(copy, src)) {
            failed_ = true;
            return ir::core::CoreLayoutId{};
        }
        const ir::core::CoreLayoutId dense{static_cast<std::uint32_t>(dense_.layouts.size())};
        backing_[src.value] = dense;
        backing_seen_[src.value] = true;
        dense_.layouts.push_back(std::move(copy));
        backing_pending_[src.value] = false;
        return dense;
    }

    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] bool nested_container() const noexcept { return nested_container_; }
    [[nodiscard]] const std::vector<FrameContainerOccurrence> &occurrences() const noexcept {
        return occurrences_;
    }
    // Non-destructive access to the dense table accumulated so far (the
    // physical planner continues emitting fixed roots after the boundary roots;
    // call `take_table()` exactly once when planning is complete).
    [[nodiscard]] const ir::core::CoreLayoutTable &table_ref() const noexcept {
        return dense_;
    }
    [[nodiscard]] ir::core::CoreLayoutTable take_table() { return std::move(dense_); }

  private:
    const ir::core::CoreLayoutTable &source_;
    ir::core::CoreLayoutTable dense_;
    // Active fixed-aggregate source ids on the current unfold path (cycle
    // termination only; it never dedups two distinct edge occurrences).
    std::unordered_set<std::uint32_t> fixed_active_;
    // Pure-data fixed subtree interning.
    std::vector<ir::core::CoreLayoutId> shared_;
    std::vector<bool> shared_pending_;
    std::vector<bool> shared_seen_;
    // Backing-realm interning.
    std::vector<ir::core::CoreLayoutId> backing_;
    std::vector<bool> backing_pending_;
    std::vector<bool> backing_seen_;
    std::unordered_map<std::uint32_t, bool> bears_;
    std::unordered_set<std::uint32_t> bear_active_;
    std::vector<FrameContainerOccurrence> occurrences_;
    bool failed_{false};
    bool nested_container_{false};

    ir::core::CoreLayoutId emit_container_occurrence(ir::core::CoreLayoutId src,
                                                     bool from_input) {
        const auto &source_container =
            std::get<ir::core::CoreLayoutContainer>(source_.layouts[src.value].shape);
        // Backing children first (post-order).
        const ir::core::CoreLayoutId element = emit_backing(source_container.element);
        std::optional<ir::core::CoreLayoutId> value;
        if (source_container.value.has_value()) {
            value = emit_backing(*source_container.value);
        }
        if (failed_) {
            return ir::core::CoreLayoutId{};
        }
        ir::core::CoreLayout copy = source_.layouts[src.value];
        auto &container = std::get<ir::core::CoreLayoutContainer>(copy.shape);
        container.element = element;
        container.value = value;
        const ir::core::CoreLayoutId dense{static_cast<std::uint32_t>(dense_.layouts.size())};
        dense_.layouts.push_back(std::move(copy));
        // Recorded in boundary DFS order: backing nodes are never occurrences.
        occurrences_.push_back(FrameContainerOccurrence{dense, from_input});
        return dense;
    }

    // Rewrite the fixed-edge children of a LOCAL copy (no dense_ reference
    // held across the recursive appends).
    [[nodiscard]] bool rewrite_fixed(ir::core::CoreLayout &dense,
                                     ir::core::CoreLayoutId src,
                                     bool from_input) {
        std::visit(
            Overloaded{
                [&](ir::core::CoreLayoutStruct &shape) {
                    const auto &source_shape =
                        std::get<ir::core::CoreLayoutStruct>(source_.layouts[src.value].shape);
                    for (std::uint32_t i = 0; i < shape.field_layouts.size(); ++i) {
                        shape.field_layouts[i] =
                            emit_fixed(source_shape.field_layouts[i], from_input);
                    }
                },
                [&](ir::core::CoreLayoutEnum &shape) {
                    const auto &source_shape =
                        std::get<ir::core::CoreLayoutEnum>(source_.layouts[src.value].shape);
                    for (std::uint32_t i = 0; i < shape.variant_payload_layouts.size(); ++i) {
                        shape.variant_payload_layouts[i] =
                            emit_fixed(source_shape.variant_payload_layouts[i], from_input);
                    }
                },
                [&](ir::core::CoreLayoutClosure &shape) {
                    if (shape.environment.has_value()) {
                        shape.environment = emit_backing(*shape.environment);
                    }
                },
                [](auto &) {},
            },
            dense.shape);
        return !failed_;
    }

    // Rewrite a backing-realm LOCAL copy's children (struct/enum fields stay in
    // the backing realm; a container there has already been rejected up front).
    [[nodiscard]] bool rewrite_backing(ir::core::CoreLayout &dense,
                                       ir::core::CoreLayoutId src) {
        std::visit(
            Overloaded{
                [&](ir::core::CoreLayoutStruct &shape) {
                    const auto &source_shape =
                        std::get<ir::core::CoreLayoutStruct>(source_.layouts[src.value].shape);
                    for (std::uint32_t i = 0; i < shape.field_layouts.size(); ++i) {
                        shape.field_layouts[i] = emit_backing(source_shape.field_layouts[i]);
                    }
                },
                [&](ir::core::CoreLayoutEnum &shape) {
                    const auto &source_shape =
                        std::get<ir::core::CoreLayoutEnum>(source_.layouts[src.value].shape);
                    for (std::uint32_t i = 0; i < shape.variant_payload_layouts.size(); ++i) {
                        shape.variant_payload_layouts[i] =
                            emit_backing(source_shape.variant_payload_layouts[i]);
                    }
                },
                [&](ir::core::CoreLayoutClosure &shape) {
                    if (shape.environment.has_value()) {
                        shape.environment = emit_backing(*shape.environment);
                    }
                },
                [](auto &) {},
            },
            dense.shape);
        return !failed_;
    }

    // Intern a pure-data (container-free) fixed subtree by source id.
    ir::core::CoreLayoutId emit_shared(ir::core::CoreLayoutId src) {
        if (shared_seen_[src.value]) {
            return shared_[src.value];
        }
        if (shared_pending_[src.value]) {
            failed_ = true;
            return ir::core::CoreLayoutId{};
        }
        shared_pending_[src.value] = true;
        ir::core::CoreLayout copy = source_.layouts[src.value];
        if (!rewrite_shared(copy, src)) {
            failed_ = true;
            return ir::core::CoreLayoutId{};
        }
        const ir::core::CoreLayoutId dense{static_cast<std::uint32_t>(dense_.layouts.size())};
        shared_[src.value] = dense;
        shared_seen_[src.value] = true;
        dense_.layouts.push_back(std::move(copy));
        shared_pending_[src.value] = false;
        return dense;
    }

    [[nodiscard]] bool rewrite_shared(ir::core::CoreLayout &dense,
                                      ir::core::CoreLayoutId src) {
        std::visit(
            Overloaded{
                [&](ir::core::CoreLayoutStruct &shape) {
                    const auto &source_shape =
                        std::get<ir::core::CoreLayoutStruct>(source_.layouts[src.value].shape);
                    for (std::uint32_t i = 0; i < shape.field_layouts.size(); ++i) {
                        shape.field_layouts[i] = emit_shared(source_shape.field_layouts[i]);
                    }
                },
                [&](ir::core::CoreLayoutEnum &shape) {
                    const auto &source_shape =
                        std::get<ir::core::CoreLayoutEnum>(source_.layouts[src.value].shape);
                    for (std::uint32_t i = 0; i < shape.variant_payload_layouts.size(); ++i) {
                        shape.variant_payload_layouts[i] =
                            emit_shared(source_shape.variant_payload_layouts[i]);
                    }
                },
                [&](ir::core::CoreLayoutContainer &) {
                    // A "pure" node never reaches a container via fixed edges.
                    failed_ = true;
                },
                [&](ir::core::CoreLayoutClosure &shape) {
                    if (shape.environment.has_value()) {
                        shape.environment = emit_backing(*shape.environment);
                    }
                },
                [](auto &) {},
            },
            dense.shape);
        return !failed_;
    }
};

// Why an input-reached container placement can fail.
enum class FramePlacementFailure {
    InvalidBacking,
    PageOverflow,
};

struct AssignedFramePlacements {
    std::vector<ir::core::CoreFrameBackingPlacement> placements;
    std::uint32_t payload_arena_base{0};
};

// Design section 6.2 sum-of-prior-backing rule, the single SSOT for both the
// emitted `ahfl.core-layout.v1` placements and the construct/closure heap
// relocation: every INPUT-REACHED fixed container occurrence, in boundary DFS
// order, receives its own placement at
// align_up(kP6CollectionBackingBase + sum of prior aligned backing extents,
// 8), and the frame-payload arena begins at the aligned high-water. Two
// simultaneously-live input containers therefore never share a base. Failure
// distinguishes an invalid (zero) backing layout from the fixed-page overflow.
[[nodiscard]] std::optional<AssignedFramePlacements>
assign_input_container_placements(const ir::core::CoreLayoutTable &dense,
                                  const std::vector<FrameContainerOccurrence> &occurrences,
                                  FramePlacementFailure &failure) {
    AssignedFramePlacements assigned;
    std::uint64_t cursor = ir::core::kP6CollectionBackingBase;
    std::uint32_t edge = 0;
    for (const FrameContainerOccurrence &occurrence : occurrences) {
        if (!occurrence.from_input) {
            continue;
        }
        const auto &container = std::get<ir::core::CoreLayoutContainer>(
            dense.layouts[occurrence.dense_layout.value].shape);
        if (container.backing_size == 0) {
            failure = FramePlacementFailure::InvalidBacking;
            return std::nullopt;
        }
        const std::uint64_t extent =
            (static_cast<std::uint64_t>(container.backing_size) + 7u) & ~std::uint64_t{7u};
        if (cursor + extent > ir::core::kCoreWasmFixedLinearMemoryCapacityBytes) {
            failure = FramePlacementFailure::PageOverflow;
            return std::nullopt;
        }
        assigned.placements.push_back(ir::core::CoreFrameBackingPlacement{
            edge,
            occurrence.dense_layout,
            static_cast<std::uint32_t>(cursor),
            static_cast<std::uint32_t>(extent),
        });
        cursor += extent;
        ++edge;
    }
    assigned.payload_arena_base =
        static_cast<std::uint32_t>((cursor + 7u) & ~std::uint64_t{7u});
    return assigned;
}

// Diagnostic-free design-6.2 input backing high-water (the frame-payload arena
// base) for an agent's input boundary: the aligned base just past every
// disjoint per-edge input-container placement. The per-activation
// construct/closure heap uses this same number as a floor so it can never be
// relocated inside a packed container's backing (design section 6.2). Returns
// the backing region base for a boundary that does not finalize into the P6
// frame lane (no P6 value/layout, nested containers, fixed-page overflow);
// build_frame_section_plan remains the authority that reports those conditions
// as diagnostics at frame eligibility time.
[[nodiscard]] std::uint32_t
input_frame_backing_high_water(const CoreProgram &program,
                               const ir::core::CoreLayoutTable &layouts,
                               CoreTypeId input_nominal) {
    const auto input_vt = p6_nominal_value_type(program, input_nominal);
    if (!input_vt.has_value() || input_vt->value >= layouts.value_layouts.size()) {
        return ir::core::kP6CollectionBackingBase;
    }
    BoundaryTableBuilder builder(layouts);
    (void)builder.emit_fixed(layouts.value_layouts[input_vt->value], /*from_input=*/true);
    if (builder.failed()) {
        return ir::core::kP6CollectionBackingBase;
    }
    const ir::core::CoreLayoutTable dense = builder.take_table();
    FramePlacementFailure failure = FramePlacementFailure::InvalidBacking;
    const auto assigned =
        assign_input_container_placements(dense, builder.occurrences(), failure);
    if (!assigned.has_value()) {
        return ir::core::kP6CollectionBackingBase;
    }
    return assigned->payload_arena_base;
}
// ---------------------------------------------------------------------------
// RFC 0026 P6-7 rung A: the `ahfl.core-layout.v1` section plan.
//
// A P6-frame agent's boundary roots are its input/output value types. The plan
// enumerates every input-reached bounded CONTAINER in a depth-first,
// declaration-order boundary walk and assigns each a DISJOINT backing placement
// by the sum-of-prior-backing rule (design section 6.2), then derives the
// frame-payload arena span (0-capacity at rung A: input String packing arrives
// with the packer in rung C; the arena address is still pinned deterministically
// so no later rung can move it). Every coordinate is compile-time constant.
// ---------------------------------------------------------------------------

struct FrameSectionPlan {
    ir::core::CoreValueTypeId input_vt{};
    ir::core::CoreValueTypeId output_vt{};
    /// Roots / placement container ids are already REMAPPED into the dense
    /// `table` id space below (they are NOT ids into the full program layout
    /// table).
    ir::core::CoreLayoutId input_layout{};
    ir::core::CoreLayoutId output_layout{};
    std::uint32_t input_size{0};
    std::uint32_t output_size{0};
    std::vector<ir::core::CoreFrameBackingPlacement> placements;
    std::uint32_t payload_arena_base{0};
    std::uint32_t payload_arena_capacity{0};
    /// V2-B: rodata literal-pool span carried to the section payload.
    std::uint32_t rodata_base{0};
    std::uint32_t rodata_extent{0};
    /// V2-C: the capability bridge control page frame (zero for a non-bridge
    /// module) and the dense per-call-site section records.
    std::uint32_t bridge_control_base{0};
    std::uint32_t bridge_block_stride{0};
    std::uint32_t bridge_control_extent{0};
    std::uint32_t bridge_spill_base{0};
    std::uint32_t bridge_spill_extent{0};
    std::vector<ir::core::CoreFrameBridgeCallSite> bridge_call_sites;
    /// The self-contained boundary table: exactly the layouts reachable from
    /// the two roots, with all edges remapped into this dense table.
    ir::core::CoreLayoutTable table;
};

[[nodiscard]] std::optional<ir::core::CoreValueTypeId>
frame_boundary_value_type(const CoreProgram &program, CoreTypeId nominal) {
    return p6_nominal_value_type(program, nominal);
}

// Build the canonical frame-layout SECTION payload structure (the one the
// custom-section encoder and the host admitter consume) from the physical
// plan. One SSOT for the boundary roots, placements, payload arena, rodata
// span and the V2-C bridge page frame / call sites.
[[nodiscard]] ir::core::CoreFrameLayoutSection
frame_section_to_layout_section(const FrameSectionPlan &plan) {
    ir::core::CoreFrameLayoutSection section;
    // v3: per-call-site private spill windows (frame-bridge v2 fix-forward).
    section.format_version = 3;
    section.table = plan.table;
    section.input_layout = plan.input_layout;
    section.output_layout = plan.output_layout;
    section.placements = plan.placements;
    section.payload_arena_base = plan.payload_arena_base;
    section.payload_arena_capacity = plan.payload_arena_capacity;
    section.rodata_base = plan.rodata_base;
    section.rodata_extent = plan.rodata_extent;
    section.bridge_control_base = plan.bridge_control_base;
    section.bridge_block_stride = plan.bridge_block_stride;
    section.bridge_control_extent = plan.bridge_control_extent;
    section.bridge_spill_base = plan.bridge_spill_base;
    section.bridge_spill_extent = plan.bridge_spill_extent;
    section.bridge_call_sites = plan.bridge_call_sites;
    return section;
}

// RFC 0026 P6-7 frame-bridge v2 D6 (rung V2-B): walk the INPUT boundary's dense
// P4-D layout and its wire schema IN LOCKSTEP (the same shape correspondence
// verify_frame_layout_wire_consistency proves), metering every String slot:
// a bounded String contributes its schema upper bound, and the first unbounded
// String marks the shared kP6FrameStringPoolBytes reservation. A shape
// disagreement fails the plan. Returns false on malformed ids or a capacity
// product that overflows the 32-bit arena.
//
// OCCURRENCE MULTIPLICITY: a struct/enum field exists once, but a bounded
// container's element subtree exists `capacity` times — the host packer bumps
// its arena cursor once per LIVE element (node_embedded_host.mjs packValue
// 'sequence'), so the schema upper bound of every String slot reachable through
// one element is multiplied by the container capacity here (a Map multiplies
// the summed key+value subtree once). The design formula is per-occurrence:
// "align8(sum 各 String 槽 schema 上界)" with size/stride/capacity the
// compile-time inputs (frame-bridge v2 section 7.1). The unbounded-String pool
// is a SINGLE shared reservation per boundary and is intentionally NOT
// multiplied: it is a conservative fallback ceiling, not a per-slot budget.
[[nodiscard]] bool accumulate_input_string_arena(
    const ir::core::CoreLayoutTable &layouts,
    ir::core::CoreLayoutId layout_id,
    const ir::core::CoreWireSchemaTable &wire,
    ir::core::CoreWireSchemaNodeId wire_id,
    std::uint64_t &bounded_sum,
    bool &has_unbounded_string) {
    if (layout_id.value >= layouts.layouts.size() ||
        wire_id.value >= wire.nodes.size()) {
        return false;
    }
    const auto &layout = layouts.layouts[layout_id.value].shape;
    const auto &shape = wire.nodes[wire_id.value].shape;
    return std::visit(
        Overloaded{
            [&](const ir::core::CoreLayoutScalar &) { return true; },
            [&](const ir::core::CoreLayoutPending &) { return false; },
            [&](const ir::core::CoreLayoutPtrLen &) {
                const auto *str = std::get_if<ir::core::CoreWireSchemaString>(&shape);
                if (str == nullptr) {
                    return false;
                }
                if (str->length_bounds.has_value()) {
                    bounded_sum += static_cast<std::uint64_t>(str->length_bounds->second);
                    if (bounded_sum > std::numeric_limits<std::uint32_t>::max()) {
                        return false;
                    }
                } else {
                    has_unbounded_string = true;
                }
                return true;
            },
            [&](const ir::core::CoreLayoutBytes &) { return false; },
            [&](const ir::core::CoreLayoutFnRef &) { return false; },
            [&](const ir::core::CoreLayoutClosure &) { return false; },
            [&](const ir::core::CoreLayoutUninhabited &) { return false; },
            [&](const ir::core::CoreLayoutStruct &s) {
                const auto *wst = std::get_if<ir::core::CoreWireSchemaStruct>(&shape);
                if (wst == nullptr || wst->fields.size() != s.field_layouts.size()) {
                    return false;
                }
                for (std::uint32_t i = 0; i < s.field_layouts.size(); ++i) {
                    if (!accumulate_input_string_arena(layouts, s.field_layouts[i], wire,
                                                       wst->fields[i].type, bounded_sum,
                                                       has_unbounded_string)) {
                        return false;
                    }
                }
                return true;
            },
            [&](const ir::core::CoreLayoutEnum &e) {
                const auto *wen = std::get_if<ir::core::CoreWireSchemaEnum>(&shape);
                if (wen == nullptr || wen->variants.size() != e.variant_payload_layouts.size()) {
                    return false;
                }
                for (std::uint32_t v = 0; v < e.variant_payload_layouts.size(); ++v) {
                    const auto payload_id = e.variant_payload_layouts[v];
                    if (payload_id.value >= layouts.layouts.size()) {
                        return false;
                    }
                    const auto *payload = std::get_if<ir::core::CoreLayoutStruct>(
                        &layouts.layouts[payload_id.value].shape);
                    if (payload == nullptr ||
                        payload->field_layouts.size() != wen->variants[v].slots.size()) {
                        return false;
                    }
                    for (std::uint32_t i = 0; i < payload->field_layouts.size(); ++i) {
                        if (!accumulate_input_string_arena(layouts,
                                                           payload->field_layouts[i], wire,
                                                           wen->variants[v].slots[i].type,
                                                           bounded_sum, has_unbounded_string)) {
                            return false;
                        }
                    }
                }
                return true;
            },
            [&](const ir::core::CoreLayoutContainer &c) {
                // A bounded List carries one element slot; a Map carries key +
                // value slots (key shapes stay outside the frame subset, so a
                // String key would already have failed wire projection). Meter
                // the child subtrees into LOCAL accumulators, then scale their
                // bounded contribution by the capacity: every one of the
                // `capacity` element (or key/value pair) slots can carry a
                // distinct String payload, and the host packer bumps one arena
                // cursor per live element. Metering the subtree once reserved
                // only one element's budget and fail-closed on schema-valid
                // inputs (e.g. List<String(0,8)>(4) packed with four 8-byte
                // strings needs 32 bytes, not 8). The unbounded pool share
                // propagates as a boolean, never multiplied (one shared pool).
                std::uint64_t child_bounded = 0;
                bool child_unbounded = false;
                const auto meter_child = [&](ir::core::CoreLayoutId child_layout,
                                             ir::core::CoreWireSchemaNodeId child_wire) {
                    return accumulate_input_string_arena(layouts, child_layout, wire,
                                                         child_wire, child_bounded,
                                                         child_unbounded);
                };
                bool child_ok = false;
                const auto *seq = std::get_if<ir::core::CoreWireSchemaSequence>(&shape);
                const auto *map = std::get_if<ir::core::CoreWireSchemaMap>(&shape);
                if (seq != nullptr) {
                    child_ok = meter_child(c.element, seq->element);
                } else if (map != nullptr) {
                    child_ok = c.value.has_value() &&
                               meter_child(c.element, map->key) &&
                               meter_child(*c.value, map->value);
                }
                if (!child_ok) {
                    return false;
                }
                constexpr std::uint64_t kU64Max =
                    std::numeric_limits<std::uint64_t>::max();
                if (child_bounded != 0 &&
                    c.capacity > kU64Max / child_bounded) {
                    return false;
                }
                const std::uint64_t scaled = child_bounded * c.capacity;
                if (bounded_sum > kU64Max - scaled ||
                    bounded_sum + scaled >
                        std::numeric_limits<std::uint32_t>::max()) {
                    return false;
                }
                bounded_sum += scaled;
                has_unbounded_string = has_unbounded_string || child_unbounded;
                return true;
            },
        },
        layout);
}


[[nodiscard]] std::optional<FrameSectionPlan>
build_frame_section_plan(const CoreProgram &program,
                         const ir::core::CoreLayoutTable &layouts,
                         const CoreAgentDecl &agent,
                         std::uint32_t rodata_extent,
                         const ir::core::CoreWireSchemaTable *frame_wire_table,
                         BridgeCallRegistry *bridge_registry,
                         CoreWasmCodegenResult &result) {
    const auto input_vt = frame_boundary_value_type(program, agent.input_type);
    const auto output_vt = frame_boundary_value_type(program, agent.output_type);
    if (!input_vt.has_value() || !output_vt.has_value()) {
        add_diag(result, core_wasm_diag::kInvalidLayout,
                 "a P6-frame agent boundary nominal has no finalized argument-less value type");
        return std::nullopt;
    }
    const ir::core::CoreLayout *input_layout =
        p6_value_layout(program, layouts, *input_vt);
    const ir::core::CoreLayout *output_layout =
        p6_value_layout(program, layouts, *output_vt);
    if (input_layout == nullptr || output_layout == nullptr) {
        add_diag(result, core_wasm_diag::kInvalidLayout,
                 "a P6-frame agent boundary nominal has no finalized P4-D layout");
        return std::nullopt;
    }

    FrameSectionPlan plan;
    plan.input_vt = *input_vt;
    plan.output_vt = *output_vt;
    const ir::core::CoreLayoutId full_input_layout =
        layouts.value_layouts[input_vt->value];
    const ir::core::CoreLayoutId full_output_layout =
        layouts.value_layouts[output_vt->value];
    // Bound the descriptor's u32 frame sizes explicitly instead of narrowing a
    // u64 layout size with an unchecked static_cast, and enforce the design
    // section 6.1 frame-region capacities here too (input <= 3072; an identity
    // final's output equals its input and a computed-final output <= 4096 is
    // gated again when computed emission lands). Today's 64 KiB RESOURCE gates
    // keep these unreachable, but the descriptor is the authority the host
    // cross-checks against the u64-encoded section.
    constexpr std::uint64_t kU32Max = std::numeric_limits<std::uint32_t>::max();
    if (input_layout->size > kU32Max ||
        input_layout->size > ir::core::kP6AggregateInputCapacity) {
        add_diag(result,
                 core_wasm_diag::kResourceExhausted,
                 "P6 frame input boundary exceeds the input frame region of the fixed 64 KiB "
                 "linear-memory page");
        return std::nullopt;
    }
    if (output_layout->size > kU32Max ||
        output_layout->size > ir::core::kP6AggregateOutputCapacity) {
        add_diag(result,
                 core_wasm_diag::kResourceExhausted,
                 "P6 frame output boundary exceeds the output frame region of the fixed 64 KiB "
                 "linear-memory page");
        return std::nullopt;
    }
    plan.input_size = static_cast<std::uint32_t>(input_layout->size);
    plan.output_size = static_cast<std::uint32_t>(output_layout->size);

    BoundaryTableBuilder builder(layouts);
    const ir::core::CoreLayoutId input_dense =
        builder.emit_fixed(full_input_layout, /*from_input=*/true);
    ir::core::CoreLayoutId output_dense = input_dense;
    if (full_output_layout.value != full_input_layout.value) {
        output_dense = builder.emit_fixed(full_output_layout, /*from_input=*/false);
    }
    if (builder.failed()) {
        if (builder.nested_container()) {
            add_diag(result, core_wasm_diag::kUnsupportedOrchestration,
                     "a P6-frame bounded collection nests another container in its backing "
                     "storage; nested bounded collections are outside the P6-7 frame lane");
        } else {
            add_diag(result, core_wasm_diag::kInvalidLayout,
                     "a P6-frame boundary layout is not finalizable into a self-contained frame "
                     "table");
        }
        return std::nullopt;
    }
    plan.input_layout = input_dense;
    plan.output_layout = output_dense;

    // Every INPUT-REACHED fixed container occurrence, in boundary DFS order,
    // gets a DISJOINT sum-of-prior-backing placement (the same SSOT the
    // construct-heap relocation uses). Output-only occurrences (a distinct
    // computed-output nominal) name no input placement at rung E.
    FramePlacementFailure placement_failure = FramePlacementFailure::InvalidBacking;
    const auto assigned =
        assign_input_container_placements(builder.table_ref(), builder.occurrences(),
                                          placement_failure);
    if (!assigned.has_value()) {
        if (placement_failure == FramePlacementFailure::InvalidBacking) {
            add_diag(result, core_wasm_diag::kInvalidLayout,
                     "a P6-frame input container has an invalid backing layout");
        } else {
            add_diag(result,
                     core_wasm_diag::kResourceExhausted,
                     "P6 frame backing placements exceed the fixed 64 KiB linear-memory page");
        }
        return std::nullopt;
    }
    plan.placements = assigned->placements;
    plan.payload_arena_base = assigned->payload_arena_base;
    // RFC 0026 P6-7 frame-bridge v2 D6 (rung V2-B): the input frame's packed
    // String payloads need a REAL arena now (it was hardcoded 0 at rung A).
    // Bounded String slots are metered exactly from their schema upper bound
    // times their occurrence multiplicity (a bounded collection multiplies its
    // element subtree by its capacity); the first unbounded String slot is
    // covered by the shared kP6FrameStringPoolBytes reservation (one pool per
    // boundary, never multiplied). The joint layout/wire walk below is the
    // single derivation.
    std::uint64_t bounded_sum = 0;
    bool has_unbounded_string = false;
    if (frame_wire_table != nullptr && frame_wire_table->frame_roots.has_value()) {
        if (!accumulate_input_string_arena(builder.table_ref(),
                                           plan.input_layout,
                                           *frame_wire_table,
                                           frame_wire_table->frame_roots->input,
                                           bounded_sum,
                                           has_unbounded_string)) {
            add_diag(result,
                     core_wasm_diag::kInvalidLayout,
                     "the P6-frame input layout and wire schema disagree on a String slot while "
                     "planning the payload arena");
            return std::nullopt;
        }
    }
    std::uint64_t arena_capacity = (bounded_sum + 7u) & ~std::uint64_t{7u};
    if (has_unbounded_string) {
        arena_capacity += kP6FrameStringPoolBytes;
    }
    if (arena_capacity > std::numeric_limits<std::uint32_t>::max() ||
        static_cast<std::uint64_t>(plan.payload_arena_base) + arena_capacity >
            kCoreWasmFixedLinearMemoryCapacityBytes) {
        add_diag(result,
                 core_wasm_diag::kResourceExhausted,
                 "the P6 input-frame String payload arena (bounded-slot bounds " +
                     std::to_string(bounded_sum) + " bytes plus the unbounded pool " +
                     (has_unbounded_string ? std::to_string(kP6FrameStringPoolBytes) : "0") +
                     " bytes) exceeds the fixed 64 KiB linear-memory page") ;
        return std::nullopt;
    }
    plan.payload_arena_capacity = static_cast<std::uint32_t>(arena_capacity);
    // V2-B: rodata span (always names the fixed [256,1024) region).
    plan.rodata_base = kP6RodataBase;
    plan.rodata_extent = rodata_extent;

    // RFC 0026 P6-7 frame-bridge v2 D3/D4/D6 (rung V2-C): plan the capability
    // bridge control page frame and the disjoint per-call-site result
    // placements. They continue the same sum cursor after the input frame
    // payload arena: control blocks (fixed-stride dense blocks), the scalar /
    // PtrLen spill slots, then per call site the result root and its payload
    // arena. The wire projection is required for every argument/result root so
    // a non-frame-walkable shape rejects here with no partial artifact.
    if (bridge_registry != nullptr && !bridge_registry->empty()) {
        if (frame_wire_table == nullptr) {
            add_diag(result, core_wasm_diag::kInvalidCapabilityAbi,
                     "a frame capability bridge needs the projected wire-schema capability table");
            return std::nullopt;
        }
        std::vector<BridgeCallPlan> &bridge_calls = bridge_registry->sites();
        const auto align8 = [](std::uint64_t value) {
            return (value + 7u) & ~std::uint64_t{7u};
        };
        // Emit the boundary roots first so result/param layout ids are stable.
        const std::uint64_t arena_end =
            static_cast<std::uint64_t>(plan.payload_arena_base) +
            plan.payload_arena_capacity;
        std::uint64_t cursor = align8(arena_end);

        // The fixed block stride covers the largest block (8 + 8*max_arity),
        // 8-aligned.
        std::uint32_t max_arity = 0;
        std::uint64_t spill_total = 0;
        for (const BridgeCallPlan &site : bridge_calls) {
            max_arity = std::max(max_arity,
                                 static_cast<std::uint32_t>(site.param_vt.size()));
            spill_total += align8(site.spill_bytes);
        }
        const std::uint64_t block_stride = align8(8u + 8u * max_arity);
        const std::uint64_t blocks_extent =
            block_stride * static_cast<std::uint64_t>(bridge_calls.size());
        // V2-C fix-forward: every bridge coordinate is a u32 wasm32 immediate,
        // so narrow the u64 stride/extent math only after a checked comparison
        // against the uint32 domain. The fixed-page checks below bound the
        // FINAL sum cursor but would let an intermediate wrap slip through.
        constexpr std::uint64_t kU32Domain =
            static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max());
        if (block_stride > kU32Domain || spill_total > kU32Domain ||
            blocks_extent > kU32Domain ||
            blocks_extent + spill_total > kU32Domain ||
            cursor > kU32Domain ||
            cursor + blocks_extent + spill_total > kU32Domain) {
            add_diag(result,
                     core_wasm_diag::kBinaryOverflow,
                     "a frame bridge control-block stride or spill span exceeds the wasm32 "
                     "uint32 coordinate domain");
            return std::nullopt;
        }
        if (cursor + blocks_extent + spill_total >
            ir::core::kCoreWasmFixedLinearMemoryCapacityBytes) {
            add_diag(result,
                     core_wasm_diag::kResourceExhausted,
                     "the frame bridge control blocks and scalar/PtrLen spill slots exceed the "
                     "fixed 64 KiB linear-memory page");
            return std::nullopt;
        }
        const std::uint32_t control_base = static_cast<std::uint32_t>(cursor);
        cursor += blocks_extent;
        const std::uint32_t spill_base = static_cast<std::uint32_t>(cursor);
        const std::uint32_t spill_extent = static_cast<std::uint32_t>(spill_total);
        cursor += spill_total;
        plan.bridge_control_base = control_base;
        plan.bridge_block_stride = static_cast<std::uint32_t>(block_stride);
        plan.bridge_control_extent =
            static_cast<std::uint32_t>(blocks_extent + spill_total);
        plan.bridge_spill_base = spill_base;
        plan.bridge_spill_extent = spill_extent;

        // Resolve the projected capability records by source SymbolId (index
        // identity never trusted from the call statement).
        std::unordered_map<std::uint64_t, const ir::core::CoreWireCapabilitySchema *>
            wire_by_symbol;
        for (const ir::core::CoreWireCapabilitySchema &schema :
             frame_wire_table->capabilities) {
            wire_by_symbol.emplace(schema.source_symbol, &schema);
        }

        std::uint32_t running_spill = 0;
        plan.bridge_call_sites.reserve(bridge_calls.size());
        for (std::uint32_t index = 0; index < bridge_calls.size(); ++index) {
            BridgeCallPlan &site = bridge_calls[index];
            const CoreCapabilityDecl &capability = program.capabilities[site.capability.value];
            if (!capability.symbol_ref.id.has_value()) {
                add_diag(result, core_wasm_diag::kInvalidCapabilityAbi,
                         "a frame bridge capability has no resolved SymbolId");
                return std::nullopt;
            }
            const auto wire_it = wire_by_symbol.find(*capability.symbol_ref.id);
            if (wire_it == wire_by_symbol.end()) {
                add_diag(result, core_wasm_diag::kInvalidCapabilityAbi,
                         "a frame bridge capability is missing from the projected wire-schema "
                         "table");
                return std::nullopt;
            }
            const ir::core::CoreWireCapabilitySchema &wire = *wire_it->second;
            if (wire.params.size() != site.param_vt.size()) {
                add_diag(result, core_wasm_diag::kInvalidCapabilityAbi,
                         "a frame bridge call arity disagrees with the wire-schema parameter count");
                return std::nullopt;
            }

            ir::core::CoreFrameBridgeCallSite record;
            record.call_site_id = site.call_site_id;
            record.source_symbol = *capability.symbol_ref.id;
            record.arity = static_cast<std::uint32_t>(site.param_vt.size());
            {
                const std::uint64_t block_offset =
                    static_cast<std::uint64_t>(index) * block_stride;
                const std::uint64_t aligned_site_spill = align8(site.spill_bytes);
                const std::uint64_t next_running_spill =
                    static_cast<std::uint64_t>(running_spill) + aligned_site_spill;
                if (block_offset > kU32Domain || next_running_spill > spill_total) {
                    add_diag(result,
                             core_wasm_diag::kBinaryOverflow,
                             "a frame bridge call-site block or spill coordinate exceeds the "
                             "wasm32 uint32 coordinate domain");
                    return std::nullopt;
                }
                site.block_offset = static_cast<std::uint32_t>(block_offset);
                site.spill_base = spill_base + running_spill;
                running_spill = static_cast<std::uint32_t>(next_running_spill);
            }
            record.block_offset = site.block_offset;
            // The site's private scalar/PtrLen spill window (v3 section): the
            // host membership-tests every spilled descriptor against exactly
            // this [base, +extent) span.
            record.spill_base = site.spill_base;
            record.spill_extent =
                static_cast<std::uint32_t>(align8(site.spill_bytes));
            record.param_layouts.reserve(site.param_vt.size());
            for (std::uint32_t p = 0; p < site.param_vt.size(); ++p) {
                const ir::core::CoreLayoutId full_param =
                    layouts.value_layouts[site.param_vt[p].value];
                const ir::core::CoreLayoutId dense_param =
                    builder.emit_fixed(full_param, /*from_input=*/false);
                if (builder.failed()) {
                    add_diag(result, core_wasm_diag::kUnsupportedCapabilityFrame,
                             "a frame bridge argument layout is not finalizable into the frame "
                             "table (a nested bounded collection is outside the frame subset)");
                    return std::nullopt;
                }
                const auto param_diags =
                    ir::core::verify_frame_layout_wire_consistency(
                        builder.table_ref(), dense_param, *frame_wire_table,
                        wire.params[p]);
                if (!param_diags.empty()) {
                    add_diag(result, core_wasm_diag::kUnsupportedCapabilityFrame,
                             "a frame bridge argument is not representable on the wire frame "
                             "subset (map/f64/decimal/duration/timestamp/uuid shapes stay "
                             "fail-closed)");
                    return std::nullopt;
                }
                record.param_layouts.push_back(dense_param);
            }
            const ir::core::CoreLayoutId full_result =
                layouts.value_layouts[site.result_vt.value];
            const ir::core::CoreLayoutId dense_result =
                builder.emit_fixed(full_result, /*from_input=*/false);
            if (builder.failed()) {
                add_diag(result, core_wasm_diag::kUnsupportedCapabilityFrame,
                         "a frame bridge result layout is not finalizable into the frame table");
                return std::nullopt;
            }
            const auto result_diags =
                ir::core::verify_frame_layout_wire_consistency(
                    builder.table_ref(), dense_result, *frame_wire_table,
                    wire.result);
            if (!result_diags.empty()) {
                add_diag(result, core_wasm_diag::kUnsupportedCapabilityFrame,
                         "a frame bridge result is not representable on the wire frame subset");
                return std::nullopt;
            }
            record.result_layout = dense_result;

            // Disjoint result placement + its String payload arena on the sum
            // cursor. The result extent is the aligned layout root size; the
            // payload arena covers bounded String upper bounds plus the shared
            // unbounded pool (D6).
            const ir::core::CoreLayout &result_layout =
                builder.table_ref().layouts[dense_result.value];
            const std::uint64_t result_extent = align8(result_layout.size);
            if (result_extent == 0 || cursor > kU32Max ||
                cursor + result_extent > ir::core::kCoreWasmFixedLinearMemoryCapacityBytes) {
                add_diag(result, core_wasm_diag::kResourceExhausted,
                         "a frame bridge result placement exceeds the fixed 64 KiB linear-memory "
                         "page");
                return std::nullopt;
            }
            site.result_base = static_cast<std::uint32_t>(cursor);
            record.result_base = site.result_base;
            record.result_extent = static_cast<std::uint32_t>(result_extent);
            cursor += result_extent;
            std::uint64_t result_bounded = 0;
            bool result_unbounded = false;
            if (!accumulate_input_string_arena(builder.table_ref(),
                                               dense_result, *frame_wire_table,
                                               wire.result, result_bounded,
                                               result_unbounded)) {
                add_diag(result, core_wasm_diag::kInvalidLayout,
                         "the frame bridge result layout and wire schema disagree on a String "
                         "slot");
                return std::nullopt;
            }
            std::uint64_t payload_capacity = align8(result_bounded);
            if (result_unbounded) {
                payload_capacity += kP6FrameStringPoolBytes;
            }
            cursor = align8(cursor);
            if (cursor + payload_capacity > ir::core::kCoreWasmFixedLinearMemoryCapacityBytes) {
                add_diag(result, core_wasm_diag::kResourceExhausted,
                         "a frame bridge result String payload arena exceeds the fixed 64 KiB "
                         "linear-memory page");
                return std::nullopt;
            }
            record.result_payload_base = static_cast<std::uint32_t>(cursor);
            record.result_payload_capacity =
                static_cast<std::uint32_t>(payload_capacity);
            cursor += payload_capacity;

            plan.bridge_call_sites.push_back(std::move(record));
        }
        // Install the page-frame coordinates the handler emit reads.
        bridge_registry->install_coordinates(control_base,
                                             static_cast<std::uint32_t>(block_stride),
                                             spill_base);
    }
    // Finalize the self-contained dense table after every boundary root and
    // bridge argument/result root has been emitted.
    plan.table = builder.take_table();
    return plan;
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
    std::optional<CoreTypeId> frame_input_nominal,
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
            if (word == std::nullopt || *word == P6ScalarKind::String) {
                add_diag(result,
                         core_wasm_diag::kUnsupportedOrchestration,
                         "fn '" + fn.name +
                             "' crosses a boundary with a non-representable parameter "
                             "(String PtrLen / f64 / multi-word types are rejected)");
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
        // Design section 6.2: the heap base must also clear the packed input
        // containers' DISJOINT per-edge backing placements and the frame-payload
        // arena, because input containers stay live for the whole run. The floor
        // is the same sum-of-prior-backing high-water the emitted
        // ahfl.core-layout.v1 section carries (single SSOT); the legacy
        // max(base + backing_size) shortcut would relocate the heap into a
        // second simultaneously-live input container's backing. The predicate
        // mirrors frame-lane eligibility in emit_core_wasm: no capability
        // import, no outlined fn, no closure, and an entry handler that
        // projects the raw input frame. At rung E that predicate implies the
        // heap stays disabled (frame agents carry no fns/closures), so this is
        // byte-neutral today and becomes load-bearing if a frame agent is ever
        // allowed outlined-fn constructs.
        const bool frame_packed_input =
            (imports == nullptr || imports->empty()) && planned_fns.empty() &&
            closure_table_out.empty() && frame_input_nominal.has_value() &&
            std::any_of(entry_builders.begin(),
                        entry_builders.end(),
                        [](const P6ComputationHandlerBuilder *builder) {
                            return builder != nullptr && builder->reads_raw_input_frame();
                        });
        std::uint64_t backing_high = ir::core::kP6CollectionBackingBase;
        if (frame_packed_input) {
            backing_high =
                input_frame_backing_high_water(program, layouts, *frame_input_nominal);
        } else {
            // Legacy lane (identity/capability/FB): every container layout
            // names the same region-relative base, so the high-water is the
            // max(base + backing_size) over the storages reachable in a step.
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
    //
    // RFC 0026 P6-7 frame-bridge v2 rung V2-A: a COMPUTED FINAL region (if-let
    // lowers to a match ending in a value return) is the second region kind that
    // legitimately owns a non-empty pattern arena.
    // RFC 0026 P6-7 frame-bridge v2 rung V2-C fix-forward: the ANY-of admits
    // not only a pure computation region and a computed-final region but ALSO
    // a BRIDGE computation region — a region that contains both a match and an
    // ordered bridge statement ("route then call"). The predicate must carry
    // the same policy.allow_bridge the per-handler region gate below uses, or
    // that handler is rejected here with the misleading hidden-arena diagnostic
    // despite being admitted by the D2/D3 region subset.
    if (!flow->storage.patterns.empty() &&
        !std::any_of(
            flow->states.begin(), flow->states.end(), [&](const ir::core::CoreFlowState &state) {
                return region_contains_match(state.body) &&
                       (is_p6_computation_region(state.body, policy.allow_bridge) ||
                        is_p6_computed_final_region(state.body));
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
    // arena uses, BEFORE any handler byte is emitted. A V2-D relocated build
    // gates against the packaged instance's node-block sub-spans (each sized by
    // the D6 capacity family from these same finalized layouts) instead of the
    // direct-agent fixed regions.
    const std::uint32_t effective_input_capacity =
        policy.frame_relocation != nullptr ? policy.frame_relocation->input_capacity
                                           : kP6AggregateInputCapacity;
    const std::uint32_t effective_context_capacity =
        policy.frame_relocation != nullptr ? policy.frame_relocation->context_capacity
                                           : kP6AggregateContextCapacity;
    if (!fits_frame_region(
            program, layouts, agent.input_type, effective_input_capacity, "input", result)) {
        return std::nullopt;
    }
    if (agent.context_kind == CoreAgentDecl::ContextKind::Struct &&
        !fits_frame_region(
            program, layouts, agent.context_type, effective_context_capacity, "context", result)) {
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
        // V2-A: a COMPUTED FINAL handler materializes into the output frame and
        // is published as ComputedReturnAction rather than ComputedGotoAction.
        bool is_final_return{false};
    };
    std::vector<PlannedComputedHandler> planned_handlers;

    // RFC 0026 P6-7 frame-bridge v2 D1 (rung V2-B): the module-wide String
    // literal pool every ENTRY-HANDLER builder interns into while planning.
    // It is frozen before handler bodies are emitted and, when non-empty, the
    // module gains exactly ONE active Data(11) section initializing the rodata
    // region. Outlined fn builders never see it (a String crosses no fn
    // boundary), so an fn String literal keeps failing closed.
    // V2-D: a workflow-module relocated build shares ONE pool across every
    // packaged agent (the packager pre-freezes the union before emission); the
    // build then neither owns nor moves it.
    RodataLiteralPool owned_rodata_pool;
    RodataLiteralPool &rodata_pool =
        policy.shared_rodata_pool != nullptr ? *policy.shared_rodata_pool
                                             : owned_rodata_pool;

    // RFC 0026 P6-7 frame-bridge v2 D3/D4 (rung V2-C): the module-wide bridge
    // call-site registry every frame-lane HANDLER builder registers into. It is
    // populated during planning and physically laid out (control blocks,
    // disjoint result placements) before handler bodies are emitted.
    BridgeCallRegistry bridge_registry;
    // V2-D emission half 2: a relocated workflow build records every bridge
    // site into the packager's shared dense registry; a direct-agent (or
    // fact-gathering) build keeps its private one.
    BridgeCallRegistry &effective_bridge_registry =
        policy.shared_bridge_registry != nullptr ? *policy.shared_bridge_registry
                                                 : bridge_registry;
    if (policy.shared_bridge_registry != nullptr) {
        effective_bridge_registry.begin_workflow_runner(policy.wf_runner);
    }

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
            if (contains_capability) {
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
                continue;
            }
            // RFC 0026 P6-7 frame-bridge v2 rung V2-A: a capability-free final
            // is either the canonical identity passthrough (v1) or a COMPUTED
            // FINAL — a P6-subset region ending in a value-bearing return that
            // materializes into the fixed output frame. The exact identity
            // shape is routed first so the v1 byte path and diagnostic survive;
            // every other return-bearing region takes the computed-final lane.
            const bool identity_shape =
                is_identity_final_shape(agent, *flow, handler->body);
            const bool computed_final_shape =
                !identity_shape &&
                is_p6_computed_final_region(handler->body) &&
                p6_region_always_returns(handler->body);
            const std::optional<CoreValueTypeId> output_vt =
                p6_nominal_value_type(program, agent.output_type);
            const bool output_layout_ready =
                output_vt.has_value() && has_finalized_layout(layouts, *output_vt);
            if (computed_final_shape && output_layout_ready) {
                if (!policy.allow_computed_goto) {
                    add_diag(result,
                             policy.unsupported_code,
                             "KR6.5 " + std::string(policy.slice) +
                                 " does not yet package a P6 computed-final handler",
                             statements.front().source_range);
                    return std::nullopt;
                }
                // Design D5 output fit gate: a computed final materializes the
                // output nominal into its OWN fixed region
                // (kP6AggregateOutputBase 12288, cap 4096). The output nominal
                // may be larger than the input, so it is gated independently of
                // the 3072-byte input region before any handler byte is emitted.
                // A V2-D relocated build gates against the packaged instance's
                // O_k node-block sub-span instead.
                const std::uint32_t effective_output_capacity =
                    policy.frame_relocation != nullptr
                        ? policy.frame_relocation->output_capacity
                        : kP6AggregateOutputCapacity;
                if (!fits_frame_region(program,
                                       layouts,
                                       agent.output_type,
                                       effective_output_capacity,
                                       "output",
                                       result)) {
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
                builder->enable_computed_final(*output_vt, state);
                builder->install_rodata_pool(&rodata_pool);
                if (policy.workflow_packaging_lane) {
                    builder->mark_workflow_packaging_lane();
                }
                if (policy.admit_normalized_entry_frame) {
                    builder->admit_normalized_entry_frame();
                }
                if (!builder->plan()) {
                    return std::nullopt;
                }
                // A computed final never goto-branches: it yields its own state
                // id after materializing the output. The successor table is
                // therefore empty. Read the raw-frame fact BEFORE moving the
                // builder (aggregate-initializer evaluation order is
                // unspecified relative to the move).
                const bool final_reads_raw = builder->reads_raw_input_frame();
                planned_handlers.push_back(PlannedComputedHandler{
                    state, std::move(builder), {}, final_reads_raw,
                    /*is_final_return=*/true});
                continue;
            }
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
            if (!handler->body.statements.empty() &&
                is_p6_computation_region(handler->body, policy.allow_bridge)) {
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
                // V2-C: a direct-agent frame handler may plan ordered capability
                // bridge calls; the workflow packaging policy keeps this off. The
                // registry (and the rodata pool) is installed ONLY into a handler
                // whose region actually reaches a capability statement: a pure
                // goto handler stays on the closed V2-A lane, so a String field
                // read there keeps failing closed instead of being silently
                // admitted merely because the MODULE has a bridge elsewhere.
                // Reaching a capability in an allow_bridge builder is what makes
                // this a bridge handler; it also owns the shared rodata pool so a
                // computed continuation can compare a bridge result against a
                // String literal straight out of the Data region.
                if (policy.allow_bridge &&
                    region_contains_capability(handler->body)) {
                    builder->install_bridge_registry(&effective_bridge_registry,
                                                     CoreStateId{state});
                    builder->install_rodata_pool(&rodata_pool);
                }
                if (policy.workflow_packaging_lane) {
                    builder->mark_workflow_packaging_lane();
                }
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
    //
    // V2-C fix-forward: a planned computed handler's ComputedGotoAction is
    // published into plan.actions only during the emit loop BELOW. Reading
    // plan.actions here would therefore see the default IdentityAction for
    // every computed handler: a state reached only through another computed
    // handler would be classified unreachable, its dense bridge call sites
    // filtered out (and its capability dropped from plan.imports), while its
    // emitted bytes still invoked the bridge at a dense id the filtered table
    // no longer contained (an OOB sites()[id] and a host-rejected module).
    // Overlay the already-planned {state -> targets} table so the acyclic /
    // reachability walks see the exact successor graph the emit loop will
    // eventually publish.
    const auto planned_successors = [&](std::uint32_t state)
        -> const std::vector<CoreStateId> * {
        for (const PlannedComputedHandler &planned : planned_handlers) {
            if (planned.state == state && !planned.is_final_return) {
                return &planned.targets;
            }
        }
        return nullptr;
    };
    const auto successors = [&](std::uint32_t state) -> std::vector<std::uint32_t> {
        if (const std::vector<CoreStateId> *targets = planned_successors(state)) {
            std::vector<std::uint32_t> out;
            out.reserve(targets->size());
            for (const CoreStateId target : *targets) {
                out.push_back(target.value);
            }
            return out;
        }
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
                              [](const ComputedReturnAction &) {
                                  return std::vector<std::uint32_t>{};
                              },
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

    // RFC 0026 P6-7 frame-bridge v2 rung V2-A: a computed final is a P6-frame
    // contract. The runv descriptor names exactly ONE authorized value_ptr base
    // (input for identity finals, output for computed finals), so one agent
    // cannot mix the two final kinds; a capability final likewise stays on the
    // opaque run2 lane and cannot share an agent with a computed final (the raw
    // P4-D and wire-JSON lanes never mix; in-handler bridging is the V2-C rung).
    const bool has_planned_computed_final =
        std::any_of(planned_handlers.begin(), planned_handlers.end(),
                    [](const PlannedComputedHandler &planned) {
                        return planned.is_final_return;
                    });
    if (has_planned_computed_final) {
        // Computed-final actions are published only after the fn fixed point, so
        // identify them from the planned-handler state set; any other declared
        // final took the identity or capability path and mixing is illegal.
        std::vector<std::uint8_t> computed_final_states(plan.actions.size(), 0);
        for (const PlannedComputedHandler &planned : planned_handlers) {
            if (planned.is_final_return) {
                computed_final_states[planned.state] = 1;
            }
        }
        for (const CoreStateId final : agent.finals) {
            if (!computed_final_states[final.value]) {
                add_diag(result,
                         core_wasm_diag::kUnsupportedCapabilityFrame,
                         "a computed final cannot share an agent with an identity or capability "
                         "final (the P6-7 frame lane has one final kind per module)");
                return std::nullopt;
            }
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
    // V2-C fix-forward: now that reachability is exact (it sees the planned
    // computed-handler targets), compact the planned handlers DOWN to the
    // reachable set BEFORE any bridge-site table, import table, fn fixed point
    // or frame section is built. Previously every planned handler — including
    // ones no run can enter — was emitted: its body bytes were dead code in the
    // dense handler function index space, its String literals pinned rodata
    // space, and (the P0 defect) its bridge statements had reserved dense call
    // sites and capability imports that the reachability filter then deleted,
    // leaving retained statements indexing a shorter table. A reachable
    // computed chain now owns contiguous handler ordinals and the full bridge
    // table.
    const std::size_t planned_before_compaction = planned_handlers.size();
    std::erase_if(planned_handlers, [&](const PlannedComputedHandler &planned) {
        return !reachable_state[planned.state];
    });
    if (planned_handlers.size() != planned_before_compaction) {
        // Rebuild the rodata pool from the surviving builders only (their
        // bytes were validated against length bounds and pool capacity during
        // planning, so a subset cannot fail either; the bool is defensive).
        std::vector<std::string> surviving_literals;
        for (const PlannedComputedHandler &planned : planned_handlers) {
            const std::vector<std::string> &literals =
                planned.builder->rodata_literals();
            surviving_literals.insert(surviving_literals.end(),
                                      literals.begin(), literals.end());
        }
        if (!rodata_pool.rebuild_from_literals(surviving_literals)) {
            add_diag(result,
                     core_wasm_diag::kResourceExhausted,
                     "the reachable-handler String literal pool exceeds its reserved rodata "
                     "region in the fixed 64 KiB linear-memory page");
            return std::nullopt;
        }
    }

    // RFC 0026 P6-7 frame-bridge v2 D3/D4 (rung V2-C): every REACHABLE planned
    // bridge call site contributes its capability import and survives into the
    // dense call-site table; a call planned in a handler reachability proved
    // dead is filtered out and the surviving sites are renumbered to a dense
    // prefix so their block coordinates stay contiguous. The surviving
    // builders' retained statements are remapped to the new ids and every
    // retained id is REQUIRED to resolve within the dense table — an emitted
    // bridge statement must never index sites() out of bounds.
    //
    // V2-D emission half 2: a relocated WORKFLOW build shares one dense
    // registry across every packaged runner. Its sites are compacted as a
    // contiguous per-runner tail onto GLOBAL dense ids (the prior runners'
    // sites keep theirs), the per-runner remap is forwarded to this build's
    // builders, and the module-global control-block / spill coordinates are
    // installed before the handler EMIT pass. The AgentPlan keeps no private
    // site/import copies; the packager flattens the shared registry once after
    // every runner is built.
    const bool workflow_bridge_lane = policy.shared_bridge_registry != nullptr;
    if (workflow_bridge_lane &&
        effective_bridge_registry.site_count_for_runner(policy.wf_runner) != 0) {
        effective_bridge_registry.install_coordinates(
            policy.wf_control_base, policy.wf_block_stride,
            policy.wf_runner_spill_base);
        auto remap = effective_bridge_registry.compact_workflow_runner(
            policy.wf_runner, reachable_state,
            policy.wf_site_id_base, policy.wf_runner_spill_base);
        if (!remap.has_value()) {
            add_diag(result,
                     core_wasm_diag::kInvalidCapabilityAbi,
                     "a reachable workflow frame bridge statement lost its dense call site "
                     "during reachability filtering (the shared bridge call-site table is "
                     "inconsistent with the handler that planned it)");
            return std::nullopt;
        }
        const std::size_t global_site_count = effective_bridge_registry.size();
        for (const PlannedComputedHandler &planned : planned_handlers) {
            if (!planned.builder->remap_bridge_call_ids(*remap, global_site_count)) {
                add_diag(result,
                         core_wasm_diag::kInvalidCapabilityAbi,
                         "a reachable workflow frame bridge statement lost its dense global "
                         "call site during reachability filtering");
                return std::nullopt;
            }
        }
    } else if (!workflow_bridge_lane && !bridge_registry.empty()) {
        std::vector<BridgeCallPlan> &all_sites = bridge_registry.sites();
        std::vector<BridgeCallPlan> reachable_sites;
        reachable_sites.reserve(all_sites.size());
        for (const BridgeCallPlan &site : all_sites) {
            if (reachable_state[site.state.value]) {
                reachable_sites.push_back(site);
            }
        }
        std::unordered_map<std::uint32_t, std::uint32_t> remap;
        for (std::uint32_t new_id = 0; new_id < reachable_sites.size(); ++new_id) {
            remap.emplace(reachable_sites[new_id].call_site_id, new_id);
            reachable_sites[new_id].call_site_id = new_id;
        }
        for (const PlannedComputedHandler &planned : planned_handlers) {
            if (!planned.builder->remap_bridge_call_ids(
                    remap, reachable_sites.size())) {
                add_diag(result,
                         core_wasm_diag::kInvalidCapabilityAbi,
                         "a reachable frame bridge statement lost its dense call site during "
                         "reachability filtering (the bridge call-site table is inconsistent "
                         "with the handler that planned it)");
                return std::nullopt;
            }
        }
        all_sites = std::move(reachable_sites);
        for (const BridgeCallPlan &site : all_sites) {
            plan.imports.push_back(site.capability);
        }
        plan.bridge_calls = all_sites;
    }
    std::sort(plan.imports.begin(), plan.imports.end(), [](auto lhs, auto rhs) {
        return lhs.value < rhs.value;
    });
    plan.imports.erase(std::unique(plan.imports.begin(), plan.imports.end()), plan.imports.end());
    // The opaque capability-FINAL lane admits at most one REACHABLE capability
    // final (the E2 least-privilege rule; a capability final on an unreachable
    // state is simply not imported). A frame bridge module (observed through
    // runv) can never also reach such a final — runv has no capability-final
    // arm — so a reachable bridge call and a reachable capability final in one
    // agent is rejected explicitly.
    std::uint32_t reachable_capability_finals = 0;
    for (std::uint32_t state = 0; state < plan.actions.size(); ++state) {
        if (reachable_state[state] &&
            std::holds_alternative<CapabilityAction>(plan.actions[state])) {
            ++reachable_capability_finals;
        }
    }
    if (reachable_capability_finals > 1) {
        add_diag(result,
                 core_wasm_diag::kUnsupportedCapabilityFrame,
                 "KR6.5 " + std::string(policy.slice) +
                     " capability-final graph reaches more than one opaque capability final");
        return std::nullopt;
    }
    const bool this_build_has_bridge =
        workflow_bridge_lane
            ? effective_bridge_registry.site_count_for_runner(policy.wf_runner) != 0
            : !bridge_registry.empty();
    if (reachable_capability_finals == 1 && this_build_has_bridge) {
        add_diag(result,
                 core_wasm_diag::kUnsupportedCapabilityFrame,
                 "a frame bridge agent cannot share its runv lane with an opaque capability "
                 "final (the P6-7 frame lane has one terminal observation kind per module)");
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

    // V2-D emission half 2: a relocated workflow build emits at the WORKFLOW
    // module's global sorted-unique import ordinals (one low table shared by
    // every packaged runner), never a per-agent table. Replacing the local
    // table after the fn-import merge keeps every downstream builder
    // (install_import_table) and the opaque-import gate on global ordinals.
    if (workflow_bridge_lane && policy.wf_imports != nullptr) {
        plan.imports = *policy.wf_imports;
    }
    // The capabilities THIS runner reaches (on the workflow lane the global
    // table legitimately contains later runners' capabilities, which must not
    // be judged as opaque imports of this runner).
    std::vector<CoreCapabilityId> runner_reachable_imports;
    if (workflow_bridge_lane) {
        for (const BridgeCallPlan &site : effective_bridge_registry.sites()) {
            if (site.runner == policy.wf_runner) {
                runner_reachable_imports.push_back(site.capability);
            }
        }
        std::sort(runner_reachable_imports.begin(), runner_reachable_imports.end(),
                  [](auto lhs, auto rhs) { return lhs.value < rhs.value; });
        runner_reachable_imports.erase(
            std::unique(runner_reachable_imports.begin(),
                        runner_reachable_imports.end()),
            runner_reachable_imports.end());
    }

    // V2-A fail-closed gate: a computed final that ALSO reaches an OPAQUE
    // capability final (or an effectful outlined fn) is the forbidden
    // raw-P4-D/wire-JSON mix. V2-C lifts this for the FRAME BRIDGE: a
    // computed-final agent may reach in-handler bridge capabilities, whose
    // results live in the same disjoint frame placements and are materialized
    // through runv. The remaining opaque imports must therefore all be
    // bridge-mode. On the V2-D workflow lane only THIS runner's reached
    // capabilities are tested (a later runner's site cannot legitimize or
    // incriminate an import here); a capability the runner bridges is found
    // through the per-runner site predicate.
    if (has_planned_computed_final) {
        const std::span<const CoreCapabilityId> checked_imports =
            workflow_bridge_lane ? std::span<const CoreCapabilityId>{runner_reachable_imports}
                                 : std::span<const CoreCapabilityId>{plan.imports};
        const bool opaque_imports =
            std::any_of(checked_imports.begin(), checked_imports.end(),
                        [&](CoreCapabilityId id) {
                            return workflow_bridge_lane
                                ? !effective_bridge_registry.uses_capability_for_runner(
                                      id, policy.wf_runner)
                                : !bridge_registry.uses_capability(id);
                        });
        if (opaque_imports) {
            add_diag(result,
                     core_wasm_diag::kUnsupportedCapabilityFrame,
                     "a computed-final agent cannot reach an opaque capability on the P6-7 "
                     "frame lane (raw P4-D finals mix only with frame-bridge capabilities, "
                     "never wire-JSON tuple finals)");
            return std::nullopt;
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
                                     agent.input_type,
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

    // V2-B: every reachable entry-handler String literal has been interned;
    // freeze the deterministic pool (byte-sorted, 8-aligned) before any body is
    // emitted so PtrLen immediates read stable offsets. The frozen image moves
    // onto the plan for the Data(11) section emitter. A V2-D relocated build
    // shares the workflow module's PRE-FROZEN pool, so it neither refreezes nor
    // owns the image.
    const bool owns_rodata_pool = policy.shared_rodata_pool == nullptr;
    if (owns_rodata_pool && !rodata_pool.empty()) {
        rodata_pool.freeze();
        plan.rodata_extent = rodata_pool.extent();
    } else if (!owns_rodata_pool) {
        plan.rodata_extent = rodata_pool.extent();
    }

    // RFC 0026 P6-7 frame-bridge v2 D3/D4 (rung V2-C): a module that projects
    // the raw P4-D input frame, materializes a computed final, OR carries an
    // in-handler capability bridge is a P6-frame module. Build its physical
    // section plan HERE (after the import table and the reachable bridge sites
    // are fixed, before handler emission), because the bridge emit reads the
    // control-block / spill coordinates the physical planner installs. The
    // handler ACTIONS (and plan.has_computed_final / reads_raw_input_frame) are
    // published only during the emit loop below, so derive the predicates from
    // the already-planned handlers here.
    const bool planned_computed_final =
        std::any_of(planned_handlers.begin(), planned_handlers.end(),
                    [](const PlannedComputedHandler &planned) {
                        return planned.is_final_return;
                    });
    const bool planned_raw_read =
        std::any_of(planned_handlers.begin(), planned_handlers.end(),
                    [](const PlannedComputedHandler &planned) {
                        return planned.builder->reads_raw_input_frame();
                    });
    const bool this_build_bridges =
        workflow_bridge_lane
            ? effective_bridge_registry.site_count_for_runner(policy.wf_runner) != 0
            : !bridge_registry.empty();
    const bool needs_frame_section =
        planned_raw_read || planned_computed_final || this_build_bridges;
    // A bridge call or a computed final REQUIRES the frame sections (there is
    // no sectionless observation for either). A raw-projecting agent whose
    // boundary is not yet wire-representable keeps the legacy sectionless
    // raw-frame fallback (its pre-P6-7 skip).
    const bool frame_section_required =
        planned_computed_final || this_build_bridges;
    const bool frame_lane_eligible =
        plan.fns.empty() && plan.closure_table.empty();
    // V2-D: a packaged agent keeps the same per-handler subset gates but the
    // WORKFLOW packager builds the module-level core-layout section; the agent
    // plan itself must not carry one. A packaged computed/bridge agent still
    // fails the fn/closure mix gate below.
    const bool build_agent_frame_section = !policy.skip_frame_section;
    if (needs_frame_section) {
        if (!frame_lane_eligible) {
            // An FB-lane agent (outlined fn / closure) may still project an
            // input field in a handler, but the v1 eligibility rule keeps it on
            // the opaque wire-JSON sectionless lane; only a REQUIRED frame
            // section (computed final / bridge) rejects here.
            if (frame_section_required) {
                add_diag(result,
                         core_wasm_diag::kUnsupportedCapabilityFrame,
                         "a P6-frame agent cannot mix the frame lane with outlined fns or "
                         "closures");
                return std::nullopt;
            }
        } else if (build_agent_frame_section) {
        const CoreAgentDecl &agent_decl = program.agents[target.value];
        auto candidate_boundary = [&]()
            -> std::optional<std::pair<ir::core::CoreValueTypeId,
                                       ir::core::CoreValueTypeId>> {
            auto input_vt = frame_boundary_value_type(program, agent_decl.input_type);
            auto output_vt = frame_boundary_value_type(program, agent_decl.output_type);
            if (!input_vt.has_value() || !output_vt.has_value()) {
                return std::nullopt;
            }
            return std::pair{*input_vt, *output_vt};
        }();
        if (!candidate_boundary.has_value()) {
            if (frame_section_required) {
                add_diag(result, core_wasm_diag::kInvalidLayout,
                         "a P6-frame agent boundary nominal has no finalized argument-less value "
                         "type");
                return std::nullopt;
            }
        } else {
            // The wire projection covers BOTH the capability imports and the
            // agent boundary roots, in one table, so the bridge argument/result
            // roots and the runv pack/encode roots share a single verified
            // schema.
            auto projection = ir::core::project_core_wire_schema(
                program, plan.imports, candidate_boundary);
            bool projection_ok = projection.ok() &&
                                 projection.table->frame_roots.has_value();
            bool physical_ok = false;
            std::optional<FrameSectionPlan> frame_section;
            const std::size_t planner_diags_before = result.diagnostics.size();
            if (projection_ok) {
                frame_section = build_frame_section_plan(
                    program, layouts, agent_decl, plan.rodata_extent,
                    &*projection.table, &bridge_registry, result);
                physical_ok = frame_section.has_value();
            }
            bool consistent = false;
            std::vector<ir::core::CoreLowerDiagnostic> bridge_site_diags;
            if (physical_ok) {
                const auto &roots = *projection.table->frame_roots;
                const auto layout_section =
                    frame_section_to_layout_section(*frame_section);
                consistent =
                    ir::core::verify_frame_layout_wire_consistency(
                        frame_section->table, frame_section->input_layout,
                        *projection.table, roots.input)
                        .empty() &&
                    ir::core::verify_frame_layout_wire_consistency(
                        frame_section->table, frame_section->output_layout,
                        *projection.table, roots.output)
                        .empty();
                if (consistent) {
                    bridge_site_diags = ir::core::verify_frame_bridge_sites(
                        layout_section, *projection.table);
                    consistent = bridge_site_diags.empty();
                }
            }
            if (projection_ok && physical_ok && consistent) {
                plan.frame_section =
                    std::make_unique<FrameSectionPlan>(std::move(*frame_section));
                plan.frame_wire_table = std::move(*projection.table);
                // The rodata pool itself moves onto the plan only AFTER every
                // handler body has been emitted below (the builders hold a
                // non-owning pointer to it).
            } else if (frame_section_required) {
                // Surface the real diagnostic when the physical planner or the
                // admission-side bridge-site verifier added one; otherwise give
                // the explicit frame rejection.
                for (const ir::core::CoreLowerDiagnostic &diag : bridge_site_diags) {
                    add_diag(result, core_wasm_diag::kUnsupportedCapabilityFrame,
                             diag.message);
                }
                if (result.diagnostics.empty()) {
                    add_diag(result,
                             core_wasm_diag::kUnsupportedCapabilityFrame,
                             "a P6-frame computed-final or capability-bridge agent is not "
                             "representable on the P6-7 frame lane (its boundary or capability "
                             "ABI must project to the v1 wire-schema frame roots)");
                }
                return std::nullopt;
            } else {
                // A raw-projecting-only agent (frame section not required)
                // normally keeps the legacy sectionless fallback when its
                // boundary does not project. But a HARD planner failure
                // (RESOURCE_EXHAUSTED: the fixed 64 KiB page genuinely cannot
                // hold the backing geometry) must NEVER downgrade to that
                // fallback — the whole point of the section is page
                // accounting, and emitting a sectionless module would hide a
                // real resource ceiling. Only an expressibility rejection
                // (unsupported-orchestration / unsupported-frame) is a soft
                // skip to the legacy lane.
                bool hard_planner_failure = false;
                for (std::size_t i = planner_diags_before;
                     i < result.diagnostics.size(); ++i) {
                    if (result.diagnostics[i].code ==
                        core_wasm_diag::kResourceExhausted) {
                        hard_planner_failure = true;
                        break;
                    }
                }
                if (hard_planner_failure) {
                    return std::nullopt;
                }
            }
            // A raw-projecting-only agent with an unprojectable boundary keeps
            // the legacy sectionless fallback: no frame plan is attached.
        }
        }
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
        const bool is_final_return = planned.is_final_return;
        std::vector<CoreStateId> targets = planned.targets;
        std::unique_ptr<P6ComputationHandlerBuilder> builder = std::move(planned.builder);
        builder->set_fn_call_tables(&instance_to_ordinal, &handler_fn_base);
        builder->install_closure_tables(fn_to_table_slot, entry_closure_type_index);
        // V2-C: a frame handler's bridge call emits the ahfl_cap import at its
        // sorted-table ordinal; fn handlers already receive this in the
        // reachable-fn driver.
        builder->install_import_table(&plan.imports);
        // V2-D: relocate this handler's fixed frame regions and state globals
        // onto a packaged instance's node-frame block inside the workflow
        // module (no-op on the direct-agent lane).
        if (policy.frame_relocation != nullptr) {
            builder->install_frame_relocation(*policy.frame_relocation);
        }
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
        // V2-D: the workflow packager sizes the per-instance scratch node block
        // from the maximum construct-scratch high-water across handlers.
        plan.p6_scratch_high =
            std::max(plan.p6_scratch_high, builder->scratch_high_water());
        plan.handlers[index] = CompiledHandler{std::move(*body)};
        if (is_final_return) {
            // V2-A: the handler materialized the output frame; runv invokes it
            // directly and returns the output base. It never goto-branches.
            plan.actions[state] = ComputedReturnAction{index};
            plan.has_computed_final = true;
        } else {
            plan.actions[state] = ComputedGotoAction{index, std::move(targets)};
            plan.has_computed_goto = true;
        }
        if (reads_raw) {
            plan.reads_raw_input_frame = true;
        }
    }
    // V2-B: move the frozen pool onto the plan only after the last handler
    // body was emitted (the builders hold non-owning pointers into it). A V2-D
    // relocated build shares the workflow module pool and leaves it in place.
    if (owns_rodata_pool && plan.rodata_extent != 0) {
        plan.rodata = std::move(rodata_pool);
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

[[nodiscard]] std::optional<WorkflowRegionPlan>
validate_workflow_region(const CoreProgram &program,
                         const CoreWorkflowDecl &workflow,
                         const ir::core::CoreLayoutTable &layouts,
                         const ir::core::CoreRegion *region,
                         CoreValueTypeId expected_type,
                         std::optional<CoreWorkflowNodeId> owner_node,
                         const std::vector<std::uint32_t> &schedule_position,
                         CoreWasmCodegenResult &result) {
    if (region == nullptr || region->statements.size() < 2) {
        add_diag(result,
                 core_wasm_diag::kUnsupportedWorkflowFrame,
                 "KR6.5 E3 workflow frame region must contain at least one path let and a yield");
        return std::nullopt;
    }
    const CoreStmt &yield_statement = region->statements.back();
    const auto *yield = std::get_if<CoreYieldStmt>(&yield_statement.node);
    if (yield == nullptr || !yield->has_value) {
        add_diag(result,
                 core_wasm_diag::kUnsupportedWorkflowFrame,
                 "a workflow frame region must end in a value yield",
                 yield_statement.source_range);
        return std::nullopt;
    }
    // Every preceding statement must be one value-binding let.
    std::vector<const CoreLetStmt *> lets;
    lets.reserve(region->statements.size() - 1);
    for (std::size_t i = 0; i + 1 < region->statements.size(); ++i) {
        const auto *let = std::get_if<CoreLetStmt>(&region->statements[i].node);
        if (let == nullptr) {
            add_diag(result,
                     core_wasm_diag::kUnsupportedWorkflowFrame,
                     "a workflow frame region must be straight-line ANF lets followed by one yield",
                     region->statements[i].source_range);
            return std::nullopt;
        }
        lets.push_back(let);
    }
    // Map each bound value id to its let (single static definition in the
    // workflow storage) and mark statement order for dependency checks.
    std::unordered_map<std::uint32_t, const CoreLetStmt *> let_by_value;
    std::unordered_map<std::uint32_t, std::size_t> let_order;
    for (std::size_t i = 0; i < lets.size(); ++i) {
        if (lets[i]->expr.value >= workflow.storage.exprs.size() ||
            lets[i]->result.value >= workflow.storage.value_types.size()) {
            add_diag(result,
                     core_wasm_diag::kInvalidCore,
                     "a workflow frame region let is out of the workflow storage range");
            return std::nullopt;
        }
        if (!let_by_value.emplace(lets[i]->result.value, lets[i]).second) {
            add_diag(result,
                     core_wasm_diag::kUnsupportedWorkflowFrame,
                     "a workflow frame value is bound more than once in one region");
            return std::nullopt;
        }
        let_order.emplace(lets[i]->result.value, i);
    }
    const auto yield_let_it = let_by_value.find(yield->value.value);
    if (yield_let_it == let_by_value.end()) {
        add_diag(result,
                 core_wasm_diag::kUnsupportedWorkflowFrame,
                 "the workflow frame yield value is not bound by a region let");
        return std::nullopt;
    }
    if (workflow.storage.value_types[yield->value.value] != expected_type) {
        add_diag(result,
                 core_wasm_diag::kUnsupportedWorkflowFrame,
                 "the workflow frame yield type does not match its declared boundary type");
        return std::nullopt;
    }

    // Validate one let expression: an exact bare path, a projected node-output
    // path, or a flat aggregate construct whose operands are already bound.
    auto check_path = [&](const CorePathExpr &path,
                          CoreValueTypeId result_vt) -> std::optional<WorkflowFrameSource> {
        if (!path.projection_resolved || path.has_local) {
            add_diag(result,
                     core_wasm_diag::kUnsupportedWorkflowFrame,
                     "a workflow frame path is unresolved or names a local");
            return std::nullopt;
        }
        if (path.root == ir::core::CorePathRoot::WorkflowInput) {
            if (!path.projection.empty()) {
                add_diag(result,
                         core_wasm_diag::kUnsupportedWorkflowFrame,
                         "a projected workflow-input field is outside the P6-7 V2-D frame subset");
                return std::nullopt;
            }
            if (path.root_type != workflow.input_type) {
                add_diag(result,
                         core_wasm_diag::kUnsupportedWorkflowFrame,
                         "a workflow input path does not identify the declared input shell");
                return std::nullopt;
            }
            return WorkflowFrameSource{WorkflowFrameSourceKind::Input, {}, result_vt};
        }
        if (path.root == ir::core::CorePathRoot::WorkflowNodeOutput) {
            if (path.workflow_node.value >= workflow.nodes.size()) {
                add_diag(result,
                         core_wasm_diag::kInvalidCore,
                         "a workflow frame path references an out-of-range node output");
                return std::nullopt;
            }
            const auto &source_node = workflow.nodes[path.workflow_node.value];
            const auto *source_instance = agent_instance(program, source_node.target_instance);
            const auto *source_payload =
                agent_instance_payload(program, source_node.target_instance);
            if (source_instance == nullptr || source_payload == nullptr ||
                source_instance->dispatch_types.size() != 3) {
                add_diag(result,
                         core_wasm_diag::kUnsupportedWorkflowFrame,
                         "a workflow node-output path has no packaged target instance");
                return std::nullopt;
            }
            if (path.root_type != source_payload->output_type) {
                add_diag(result,
                         core_wasm_diag::kUnsupportedWorkflowFrame,
                         "a workflow node-output frame does not match its target instance output");
                return std::nullopt;
            }
            if (owner_node.has_value() &&
                (!workflow_node_is_ancestor(workflow, *owner_node, path.workflow_node) ||
                 schedule_position[path.workflow_node.value] >=
                     schedule_position[owner_node->value])) {
                add_diag(result,
                         core_wasm_diag::kUnsupportedWorkflowFrame,
                         "a workflow frame node input reads an output that is not a scheduled ancestor");
                return std::nullopt;
            }
            if (path.projection.empty() &&
                result_vt != source_instance->dispatch_types[2]) {
                add_diag(result,
                         core_wasm_diag::kUnsupportedWorkflowFrame,
                         "a workflow node-output frame does not match its target instance output");
                return std::nullopt;
            }
            return WorkflowFrameSource{
                WorkflowFrameSourceKind::NodeOutput, path.workflow_node, result_vt};
        }
        add_diag(result,
                 core_wasm_diag::kUnsupportedWorkflowFrame,
                 "a workflow frame path root is neither workflow input nor a node output");
        return std::nullopt;
    };

    for (std::size_t i = 0; i < lets.size(); ++i) {
        const CoreLetStmt &let = *lets[i];
        const CoreExpr &expr = workflow.storage.exprs[let.expr.value];
        const CoreValueTypeId value_type = workflow.storage.value_types[let.result.value];
        if (expr.result_type != value_type) {
            add_diag(result,
                     core_wasm_diag::kInvalidCore,
                     "a workflow frame let result type disagrees with its expression type");
            return std::nullopt;
        }
        if (!has_finalized_layout(layouts, value_type)) {
            add_diag(result,
                     core_wasm_diag::kInvalidLayout,
                     "a workflow frame value has no finalized P4-D layout");
            return std::nullopt;
        }
        if (const auto *path = std::get_if<CorePathExpr>(&expr.node)) {
            auto source = check_path(*path, value_type);
            if (!source.has_value()) {
                return std::nullopt;
            }
            continue;
        }
        if (const auto *construct = std::get_if<CoreConstructExpr>(&expr.node)) {
            if (!construct->resolved) {
                add_diag(result,
                         core_wasm_diag::kUnsupportedWorkflowFrame,
                         "a workflow frame constructor is not type-resolved");
                return std::nullopt;
            }
            // Operands must be bound by an EARLIER let.
            for (const CoreConstructArg &arg : construct->args) {
                const auto dep = let_order.find(arg.value.value);
                if (dep == let_order.end() || dep->second >= i) {
                    add_diag(result,
                             core_wasm_diag::kUnsupportedWorkflowFrame,
                             "a workflow frame constructor operand is not bound by an earlier let");
                    return std::nullopt;
                }
            }
            continue;
        }
        add_diag(result,
                 core_wasm_diag::kUnsupportedWorkflowFrame,
                 "a workflow frame region accepts only path reads and flat aggregate constructors "
                 "(no effects, match, coercion or computation nodes)",
                 expr.source_range);
        return std::nullopt;
    }

    // Reachability: every let must be used (transitively from the yield value).
    std::vector<bool> reachable(lets.size(), false);
    std::vector<std::uint32_t> work{yield->value.value};
    while (!work.empty()) {
        const std::uint32_t value = work.back();
        work.pop_back();
        const auto order_it = let_order.find(value);
        if (order_it == let_order.end()) {
            add_diag(result,
                     core_wasm_diag::kInvalidCore,
                     "a workflow frame value reference is unbound");
            return std::nullopt;
        }
        const std::size_t order = order_it->second;
        if (reachable[order]) {
            continue;
        }
        reachable[order] = true;
        const CoreExpr &expr = workflow.storage.exprs[lets[order]->expr.value];
        if (const auto *construct = std::get_if<CoreConstructExpr>(&expr.node)) {
            for (const CoreConstructArg &arg : construct->args) {
                work.push_back(arg.value.value);
            }
        }
    }
    if (std::any_of(reachable.begin(), reachable.end(), [](bool r) { return !r; })) {
        add_diag(result,
                 core_wasm_diag::kUnsupportedWorkflowFrame,
                 "a workflow frame region binds an orphan value its yield does not reach");
        return std::nullopt;
    }

    // Translate the lets to the materializer's self-contained form. Every let
    // has already been validated as a path read or a flat construct; this loop
    // only collects it and meters the region's construct-scratch high-water.
    auto align8_u32 = [](std::uint64_t value) -> std::uint64_t {
        return (value + 7u) & ~std::uint64_t{7u};
    };
    std::vector<WorkflowFrameLet> frame_lets;
    frame_lets.reserve(lets.size());
    std::uint64_t construct_scratch = 0;
    for (const auto *let_stmt : lets) {
        const CoreLetStmt &let = *let_stmt;
        const CoreExpr &expr = workflow.storage.exprs[let.expr.value];
        const CoreValueTypeId value_type = workflow.storage.value_types[let.result.value];
        WorkflowFrameLet frame_let;
        frame_let.result = let.result;
        frame_let.value_type = value_type;
        if (const auto *path = std::get_if<CorePathExpr>(&expr.node)) {
            auto source = check_path(*path, value_type);
            if (!source.has_value()) {
                return std::nullopt;
            }
            frame_let.is_construct = false;
            frame_let.source = *source;
            frame_let.path = path;
        } else {
            const auto *construct = std::get_if<CoreConstructExpr>(&expr.node);
            if (construct == nullptr) {
                add_diag(result,
                         core_wasm_diag::kUnsupportedWorkflowFrame,
                         "a workflow frame region accepts only path reads and flat constructors");
                return std::nullopt;
            }
            frame_let.is_construct = true;
            frame_let.construct = construct;
            if (value_type.value >= layouts.value_layouts.size()) {
                add_diag(result,
                         core_wasm_diag::kInvalidLayout,
                         "a workflow frame constructor has no finalized value layout");
                return std::nullopt;
            }
            const CoreLayoutId construct_layout = layouts.value_layouts[value_type.value];
            if (construct_layout.value >= layouts.layouts.size()) {
                add_diag(result,
                         core_wasm_diag::kInvalidLayout,
                         "a workflow frame constructor names an out-of-range layout");
                return std::nullopt;
            }
            construct_scratch +=
                align8_u32(layouts.layouts[construct_layout.value].size);
            if (construct_scratch > std::numeric_limits<std::uint32_t>::max()) {
                add_diag(result,
                         core_wasm_diag::kBinaryOverflow,
                         "a workflow frame region's construct scratch exceeds the wasm32 domain");
                return std::nullopt;
            }
        }
        frame_lets.push_back(std::move(frame_let));
    }

    WorkflowRegionPlan plan;
    plan.yield_value = yield->value;
    plan.lets = std::move(frame_lets);
    plan.construct_scratch = static_cast<std::uint32_t>(construct_scratch);
    const CoreLetStmt &yield_let = *yield_let_it->second;
    const CoreExpr &yield_expr = workflow.storage.exprs[yield_let.expr.value];
    if (const auto *path = std::get_if<CorePathExpr>(&yield_expr.node)) {
        // A bare path in a single-let region is the exact opaque forwarding
        // shape; a projected path is a P4-D region the V2-D scheduler
        // materializes.
        if (path->projection.empty() && lets.size() == 1) {
            auto source =
                check_path(*path, workflow.storage.value_types[yield->value.value]);
            if (!source.has_value()) {
                return std::nullopt;
            }
            plan.source = *source;
            return plan;
        }
        auto source =
            check_path(*path, workflow.storage.value_types[yield->value.value]);
        if (!source.has_value()) {
            return std::nullopt;
        }
        plan.source = *source;
    }
    // A projected path, a flat construct or a multi-let region is a P4-D frame
    // the in-module scheduler constructs word-by-word (V2-D).
    plan.constructed = true;
    return plan;
}

[[nodiscard]] std::optional<std::uint32_t> workflow_runner_index(const WorkflowPlan &plan,
                                                                 CoreInstanceId instance);

// V2-D fix-forward: whether one layout edge is an aggregate the runtime
// represents as a CHILD ADDRESS in the module's pointer-tree form (a struct or
// a payload-bearing enum). A tag-only enum is physically one inline
// discriminant word and a container is its two-word inline header, so neither
// edge is dereferenced.
[[nodiscard]] bool workflow_layout_is_child_aggregate(const ir::core::CoreLayout &layout) {
    if (std::holds_alternative<ir::core::CoreLayoutStruct>(layout.shape)) {
        return true;
    }
    if (const auto *tagged = std::get_if<ir::core::CoreLayoutEnum>(&layout.shape)) {
        return !std::ranges::all_of(tagged->variant_payload_sizes,
                                    [](std::uint64_t size) { return size == 0; });
    }
    return false;
}

// True when copying the aggregate named by `id` emits a child-address load
// (copy_struct / copy_enum dereference every struct or payload-enum field).
// The immediate children decide it: a struct child is itself the dereffed
// edge, and an enum child's payload struct fields are walked one level deeper.
[[nodiscard]] bool workflow_layout_emits_child_dereference(
    const ir::core::CoreLayoutTable &layouts,
    CoreLayoutId id,
    std::uint32_t depth) {
    if (id.value >= layouts.layouts.size() || depth > 64u) {
        return true; // fail closed on an out-of-range / runaway edge
    }
    const ir::core::CoreLayout &layout = layouts.layouts[id.value];
    if (const auto *structure = std::get_if<ir::core::CoreLayoutStruct>(&layout.shape)) {
        for (const CoreLayoutId field : structure->field_layouts) {
            if (field.value >= layouts.layouts.size()) {
                return true;
            }
            if (workflow_layout_is_child_aggregate(layouts.layouts[field.value])) {
                return true;
            }
        }
        return false;
    }
    if (const auto *tagged = std::get_if<ir::core::CoreLayoutEnum>(&layout.shape)) {
        for (const CoreLayoutId payload : tagged->variant_payload_layouts) {
            if (workflow_layout_emits_child_dereference(layouts, payload, depth + 1u)) {
                return true;
            }
        }
        return false;
    }
    return false;
}

// V2-D fix-forward: true when materializing `path` off an INLINE P4-D source
// frame (the host-packed workflow Input or a node's inline O_k block) would
// emit a child-address load against inline bytes. The scheduler materializer
// (latch_path_slot + copy_aggregate) speaks the module POINTER-TREE form:
// every non-last projection step is an i32.load child dereference, and an
// aggregate leaf is expanded by re-dereferencing its children. Only the bare
// host-packed entry frame is rewritten into pointer-tree form before its
// runner, so every other materialized region must fail closed on these edges.
[[nodiscard]] bool workflow_inline_path_needs_child_dereference(
    const CoreProgram &program,
    const ir::core::CoreLayoutTable &layouts,
    const CorePathExpr &path,
    CoreValueTypeId leaf_value_type) {
    for (std::uint32_t i = 0; i + 1 < path.projection.size(); ++i) {
        const ir::core::CoreProjectionStep &step = path.projection[i];
        const auto *owner = p6_nominal_struct_layout(program, layouts, step.owner_type);
        if (owner == nullptr || step.field.value >= owner->field_layouts.size()) {
            return true;
        }
        const CoreLayoutId edge = owner->field_layouts[step.field.value];
        if (edge.value >= layouts.layouts.size() ||
            workflow_layout_is_child_aggregate(layouts.layouts[edge.value])) {
            return true;
        }
    }
    if (leaf_value_type.value >= layouts.value_layouts.size()) {
        return true;
    }
    return workflow_layout_emits_child_dereference(
        layouts, layouts.value_layouts[leaf_value_type.value], 0u);
}

// V2-D D6: the compile-time single-page capacity family for a workflow that
// contains at least one P6-frame node. Every extent is a compile-time constant;
// nothing here is runtime-growable. Layout order (frame-bridge v2 design
// section 6.1), cursor starting just above the (capability-workflow-only)
// node-event region:
//
//   bridge control-block page frame + scalar/PtrLen spill slots
//   per-packaged-p6-instance node blocks: I / C / scratch / O
//   host-packed entry payload arena
//   per-bridge-call-site result placements + their payload shares
//   workflow output slot
//
// The rodata image is checked separately against the fixed [256,1024) pool.
// String payload accounting uses the shared kP6FrameStringPoolBytes pool share
// for the entry and one share per bridge result placement (exact bounded-slot
// metering arrives with the merged boundary wire projection in the module
// emission slice; the pool shares make this a conservative upper bound).
[[nodiscard]] bool plan_workflow_p6_capacity_family(
    const CoreProgram &program,
    const ir::core::CoreLayoutTable &layouts,
    const CoreWorkflowDecl &workflow,
    WorkflowPlan &plan,
    const std::vector<WorkflowP6GatherFacts> &gathered,
    CoreWasmCodegenResult &result) {
    constexpr std::uint64_t kPage = kCoreWasmFixedLinearMemoryCapacityBytes;
    const auto align8 = [](std::uint64_t value) {
        return (value + 7u) & ~std::uint64_t{7u};
    };
    auto overflow = [&](std::string_view region) {
        add_diag(result,
                 core_wasm_diag::kBinaryOverflow,
                 std::string("the workflow ") + std::string(region) +
                     " extent overflows the wasm32 address domain");
    };
    auto exhausted = [&](std::string_view region, std::uint64_t extent) {
        add_diag(result,
                 core_wasm_diag::kResourceExhausted,
                 "the workflow " + std::string(region) + " (" +
                     std::to_string(extent) +
                     " bytes, with the per-instance node blocks, bridge control page, entry "
                     "payload arena and result placements) exceeds the fixed 64 KiB linear-memory "
                     "page; split the workflow or shrink the boundary/container capacities");
    };

    // Merged rodata image: concatenation of the per-agent frozen pools. It must
    // fit the single fixed [256,1024) pool (cross-agent deduplication is applied
    // when the image is materialized in the module-emission slice; the sum here
    // is the conservative pre-dedup bound).
    std::uint64_t rodata_total = 0;
    for (const WorkflowP6GatherFacts &g : gathered) {
        rodata_total += g.rodata_extent;
        if (rodata_total > std::numeric_limits<std::uint32_t>::max()) {
            overflow("read-only literal pool");
            return false;
        }
    }
    if (rodata_total > kP6RodataCapacity) {
        exhausted("read-only literal pool", rodata_total);
        return false;
    }

    // Cursor start: immediately above the node-event region on a capability
    // workflow (the same two-phase math compute_event_layout performs); the
    // fixed event base on an identity-shape workflow.
    std::uint64_t cursor = kNodeEventLogBase;
    if (!plan.imports.empty()) {
        const std::uint32_t n = static_cast<std::uint32_t>(workflow.nodes.size());
        if (n != 0 && n > (std::numeric_limits<std::uint32_t>::max() - kNodeEventHeaderBytes) /
                              kNodeEventRecordBytes) {
            overflow("node-event region");
            return false;
        }
        const std::uint64_t event_end =
            align8(std::uint64_t{kNodeEventLogBase} + kNodeEventHeaderBytes +
                   std::uint64_t{n} * kNodeEventRecordBytes);
        if (event_end > std::numeric_limits<std::uint32_t>::max()) {
            overflow("node-event region");
            return false;
        }
        cursor = event_end;
    }

    // (1) Merged bridge control-block page frame: dense fixed-stride blocks for
    // every in-handler bridge call site (agent order, then local ANF order)
    // followed by the scalar/PtrLen spill slots.
    std::uint32_t total_sites = 0;
    std::uint32_t max_arity = 0;
    std::uint64_t spill_total = 0;
    for (const WorkflowP6GatherFacts &g : gathered) {
        if (total_sites > std::numeric_limits<std::uint32_t>::max() - g.bridge_site_count) {
            overflow("bridge call-site table");
            return false;
        }
        total_sites += g.bridge_site_count;
        max_arity = std::max(max_arity, g.bridge_max_arity);
        spill_total += g.bridge_spill_extent;
        if (spill_total > std::numeric_limits<std::uint32_t>::max()) {
            overflow("bridge spill region");
            return false;
        }
    }
    if (total_sites > 0) {
        const std::uint64_t block_stride = align8(8u + 8u * std::uint64_t{max_arity});
        const std::uint64_t blocks_extent = block_stride * total_sites;
        const std::uint64_t control_extent = blocks_extent + spill_total;
        if (block_stride > std::numeric_limits<std::uint32_t>::max() ||
            control_extent > std::numeric_limits<std::uint32_t>::max() ||
            cursor > std::numeric_limits<std::uint32_t>::max() - control_extent) {
            overflow("bridge control-block page frame");
            return false;
        }
        plan.bridge_control_base = static_cast<std::uint32_t>(cursor);
        plan.bridge_block_stride = static_cast<std::uint32_t>(block_stride);
        plan.bridge_control_extent = static_cast<std::uint32_t>(control_extent);
        plan.bridge_spill_base =
            static_cast<std::uint32_t>(cursor + blocks_extent);
        plan.bridge_spill_extent = static_cast<std::uint32_t>(spill_total);
        cursor += control_extent;
    }

    // (2) Per-P6-runner node-frame blocks (only P6 instances occupy a block;
    // opaque capability-final nodes keep their heap tuple and own no fixed
    // block). Each block is I_k / C_k / scratch_k / O_k, every sub-span
    // 8-aligned and sized from the finalized boundary layouts + the gathered
    // scratch high-water.
    plan.node_blocks_base = static_cast<std::uint32_t>(cursor);
    // p6_block_by_runner is already initialized to kInvalidP6Block in
    // build_workflow_plan; the loop below assigns P6 ordinals.
    auto layout_size = [&](CoreValueTypeId vt) -> std::optional<std::uint64_t> {
        if (vt.value >= layouts.value_layouts.size()) {
            return std::nullopt;
        }
        const ir::core::CoreLayoutId id = layouts.value_layouts[vt.value];
        if (id.value >= layouts.layouts.size()) {
            return std::nullopt;
        }
        return layouts.layouts[id.value].size;
    };
    // Gather each P6 runner's aligned block parts in P6-only dense order, then
    // derive the dense cursor through the shared pure arithmetic.
    std::vector<std::uint32_t> p6_runners;
    p6_runners.reserve(plan.packaged_instances.size());
    for (std::uint32_t runner = 0; runner < plan.packaged_instances.size(); ++runner) {
        if (!gathered[runner].p6) {
            continue;
        }
        plan.p6_block_by_runner[runner] =
            static_cast<std::uint32_t>(p6_runners.size());
        p6_runners.push_back(runner);
    }
    std::vector<CoreWasmP6NodeBlockParts> block_parts(p6_runners.size());
    for (std::uint32_t p6 = 0; p6 < p6_runners.size(); ++p6) {
        const std::uint32_t runner = p6_runners[p6];
        const auto *instance = agent_instance(program, plan.packaged_instances[runner]);
        if (instance == nullptr || instance->dispatch_types.size() != 3) {
            add_diag(result,
                     core_wasm_diag::kInvalidCore,
                     "a packaged workflow instance has no exact input/context/output descriptor");
            return false;
        }
        const auto input_size = layout_size(instance->dispatch_types[0]);
        const auto context_size = layout_size(instance->dispatch_types[1]);
        const auto output_size = layout_size(instance->dispatch_types[2]);
        if (!input_size.has_value() || !context_size.has_value() || !output_size.has_value()) {
            add_diag(result,
                     core_wasm_diag::kInvalidLayout,
                     "a packaged workflow node boundary has no finalized P4-D layout");
            return false;
        }
        const std::uint64_t scratch_size =
            align8(std::max<std::uint64_t>(gathered[runner].scratch_high, 16u));
        block_parts[p6] = CoreWasmP6NodeBlockParts{
            static_cast<std::uint32_t>(align8(*input_size)),
            static_cast<std::uint32_t>(align8(*context_size)),
            static_cast<std::uint32_t>(scratch_size),
            static_cast<std::uint32_t>(align8(*output_size))};
    }
    const auto block_cursor = plan_p6_node_block_cursor(cursor, block_parts);
    if (!block_cursor.has_value()) {
        overflow("node-frame block region");
        return false;
    }
    plan.node_blocks.resize(p6_runners.size());
    for (std::uint32_t p6 = 0; p6 < p6_runners.size(); ++p6) {
        const std::uint32_t runner = p6_runners[p6];
        const auto *instance = agent_instance(program, plan.packaged_instances[runner]);
        const CoreWasmP6NodeBlockParts &parts = block_parts[p6];
        WorkflowNodeBlock &block = plan.node_blocks[p6];
        block.instance = plan.packaged_instances[runner];
        const ir::core::CoreLayoutId in_id =
            layouts.value_layouts[instance->dispatch_types[0].value];
        const ir::core::CoreLayoutId ctx_id =
            layouts.value_layouts[instance->dispatch_types[1].value];
        const ir::core::CoreLayoutId out_id =
            layouts.value_layouts[instance->dispatch_types[2].value];
        block.input_layout = in_id.value;
        block.context_layout = ctx_id.value;
        block.output_layout = out_id.value;
        // The recorded block sizes are the 8-aligned backing extents (exactly
        // the strides the bases step by), so the transport verifier can
        // re-derive them from the named layout roots and a crafted section
        // cannot claim an 8-byte root and name a multi-KiB span.
        block.input_size = parts.input;
        block.context_size = parts.context;
        block.output_size = parts.output;
        block.input_base = block_cursor->bases[p6][0];
        block.context_base = block_cursor->bases[p6][1];
        block.scratch_base = block_cursor->bases[p6][2];
        block.scratch_size = parts.scratch;
        block.output_base = block_cursor->bases[p6][3];
    }
    const std::uint64_t node_blocks_extent = block_cursor->extent;
    cursor += node_blocks_extent;
    plan.node_blocks_extent = static_cast<std::uint32_t>(node_blocks_extent);

    // (2b) Scheduler construct-scratch: the in-module P4-D materializer
    // builds projected/constructed node inputs and the workflow return frame
    // word-by-word in one fixed scratch region (design §6.3: scalar loads,
    // aggregate word copies, PtrLen zero-copy shares). The materialized
    // regions run strictly sequentially (each node input is consumed into its
    // I_k before its runner; the return runs after every node), so their
    // scratch windows never co-exist live and one region the size of the
    // LARGEST single region's constructs suffices.
    std::uint64_t region_scratch = 0;
    for (const WorkflowRegionPlan &region : plan.node_regions) {
        region_scratch = std::max(region_scratch,
                                  static_cast<std::uint64_t>(region.construct_scratch));
    }
    region_scratch = std::max(
        region_scratch, static_cast<std::uint64_t>(plan.return_region.construct_scratch));
    if (region_scratch != 0) {
        region_scratch = (region_scratch + 7u) & ~std::uint64_t{7u};
        if (region_scratch > std::numeric_limits<std::uint32_t>::max() ||
            cursor > std::numeric_limits<std::uint32_t>::max() - region_scratch) {
            overflow("scheduler construct-scratch region");
            return false;
        }
        plan.region_scratch_base = static_cast<std::uint32_t>(cursor);
        plan.region_scratch_extent = static_cast<std::uint32_t>(region_scratch);
        cursor += region_scratch;
    }

    // (3) Host-packed entry payload arena: one shared pool share (the bounded
    // String slots are metered exactly from the projected entry wire schema in
    // the emission slice).
    plan.entry_payload_base = static_cast<std::uint32_t>(cursor);
    plan.entry_payload_capacity = kP6FrameStringPoolBytes;
    cursor += align8(kP6FrameStringPoolBytes);
    if (cursor > std::numeric_limits<std::uint32_t>::max()) {
        overflow("entry payload arena");
        return false;
    }

    // (4) Per-call-site bridge result placements: aligned result structures
    // plus one pool share each for result String payloads.
    for (const WorkflowP6GatherFacts &g : gathered) {
        const std::uint64_t extent =
            std::uint64_t{g.bridge_result_extent} +
            std::uint64_t{g.bridge_site_count} * kP6FrameStringPoolBytes;
        cursor += align8(extent);
        if (cursor > std::numeric_limits<std::uint32_t>::max()) {
            overflow("bridge result placement region");
            return false;
        }
    }

    // (5) Workflow output slot (sized from the declared workflow output).
    const auto wf_output_size = layout_size(plan.wf_output_vt);
    if (!wf_output_size.has_value()) {
        add_diag(result,
                 core_wasm_diag::kInvalidLayout,
                 "the workflow output boundary has no finalized P4-D layout");
        return false;
    }
    plan.wf_output_size = static_cast<std::uint32_t>(*wf_output_size);
    plan.wf_output_base = static_cast<std::uint32_t>(cursor);
    cursor += align8(*wf_output_size);

    // (5c) V2-D emission half 2: state-entry trace ring. One 8-byte record per
    // declared state of every packaged P6 agent is the conservative maximum
    // (a deterministic single run enters each state at most once); the first
    // 8 bytes hold the record count header.
    std::uint64_t trace_state_count = 0;
    for (const CoreInstanceId instance_id : plan.packaged_instances) {
        const auto *instance = agent_instance(program, instance_id);
        const auto *payload = agent_instance_payload(program, instance_id);
        if (instance == nullptr || payload == nullptr) {
            add_diag(result, core_wasm_diag::kInvalidCore,
                     "the workflow trace planner found a packaged non-agent instance");
            return false;
        }
        trace_state_count += program.agents[payload->base.value].states.size();
    }
    {
        const std::uint64_t trace_extent = align8(8u + 8u * trace_state_count);
        if (cursor > std::numeric_limits<std::uint32_t>::max() - trace_extent) {
            overflow("state-entry trace ring");
            return false;
        }
        plan.state_trace_base = static_cast<std::uint32_t>(cursor);
        plan.state_trace_capacity = static_cast<std::uint32_t>(trace_extent);
        cursor += trace_extent;
    }

    // (5b) Entry inline->pointer-tree normalization scratch: the host packs the
    // entry frame as an INLINE graph; the scheduler rewrites it into the module
    // pointer-tree form (one addressed window per inline aggregate child) so a
    // packaged handler can dereference an aggregate field straight off I_k.
    // Metered from the entry layout's sub-aggregate sizes.
    if (const auto first_node = plan.schedule.empty()
                                    ? std::nullopt
                                    : std::optional<CoreWorkflowNodeId>{plan.schedule.front()}) {
        const auto &first = plan.nodes[first_node->value];
        const auto runner = workflow_runner_index(plan, first.target_instance);
        const bool entry_is_p6 =
            runner.has_value() &&
            plan.agent_plans[*runner].reads_raw_input_frame;
        if (entry_is_p6) {
            const auto *instance = agent_instance(program, first.target_instance);
            if (instance == nullptr || instance->dispatch_types.size() != 3) {
                add_diag(result,
                         core_wasm_diag::kInvalidCore,
                         "the workflow entry instance has no exact dispatch triplet");
                return false;
            }
            const CoreValueTypeId entry_vt = instance->dispatch_types[0];
            if (entry_vt.value >= layouts.value_layouts.size()) {
                add_diag(result, core_wasm_diag::kInvalidLayout,
                         "the workflow entry nominal has no finalized value type");
                return false;
            }
            // Meter one normalize window per aggregate CHILD OCCURRENCE
            // (struct/enum field), never per distinct layout: two siblings of
            // the same type each get their own runtime window. An enum window
            // is sized by its own layout extent (it already spans its largest
            // variant payload); the payload fields must be flat words, exactly
            // the shape normalize_walk accepts. Containers/Uuid/closures and a
            // nested aggregate inside an enum payload fail closed here as they
            // do at emission.
            std::uint64_t normalize_extent = 0;
            const auto meter_aggregate = [&](auto &&self, CoreLayoutId id,
                                            std::uint32_t depth) -> bool {
                if (id.value >= layouts.layouts.size() || depth > 64u) {
                    return false;
                }
                const ir::core::CoreLayout &layout = layouts.layouts[id.value];
                const auto *structure =
                    std::get_if<ir::core::CoreLayoutStruct>(&layout.shape);
                if (structure == nullptr) {
                    // A root enum needs no window (nothing addresses its slot).
                    return depth == 0u &&
                           std::holds_alternative<ir::core::CoreLayoutEnum>(layout.shape);
                }
                for (const CoreLayoutId child : structure->field_layouts) {
                    if (child.value >= layouts.layouts.size()) {
                        return false;
                    }
                    const ir::core::CoreLayout &field = layouts.layouts[child.value];
                    if (std::holds_alternative<ir::core::CoreLayoutScalar>(field.shape) ||
                        std::holds_alternative<ir::core::CoreLayoutPtrLen>(field.shape)) {
                        continue;
                    }
                    if (std::holds_alternative<ir::core::CoreLayoutStruct>(field.shape)) {
                        normalize_extent += align8(field.size);
                        if (!self(self, child, depth + 1u)) {
                            return false;
                        }
                        continue;
                    }
                    if (const auto *tagged =
                            std::get_if<ir::core::CoreLayoutEnum>(&field.shape)) {
                        normalize_extent += align8(field.size);
                        for (const CoreLayoutId payload :
                             tagged->variant_payload_layouts) {
                            if (payload.value >= layouts.layouts.size()) {
                                return false;
                            }
                            const auto *payload_struct =
                                std::get_if<ir::core::CoreLayoutStruct>(
                                    &layouts.layouts[payload.value].shape);
                            if (payload_struct == nullptr) {
                                return false;
                            }
                            for (const CoreLayoutId payload_field :
                                 payload_struct->field_layouts) {
                                if (payload_field.value >= layouts.layouts.size()) {
                                    return false;
                                }
                                const ir::core::CoreLayout &pf =
                                    layouts.layouts[payload_field.value];
                                if (!std::holds_alternative<
                                        ir::core::CoreLayoutScalar>(pf.shape) &&
                                    !std::holds_alternative<
                                        ir::core::CoreLayoutPtrLen>(pf.shape)) {
                                    return false;
                                }
                            }
                        }
                        continue;
                    }
                    return false;
                }
                return true;
            };
            if (!meter_aggregate(meter_aggregate,
                                 layouts.value_layouts[entry_vt.value], 0u)) {
                add_diag(result, core_wasm_diag::kInvalidLayout,
                         "the workflow entry layout cannot be normalized in-module");
                return false;
            }
            if (normalize_extent != 0) {
                if (cursor > std::numeric_limits<std::uint32_t>::max() -
                                 normalize_extent) {
                    overflow("entry normalization region");
                    return false;
                }
                plan.entry_normalize_base = static_cast<std::uint32_t>(cursor);
                plan.entry_normalize_extent =
                    static_cast<std::uint32_t>(normalize_extent);
                cursor += normalize_extent;
            }
        }
    }

    if (cursor > std::numeric_limits<std::uint32_t>::max()) {
        overflow("P6 frame high-water");
        return false;
    }
    if (cursor > kPage) {
        exhausted("P6 frame high-water", cursor);
        return false;
    }
    // The construct heap shares the same page and starts above this cursor in
    // the module-emission slice; the checked-alloc gate covers its growth at
    // runtime. Record the reserved high-water as its planned base.
    plan.wf_heap_base = static_cast<std::uint32_t>(cursor);
    return true;
}

[[nodiscard]] bool build_workflow_frame_sections(const CoreProgram &program,
                                                 const ir::core::CoreLayoutTable &layouts,
                                                 const CoreWorkflowDecl &workflow,
                                                 WorkflowPlan &plan,
                                                 CoreWasmCodegenResult &result);

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

    // V2-D: first validate every node input region and the return region with
    // the layout-aware region gate (exact bare forwarding vs a projected /
    // constructed P4-D region). These facts decide which nodes run on the P6
    // frame lane and what the scheduler must materialize before each runner.
    std::vector<WorkflowRegionPlan> node_regions(workflow.nodes.size());
    plan.node_regions.resize(workflow.nodes.size());
    {
        bool regions_ok = true;
        for (std::uint32_t id = 0; id < workflow.nodes.size(); ++id) {
            const auto &node = workflow.nodes[id];
            const auto *instance = agent_instance(program, node.target_instance);
            if (instance == nullptr || instance->dispatch_types.size() != 3) {
                add_diag(result,
                         core_wasm_diag::kInvalidCore,
                         "workflow target instance has no exact input/context/output descriptor");
                regions_ok = false;
                break;
            }
            auto region = validate_workflow_region(program,
                                                    workflow,
                                                    layouts,
                                                    node.input_region.get(),
                                                    instance->dispatch_types[0],
                                                    CoreWorkflowNodeId{id},
                                                    schedule_position,
                                                    result);
            if (!region.has_value() ||
                !has_finalized_layout(layouts, instance->dispatch_types[2])) {
                regions_ok = false;
                break;
            }
            node_regions[id] = *region;
            plan.node_regions[id] = std::move(*region);
        }
        if (workflow.return_region == nullptr || workflow.return_region->statements.empty()) {
            add_diag(result, core_wasm_diag::kUnsupportedWorkflowFrame,
                     "workflow return region is absent");
            regions_ok = false;
        }
        CoreValueTypeId wf_output_type{};
        if (regions_ok) {
            // The region ends with the value yield; the yielded value's type is
            // the workflow output frame type.
            const auto *return_yield =
                std::get_if<CoreYieldStmt>(&workflow.return_region->statements.back().node);
            if (return_yield == nullptr ||
                return_yield->value.value >= workflow.storage.value_types.size()) {
                add_diag(result, core_wasm_diag::kUnsupportedWorkflowFrame,
                         "workflow return region must end in one value yield");
                regions_ok = false;
            } else {
                wf_output_type =
                    workflow.storage.value_types[return_yield->value.value];
                const auto *nominal =
                    std::get_if<CoreVtNominal>(&program.value_types[wf_output_type.value].node);
                if (nominal == nullptr || nominal->base != workflow.output_type) {
                    add_diag(result, core_wasm_diag::kUnsupportedWorkflowFrame,
                             "workflow return frame is not the exact declared output nominal type");
                    regions_ok = false;
                }
            }
        }
        if (regions_ok) {
            const auto &yield_stmt =
                *std::get_if<CoreYieldStmt>(&workflow.return_region->statements.back().node);
            plan.wf_output_vt =
                workflow.storage.value_types[yield_stmt.value.value];
            auto region = validate_workflow_region(program,
                                                    workflow,
                                                    layouts,
                                                    workflow.return_region.get(),
                                                    plan.wf_output_vt,
                                                    std::nullopt,
                                                    schedule_position,
                                                    result);
            if (!region.has_value()) {
                regions_ok = false;
            } else {
                plan.output = region->source;
                plan.return_region = std::move(*region);
            }
        }
        if (!regions_ok) {
            return std::nullopt;
        }
    }

    // V2-D RETURN: the scheduler rewrites the host-packed INLINE entry frame
    // into module POINTER-TREE form before ONE runner executes: the runner of
    // the FIRST scheduled node, and only when that node's input region is the
    // bare host-packed forward (!constructed, sourced from the workflow
    // Input). Every other I_k is scheduler-materialized INLINE (a bare
    // forward out of an upstream O_k or a projected/constructed region) and no
    // normalization runs for it, so the input-inline provenance gate must stay
    // CLOSED for those handlers: lifting it would let a handler dereference
    // inline aggregate words as child addresses. This predicate is the single
    // authority both packaging builds use.
    const CoreWorkflowNodeId entry_node_id = plan.schedule.front();
    const auto entry_runner_index =
        workflow_runner_index(plan, workflow.nodes[entry_node_id.value].target_instance);
    const WorkflowRegionPlan &entry_region = node_regions[entry_node_id.value];
    const bool entry_frame_is_normalized =
        entry_runner_index.has_value() && !entry_region.constructed &&
        entry_region.source.kind == WorkflowFrameSourceKind::Input;
    auto admit_for_runner = [&](std::uint32_t runner) {
        return entry_frame_is_normalized && runner == *entry_runner_index;
    };

    // V2-D: build every packaged agent plan in FACT-GATHERING mode (agent-lane
    // coordinates, no agent frame section). This proves the SAME per-handler
    // gates the direct agent passes and reveals computed finals, bridge sites,
    // rodata extent, scratch high-water and imports. A second, relocated build
    // below produces the workflow-module handler bytes.
    using GatheredAgent = WorkflowP6GatherFacts;
    std::vector<GatheredAgent> gathered(plan.packaged_instances.size());
    {
        std::vector<AgentPlan> fact_plans;
        fact_plans.reserve(plan.packaged_instances.size());
        for (std::uint32_t runner = 0; runner < plan.packaged_instances.size(); ++runner) {
            const auto instance_id = plan.packaged_instances[runner];
            const auto *payload = agent_instance_payload(program, instance_id);
            if (payload == nullptr) {
                add_diag(result, core_wasm_diag::kInvalidCore,
                         "packaged workflow target is not an agent instance");
                return std::nullopt;
            }
            auto agent_plan = build_agent_plan(
                program, layouts, payload->base, result,
                AgentPlanPolicy{.unsupported_code =
                                    core_wasm_diag::kUnsupportedWorkflowFrame,
                                .allow_capability = true,
                                // V2-D: a packaged agent passes the SAME
                                // per-handler region/SSA/safety gates a
                                // standalone agent passes (computed finals,
                                // match arenas, ordered bridge calls).
                                .allow_computed_goto = true,
                                .allow_bridge = true,
                                .slice = "E3",
                                // V2-D RETURN: only the runner whose bare
                                // host-packed entry frame the scheduler
                                // normalizes in place may project through an
                                // aggregate of its INPUT frame; every other
                                // runner receives an inline I_k and keeps the
                                // provenance gate closed.
                                .admit_normalized_entry_frame = admit_for_runner(runner),
                                // The workflow packager owns the module-level
                                // core-layout section; this is fact gathering.
                                .skip_frame_section = true,
                                .workflow_packaging_lane = true});
            if (!agent_plan.has_value()) {
                return std::nullopt;
            }
            gathered[runner].p6 = agent_plan->has_computed_final ||
                                  agent_plan->has_computed_goto ||
                                  !agent_plan->bridge_calls.empty();
            gathered[runner].scratch_high = agent_plan->p6_scratch_high;
            gathered[runner].rodata_extent = agent_plan->rodata_extent;
            gathered[runner].bridge_site_count =
                static_cast<std::uint32_t>(agent_plan->bridge_calls.size());
            for (const BridgeCallPlan &site : agent_plan->bridge_calls) {
                gathered[runner].bridge_max_arity = std::max(
                    gathered[runner].bridge_max_arity,
                    static_cast<std::uint32_t>(site.param_vt.size()));
                gathered[runner].bridge_spill_extent +=
                    static_cast<std::uint32_t>((std::uint64_t{site.spill_bytes} + 7u) &
                                               ~std::uint64_t{7u});
                // P6-7 V2-D fix-forward: every planned bridge result must own a
                // finalized value layout. With skip_frame_section=true the
                // agent-lane physical planner that otherwise proves this does
                // not run, so an out-of-range value type / layout id must fail
                // closed here rather than silently account zero result bytes
                // and under-count the D6 result-placement region.
                if (site.result_vt.value >= layouts.value_layouts.size()) {
                    add_diag(result,
                             core_wasm_diag::kInvalidLayout,
                             "a packaged workflow frame bridge result has no finalized P4-D "
                             "value layout");
                    return std::nullopt;
                }
                const ir::core::CoreLayoutId result_layout_id =
                    layouts.value_layouts[site.result_vt.value];
                if (result_layout_id.value >= layouts.layouts.size()) {
                    add_diag(result,
                             core_wasm_diag::kInvalidLayout,
                             "a packaged workflow frame bridge result names an invalid P4-D "
                             "layout id");
                    return std::nullopt;
                }
                const auto &result_layout = layouts.layouts[result_layout_id.value];
                gathered[runner].bridge_result_extent +=
                    static_cast<std::uint32_t>((result_layout.size + 7u) &
                                               ~std::uint64_t{7u});
            }
            fact_plans.push_back(std::move(*agent_plan));
        }
        // Merge the module capability import table (sorted unique).
        for (const AgentPlan &fact : fact_plans) {
            for (const CoreCapabilityId id : fact.imports) {
                plan.imports.push_back(id);
            }
        }
        std::sort(plan.imports.begin(), plan.imports.end(),
                  [](auto lhs, auto rhs) { return lhs.value < rhs.value; });
        plan.imports.erase(std::unique(plan.imports.begin(), plan.imports.end()),
                           plan.imports.end());
        if (plan.imports.size() >
            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max() - 6u)) {
            add_diag(result, core_wasm_diag::kInvalidCapabilityAbi,
                     "reachable capability import table exceeds the wasm32 index domain");
            return std::nullopt;
        }

        // A P6 node's input region must be materializable by the scheduler and
        // a non-P6 (opaque) node may not take a constructed frame; pin the
        // per-node lane + host-pack facts. Also enforce D5: two nodes may not
        // reuse one packaged instance in this rung.
        std::unordered_map<std::uint32_t, std::uint32_t> instance_users;
        for (std::uint32_t id = 0; id < workflow.nodes.size(); ++id) {
            const ir::core::CoreWorkflowNode &node = workflow.nodes[id];
            const auto runner = workflow_runner_index(plan, node.target_instance);
            if (!runner.has_value()) {
                add_diag(result, core_wasm_diag::kInvalidCore,
                         "workflow node target has no packaged runner plan");
                return std::nullopt;
            }
            const std::uint32_t runner_id = *runner;
            instance_users[node.target_instance.value] += 1;
            const bool p6 = gathered[runner_id].p6;
            const WorkflowRegionPlan &region = node_regions[id];
            if (!p6 && region.constructed) {
                add_diag(result, core_wasm_diag::kUnsupportedWorkflowFrame,
                         "an opaque capability-final node cannot take a constructed P4-D input "
                         "(only a P6 computed/bridge node does); add the frame-bridge packaging");
                return std::nullopt;
            }
            WorkflowNodePlan node_plan;
            node_plan.node = CoreWorkflowNodeId{id};
            node_plan.target_instance = node.target_instance;
            node_plan.input = region.source;
            node_plan.schedule_pos = schedule_position[id];
            if (!fact_plans[runner_id].imports.empty()) {
                // Record ALL reachable capabilities for this node. A P6 bridge
                // node can call multiple capabilities across different branches
                // (e.g. HandleGeneral and HandleTechnical in a routing flow);
                // the exec-manifest must account for every one so the wire-schema
                // admission's set-equality check sees them as referenced.
                for (const CoreCapabilityId cap : fact_plans[runner_id].imports) {
                    const auto &symbol = program.capabilities[cap.value].symbol_ref;
                    if (!symbol.id.has_value()) {
                        add_diag(result, core_wasm_diag::kInvalidCapabilityAbi,
                                 "workflow node capability has no source SymbolId");
                        return std::nullopt;
                    }
                    node_plan.capabilities.emplace_back(cap, *symbol.id);
                }
            }
            plan.nodes[id] = std::move(node_plan);
        }
        // D5 lifecycle rule: in a P6 workflow two different nodes may NOT reuse
        // one packaged instance (each needs its own fixed node-frame block; a
        // re-use analysis without observable overlap is a later slice). An
        // all-opaque workflow keeps sharing its single deduped runner across any
        // number of nodes, exactly as the E3/E4 baseline did.
        plan.has_p6_nodes = std::any_of(
            gathered.begin(), gathered.end(),
            [](const GatheredAgent &g) { return g.p6; });
        if (plan.has_p6_nodes) {
            for (const auto &[instance_id, users] : instance_users) {
                (void)instance_id;
                if (users > 1) {
                    add_diag(result, core_wasm_diag::kResourceExhausted,
                             "two workflow nodes reuse one packaged agent instance; V2-D assigns one "
                             "node-frame block per instance in a P6-frame workflow and rejects reuse "
                             "(re-use analysis pending)");
                    return std::nullopt;
                }
            }
        }
        // The agent-lane fact plans are the plans the workflow module's opaque
        // runner/manifest emission consumes; a P6 workflow's relocated handler
        // bodies replace the runner body in the module-emission slice.
        plan.agent_plans = std::move(fact_plans);
    }

    // V2-D fix-forward: the scheduler materializer copies every P6 node's I_k
    // and the workflow output frame from INLINE sources (the host-packed
    // workflow Input and every upstream node's inline O_k block) using
    // pointer-tree child dereferences. Only the single bare host-packed ENTRY
    // frame is skipped (the runner consumes it after in-place
    // normalization). Every other materialized region must therefore project
    // only top-level words / flat aggregates; a nested struct / payload-enum
    // edge has no child address in an inline frame and would be read as a
    // garbage window, so reject the workflow until inline->pointer-tree
    // normalization covers scheduler-materialized regions.
    if (plan.has_p6_nodes) {
        const auto region_reads_inline_child = [&](const WorkflowRegionPlan &region) -> bool {
            for (const WorkflowFrameLet &let : region.lets) {
                if (let.is_construct || let.path == nullptr) {
                    continue;
                }
                if (workflow_inline_path_needs_child_dereference(
                        program, layouts, *let.path, let.value_type)) {
                    return true;
                }
            }
            return false;
        };
        for (std::uint32_t id = 0; id < workflow.nodes.size(); ++id) {
            const CoreWorkflowNodeId node_id{id};
            const auto runner = workflow_runner_index(plan, workflow.nodes[id].target_instance);
            if (!runner.has_value() || !gathered[*runner].p6) {
                continue; // an opaque node's region is not scheduler-materialized
            }
            const WorkflowRegionPlan &region = plan.node_regions[id];
            const bool host_packed_entry =
                node_id == plan.schedule.front() && !region.constructed &&
                region.source.kind == WorkflowFrameSourceKind::Input;
            if (host_packed_entry) {
                continue; // normalized in place by the scheduler before the runner
            }
            if (region_reads_inline_child(region)) {
                add_diag(result,
                         core_wasm_diag::kUnsupportedWorkflowFrame,
                         "a scheduler-materialized workflow node frame projects through a "
                         "nested struct/enum of an inline source frame (the host-packed entry or "
                         "an upstream node output); only a flat inline frame is materializable "
                         "until the inline->pointer-tree rewrite covers non-entry regions");
                return std::nullopt;
            }
        }
        if (region_reads_inline_child(plan.return_region)) {
            add_diag(result,
                     core_wasm_diag::kUnsupportedWorkflowFrame,
                     "the workflow return frame projects through a nested struct/enum of an "
                     "inline source frame (the host-packed entry or a node output); only a flat "
                     "inline return is materializable until the scheduler region rewrite "
                     "covers the return frame");
            return std::nullopt;
        }
    }

    // WH-5b.1: p6_block_by_runner must be populated for EVERY workflow (not
    // just P6 ones) so the scheduler's unconditional p6_node check never
    // dereferences an empty vector. All-opaque workflows keep every entry at
    // kInvalidP6Block; plan_workflow_p6_capacity_family assigns P6 ordinals.
    plan.p6_block_by_runner.assign(
        plan.packaged_instances.size(), kInvalidP6Block);

    // V2-D physical planning: run the D6 compile-time single-page capacity
    // family over the node-frame blocks, the merged bridge control page frame,
    // the merged rodata image, the host-packed entry arena and the per-call-site
    // result placements. The workflow MODULE packaging (relocated handler
    // emission, computed runners, in-module scheduler sequences, the v2
    // core-layout section and the descriptor fields) is the following V2-D
    // codegen slice; until it lands a workflow that actually needs the P6 node
    // lane is rejected here with a precise diagnostic, while an all-opaque
    // workflow never enters this path and keeps its byte-identical module.
    if (plan.has_p6_nodes &&
        !plan_workflow_p6_capacity_family(program, layouts, workflow, plan, gathered, result)) {
        return std::nullopt;
    }
    if (plan.has_p6_nodes) {
        // V2-D emission half 1: a p6 runner whose ONLY computed content is a
        // COMPUTED FINAL (with projected/constructed node inputs and a
        // projected/constructed workflow return materialized by the in-module
        // scheduler) is emitted by encode_workflow_module. The remaining V2-D
        // handler families still fail closed with a precise diagnostic:
        //   * a non-final computed-goto preamble (the relocated handler-loop
        //     computed runner with state threading);
        //   * an in-handler capability bridge (the workflow module's bridge
        //     functype, control blocks and host callback lane).
        // An all-opaque workflow never enters this path and keeps its
        // byte-identical module.
        // V2-D emission half 2: one shared dense bridge registry spans every
        // packaged runner; a runner's sites compact as a contiguous per-runner
        // tail onto GLOBAL dense ids (block_offset = global_id * block_stride),
        // and the per-runner spill window begins at bridge_spill_base plus the
        // prior runners' aligned spill totals (the same arithmetic the D6
        // capacity family used to size the merged page frame).
        BridgeCallRegistry workflow_bridge_registry;
        std::uint32_t global_site_base = 0;
        // The ACTUAL post-compaction spill cursor: each runner's spill window
        // densely follows the prior runner's. The D6 capacity family reserved a
        // conservative per-runner bound; the section records the tight extents.
        std::uint32_t actual_spill_cursor = plan.bridge_spill_base;

        // V2-D emission half 1: second, relocated build of every packaged P6
        // runner's agent plan. The builders emit onto the instance's node block
        // and into the shared, merged rodata pool; the opaque runner path still
        // consumes the static GotoAction walk (the relocated handlers are
        // appended as separate functions).
        RodataLiteralPool merged_rodata;
        {
            std::vector<std::string> all_literals;
            for (const AgentPlan &fact : plan.agent_plans) {
                for (const std::string &literal : fact.rodata.frozen_literals()) {
                    all_literals.push_back(literal);
                }
            }
            if (!merged_rodata.rebuild_from_literals(all_literals)) {
                add_diag(result,
                         core_wasm_diag::kResourceExhausted,
                         "the merged workflow rodata literal pool exceeds its fixed region");
                return std::nullopt;
            }
            merged_rodata.freeze();
            plan.workflow_rodata_extent = merged_rodata.extent();
        }
        plan.workflow_rodata = std::move(merged_rodata);

        plan.relocated_handlers.resize(plan.packaged_instances.size());
        for (std::uint32_t runner = 0; runner < plan.packaged_instances.size(); ++runner) {
            const CoreInstanceId instance_id = plan.packaged_instances[runner];
            const auto *payload = agent_instance_payload(program, instance_id);
            if (payload == nullptr) {
                add_diag(result, core_wasm_diag::kInvalidCore,
                         "packaged workflow target is not an agent instance");
                return std::nullopt;
            }
            const AgentPlan &runner_fact = plan.agent_plans[runner];
            const bool p6 =
                runner_fact.has_computed_final ||
                runner_fact.has_computed_goto ||
                !runner_fact.bridge_calls.empty();
            const std::uint32_t runner_spill_base = actual_spill_cursor;
            if (!p6) {
                continue;
            }
            // WH-5b.1: the block lookup is P6-only; opaque runners have no
            // node_block entry (p6_block_by_runner[runner] == kInvalidP6Block).
            const WorkflowNodeBlock &block =
                plan.node_blocks[plan.p6_block_by_runner[runner]];
            // V2-D emission half 2: the generic handler-dispatch P6 runner can
            // only observe a COMPUTED RETURN terminal (it materializes O_k and
            // returns it). A scalar computed-goto preamble that feeds an opaque
            // Identity / tuple Capability terminal still cannot be packaged: the
            // static opaque runner cannot dispatch the computed states, and a
            // P6 runner has no opaque-import arm, so the node would complete
            // without ever invoking the terminal capability. Walk the EXACT
            // successor graph (plain gotos AND computed-goto targets) and
            // require every reachable action to be a goto/computed-goto/
            // computed-return — never an opaque Identity or tuple Capability.
            {
                std::vector<bool> seen(runner_fact.actions.size(), false);
                std::vector<std::uint32_t> worklist{runner_fact.initial.value};
                bool opaque_terminal_reachable = false;
                while (!worklist.empty()) {
                    const std::uint32_t current = worklist.back();
                    worklist.pop_back();
                    if (current >= seen.size() || seen[current]) {
                        continue;
                    }
                    seen[current] = true;
                    std::visit(Overloaded{
                        [&](const GotoAction &go) { worklist.push_back(go.target.value); },
                        [&](const ComputedGotoAction &go) {
                            for (const CoreStateId target : go.targets) {
                                worklist.push_back(target.value);
                            }
                        },
                        [&](const ComputedReturnAction &) {},
                        [&](const IdentityAction &) { opaque_terminal_reachable = true; },
                        [&](const CapabilityAction &) { opaque_terminal_reachable = true; },
                    }, runner_fact.actions[current]);
                }
                if (opaque_terminal_reachable) {
                    add_diag(result,
                             core_wasm_diag::kUnsupportedWorkflowFrame,
                             "a scalar computed-goto preamble cannot feed an opaque identity or "
                             "tuple-capability terminal on the P6 workflow lane (the generic "
                             "computed runner observes only a relocated computed-final handler; "
                             "route the terminal capability through an in-handler frame bridge)");
                    return std::nullopt;
                }
            }
            P6FrameRelocation relocation;
            relocation.input_base = block.input_base;
            relocation.input_capacity = block.input_size;
            relocation.context_base = block.context_base;
            relocation.context_capacity = block.context_size;
            relocation.scratch_base = block.scratch_base;
            relocation.scratch_capacity = block.scratch_size;
            relocation.output_base = block.output_base;
            relocation.output_capacity = block.output_size;
            // The per-runner private state globals follow the fixed five and,
            // on a capability workflow, the sixth pending-latch global (see
            // encode_workflow_module's global section); the base therefore
            // depends on whether the workflow carries imports.
            relocation.current_state_global =
                (plan.imports.empty() ? 5u : 6u) + runner;
            relocation.transition_count_global = kWorkflowGlobalTransitionCount;
            relocation.heap_next_global = kWorkflowGlobalHeapNext;
            auto relocated = build_agent_plan(
                program, layouts, payload->base, result,
                AgentPlanPolicy{.unsupported_code = core_wasm_diag::kUnsupportedWorkflowFrame,
                               .allow_capability = true,
                               .allow_computed_goto = true,
                               .allow_bridge = true,
                               .slice = "E3",
                               // V2-D RETURN: match the fact-gathering build:
                               // only the entry runner with the scheduler-
                               // normalized bare host-packed frame may read
                               // INPUT through aggregate edges.
                               .admit_normalized_entry_frame = admit_for_runner(runner),
                               .skip_frame_section = true,
                               .workflow_packaging_lane = true,
                               .frame_relocation = &relocation,
                               .shared_rodata_pool = &plan.workflow_rodata,
                               .shared_bridge_registry = &workflow_bridge_registry,
                               .wf_runner = runner,
                               .wf_site_id_base = global_site_base,
                               .wf_runner_spill_base = runner_spill_base,
                               .wf_control_base = plan.bridge_control_base,
                               .wf_block_stride = plan.bridge_block_stride,
                               .wf_imports = &plan.imports});
            if (!relocated.has_value()) {
                return std::nullopt;
            }
            if (relocated->handlers.size() != plan.agent_plans[runner].handlers.size()) {
                add_diag(result,
                         core_wasm_diag::kInternalInvalid,
                         "a relocated packaged agent emitted a different handler count than its "
                         "fact-gathering plan");
                return std::nullopt;
            }
            global_site_base += static_cast<std::uint32_t>(
                workflow_bridge_registry.site_count_for_runner(runner));
            actual_spill_cursor =
                runner_spill_base + workflow_bridge_registry.runner_spill_extent(
                                        runner, runner_spill_base);
            plan.relocated_handlers[runner] = std::move(relocated->handlers);
        }
        plan.workflow_bridge_sites = workflow_bridge_registry.sites();

        // Build the merged dense frame-layout table (one BoundaryTableBuilder
        // over the workflow entry, every node I/O/C root and the workflow output,
        // plus every bridge argument/result root) and the per-node + workflow
        // wire roots.
        if (!build_workflow_frame_sections(program, layouts, workflow, plan, result)) {
            return std::nullopt;
        }
    }
    return plan;
}

// V2-D: build the workflow module's core-layout section and its boundary wire
// schema. The dense layout table is the union of the workflow entry nominal,
// every packaged node's input/context/output boundary and the workflow output
// nominal, emitted through one BoundaryTableBuilder (two fixed slots that share
// a source layout hash-cons to one dense node). The wire schema carries the same
// roots (node_inputs/node_outputs plus the workflow output root) so the Node
// host packs the entry I frame and encodes the workflow output with one table.
[[nodiscard]] bool build_workflow_frame_sections(const CoreProgram &program,
                                                 const ir::core::CoreLayoutTable &layouts,
                                                 const CoreWorkflowDecl &workflow,
                                                 WorkflowPlan &plan,
                                                 CoreWasmCodegenResult &result) {
    const std::uint32_t runner_count =
        static_cast<std::uint32_t>(plan.packaged_instances.size());
    // WH-5b.1: node_blocks, dense node layouts, and wire-schema node roots are
    // P6-RUNNER-ONLY dense order. Opaque runners have no fixed frame block and
    // no P4-D layout; their wire-JSON encoding is handled through the capability
    // param/result binding, not frame_roots.
    const std::uint32_t p6_count = static_cast<std::uint32_t>(
        plan.node_blocks.size());

    std::vector<CoreValueTypeId> node_input_vts;
    std::vector<CoreValueTypeId> node_context_vts;
    std::vector<CoreValueTypeId> node_output_vts;
    node_input_vts.reserve(p6_count);
    node_context_vts.reserve(p6_count);
    node_output_vts.reserve(p6_count);
    for (std::uint32_t runner = 0; runner < runner_count; ++runner) {
        const auto *instance =
            agent_instance(program, plan.packaged_instances[runner]);
        if (instance == nullptr || instance->dispatch_types.size() != 3) {
            add_diag(result, core_wasm_diag::kInvalidCore,
                     "a packaged workflow instance has no exact dispatch triplet");
            return false;
        }
        if (plan.p6_block_by_runner[runner] == kInvalidP6Block) {
            continue;
        }
        const auto nominal_base = [&](CoreValueTypeId vt) -> std::optional<CoreTypeId> {
            if (vt.value >= program.value_types.size()) {
                return std::nullopt;
            }
            const auto *nominal =
                std::get_if<CoreVtNominal>(&program.value_types[vt.value].node);
            if (nominal == nullptr) {
                return std::nullopt;
            }
            return nominal->base;
        };
        const auto input_type = nominal_base(instance->dispatch_types[0]);
        const auto context_type = nominal_base(instance->dispatch_types[1]);
        const auto output_type = nominal_base(instance->dispatch_types[2]);
        if (!input_type.has_value() || !context_type.has_value() || !output_type.has_value()) {
            add_diag(result, core_wasm_diag::kInvalidLayout,
                     "a packaged workflow node dispatch type is not a nominal");
            return false;
        }
        const auto input_vt = frame_boundary_value_type(program, *input_type);
        const auto context_vt = frame_boundary_value_type(program, *context_type);
        const auto output_vt = frame_boundary_value_type(program, *output_type);
        if (!input_vt.has_value() || !context_vt.has_value() || !output_vt.has_value()) {
            add_diag(result, core_wasm_diag::kInvalidLayout,
                     "a packaged workflow node boundary has no finalized value type");
            return false;
        }
        node_input_vts.push_back(*input_vt);
        node_context_vts.push_back(*context_vt);
        node_output_vts.push_back(*output_vt);
    }
    const auto wf_input_vt = frame_boundary_value_type(program, workflow.input_type);
    const auto wf_output_vt = frame_boundary_value_type(program, workflow.output_type);
    if (!wf_input_vt.has_value() || !wf_output_vt.has_value()) {
        add_diag(result, core_wasm_diag::kInvalidLayout,
                 "the workflow boundary has no finalized value type");
        return false;
    }

    // Wire projection: capabilities plus per-node roots; the agent input/output
    // pair is the WORKFLOW boundary (so frame_roots.input/output name it) and
    // node_inputs/node_outputs run parallel to the packaged-instance table.
    auto projection = ir::core::project_core_wire_schema(
        program, plan.imports,
        std::pair{*wf_input_vt, *wf_output_vt},
        std::pair{node_input_vts, node_output_vts});
    if (!projection.ok() || !projection.table->frame_roots.has_value()) {
        add_diag(result,
                 core_wasm_diag::kUnsupportedWorkflowFrame,
                 "a P6 workflow boundary is not wire-transportable on the V2-D frame lane");
        return false;
    }

    // Dense layout table (P6-runner-only dense order, matching node_blocks).
    BoundaryTableBuilder builder(layouts);
    plan.dense_node_input_layouts.resize(p6_count);
    plan.dense_node_context_layouts.resize(p6_count);
    plan.dense_node_output_layouts.resize(p6_count);
    const ir::core::CoreLayoutId wf_input_full =
        layouts.value_layouts[wf_input_vt->value];
    const ir::core::CoreLayoutId wf_output_full =
        layouts.value_layouts[wf_output_vt->value];
    // Emit the workflow input/output roots first (the section's input root is
    // encoded kInvalid for a workflow table; output names the real root).
    (void)builder.emit_fixed(wf_input_full, /*from_input=*/true);
    const ir::core::CoreLayoutId wf_output_dense =
        builder.emit_fixed(wf_output_full, /*from_input=*/false);
    for (std::uint32_t p6 = 0; p6 < p6_count; ++p6) {
        const ir::core::CoreLayoutId input_full =
            layouts.value_layouts[node_input_vts[p6].value];
        const ir::core::CoreLayoutId context_full =
            layouts.value_layouts[node_context_vts[p6].value];
        const ir::core::CoreLayoutId output_full =
            layouts.value_layouts[node_output_vts[p6].value];
        plan.dense_node_input_layouts[p6] =
            builder.emit_fixed(input_full, /*from_input=*/true);
        // A context root is never a container-bearing fixed frame in this rung.
        plan.dense_node_context_layouts[p6] =
            builder.emit_fixed(context_full, /*from_input=*/false);
        plan.dense_node_output_layouts[p6] =
            builder.emit_fixed(output_full, /*from_input=*/false);
        if (builder.failed()) {
            add_diag(result,
                     builder.nested_container()
                         ? core_wasm_diag::kUnsupportedWorkflowFrame
                         : core_wasm_diag::kInvalidLayout,
                     builder.nested_container()
                         ? "a P6 workflow frame nests a bounded collection in backing storage"
                         : "a P6 workflow boundary layout is not finalizable into the frame table");
            return false;
        }
    }
    plan.dense_wf_output_layout = wf_output_dense;

    // V2-D emission half 2: plan the merged capability bridge page frame's
    // dense records and the disjoint per-call-site result placements + payload
    // arenas. The sites are grouped per runner (global dense call_site_id
    // order); each runner's results occupy the exact conservative window the
    // D6 capacity family reserved (aligned result-root sum + one pool share per
    // fact-gathered site), and the sites chain densely inside it, so the
    // resulting cursor after every window equals plan.wf_output_base.
    std::vector<ir::core::CoreFrameBridgeCallSite> bridge_records;
    std::uint32_t workflow_actual_spill_extent = 0;
    if (!plan.workflow_bridge_sites.empty()) {
        std::unordered_map<std::uint64_t, const ir::core::CoreWireCapabilitySchema *>
            wire_by_symbol;
        for (const ir::core::CoreWireCapabilitySchema &schema :
             projection.table->capabilities) {
            wire_by_symbol.emplace(schema.source_symbol, &schema);
        }
        const auto align8 = [](std::uint64_t value) {
            return (value + 7u) & ~std::uint64_t{7u};
        };
        const std::uint32_t total_sites =
            static_cast<std::uint32_t>(plan.workflow_bridge_sites.size());
        const std::uint64_t blocks_extent =
            std::uint64_t{total_sites} * plan.bridge_block_stride;
        std::uint32_t spill_top = plan.bridge_spill_base;
        for (const BridgeCallPlan &site : plan.workflow_bridge_sites) {
            spill_top = std::max(
                spill_top,
                site.spill_base +
                    static_cast<std::uint32_t>(align8(site.spill_bytes)));
        }
        workflow_actual_spill_extent = spill_top - plan.bridge_spill_base;
        const std::uint64_t control_extent = blocks_extent + workflow_actual_spill_extent;
        if (control_extent > plan.bridge_control_extent) {
            add_diag(result,
                     core_wasm_diag::kInternalInvalid,
                     "the exact workflow bridge control frame exceeds its D6-reserved extent");
            return false;
        }

        std::uint64_t cursor =
            std::uint64_t{plan.entry_payload_base} +
            align8(plan.entry_payload_capacity);
        std::size_t site_index = 0;
        bridge_records.reserve(plan.workflow_bridge_sites.size());
        for (std::uint32_t runner = 0; runner < runner_count; ++runner) {
            // Replay the D6 per-runner conservative window exactly.
            std::uint64_t result_extent_sum = 0;
            for (const BridgeCallPlan &fact_site :
                 plan.agent_plans[runner].bridge_calls) {
                if (fact_site.result_vt.value >= layouts.value_layouts.size()) {
                    add_diag(result, core_wasm_diag::kInvalidLayout,
                             "a workflow bridge result has no finalized value layout");
                    return false;
                }
                const ir::core::CoreLayoutId result_full =
                    layouts.value_layouts[fact_site.result_vt.value];
                result_extent_sum += align8(layouts.layouts[result_full.value].size);
            }
            const std::uint64_t window = align8(
                result_extent_sum +
                std::uint64_t{plan.agent_plans[runner].bridge_calls.size()} *
                    kP6FrameStringPoolBytes);
            const std::uint64_t window_end = cursor + window;

            while (site_index < plan.workflow_bridge_sites.size() &&
                   plan.workflow_bridge_sites[site_index].runner == runner) {
                const BridgeCallPlan &site = plan.workflow_bridge_sites[site_index];
                const CoreCapabilityDecl &capability =
                    program.capabilities[site.capability.value];
                if (!capability.symbol_ref.id.has_value()) {
                    add_diag(result, core_wasm_diag::kInvalidCapabilityAbi,
                             "a workflow bridge capability has no resolved SymbolId");
                    return false;
                }
                const auto wire_it = wire_by_symbol.find(*capability.symbol_ref.id);
                if (wire_it == wire_by_symbol.end()) {
                    add_diag(result, core_wasm_diag::kInvalidCapabilityAbi,
                             "a workflow bridge capability is missing from the wire-schema table");
                    return false;
                }
                const ir::core::CoreWireCapabilitySchema &wire = *wire_it->second;
                if (wire.params.size() != site.param_vt.size()) {
                    add_diag(result, core_wasm_diag::kInvalidCapabilityAbi,
                             "a workflow bridge call arity disagrees with the wire-schema "
                             "parameter count");
                    return false;
                }
                ir::core::CoreFrameBridgeCallSite record;
                record.call_site_id = site.call_site_id;
                record.source_symbol = *capability.symbol_ref.id;
                record.arity = static_cast<std::uint32_t>(site.param_vt.size());
                record.block_offset =
                    site.call_site_id * plan.bridge_block_stride;
                record.param_layouts.reserve(site.param_vt.size());
                for (std::uint32_t p = 0; p < site.param_vt.size(); ++p) {
                    const ir::core::CoreLayoutId full_param =
                        layouts.value_layouts[site.param_vt[p].value];
                    const ir::core::CoreLayoutId dense_param =
                        builder.emit_fixed(full_param, /*from_input=*/false);
                    if (builder.failed()) {
                        add_diag(result, core_wasm_diag::kUnsupportedCapabilityFrame,
                                 "a workflow bridge argument layout is not finalizable into "
                                 "the frame table");
                        return false;
                    }
                    if (!ir::core::verify_frame_layout_wire_consistency(
                             builder.table_ref(), dense_param, *projection.table,
                             wire.params[p])
                             .empty()) {
                        add_diag(result, core_wasm_diag::kUnsupportedCapabilityFrame,
                                 "a workflow bridge argument is not wire-representable on the "
                                 "frame-bridge subset");
                        return false;
                    }
                    record.param_layouts.push_back(dense_param);
                }
                const ir::core::CoreLayoutId full_result =
                    layouts.value_layouts[site.result_vt.value];
                const ir::core::CoreLayoutId dense_result =
                    builder.emit_fixed(full_result, /*from_input=*/false);
                if (builder.failed()) {
                    add_diag(result, core_wasm_diag::kUnsupportedCapabilityFrame,
                             "a workflow bridge result layout is not finalizable into the frame "
                             "table");
                    return false;
                }
                if (!ir::core::verify_frame_layout_wire_consistency(
                         builder.table_ref(), dense_result, *projection.table,
                         wire.result)
                         .empty()) {
                    add_diag(result, core_wasm_diag::kUnsupportedCapabilityFrame,
                             "a workflow bridge result is not wire-representable on the "
                             "frame-bridge subset");
                    return false;
                }
                record.result_layout = dense_result;
                const ir::core::CoreLayout &result_layout =
                    builder.table_ref().layouts[dense_result.value];
                const std::uint64_t result_extent = align8(result_layout.size);
                if (result_extent == 0 ||
                    cursor + result_extent > window_end) {
                    add_diag(result, core_wasm_diag::kResourceExhausted,
                             "a workflow bridge result placement exceeds its D6-reserved runner "
                             "window");
                    return false;
                }
                record.result_base = static_cast<std::uint32_t>(cursor);
                record.result_extent = static_cast<std::uint32_t>(result_extent);
                cursor += result_extent;
                std::uint64_t result_bounded = 0;
                bool result_unbounded = false;
                if (!accumulate_input_string_arena(builder.table_ref(),
                                                   dense_result,
                                                   *projection.table,
                                                   wire.result,
                                                   result_bounded,
                                                   result_unbounded)) {
                    add_diag(result, core_wasm_diag::kInvalidLayout,
                             "the workflow bridge result layout and wire schema disagree on a "
                             "String slot");
                    return false;
                }
                std::uint64_t payload_capacity = align8(result_bounded);
                if (result_unbounded) {
                    payload_capacity += kP6FrameStringPoolBytes;
                }
                if (payload_capacity != 0) {
                    cursor = align8(cursor);
                    if (cursor + payload_capacity > window_end) {
                        add_diag(result, core_wasm_diag::kResourceExhausted,
                                 "a workflow bridge result payload arena exceeds its D6-reserved "
                                 "runner window");
                        return false;
                    }
                    record.result_payload_base =
                        static_cast<std::uint32_t>(cursor);
                    record.result_payload_capacity =
                        static_cast<std::uint32_t>(payload_capacity);
                    cursor += payload_capacity;
                }
                record.spill_base = site.spill_base;
                record.spill_extent =
                    static_cast<std::uint32_t>(align8(site.spill_bytes));
                bridge_records.push_back(std::move(record));
                ++site_index;
            }
            // The next runner's window begins exactly where D6 placed it.
            cursor = window_end;
        }
        if (site_index != plan.workflow_bridge_sites.size() ||
            cursor != plan.wf_output_base) {
            add_diag(result,
                     core_wasm_diag::kInternalInvalid,
                     "the workflow bridge result-placement cursor disagrees with the D6 capacity "
                     "family");
            return false;
        }
    }

    ir::core::CoreFrameLayoutSection section;
    section.format_version = 3;
    section.table = builder.table_ref();
    section.input_layout = ir::core::CoreLayoutId{ir::core::CoreLayoutId::kInvalid};
    section.output_layout = wf_output_dense;
    section.rodata_base = ir::core::kP6RodataBase;
    section.rodata_extent = plan.workflow_rodata_extent;
    section.bridge_control_base = plan.bridge_control_base;
    section.bridge_block_stride = plan.bridge_block_stride;
    section.bridge_control_extent =
        static_cast<std::uint32_t>(std::uint64_t{bridge_records.size()} *
                                  plan.bridge_block_stride) +
        workflow_actual_spill_extent;
    section.bridge_spill_base = plan.bridge_spill_base;
    section.bridge_spill_extent = workflow_actual_spill_extent;
    section.bridge_call_sites = std::move(bridge_records);
    section.entry_payload_base = plan.entry_payload_base;
    section.entry_payload_capacity = plan.entry_payload_capacity;
    section.workflow_output_base = plan.wf_output_base;
    section.state_trace_base = plan.state_trace_base;
    section.state_trace_capacity = plan.state_trace_capacity;
    section.node_blocks.reserve(plan.node_blocks.size());
    for (std::uint32_t p6 = 0; p6 < plan.node_blocks.size(); ++p6) {
        const WorkflowNodeBlock &block = plan.node_blocks[p6];
        ir::core::CoreFrameLayoutSection::NodeBlock out;
        out.input_layout = plan.dense_node_input_layouts[p6];
        out.context_layout = plan.dense_node_context_layouts[p6];
        out.output_layout = plan.dense_node_output_layouts[p6];
        out.input_size = block.input_size;
        out.context_size = block.context_size;
        out.output_size = block.output_size;
        out.input_base = block.input_base;
        out.context_base = block.context_base;
        out.scratch_base = block.scratch_base;
        out.scratch_size = block.scratch_size;
        out.output_base = block.output_base;
        section.node_blocks.push_back(std::move(out));
    }
    // Local admission: pairwise disjoint in-page spans and root consistency.
    auto encoded = ir::core::encode_core_frame_layout_section(section);
    if (!encoded.ok()) {
        for (const auto &diag : encoded.diagnostics) {
            add_diag(result, core_wasm_diag::kInvalidLayout, diag.message);
        }
        return false;
    }
    auto decoded = ir::core::decode_core_frame_layout_section(*encoded.bytes);
    if (!decoded.ok()) {
        for (const auto &diag : decoded.diagnostics) {
            add_diag(result, core_wasm_diag::kInvalidLayout, diag.message);
        }
        return false;
    }
    // Boundary layout/wire consistency on every P6 node root and the workflow
    // output root.
    const auto &roots = projection.table->frame_roots;
    if (roots->node_inputs.size() != p6_count ||
        roots->node_outputs.size() != p6_count) {
        add_diag(result, core_wasm_diag::kInvalidLayout,
                 "the workflow wire schema node root count disagrees with the P6 runner table");
        return false;
    }
    for (std::uint32_t p6 = 0; p6 < p6_count; ++p6) {
        auto in_diags = ir::core::verify_frame_layout_wire_consistency(
            section.table, plan.dense_node_input_layouts[p6],
            *projection.table, roots->node_inputs[p6]);
        auto out_diags = ir::core::verify_frame_layout_wire_consistency(
            section.table, plan.dense_node_output_layouts[p6],
            *projection.table, roots->node_outputs[p6]);
        if (!in_diags.empty() || !out_diags.empty()) {
            add_diag(result,
                     core_wasm_diag::kUnsupportedWorkflowFrame,
                     "a P6 workflow node layout/wire boundary is inconsistent");
            return false;
        }
    }
    if (!ir::core::verify_frame_layout_wire_consistency(
             section.table, wf_output_dense, *projection.table, roots->output)
             .empty()) {
        add_diag(result,
                 core_wasm_diag::kUnsupportedWorkflowFrame,
                 "the P6 workflow output layout/wire boundary is inconsistent");
        return false;
    }

    plan.frame_section = std::move(section);
    plan.frame_wire_table = std::move(*projection.table);
    return true;
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
        } else if (const auto *computed =
                       std::get_if<ComputedReturnAction>(&plan.actions[state])) {
            // V2-A: run2 carries opaque wire-JSON and has no P4-D computed-final
            // contract; a computed final is observable only through runv. Its
            // handler materializes into the output frame, so drop the yielded
            // state id and trap here rather than return an undefined tuple.
            append_indexed_op(body, kOpCall, functions.handler(computed->function));
            body.byte(kOpDrop);
            body.byte(kOpUnreachable);
        } else {
            append_capability_return(body, plan, std::get<CapabilityAction>(plan.actions[state]));
        }
        body.byte(kOpEnd);
    }
    body.byte(kOpUnreachable);
    body.byte(kOpEnd);
    return body;
}

// RFC 0026 P6-7 D3 + frame-bridge v2 rung V2-A: runv() ->
// (status:i32, value_ptr:i32). It drives the same deterministic state walk
// run2 performs, then switches on the reached final:
//   * an IDENTITY final returns (OK, kP6AggregateInputBase) — the borrowed
//     input frame, no copy;
//   * a COMPUTED final invokes its materializing handler and returns
//     (OK, kP6AggregateOutputBase);
//   * a capability final is forbidden on the p6-frame lane by planning, so
//     reaching one traps (the fallthrough unreachable).
[[nodiscard]] std::optional<ByteBuffer>
make_runv_body(const AgentPlan &plan, const FunctionTable &functions) {
    ByteBuffer body;
    body.u32(1);
    body.u32(1);
    body.byte(kI32); // local 0 = fuel (runv is parameter-free)

    append_run_to_final(body, plan, functions, 0);
    bool has_arm = false;
    for (std::uint32_t state = 0; state < plan.actions.size(); ++state) {
        const auto &action = plan.actions[state];
        const bool is_identity = std::holds_alternative<IdentityAction>(action);
        const auto *computed = std::get_if<ComputedReturnAction>(&action);
        if (!is_identity && computed == nullptr) {
            // A capability final has no runv arm; reaching it traps.
            continue;
        }
        has_arm = true;
        append_indexed_op(body, kOpGlobalGet, kGlobalCurrentState);
        append_const(body, state);
        body.byte(kOpI32Eq);
        body.byte(kOpIf);
        body.byte(kEmptyBlock);
        if (is_identity) {
            append_const(body, AHFL_CAP_OK);
            append_const(body, kP6AggregateInputBase);
            body.byte(kOpReturn);
        } else {
            // Materialize the output frame, then return its fixed base.
            append_indexed_op(body, kOpCall, functions.handler(computed->function));
            body.byte(kOpDrop);
            append_const(body, AHFL_CAP_OK);
            append_const(body, kP6AggregateOutputBase);
            body.byte(kOpReturn);
        }
        body.byte(kOpEnd);
    }
    if (!has_arm) {
        // A p6-frame module is emitted only when the planner admits at least
        // one identity or computed final; guarding keeps the body fail-closed
        // if that invariant shifts.
        return std::nullopt;
    }
    body.byte(kOpUnreachable);
    body.byte(kOpEnd);
    return body;
}

// Forward declaration: the agent exec-manifest encoder is defined after
// encode_module but emitted by it (WH-4 fix-forward D-C).
[[nodiscard]] std::optional<std::vector<std::uint8_t>>
encode_agent_exec_manifest(const CoreProgram &program, const AgentPlan &plan);

[[nodiscard]] std::optional<std::vector<std::uint8_t>>
encode_module(const CoreProgram &program,
              const AgentPlan &plan,
              std::span<const std::uint8_t> wire_schema_payload,
              std::span<const std::uint8_t> frame_layout_payload,
              bool p6_frame) {
    const FunctionTable functions{static_cast<std::uint32_t>(plan.imports.size()),
                                  static_cast<std::uint32_t>(plan.handlers.size())};
    ByteBuffer module;
    module.raw({0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00});

    const bool has_bridge = !plan.bridge_calls.empty();
    // One capability uses exactly one mode per module: a bridge call site
    // names the bridge functype; every other capability import keeps the opaque
    // tuple type.
    std::vector<bool> import_is_bridge(plan.imports.size(), false);
    if (has_bridge) {
        for (const BridgeCallPlan &site : plan.bridge_calls) {
            const auto it = std::find(plan.imports.begin(), plan.imports.end(), site.capability);
            if (it == plan.imports.end()) {
                return std::nullopt;
            }
            import_is_bridge[static_cast<std::size_t>(
                std::distance(plan.imports.begin(), it))] = true;
        }
    }

    ByteBuffer types;
    types.u32(5 + static_cast<std::uint32_t>(plan.fns.size()) +
                  static_cast<std::uint32_t>(plan.closure_signatures.size()) +
                  (p6_frame ? 1u : 0u) + (has_bridge ? 1u : 0u));
    append_func_type(types, {}, {kI32});
    append_func_type(types, {kI32}, {kI32});
    append_func_type(types, {kI32, kI32}, {});
    append_func_type(types, {kI32, kI32}, {kI32});
    append_func_type(types, {kI32, kI32}, {kI32, kI32, kI32});
    // RFC 0026 P6-7 D3: the additive runv functype index follows every fixed,
    // per-fn, and closure type so no existing index moves. The V2-C bridge
    // functype is appended after runv (it is the very last type).
    const std::uint32_t runv_type_index =
        5u + static_cast<std::uint32_t>(plan.fns.size()) +
        static_cast<std::uint32_t>(plan.closure_signatures.size());
    const std::uint32_t bridge_type_index = runv_type_index + (p6_frame ? 1u : 0u);
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
    // RFC 0026 P6-7 D3: runv `() -> (status:i32, value_ptr:i32)` is appended
    // after every existing type so no index moves. The V2-C bridge functype
    // `(i32)->(i32,i32)` follows as the very LAST type (one mode per capability
    // field; an old host that meets the unknown functype fails closed).
    if (p6_frame) {
        append_func_type(types, {}, {kI32, kI32});
    }
    if (has_bridge) {
        append_func_type(types, {kI32}, {kI32, kI32});
    }
    if (!append_section(module, kSectionType, types)) {
        return std::nullopt;
    }

    if (!plan.imports.empty()) {
        ByteBuffer imports;
        imports.u32(static_cast<std::uint32_t>(plan.imports.size()));
        for (std::size_t ordinal = 0; ordinal < plan.imports.size(); ++ordinal) {
            const auto id = plan.imports[ordinal];
            const auto symbol = *program.capabilities[id.value].symbol_ref.id;
            if (!imports.name("ahfl_cap") || !imports.name("cap_" + std::to_string(symbol))) {
                return std::nullopt;
            }
            imports.byte(kImportFunction);
            imports.u32(import_is_bridge[ordinal] ? bridge_type_index
                                                 : kTypeCapabilityTuple);
        }
        if (!append_section(module, kSectionImport, imports)) {
            return std::nullopt;
        }
    }

    ByteBuffer functions_section;
    functions_section.u32(functions.defined_count() +
                          static_cast<std::uint32_t>(plan.fns.size()) +
                          (p6_frame ? 1u : 0u));
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
    // RFC 0026 P6-7: runv is the last defined function, using the additive
    // runv functype appended after every per-fn/closure type.
    if (p6_frame) {
        functions_section.u32(runv_type_index);
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
    exports.u32(p6_frame ? 10u : 9u);
    const std::uint32_t runv_index =
        functions.import_count + functions.defined_count() +
        static_cast<std::uint32_t>(plan.fns.size());
    const bool exports_ok =
        append_export(exports, "memory", kExportMemory, 0) &&
        append_export(exports, "alloc", kExportFunction, functions.alloc()) &&
        append_export(exports, "dealloc", kExportFunction, functions.dealloc()) &&
        append_export(exports, "run", kExportFunction, functions.run()) &&
        append_export(exports, "run2", kExportFunction, functions.run2()) &&
        append_export(exports, "step", kExportFunction, functions.step()) &&
        append_export(exports, "current_state", kExportFunction, functions.current_state()) &&
        append_export(exports, "transition_count", kExportGlobal, kGlobalTransitionCount) &&
        append_export(exports, "ahfl_abi_version", kExportGlobal, kGlobalAbiVersion) &&
        (!p6_frame || append_export(exports, "runv", kExportFunction, runv_index));
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
             static_cast<std::uint32_t>(plan.fns.size()) + (p6_frame ? 1u : 0u));
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
    // RFC 0026 P6-7 D3: runv is the final code entry on a p6-frame module.
    if (p6_frame) {
        const auto runv = make_runv_body(plan, functions);
        if (!runv.has_value() || !code.sized(*runv)) {
            return std::nullopt;
        }
    }
    if (!append_section(module, kSectionCode, code)) {
        return std::nullopt;
    }

    // RFC 0026 P6-7 frame-bridge v2 D1 (rung V2-B): exactly ONE additive
    // active Data(11) section, in canonical position AFTER Code(10) and before
    // the EOF custom sections. Its single active segment (flags 0: memory 0,
    // active, offset i32.const kP6RodataBase; end) initializes the rodata
    // region with the frozen literal image. A module with no String literal
    // emits no Data section and stays byte-identical to V2-A.
    if (plan.rodata_extent != 0) {
        ByteBuffer data;
        data.u32(1); // one segment
        data.byte(0); // active, implicit memory index 0
        data.byte(kOpI32Const);
        data.s32(static_cast<std::int32_t>(kP6RodataBase));
        data.byte(kOpEnd);
        const std::string &image = plan.rodata.image();
        if (image.size() > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        data.u32(static_cast<std::uint32_t>(image.size()));
        data.raw_span(std::span<const std::uint8_t>{
            reinterpret_cast<const std::uint8_t *>(image.data()), image.size()});
        if (!append_section(module, kSectionData, data)) {
            return std::nullopt;
        }
    }

    // RFC 0026 E4-B1: exactly one wire-schema custom section, fixed after the
    // Code section, present iff the agent has reachable capability imports (the
    // caller passes an empty payload otherwise). The custom payload is the
    // canonical Wasm custom-section framing (name-length LEB + name) followed by
    // the encoded table bytes (`AHFLWS...`) verbatim; no re-projection here.
    // RFC 0026 P6-7 rung A: a P6-frame module carries the deterministic
    // core-layout custom section immediately BEFORE the EOF wire-schema
    // section. Identity/capability agents pass an empty payload and gain
    // neither section, staying byte-identical.
    if (!frame_layout_payload.empty()) {
        ByteBuffer frame_custom;
        if (!frame_custom.name(kCoreLayoutSectionName)) {
            return std::nullopt;
        }
        frame_custom.raw_span(frame_layout_payload);
        if (!append_section(module, kSectionCustom, frame_custom)) {
            return std::nullopt;
        }
    }

    // RFC 0026 KR6.8 WH-4 fix-forward D-C: a capability agent ends with the
    // exec-manifest custom section (AHFLXM) EXACTLY ONCE, IMMEDIATELY BEFORE
    // the wire-schema custom section (AHFLWS), mirroring the capability-
    // workflow emission. The manifest is the flat capability list the A2
    // decoder's agent arm turns into call sites (one per capability).
    //
    // The predicate is `!plan.imports.empty()`: EVERY capability agent carries
    // the manifest -- both the opaque-lane WireJson agent AND the bridge-lane
    // P6-frame agent. The WH-3 capability_import executor
    // (make_capability_import_callback) is the single production import path
    // for both lanes, and it requires the A2-admitted module (which requires
    // the manifest). A bridge agent is a FRAME module (it carries the
    // core-layout custom + bridge control-block coordinates) WITH a bridge
    // import; its section order is [core-layout, AHFLXM, AHFLWS]. A2 tolerates
    // the core-layout custom before the manifest (an unknown custom before the
    // manifest is skipped), so A2 admission succeeds. The frame admit path
    // (admit_core_wasm_frame_sections, exactly-two-custom-sections) is for
    // PURE frame agents (no imports) only; a bridge agent never goes through
    // it -- it goes through A2 + the WH-3 executor, which walks the bridge
    // control block from the descriptor's frame_section.
    if (!plan.imports.empty()) {
        auto manifest = encode_agent_exec_manifest(program, plan);
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
    }

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
        out.byte(static_cast<std::uint8_t>(node.capabilities.size()));
        for (const auto &[cap, sym] : node.capabilities) {
            if (cap.value == CoreCapabilityId::kInvalid) {
                return std::nullopt;
            }
            out.u32(cap.value);
            out.u64(sym);
        }
    }
    return std::move(out).take();
}

// RFC 0026 KR6.8 WH-4 fix-forward D-C: emit the AHFLXM execution-manifest
// payload for a capability AGENT module, byte-identical to the A2 decoder's
// agent arm. Grammar: magic(6) + version(1) + entry_kind=1 + agent_id(4) +
// capability_count(4) + capabilities[] { capability_id(4) + source_symbol(8)
// }. An agent has no workflow schedule / nodes, so the manifest is a flat
// capability list; each capability becomes one A2 call site. EVERY capability
// agent reaches here (opaque-lane WireJson AND bridge-lane P6-frame): the WH-3
// executor is the single production import path for both lanes and needs the
// A2-admitted module, so the caller gates emission on `!plan.imports.empty()`.
[[nodiscard]] std::optional<std::vector<std::uint8_t>>
encode_agent_exec_manifest(const CoreProgram &program, const AgentPlan &plan) {
    if (plan.agent.value == CoreAgentId::kInvalid) {
        return std::nullopt;
    }
    if (plan.imports.size() >
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return std::nullopt;
    }
    ByteBuffer out;
    out.raw_span(
        std::span<const std::uint8_t>(kExecManifestMagic.data(), kExecManifestMagic.size()));
    out.byte(kExecManifestVersion);
    out.byte(kExecManifestEntryKindAgent);
    out.u32(plan.agent.value);
    out.u32(static_cast<std::uint32_t>(plan.imports.size()));
    for (const auto cap_id : plan.imports) {
        if (cap_id.value == CoreCapabilityId::kInvalid) {
            return std::nullopt;
        }
        const auto &symbol = program.capabilities[cap_id.value].symbol_ref;
        if (!symbol.id.has_value()) {
            return std::nullopt;
        }
        out.u32(cap_id.value);
        out.u64(*symbol.id);
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

// V2-D: the P6 tuple runner for one packaged computed node (computed-final
// half 1; computed-goto + in-handler capability bridge half 2). Same
// (i32,i32)->(i32,i32,i32) signature as the opaque runner (the scheduler
// passes I_k/I_k-size and receives OK,O_k,O_k-size). It drives the packaged
// agent's deterministic state machine on the runner's private state global:
//   * a plain GotoAction is taken inline (latch the target state, bump the
//     workflow transition counter exactly once -- the same evidence the
//     opaque GotoAction walker emits);
//   * a ComputedGotoAction invokes the relocated computed handler, which runs
//     the non-final computation region (including an ordered capability
//     bridge), latches its own successor state and bumps the counter once on
//     the goto path it takes;
//   * a ComputedReturnAction invokes the relocated materializing handler and
//     returns (OK, O_k, O_k-size);
//   * an Identity / opaque Capability action cannot occur on the P6 node lane
//     (the workflow packager routes opaque terminals to the opaque runner), so
//     reaching one traps. A bounded fuel ladder makes a non-terminating
//     action graph trap instead of looping forever.
    // V2-D emission half 2: append one (runner, state) 8-byte record to the fixed
// state-entry trace ring using the runner's one fuel local (local 2) as
// scratch. Layout: u32 count at [base], records at [base+8+8*i]. A stored
// count outside the record window traps (fail-closed; the record capacity was
// sized for every declared state of every packaged agent).
void append_state_trace_record(ByteBuffer &body,
                               std::uint32_t trace_base,
                               std::uint32_t trace_capacity,
                               std::uint32_t runner,
                               std::uint32_t state) {
    constexpr std::uint32_t kScratch = 3u;
    // scratch = count (loaded from the header).
    append_const(body, trace_base);
    body.byte(kOpI32Load);
    body.u32(kAlignI32);
    body.u32(0);
    append_indexed_op(body, kOpLocalSet, kScratch);
    // if (count*8 + 8 > capacity-8) trap  (unsigned multiply + checked add).
    append_indexed_op(body, kOpLocalGet, kScratch);
    append_const(body, 8u);
    body.byte(kOpI32Mul);
    append_const(body, 8u);
    body.byte(kOpI32Add);
    append_const(body, trace_capacity - 8u);
    body.byte(kOpI32GtU);
    body.byte(kOpIf);
    body.byte(kEmptyBlock);
    body.byte(kOpUnreachable);
    body.byte(kOpEnd);
    // scratch = trace_base + 8 + count*8.
    append_const(body, trace_base + 8u);
    append_indexed_op(body, kOpLocalGet, kScratch);
    append_const(body, 8u);
    body.byte(kOpI32Mul);
    body.byte(kOpI32Add);
    append_indexed_op(body, kOpLocalSet, kScratch);
    // [scratch+0] = runner, [scratch+4] = state.
    append_indexed_op(body, kOpLocalGet, kScratch);
    append_const(body, runner);
    body.byte(kOpI32Store);
    body.u32(kAlignI32);
    body.u32(0);
    append_indexed_op(body, kOpLocalGet, kScratch);
    append_const(body, state);
    body.byte(kOpI32Store);
    body.u32(kAlignI32);
    body.u32(4);
    // count header += 1 (read the current count, add one, publish).
    append_const(body, trace_base);
    append_const(body, trace_base);
    body.byte(kOpI32Load);
    body.u32(kAlignI32);
    body.u32(0);
    append_const(body, 1);
    body.byte(kOpI32Add);
    body.byte(kOpI32Store);
    body.u32(kAlignI32);
    body.u32(0);
}

[[nodiscard]] std::optional<ByteBuffer>
make_workflow_p6_runner_body(const AgentPlan &agent_plan,
                             std::uint32_t runner_index,
                             const WorkflowNodeBlock &block,
                             std::uint32_t handler_base,
                             std::uint32_t state_global,
                             std::uint32_t state_trace_base,
                             std::uint32_t state_trace_capacity,
                             const WorkflowFunctionTable &functions) {
    (void)functions;
    (void)runner_index;
    if (agent_plan.actions.empty()) {
        return std::nullopt;
    }

    ByteBuffer body;
    body.u32(1);
    body.u32(2);
    body.byte(kI32); // locals 2 = bounded walk fuel, 3 = state-trace scratch
    // Reset the runner's private state global to the initial state on every
    // node invocation (one packaged instance runs exactly once per run2 today,
    // but the reset makes re-invocation deterministic).
    append_const(body, agent_plan.initial.value);
    append_indexed_op(body, kOpGlobalSet, state_global);
    append_const(body, static_cast<std::uint32_t>(agent_plan.actions.size()) + 1u);
    append_indexed_op(body, kOpLocalSet, 2);

    body.byte(kOpBlock);
    body.byte(kEmptyBlock); // $exit
    body.byte(kOpLoop);
    body.byte(kEmptyBlock); // $continue

    // Fuel guard.
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

    // Dispatch ladder on the runner's current-state global. Each branch pushes
    // its own fresh copy of the state word (the if/else chain consumes one
    // compare value per branch, exactly like the agent step() ladder). A
    // branch for state `state` is nested `state + 1` ifs deep inside the loop:
    // label 0 is its innermost if, label `state + 1` is the loop (continue the
    // dispatch), and label `state + 2` is the enclosing block (exit to the
    // runner's OK/O_k return).
    for (std::uint32_t state = 0; state < agent_plan.actions.size(); ++state) {
        const StateAction &action = agent_plan.actions[state];
        append_indexed_op(body, kOpGlobalGet, state_global);
        body.byte(kOpI32Const);
        body.s32_nonnegative(state);
        body.byte(kOpI32Eq);
        body.byte(kOpIf);
        body.byte(kEmptyBlock);
        const std::uint32_t continue_depth = state + 1u;
        const std::uint32_t exit_depth = state + 2u;
        // Real state-entry evidence: one record for every dispatched state
        // (plain goto, computed goto successor, computed final).
        append_state_trace_record(body, state_trace_base,
                                  state_trace_capacity, runner_index, state);
        if (const auto *go = std::get_if<GotoAction>(&action)) {
            append_const(body, go->target.value);
            append_indexed_op(body, kOpGlobalSet, state_global);
            append_indexed_op(body, kOpGlobalGet, kWorkflowGlobalTransitionCount);
            append_const(body, 1);
            body.byte(kOpI32Add);
            append_indexed_op(body, kOpGlobalSet, kWorkflowGlobalTransitionCount);
            body.byte(kOpBr);
            body.u32(continue_depth); // -> $continue
        } else if (const auto *computed_go =
                       std::get_if<ComputedGotoAction>(&action)) {
            append_indexed_op(body, kOpCall,
                             handler_base + computed_go->function);
            body.byte(kOpDrop);
            body.byte(kOpBr);
            body.u32(continue_depth); // -> $continue
        } else if (const auto *computed_return =
                       std::get_if<ComputedReturnAction>(&action)) {
            // The handler materializes O_k; leave the exit block and return it.
            append_indexed_op(body, kOpCall,
                             handler_base + computed_return->function);
            body.byte(kOpDrop);
            body.byte(kOpBr);
            body.u32(exit_depth); // -> $exit
        } else {
            body.byte(kOpUnreachable);
        }
        body.byte(kOpElse);
    }
    body.byte(kOpUnreachable);
    for (std::size_t state = 0; state < agent_plan.actions.size(); ++state) {
        body.byte(kOpEnd);
    }
    body.byte(kOpEnd); // $continue
    body.byte(kOpEnd); // $exit

    append_const(body, AHFL_CAP_OK);
    append_const(body, block.output_base);
    append_const(body, block.output_size);
    body.byte(kOpEnd);
    return body;
}

// ----------------------------------------------------------------------------
// V2-D in-module P4-D frame materializer
//
// The scheduler emits, before each packaged P6 node runner and after the whole
// schedule, word-by-word P4-D copy sequences that build projected/constructed
// node input frames and the workflow return frame (design §6.3). It is the
// scheduler-side twin of the V2-A computed-final materializer:
//   * a scalar / tag-only-enum slot is copied width-exactly (i32 / i64);
//   * a String PtrLen slot is copied as its two inline words, so the payload
//     pointer is shared zero-copy (it names only rodata / the entry arena / an
//     upstream node block, all stable for the run);
//   * aggregate children are EXPANDED inline from the module pointer tree the
//     source region writes, including the active-variant-only enum payload
//     ladder;
//   * padding is established by a bounded zero-fill of the destination region.
//
// A path let is either a bare root value (the frame itself) or a projected
// field: its source base is the workflow entry input (run2 local 0) or a
// predecessor node's O_k block, and an aggregate field is reached through the
// SAME i32-address dereference walk a frame handler projection uses. A
// construct let lays its aggregate out in the scheduler's fixed construct
// scratch region. The materializer keeps one address-stack local per aggregate
// nesting depth (the caller reserves them in run2's local pool).
// ----------------------------------------------------------------------------

class WorkflowFrameMaterializer {
  public:
    WorkflowFrameMaterializer(const CoreProgram &program,
                              const ir::core::CoreLayoutTable &layouts,
                              const WorkflowPlan &plan,
                              CoreWasmCodegenResult &result)
        : program_(program), layouts_(layouts), plan_(plan), result_(result) {}

    // Number of address locals one run2 invocation needs for this plan. The
    // materializer uses one local per aggregate level (depth+1); the entry
    // normalizer uses a paired (src,dst) pair per level (2*(depth+1)). Return
    // the larger requirement so a single reserved run2 block serves both.
    [[nodiscard]] static std::uint32_t
    required_locals(const CoreProgram &program,
                    const ir::core::CoreLayoutTable &layouts,
                    const WorkflowPlan &plan) {
        std::uint32_t materializer_depth = 0;
        std::uint32_t normalize_depth = 0;
        auto consider_layout = [&](CoreLayoutId id, std::uint32_t &depth) {
            depth = std::max(depth, layout_copy_depth(layouts, id));
        };
        auto consider_value = [&](CoreValueTypeId vt, std::uint32_t &depth) {
            if (vt.value < layouts.value_layouts.size()) {
                consider_layout(layouts.value_layouts[vt.value], depth);
            }
        };
        (void)program;
        consider_value(plan.wf_output_vt, materializer_depth);
        if (plan.entry_normalize_extent != 0) {
            // The entry normalization walks the entry node's input layout.
            if (!plan.schedule.empty()) {
                const auto &node = plan.nodes[plan.schedule.front().value];
                if (auto r = workflow_runner_index(plan, node.target_instance);
                    r.has_value() &&
                    plan.p6_block_by_runner[*r] != kInvalidP6Block) {
                    consider_layout(
                        CoreLayoutId{plan.node_blocks[plan.p6_block_by_runner[*r]]
                                         .input_layout},
                        normalize_depth);
                }
            }
        }
        for (const WorkflowRegionPlan &region : plan.node_regions) {
            for (const WorkflowFrameLet &let : region.lets) {
                consider_value(let.value_type, materializer_depth);
            }
        }
        for (const WorkflowFrameLet &let : plan.return_region.lets) {
            consider_value(let.value_type, materializer_depth);
        }
        const std::uint32_t materializer_locals = materializer_depth + 1u;
        // The normalizer always needs the ROOT paired (src,dst) address locals,
        // even for a flat struct whose only children are scalar/PtrLen/tag
        // words (depth 0): normalize_inline_input latches norm_src(0) and
        // norm_dst(0) unconditionally, and every deeper aggregate nesting
        // level adds one more pair.
        const std::uint32_t normalize_locals =
            normalize_depth == 0u ? 2u : 2u * (normalize_depth + 1u);
        return std::max(materializer_locals, normalize_locals);
    }

    // Materialize one region into the frame at dst_base (a node I_k block or
    // the workflow output slot). entry_ptr_local is run2's local naming the
    // host-packed entry input pointer. addr_base is the first reserved
    // address-stack local; the zero-fill cursor takes addr_base-1.
    [[nodiscard]] bool emit(ByteBuffer &body,
                            const WorkflowRegionPlan &region,
                            std::uint32_t dst_base,
                            CoreLayoutId dst_layout,
                            std::uint32_t entry_ptr_local,
                            std::uint32_t cursor_local,
                            std::uint32_t addr_base) {
        if (dst_layout.value >= layouts_.layouts.size()) {
            return fail("a materialized frame names an out-of-range destination layout");
        }
        const std::uint64_t dst_size = layouts_.layouts[dst_layout.value].size;
        if (dst_size == 0 || dst_size % 4 != 0 ||
            dst_size > std::numeric_limits<std::uint32_t>::max()) {
            return fail("a materialized frame destination has an invalid size");
        }
        body_ = &body;
        entry_ptr_local_ = entry_ptr_local;
        cursor_local_ = cursor_local;
        addr_base_ = addr_base;
        construct_cursor_ = 0;
        lets_ = region.lets;
        construct_off_.clear();

        if (!emit_zero_fill(dst_base, static_cast<std::uint32_t>(dst_size))) {
            return false;
        }
        // Build every aggregate construct into scratch in ANF order.
        for (const WorkflowFrameLet &let : lets_) {
            if (!let.is_construct) {
                continue;
            }
            const CoreLayoutId root = layouts_.value_layouts[let.value_type.value];
            const std::uint32_t off = reserve_construct(let.result, root);
            if (off == std::numeric_limits<std::uint32_t>::max()) {
                return false;
            }
            if (!emit_construct(let, root, plan_.region_scratch_base + off)) {
                return false;
            }
        }
        // The yield value lands at the destination root.
        if (!emit_yield(region.yield_value, dst_layout, dst_base, 0)) {
            return false;
        }
        return true;
    }

    // V2-D RETURN: turn the host-packed INLINE entry frame in I_k (the host
    // packs it at run2's input pointer, which IS the entry node's I_k base)
    // into the module POINTER-TREE form, so a packaged handler that reads an
    // aggregate field off its input frame dereferences a child address like on
    // the module's own construct path. Scalars/PtrLen/tag words stay inline in
    // place; each struct/enum child is copied into its own addressed,
    // zero-filled window in the entry-normalize scratch and the parent slot
    // stores that window's address. An enum child copies the discriminant plus
    // ONLY the active variant's payload (a run-time ladder), exactly as the
    // V2-A materializer does. A container/Uuid/closure field fails closed.
    //
    // The transform is IN-PLACE at the root: the host zero-fills the frame
    // regions before packing, so the root is never bulk-zeroed here (that would
    // destroy the inline source bytes the windows copy from); each child window
    // in scratch is zero-filled before its active words are copied.
    //
    // Local layout (all i32): the zero-fill cursor (cursor_local), the
    // allocation cursor (normalize_cursor), then paired (src,dst) address
    // locals per nesting level (addr_base+2*level is src, +2*level+1 dst).
    [[nodiscard]] bool normalize_inline_input(ByteBuffer &body,
                                              CoreLayoutId root,
                                              std::uint32_t dst_base,
                                              std::uint32_t entry_ptr_local,
                                              std::uint32_t cursor_local,
                                              std::uint32_t normalize_cursor,
                                              std::uint32_t addr_base) {
        if (root.value >= layouts_.layouts.size() ||
            plan_.entry_normalize_base == 0) {
            return fail("entry normalization runs without a planned scratch region");
        }
        body_ = &body;
        entry_ptr_local_ = entry_ptr_local;
        cursor_local_ = cursor_local;
        normalize_cursor_local_ = normalize_cursor;
        addr_base_ = addr_base;
        emit_const(plan_.entry_normalize_base);
        append_indexed_op(*body_, kOpLocalSet, normalize_cursor_local_);
        // Root source and destination are the same I_k window.
        append_indexed_op(*body_, kOpLocalGet, entry_ptr_local_);
        append_indexed_op(*body_, kOpLocalSet, norm_src(0));
        emit_const(dst_base);
        append_indexed_op(*body_, kOpLocalSet, norm_dst(0));
        return normalize_walk(root, 0);
    }

    [[nodiscard]] std::uint32_t norm_src(std::uint32_t level) const {
        return addr_base_ + 2u * level;
    }
    [[nodiscard]] std::uint32_t norm_dst(std::uint32_t level) const {
        return addr_base_ + 2u * level + 1u;
    }

    // Copy [src_local+src_off] -> [dst_local+dst_off], one i32/i64 word or the
    // two PtrLen words.
    void norm_copy_word(bool wide, std::uint32_t src_local, std::uint32_t src_off,
                        std::uint32_t dst_local, std::uint32_t dst_off) {
        append_indexed_op(*body_, kOpLocalGet, dst_local);
        if (dst_off != 0) {
            emit_const(dst_off);
            body_->byte(kOpI32Add);
        }
        append_indexed_op(*body_, kOpLocalGet, src_local);
        if (src_off != 0) {
            emit_const(src_off);
            body_->byte(kOpI32Add);
        }
        body_->byte(wide ? kOpI64Load : kOpI32Load);
        body_->u32(wide ? kAlignI64 : kAlignI32);
        body_->u32(0);
        body_->byte(wide ? kOpI64Store : kOpI32Store);
        body_->u32(wide ? kAlignI64 : kAlignI32);
        body_->u32(0);
    }

    void norm_copy_leaf(const ir::core::CoreLayout &field,
                        std::uint32_t src_local, std::uint32_t src_off,
                        std::uint32_t dst_local, std::uint32_t dst_off) {
        const bool wide =
            std::holds_alternative<ir::core::CoreLayoutScalar>(field.shape) &&
            std::get_if<ir::core::CoreLayoutScalar>(&field.shape)->repr ==
                ir::core::CoreScalarRepr::I64;
        norm_copy_word(wide, src_local, src_off, dst_local, dst_off);
        if (std::holds_alternative<ir::core::CoreLayoutPtrLen>(field.shape)) {
            norm_copy_word(false, src_local, src_off + 4u, dst_local, dst_off + 4u);
        }
    }

    // Allocate one aligned child window in normalize scratch, zero it, and
    // latch its address into norm_dst(level+1). The source frame is INLINE, so
    // the child bytes sit in place at norm_src(parent)+src_slot_off (a plain
    // address, NOT a loaded child pointer); that computed address becomes
    // norm_src(level+1).
    [[nodiscard]] bool norm_enter_child(CoreLayoutId edge,
                                        std::uint32_t parent_level,
                                        std::uint32_t src_slot_off) {
        append_indexed_op(*body_, kOpLocalGet, norm_src(parent_level));
        if (src_slot_off != 0) {
            emit_const(src_slot_off);
            body_->byte(kOpI32Add);
        }
        append_indexed_op(*body_, kOpLocalSet, norm_src(parent_level + 1u));
        const std::uint32_t size = static_cast<std::uint32_t>(
            (layouts_.layouts[edge.value].size + 7u) & ~std::uint64_t{7u});
        append_indexed_op(*body_, kOpLocalGet, normalize_cursor_local_);
        append_indexed_op(*body_, kOpLocalTee, norm_dst(parent_level + 1u));
        emit_const(size);
        body_->byte(kOpI32Add);
        append_indexed_op(*body_, kOpLocalSet, normalize_cursor_local_);
        if (!emit_zero_fill_local(norm_dst(parent_level + 1u),
                                  static_cast<std::uint32_t>(
                                      (layouts_.layouts[edge.value].size + 3u) &
                                      ~std::uint64_t{3u}))) {
            return false;
        }
        return true;
    }

    // Store norm_dst(level+1) into the parent destination slot, publishing the
    // child window address (the pointer-tree edge).
    void norm_publish_child(std::uint32_t parent_level, std::uint32_t dst_slot_off) {
        append_indexed_op(*body_, kOpLocalGet, norm_dst(parent_level));
        if (dst_slot_off != 0) {
            emit_const(dst_slot_off);
            body_->byte(kOpI32Add);
        }
        append_indexed_op(*body_, kOpLocalGet, norm_dst(parent_level + 1u));
        body_->byte(kOpI32Store);
        body_->u32(kAlignI32);
        body_->u32(0);
    }

    // Copy ONLY the active variant's payload fields (scalars/PtrLen) for an
    // enum window. A nested aggregate inside a payload fails closed (a later
    // rung); the discriminant itself is copied by the caller.
    [[nodiscard]] bool norm_copy_active_enum_payload(
        const ir::core::CoreLayoutEnum &tagged, std::uint32_t level) {
        for (std::uint32_t ordinal = 0;
             ordinal < tagged.variant_payload_layouts.size(); ++ordinal) {
            const CoreLayoutId payload = tagged.variant_payload_layouts[ordinal];
            if (payload.value >= layouts_.layouts.size()) {
                return fail("entry normalization names an out-of-range enum payload");
            }
            const auto *fields =
                std::get_if<ir::core::CoreLayoutStruct>(&layouts_.layouts[payload.value].shape);
            if (fields == nullptr) {
                return fail("an enum payload layout is not a struct");
            }
            if (fields->field_layouts.empty()) {
                continue;
            }
            append_indexed_op(*body_, kOpLocalGet, norm_src(level));
            body_->byte(kOpI32Load);
            body_->u32(kAlignI32);
            body_->u32(0);
            emit_const(ordinal);
            body_->byte(kOpI32Eq);
            body_->byte(kOpIf);
            body_->byte(kEmptyBlock);
            for (std::uint32_t i = 0; i < fields->field_layouts.size(); ++i) {
                const CoreLayoutId field_id = fields->field_layouts[i];
                if (field_id.value >= layouts_.layouts.size()) {
                    return fail("entry normalization walks an out-of-range payload field");
                }
                const ir::core::CoreLayout &field = layouts_.layouts[field_id.value];
                if (!leaf_is_word(field) &&
                    !std::holds_alternative<ir::core::CoreLayoutPtrLen>(field.shape)) {
                    return fail("entry normalization of a nested aggregate inside an enum "
                                "payload is a later rung");
                }
                const std::uint32_t off =
                    static_cast<std::uint32_t>(tagged.payload_offset) +
                    static_cast<std::uint32_t>(fields->field_offsets[i]);
                norm_copy_leaf(field, norm_src(level), off, norm_dst(level), off);
            }
            body_->byte(kOpEnd);
        }
        return true;
    }

    // Zero-fill [dst_local .. +size) using cursor_local_.
    [[nodiscard]] bool emit_zero_fill_local(std::uint32_t dst_local,
                                            std::uint32_t size) {
        emit_const(0);
        append_indexed_op(*body_, kOpLocalSet, cursor_local_);
        body_->byte(kOpBlock);
        body_->byte(kEmptyBlock);
        body_->byte(kOpLoop);
        body_->byte(kEmptyBlock);
        append_indexed_op(*body_, kOpLocalGet, cursor_local_);
        emit_const(size);
        body_->byte(kOpI32GeU);
        body_->byte(kOpBrIf);
        body_->u32(1);
        append_indexed_op(*body_, kOpLocalGet, dst_local);
        append_indexed_op(*body_, kOpLocalGet, cursor_local_);
        body_->byte(kOpI32Add);
        emit_const(0);
        body_->byte(kOpI32Store);
        body_->u32(kAlignI32);
        body_->u32(0);
        append_indexed_op(*body_, kOpLocalGet, cursor_local_);
        emit_const(4);
        body_->byte(kOpI32Add);
        append_indexed_op(*body_, kOpLocalSet, cursor_local_);
        body_->byte(kOpBr);
        body_->u32(0);
        body_->byte(kOpEnd);
        body_->byte(kOpEnd);
        return true;
    }

    [[nodiscard]] bool normalize_walk(CoreLayoutId id, std::uint32_t level) {
        if (id.value >= layouts_.layouts.size()) {
            return fail("entry normalization walks an out-of-range layout");
        }
        const ir::core::CoreLayout &layout = layouts_.layouts[id.value];
        const auto *structure = std::get_if<ir::core::CoreLayoutStruct>(&layout.shape);
        if (structure == nullptr) {
            if (level == 0 &&
                std::holds_alternative<ir::core::CoreLayoutEnum>(layout.shape)) {
                // The root frame IS the enum at its own base: its tag and active
                // payload already sit inline at fixed offsets and no parent slot
                // ever addresses them, so nothing moves.
                return true;
            }
            return fail("entry normalization reaches a non-struct frame level");
        }
        for (std::uint32_t i = 0; i < structure->field_layouts.size(); ++i) {
            const CoreLayoutId edge = structure->field_layouts[i];
            if (edge.value >= layouts_.layouts.size()) {
                return fail("entry normalization walks an out-of-range field");
            }
            const std::uint32_t off =
                static_cast<std::uint32_t>(structure->field_offsets[i]);
            const ir::core::CoreLayout &field = layouts_.layouts[edge.value];
            if (leaf_is_word(field) ||
                std::holds_alternative<ir::core::CoreLayoutPtrLen>(field.shape)) {
                norm_copy_leaf(field, norm_src(level), off, norm_dst(level), off);
                continue;
            }
            if (std::holds_alternative<ir::core::CoreLayoutStruct>(field.shape)) {
                // Inline source child -> addressed destination window; the
                // parent slot stores that window's address.
                if (!norm_enter_child(edge, level, off)) {
                    return false;
                }
                if (!normalize_walk(edge, level + 1u)) {
                    return false;
                }
                norm_publish_child(level, off);
                continue;
            }
            if (const auto *tagged =
                    std::get_if<ir::core::CoreLayoutEnum>(&field.shape)) {
                if (!norm_enter_child(edge, level, off)) {
                    return false;
                }
                // Discriminant, then ONLY the active variant's payload.
                norm_copy_word(false, norm_src(level + 1u), 0,
                               norm_dst(level + 1u), 0);
                if (!norm_copy_active_enum_payload(*tagged, level + 1u)) {
                    return false;
                }
                norm_publish_child(level, off);
                continue;
            }
            return fail("entry normalization reaches a collection/Uuid/closure field");
        }
        return true;
    }


  public:
    const CoreProgram &program_;
    const ir::core::CoreLayoutTable &layouts_;
    const WorkflowPlan &plan_;
    CoreWasmCodegenResult &result_;
    ByteBuffer *body_{nullptr};
    std::uint32_t entry_ptr_local_{0};
    std::uint32_t cursor_local_{0};
    std::uint32_t normalize_cursor_local_{0};
    std::uint32_t addr_base_{0};
    std::uint32_t construct_cursor_{0};
    std::span<const WorkflowFrameLet> lets_;
    // Construct-scratch window offset keyed by the SSA CoreValueId a region
    // let binds (never the value-type index: those are separate index spaces).
    std::unordered_map<std::uint32_t, std::uint32_t> construct_off_;

    [[nodiscard]] bool fail(std::string message) {
        add_diag(result_, core_wasm_diag::kUnsupportedWorkflowFrame, std::move(message));
        return false;
    }

    void emit_const(std::uint32_t value) { append_const(*body_, value); }

    [[nodiscard]] std::uint32_t addr_local(std::uint32_t level) const {
        return addr_base_ + level;
    }

    // Deepest aggregate nesting of one layout (0 for a scalar/flat struct).
    [[nodiscard]] static std::uint32_t
    layout_copy_depth(const ir::core::CoreLayoutTable &layouts, CoreLayoutId id) {
        if (id.value >= layouts.layouts.size()) {
            return 0;
        }
        const ir::core::CoreLayout &layout = layouts.layouts[id.value];
        std::uint32_t deepest = 0;
        if (const auto *s = std::get_if<ir::core::CoreLayoutStruct>(&layout.shape)) {
            for (const CoreLayoutId child : s->field_layouts) {
                if (child.value >= layouts.layouts.size()) {
                    continue;
                }
                const ir::core::CoreLayout &child_layout = layouts.layouts[child.value];
                if (!leaf_is_word_static(child_layout) &&
                    (std::holds_alternative<ir::core::CoreLayoutStruct>(child_layout.shape) ||
                     std::holds_alternative<ir::core::CoreLayoutContainer>(child_layout.shape) ||
                     std::holds_alternative<ir::core::CoreLayoutEnum>(child_layout.shape))) {
                    deepest = std::max(deepest, 1u + layout_copy_depth(layouts, child));
                }
            }
        } else if (const auto *e = std::get_if<ir::core::CoreLayoutEnum>(&layout.shape)) {
            for (const CoreLayoutId payload : e->variant_payload_layouts) {
                deepest = std::max(deepest, layout_copy_depth(layouts, payload));
            }
        }
        return deepest;
    }

    [[nodiscard]] static bool leaf_is_word_static(const ir::core::CoreLayout &layout) {
        if (const auto *scalar = std::get_if<ir::core::CoreLayoutScalar>(&layout.shape)) {
            return scalar->repr != ir::core::CoreScalarRepr::F64;
        }
        if (const auto *tagged = std::get_if<ir::core::CoreLayoutEnum>(&layout.shape)) {
            return std::ranges::all_of(tagged->variant_payload_sizes,
                                      [](std::uint64_t size) { return size == 0; });
        }
        return false;
    }

    [[nodiscard]] bool leaf_is_word(const ir::core::CoreLayout &layout) const {
        return leaf_is_word_static(layout);
    }

    // Bounded cursor loop zeroing [base, +size) one i32 word at a time.
    [[nodiscard]] bool emit_zero_fill(std::uint32_t base, std::uint32_t size) {
        emit_const(0);
        append_indexed_op(*body_, kOpLocalSet, cursor_local_);
        body_->byte(kOpBlock);
        body_->byte(kEmptyBlock);
        body_->byte(kOpLoop);
        body_->byte(kEmptyBlock);
        append_indexed_op(*body_, kOpLocalGet, cursor_local_);
        emit_const(size);
        body_->byte(kOpI32GeU);
        body_->byte(kOpBrIf);
        body_->u32(1);
        emit_const(base);
        append_indexed_op(*body_, kOpLocalGet, cursor_local_);
        body_->byte(kOpI32Add);
        emit_const(0);
        body_->byte(kOpI32Store);
        body_->u32(kAlignI32);
        body_->u32(0);
        append_indexed_op(*body_, kOpLocalGet, cursor_local_);
        emit_const(4);
        body_->byte(kOpI32Add);
        append_indexed_op(*body_, kOpLocalSet, cursor_local_);
        body_->byte(kOpBr);
        body_->u32(0);
        body_->byte(kOpEnd);
        body_->byte(kOpEnd);
        return true;
    }

    [[nodiscard]] const WorkflowFrameLet *find_let(CoreValueId value) const {
        for (const WorkflowFrameLet &let : lets_) {
            if (let.result == value) {
                return &let;
            }
        }
        return nullptr;
    }

    // Push the ABSOLUTE base of a path root: the entry input pointer or an
    // upstream P6 node's O_k block base. An upstream OPAQUE node owns no fixed
    // output block (its result lives in the bump heap as wire JSON), so a
    // frame region that roots an opaque output is not materializable on this
    // rung.
    [[nodiscard]] bool emit_root_base(const WorkflowFrameSource &source) {
        if (source.kind == WorkflowFrameSourceKind::Input) {
            append_indexed_op(*body_, kOpLocalGet, entry_ptr_local_);
            return true;
        }
        const auto runner =
            workflow_runner_index(plan_, plan_.nodes[source.node.value].target_instance);
        if (!runner.has_value() ||
            plan_.p6_block_by_runner[*runner] == kInvalidP6Block) {
            return false;
        }
        emit_const(
            plan_.node_blocks[plan_.p6_block_by_runner[*runner]].output_base);
        return true;
    }

    // Latch the address of one path's FINAL field slot (or the bare root itself
    // when projection is empty) into addr_local(level).
    [[nodiscard]] bool latch_path_slot(const WorkflowFrameLet &let, std::uint32_t level) {
        if (!emit_root_base(let.source)) {
            return false;
        }
        const auto &projection = let.path->projection;
        for (std::size_t i = 0; i < projection.size(); ++i) {
            const auto *owner = p6_nominal_struct_layout(
                program_, layouts_, projection[i].owner_type);
            if (owner == nullptr || projection[i].field.value >= owner->field_offsets.size()) {
                return false;
            }
            const std::uint32_t off =
                static_cast<std::uint32_t>(owner->field_offsets[projection[i].field.value]);
            const bool last = i + 1 == projection.size();
            if (off != 0) {
                emit_const(off);
                body_->byte(kOpI32Add);
            }
            if (!last) {
                body_->byte(kOpI32Load);
                body_->u32(kAlignI32);
                body_->u32(0);
            }
        }
        append_indexed_op(*body_, kOpLocalSet, addr_local(level));
        return true;
    }

    [[nodiscard]] std::optional<CoreLayoutId> path_leaf_layout(const WorkflowFrameLet &let) const {
        if (let.path->projection.empty()) {
            if (let.value_type.value >= layouts_.value_layouts.size()) {
                return std::nullopt;
            }
            return layouts_.value_layouts[let.value_type.value];
        }
        const auto &step = let.path->projection.back();
        const auto *owner =
            p6_nominal_struct_layout(program_, layouts_, step.owner_type);
        if (owner == nullptr || step.field.value >= owner->field_layouts.size()) {
            return std::nullopt;
        }
        return owner->field_layouts[step.field.value];
    }

    [[nodiscard]] std::uint32_t reserve_construct(CoreValueId result,
                                                  CoreLayoutId root) {
        if (root.value >= layouts_.layouts.size()) {
            add_diag(result_, core_wasm_diag::kInvalidLayout,
                     "a workflow constructor names no finalized layout");
            return std::numeric_limits<std::uint32_t>::max();
        }
        const std::uint64_t size =
            (layouts_.layouts[root.value].size + 7u) & ~std::uint64_t{7u};
        const std::uint64_t start =
            (static_cast<std::uint64_t>(construct_cursor_) + 7u) & ~std::uint64_t{7u};
        if (start + size > plan_.region_scratch_extent) {
            add_diag(result_,
                     core_wasm_diag::kResourceExhausted,
                     "the workflow scheduler construct-scratch region is exhausted");
            return std::numeric_limits<std::uint32_t>::max();
        }
        const auto off = static_cast<std::uint32_t>(start);
        construct_cursor_ = static_cast<std::uint32_t>(start + size);
        construct_off_[result.value] = off;
        return off;
    }

    [[nodiscard]] std::optional<std::uint32_t>
    construct_offset(CoreValueId value) const {
        const auto it = construct_off_.find(value.value);
        if (it == construct_off_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    [[nodiscard]] bool emit_construct(const WorkflowFrameLet &let,
                                      CoreLayoutId root,
                                      std::uint32_t dst_addr) {
        const CoreConstructExpr &construct = *let.construct;
        const std::uint32_t size = static_cast<std::uint32_t>(
            (layouts_.layouts[root.value].size + 3u) & ~std::uint64_t{3u});
        if (!emit_zero_fill(dst_addr, size)) {
            return false;
        }
        if (!construct.is_enum_variant) {
            const auto *structure =
                std::get_if<ir::core::CoreLayoutStruct>(&layouts_.layouts[root.value].shape);
            if (structure == nullptr) {
                return fail("a workflow struct constructor has no struct layout");
            }
            for (const CoreConstructArg &arg : construct.args) {
                if (arg.field.value >= structure->field_offsets.size()) {
                    return fail("a workflow constructor names an out-of-range field");
                }
                const std::uint32_t off =
                    static_cast<std::uint32_t>(structure->field_offsets[arg.field.value]);
                if (!store_operand(arg.value, structure->field_layouts[arg.field.value],
                                   dst_addr + off)) {
                    return false;
                }
            }
            return true;
        }
        const auto *tagged =
            std::get_if<ir::core::CoreLayoutEnum>(&layouts_.layouts[root.value].shape);
        if (tagged == nullptr ||
            construct.variant.value >= tagged->variant_payload_layouts.size()) {
            return fail("a workflow enum constructor has no matching variant layout");
        }
        emit_const(dst_addr);
        emit_const(construct.variant.value);
        body_->byte(kOpI32Store);
        body_->u32(kAlignI32);
        body_->u32(0);
        const CoreLayoutId payload = tagged->variant_payload_layouts[construct.variant.value];
        const auto *payload_struct =
            std::get_if<ir::core::CoreLayoutStruct>(&layouts_.layouts[payload.value].shape);
        if (payload_struct == nullptr) {
            return fail("a workflow enum constructor payload is not a struct");
        }
        for (const CoreConstructArg &arg : construct.args) {
            if (arg.field.value >= payload_struct->field_offsets.size()) {
                return fail("a workflow enum payload constructor names an out-of-range slot");
            }
            const std::uint32_t off =
                static_cast<std::uint32_t>(tagged->payload_offset +
                                           payload_struct->field_offsets[arg.field.value]);
            if (!store_operand(arg.value, payload_struct->field_layouts[arg.field.value],
                               dst_addr + off)) {
                return false;
            }
        }
        return true;
    }

    // Store one constructor operand at one destination slot.
    [[nodiscard]] bool store_operand(CoreValueId value, CoreLayoutId edge,
                                     std::uint32_t dst_slot_addr) {
        if (edge.value >= layouts_.layouts.size()) {
            return fail("a workflow constructor operand names an out-of-range layout edge");
        }
        const ir::core::CoreLayout &field = layouts_.layouts[edge.value];
        // A tag-only enum is physically one inline i32 discriminant word (its
        // CoreLayoutEnum shape carries only zero-sized payloads), so it takes
        // the scalar/PtrLen leaf path below; only a payload-bearing enum or a
        // struct child is an aggregate ADDRESS.
        const bool aggregate_child =
            std::holds_alternative<ir::core::CoreLayoutStruct>(field.shape) ||
            (std::holds_alternative<ir::core::CoreLayoutEnum>(field.shape) &&
             !std::ranges::all_of(
                 std::get<ir::core::CoreLayoutEnum>(field.shape).variant_payload_sizes,
                 [](std::uint64_t size) { return size == 0; }));
        if (aggregate_child) {
            const auto off = construct_offset(value);
            if (!off.has_value()) {
                return fail("a workflow aggregate constructor operand is not a prior aggregate "
                            "let");
            }
            emit_const(dst_slot_addr);
            emit_const(plan_.region_scratch_base + *off);
            body_->byte(kOpI32Store);
            body_->u32(kAlignI32);
            body_->u32(0);
            return true;
        }
        if (std::holds_alternative<ir::core::CoreLayoutContainer>(field.shape)) {
            return fail("a workflow frame constructor cannot build a bounded collection this rung");
        }
        if (std::holds_alternative<ir::core::CoreLayoutBytes>(field.shape) ||
            std::holds_alternative<ir::core::CoreLayoutClosure>(field.shape)) {
            return fail("a workflow frame constructor reaches a Uuid/closure operand");
        }
        // Scalar / tag-enum / String PtrLen operand: it must be a PRIOR PROJECTED
        // path let (a scalar constructor input is outside the workflow frame
        // subset); latch its source slot and copy the leaf word(s).
        const WorkflowFrameLet *let = find_let(value);
        if (let == nullptr || let->is_construct || let->path->projection.empty()) {
            return fail("a workflow scalar constructor operand must be a prior projected field");
        }
        const auto leaf = path_leaf_layout(*let);
        if (!leaf.has_value() || leaf->value != edge.value) {
            return fail("a workflow constructor operand layout disagrees with its projected slot");
        }
        if (!latch_path_slot(*let, 0)) {
            return fail("a workflow scalar operand projection is not materializable");
        }
        copy_inline_leaf(field, /*src_local=*/addr_local(0), /*src_off=*/0, dst_slot_addr);
        return true;
    }

    // Copy one inline scalar/PtrLen/tag-enum leaf: [src_local + src_off] -> the
    // destination slot address (an i32 word, an i64 word, or the two PtrLen
    // words).
    void copy_inline_leaf(const ir::core::CoreLayout &field,
                          std::uint32_t src_local,
                          std::uint32_t src_off,
                          std::uint32_t dst_addr) {
        const bool wide =
            std::holds_alternative<ir::core::CoreLayoutScalar>(field.shape) &&
            std::get_if<ir::core::CoreLayoutScalar>(&field.shape)->repr ==
                ir::core::CoreScalarRepr::I64;
        emit_copy_word(wide, src_local, src_off, dst_addr, 0);
        if (std::holds_alternative<ir::core::CoreLayoutPtrLen>(field.shape)) {
            emit_copy_word(false, src_local, src_off + 4u, dst_addr, 4u);
        }
    }

    void emit_copy_word(bool wide,
                        std::uint32_t src_local,
                        std::uint32_t src_off,
                        std::uint32_t dst_addr,
                        std::uint32_t dst_off) {
        emit_const(dst_addr + dst_off);
        append_indexed_op(*body_, kOpLocalGet, src_local);
        if (src_off != 0) {
            emit_const(src_off);
            body_->byte(kOpI32Add);
        }
        body_->byte(wide ? kOpI64Load : kOpI32Load);
        body_->u32(wide ? kAlignI64 : kAlignI32);
        body_->u32(0);
        body_->byte(wide ? kOpI64Store : kOpI32Store);
        body_->u32(wide ? kAlignI64 : kAlignI32);
        body_->u32(0);
    }

    // Expand the yield value's frame into [dst_base + dst_off].
    [[nodiscard]] bool emit_yield(CoreValueId value, CoreLayoutId dst_layout,
                                  std::uint32_t dst_base, std::uint64_t dst_off) {
        const WorkflowFrameLet *let = find_let(value);
        if (let == nullptr) {
            return fail("the workflow frame yield value is not bound by a region let");
        }
        if (let->is_construct) {
            const auto off = construct_offset(value);
            if (!off.has_value()) {
                return fail("the workflow yield value has no constructed frame");
            }
            emit_const(plan_.region_scratch_base + *off);
            append_indexed_op(*body_, kOpLocalSet, addr_local(0));
            return copy_aggregate(dst_layout, 0, 0, dst_base, dst_off);
        }
        const auto leaf = path_leaf_layout(*let);
        if (!leaf.has_value()) {
            return fail("a workflow yield path has no finalized leaf layout");
        }
        const ir::core::CoreLayout &leaf_layout = layouts_.layouts[leaf->value];
        const bool aggregate_leaf =
            std::holds_alternative<ir::core::CoreLayoutStruct>(leaf_layout.shape) ||
            std::holds_alternative<ir::core::CoreLayoutEnum>(leaf_layout.shape);
        if (let->path->projection.empty()) {
            // Bare root: the value IS the source frame.
            if (!latch_path_slot(*let, 0)) {
                return fail("a workflow bare yield root is not materializable");
            }
            return copy_aggregate(dst_layout, 0, 0, dst_base, dst_off);
        }
        if (!latch_path_slot(*let, 0)) {
            return fail("a workflow yield projection is not materializable");
        }
        if (aggregate_leaf) {
            return copy_aggregate(*leaf, 0, 0, dst_base, dst_off);
        }
        // Scalar / PtrLen projection landing at the frame root (a degenerate
        // scalar output nominal).
        if (std::holds_alternative<ir::core::CoreLayoutContainer>(leaf_layout.shape)) {
            return fail("a workflow frame yield cannot project a bounded collection this rung");
        }
        copy_inline_leaf(leaf_layout, addr_local(0), 0,
                         dst_base + static_cast<std::uint32_t>(dst_off));
        return true;
    }

    // Expand the aggregate named by [addr_local(level) + src_off] into the
    // inline frame at [dst_base + dst_off].
    [[nodiscard]] bool copy_aggregate(CoreLayoutId id,
                                      std::uint32_t level,
                                      std::uint32_t src_off,
                                      std::uint32_t dst_base,
                                      std::uint64_t dst_off) {
        if (id.value >= layouts_.layouts.size()) {
            return fail("a workflow frame copy walks an out-of-range layout");
        }
        if (level + 1u > 64u) {
            return fail("a workflow frame copy exceeds its address stack");
        }
        const ir::core::CoreLayout &layout = layouts_.layouts[id.value];
        if (const auto *structure = std::get_if<ir::core::CoreLayoutStruct>(&layout.shape)) {
            return copy_struct(*structure, level, src_off, dst_base, dst_off);
        }
        if (const auto *tagged = std::get_if<ir::core::CoreLayoutEnum>(&layout.shape)) {
            return copy_enum(*tagged, level, src_off, dst_base, dst_off);
        }
        if (std::holds_alternative<ir::core::CoreLayoutContainer>(layout.shape)) {
            // The 8-byte inline (ptr,len) header; the backing placement is
            // shared and never copied.
            emit_copy_word(false, addr_local(level), src_off, dst_base,
                           static_cast<std::uint32_t>(dst_off));
            emit_copy_word(false, addr_local(level), src_off + 4u, dst_base,
                           static_cast<std::uint32_t>(dst_off) + 4u);
            return true;
        }
        return fail("a workflow frame copy reaches a non-aggregate frame shape");
    }

    [[nodiscard]] bool copy_struct(const ir::core::CoreLayoutStruct &structure,
                                   std::uint32_t level,
                                   std::uint32_t src_off,
                                   std::uint32_t dst_base,
                                   std::uint64_t dst_off) {
        for (std::uint32_t i = 0; i < structure.field_layouts.size(); ++i) {
            const CoreLayoutId edge = structure.field_layouts[i];
            if (edge.value >= layouts_.layouts.size()) {
                return fail("a workflow struct copy walks an out-of-range field");
            }
            const std::uint32_t off = static_cast<std::uint32_t>(structure.field_offsets[i]);
            const ir::core::CoreLayout &field = layouts_.layouts[edge.value];
            if (leaf_is_word(field) ||
                std::holds_alternative<ir::core::CoreLayoutPtrLen>(field.shape)) {
                copy_inline_leaf(field, addr_local(level), src_off + off,
                                 dst_base + static_cast<std::uint32_t>(dst_off + off));
                continue;
            }
            if (std::holds_alternative<ir::core::CoreLayoutBytes>(field.shape) ||
                std::holds_alternative<ir::core::CoreLayoutClosure>(field.shape)) {
                return fail("a workflow frame copy reaches a Uuid/closure field");
            }
            if (std::holds_alternative<ir::core::CoreLayoutStruct>(field.shape) ||
                std::holds_alternative<ir::core::CoreLayoutEnum>(field.shape) ||
                std::holds_alternative<ir::core::CoreLayoutContainer>(field.shape)) {
                // Latch the child address from the parent's field slot, then
                // expand that child inline.
                append_indexed_op(*body_, kOpLocalGet, addr_local(level));
                if (src_off + off != 0) {
                    emit_const(src_off + off);
                    body_->byte(kOpI32Add);
                }
                body_->byte(kOpI32Load);
                body_->u32(kAlignI32);
                body_->u32(0);
                append_indexed_op(*body_, kOpLocalSet, addr_local(level + 1u));
                if (!copy_aggregate(edge, level + 1u, 0, dst_base, dst_off + off)) {
                    return false;
                }
                continue;
            }
            return fail("a workflow frame copy reaches a non-v1 frame field");
        }
        return true;
    }

    [[nodiscard]] bool copy_enum(const ir::core::CoreLayoutEnum &tagged,
                                 std::uint32_t level,
                                 std::uint32_t src_off,
                                 std::uint32_t dst_base,
                                 std::uint64_t dst_off) {
        // Discriminant at offset 0.
        emit_copy_word(false, addr_local(level), src_off, dst_base,
                       static_cast<std::uint32_t>(dst_off));
        // Copy ONLY the active variant's payload, selected at run time by the
        // source discriminant (the same fail-closed ladder the V2-A final
        // materializer uses).
        for (std::uint32_t ordinal = 0;
             ordinal < tagged.variant_payload_layouts.size(); ++ordinal) {
            const CoreLayoutId payload = tagged.variant_payload_layouts[ordinal];
            if (payload.value >= layouts_.layouts.size()) {
                return fail("a workflow enum copy names an out-of-range payload");
            }
            const auto *payload_struct =
                std::get_if<ir::core::CoreLayoutStruct>(&layouts_.layouts[payload.value].shape);
            if (payload_struct == nullptr) {
                return fail("a workflow enum payload is not a struct layout");
            }
            if (payload_struct->field_layouts.empty()) {
                continue;
            }
            append_indexed_op(*body_, kOpLocalGet, addr_local(level));
            if (src_off != 0) {
                emit_const(src_off);
                body_->byte(kOpI32Add);
            }
            body_->byte(kOpI32Load);
            body_->u32(kAlignI32);
            body_->u32(0);
            emit_const(ordinal);
            body_->byte(kOpI32Eq);
            body_->byte(kOpIf);
            body_->byte(kEmptyBlock);
            const bool ok = copy_struct(*payload_struct, level,
                                        src_off +
                                            static_cast<std::uint32_t>(tagged.payload_offset),
                                        dst_base, dst_off + tagged.payload_offset);
            body_->byte(kOpEnd);
            if (!ok) {
                return false;
            }
        }
        // A corrupt discriminant names no payload: trap.
        append_indexed_op(*body_, kOpLocalGet, addr_local(level));
        if (src_off != 0) {
            emit_const(src_off);
            body_->byte(kOpI32Add);
        }
        body_->byte(kOpI32Load);
        body_->u32(kAlignI32);
        body_->u32(0);
        emit_const(static_cast<std::uint32_t>(tagged.variant_payload_layouts.size()));
        body_->byte(kOpI32GeU);
        body_->byte(kOpIf);
        body_->byte(kEmptyBlock);
        body_->byte(kOpUnreachable);
        body_->byte(kOpEnd);
        return true;
    }
};

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
// `identity_tag` forces tag-0 for P6-packaged nodes: a bridge P6 node may carry
// capabilities, but its scheduler-level completion is an identity event (the
// capability call happens inside the runner, not at the scheduler boundary).
void append_event_record_write(ByteBuffer &body,
                               const WorkflowNodePlan &node,
                               std::uint32_t status_local,
                               bool identity_tag) {
    const std::uint32_t record_addr =
        kNodeEventRecordsBase + node.schedule_pos * kNodeEventRecordBytes;
    const bool has_cap = !identity_tag && !node.capabilities.empty();
    const std::uint8_t tag = has_cap ? kEventTagCapability : kEventTagIdentity;
    // [0..3]: tag u8 in byte 0, pad[1..3] == 0 (one aligned 4-byte store).
    append_i32_store_const(body, record_addr + 0u, static_cast<std::uint32_t>(tag));
    // [4..7]: workflow_node_id.
    append_i32_store_const(body, record_addr + 4u, node.node.value);
    // [8..11]: schedule_pos.
    append_i32_store_const(body, record_addr + 8u, node.schedule_pos);
    // [12..15]: capability (0 for identity). For a multi-capability node the
    // event record carries the first capability (the legacy node-event identity);
    // the exec-manifest accounts for all of them.
    append_i32_store_const(
        body, record_addr + 12u, has_cap ? node.capabilities.front().first.value : 0u);
    // [16..23]: source_symbol u64 (0 for identity). A capability source SymbolId
    // is a non-negative value bounded by the existing E2 host-ABI contract
    // (build_agent_plan rejects SymbolId > UINT32_MAX), so it is always < 2^63
    // and the canonical ByteBuffer::s64 signed-LEB128 emitter needs no special
    // sign extension here.
    if (has_cap) {
        append_const(body, record_addr + 16u);
        body.byte(kOpI64Const);
        body.s64(static_cast<std::int64_t>(node.capabilities.front().second));
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

// V2-D: zero-fill one node-frame sub-span one i32 word at a time using the
// caller's cursor local (the scheduler zeroes C_k before the packaged runner
// executes, so a context field without a literal default is deterministically
// zero and padding reads back as 0). The span size is 4-aligned by the D6
// planner.
void append_word_zero_fill(ByteBuffer &body,
                           std::uint32_t base,
                           std::uint32_t byte_size,
                           std::uint32_t cursor_local) {
    append_const(body, base);
    append_indexed_op(body, kOpLocalSet, cursor_local);
    body.byte(kOpBlock);
    body.byte(kEmptyBlock);
    body.byte(kOpLoop);
    body.byte(kEmptyBlock);
    append_indexed_op(body, kOpLocalGet, cursor_local);
    append_const(body, base + byte_size);
    body.byte(kOpI32GeU);
    body.byte(kOpBrIf);
    body.u32(1u);
    append_indexed_op(body, kOpLocalGet, cursor_local);
    append_const(body, 0);
    body.byte(kOpI32Store);
    body.u32(kAlignI32);
    body.u32(0u);
    append_indexed_op(body, kOpLocalGet, cursor_local);
    append_const(body, 4u);
    body.byte(kOpI32Add);
    append_indexed_op(body, kOpLocalSet, cursor_local);
    body.byte(kOpBr);
    body.u32(0u);
    body.byte(kOpEnd);
    body.byte(kOpEnd);
}

[[nodiscard]] bool append_workflow_schedule(ByteBuffer &body,
                                            const CoreProgram &program,
                                            const ir::core::CoreLayoutTable &layouts,
                                            const WorkflowPlan &plan,
                                            const WorkflowFunctionTable &functions,
                                            std::uint32_t status_local,
                                            std::uint32_t cursor_local,
                                            std::uint32_t normalize_cursor_local,
                                            std::uint32_t addr_local_base,
                                            CoreWasmCodegenResult &result) {
    const bool capability_workflow = !plan.imports.empty();
    append_const(body, 0);
    append_indexed_op(body, kOpGlobalSet, kWorkflowGlobalTransitionCount);
    append_const(body, 0);
    append_indexed_op(body, kOpGlobalSet, kWorkflowGlobalCompletedCount);
    // V2-D emission half 2: reset the state-entry trace count header (the ring
    // contents are read only through the count, so no body wipe is needed).
    if (plan.has_p6_nodes && plan.state_trace_capacity != 0) {
        append_i32_store_const(body, plan.state_trace_base, 0u);
    }

    if (capability_workflow) {
        // Reset the node-event header: event_count = 0 and pad[4..7] = 0. The
        // latch first-instruction gate (in run2) has already run before this.
        append_i32_store_const(body, kNodeEventLogBase + 0u, 0u);
        append_i32_store_const(body, kNodeEventLogBase + 4u, 0u);
    }

    WorkflowFrameMaterializer materializer(program, layouts, plan, result);

    for (const auto node_id : plan.schedule) {
        if (node_id.value >= plan.nodes.size()) {
            return false;
        }
        const auto &node = plan.nodes[node_id.value];
        const auto runner = workflow_runner_index(plan, node.target_instance);
        const auto ptr_local = workflow_node_ptr_local(node_id);
        const auto len_local = workflow_node_len_local(node_id);
        if (!runner.has_value() || !ptr_local.has_value() || !len_local.has_value()) {
            return false;
        }
        const bool p6_node =
            plan.p6_block_by_runner[*runner] != kInvalidP6Block;
        if (p6_node) {
            const WorkflowNodeBlock &block =
                plan.node_blocks[plan.p6_block_by_runner[*runner]];
            // V2-D: every packaged runner starts from a zeroed context frame
            // (scalar defaults are zero; a String default PtrLen is not in this
            // rung, so no Data segment is materialized into C_k).
            if (block.context_size != 0) {
                append_word_zero_fill(body, block.context_base,
                                      block.context_size, cursor_local);
            }
            // V2-D: materialize the node's projected/constructed input frame
            // into its fixed I_k block, then invoke the packaged P6 runner
            // with (I_k, input_size). It walks the plain-goto chain and invokes
            // the relocated computed-final handler, returning
            // (OK, O_k, output_size); anything else traps. A BARE
            // forward needs no materialization: the host packed the entry
            // directly into I_k, so emitting the region would zero-fill the
            // host-packed bytes.
            const WorkflowRegionPlan &region = plan.node_regions[node_id.value];
            // V2-D: the host packs the workflow entry frame directly into the
            // FIRST scheduled node's I_k. A bare-forward region on that entry
            // node therefore needs no materialization (emitting it would zero
            // and self-copy the host-packed bytes). Every OTHER p6 node must be
            // materialized, even for a bare forward: its region reads the entry
            // pointer or an upstream node's O_k block and copies the frame into
            // this node's own fixed I_k.
            const bool host_packed_entry =
                node_id == plan.schedule.front() && !region.constructed &&
                region.source.kind == WorkflowFrameSourceKind::Input;
            // V2-D RETURN: a BARE entry node receives the host-packed INLINE
            // frame directly in I_k. When its packaged runner projects an
            // aggregate field off the input, rewrite I_k into module
            // pointer-tree form in-place before the runner executes; a bare
            // forward on a runner that reads only top-level words needs no
            // normalization, and a constructed region already emits its own
            // materialized frame.
            const bool entry_needs_normalize =
                node_id == plan.schedule.front() &&
                !region.constructed && plan.entry_normalize_extent != 0u &&
                plan.agent_plans[*runner].reads_raw_input_frame;
            if (entry_needs_normalize) {
                if (!materializer.normalize_inline_input(
                        body,
                        CoreLayoutId{block.input_layout},
                        block.input_base,
                        /*entry_ptr_local=*/0,
                        cursor_local,
                        normalize_cursor_local,
                        addr_local_base)) {
                    return false;
                }
            }
            if (region.constructed || !host_packed_entry) {
                if (!materializer.emit(body,
                                       region,
                                       block.input_base,
                                       CoreLayoutId{block.input_layout},
                                       /*entry_ptr_local=*/0,
                                       cursor_local,
                                       addr_local_base)) {
                    return false;
                }
            }
            append_const(body, block.input_base);
            append_const(body, block.input_size);
            append_indexed_op(body, kOpCall, functions.runner(*runner));
            append_indexed_op(body, kOpLocalSet, *len_local);
            append_indexed_op(body, kOpLocalSet, *ptr_local);
            append_indexed_op(body, kOpLocalSet, status_local);
            // status must be OK.
            append_indexed_op(body, kOpLocalGet, status_local);
            append_const(body, AHFL_CAP_OK);
            body.byte(kOpI32Ne);
            body.byte(kOpIf);
            body.byte(kEmptyBlock);
            body.byte(kOpUnreachable);
            body.byte(kOpEnd);
            // The runner must name exactly its fixed O_k block and the output
            // layout's size (fail-closed against a forged computed runner).
            append_indexed_op(body, kOpLocalGet, *ptr_local);
            append_const(body, block.output_base);
            body.byte(kOpI32Ne);
            body.byte(kOpIf);
            body.byte(kEmptyBlock);
            body.byte(kOpUnreachable);
            body.byte(kOpEnd);
            append_indexed_op(body, kOpLocalGet, *len_local);
            append_const(body, block.output_size);
            body.byte(kOpI32Ne);
            body.byte(kOpIf);
            body.byte(kEmptyBlock);
            body.byte(kOpUnreachable);
            body.byte(kOpEnd);
            // WH-5b.1 fix (b): in a hybrid module (event region exists) a P6
            // node writes its tag-0 identity record and publishes
            // event_count = schedule_pos + 1, so the next cap node's defensive
            // bound sees event_count == its own dense schedule_pos. Pure P6
            // modules (no imports, no event region) keep the old behavior.
            if (capability_workflow) {
                append_event_record_write(body, node, status_local,
                                          /*identity_tag=*/true);
                append_const(body, kNodeEventLogBase + 0u);
                append_const(body, node.schedule_pos + 1u);
                body.byte(kOpI32Store);
                body.u32(2u);
                body.u32(0u);
            }
            append_indexed_op(body, kOpGlobalGet, kWorkflowGlobalCompletedCount);
            append_const(body, 1);
            body.byte(kOpI32Add);
            append_indexed_op(body, kOpGlobalSet, kWorkflowGlobalCompletedCount);
            continue;
        }
        if (!append_workflow_source(body, node.input)) {
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
        append_event_record_write(body, node, status_local,
                                  /*identity_tag=*/false);
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
make_workflow_run2_body(const CoreProgram &program,
                        const ir::core::CoreLayoutTable &layouts,
                        const WorkflowPlan &plan,
                        const WorkflowFunctionTable &functions,
                        CoreWasmCodegenResult &result) {
    if (plan.nodes.size() >
        (static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) - 1u) / 2u) {
        return std::nullopt;
    }
    const auto node_locals = static_cast<std::uint32_t>(plan.nodes.size()) * 2u;
    const auto status_local = 2u + node_locals;
    // V2-D: the frame materializer needs one zero-fill cursor plus an
    // address-stack local per aggregate nesting depth. They sit above the
    // scheduler's status scratch and are absent on an all-opaque workflow, so
    // its run2 local declaration is byte-identical.
    const bool p6 = plan.has_p6_nodes;
    const std::uint32_t address_locals =
        p6 ? WorkflowFrameMaterializer::required_locals(program, layouts, plan) : 0u;
    // The V2-D scheduler locals are contiguous above the node/status slots:
    // the zero-fill cursor, the entry-normalization cursor (reserved even when
    // this workflow performs no normalization, so the address-stack base never
    // aliases a different local between two workflow shapes), then the
    // address-stack locals.
    const std::uint32_t cursor_local = status_local + 1u;
    const std::uint32_t normalize_cursor_local = cursor_local + 1u;
    const std::uint32_t addr_base = cursor_local + 2u;
    const std::uint32_t extra_locals =
        p6 ? 2u + address_locals : 0u;

    ByteBuffer body;
    body.u32(1);
    body.u32(node_locals + 1u + extra_locals);
    body.byte(kI32); // node (ptr,len) pairs, status, then V2-D materializer locals

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

    if (!append_workflow_schedule(body,
                                  program,
                                  layouts,
                                  plan,
                                  functions,
                                  status_local,
                                  cursor_local,
                                  normalize_cursor_local,
                                  addr_base,
                                  result)) {
        return std::nullopt;
    }
    append_const(body, AHFL_CAP_OK);
    if (p6) {
        // V2-D: materialize the workflow return frame into the fixed workflow
        // output slot and return it (with the declared output layout size),
        // never a borrowed node pointer.
        WorkflowFrameMaterializer materializer(program, layouts, plan, result);
        const CoreLayoutId wf_output_layout =
            layouts.value_layouts[plan.wf_output_vt.value];
        if (!materializer.emit(body,
                               plan.return_region,
                               plan.wf_output_base,
                               wf_output_layout,
                               /*entry_ptr_local=*/0,
                               cursor_local,
                               addr_base)) {
            return std::nullopt;
        }
        append_const(body, plan.wf_output_base);
        append_const(body, plan.wf_output_size);
        body.byte(kOpEnd);
        return body;
    }
    if (!append_workflow_source(body, plan.output)) {
        return std::nullopt;
    }
    body.byte(kOpEnd);
    return body;
}

[[nodiscard]] ByteBuffer make_workflow_run_body(const WorkflowPlan &plan,
                                                const WorkflowFunctionTable &functions) {
    ByteBuffer body;
    if (!plan.imports.empty() || plan.has_p6_nodes) {
        // Legacy run is pointer-only; the v1 (ptr,len) ABI cannot carry a status,
        // so a capability-workflow's run traps before any state mutation, input
        // read, or capability effect (a PENDING must never leak through run).
        // A V2-D p6 workflow has no opaque run2 (ptr,len) contract either: its
        // entry is a packed P4-D frame, so legacy run traps.
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
                       const ir::core::CoreLayoutTable &layouts,
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
    const bool p6 = plan.has_p6_nodes;
    const auto import_count =
        capability_workflow ? static_cast<std::uint32_t>(plan.imports.size()) : 0u;

    // RFC 0026 E4-B2-C two-phase memory sizing (seam §4.4/§5.2). PHASE 1 checked
    // wasm32 arithmetic -> BINARY_OVERFLOW; PHASE 2 capacity vs the fixed 64 KiB
    // page -> RESOURCE_EXHAUSTED. Identity workflows keep heap_base = 1024. A
    // V2-D p6 workflow has no node-event region (its nodes carry no opaque
    // capability); its bump heap starts at the log base and is unused by the
    // relocated frame handlers (they build in static scratch).
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

    // V2-D: relocated handler layout. Handler functions follow run2; runner r
    // owns the contiguous range [handler_offset[r], +handler_count[r]).
    const std::uint32_t runner_count =
        static_cast<std::uint32_t>(plan.packaged_instances.size());
    std::vector<std::uint32_t> handler_offset(runner_count, 0);
    std::uint32_t total_handlers = 0;
    for (std::uint32_t r = 0; r < runner_count; ++r) {
        handler_offset[r] = total_handlers;
        if (r < plan.relocated_handlers.size()) {
            total_handlers +=
                static_cast<std::uint32_t>(plan.relocated_handlers[r].size());
        }
    }

    const WorkflowFunctionTable functions{import_count, runner_count, total_handlers, 0u};
    ByteBuffer module;
    module.raw({0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00});

    // V2-D emission half 2: a capability reached by an IN-HANDLER bridge uses
    // the additive (i32)->(i32,i32) control-block functype (type index 6); a
    // capability reached only by an opaque terminal keeps the tuple type 4.
    // One capability has exactly one mode per module.
    const auto workflow_import_is_bridge = [&](CoreCapabilityId id) {
        return std::any_of(plan.workflow_bridge_sites.begin(),
                           plan.workflow_bridge_sites.end(),
                           [&](const BridgeCallPlan &site) {
                               return site.capability == id;
                           });
    };
    const bool has_bridge_imports =
        std::any_of(plan.imports.begin(), plan.imports.end(), workflow_import_is_bridge);

    ByteBuffer types;
    // V2-D: type index 5 (`() -> i32`) is appended only when the module carries
    // relocated handlers; type index 6 (`(i32)->(i32,i32)`, the frame-bridge
    // control-block protocol) only when a packaged handler bridges. An
    // all-opaque workflow keeps its exact five types.
    const std::uint32_t workflow_type_count =
        5u + (p6 ? 1u : 0u) + (p6 && has_bridge_imports ? 1u : 0u);
    types.u32(workflow_type_count);
    append_func_type(types, {}, {kI32});
    append_func_type(types, {kI32}, {kI32});
    append_func_type(types, {kI32, kI32}, {});
    append_func_type(types, {kI32, kI32}, {kI32});
    append_func_type(types, {kI32, kI32}, {kI32, kI32, kI32});
    constexpr std::uint8_t kWorkflowHandlerType = 5;
    constexpr std::uint8_t kWorkflowBridgeType = 6;
    if (p6) {
        append_func_type(types, {}, {kI32}); // relocated `() -> i32` handlers
        if (has_bridge_imports) {
            append_func_type(types, {kI32}, {kI32, kI32});
        }
    }
    if (!append_section(module, kSectionType, types)) {
        return std::nullopt;
    }
    (void)kWorkflowHandlerType;
    (void)kWorkflowBridgeType;

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
            imports.u32(workflow_import_is_bridge(id) ? kWorkflowBridgeType
                                                     : kTypeCapabilityTuple);
        }
        if (!append_section(module, kSectionImport, imports)) {
            return std::nullopt;
        }
    }

    ByteBuffer functions_section;
    // RFC 0026 FB-1: on an opaque workflow defined_count() is
    // runner_count+6; V2-D appends the relocated `() -> i32` handler functions
    // (type index 5) after run2, so an all-opaque workflow keeps its
    // byte-identical function section.
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
    for (std::uint32_t r = 0; r < functions.runner_count; ++r) {
        if (r >= plan.relocated_handlers.size()) {
            continue;
        }
        for (const CompiledHandler &h : plan.relocated_handlers[r]) {
            functions_section.u32(5); // `() -> i32` handler type
            (void)h;
        }
    }
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
    // V2-D: a P6 workflow appends one mutable private current-state global PER
    // PACKAGED RUNNER after the fixed five (a relocated handler latches its own
    // runner global). Additive, so an opaque workflow is byte-identical.
    ByteBuffer globals;
    const std::uint32_t global_count =
        (capability_workflow ? 6u : 5u) + (p6 ? runner_count : 0u);
    globals.u32(global_count);
    append_global(globals, true, 0);
    append_global(globals, false, 1);
    append_global(globals, true, heap_base);
    append_global(globals, false, static_cast<std::uint32_t>(plan.nodes.size()));
    append_global(globals, true, 0);
    if (capability_workflow) {
        append_global(globals, true, 0); // kWorkflowGlobalPendingLatched
    }
    if (p6) {
        for (std::uint32_t r = 0; r < runner_count; ++r) {
            append_global(globals, true, plan.agent_plans[r].initial.value);
        }
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
    for (std::uint32_t r = 0; r < runner_count; ++r) {
        const bool runner_is_p6 =
            r < plan.relocated_handlers.size() && !plan.relocated_handlers[r].empty();
        if (runner_is_p6) {
            auto runner = make_workflow_p6_runner_body(
                plan.agent_plans[r], r,
                plan.node_blocks[plan.p6_block_by_runner[r]],
                /*handler_base=*/functions.handler(handler_offset[r]),
                /*state_global=*/(plan.imports.empty() ? 5u : 6u) + r,
                plan.state_trace_base,
                plan.state_trace_capacity,
                functions);
            if (!runner.has_value() || !code.sized(*runner)) {
                return std::nullopt;
            }
        } else {
            // P2-6 branching-walk honesty: a WireJson (non-P6) agent whose
            // walk terminal is a ComputedGotoAction has a branching / computed
            // action. The descriptor walk is a linear chain of GotoAction
            // edges; a ComputedGotoAction terminal means the agent takes a
            // runtime-dependent branch the walk cannot represent. The D-B
            // state reconstruction would emit a dishonest sequence, so reject
            // the module at codegen time rather than emit a broken walk.
            auto walk = workflow_initial_transitions(plan.agent_plans[r]);
            if (walk.has_value() &&
                walk->terminal.value < plan.agent_plans[r].actions.size()) {
                const auto &terminal_action =
                    plan.agent_plans[r].actions[walk->terminal.value];
                if (std::holds_alternative<ComputedGotoAction>(terminal_action)) {
                    add_diag(result,
                             core_wasm_diag::kUnsupportedOrchestration,
                             "WireJson agent has a computed-goto (branching) terminal; "
                             "the linear walk cannot represent runtime-dependent branches. "
                             "Use a P6-frame agent for computed routing.");
                    return std::nullopt;
                }
            }
            auto runner = make_workflow_runner_body(plan.agent_plans[r], plan.imports);
            if (!runner.has_value() || !code.sized(*runner)) {
                return std::nullopt;
            }
        }
    }
    // V2-D: run and run2 precede the relocated handlers in the code section,
    // matching the function section's declaration order (alloc, dealloc,
    // current_state, step, runners, run, run2, handlers).
    const auto run = make_workflow_run_body(plan, functions);
    auto run2 = make_workflow_run2_body(program, layouts, plan, functions, result);
    if (!run2.has_value() || !code.sized(run) || !code.sized(*run2)) {
        return std::nullopt;
    }
    // V2-D: the relocated frame handlers follow run2 in one flat per-runner
    // range.
    for (std::uint32_t r = 0; r < runner_count; ++r) {
        if (r >= plan.relocated_handlers.size()) {
            continue;
        }
        for (const CompiledHandler &handler : plan.relocated_handlers[r]) {
            if (!code.sized(handler.body)) {
                return std::nullopt;
            }
        }
    }
    if (!append_section(module, kSectionCode, code)) {
        return std::nullopt;
    }

    // V2-D: exactly ONE additive active Data(11) section for a P6 workflow's
    // merged rodata image, in canonical position after Code(10). An opaque
    // workflow emits no Data section and keeps its bytes.
    if (p6 && plan.workflow_rodata_extent != 0) {
        ByteBuffer data;
        data.u32(1);
        data.byte(0);
        data.byte(kOpI32Const);
        data.s32(static_cast<std::int32_t>(ir::core::kP6RodataBase));
        data.byte(kOpEnd);
        const std::string &image = plan.workflow_rodata.image();
        data.u32(static_cast<std::uint32_t>(image.size()));
        data.raw_span(std::span<const std::uint8_t>{
            reinterpret_cast<const std::uint8_t *>(image.data()), image.size()});
        if (!append_section(module, kSectionData, data)) {
            return std::nullopt;
        }
    }

    // V2-D: a P6 workflow carries the core-layout section (node blocks, the
    // entry payload arena, the workflow output slot, rodata span and the dense
    // layout table) as an EOF custom section, mirroring the agent p6-frame
    // lane. The matching boundary wire-schema section is appended exactly
    // once further below: a hybrid p6 + capability workflow's table already
    // carries BOTH the capability roots and the per-node frame roots
    // (project_core_wire_schema over plan.imports and the boundary pairs), so
    // it must be the module's single FINAL AHFLWS section, emitted after the
    // exec-manifest section.
    if (p6) {
        if (!plan.frame_section.has_value() || !plan.frame_wire_table.has_value()) {
            add_diag(result,
                     core_wasm_diag::kInternalInvalid,
                     "a V2-D workflow carries no frame-layout / wire-schema sections");
            return std::nullopt;
        }
        auto layout_payload = ir::core::encode_core_frame_layout_section(*plan.frame_section);
        if (!layout_payload.ok()) {
            for (const auto &diag : layout_payload.diagnostics) {
                add_diag(result, core_wasm_diag::kInvalidLayout, diag.message);
            }
            return std::nullopt;
        }
        ByteBuffer layout_custom;
        if (!layout_custom.name(ir::core::kCoreLayoutSectionName)) {
            return std::nullopt;
        }
        layout_custom.raw_span(*layout_payload.bytes);
        if (!append_section(module, kSectionCustom, layout_custom)) {
            return std::nullopt;
        }
    }

    // RFC 0026 E4-B2-C: a capability workflow ends with the exec-manifest
    // custom section (AHFLXM) EXACTLY ONCE, IMMEDIATELY BEFORE the wire-schema
    // custom section (AHFLWS), which remains the module's FINAL section at
    // EOF. The FINAL AHFLWS section is emitted for every workflow here from a
    // SINGLE table: for a P6 workflow that is the merged capabilities +
    // frame-roots table (encoded into this owning buffer); for an opaque
    // capability workflow it is the capability-only projection the caller
    // passes in.
    std::vector<std::uint8_t> merged_wire_payload;
    if (p6 || capability_workflow) {
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
        }
        std::span<const std::uint8_t> final_wire_payload;
        if (p6) {
            auto wire_payload = ir::core::encode_core_wire_schema_table(*plan.frame_wire_table);
            if (!wire_payload.ok()) {
                add_diag(result,
                         core_wasm_diag::kBinaryOverflow,
                         "a V2-D workflow wire-schema section payload exceeds the encoding "
                         "domain");
                return std::nullopt;
            }
            merged_wire_payload = std::move(*wire_payload.bytes);
            final_wire_payload = merged_wire_payload;
        } else {
            if (wire_schema_payload.empty()) {
                return std::nullopt;
            }
            final_wire_payload = wire_schema_payload;
        }
        ByteBuffer schema_custom;
        if (!schema_custom.name(kWireSchemaSectionName)) {
            return std::nullopt;
        }
        schema_custom.raw_span(final_wire_payload);
        if (!append_section(module, kSectionCustom, schema_custom)) {
            return std::nullopt;
        }
    }
    return std::move(module).take();
}

} // namespace

std::optional<CoreWasmP6NodeBlockCursor>
plan_p6_node_block_cursor(const std::uint64_t start_cursor,
                          const std::span<const CoreWasmP6NodeBlockParts> blocks) {
    constexpr auto kMax = std::numeric_limits<std::uint32_t>::max();
    if (start_cursor > kMax) {
        return std::nullopt;
    }
    CoreWasmP6NodeBlockCursor plan;
    plan.base = static_cast<std::uint32_t>(start_cursor);
    std::uint64_t cursor = start_cursor;
    std::uint64_t extent = 0;
    plan.bases.reserve(blocks.size());
    for (const CoreWasmP6NodeBlockParts &block : blocks) {
        const std::uint64_t parts[4] = {block.input, block.context, block.scratch,
                                        block.output};
        const std::uint64_t block_extent = parts[0] + parts[1] + parts[2] + parts[3];
        if (block_extent > kMax - extent || cursor > kMax - block_extent) {
            return std::nullopt;
        }
        std::array<std::uint32_t, 4> bases{};
        std::uint64_t within = 0;
        for (std::size_t part = 0; part < 4; ++part) {
            bases[part] = static_cast<std::uint32_t>(cursor + within);
            within += parts[part];
        }
        plan.bases.push_back(bases);
        extent += block_extent;
        cursor += block_extent;
    }
    plan.extent = static_cast<std::uint32_t>(extent);
    return plan;
}

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

// The ordered state names one agent runner enters on a single invocation:
// the initial state followed by the deterministic goto chain, ending at the
// terminal. V2-D emission half 2: the walk follows the REAL reachable chain
// (a GotoAction target is the only successor the packaged runner takes), so
// UNREACHABLE states (e.g. the untaken if-else arm of a non-final routing
// handler) are never observed. A computed-goto / computed-return terminal
// ends the chain exactly where its plain-goto equivalent would. Derived from
// the same AgentPlan the module runner executes, never re-walked from the
// graph edges (which name unreachable declarations).
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
            break; // terminal: identity / capability / computed goto-return
        }
        state = go->target;
    }
    return walk;
}

[[nodiscard]] CoreWasmExecutionDescriptor build_agent_descriptor(const CoreProgram &program,
                                                                 const AgentPlan &plan,
                                                                 const FrameSectionPlan *frame_plan,
                                                                 const ir::core::CoreWireSchemaTable
                                                                     *frame_wire_schema) {
    CoreWasmExecutionDescriptor descriptor;
    descriptor.is_workflow = false;
    // RFC 0026 P6-7 D7: a frame-section-eligible module is the real `p6_frame`
    // contract (runv + both boundary sections); every other agent is opaque
    // wire-JSON on the run2 lane. A raw-frame agent whose boundary is not yet
    // wire-representable emits the rung-A sectionless fallback and is not given
    // the p6-frame descriptor.
    descriptor.frame_contract = frame_plan != nullptr ? CoreWasmFrameContract::P6Frame
                                                      : CoreWasmFrameContract::WireJson;
    if (frame_plan != nullptr) {
        CoreWasmFrameLane lane;
        // RFC 0026 P6-7 frame-bridge v2 rung V2-A: the final-kind discriminator
        // is the sole authority for runv's authorized value_ptr base. An
        // identity final returns the borrowed input base (1024); a computed
        // final materializes into the fixed output base (12288).
        const bool computed_final = plan.has_computed_final;
        lane.final_kind = computed_final ? "computed" : "identity";
        lane.input_base = ir::core::kP6AggregateInputBase;
        lane.input_size = frame_plan->input_size;
        lane.output_base = computed_final ? ir::core::kP6AggregateOutputBase
                                          : ir::core::kP6AggregateInputBase;
        lane.output_size = frame_plan->output_size;
        lane.placements.reserve(frame_plan->placements.size());
        for (const auto &placement : frame_plan->placements) {
            lane.placements.push_back(CoreWasmFramePlacement{
                placement.edge_index, placement.base, placement.extent});
        }
        lane.payload_arena_base = frame_plan->payload_arena_base;
        lane.payload_arena_capacity = frame_plan->payload_arena_capacity;
        lane.rodata_base = frame_plan->rodata_base;
        lane.rodata_extent = frame_plan->rodata_extent;
        // V2-C: the capability bridge control page frame and per-call-site
        // facts the Node host callback walks.
        lane.bridge_control_base = frame_plan->bridge_control_base;
        lane.bridge_block_stride = frame_plan->bridge_block_stride;
        lane.bridge_control_extent = frame_plan->bridge_control_extent;
        lane.bridge_spill_base = frame_plan->bridge_spill_base;
        lane.bridge_spill_extent = frame_plan->bridge_spill_extent;
        lane.bridge_call_sites.reserve(frame_plan->bridge_call_sites.size());
        for (const auto &site : frame_plan->bridge_call_sites) {
            CoreWasmBridgeCallSite bridge;
            bridge.call_site_id = site.call_site_id;
            const auto import_it = std::find_if(
                plan.imports.begin(), plan.imports.end(),
                [&](CoreCapabilityId id) {
                    return program.capabilities[id.value].symbol_ref.id.has_value() &&
                           *program.capabilities[id.value].symbol_ref.id ==
                               site.source_symbol;
                });
            bridge.import_ordinal =
                import_it == plan.imports.end()
                    ? std::numeric_limits<std::uint32_t>::max()
                    : static_cast<std::uint32_t>(
                          std::distance(plan.imports.begin(), import_it));
            bridge.source_symbol = site.source_symbol;
            bridge.arity = site.arity;
            bridge.block_offset = site.block_offset;
            bridge.param_wire.reserve(site.param_layouts.size());
            bridge.param_layout.reserve(site.param_layouts.size());
            if (frame_wire_schema != nullptr) {
                const auto cap_it = std::find_if(
                    frame_wire_schema->capabilities.begin(),
                    frame_wire_schema->capabilities.end(),
                    [&](const ir::core::CoreWireCapabilitySchema &schema) {
                        return schema.source_symbol == site.source_symbol;
                    });
                if (cap_it != frame_wire_schema->capabilities.end()) {
                    for (const auto param : cap_it->params) {
                        bridge.param_wire.push_back(param.value);
                    }
                    bridge.result_wire = cap_it->result.value;
                }
            }
            for (const auto param_layout : site.param_layouts) {
                bridge.param_layout.push_back(param_layout.value);
            }
            bridge.result_layout = site.result_layout.value;
            bridge.result_base = site.result_base;
            bridge.result_extent = site.result_extent;
            bridge.result_payload_base = site.result_payload_base;
            bridge.result_payload_capacity = site.result_payload_capacity;
            bridge.spill_base = site.spill_base;
            bridge.spill_extent = site.spill_extent;
            lane.bridge_call_sites.push_back(std::move(bridge));
        }
        descriptor.frame = std::move(lane);
        // Carry the exact boundary tables the sections encode so a generic host
        // can pack/encode without a second projection. Both are the same values
        // used to emit the module's verified custom sections.
        ir::core::CoreFrameLayoutSection section =
            frame_section_to_layout_section(*frame_plan);
        descriptor.frame_section = std::move(section);
        if (frame_wire_schema != nullptr) {
            descriptor.wire_schema = *frame_wire_schema;
        }
    }
    descriptor.agent_name = program.agents[plan.agent.value].symbol_ref.canonical_name;
    descriptor.states = program.agents[plan.agent.value].states;
    descriptor.initial_state = plan.initial.value;
    descriptor.imports = build_import_descriptors(program, plan.imports);
    // V2-C: tag each import with its functype protocol.
    for (const BridgeCallPlan &site : plan.bridge_calls) {
        const auto symbol = *program.capabilities[site.capability.value].symbol_ref.id;
        for (auto &import : descriptor.imports) {
            if (import.field == "cap_" + std::to_string(symbol)) {
                import.mode = "bridge";
            }
        }
    }
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
    // WH-4 fix-forward P1-2: carry the canonical workflow name so the wasm
    // lane populates ExecutionMetadataStore with the SAME display name the
    // evaluator uses (metadata parity for ahfl.run-report byte-compare).
    {
        const auto &workflow_decl = program.workflows[plan.workflow.value];
        descriptor.workflow_name = workflow_decl.symbol_ref.canonical_name.empty()
                                       ? workflow_decl.name
                                       : workflow_decl.symbol_ref.canonical_name;
        // WH-4b: carry the dense workflow index so the wasm lane originates a
        // REAL recovery-snapshot WorkflowId (not {0}) and fail-closes on a
        // resume-time mismatch (design 12.6.5 step 1).
        descriptor.workflow_index = plan.workflow.value;
    }
    // V2-D: a workflow with a packaged computed node is a P6-frame module: the
    // host packs the entry into the entry node's I block, run2 drives the
    // in-module scheduler/runner lane, and the workflow output slot is encoded
    // against the boundary wire schema. An all-opaque workflow stays wire-JSON.
    const bool p6 = plan.has_p6_nodes;
    descriptor.frame_contract =
        p6 ? CoreWasmFrameContract::P6Frame : CoreWasmFrameContract::WireJson;
    descriptor.imports = build_import_descriptors(program, plan.imports);
    descriptor.event_log_base = ir::core::kNodeEventLogBase;
    descriptor.event_header_bytes = ir::core::kNodeEventHeaderBytes;
    descriptor.event_record_bytes = ir::core::kNodeEventRecordBytes;
    descriptor.event_records_base = ir::core::kNodeEventRecordsBase;
    descriptor.workflow_node_count = static_cast<std::uint32_t>(plan.nodes.size());

    if (p6 && plan.frame_section.has_value() && plan.frame_wire_table.has_value()) {
        CoreWasmFrameLane lane;
        lane.final_kind = "computed";
        lane.rodata_base = ir::core::kP6RodataBase;
        lane.rodata_extent = plan.workflow_rodata_extent;
        lane.payload_arena_base = plan.entry_payload_base;
        lane.payload_arena_capacity = plan.entry_payload_capacity;
        // The workflow entry is packed into the first scheduled node's I block
        // (the packer keys the node-block coordinate off the schedule) and run2
        // returns the fixed workflow output slot.
        lane.input_base = plan.node_blocks.front().input_base;
        lane.input_size = plan.node_blocks.front().input_size;
        lane.output_base = plan.wf_output_base;
        lane.output_size = plan.wf_output_size;
        // V2-D emission half 2: the merged in-handler capability bridge page
        // frame and the dense per-call-site facts the Node host callback walks
        // (resolved from the frame section + the workflow wire-schema table).
        lane.bridge_control_base = plan.frame_section->bridge_control_base;
        lane.bridge_block_stride = plan.frame_section->bridge_block_stride;
        lane.bridge_control_extent = plan.frame_section->bridge_control_extent;
        lane.bridge_spill_base = plan.frame_section->bridge_spill_base;
        lane.bridge_spill_extent = plan.frame_section->bridge_spill_extent;
        std::unordered_map<std::uint64_t, const ir::core::CoreWireCapabilitySchema *>
            wire_by_symbol;
        for (const ir::core::CoreWireCapabilitySchema &schema :
             plan.frame_wire_table->capabilities) {
            wire_by_symbol.emplace(schema.source_symbol, &schema);
        }
        lane.bridge_call_sites.reserve(plan.frame_section->bridge_call_sites.size());
        for (const auto &site : plan.frame_section->bridge_call_sites) {
            const auto wire_it = wire_by_symbol.find(site.source_symbol);
            if (wire_it == wire_by_symbol.end()) {
                continue; // the section admission already proved every symbol resolves
            }
            CoreWasmBridgeCallSite bridge;
            bridge.call_site_id = site.call_site_id;
            const auto import_it = std::find_if(
                plan.imports.begin(), plan.imports.end(),
                [&](CoreCapabilityId id) {
                    return program.capabilities[id.value].symbol_ref.id.has_value() &&
                           *program.capabilities[id.value].symbol_ref.id ==
                               site.source_symbol;
                });
            bridge.import_ordinal =
                import_it == plan.imports.end()
                    ? std::numeric_limits<std::uint32_t>::max()
                    : static_cast<std::uint32_t>(
                          std::distance(plan.imports.begin(), import_it));
            bridge.source_symbol = site.source_symbol;
            bridge.arity = site.arity;
            bridge.block_offset = site.block_offset;
            for (const auto param : wire_it->second->params) {
                bridge.param_wire.push_back(param.value);
            }
            bridge.result_wire = wire_it->second->result.value;
            for (const auto param_layout : site.param_layouts) {
                bridge.param_layout.push_back(param_layout.value);
            }
            bridge.result_layout = site.result_layout.value;
            bridge.result_base = site.result_base;
            bridge.result_extent = site.result_extent;
            bridge.result_payload_base = site.result_payload_base;
            bridge.result_payload_capacity = site.result_payload_capacity;
            bridge.spill_base = site.spill_base;
            bridge.spill_extent = site.spill_extent;
            lane.bridge_call_sites.push_back(std::move(bridge));
        }
        descriptor.frame = std::move(lane);
        descriptor.frame_section = *plan.frame_section;
        descriptor.wire_schema = *plan.frame_wire_table;
    }

    // V2-D emission half 2: tag every import reached by an in-handler bridge
    // with the bridge functype protocol (the rest stay the opaque tuple
    // forward). One capability has exactly one mode per module.
    for (const ir::core::CoreFrameBridgeCallSite &site :
         plan.frame_section ? plan.frame_section->bridge_call_sites
                            : std::span<const ir::core::CoreFrameBridgeCallSite>{}) {
        for (auto &import : descriptor.imports) {
            if (import.field == "cap_" + std::to_string(site.source_symbol)) {
                import.mode = "bridge";
            }
        }
    }

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
        walk.all_states = program.agents[agent_plan.agent.value].states;
        descriptor.agents.push_back(std::move(walk));
    }

    // Node schedule in Kahn execution order.
    descriptor.nodes.reserve(plan.schedule.size());
    const auto &workflow_decl = program.workflows[plan.workflow.value];
    for (std::uint32_t position = 0; position < plan.schedule.size(); ++position) {
        const auto node_id = plan.schedule[position];
        const auto &node = plan.nodes[node_id.value];
        CoreWasmNodeDescriptor node_descriptor;
        node_descriptor.node_id = node.node.value;
        node_descriptor.schedule_pos = node.schedule_pos;
        node_descriptor.runner = workflow_runner_index(plan, node.target_instance).value_or(0);
        node_descriptor.name = workflow_decl.nodes[node.node.value].node_name;
        // WH-5b.1: per-node P6 flag + P6-only node_blocks ordinal.
        const auto p6_ordinal =
            plan.p6_block_by_runner[node_descriptor.runner];
        node_descriptor.is_p6 = p6_ordinal != kInvalidP6Block;
        if (node_descriptor.is_p6) {
            node_descriptor.p6_block_ordinal = p6_ordinal;
        }
        // WH-4 fix-forward P1-3: carry the DAG predecessor node ids (the
        // workflow `after` edges) so an embedded host emits
        // NodeScheduled.dependencies without re-deriving the DAG. The after
        // list is dense source-order node ids, the same id space as node_id.
        node_descriptor.dependencies.reserve(
            workflow_decl.nodes[node.node.value].after.size());
        for (const auto dependency : workflow_decl.nodes[node.node.value].after) {
            node_descriptor.dependencies.push_back(dependency.value);
        }
        if (!node.capabilities.empty()) {
            node_descriptor.has_capability = true;
            node_descriptor.capability_ordinal =
                workflow_import_function_index(plan.imports, node.capabilities.front().first)
                    .value_or(0);
            node_descriptor.source_symbol = node.capabilities.front().second;
            for (const auto &[cap, sym] : node.capabilities) {
                auto ordinal = workflow_import_function_index(plan.imports, cap);
                if (ordinal.has_value()) {
                    node_descriptor.all_capabilities.emplace_back(*ordinal, sym);
                }
            }
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
        // RFC 0026 E4-B2-C: an OPAQUE capability workflow projects the
        // deterministic wire schema for exactly its reachable capability
        // imports (same authority the E2 agent path uses). A P6 workflow's
        // frame-wire projection (built inside build_workflow_plan) already
        // covers those same capability roots MERGED with the per-node frame
        // roots, so re-projecting a capability-only table here would emit a
        // second, contradictory AHFLWS section; the P6 path passes an empty
        // payload and encode_workflow_module serializes the single merged
        // table as the module's final wire-schema section. Identity workflows
        // have no imports and skip it too.
        std::vector<std::uint8_t> wire_schema_payload;
        if (!plan->imports.empty() && !plan->has_p6_nodes) {
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
        auto bytes = encode_workflow_module(program, layouts, *plan, wire_schema_payload, result);
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

    // RFC 0026 P6-7 rung A: a raw-input-frame agent CAN become a P6-frame
    // module carrying (1) a deterministic `ahfl.core-layout.v1` section (P4-D
    // table + boundary roots + disjoint backing placements) and (2) a
    // boundary-root extension of the wire-schema projection.
    //
    // Eligibility is deliberately NARROW (design sections 3.4/4.1/11): the P4-D
    // frame lane and the wire-JSON capability lane never mix, and the
    // closure/outlined-fn ABI is an FB-lane concern. A frame module therefore
    // has NO capability imports and NO outlined fn / closure — exactly the
    // predicate that separates the two real `p6_frame` census cases from the
    // FB-1..FB-4 outlined-fn/closure set. Keying off reads_raw_input_frame
    // alone latched for those FB agents too and mutated the bytes of
    // non-design-pinned modules (including appending a frame-root block to a
    // capability module's wire-schema section, the raw+capability hybrid
    // section 11 forbids).
    //
    // RFC 0026 P6-7 frame-bridge v2 rung V2-A: a COMPUTED FINAL is the second
    // frame-module predicate (design §7.3 emission disjunction): such an agent
    // may never project its raw input (its handlers only construct the output)
    // but still carries the layout/wire sections and runv.
    // RFC 0026 P6-7 frame-bridge v2 (rung V2-C): the frame section is physically
    // planned exactly once, inside build_agent_plan BEFORE handler emission (a
    // bridge handler's bytes embed the control-block coordinates). The emit
    // entry only serializes the already-verified plan; a raw-projecting agent
    // whose boundary is not wire-representable carries no plan and stays on the
    // legacy sectionless raw-frame fallback.
    std::optional<FrameSectionPlan> frame_plan;
    if (plan->frame_section) {
        frame_plan = std::move(*plan->frame_section);
        plan->frame_section.reset();
    }
    const bool is_frame_module = frame_plan.has_value();
    std::optional<std::pair<ir::core::CoreValueTypeId, ir::core::CoreValueTypeId>>
        frame_boundary;
    if (is_frame_module) {
        frame_boundary = std::pair{frame_plan->input_vt, frame_plan->output_vt};
    }

    // RFC 0026 E4-B1: the wire schema is projected once during planning for a
    // frame module (capability roots + agent boundary roots). An opaque
    // capability-final / FB module has no frame plan, so project its
    // capability-only table here (the pre-V2-C path). A no-import non-frame
    // agent carries no section.
    std::vector<std::uint8_t> wire_schema_payload;
    std::optional<ir::core::CoreWireSchemaTable> frame_wire_table;
    if (is_frame_module) {
        if (!plan->frame_wire_table.has_value()) {
            add_diag(result, core_wasm_diag::kInvalidCapabilityAbi,
                     "a frame agent carries no projected wire-schema table");
            return result;
        }
        auto encoded = ir::core::encode_core_wire_schema_table(*plan->frame_wire_table);
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
        frame_wire_table = *plan->frame_wire_table;
    } else if (!plan->imports.empty()) {
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
            add_diag(result,
                     core_wasm_diag::kBinaryOverflow,
                     "wire-schema section payload exceeds the encoding domain");
            return result;
        }
        wire_schema_payload = std::move(*encoded.bytes);
    }

    // RFC 0026 P6-7: canonically encode the pre-built core-layout section
    // (compile-side admission already ran the layout/wire + bridge consistency
    // checks at planning time).
    std::vector<std::uint8_t> frame_layout_payload;
    if (is_frame_module) {
        ir::core::CoreFrameLayoutSection section =
            frame_section_to_layout_section(*frame_plan);
        auto layout_encoded = ir::core::encode_core_frame_layout_section(section);
        if (!layout_encoded.ok()) {
            std::string message = "core-layout section payload is not encodable";
            ir::SourceRangeOpt range;
            if (!layout_encoded.diagnostics.empty()) {
                const auto &first = layout_encoded.diagnostics.front();
                message += " (" + first.code + ")";
                range = first.source_range;
            }
            add_diag(result, core_wasm_diag::kBinaryOverflow, std::move(message), range);
            return result;
        }
        frame_layout_payload = std::move(*layout_encoded.bytes);
    }

    auto bytes = encode_module(program, *plan, wire_schema_payload, frame_layout_payload,
                               is_frame_module);
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
    if (frame_plan.has_value()) {
        artifact.exports.push_back("runv");
    }
    for (const auto id : plan->imports) {
        artifact.imports.push_back("ahfl_cap.cap_" +
                                   std::to_string(*program.capabilities[id.value].symbol_ref.id));
    }
    result.descriptor = build_agent_descriptor(program, *plan,
                                               frame_plan.has_value() ? &*frame_plan : nullptr,
                                               frame_wire_table.has_value()
                                                   ? &*frame_wire_table
                                                   : nullptr);
    result.artifact = std::move(artifact);
    return result;
}

} // namespace ahfl::backends
