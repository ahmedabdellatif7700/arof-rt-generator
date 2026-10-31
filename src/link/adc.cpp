#include "link/adc.h"

#include <algorithm>
#include <cmath>

namespace arof {

AdcConstants adc_calibrate(const double* x, size_t n, const Link::Adc& p) {
    AdcConstants c;
    c.levels = 1 << p.bits;
    double m = 0, mn = kInf, mx = -kInf;
    for (size_t i = 0; i < n; ++i) {
        m += x[i];
        mn = std::min(mn, x[i]);
        mx = std::max(mx, x[i]);
    }
    m /= static_cast<double>(n);
    if (std::isfinite(p.clip_std)) {
        double ss = 0;
        for (size_t i = 0; i < n; ++i) ss += (x[i] - m) * (x[i] - m);
        const double s = std::sqrt(ss / static_cast<double>(n - 1));   // std(x)
        c.clip_lo = m - p.clip_std * s;
        c.clip_hi = m + p.clip_std * s;
        mn = std::min(std::max(mn, c.clip_lo), c.clip_hi);             // min of the clipped x
        mx = std::min(std::max(mx, c.clip_lo), c.clip_hi);
    }
    c.lo = mn;
    c.step = (mx - mn) / (c.levels - 1);
    return c;
}

void adc(double* x, size_t n, const AdcConstants& c, const Link::Adc& p) {
    const double top = c.levels - 1;
    for (size_t i = 0; i < n; ++i) {
        double v = std::min(std::max(x[i], c.clip_lo), c.clip_hi);
        if (p.enable)                                                   // lo + step*round((x - lo)/step)
            v = c.lo + c.step * std::clamp(std::round((v - c.lo) / c.step), 0.0, top);
        x[i] = v;
    }
}

}  // namespace arof
