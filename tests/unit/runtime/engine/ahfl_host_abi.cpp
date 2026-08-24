// RFC 0021 slice 1 contract test: the ahfl_host.h capability embedding ABI.
//
// The header is a published C FFI boundary. These assertions lock the ABI
// invariants the design review made non-negotiable: fixed 32-bit widths for the
// status/format/version types, OK == 0, the size-prefixed args struct with
// struct_size as its first field, and the version constant. The header is C but
// guarded by extern "C"; this project is C++-only, so we compile it as C++ and
// verify the contract holds under a C++ translation unit.

#include "ahfl/runtime/ahfl_host.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <type_traits>

namespace {

int test_count = 0;
int pass_count = 0;

void check(bool condition, const std::string &name) {
    ++test_count;
    if (condition) {
        ++pass_count;
    } else {
        std::cerr << "FAIL: " << name << "\n";
    }
}

// The ABI types must be exactly 32-bit unsigned — a bare enum would have
// implementation-defined width and break the boundary.
static_assert(std::is_same_v<ahfl_cap_status, std::uint32_t>,
              "ahfl_cap_status must be uint32_t");
static_assert(std::is_same_v<ahfl_wire_format, std::uint32_t>,
              "ahfl_wire_format must be uint32_t");
static_assert(sizeof(ahfl_cap_status) == 4, "status is 32-bit");

// OK must stay 0 (fail-closed: anything non-OK is a failure/pending).
static_assert(AHFL_CAP_OK == 0u, "OK must be 0");
static_assert(AHFL_CAP_ERROR == 1u && AHFL_CAP_PENDING == 2u, "core status codes pinned");

// struct_size must be the first field so readers can detect appended fields.
static_assert(offsetof(ahfl_invoke_args, struct_size) == 0,
              "struct_size must be the first field of ahfl_invoke_args");

void test_version_constant() {
    check(AHFL_ABI_VERSION == 1u, "ABI version constant is 1");
}

void test_wire_format() {
    check(AHFL_WIRE_VALUE_JSON == 1u, "value_json wire format is 1");
}

void test_args_struct_shape() {
    // A caller populating the struct sets struct_size to its own sizeof, so the
    // runtime can tell which (appended) fields are present.
    ahfl_invoke_args args{};
    args.struct_size = static_cast<std::uint32_t>(sizeof(ahfl_invoke_args));
    args.cap_id = 42u;
    check(args.struct_size == sizeof(ahfl_invoke_args) && args.cap_id == 42u,
          "ahfl_invoke_args is populated by struct_size + cap_id");
    check(args.result_ptr == nullptr && args.result_len == nullptr,
          "value-initialized args has null out-params");
}

void test_status_values_distinct() {
    check(AHFL_CAP_OK != AHFL_CAP_ERROR && AHFL_CAP_ERROR != AHFL_CAP_PENDING,
          "the three core status codes are distinct");
}

} // namespace

int main() {
    test_version_constant();
    test_wire_format();
    test_args_struct_shape();
    test_status_values_distinct();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
