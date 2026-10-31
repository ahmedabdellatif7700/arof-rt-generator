// transmit_receive.m, transmitter part:
//   idx = randi([0 M-1], ...);  d = qammod(idx, M, 'gray', 'UnitAveragePower', true);
//   s = upfirdn(d, rrc, sps);   v = real(s .* exp(1j*2*pi*f_if*t))
// Streaming: a polyphase upfirdn (span+1 symbols of state) and a one-period IF table indexed
// by the global sample count, so the phase is continuous. The AWG scaling to vpp follows in
// TransmitReceive.
#pragma once

#include <cstdint>
#include <vector>

#include "core/comm.h"
#include "core/rng.h"
#include "link/default_tx.h"

namespace arof {

class Transmitter {
public:
    Transmitter(const TxConfig& cfg, uint64_t seed);
    // n random symbols: appends their labels and points, and n*sps samples of v
    void process(size_t n, std::vector<uint8_t>& idx, std::vector<cd>& d, std::vector<double>& v);
    // the same from given labels (tests)
    void process_indices(const uint8_t* idx, size_t n, std::vector<double>& v);
    uint64_t samples() const { return n_; }
    const std::vector<double>& rrc() const { return rrc_; }

private:
    void shape(cd d, std::vector<double>& v);
    int sps_, span_;
    std::vector<double> rrc_;
    std::vector<cd> hist_;                  // the last span+1 symbols, newest at [0]
    std::vector<double> cos_, sin_;         // one IF period
    uint64_t n_ = 0;
    Rng rng_;
};

}  // namespace arof
