#include "core/comm.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <stdexcept>

namespace arof {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

const std::array<cd, 16>& qam16_gray() {
    // qammod's Gray map on the integer grid (checked against tx_indices and Y of
    // arof_training_data.mat), divided by sqrt(10)
    static const std::array<cd, 16> pts = [] {
        const int g[16][2] = {{-3, 3}, {-3, 1}, {-3, -3}, {-3, -1}, {-1, 3}, {-1, 1},
                              {-1, -3}, {-1, -1}, {3, 3},  {3, 1},   {3, -3},  {3, -1},
                              {1, 3},   {1, 1},   {1, -3},  {1, -1}};
        std::array<cd, 16> p{};
        for (int i = 0; i < 16; ++i) p[i] = cd(g[i][0], g[i][1]) / std::sqrt(10.0);
        return p;
    }();
    return pts;
}

uint8_t qamdemod16(cd y) {
    // nearest level per axis, then its Gray label
    static const uint8_t i_bits[4] = {0, 1, 3, 2};   // I = -3, -1, 1, 3 -> label >> 2
    static const uint8_t q_bits[4] = {2, 3, 1, 0};   // Q = -3, -1, 1, 3 -> label & 3
    auto level = [](double v) {
        const double l = std::round((v * std::sqrt(10.0) + 3) / 2);
        return static_cast<int>(std::clamp(l, 0.0, 3.0));
    };
    return static_cast<uint8_t>(i_bits[level(y.real())] << 2 | q_bits[level(y.imag())]);
}

std::vector<double> rcosdesign_sqrt(double beta, int span, int sps) {
    const int n = span * sps + 1;
    const double delay = span * sps / 2.0;
    const double sqrt_eps = std::sqrt(DBL_EPSILON);
    std::vector<double> b(n);
    for (int i = 0; i < n; ++i) {
        const double t = (i - delay) / sps;
        if (t == 0) {
            b[i] = -1 / (kPi * sps) * (kPi * (beta - 1) - 4 * beta);
        } else if (std::fabs(std::fabs(4 * beta * t) - 1.0) < sqrt_eps) {
            b[i] = 1 / (2 * kPi * sps) *
                   (kPi * (beta + 1) * std::sin(kPi * (beta + 1) / (4 * beta)) -
                    4 * beta * std::sin(kPi * (beta - 1) / (4 * beta)) +
                    kPi * (beta - 1) * std::cos(kPi * (beta - 1) / (4 * beta)));
        } else {
            b[i] = -4 * beta / sps *
                   (std::cos((1 + beta) * kPi * t) + std::sin((1 - beta) * kPi * t) / (4 * beta * t)) /
                   (kPi * ((4 * beta * t) * (4 * beta * t) - 1));
        }
    }
    double e = 0;
    for (double v : b) e += v * v;
    for (double& v : b) v /= std::sqrt(e);
    return b;
}

int tone_period(double f, double fs) {
    const double r = f / fs;
    for (int p = 1; p <= 10000; ++p)
        if (std::fabs(r * p - std::round(r * p)) < 1e-9) return p;
    throw std::invalid_argument("f_if/fs has no period up to 10000 samples");
}

void tone_table(double f, double fs, std::vector<double>& c, std::vector<double>& s) {
    const int period = tone_period(f, fs);
    c.resize(period);
    s.resize(period);
    for (int i = 0; i < period; ++i) {
        const double ph = 2 * kPi * std::fmod(f / fs * i, 1.0);
        c[i] = std::cos(ph);
        s[i] = std::sin(ph);
    }
}

}  // namespace arof
