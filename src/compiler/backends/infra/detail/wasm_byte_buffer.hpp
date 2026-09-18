#pragma once

// RFC 0026 P6 (KR6.6): the WebAssembly byte-emitting buffer shared by the
// KR6.5 E1-E3 encoders and the P6 scalar stack machine. Lives in an internal
// detail header (not under include/) so unit tests can pin the exact emitted
// bytes, including canonical signed-LEB128 immediates, without linking a
// duplicate copy of the encoder.

#include <cstdint>
#include <initializer_list>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace ahfl::backends::detail {

// Arithmetic (sign-filling) right shift, written out instead of relying on
// signed `>>` (which was implementation-defined before C++20). Negative
// inputs get the vacated high bits refilled with ones; non-negative inputs
// use a plain logical shift.
template <typename T>
    requires std::is_signed_v<T>
[[nodiscard]] constexpr T arithmetic_shift_right(T value, unsigned amount) {
    using Unsigned = std::make_unsigned_t<T>;
    constexpr unsigned kWidth = std::numeric_limits<Unsigned>::digits;
    if (amount >= kWidth) {
        return value < 0 ? static_cast<T>(~Unsigned{0}) : T{0};
    }
    auto bits = static_cast<Unsigned>(value) >> amount;
    if (value < 0 && amount > 0) {
        bits |= (~Unsigned{0} << (kWidth - amount));
    }
    return static_cast<T>(bits);
}

class ByteBuffer {
  public:
    void byte(std::uint8_t value) {
        bytes_.push_back(value);
    }
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
    // Unsigned-spelling LEB128 that may carry a value up to UINT32_MAX: emit
    // the extra continuation byte when the payload's sign bit would otherwise
    // read as a negative signed immediate.
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
    // Canonical signed LEB128 immediates for i32.const / i64.const. The
    // sign-extension termination rule uses the signed (arithmetically shifted)
    // remaining value: stop on zero with a clear sign bit or -1 with a set
    // sign bit. The encoding is minimal (wasm strict validation rejects unused
    // high bits in the final byte).
    void s32(std::int32_t value) {
        append_signed(value);
    }
    void s64(std::int64_t value) {
        append_signed(value);
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
    [[nodiscard]] std::vector<std::uint8_t> take() && {
        return std::move(bytes_);
    }

  private:
    template <typename T>
        requires std::is_signed_v<T>
    void append_signed(T input) {
        T value = input;
        bool more = true;
        do {
            auto next =
                static_cast<std::uint8_t>(static_cast<std::make_unsigned_t<T>>(value) & 0x7fu);
            value = arithmetic_shift_right(value, 7);
            const bool sign_bit = (next & 0x40u) != 0;
            if ((value == 0 && !sign_bit) || (value == static_cast<T>(-1) && sign_bit)) {
                more = false;
            } else {
                next = static_cast<std::uint8_t>(next | 0x80u);
            }
            byte(next);
        } while (more);
    }

    std::vector<std::uint8_t> bytes_;
};

} // namespace ahfl::backends::detail
