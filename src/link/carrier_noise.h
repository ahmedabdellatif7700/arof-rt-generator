// matlab/link/carrier_noise.m: complex AWGN at cnr_db below the unmodulated carrier power
// ref.a0^2, the CNR measured in ref.bw and the noise spread over the simulation bandwidth fs:
//   sigma2 = a0^2 / 10^(cnr_db/10) * fs / bw;   n = sqrt(sigma2/2) * complex(randn, randn)
#pragma once

#include <cmath>

#include "core/rng.h"
#include "link/default_link.h"

namespace arof {

// sqrt(sigma2/2): standard deviation of each component
inline double carrier_noise_sigma(const Link::Ref& ref, double cnr_db, double fs) {
    const double sigma2 = ref.a0 * ref.a0 / std::pow(10.0, cnr_db / 10) * fs / ref.bw;
    return std::sqrt(sigma2 / 2);
}

inline cd carrier_noise(Rng& rng, double sigma) {
    const double re = rng.normal();
    const double im = rng.normal();
    return sigma * cd(re, im);
}

}  // namespace arof
