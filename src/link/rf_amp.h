// matlab/link/rf_amp.m: RAU amplifier, AGC back to the carrier reference, then the PA's
// Rapp AM/AM at asat*a0 (p.enable), plus the optional memory polynomial (p.memory.enable)
//   A -> Rapp(A) + a0 * sum_m (linear(m)*u_m + cubic(m)*u_m.*|u_m|^2),  u = A_in/a0
// with u_m delayed by m*round(delay_s*fs). The delay line keeps its state across blocks.
#pragma once

#include <vector>

#include "link/default_link.h"

namespace arof {

class RfAmp {
public:
    RfAmp(const Link::Rf& p, const Link::Ref& ref, double rop_dbm, double rop_ref_dbm, double fs);
    void set(const Link::Rf& p, const Link::Ref& ref, double rop_dbm, double rop_ref_dbm, double fs);
    void process(cd* A, size_t n);

private:
    Link::Rf p_;
    double agc_, a0_, asat_;
    int D_ = 0;
    std::vector<cd> u_;                     // last numel(linear)*D values of u
    size_t pos_ = 0;
};

}  // namespace arof
