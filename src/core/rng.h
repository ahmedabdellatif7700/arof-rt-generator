// Random numbers: xoshiro256++ with a ziggurat normal (Doornik's ZIGNOR, 128 layers).
// MATLAB's rng(seed) stream is not reproduced; the link is compared by its statistics.
// Each random source has its own stream derived from the seed, so switching one source on or
// off does not shift the others. Same seed and settings: same stream, bit for bit.
#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace arof {

// Independent streams, one per random source of the link.
enum class Stream : uint64_t { symbols = 1, pd_noise = 2, ue_noise = 3, rin = 4 };

inline uint64_t splitmix64(uint64_t& s) {
    uint64_t z = (s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

namespace detail {

// ZIGNOR tables (J. A. Doornik, "An improved ziggurat method to generate normal random
// samples", 2005): 128 blocks, R = 3.442619855899, V = 9.91256303526217e-3.
struct ZigTables {
    static constexpr int C = 128;
    static constexpr double R = 3.442619855899;
    static constexpr double V = 9.91256303526217e-3;
    std::array<double, C + 1> x{};
    std::array<double, C> r{};
    ZigTables() {
        double f = std::exp(-0.5 * R * R);
        x[0] = V / f;
        x[1] = R;
        x[C] = 0;
        for (int i = 2; i < C; ++i) {
            x[i] = std::sqrt(-2 * std::log(V / x[i - 1] + f));
            f = std::exp(-0.5 * x[i] * x[i]);
        }
        for (int i = 0; i < C; ++i) r[i] = x[i + 1] / x[i];
    }
};

inline const ZigTables& zig() {
    static const ZigTables t;
    return t;
}

}  // namespace detail

class Rng {
public:
    Rng() : Rng(0, Stream::symbols) {}
    Rng(uint64_t seed, Stream stream) {
        uint64_t sm = seed ^ (static_cast<uint64_t>(stream) * 0xD1B54A32D192ED03ull);
        for (auto& w : s_) w = splitmix64(sm);
    }

    // xoshiro256++ (Blackman and Vigna)
    uint64_t next() {
        const uint64_t result = rotl(s_[0] + s_[3], 23) + s_[0];
        const uint64_t t = s_[1] << 17;
        s_[2] ^= s_[0];
        s_[3] ^= s_[1];
        s_[1] ^= s_[2];
        s_[0] ^= s_[3];
        s_[2] ^= t;
        s_[3] = rotl(s_[3], 45);
        return result;
    }

    // uniform on [0, 1), 53 bits
    double uniform() { return static_cast<double>(next() >> 11) * 0x1.0p-53; }
    // uniform on (0, 1)
    double uniform_open() { return (static_cast<double>(next() >> 11) + 0.5) * 0x1.0p-53; }
    // integer on [0, 2^log2n): randi([0 M-1]) for M = 2^log2n
    uint32_t below_pow2(unsigned log2n) { return static_cast<uint32_t>(next() >> (64 - log2n)); }

    // standard normal: randn
    double normal() {
        const auto& z = detail::zig();
        for (;;) {
            const uint64_t b = next();
            const int i = static_cast<int>(b & 0x7F);
            const double u = 2 * (static_cast<double>(b >> 11) * 0x1.0p-53) - 1;
            if (std::fabs(u) < z.r[i]) return u * z.x[i];       // inside the rectangle
            if (i == 0) return tail(u < 0);                       // base strip: the tail
            const double x = u * z.x[i];                          // wedge
            const double f0 = std::exp(-0.5 * (z.x[i] * z.x[i] - x * x));
            const double f1 = std::exp(-0.5 * (z.x[i + 1] * z.x[i + 1] - x * x));
            if (f1 + uniform() * (f0 - f1) < 1.0) return x;
        }
    }

private:
    static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

    double tail(bool negative) {
        const double r = detail::ZigTables::R;
        double x, y;
        do {
            x = std::log(uniform_open()) / r;
            y = std::log(uniform_open());
        } while (-2 * y < x * x);
        return negative ? x - r : r - x;
    }

    std::array<uint64_t, 4> s_{};
};

}  // namespace arof
