#include "link/receiver.h"

#include <cmath>
#include <stdexcept>

namespace arof {

Acquisition acquire(const TxConfig& cfg, const std::vector<double>& rrc, const double* r, int64_t t0,
                    size_t n, double r_mean, const cd* d, int64_t k0, size_t n_train) {
    const int sps = cfg.sps();
    const int len = static_cast<int>(rrc.size());
    const int nominal = cfg.rrc_span * sps;           // delay of the RRC pair (0-based)
    std::vector<double> ct, st;
    tone_table(cfg.f_if, cfg.fs, ct, st);
    const auto period = static_cast<int64_t>(ct.size());

    // y = 2*(r - mean(r)) .* exp(-1j*2*pi*f_if*t) over the times the search needs
    const int64_t ta = nominal + sps * k0 - (len - 1);
    const int64_t tb = nominal + sps + sps * (k0 + static_cast<int64_t>(n_train) - 1);
    if (tb >= t0 + static_cast<int64_t>(n) || (ta < t0 && t0 > 0))
        throw std::runtime_error("acquire: the calibration block does not cover the training symbols");
    std::vector<cd> y(tb - ta + 1);
    for (int64_t t = std::max<int64_t>(ta, 0); t <= tb; ++t) {   // zero padding before t = 0
        const double a = 2 * (r[t - t0] - r_mean);
        const int64_t ph = t % period;
        y[t - ta] = cd(a * ct[ph], -a * st[ph]);
    }
    auto z = [&](int64_t m) {                         // conv(y, rrc) at sample m
        double zr = 0, zi = 0;
        const cd* yy = y.data() + (m - ta);
        for (int i = 0; i < len; ++i) {
            zr += yy[-i].real() * rrc[i];
            zi += yy[-i].imag() * rrc[i];
        }
        return cd(zr, zi);
    };

    Acquisition best{nominal, 1, -1};
    std::vector<cd> zs(n_train), zbest;
    for (int o = nominal; o <= nominal + sps; ++o) {  // timing: best training correlation
        cd num = 0;
        double nrm = 0;
        for (size_t j = 0; j < n_train; ++j) {
            zs[j] = z(o + sps * (k0 + static_cast<int64_t>(j)));
            num += std::conj(zs[j]) * d[j];
            nrm += std::norm(zs[j]);
        }
        const double c = std::abs(num) / std::sqrt(nrm);
        if (c > best.corr) {
            best.corr = c;
            best.offset = o;
            zbest = zs;
        }
    }
    cd num = 0;                                       // complex gain, least squares
    double den = 0;
    for (size_t j = 0; j < n_train; ++j) {
        num += std::conj(zbest[j]) * d[j];
        den += std::norm(zbest[j]);
    }
    best.gain = num / den;
    return best;
}

Receiver::Receiver(const TxConfig& cfg)
    : sps_(cfg.sps()), rrc_(rcosdesign_sqrt(cfg.rolloff, cfg.rrc_span, cfg.sps())) {
    len_ = static_cast<int>(rrc_.size());
    tone_table(cfg.f_if, cfg.fs, cos_, sin_);
    y_.assign(len_ - 1, cd(0));                      // conv's zero padding before time 0
    y_t0_ = -(len_ - 1);
    c_.offset = cfg.rrc_span * sps_;
}

void Receiver::process(const double* r, size_t n, std::vector<RxSymbol>& out) {
    const auto period = static_cast<int64_t>(cos_.size());
    int64_t ph = t_ % period;
    for (size_t i = 0; i < n; ++i) {
        const double a = 2 * (r[i] - c_.r_mean);
        y_.emplace_back(a * cos_[ph], -a * sin_[ph]);
        if (++ph == period) ph = 0;
    }
    t_ += static_cast<int64_t>(n);

    while (!sym_.empty()) {
        const int64_t m = c_.offset + static_cast<int64_t>(sps_) * static_cast<int64_t>(k_front_);
        if (m >= t_) break;
        const auto [d, idx] = sym_.front();
        sym_.pop_front();
        const uint64_t k = k_front_++;
        if (m - (len_ - 1) < y_t0_) {                // history already gone (timing moved)
            ++skipped_;
            continue;
        }
        const cd* yy = y_.data() + (m - y_t0_);
        double zr = 0, zi = 0;
        for (int j = 0; j < len_; ++j) {
            zr += yy[-j].real() * rrc_[j];
            zi += yy[-j].imag() * rrc_[j];
        }
        out.push_back({c_.gain * cd(zr, zi), d, idx, k});
    }
    // keep the history the next instants need
    const auto keep = static_cast<size_t>(len_ - 1);
    if (y_.size() > keep) {
        const size_t drop = y_.size() - keep;
        y_.erase(y_.begin(), y_.begin() + static_cast<std::ptrdiff_t>(drop));
        y_t0_ += static_cast<int64_t>(drop);
    }
}

}  // namespace arof
