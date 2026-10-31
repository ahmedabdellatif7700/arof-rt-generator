// matlab/link/rf_response.m: RAU amplitude/phase ripple as a weak echo on the envelope,
//   A(t) -> A(t) + rho * exp(j*phase_rad) * A(t - delay_s)
// Disabled (the default): no ripple. The delay line keeps its state across blocks.
#pragma once

#include <vector>

#include "link/default_link.h"

namespace arof {

class RfResponse {
public:
    RfResponse(const Link::Ripple& p, double fs) { set(p, fs); }
    void set(const Link::Ripple& p, double fs);
    void process(cd* A, size_t n);

private:
    bool enable_ = false;
    cd echo_;
    std::vector<cd> hist_;                  // last D input samples
    size_t pos_ = 0;
};

}  // namespace arof
