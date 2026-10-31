#include "link/photodetector.h"

#include <cmath>

#include "link/carrier_noise.h"
#include "link/rapp.h"

namespace arof {

Photodetector::Photodetector(const Link::Pd& p, const Link::Ref& ref, double fs, double rop_dbm,
                             uint64_t seed)
    : rin_rng_(seed, Stream::rin), noise_rng_(seed, Stream::pd_noise) {
    set(p, ref, fs, rop_dbm);
}

void Photodetector::set(const Link::Pd& p, const Link::Ref& ref, double fs, double rop_dbm) {
    p_ = p;
    s_ = std::pow(10.0, (rop_dbm - p.rop_ref_dbm) / 10);
    rin_sigma_ = std::sqrt(std::pow(10.0, p.rin.dbhz / 10) * fs / 2);
    asat_ = p.sat.level * ref.a0;
    noise_sigma_ = carrier_noise_sigma(ref, p.cnr_db, fs);
}

void Photodetector::process(cd* A, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        cd a = s_ * A[i];
        if (p_.rin.enable) {
            const double n1 = rin_rng_.normal(), n2 = rin_rng_.normal();
            a *= 1 + rin_sigma_ * (n1 + n2) / 2;
        }
        if (p_.sat.enable) a = rapp(a, asat_, p_.sat.smooth);
        if (p_.enable) a += carrier_noise(noise_rng_, noise_sigma_);
        A[i] = a;
    }
}

}  // namespace arof
