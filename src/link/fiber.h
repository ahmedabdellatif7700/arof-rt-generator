// matlab/link/fiber.m: chromatic dispersion on the modulated field,
//   E(f) * exp(j*beta2/2*(2*pi*f)^2*L),   beta2 = -D*lambda^2 / (2*pi*c)
// Streaming: fiber.m filters the whole sequence with one FFT; here an overlap-save FFT
// (nfft 8192, overlap 256) with the impulse response truncated to +-128 samples. Against the
// one-FFT result this leaves ~2e-5 of the signal rms at 15 km (~1.3e-4 at 100 km), far below
// the PD noise. The stage has no net delay: it holds its output until the samples are final.
// Disabled: no dispersion.
#pragma once

#include <cstdint>
#include <vector>

#include <fftw3.h>

#include "link/default_link.h"

namespace arof {

class Fiber {
public:
    // idle: the field before the first sample (the unmodulated carrier)
    Fiber(const Link::Fiber& p, double fs, cd idle, int nfft = 8192, int half_len = 128);
    ~Fiber();
    Fiber(const Fiber&) = delete;
    Fiber& operator=(const Fiber&) = delete;

    // new length (live change); the pending samples are kept
    void set(const Link::Fiber& p);
    // appends the output samples that became final (the count varies from call to call)
    void process(const cd* in, size_t n, std::vector<cd>& out);
    // relative energy of the impulse response outside +-half_len
    double truncation_error() const { return trunc_err_; }
    // the most samples held back at any time
    int max_holdback() const { return enable_ ? nfft_ : 0; }

private:
    void run_block(std::vector<cd>& out);
    bool enable_;
    double fs_;
    int nfft_, half_, overlap_, step_;
    std::vector<cd> H_;                     // FFT of the truncated, delayed response / nfft
    fftw_complex* buf_ = nullptr;
    fftw_plan fwd_ = nullptr, inv_ = nullptr;
    std::vector<cd> in_;                    // overlap history + pending new samples
    uint64_t to_drop_;                      // outputs before time 0
    double trunc_err_ = 0;
};

}  // namespace arof
