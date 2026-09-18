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

// The scratch arena is every byte from its base to the end of the fixed page.
// A plan whose constructors need more fails closed (RESOURCE-class rejection),
// so the arena can never overflow into unowned memory.
inline constexpr std::uint32_t kP6AggregateScratchCapacity =
    kCoreWasmFixedLinearMemoryCapacityBytes - kP6AggregateScratchBase;

static_assert(kP6AggregateInputBase % 8 == 0 && kP6AggregateContextBase % 8 == 0 &&
                  kP6AggregateScratchBase % 8 == 0,
              "P6 aggregate frame bases are 8-byte aligned (the widest P4-D scalar)");
static_assert(kP6AggregateContextBase >= kP6AggregateInputBase &&
                  kP6AggregateScratchBase >= kP6AggregateContextBase,
              "P6 aggregate frame regions are ordered and non-overlapping");
static_assert(kP6AggregateScratchBase < kCoreWasmFixedLinearMemoryCapacityBytes,
              "the P6 aggregate scratch arena starts inside the fixed single page");

} // namespace ahfl::ir::core
