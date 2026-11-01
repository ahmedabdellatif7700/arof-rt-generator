// The training dataset back from the received samples. Each sample is 2T + 2 Q8.24 words
// (src/stream/sample_former.h): X = [Re x(n) Im x(n) ... Re x(n-T+1) Im x(n-T+1)] (newest first),
// Y = [Re d(n) Im d(n)]. reconstruct() decodes the words (value = word / 2^24) and rebuilds the
// fields of export_dataset.m (reference/arof_training_data.mat); write_mat() saves them (matio).
//
// One difference from a recording's export: the symbol fields (rx_aligned, tx_*) have one entry
// per sample, the symbol each sample targets. The stream carries the received value of the
// oldest tap's first symbol but not its sent point, so the extra leading symbol of the reference
// layout (9999 symbols for 9998 samples) cannot be rebuilt. X and Y are those of the stream.
#pragma once

#include <cmath>
#include <complex>
#include <cstdint>
#include <string>
#include <vector>

#include "mat_file.h"

namespace arof {

struct Dataset {
    size_t n = 0;                               // samples
    int taps = 0;
    bool grid = true;                           // scale "grid": x, d times sqrt(10), Y on {+-1, +-3}
    std::vector<double> X;                      // n x 2T, column-major (as MATLAB)
    std::vector<double> Y;                      // n x 2, column-major
    std::vector<std::complex<double>> rx, tx;   // per sample: newest tap x(n), target d(n)
    std::vector<uint8_t> idx;                   // Gray label of d(n), as qammod(0:15, 16, 'gray')
    double evm_no_eq = 0;                       // export_dataset.m: sqrt(mean|x - d|^2 / mean|d|^2)
};

// qammod(0:15, 16, 'gray') on the integer grid
inline const std::complex<double>* qam16_grid() {
    static const std::complex<double> p[16] = {
        {-3, 3}, {-3, 1}, {-3, -3}, {-3, -1}, {-1, 3}, {-1, 1}, {-1, -3}, {-1, -1},
        {3, 3},  {3, 1},  {3, -3},  {3, -1},  {1, 3},  {1, 1},  {1, -3},  {1, -1}};
    return p;
}

inline Dataset reconstruct(const std::vector<int32_t>& words, int words_per_sample, bool grid) {
    Dataset ds;
    ds.taps = (words_per_sample - 2) / 2;
    ds.grid = grid;
    const size_t w = static_cast<size_t>(words_per_sample);
    ds.n = w ? words.size() / w : 0;
    const size_t nx = static_cast<size_t>(2 * ds.taps);
    ds.X.resize(ds.n * nx);
    ds.Y.resize(ds.n * 2);
    ds.rx.resize(ds.n);
    ds.tx.resize(ds.n);
    ds.idx.resize(ds.n);
    const double unit = std::ldexp(1.0, -24);
    const double k = grid ? 1.0 : std::sqrt(10.0);   // to the integer grid, for the labels
    double se = 0, sd = 0;
    for (size_t i = 0; i < ds.n; ++i) {
        const int32_t* s = words.data() + i * w;
        for (size_t c = 0; c < nx; ++c) ds.X[c * ds.n + i] = s[c] * unit;
        ds.Y[i] = s[nx] * unit;
        ds.Y[ds.n + i] = s[nx + 1] * unit;
        ds.rx[i] = {ds.X[i], ds.X[ds.n + i]};
        ds.tx[i] = {ds.Y[i], ds.Y[ds.n + i]};
        int best = 0;
        double dmin = 1e300;
        for (int m = 0; m < 16; ++m) {
            const double dist = std::norm(k * ds.tx[i] - qam16_grid()[m]);
            if (dist < dmin) dmin = dist, best = m;
        }
        ds.idx[i] = static_cast<uint8_t>(best);
        se += std::norm(ds.rx[i] - ds.tx[i]);
        sd += std::norm(ds.tx[i]);
    }
    ds.evm_no_eq = sd > 0 ? std::sqrt(se / sd) : 0;
    return ds;
}

// export_dataset.m's fields; vpp_mv, rop_dbm, seed from the stream's CONFIG frame
inline void write_mat(const std::string& path, const Dataset& ds, double vpp_mv, double rop_dbm, double seed) {
    MatFile m(path);
    const size_t n = ds.n;
    m.add<double>("X", ds.X.data(), nullptr, n, static_cast<size_t>(2 * ds.taps));
    if (ds.grid) {
        std::vector<int16_t> y(ds.Y.size());
        for (size_t i = 0; i < y.size(); ++i) y[i] = static_cast<int16_t>(std::lround(ds.Y[i]));
        m.add<int16_t>("Y", y.data(), nullptr, n, 2);
    } else {
        m.add<double>("Y", ds.Y.data(), nullptr, n, 2);
    }
    std::vector<double> re(n), im(n);
    auto complex_field = [&](const char* name, const std::vector<std::complex<double>>& v) {
        for (size_t i = 0; i < n; ++i) re[i] = v[i].real(), im[i] = v[i].imag();
        m.add<double>(name, re.data(), im.data(), n, 1);
    };
    complex_field("rx_aligned", ds.rx);
    complex_field("tx_aligned", ds.tx);
    complex_field("tx_symbols", ds.tx);
    m.add<uint8_t>("tx_indices", ds.idx.data(), nullptr, n, 1);
    std::vector<uint8_t> bits(4 * n);                       // de2bi(idx, 4, 'left-msb'), one column
    for (size_t i = 0; i < n; ++i)
        for (int b = 0; b < 4; ++b) bits[4 * i + b] = (ds.idx[i] >> (3 - b)) & 1;
    m.add<uint8_t>("tx_bits", bits.data(), nullptr, bits.size(), 1);
    m.scalar<uint8_t>("mod_order", 16);
    m.scalar<uint8_t>("bits_per_symbol", 4);
    m.scalar<uint8_t>("delay", static_cast<uint8_t>(ds.taps - 1));
    m.scalar<uint8_t>("equalizer_taps", static_cast<uint8_t>(ds.taps));
    m.scalar<uint8_t>("equalizer_input_size", static_cast<uint8_t>(2 * ds.taps));
    m.scalar<uint8_t>("equalizer_output_size", 2);
    m.scalar<double>("vpp_mv", vpp_mv);
    m.scalar<double>("rop_dbm", rop_dbm);
    m.scalar<double>("seed", seed);
    m.scalar<double>("evm_no_eq", ds.evm_no_eq);
    m.text("source", "A-RoF 60 GHz link (Liu JLT 2017), arof_stream, received by arof_rx");
}

}  // namespace arof
