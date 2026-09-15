// RFC 0026 KR6.5 E4-B2-D2a: see the matching header for the contract.

#include "runtime/engine/core_wasm_resume_capacity.hpp"

#include <limits>

#include "ahfl/compiler/ir/core_wasm_abi_constants.hpp"

namespace ahfl::runtime::core_wasm_resume_capacity {

namespace controller = core_wasm_resume_controller;
using controller::LinearMemoryCapacityBytes;

[[nodiscard]] LinearMemoryCapacityBytes fixed_single_page_capacity() noexcept {
    return LinearMemoryCapacityBytes{ir::core::kCoreWasmFixedLinearMemoryCapacityBytes};
}

[[nodiscard]] std::optional<std::uint64_t>
checked_reservation_add(std::uint64_t reservation, std::uint64_t addend) noexcept {
    if (reservation > std::numeric_limits<std::uint64_t>::max() - addend) {
        return std::nullopt;
    }
    return reservation + addend;
}

[[nodiscard]] bool fits_within_capacity(LinearMemoryCapacityBytes capacity,
                                        std::uint64_t reserved_bytes) noexcept {
    return reserved_bytes <= capacity.value;
}

} // namespace ahfl::runtime::core_wasm_resume_capacity
