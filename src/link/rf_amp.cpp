#include "link/rf_amp.h"

#include <cmath>

#include "link/rapp.h"

namespace arof {

RfAmp::RfAmp(const Link::Rf& p, const Link::Ref& ref, double rop_dbm, double rop_ref_dbm, double fs) {
    set(p, ref, rop_dbm, rop_ref_dbm, fs);
}

void RfAmp::set(const Link::Rf& p, const Link::Ref& ref, double rop_dbm, double rop_ref_dbm, double fs) {
    p_ = p;
    agc_ = std::pow(10.0, (rop_dbm - rop_ref_dbm) / 10);
    a0_ = ref.a0;
    asat_ = p.asat * ref.a0;
    const int D = static_cast<int>(std::lround(p.memory.delay_s * fs));
    const size_t len = p.memory.linear.size() * static_cast<size_t>(D);
    if (D != D_ || u_.size() != len) {                    // zeros before the first sample
        u_.assign(len, cd(0));
        pos_ = 0;
        D_ = D;
    }
}

void RfAmp::process(cd* A, size_t n) {
    const size_t len = u_.size();
    const size_t terms = p_.memory.linear.size();
    for (size_t i = 0; i < n; ++i) {
        const cd a_in = A[i] / agc_;
        cd a = p_.enable ? rapp(a_in, asat_, p_.smooth) : a_in;
        if (len > 0) {
            const cd u = a_in / a0_;
            if (p_.memory.enable) {
                // u_ is a ring of the last len values; u_[pos_] is u(n - len)
                for (size_t m = 1; m <= terms; ++m) {
                    const cd um = u_[(pos_ + len - m * D_) % len];   // u(n - m*D)
                    a += a0_ * (p_.memory.linear[m - 1] * um + p_.memory.cubic[m - 1] * um * std::norm(um));
                }
            }
            u_[pos_] = u;
            if (++pos_ == len) pos_ = 0;
        }
        A[i] = a;
    }
}

}  // namespace arof
