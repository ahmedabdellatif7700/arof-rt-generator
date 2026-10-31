#include "link/envelope_detector.h"

#include <cmath>

namespace arof {

void EnvelopeDetector::set(const Link::Det& p, double fs) {
    enable_ = p.enable;
    alpha_ = 1 - std::exp(-2 * kPi * p.bw / fs);
}

void EnvelopeDetector::process(const cd* A, size_t n, std::vector<double>& x) {
    x.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const double v = std::norm(A[i]);                 // abs(A).^2
        if (!enable_) {
            x[i] = v;
            continue;
        }
        if (first_) {                                     // zi = (1-alpha)*x(1): y(1) = x(1)
            y_ = v;
            first_ = false;
        }
        y_ = alpha_ * v + (1 - alpha_) * y_;
        x[i] = y_;
    }
}

}  // namespace arof
