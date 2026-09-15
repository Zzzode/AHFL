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

} // namespace ahfl::ir::core
