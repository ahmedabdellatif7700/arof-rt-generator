// matlab/link/adc.m: scope quantization, bits over the waveform's range, after the optional
// clipping at mean -+ clip_std*std. Streaming: adc.m takes mean, std, min and max of the whole
// waveform; here they are constants from a calibration block (a fixed full scale), and samples
// outside the calibrated range saturate at the end codes, as a real ADC.
#pragma once

#include <vector>

#include "link/default_link.h"

namespace arof {

struct AdcConstants {
    double clip_lo = -kInf, clip_hi = kInf;   // m -+ clip_std*s (no clipping when clip_std = inf)
    double lo = 0, step = 1;                  // lo = min(x), step = (max(x) - lo)/(2^bits - 1)
    int levels = 256;
};

// adc.m's rules on the calibration block
AdcConstants adc_calibrate(const double* x, size_t n, const Link::Adc& p);
// adc.m with the frozen constants, in place
void adc(double* x, size_t n, const AdcConstants& c, const Link::Adc& p);

}  // namespace arof
