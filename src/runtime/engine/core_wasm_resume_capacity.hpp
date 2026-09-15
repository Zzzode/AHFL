#pragma once

// RFC 0026 KR6.5 E4-B2-D2a: the host-INDEPENDENT authority that binds the resume
// controller's caller-supplied `LinearMemoryCapacityBytes` to the REAL fixed
// single-page Core-Wasm VM capacity.
//
// This is a tiny pure authority with NO Wasm VM: it constructs the controller's
// capacity strong type from the shared compiler ABI SSOT
// (`ahfl/compiler/ir/core_wasm_abi_constants.hpp`) and provides the checked
// within-capacity arithmetic a future D2a VM/host adapter needs to size its
// total reservation. It never re-declares 65536. The decision-only resume
// controller keeps its own two-pass TOTAL preflight; this header only gives its
// callers the one correct capacity value and the primitives to stay inside it.

#include <cstdint>
#include <optional>

#include "ahfl/compiler/ir/core_wasm_abi_constants.hpp"
#include "runtime/engine/core_wasm_resume_controller.hpp" // LinearMemoryCapacityBytes

namespace ahfl::runtime::core_wasm_resume_capacity {

// The fixed single-page capacity every emitted Core-Wasm module actually has,
// typed as the controller's strong capacity type.
[[nodiscard]] core_wasm_resume_controller::LinearMemoryCapacityBytes
fixed_single_page_capacity() noexcept;

// Accumulate one reservation addend without unsigned wrap. Returns the new
// total, or std::nullopt when `reservation + addend` overflows u64.
[[nodiscard]] std::optional<std::uint64_t>
checked_reservation_add(std::uint64_t reservation, std::uint64_t addend) noexcept;

// Whether a fully-accumulated reservation lies within the capacity (equality
// fits: a reservation equal to the capacity occupies exactly the whole page).
[[nodiscard]] bool fits_within_capacity(
    core_wasm_resume_controller::LinearMemoryCapacityBytes capacity,
    std::uint64_t reserved_bytes) noexcept;

} // namespace ahfl::runtime::core_wasm_resume_capacity
