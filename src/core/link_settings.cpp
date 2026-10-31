#include "core/link_settings.h"

#include <cmath>

namespace arof {

Link apply_link_settings(Link l, const LinkSettings& s) {
    l.fiber.length_m = s.fiber_m;                              // fiber_len
    l.mzm.vbias = l.mzm.vbias + s.bias_drift * l.mzm.vpi;      // bias_drift
    if (s.pa_memory > 0) {                                     // pa_memory
        l.rf.memory.enable = true;
        for (auto& b : l.rf.memory.linear) b *= s.pa_memory;
        for (auto& b : l.rf.memory.cubic) b *= s.pa_memory;
    }
    if (s.ripple_rho > 0) {                                    // ripple
        l.ripple.enable = true;
        l.ripple.rho = s.ripple_rho;
    }
    if (std::isfinite(s.pd_sat)) {                             // pd_sat
        l.pd.sat.enable = true;
        l.pd.sat.level = s.pd_sat;
    }
    l.adc.clip_std = s.adc_clip;                               // adc_clip
    if (std::isfinite(s.rin_dbhz)) {                           // rin
        l.pd.rin.enable = true;
        l.pd.rin.dbhz = s.rin_dbhz;
    }
    return l;
}

}  // namespace arof
