// matlab/link/driver_amp.m: electrical driver amplifier before the MZM. Gain with Rapp
// output saturation:  vd = G*v / (1 + |G*v/vsat|^(2s))^(1/(2s)). Disabled: linear gain.
#pragma once

#include <cmath>

#include "link/default_link.h"

namespace arof {

inline double driver_amp(double v, const Link::Driver& p) {
    const double g = p.gain * v;
    if (!p.enable) return g;
    if (p.smooth == 2) {                          // (1 + u^4)^(1/4) as sqrt(sqrt())
        const double u = (g / p.vsat) * (g / p.vsat);
        return g / std::sqrt(std::sqrt(1 + u * u));
    }
    return g / std::pow(1 + std::pow(std::fabs(g / p.vsat), 2 * p.smooth), 1 / (2 * p.smooth));
}

}  // namespace arof
