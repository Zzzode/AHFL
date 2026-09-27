#pragma once

// RFC 0026 KR6.5 E4-B2-D2a: single source of truth for the Core-Wasm ABI's
// fixed linear-memory capacity.
//
// Every Core-Wasm module the backend emits declares exactly one linear memory
// with a minimum of one WebAssembly page and no declared maximum, so the
// compiler and the runtime share ONE fixed byte capacity. Both the Wasm emitter
// (the Memory section limits, the capability-private checked bump allocator,
// and the node-event buffer layout) and the host-independent resume capacity
// authority (src/runtime/engine/core_wasm_resume_capacity.hpp) consume these
// constants so that no layer can re-declare the 65536 magic number. Changing
// these changes the emitted module's memory limits and is therefore a
// wire-visible ABI break.

#include <cstdint>

namespace ahfl::ir::core {

// A WebAssembly linear-memory page is fixed at 64 KiB by the specification.
inline constexpr std::uint32_t kCoreWasmLinearMemoryPageSizeBytes = 65536;

// Every emitted Core-Wasm module declares exactly this minimum page count and
// no declared maximum, so the runtime capacity is the fixed single page.
inline constexpr std::uint32_t kCoreWasmFixedLinearMemoryMinPages = 1;

// The whole linear memory's fixed single-page byte capacity.
inline constexpr std::uint32_t kCoreWasmFixedLinearMemoryCapacityBytes =
    kCoreWasmLinearMemoryPageSizeBytes * kCoreWasmFixedLinearMemoryMinPages;

static_assert(kCoreWasmFixedLinearMemoryCapacityBytes == 65536,
              "Core-Wasm linear memory is fixed at exactly one 64 KiB page");

// RFC 0026 P6-4 (KR6.6): the internal aggregate-frame convention — the INPUT
// side of the eventual P6-7 frame decision, deliberately NOT a wire format.
//
// An aggregate value (a struct, or an enum with a payload) is represented at
// runtime by an i32 ADDRESS into the module's private linear memory pointing at
// its P4-D shaped bytes (`CoreLayoutStruct::field_offsets` /
// `CoreLayoutEnum::payload_offset`). Three regions are reserved below the host
// bump heap so that the compiler, the emitted module, and the embedded host all
// derive their addresses from ONE place:
//
//   [kP6AggregateInputBase,   + input_size)  the agent input frame
//   [kP6AggregateContextBase, + ctx_size)    the agent context frame (0-init)
//   [kP6AggregateScratchBase,            )   per-plan constructor scratch slots
//
// The input frame is written by the HOST at the P4-D offsets of the input
// struct (real memory, no JSON); the context frame starts zeroed; the scratch
// arena backs `CoreConstructExpr`, whose monotone slot allocator is the same
// stack-slot discipline the P6-3 match scratch locals use. Every address is a
// compile-time constant of the shape kind, so a module carries no per-run state
// for them and `step()` stays argument-free.
inline constexpr std::uint32_t kP6AggregateInputBase = 1024;
inline constexpr std::uint32_t kP6AggregateContextBase = 4096;
inline constexpr std::uint32_t kP6AggregateScratchBase = 7168;
// RFC 0026 P6-7 (D5): the computed OUTPUT frame splits the old scratch window:
// [7168,12288) stays constructor scratch (capacity 9216 -> 5120) and
// [12288,16384) is the fixed output frame a computed final materializes into
// (capacity 4096). A pure identity/capability module never emits runv and never
// names the output base, so these constants change no module byte on those
// lanes; they are gates and the runv root authority only.
inline constexpr std::uint32_t kP6AggregateOutputBase = 12288;

// RFC 0026 P6-5 (KR6.6): the element BACKING STORE region — a fourth reserved
// region for BOUNDED-collection element storage, immediately after the
// constructor scratch arena. It is the counterpart of the aggregate frames for
// the collection lane: a host materialises a collection's elements here (one
// `stride` apart, from the P4-D `CoreLayoutContainer`), and the module's
// `emit_collection` reads/writes them at the header's `ptr`.
inline constexpr std::uint32_t kP6CollectionBackingBase = 16384;

// The element BACKING region is every byte from its base up to the fixed page's
// end, so the container's scaled extent must FIT it. A plan whose container
// needs more fails closed (RESOURCE-class rejection): the emitter computes
// `ptr + index * stride` in wrapping i32 arithmetic, so a capacity whose scaled
// address leaves the region would silently wrap to an in-page address the index
// has no right to. This is the ONE authority for that budget, shared by the plan
// guard and any host materialising the backing store.
inline constexpr std::uint32_t kP6CollectionBackingCapacity =
    kCoreWasmFixedLinearMemoryCapacityBytes - kP6CollectionBackingBase;

// The scratch arena is every byte from its base up to the computed OUTPUT frame
// (RFC 0026 P6-7 D5): 7168..12288, capacity 5120. Before P6-7 the scratch
// window ran to the backing base (9216 bytes); the output frame carves the upper
// 4096 bytes out. The scratch planner already fail-closes on overflow, so this
// is a compile-time RESOURCE gate. A module with zero scratch slots (the
// identity/capability E1-E3 lanes) emits no scratch-referencing byte, so the
// reduction is byte-invisible on those lanes.
inline constexpr std::uint32_t kP6AggregateScratchCapacity =
    kP6AggregateOutputBase - kP6AggregateScratchBase;
// The computed output frame owns [12288,16384): exactly 4096 bytes, up to the
// collection backing base.
inline constexpr std::uint32_t kP6AggregateOutputCapacity =
    kP6CollectionBackingBase - kP6AggregateOutputBase;

// The input frame owns every byte from its base up to the context base, and the
// context frame every byte up to the scratch arena. Each frame is a FIXED region
// of the single page, so the agent's input / context struct must FIT its region:
// a struct whose P4-D `CoreLayout::size` exceeds its capacity would silently emit
// loads/stores into the neighbouring region (or past the page). The emitter fails
// closed (RESOURCE-class) rather than produce an address no host ever wrote.
inline constexpr std::uint32_t kP6AggregateInputCapacity =
    kP6AggregateContextBase - kP6AggregateInputBase;
inline constexpr std::uint32_t kP6AggregateContextCapacity =
    kP6AggregateScratchBase - kP6AggregateContextBase;

static_assert(kP6AggregateInputBase % 8 == 0 && kP6AggregateContextBase % 8 == 0 &&
                  kP6AggregateScratchBase % 8 == 0 && kP6AggregateOutputBase % 8 == 0 &&
                  kP6CollectionBackingBase % 8 == 0,
              "P6 frame / backing bases are 8-byte aligned (the widest P4-D scalar)");
static_assert(kP6AggregateContextBase >= kP6AggregateInputBase &&
                  kP6AggregateScratchBase >= kP6AggregateContextBase &&
                  kP6AggregateOutputBase >= kP6AggregateScratchBase &&
                  kP6CollectionBackingBase >= kP6AggregateOutputBase,
              "P6 reserved regions are ordered and non-overlapping");
static_assert(kP6CollectionBackingBase < kCoreWasmFixedLinearMemoryCapacityBytes,
              "the P6 collection backing region starts inside the fixed single page");
static_assert(kP6CollectionBackingCapacity > 0,
              "the P6 collection backing region reserves at least one byte");
static_assert(kP6AggregateScratchCapacity == 5120 && kP6AggregateOutputCapacity == 4096,
              "the output frame splits the old scratch window into 5120 scratch + 4096 output");
static_assert(kP6AggregateInputCapacity > 0 && kP6AggregateContextCapacity > 0 &&
                  kP6AggregateScratchCapacity > 0 && kP6AggregateOutputCapacity > 0,
              "each P6 aggregate frame region reserves at least one byte");

// RFC 0026 P6-7 frame-bridge v2 D1 (rung V2-B): the READ-ONLY literal pool
// region. A module that constructs at least one String literal emits ONE
// additive active Data section (wasm section id 11) that initializes
// [kP6RodataBase, +extent) with the hash-consed, byte-sorted UTF-8 literal
// bytes at instantiation. A constructed String is the PtrLen immediate pair
// (kP6RodataBase + pool_offset, byte_length); the module never stores into
// this region. [0, kP6RodataBase) stays the zero page / null-deref guard
// band, and the input frame at kP6AggregateInputBase (1024) begins exactly
// where the rodata region ends, so no existing reservation moves.
inline constexpr std::uint32_t kP6RodataBase = 256;
inline constexpr std::uint32_t kP6RodataCapacity =
    kP6AggregateInputBase - kP6RodataBase; // [256,1024)

static_assert(kP6RodataBase % 8 == 0, "the rodata region is 8-byte aligned");
static_assert(kP6RodataCapacity == 768, "the rodata region is exactly 768 bytes");

// RFC 0026 P6-7 frame-bridge v2 D6 (rung V2-B): the compile-time fallback
// pool budget for UNBOUNDED String payloads packed into a frame's payload
// arena. Bounded String slots are metered exactly from their schema upper
// bound and are never double-counted with this pool; the pool is one fixed
// reservation (never a runtime growth request) for every unbounded String
// slot of one packed frame. The D6 page-capacity comparison family accounts
// for it together with the rodata region, backing placements, frames and
// scratch against the single 64 KiB page.
inline constexpr std::uint32_t kP6FrameStringPoolBytes = 2048;

static_assert(kP6FrameStringPoolBytes % 8 == 0,
              "the unbounded-String pool reservation is 8-byte aligned");

// RFC 0026 P6-5 (KR6.6): the internal bounded-collection HANDLE convention — the
// INPUT side of the eventual P6-7 frame decision, deliberately NOT a wire format.
//
// A bounded collection value is represented at runtime by an i32 ADDRESS into
// the module's private linear memory pointing at its INLINE `(ptr, len)` header
// (P4-D `CoreLayoutContainer`, size 8 / align 4). The header's two words are:
//
//   [ptr  : i32 @ 0]  the address of the element BACKING STORE (stride * capacity
//                     bytes, reserved below the bump heap)
//   [len  : i32 @ 4]  the current logical element count (<= capacity)
//
// The element at index `i` lives at `ptr + i * stride + value_offset`, where
// `stride`, `capacity` and `value_offset` (a Map's value slot) all come from the
// P4-D `CoreLayoutContainer` — the ONE layout authority. The header offsets are
// a compile-time ABI constant so the compiler, the emitted module, and the
// embedded host derive them from ONE place, exactly as the aggregate frame bases
// above.
inline constexpr std::uint32_t kP6CollectionHeaderPtrOffset = 0;
inline constexpr std::uint32_t kP6CollectionHeaderLenOffset = 4;
inline constexpr std::uint32_t kP6CollectionHeaderSize = 8;

static_assert(kP6CollectionHeaderLenOffset == kP6CollectionHeaderPtrOffset + 4 &&
                  kP6CollectionHeaderSize == kP6CollectionHeaderLenOffset + 4,
              "the P6 collection header is exactly a (ptr:i32, len:i32) pair");

// ----------------------------------------------------------------------------
// Capability-workflow node-event buffer (RFC 0026 E4-B2-C)
// ----------------------------------------------------------------------------
//
// A capability-workflow module reserves a fixed node-event region at the low
// end of its linear memory, BELOW the bump heap:
//
//   [kNodeEventLogBase, +8)                         8-byte header:
//                                                     [0..3] event_count u32-LE
//                                                     [4..7] reserved, always 0
//   [kNodeEventRecordsBase, + node_count * 40)      one 40-byte record per
//                                                     scheduled workflow node
//
// `heap_base = align_up(kNodeEventLogBase + 8 + node_count * 40, 8)`. These
// constants are the single wire-grammar authority shared by the emitter
// (src/compiler/backends/infra/core_wasm_codegen.cpp), the runtime structural
// decoder (src/runtime/engine/core_wasm_node_events.cpp), and the KR6.7
// manifest-driven Node embedded host (which reads the same region to
// reconstruct the canonical observation). An identity workflow (no capability
// imports) emits no records; its heap starts at kNodeEventLogBase.
inline constexpr std::uint32_t kNodeEventLogBase = 1024;
inline constexpr std::uint32_t kNodeEventHeaderBytes = 8;
inline constexpr std::uint32_t kNodeEventRecordBytes = 40;
inline constexpr std::uint32_t kNodeEventRecordsBase =
    kNodeEventLogBase + kNodeEventHeaderBytes; // 1032

static_assert(kNodeEventRecordsBase == 1032,
              "node-event records start at byte 1032 (1024 + 8-byte header)");
static_assert(kNodeEventRecordBytes == 40, "one node-event record is 40 bytes");

// ----------------------------------------------------------------------------
// RFC 0026 FB-2: outlined-fn native recursion stack budget
// ----------------------------------------------------------------------------
//
// FB-2 replaces the FB-1 "no recursion" rule with a compile-time depth
// lattice (see core_recursion.hpp): every recursive fn group carries a sealed
// static depth derived from bounded-container capacities / literals, never from
// the erased `decreases` measure. Recursion runs as ordinary wasm `call`s (no
// tail-call proposal), so the outlined functions consume real native engine
// stack. This is the engine-safe MAXIMUM total direct-fn call depth the
// backend accepts on top of the fixed ABI handler frame; a program whose sealed
// worst-case depth exceeds it is RESOURCE-fail-closed at codegen. The number is
// deliberately conservative: the lowest mainstream wasm engine call-depth
// floors are well above it, and every bounded-container capacity inside the
// fixed 64 KiB page plus the handler frame stays under it. FB-3 extends the
// same gate with the closure-env heap accounting (design §6.3); this constant
// is the native-stack leg of the ONE page/resource budget.
inline constexpr std::uint32_t kFnRecursionNativeStackDepthMax = 1024;

} // namespace ahfl::ir::core
