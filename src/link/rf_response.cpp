#include "link/rf_response.h"

#include <cmath>

namespace arof {

void RfResponse::set(const Link::Ripple& p, double fs) {
    enable_ = p.enable;
    echo_ = p.rho * std::exp(cd(0, p.phase_rad));
    const auto D = static_cast<size_t>(std::lround(p.delay_s * fs));
    if (hist_.size() != D) {                  // zeros before the first sample
        hist_.assign(D, cd(0));
        pos_ = 0;
    }
}

void RfResponse::process(cd* A, size_t n) {
    const size_t D = hist_.size();
    if (D == 0) {
        if (enable_)
            for (size_t i = 0; i < n; ++i) A[i] += echo_ * A[i];
        return;
    }
    for (size_t i = 0; i < n; ++i) {
        const cd a = A[i];
        if (enable_) A[i] = a + echo_ * hist_[pos_];      // hist_[pos_] is A(t - D)
        hist_[pos_] = a;
        if (++pos_ == D) pos_ = 0;
    }
}

}  // namespace arof
