// The settings of main_demo.m section 0 (operating point and impairment switches, with their
// "off" values) and how they act on the link: as the apply functions of
// matlab/core/impairment_sweeps.m, which main_demo.m writes out inline.
#pragma once

#include "link/default_link.h"

namespace arof {

struct LinkSettings {
    double vpp_mv = 300;            // AWG peak-to-peak drive (mV)
    double rop_dbm = -11.75;        // received optical power at the PD (model, dBm)
    double fiber_m = 15e3;          // fiber length
    double bias_drift = 0;          // MZM bias offset (x Vpi), e.g. 0.1
    double rin_dbhz = -kInf;        // laser RIN per laser (dB/Hz), e.g. -130
    double pd_sat = kInf;           // PD saturation level (x carrier), e.g. 1
    double pa_memory = 0;           // PA memory polynomial strength, e.g. 1
    double ripple_rho = 0;          // RAU echo amplitude at 1 ns, e.g. 0.2
    double adc_clip = kInf;         // ADC full scale (x std), e.g. 1.2
};

// base (default_link with any stage overrides) with the switches applied
Link apply_link_settings(Link base, const LinkSettings& s);

}  // namespace arof
