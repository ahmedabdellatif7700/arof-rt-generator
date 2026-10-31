#include "link/fiber.h"

#include <algorithm>
#include <stdexcept>

namespace arof {

Fiber::Fiber(const Link::Fiber& p, double fs, cd idle, int nfft, int half_len)
    : enable_(p.enable), fs_(fs), nfft_(nfft), half_(half_len), overlap_(2 * half_len),
      step_(nfft - 2 * half_len), to_drop_(half_len) {
    if (!enable_) return;
    if (step_ <= 0) throw std::invalid_argument("fiber: nfft too small for the overlap");
    buf_ = fftw_alloc_complex(nfft_);
    fwd_ = fftw_plan_dft_1d(nfft_, buf_, buf_, FFTW_FORWARD, FFTW_MEASURE);
    inv_ = fftw_plan_dft_1d(nfft_, buf_, buf_, FFTW_BACKWARD, FFTW_MEASURE);
    set(p);
    in_.assign(overlap_, idle);
}

Fiber::~Fiber() {
    if (fwd_) fftw_destroy_plan(fwd_);
    if (inv_) fftw_destroy_plan(inv_);
    if (buf_) fftw_free(buf_);
}

void Fiber::set(const Link::Fiber& p) {
    if (!enable_) return;
    // exp(1j*beta2/2*(2*pi*f).^2*length_m) on the FFT grid, f >= fs/2 wrapped to negative
    const double beta2 = -p.D * p.lambda * p.lambda / (2 * kPi * kC0);
    auto* c = reinterpret_cast<cd*>(buf_);
    for (int k = 0; k < nfft_; ++k) {
        double f = static_cast<double>(k) / nfft_ * fs_;
        if (f >= fs_ / 2) f -= fs_;
        const double w = 2 * kPi * f;
        c[k] = std::exp(cd(0, beta2 / 2 * w * w * p.length_m));
    }
    fftw_execute(inv_);                                   // impulse response * nfft
    std::vector<cd> h(nfft_);
    double e_all = 0, e_in = 0;
    for (int k = 0; k < nfft_; ++k) {
        h[k] = c[k] / static_cast<double>(nfft_);
        e_all += std::norm(h[k]);
    }
    // truncate to taps -half..half, delayed by half: a causal FIR of 2*half+1 taps
    std::fill(c, c + nfft_, cd(0));
    for (int j = 0; j <= 2 * half_; ++j) {
        const cd tap = h[(j - half_ + nfft_) % nfft_];
        c[j] = tap;
        e_in += std::norm(tap);
    }
    trunc_err_ = std::max(0.0, 1 - e_in / e_all);
    fftw_execute(fwd_);
    H_.assign(c, c + nfft_);
    for (auto& z : H_) z /= static_cast<double>(nfft_);   // FFTW's inverse is unnormalized
}

void Fiber::run_block(std::vector<cd>& out) {
    auto* c = reinterpret_cast<cd*>(buf_);
    std::copy(in_.begin(), in_.begin() + nfft_, c);
    fftw_execute(fwd_);
    for (int k = 0; k < nfft_; ++k) c[k] *= H_[k];
    fftw_execute(inv_);
    // valid outputs at positions overlap..nfft-1; the first half_ ever precede time 0
    int first = overlap_;
    if (to_drop_ > 0) {
        const auto drop = static_cast<int>(std::min<uint64_t>(to_drop_, step_));
        first += drop;
        to_drop_ -= drop;
    }
    out.insert(out.end(), c + first, c + nfft_);
    in_.erase(in_.begin(), in_.begin() + step_);
}

void Fiber::process(const cd* in, size_t n, std::vector<cd>& out) {
    if (!enable_) {
        out.insert(out.end(), in, in + n);
        return;
    }
    in_.insert(in_.end(), in, in + n);
    while (in_.size() >= static_cast<size_t>(nfft_)) run_block(out);
}

}  // namespace arof
