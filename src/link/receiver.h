// transmit_receive.m, offline DSP part:
//   z = conv(2*(r - mean(r)) .* exp(-1j*2*pi*f_if*t), rrc(:));
//   timing: the offset o in nominal..nominal+sps with the best |zs'*d(train)|/norm(zs);
//   g = (x(train)'*d(train)) / (x(train)'*x(train));   x = g * z(offset + k*sps)
// Streaming: mean(r), the offset and g are found once on a calibration block (acquire) and
// then frozen; the matched filter runs only at the symbol instants.
#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include "core/comm.h"
#include "link/default_tx.h"

namespace arof {

// One received symbol: x = g * z(offset + k*sps), the sent point d (unit power), its label,
// and k, the symbol count since the start of the program.
struct RxSymbol {
    cd x;
    cd d;
    uint8_t idx;
    uint64_t k;
};

struct RxConstants {
    double r_mean = 0;
    int offset = 0;     // 0-based sample of z for symbol 0 (MATLAB's offset - 1)
    cd gain = 1;
};

struct Acquisition {
    int offset;
    cd gain;
    double corr;        // best training correlation
};

// Timing search and gain. r: quantized ADC samples for times t0 .. t0+n-1 (time 0 is the first
// sample of symbol 0; earlier samples are conv's zero padding). d: the n_train training points,
// the first of them symbol k0.
Acquisition acquire(const TxConfig& cfg, const std::vector<double>& rrc, const double* r, int64_t t0,
                    size_t n, double r_mean, const cd* d, int64_t k0, size_t n_train);

class Receiver {
public:
    explicit Receiver(const TxConfig& cfg);
    void set_constants(const RxConstants& c) { c_ = c; }
    const RxConstants& constants() const { return c_; }
    // the sent symbols, in order, as the transmitter generates them
    void push_symbol(cd d, uint8_t idx) { sym_.emplace_back(d, idx); }
    // quantized ADC samples, contiguous in time from t = 0; appends the symbols whose
    // matched-filter instant is reached
    void process(const double* r, size_t n, std::vector<RxSymbol>& out);
    int64_t time() const { return t_; }
    uint64_t skipped() const { return skipped_; }   // symbols lost to a timing change

private:
    int sps_, len_;                         // len_: rrc taps
    std::vector<double> rrc_;
    std::vector<double> cos_, sin_;
    RxConstants c_;
    std::vector<cd> y_;                     // down-converted samples, y_[0] at time y_t0_
    int64_t y_t0_, t_ = 0;
    std::deque<std::pair<cd, uint8_t>> sym_;
    uint64_t k_front_ = 0, skipped_ = 0;
};

}  // namespace arof
