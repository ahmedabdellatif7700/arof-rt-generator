#include "core/evaluate_outputs.h"

#include <cmath>

namespace arof {

EvmBer evm_ber(const RxSymbol* s, size_t n) {
    EvmBer e;
    e.n = n;
    if (n == 0) return e;
    double se = 0;
    uint64_t err = 0;
    for (size_t i = 0; i < n; ++i) {
        se += std::norm(s[i].x - s[i].d);
        err += static_cast<uint64_t>(__builtin_popcount(static_cast<unsigned>(qamdemod16(s[i].x) ^ s[i].idx)));
    }
    e.evm = std::sqrt(se / static_cast<double>(n));
    e.ber = static_cast<double>(err) / (4.0 * static_cast<double>(n));
    return e;
}

EvmBer evaluate_no_eq(const RxSymbol* s, size_t n_sym, int taps, double train_frac) {
    const auto first = static_cast<long>(taps + std::lround(train_frac * static_cast<double>(n_sym)));
    const long tau = taps / 2;
    if (first > static_cast<long>(n_sym)) return {};
    // ks_test - tau, 1-based -> 0-based
    const long a = first - tau - 1, b = static_cast<long>(n_sym) - tau - 1;
    return evm_ber(s + a, static_cast<size_t>(b - a + 1));
}

}  // namespace arof
