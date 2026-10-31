// The MATLAB Communications / Signal Processing Toolbox functions the link uses.
#pragma once

#include <array>
#include <complex>
#include <cstdint>
#include <vector>

namespace arof {

using cd = std::complex<double>;

// qammod(0:15, 16, 'gray', 'UnitAveragePower', true)
const std::array<cd, 16>& qam16_gray();
// qamdemod(y, 16, 'gray', 'UnitAveragePower', true): Gray label of the nearest point
uint8_t qamdemod16(cd y);

// rcosdesign(beta, span, sps, 'sqrt'): span*sps + 1 taps, unit energy
std::vector<double> rcosdesign_sqrt(double beta, int span, int sps);

// Period in samples of exp(1j*2*pi*f*n/fs) (25 for 0.8 GHz at 20 GS/s); throws if there is
// none up to 10000.
int tone_period(double f, double fs);
// cos and sin of 2*pi*f*n/fs over one period
void tone_table(double f, double fs, std::vector<double>& c, std::vector<double>& s);

}  // namespace arof
