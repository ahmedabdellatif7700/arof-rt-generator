#include "link/transmitter.h"

#include <algorithm>
#include <stdexcept>

namespace arof {

Transmitter::Transmitter(const TxConfig& cfg, uint64_t seed)
    : sps_(cfg.sps()), span_(cfg.rrc_span), rrc_(rcosdesign_sqrt(cfg.rolloff, cfg.rrc_span, cfg.sps())),
      hist_(cfg.rrc_span + 1), rng_(seed, Stream::symbols) {
    if (cfg.M != 16) throw std::invalid_argument("only M = 16 is implemented");
    tone_table(cfg.f_if, cfg.fs, cos_, sin_);
}

void Transmitter::shape(cd d, std::vector<double>& v) {
    // upfirdn(d, rrc, sps): sample sps*k + ph is sum_j d(k-j) * rrc(ph + sps*j)
    std::rotate(hist_.rbegin(), hist_.rbegin() + 1, hist_.rend());
    hist_[0] = d;
    const size_t period = cos_.size();
    size_t ph_if = n_ % period;
    const int last = span_ * sps_;
    for (int ph = 0; ph < sps_; ++ph) {
        double sr = 0, si = 0;
        for (int j = 0, k = ph; k <= last; ++j, k += sps_) {
            sr += hist_[j].real() * rrc_[k];
            si += hist_[j].imag() * rrc_[k];
        }
        v.push_back(sr * cos_[ph_if] - si * sin_[ph_if]);   // real(s .* exp(1j*2*pi*f_if*t))
        if (++ph_if == period) ph_if = 0;
    }
    n_ += sps_;
}

void Transmitter::process(size_t n, std::vector<uint8_t>& idx, std::vector<cd>& d, std::vector<double>& v) {
    const auto& pts = qam16_gray();
    v.reserve(v.size() + n * sps_);
    for (size_t i = 0; i < n; ++i) {
        const auto k = static_cast<uint8_t>(rng_.below_pow2(4));   // randi([0 M-1])
        idx.push_back(k);
        d.push_back(pts[k]);
        shape(pts[k], v);
    }
}

void Transmitter::process_indices(const uint8_t* idx, size_t n, std::vector<double>& v) {
    const auto& pts = qam16_gray();
    for (size_t i = 0; i < n; ++i) shape(pts[idx[i] & 15], v);
}

}  // namespace arof
