// matlab/link/envelope_detector.m: square-law detector, x = |A|^2, then (p.enable) a
// first-order low-pass at bw: filter(alpha, [1, alpha-1], x, (1-alpha)*x(1)).
// The very first sample uses MATLAB's initial condition; afterwards the state carries over.
#pragma once

#include <vector>

#include "link/default_link.h"

namespace arof {

class EnvelopeDetector {
public:
    EnvelopeDetector(const Link::Det& p, double fs) { set(p, fs); }
    void set(const Link::Det& p, double fs);
    void process(const cd* A, size_t n, std::vector<double>& x);

private:
    bool enable_;
    double alpha_, y_ = 0;
    bool first_ = true;
};

}  // namespace arof
