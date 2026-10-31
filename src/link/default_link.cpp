#include "link/default_link.h"

#include "link/mzm.h"

namespace arof {

Link default_link(double rs) {
    Link link;
    link.fiber.lambda = link.lambda_sig;
    link.wireless.f_rf = kC0 / link.lambda_lo - kC0 / link.lambda_sig;
    link.ref.a0 = std::abs(mzm(0, link.mzm));
    link.ref.bw = rs;
    return link;
}

}  // namespace arof
