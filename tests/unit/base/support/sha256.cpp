#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "base/support/sha256.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

// Byte span over a string_view's raw bytes (embedded NUL bytes included).
[[nodiscard]] std::span<const std::uint8_t> bytes_of(std::string_view text) {
    return std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t *>(text.data()), text.size());
}

// Parse a fixed hex vector (from the published standard) into a raw digest so a
// test never compares the implementation against its own output. This helper is
// test-only and is NOT a production API.
[[nodiscard]] ahfl::support::Sha256Digest digest_from_hex(std::string_view hex) {
    REQUIRE(hex.size() == 64);
    ahfl::support::Sha256Digest out{};
    const auto nibble = [](char c) -> std::uint8_t {
        if (c >= '0' && c <= '9') {
            return static_cast<std::uint8_t>(c - '0');
        }
        return static_cast<std::uint8_t>((c - 'a') + 10);
    };
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<std::uint8_t>((nibble(hex[i * 2]) << 4U) | nibble(hex[i * 2 + 1]));
    }
    return out;
}

} // namespace

TEST_CASE("SHA-256 FIPS 180-4 known-answer vectors") {
    // Fixed expected values from FIPS 180-4 / NIST CAVP; hardcoded, not generated.
    CHECK(ahfl::support::sha256_hex(std::string_view{""}) ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(ahfl::support::sha256_hex(std::string_view{"abc"}) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    // 56-byte two-block message (FIPS 180-4 example 2).
    CHECK(ahfl::support::sha256_hex(std::string_view{
              "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"}) ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("SHA-256 one-million 'a' long-input vector") {
    // FIPS 180-4: 1,000,000 repetitions of 'a'.
    const std::string million(1000000, 'a');
    CHECK(ahfl::support::sha256_hex(std::string_view{million}) ==
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("SHA-256 block-boundary lengths have fixed expected digests") {
    // Messages of length 55/56/63/64/65 bytes of 'a', expected values from an
    // independent SHA-256 computation, hardcoded here.
    struct Case {
        std::size_t length;
        std::string_view expected;
    };
    const std::array<Case, 5> cases{{
        {55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
        {56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
        {63, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
        {64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
        {65, "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"},
    }};
    for (const auto &c : cases) {
        const std::string input(c.length, 'a');
        CHECK(ahfl::support::sha256_hex(std::string_view{input}) == c.expected);
    }
}

TEST_CASE("SHA-256 hashes embedded NUL bytes without truncation") {
    // "a\0b\0c" — a bare C-string length walk would stop at the first NUL.
    const std::string with_nul = std::string("a") + '\0' + "b" + '\0' + "c";
    REQUIRE(with_nul.size() == 5);
    const std::vector<std::uint8_t> raw{0x61, 0x00, 0x62, 0x00, 0x63};

    // Fixed external expected value for the exact 5 bytes 61 00 62 00 63.
    const auto expected =
        digest_from_hex("8badde10c760e9b702defb4b5e225de79c515b1d2a5cfb000e140f3c6fbb5629");
    CHECK(ahfl::support::sha256(std::span<const std::uint8_t>(raw)) == expected);
    CHECK(ahfl::support::sha256_hex(std::string_view{with_nul}) ==
          "8badde10c760e9b702defb4b5e225de79c515b1d2a5cfb000e140f3c6fbb5629");
    // And it must differ from hashing only "a" (proves no truncation at the NUL).
    CHECK(ahfl::support::sha256_hex(std::string_view{with_nul}) !=
          ahfl::support::sha256_hex(std::string_view{"a"}));
}

TEST_CASE("SHA-256 span / hex / string_view forms agree") {
    const std::string message = "the quick brown fox jumps over the lazy dog";
    const auto digest = ahfl::support::sha256(bytes_of(message));
    const std::string hex_span = ahfl::support::sha256_hex(bytes_of(message));
    const std::string hex_view = ahfl::support::sha256_hex(std::string_view{message});

    // The span and string_view hex forms describe the same bytes.
    CHECK(hex_span == hex_view);
    // Independent expected value (hardcoded); raw digest matches the same vector.
    CHECK(hex_span == "05c6e08f1d9fdafa03147fcb8f82f124c76d2f70e3d989dc8aadb5e7d7450bec");
    CHECK(digest ==
          digest_from_hex("05c6e08f1d9fdafa03147fcb8f82f124c76d2f70e3d989dc8aadb5e7d7450bec"));
}

TEST_CASE("SHA-256 empty span equals empty string_view") {
    const std::array<std::uint8_t, 0> empty{};
    CHECK(ahfl::support::sha256_hex(std::span<const std::uint8_t>(empty)) ==
          ahfl::support::sha256_hex(std::string_view{""}));
    CHECK(ahfl::support::sha256(std::span<const std::uint8_t>(empty)) ==
          digest_from_hex("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
}
