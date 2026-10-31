// matlab/core/evaluate_outputs.m for the no-EQ output: on the test symbols
//   ks_test = taps + round(train_frac*n_sym) : n_sym,  tau = floor(taps/2)
// the output is x(ks_test - tau) against d(ks_test - tau):
//   evm = sqrt(mean(abs(y - d_ref).^2))   (unit average power, no further normalization)
//   ber = biterr(qamdemod(y), idx_ref) / (numel(ks_test) * 4)
#pragma once

#include <cstddef>

#include "link/receiver.h"

namespace arof {

struct EvmBer {
    double evm = 0, ber = 0;
    size_t n = 0;           // symbols evaluated
};

// evaluate_outputs.m, no EQ, on a sequence of n_sym received symbols
EvmBer evaluate_no_eq(const RxSymbol* s, size_t n_sym, int taps, double train_frac);
// the same measures over every symbol given
EvmBer evm_ber(const RxSymbol* s, size_t n);

}  // namespace arof
