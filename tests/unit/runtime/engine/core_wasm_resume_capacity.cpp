// RFC 0026 KR6.5 E4-B2-D2a foundation regression for the host-independent resume
// capacity authority. Hand-rolled check()/main(). Pins the fixed single-page SSOT
// value (65536), the one-page facts, and the checked binding/fit arithmetic edge
// cases. FOUNDATION only: no VM, no durable resume.

#include "runtime/engine/core_wasm_resume_capacity.hpp"

#include "ahfl/compiler/ir/core_wasm_abi_constants.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>

namespace {

using ahfl::ir::core::kCoreWasmFixedLinearMemoryCapacityBytes;
using ahfl::ir::core::kCoreWasmFixedLinearMemoryMinPages;
using ahfl::ir::core::kCoreWasmLinearMemoryPageSizeBytes;
using ahfl::runtime::core_wasm_resume_capacity::checked_reservation_add;
using ahfl::runtime::core_wasm_resume_capacity::fixed_single_page_capacity;
using ahfl::runtime::core_wasm_resume_capacity::fits_within_capacity;
using ahfl::runtime::core_wasm_resume_controller::LinearMemoryCapacityBytes;

int g_failures = 0;

void check(bool ok, std::string_view name) {
    if (!ok) {
        ++g_failures;
        std::cerr << "FAIL: " << name << "\n";
    }
}

} // namespace

int main() {
    // Fixed single-page facts.
    check(kCoreWasmLinearMemoryPageSizeBytes == 65536, "page_size_is_65536");
    check(kCoreWasmFixedLinearMemoryMinPages == 1, "min_pages_is_one");
    check(kCoreWasmFixedLinearMemoryCapacityBytes == 65536,
          "fixed_capacity_constant_is_65536");
    check(static_cast<std::uint64_t>(kCoreWasmLinearMemoryPageSizeBytes) *
                    kCoreWasmFixedLinearMemoryMinPages ==
                kCoreWasmFixedLinearMemoryCapacityBytes,
          "capacity_is_page_size_times_min_pages");

    // The runtime authority binds the controller's strong type to the SSOT.
    const LinearMemoryCapacityBytes capacity = fixed_single_page_capacity();
    check(capacity == LinearMemoryCapacityBytes{65536}, "bound_capacity_is_65536");
    check(capacity.value == kCoreWasmFixedLinearMemoryCapacityBytes,
          "bound_capacity_matches_abi_ssot");

    // Within-capacity predicate: equality fits (occupies exactly the whole page).
    check(fits_within_capacity(capacity, 0), "zero_reservation_fits");
    check(fits_within_capacity(capacity, 1), "one_byte_fits");
    check(fits_within_capacity(capacity, 65535), "capacity_minus_one_fits");
    check(fits_within_capacity(capacity, 65536), "exact_capacity_fits");
    check(!fits_within_capacity(capacity, 65537), "capacity_plus_one_does_not_fit");
    check(!fits_within_capacity(capacity, std::numeric_limits<std::uint64_t>::max()),
          "u64_max_does_not_fit");

    // Checked reservation accumulation.
    {
        const auto total = checked_reservation_add(0, 0);
        check(total.has_value() && *total == 0, "add_zero_to_zero");
    }
    {
        const auto total = checked_reservation_add(1024, 40);
        check(total.has_value() && *total == 1064, "add_40_to_1024");
    }
    {
        // Accumulate to exactly the capacity: every add fits and the final total
        // is within capacity.
        std::uint64_t total = 0;
        const auto a = checked_reservation_add(total, 1024);
        check(a.has_value(), "heap_base_add_ok");
        if (a.has_value()) {
            total = *a;
        }
        const auto b = checked_reservation_add(total, 65536 - 1024);
        check(b.has_value() && *b == 65536, "accumulate_to_exact_capacity");
        if (b.has_value()) {
            check(fits_within_capacity(capacity, *b), "exact_total_within_capacity");
        }
    }
    {
        // One byte past the capacity overflows the fit verdict even though the
        // u64 arithmetic itself is legal.
        const auto total = checked_reservation_add(65536, 1);
        check(total.has_value() && *total == 65537, "one_past_arithmetic_ok");
        if (total.has_value()) {
            check(!fits_within_capacity(capacity, *total), "one_past_fails_fit");
        }
    }
    {
        // Unsigned-wrap guard: any addend past the u64 headroom is rejected.
        const auto overflow = checked_reservation_add(
            std::numeric_limits<std::uint64_t>::max(), 1);
        check(!overflow.has_value(), "u64_overflow_rejected");
    }
    {
        const auto overflow_zero_addend = checked_reservation_add(
            std::numeric_limits<std::uint64_t>::max() - 4, 5);
        check(!overflow_zero_addend.has_value(), "boundary_overflow_rejected");
    }
    {
        const auto boundary_ok = checked_reservation_add(
            std::numeric_limits<std::uint64_t>::max() - 4, 4);
        check(boundary_ok.has_value() && *boundary_ok ==
                                             std::numeric_limits<std::uint64_t>::max(),
              "boundary_exact_add_ok");
    }

    if (g_failures == 0) {
        std::cout << "core_wasm_resume_capacity: all checks passed\n";
        return 0;
    }
    std::cerr << "core_wasm_resume_capacity: " << g_failures << " failure(s)\n";
    return 1;
}
