// Rapp AM/AM compression of a complex envelope, as written in photodetector.m and rf_amp.m:
//   A ./ max(r, eps) .* (r ./ (1 + (r/asat).^(2*s)).^(1/(2*s))),   r = abs(A)
#pragma once

#include <algorithm>
#include <cfloat>
#include <cmath>

#include "link/default_link.h"

namespace arof {

inline cd rapp(cd a, double asat, double s) {
    const double m2 = std::norm(a);
    const double m = std::sqrt(m2);
    double den;
    if (s == 2) {
        const double q = m2 / (asat * asat);
        den = std::sqrt(std::sqrt(1 + q * q));
    } else {
        den = std::pow(1 + std::pow(m / asat, 2 * s), 1 / (2 * s));
    }
    return a / std::max(m, DBL_EPSILON) * (m / den);
}

}  // namespace arof
