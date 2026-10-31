// matlab/link/default_link.m: parameters of every link stage, with the same structure and
// names (link.driver.gain, link.pd.rin.dbhz, ...). (*) marks the assumptions of the study.
#pragma once

#include <array>
#include <complex>
#include <limits>

namespace arof {

using cd = std::complex<double>;
constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kPi = 3.14159265358979323846;
constexpr double kC0 = 299792458;

struct Link {
    double lambda_sig = 1552.90e-9;     // modulated DFB laser
    double lambda_lo = 1552.42e-9;      // second DFB laser; the beat is at ~59.7 GHz

    struct Driver {
        bool enable = true;
        double gain = 24;               // * fitted
        double vsat = 3, smooth = 2;    // * Rapp saturation (V), smoothness
    } driver;
    struct Mzm {
        bool enable = true;
        double vpi = 4.4, vbias = 2.2;  // V, quadrature bias
    } mzm;
    struct Fiber {
        bool enable = true;
        double length_m = 15e3;
        double D = 17e-6;               // * 17 ps/(nm km), standard SMF
        double lambda = 1552.90e-9;
    } fiber;
    struct Pd {
        bool enable = true;
        double rop_ref_dbm = -10;       // *
        double cnr_db = 37;             // * lumped RIN/ASE/shot/thermal, fitted
        struct Rin {
            bool enable = false;
            double dbhz = -125;         // * laser RIN, each laser
        } rin;
        struct Sat {
            bool enable = false;
            double level = 1, smooth = 2;   // * PD saturation / a0
        } sat;
    } pd;
    struct Rf {
        bool enable = true;
        double asat = 1.3, smooth = 2;  // * PA saturation / carrier, Rapp
        struct Memory {
            bool enable = false;
            double delay_s = 0.6e-9;    // * PA memory polynomial
            std::array<double, 2> linear{-0.08, 0.03};
            std::array<double, 2> cubic{-0.05, 0.02};
        } memory;
    } rf;
    struct Ripple {
        bool enable = false;
        double rho = 0.2, delay_s = 1e-9, phase_rad = 0;   // * RAU response ripple (echo)
    } ripple;
    struct Wireless {
        bool enable = true;
        double f_rf = kC0 / 1552.42e-9 - kC0 / 1552.90e-9;
        double distance_m = 1;
        double g_tx_dbi = 20, g_rx_dbi = 20;    // horn antennas
        double p_tx_dbm = -11, nf_db = 7;       // * fitted: -11 dBm gives a 42 dB UE CNR
    } wireless;
    struct Det {
        bool enable = true;
        double bw = 1.5e9;              // * IF top edge is 1.15 GHz
    } det;
    struct Adc {
        bool enable = true;
        int bits = 8;                   // * scope resolution
        double clip_std = kInf;         // * limited ADC range; study: 1.2
    } adc;
    struct Ref {
        double a0 = 0;                  // unmodulated carrier amplitude, abs(mzm(0, link.mzm))
        double bw = 0;                  // CNRs are in the symbol-rate band
    } ref;
};

// default_link(rs)
Link default_link(double rs);

}  // namespace arof
