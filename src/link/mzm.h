// matlab/link/mzm.m: single-arm Mach-Zehnder modulator, optical field of the modulated laser:
//   E = (1 + exp(j*theta)) / 2,   theta = pi*(vbias + vd)/vpi
// Disabled: an ideal linear intensity modulator with the same slope at the bias.
#pragma once

#include <algorithm>
#include <cmath>

#include "link/default_link.h"

namespace arof {

inline cd mzm(double vd, const Link::Mzm& p) {
    if (!p.enable) return cd(std::sqrt(std::max((1 - kPi * vd / p.vpi) / 2, 0.0)), 0);
    const double th = kPi * (p.vbias + vd) / p.vpi;
    return cd((1 + std::cos(th)) / 2, std::sin(th) / 2);
}

}  // namespace arof
