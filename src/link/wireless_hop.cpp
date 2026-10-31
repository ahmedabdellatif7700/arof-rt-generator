#include "link/wireless_hop.h"

#include <cmath>

#include "link/carrier_noise.h"

namespace arof {

WirelessHop::WirelessHop(const Link::Wireless& p, const Link::Ref& ref, double fs, uint64_t seed)
    : rng_(seed, Stream::ue_noise) {
    set(p, ref, fs);
}

void WirelessHop::set(const Link::Wireless& p, const Link::Ref& ref, double fs) {
    enable_ = p.enable;
    const double fspl_db = 20 * std::log10(4 * kPi * p.distance_m * p.f_rf / kC0);
    const double noise_dbm = 10 * std::log10(1.380649e-23 * 290 * 1e3) + 10 * std::log10(ref.bw) + p.nf_db;
    cnr_db_ = p.p_tx_dbm + p.g_tx_dbi + p.g_rx_dbi - fspl_db - noise_dbm;
    sigma_ = carrier_noise_sigma(ref, cnr_db_, fs);
}

void WirelessHop::process(cd* A, size_t n) {
    if (!enable_) return;
    for (size_t i = 0; i < n; ++i) A[i] += carrier_noise(rng_, sigma_);
}

}  // namespace arof
