// matlab/link/photodetector.m: optical heterodyne on the PD, the 60-GHz photocurrent
//   A = 10^((rop_dbm - rop_ref_dbm)/10) * E
// then, if enabled: laser RIN, A .* (1 + sigma*(n1 + n2)/2), sigma = sqrt(10^(dbhz/10)*fs/2);
// PD saturation, Rapp at sat.level*a0; and (p.enable) the PD noise at cnr_db (carrier_noise).
#pragma once

#include <cstdint>

#include "core/rng.h"
#include "link/default_link.h"

namespace arof {

class Photodetector {
public:
    Photodetector(const Link::Pd& p, const Link::Ref& ref, double fs, double rop_dbm, uint64_t seed);
    void set(const Link::Pd& p, const Link::Ref& ref, double fs, double rop_dbm);
    void process(cd* A, size_t n);

private:
    Link::Pd p_;
    double s_, rin_sigma_, asat_, noise_sigma_;
    Rng rin_rng_, noise_rng_;
};

}  // namespace arof
