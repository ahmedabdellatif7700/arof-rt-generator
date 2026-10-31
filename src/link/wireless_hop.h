// matlab/link/wireless_hop.m: 60-GHz line-of-sight hop and the UE's LNA. The carrier leaves
// the RAU at p_tx_dbm, so the CNR at the UE is
//   CNR = p_tx + g_tx + g_rx - FSPL - (kT [dBm/Hz] + 10*log10(bw) + NF)
// and the LNA restores the level. Disabled: no noise.
#pragma once

#include <cstdint>

#include "core/rng.h"
#include "link/default_link.h"

namespace arof {

class WirelessHop {
public:
    WirelessHop(const Link::Wireless& p, const Link::Ref& ref, double fs, uint64_t seed);
    void set(const Link::Wireless& p, const Link::Ref& ref, double fs);
    void process(cd* A, size_t n);
    double cnr_db() const { return cnr_db_; }

private:
    bool enable_;
    double cnr_db_, sigma_;
    Rng rng_;
};

}  // namespace arof
