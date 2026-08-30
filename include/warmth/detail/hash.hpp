#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <array>
#include <string_view>

namespace warmth::detail {

inline std::uint32_t crc32(const void* data, std::size_t length, std::uint32_t seed = 0xFFFFFFFFu) {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1U) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        return t;
    }();

    const auto* p = static_cast<const std::uint8_t*>(data);
    std::uint32_t c = seed;
    for (std::size_t i = 0; i < length; ++i)
        c = table[(c ^ static_cast<std::uint32_t>(p[i])) & 0xFFU] ^ (c >> 8);
    return c ^ 0xFFFFFFFFU;
}

class Sha256 {
public:
    Sha256() { reset(); }

    void reset() noexcept {
        state_ = {
            0x6a09e667UL, 0xbb67ae85UL, 0x3c6ef372UL, 0xa54ff53aUL,
            0x510e527fUL, 0x9b05688cUL, 0x1f83d9abUL, 0x5be0cd19UL
        };
        total_ = 0;
        bufLen_ = 0;
    }

    void update(const void* data, std::size_t length) noexcept {
        const auto* p = static_cast<const std::uint8_t*>(data);
        total_ += length;
        while (length > 0) {
            const std::size_t n = (length < 64 - bufLen_) ? length : (64 - bufLen_);
            std::memcpy(buf_ + bufLen_, p, n);
            bufLen_ += n;
            p += n;
            length -= n;
            if (bufLen_ == 64) { transform(buf_, 1); bufLen_ = 0; }
        }
    }

    std::array<std::uint8_t, 32> finish() noexcept {
        const std::uint64_t bitLen = static_cast<std::uint64_t>(total_) * 8;
        const std::uint8_t pad = 0x80;
        update(&pad, 1);
        const std::uint8_t zero = 0x00;
        while (bufLen_ != 56) update(&zero, 1);
        std::array<std::uint8_t, 8> len{};
        for (int i = 0; i < 8; ++i) len[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(bitLen >> (56 - i * 8));
        update(len.data(), len.size());

        std::array<std::uint8_t, 32> digest{};
        for (std::size_t i = 0; i < 8; ++i) {
            digest[i * 4 + 0] = static_cast<std::uint8_t>(state_[i] >> 24);
            digest[i * 4 + 1] = static_cast<std::uint8_t>(state_[i] >> 16);
            digest[i * 4 + 2] = static_cast<std::uint8_t>(state_[i] >> 8);
            digest[i * 4 + 3] = static_cast<std::uint8_t>(state_[i] >> 0);
        }
        return digest;
    }

    static std::array<std::uint8_t, 32> digest(const void* data, std::size_t length) noexcept {
        Sha256 h;
        h.update(data, length);
        return h.finish();
    }

private:
    static std::uint32_t rotr(std::uint32_t x, int n) noexcept { return (x >> n) | (x << (32 - n)); }

    void transform(const std::uint8_t* p, std::size_t blocks) noexcept {
        static constexpr std::array<std::uint32_t, 64> K = {
            0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL, 0x3956c25bUL,
            0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL, 0xd807aa98UL, 0x12835b01UL,
            0x243185beUL, 0x550c7dc3UL, 0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL,
            0xc19bf174UL, 0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL,
            0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL, 0x983e5152UL,
            0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL, 0xc6e00bf3UL, 0xd5a79147UL,
            0x06ca6351UL, 0x14292967UL, 0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL,
            0x53380d13UL, 0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
            0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL, 0xd192e819UL,
            0xd6990624UL, 0xf40e3585UL, 0x106aa070UL, 0x19a4c116UL, 0x1e376c08UL,
            0x2748774cUL, 0x34b0bcb5UL, 0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL,
            0x682e6ff3UL, 0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL,
            0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL
        };
        for (std::size_t b = 0; b < blocks; ++b) {
            std::array<std::uint32_t, 64> w{};
            for (int i = 0; i < 16; ++i) {
                w[static_cast<std::size_t>(i)] =
                    (static_cast<std::uint32_t>(p[i * 4]) << 24) |
                    (static_cast<std::uint32_t>(p[i * 4 + 1]) << 16) |
                    (static_cast<std::uint32_t>(p[i * 4 + 2]) << 8) |
                    (static_cast<std::uint32_t>(p[i * 4 + 3]));
            }
            for (int i = 16; i < 64; ++i) {
                const std::uint32_t s0 = rotr(w[static_cast<std::size_t>(i - 15)], 7) ^ rotr(w[static_cast<std::size_t>(i - 15)], 18) ^ (w[static_cast<std::size_t>(i - 15)] >> 3);
                const std::uint32_t s1 = rotr(w[static_cast<std::size_t>(i - 2)], 17) ^ rotr(w[static_cast<std::size_t>(i - 2)], 19) ^ (w[static_cast<std::size_t>(i - 2)] >> 10);
                w[static_cast<std::size_t>(i)] = w[static_cast<std::size_t>(i - 16)] + s0 + w[static_cast<std::size_t>(i - 7)] + s1;
            }
            std::uint32_t a = state_[0], b0 = state_[1], c = state_[2], d = state_[3];
            std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
            for (int i = 0; i < 64; ++i) {
                const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
                const std::uint32_t ch = (e & f) ^ (~e & g);
                const std::uint32_t t1 = h + s1 + ch + K[static_cast<std::size_t>(i)] + w[static_cast<std::size_t>(i)];
                const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
                const std::uint32_t maj = (a & b0) ^ (a & c) ^ (b0 & c);
                const std::uint32_t t2 = s0 + maj;
                h = g; g = f; f = e; e = d + t1;
                d = c; c = b0; b0 = a; a = t1 + t2;
            }
            state_[0] += a; state_[1] += b0; state_[2] += c; state_[3] += d;
            state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
            p += 64;
        }
    }

    std::array<std::uint32_t, 8> state_{};
    std::uint64_t total_ = 0;
    std::uint8_t buf_[64]{};
    std::size_t bufLen_ = 0;
};

inline std::array<std::uint8_t, 16> digest16(const void* data, std::size_t length) noexcept {
    const auto full = Sha256::digest(data, length);
    std::array<std::uint8_t, 16> out{};
    for (std::size_t i = 0; i < 16; ++i) out[i] = full[i];
    return out;
}

inline std::array<std::uint8_t, 16> digest16(std::string_view s) noexcept {
    return digest16(s.data(), s.size());
}

} // namespace warmth::detail
