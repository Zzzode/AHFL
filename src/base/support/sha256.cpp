#include "base/support/sha256.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ahfl::support {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
    0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
    0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
    0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
    0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
    0xc67178f2U,
};

// SHA-256 processes 64-byte blocks; the total message length is encoded as a
// 64-bit big-endian BIT count, so the message must be shorter than 2^64 bits.
constexpr std::size_t kBlockBytes = 64;
constexpr std::uint64_t kMaxMessageBytes = UINT64_MAX / 8ULL;

[[nodiscard]] std::uint32_t read_be32(const std::uint8_t *bytes) noexcept {
    return (static_cast<std::uint32_t>(bytes[0]) << 24U) |
           (static_cast<std::uint32_t>(bytes[1]) << 16U) |
           (static_cast<std::uint32_t>(bytes[2]) << 8U) | static_cast<std::uint32_t>(bytes[3]);
}

[[nodiscard]] std::uint32_t choose(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept {
    return (x & y) ^ (~x & z);
}

[[nodiscard]] std::uint32_t majority(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept {
    return (x & y) ^ (x & z) ^ (y & z);
}

[[nodiscard]] std::uint32_t big_sigma0(std::uint32_t value) noexcept {
    return std::rotr(value, 2) ^ std::rotr(value, 13) ^ std::rotr(value, 22);
}

[[nodiscard]] std::uint32_t big_sigma1(std::uint32_t value) noexcept {
    return std::rotr(value, 6) ^ std::rotr(value, 11) ^ std::rotr(value, 25);
}

[[nodiscard]] std::uint32_t small_sigma0(std::uint32_t value) noexcept {
    return std::rotr(value, 7) ^ std::rotr(value, 18) ^ (value >> 3U);
}

[[nodiscard]] std::uint32_t small_sigma1(std::uint32_t value) noexcept {
    return std::rotr(value, 17) ^ std::rotr(value, 19) ^ (value >> 10U);
}

void compress_block(const std::uint8_t *block, std::array<std::uint32_t, 8> &hash) noexcept {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0; index < 16; ++index) {
        words[index] = read_be32(block + (index * 4));
    }
    for (std::size_t index = 16; index < words.size(); ++index) {
        words[index] = small_sigma1(words[index - 2]) + words[index - 7] +
                       small_sigma0(words[index - 15]) + words[index - 16];
    }

    auto a = hash[0];
    auto b = hash[1];
    auto c = hash[2];
    auto d = hash[3];
    auto e = hash[4];
    auto f = hash[5];
    auto g = hash[6];
    auto h = hash[7];

    for (std::size_t index = 0; index < words.size(); ++index) {
        const auto t1 = h + big_sigma1(e) + choose(e, f, g) + kRoundConstants[index] + words[index];
        const auto t2 = big_sigma0(a) + majority(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    hash[0] += a;
    hash[1] += b;
    hash[2] += c;
    hash[3] += d;
    hash[4] += e;
    hash[5] += f;
    hash[6] += g;
    hash[7] += h;
}

// Allocation-free incremental SHA-256 state (internal only; NOT a public API).
// Holds only the 8 hash words, a single 64-byte partial block, its fill count,
// and the running total byte count. `update` may be called any number of times;
// the total-length domain is enforced on every update BEFORE any state changes.
class Sha256State {
  public:
    void update(std::span<const std::uint8_t> data) {
        if (data.size() > kMaxMessageBytes - total_bytes_) {
            // total_bytes_ + data.size() would exceed the 2^64-bit domain; reject
            // before touching the compressor. Fixed text: no size/data echo.
            throw std::length_error("sha256 input exceeds the supported message length");
        }
        total_bytes_ += data.size();

        std::size_t offset = 0;
        if (partial_len_ != 0) {
            const std::size_t need = kBlockBytes - partial_len_;
            const std::size_t take = data.size() < need ? data.size() : need;
            for (std::size_t i = 0; i < take; ++i) {
                partial_[partial_len_ + i] = data[i];
            }
            partial_len_ += take;
            offset += take;
            if (partial_len_ == kBlockBytes) {
                compress_block(partial_.data(), hash_);
                partial_len_ = 0;
            }
        }

        while (data.size() - offset >= kBlockBytes) {
            compress_block(data.data() + offset, hash_);
            offset += kBlockBytes;
        }

        for (std::size_t i = offset; i < data.size(); ++i) {
            partial_[partial_len_++] = data[i];
        }
    }

    [[nodiscard]] Sha256Digest finalize() {
        // FIPS 180-4 padding: 0x80, then zero bytes until 56 bytes into the final
        // block, then the 64-bit big-endian message BIT length.
        // total_bytes_ <= UINT64_MAX/8 is guaranteed by the update() domain check,
        // so this multiply cannot overflow.
        const std::uint64_t bit_length = total_bytes_ * 8ULL;
        std::array<std::uint8_t, 1> one{0x80U};
        append_padding(one);
        while (partial_len_ != 56) {
            std::array<std::uint8_t, 1> zero{0x00U};
            append_padding(zero);
        }
        std::array<std::uint8_t, 8> length_be{};
        for (int shift = 56, i = 0; shift >= 0; shift -= 8, ++i) {
            length_be[static_cast<std::size_t>(i)] =
                static_cast<std::uint8_t>((bit_length >> shift) & 0xffU);
        }
        append_padding(length_be);

        Sha256Digest digest{};
        for (std::size_t word = 0; word < hash_.size(); ++word) {
            digest[word * 4 + 0] = static_cast<std::uint8_t>((hash_[word] >> 24U) & 0xffU);
            digest[word * 4 + 1] = static_cast<std::uint8_t>((hash_[word] >> 16U) & 0xffU);
            digest[word * 4 + 2] = static_cast<std::uint8_t>((hash_[word] >> 8U) & 0xffU);
            digest[word * 4 + 3] = static_cast<std::uint8_t>(hash_[word] & 0xffU);
        }
        return digest;
    }

  private:
    // Padding bytes are appended through the same partial-block machinery as data,
    // but WITHOUT touching total_bytes_ or the domain check (padding is not message
    // content). One block is flushed as soon as the partial buffer fills.
    void append_padding(std::span<const std::uint8_t> pad) noexcept {
        for (const std::uint8_t byte : pad) {
            partial_[partial_len_++] = byte;
            if (partial_len_ == kBlockBytes) {
                compress_block(partial_.data(), hash_);
                partial_len_ = 0;
            }
        }
    }

    std::array<std::uint32_t, 8> hash_{
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
    };
    std::array<std::uint8_t, kBlockBytes> partial_{};
    std::size_t partial_len_{0};
    std::uint64_t total_bytes_{0};
};

[[nodiscard]] std::string to_hex(const Sha256Digest &digest) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.resize(digest.size() * 2);
    for (std::size_t i = 0; i < digest.size(); ++i) {
        out[i * 2 + 0] = kDigits[(digest[i] >> 4U) & 0x0fU];
        out[i * 2 + 1] = kDigits[digest[i] & 0x0fU];
    }
    return out;
}

} // namespace

Sha256Digest sha256(std::span<const std::uint8_t> data) {
    Sha256State state;
    state.update(data);
    return state.finalize();
}

std::string sha256_hex(std::span<const std::uint8_t> data) {
    return to_hex(sha256(data));
}

std::string sha256_hex(std::string_view bytes) {
    return sha256_hex(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size()));
}

} // namespace ahfl::support
