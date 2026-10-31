// Each module of src/link against the formula of its matlab/link/*.m file, written out over the
// whole sequence, and across block boundaries. Includes the disabled behavior of every stage.
#include <algorithm>
#include <cfloat>

#include "check.h"
#include "core/comm.h"
#include "link/adc.h"
#include "link/carrier_noise.h"
#include "link/default_link.h"
#include "link/default_tx.h"
#include "link/driver_amp.h"
#include "link/envelope_detector.h"
#include "link/fiber.h"
#include "link/mzm.h"
#include "link/photodetector.h"
#include "link/rf_amp.h"
#include "link/rf_response.h"
#include "link/transmitter.h"
#include "link/wireless_hop.h"

using namespace arof;

namespace {

const TxConfig kTx = default_tx();
const Link kLink = default_link(kTx.rs);

// A ./ max(r, eps) .* (r ./ (1 + (r/asat).^(2*s)).^(1/(2*s)))
cd rapp_ref(cd a, double asat, double s) {
    const double r = std::abs(a);
    return a / std::max(r, DBL_EPSILON) * (r / std::pow(1 + std::pow(r / asat, 2 * s), 1 / (2 * s)));
}

double max_abs_diff(const std::vector<cd>& a, const std::vector<cd>& b, size_t from, size_t to) {
    double m = 0;
    for (size_t i = from; i < to; ++i) m = std::max(m, std::abs(a[i] - b[i]));
    return m;
}

std::vector<cd> test_envelope(int n) {
    std::vector<cd> A(n);
    for (int i = 0; i < n; ++i) A[i] = std::polar(0.6 + 0.4 * std::sin(0.05 * i), 0.07 * i);
    return A;
}

}  // namespace

TEST(default_link_values) {
    CHECK_NEAR(kLink.ref.a0, std::abs((1.0 + std::exp(cd(0, kPi * 2.2 / 4.4))) / 2.0), 1e-15);
    CHECK(kLink.ref.bw == 0.4e9);
    CHECK_NEAR(kLink.wireless.f_rf / 1e9, 59.7, 0.05);
    CHECK(kLink.fiber.lambda == kLink.lambda_sig);
    CHECK(!kLink.pd.rin.enable && !kLink.pd.sat.enable && !kLink.rf.memory.enable && !kLink.ripple.enable);
    CHECK(std::isinf(kLink.adc.clip_std));
}

TEST(rcosdesign_sqrt_shape) {
    const auto h = rcosdesign_sqrt(0.75, 16, 50);
    CHECK(h.size() == 801);
    double e = 0;
    for (double v : h) e += v * v;
    CHECK_NEAR(e, 1, 1e-12);
    for (size_t i = 0; i < h.size(); ++i) CHECK_NEAR(h[i], h[h.size() - 1 - i], 1e-15);
    // before normalization: peak -1/(pi*sps)*(pi*(beta-1) - 4*beta); the general formula at t = -1.5
    const double beta = 0.75, sps = 50, t = -1.5;
    const double peak = -1 / (kPi * sps) * (kPi * (beta - 1) - 4 * beta);
    const double b = -4 * beta / sps *
                     (std::cos((1 + beta) * kPi * t) + std::sin((1 - beta) * kPi * t) / (4 * beta * t)) /
                     (kPi * ((4 * beta * t) * (4 * beta * t) - 1));
    CHECK_NEAR(h[400] / h[325], peak / b, 1e-12);
    // rrc * rrc every sps: a delta, up to the truncation of the 16-symbol span
    const int n = static_cast<int>(h.size());
    std::vector<double> c(2 * n - 1, 0);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) c[i + j] += h[i] * h[j];
    CHECK_NEAR(c[n - 1], 1, 1e-12);
    double isi = 0;
    for (int k = 1; n - 1 + 50 * k < 2 * n - 1; ++k) isi = std::max(isi, std::fabs(c[n - 1 + 50 * k]));
    CHECK(isi < 2e-3);
}

TEST(qammod_qamdemod_gray) {
    const auto& q = qam16_gray();
    const int ref[16][2] = {{-3, 3}, {-3, 1}, {-3, -3}, {-3, -1}, {-1, 3}, {-1, 1}, {-1, -3}, {-1, -1},
                            {3, 3},  {3, 1},   {3, -3},  {3, -1},  {1, 3},  {1, 1},  {1, -3},  {1, -1}};
    double pw = 0;
    for (int i = 0; i < 16; ++i) {
        CHECK_NEAR(q[i].real() * std::sqrt(10.0), ref[i][0], 1e-12);
        CHECK_NEAR(q[i].imag() * std::sqrt(10.0), ref[i][1], 1e-12);
        pw += std::norm(q[i]) / 16;
        CHECK(qamdemod16(q[i]) == i);
        CHECK(qamdemod16(q[i] + cd(0.3, -0.3) / std::sqrt(10.0)) == i);
    }
    CHECK_NEAR(pw, 1, 1e-12);
    for (int i = 0; i < 16; ++i)                     // Gray: neighbours differ in one bit
        for (int j = 0; j < 16; ++j)
            if (std::abs(std::abs(q[i] - q[j]) - 2 / std::sqrt(10.0)) < 1e-9) CHECK(__builtin_popcount(i ^ j) == 1);
}

TEST(transmitter_matches_upfirdn) {
    const int sps = kTx.sps();
    std::vector<uint8_t> idx(300);
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = static_cast<uint8_t>((i * 7 + i / 3) % 16);
    Transmitter tx(kTx, 1);
    std::vector<double> v;
    size_t pos = 0;
    for (size_t b : {1, 2, 50, 7, 240}) {             // uneven blocks
        tx.process_indices(idx.data() + pos, b, v);
        pos += b;
    }
    CHECK(v.size() == idx.size() * sps);
    const auto h = rcosdesign_sqrt(kTx.rolloff, kTx.rrc_span, sps);
    const auto& q = qam16_gray();
    double err = 0;
    for (size_t n = 0; n < v.size(); ++n) {           // s = upfirdn(d, rrc, sps)
        cd s = 0;
        for (size_t k = 0; k < idx.size(); ++k) {
            const long m = static_cast<long>(n) - static_cast<long>(k) * sps;
            if (m >= 0 && m < static_cast<long>(h.size())) s += q[idx[k]] * h[m];
        }
        const double t = n / kTx.fs;                  // v = real(s .* exp(1j*2*pi*f_if*t))
        err = std::max(err, std::fabs(v[n] - std::real(s * std::exp(cd(0, 2 * kPi * kTx.f_if * t)))));
    }
    CHECK(err < 1e-12);
}

TEST(driver_amp_and_mzm) {
    Link::Driver drv = kLink.driver;
    Link::Mzm mz = kLink.mzm;
    mz.vbias += 0.05 * mz.vpi;
    for (int i = -100; i <= 100; ++i) {
        const double v = i * 0.004;
        const double g = drv.gain * v;
        const double vd = g / std::pow(1 + std::pow(std::fabs(g / drv.vsat), 2 * drv.smooth), 1 / (2 * drv.smooth));
        CHECK_NEAR(driver_amp(v, drv), vd, 1e-14);
        const cd E = (1.0 + std::exp(cd(0, kPi * (mz.vbias + vd) / mz.vpi))) / 2.0;
        CHECK(std::abs(mzm(vd, mz) - E) < 1e-15);
    }
    drv.enable = false;                               // disabled: linear gain
    CHECK(driver_amp(0.3, drv) == drv.gain * 0.3);
    mz.enable = false;                                // disabled: sqrt(max((1 - pi*vd/vpi)/2, 0))
    CHECK_NEAR(mzm(0.5, mz).real(), std::sqrt((1 - kPi * 0.5 / mz.vpi) / 2), 1e-15);
    CHECK(mzm(0.5, mz).imag() == 0);
    CHECK(mzm(10, mz) == cd(0));
}

TEST(fiber_matches_one_fft) {
    // fiber.m: ifft(fft(E) .* exp(1j*beta2/2*(2*pi*f).^2*length_m)) over the whole sequence
    for (double L : {15e3, 100e3}) {
        Link::Fiber p = kLink.fiber;
        p.length_m = L;
        Transmitter tx(kTx, 5);                       // the real field at the MZM output
        std::vector<uint8_t> idx;
        std::vector<cd> d, E;
        std::vector<double> v;
        tx.process(800, idx, d, v);
        for (double x : v) E.push_back(mzm(driver_amp(0.556 * x, kLink.driver), kLink.mzm));
        const int n = static_cast<int>(E.size());

        fftw_complex* b = fftw_alloc_complex(n);
        fftw_plan f = fftw_plan_dft_1d(n, b, b, FFTW_FORWARD, FFTW_ESTIMATE);
        fftw_plan g = fftw_plan_dft_1d(n, b, b, FFTW_BACKWARD, FFTW_ESTIMATE);
        auto* c = reinterpret_cast<cd*>(b);
        std::copy(E.begin(), E.end(), c);
        fftw_execute(f);
        const double beta2 = -p.D * p.lambda * p.lambda / (2 * kPi * kC0);
        for (int k = 0; k < n; ++k) {
            double fr = static_cast<double>(k) / n * kTx.fs;
            if (fr >= kTx.fs / 2) fr -= kTx.fs;
            const double w = 2 * kPi * fr;
            c[k] *= std::exp(cd(0, beta2 / 2 * w * w * L)) / static_cast<double>(n);
        }
        fftw_execute(g);
        std::vector<cd> ref(c, c + n);
        fftw_destroy_plan(f);
        fftw_destroy_plan(g);
        fftw_free(b);

        Fiber fib(p, kTx.fs, E[0]);
        std::vector<cd> out;
        for (int pos = 0; pos < n;) {                 // uneven blocks
            const int m = std::min(n - pos, 333 + pos % 1000);
            fib.process(E.data() + pos, m, out);
            pos += m;
        }
        CHECK(out.size() > 30000);
        double rms = 0;
        for (const auto& e : E) rms += std::norm(e - E[0]) / n;
        rms = std::sqrt(rms);
        // away from the ends, where the one FFT wraps around. The +-128-tap window leaves ~2e-5
        // (15 km) and ~1.3e-4 (100 km) of the signal rms; the PD noise is ~2e-2.
        const double err = max_abs_diff(out, ref, 500, std::min<size_t>(out.size(), n - 500));
        std::printf("    fiber %g km: max error %.2e (signal rms %.2e), truncation %.2e\n", L / 1e3, err, rms,
                    fib.truncation_error());
        CHECK(err < (L < 50e3 ? 5e-5 : 3e-4) * rms);
    }
}

TEST(fiber_zero_length_and_disabled) {
    std::vector<cd> E(20000);
    for (size_t i = 0; i < E.size(); ++i) E[i] = cd(std::sin(i * 0.01), std::cos(i * 0.003));
    Link::Fiber p = kLink.fiber;
    p.length_m = 0;
    Fiber zero(p, kTx.fs, E[0]);
    std::vector<cd> out;
    zero.process(E.data(), E.size(), out);
    CHECK(max_abs_diff(out, E, 0, out.size()) < 1e-12);
    p.enable = false;
    Fiber off(p, kTx.fs, E[0]);
    out.clear();
    off.process(E.data(), E.size(), out);
    CHECK(out.size() == E.size() && max_abs_diff(out, E, 0, E.size()) == 0);
}

TEST(photodetector_noise_level) {
    Photodetector pd(kLink.pd, kLink.ref, kTx.fs, kLink.pd.rop_ref_dbm, 3);   // s = 1
    std::vector<cd> A(400000, cd(0));
    pd.process(A.data(), A.size());
    double s = 0;
    for (auto a : A) s += std::norm(a);
    // carrier_noise: sigma2 = a0^2 / 10^(cnr_db/10) * fs / bw, complex
    const double sigma2 = kLink.ref.a0 * kLink.ref.a0 / std::pow(10, kLink.pd.cnr_db / 10) * kTx.fs / kLink.ref.bw;
    CHECK_NEAR(s / A.size() / sigma2, 1, 0.01);
}

TEST(photodetector_scaling_rin_sat) {
    Link::Pd p = kLink.pd;
    p.enable = false;                                 // no PD noise
    p.sat.enable = true;
    p.sat.level = 1;
    const double rop = -8;
    Photodetector pd(p, kLink.ref, kTx.fs, rop, 1);
    std::vector<cd> A = test_envelope(200), ref;
    for (auto a : A) ref.push_back(rapp_ref(std::pow(10.0, (rop - p.rop_ref_dbm) / 10) * a, kLink.ref.a0, 2));
    pd.process(A.data(), A.size());
    CHECK(max_abs_diff(A, ref, 0, A.size()) < 1e-14);

    // RIN on does not shift the PD noise stream: a zero field gives the noise alone
    Link::Pd q = kLink.pd;
    std::vector<cd> a(1000, cd(0)), b(1000, cd(0));
    Photodetector off(q, kLink.ref, kTx.fs, -10, 9);
    q.rin.enable = true;
    Photodetector on(q, kLink.ref, kTx.fs, -10, 9);
    off.process(a.data(), a.size());
    on.process(b.data(), b.size());
    CHECK(max_abs_diff(a, b, 0, a.size()) == 0);
}

TEST(rf_amp_with_memory) {
    Link::Rf p = kLink.rf;
    p.memory.enable = true;
    const double rop = -11.75;
    RfAmp amp(p, kLink.ref, rop, kLink.pd.rop_ref_dbm, kTx.fs);
    const int n = 500;
    std::vector<cd> A = test_envelope(n);
    // rf_amp.m over the whole sequence
    const double a0 = kLink.ref.a0;
    std::vector<cd> A_in(n), ref(n);
    for (int i = 0; i < n; ++i) {
        A_in[i] = A[i] / std::pow(10.0, (rop - kLink.pd.rop_ref_dbm) / 10);
        ref[i] = rapp_ref(A_in[i], p.asat * a0, p.smooth);
    }
    const int D = static_cast<int>(std::lround(p.memory.delay_s * kTx.fs));
    CHECK(D == 12);
    for (int m = 1; m <= 2; ++m)
        for (int i = m * D; i < n; ++i) {
            const cd um = A_in[i - m * D] / a0;
            ref[i] += a0 * (p.memory.linear[m - 1] * um + p.memory.cubic[m - 1] * um * std::norm(um));
        }
    for (int pos = 0; pos < n; pos += 37) amp.process(A.data() + pos, std::min(37, n - pos));
    CHECK(max_abs_diff(A, ref, 0, n) < 1e-14);

    p.enable = false;                                 // disabled Rapp, memory still applies
    p.memory.enable = false;
    RfAmp lin(p, kLink.ref, rop, kLink.pd.rop_ref_dbm, kTx.fs);
    std::vector<cd> B = test_envelope(10);
    lin.process(B.data(), B.size());
    CHECK(max_abs_diff(B, std::vector<cd>(A_in.begin(), A_in.begin() + 10), 0, 10) < 1e-15);
}

TEST(rf_response_echo) {
    Link::Ripple p = kLink.ripple;
    p.enable = true;
    p.rho = 0.2;
    p.phase_rad = 0.7;
    RfResponse rr(p, kTx.fs);
    const int n = 300;
    std::vector<cd> A = test_envelope(n), ref(n);
    const int D = static_cast<int>(std::lround(p.delay_s * kTx.fs));
    CHECK(D == 20);
    for (int i = 0; i < n; ++i) ref[i] = A[i] + (i >= D ? p.rho * std::exp(cd(0, p.phase_rad)) * A[i - D] : cd(0));
    for (int pos = 0; pos < n; pos += 13) rr.process(A.data() + pos, std::min(13, n - pos));
    CHECK(max_abs_diff(A, ref, 0, n) < 1e-15);
    RfResponse off(kLink.ripple, kTx.fs);             // disabled by default
    std::vector<cd> B = test_envelope(50), C = B;
    off.process(B.data(), B.size());
    CHECK(max_abs_diff(B, C, 0, 50) == 0);
}

TEST(wireless_hop_cnr) {
    WirelessHop wl(kLink.wireless, kLink.ref, kTx.fs, 1);
    const auto& p = kLink.wireless;
    const double fspl_db = 20 * std::log10(4 * kPi * p.distance_m * p.f_rf / kC0);
    const double noise_dbm = 10 * std::log10(1.380649e-23 * 290 * 1e3) + 10 * std::log10(kLink.ref.bw) + p.nf_db;
    CHECK_NEAR(wl.cnr_db(), p.p_tx_dbm + p.g_tx_dbi + p.g_rx_dbi - fspl_db - noise_dbm, 1e-12);
    CHECK_NEAR(wl.cnr_db(), 42.0, 0.05);              // default_link: -11 dBm gives a 42 dB UE CNR
    Link::Wireless q = p;
    q.enable = false;
    WirelessHop off(q, kLink.ref, kTx.fs, 1);
    std::vector<cd> A = test_envelope(50), B = A;
    off.process(A.data(), A.size());
    CHECK(max_abs_diff(A, B, 0, 50) == 0);
}

TEST(envelope_detector_matches_filter) {
    EnvelopeDetector det(kLink.det, kTx.fs);
    const int n = 3000;
    std::vector<cd> A(n);
    for (int i = 0; i < n; ++i) A[i] = cd(std::sin(0.3 * i) + 0.5, std::cos(0.11 * i));
    std::vector<double> x, part;
    for (int pos = 0; pos < n; pos += 101) {
        det.process(A.data() + pos, std::min(101, n - pos), part);
        x.insert(x.end(), part.begin(), part.end());
    }
    // filter(alpha, [1, alpha-1], x, (1-alpha)*x(1)), direct form II transposed
    const double alpha = 1 - std::exp(-2 * kPi * kLink.det.bw / kTx.fs);
    double z = (1 - alpha) * std::norm(A[0]), err = 0;
    for (int i = 0; i < n; ++i) {
        const double y = alpha * std::norm(A[i]) + z;
        z = -(alpha - 1) * y;
        err = std::max(err, std::fabs(x[i] - y));
    }
    CHECK(err < 1e-14);
    Link::Det q = kLink.det;
    q.enable = false;                                 // square law only
    EnvelopeDetector raw(q, kTx.fs);
    raw.process(A.data(), 10, part);
    for (int i = 0; i < 10; ++i) CHECK(part[i] == std::norm(A[i]));
}

TEST(adc_rules) {
    std::vector<double> x;
    for (int i = 0; i <= 1000; ++i) x.push_back(1 + 0.001 * i + 0.0003 * std::sin(i));
    const AdcConstants c = adc_calibrate(x.data(), x.size(), kLink.adc);
    const auto [mn, mx] = std::minmax_element(x.begin(), x.end());
    CHECK(c.lo == *mn);
    CHECK_NEAR(c.step, (*mx - *mn) / 255, 1e-15);
    std::vector<double> q = x;
    adc(q.data(), q.size(), c, kLink.adc);
    for (size_t i = 0; i < x.size(); ++i) CHECK_NEAR(q[i], c.lo + c.step * std::round((x[i] - c.lo) / c.step), 1e-15);
    double out[2] = {*mn - 1, *mx + 1};               // a fixed full scale: saturates
    adc(out, 2, c, kLink.adc);
    CHECK_NEAR(out[0], c.lo, 1e-15);
    CHECK_NEAR(out[1], c.lo + 255 * c.step, 1e-12);

    Link::Adc p = kLink.adc;                          // clip at mean -+ clip_std*std
    p.clip_std = 1.2;
    const AdcConstants k = adc_calibrate(x.data(), x.size(), p);
    double m = 0, ss = 0;
    for (double v : x) m += v / x.size();
    for (double v : x) ss += (v - m) * (v - m);
    const double s = std::sqrt(ss / (x.size() - 1));
    CHECK_NEAR(k.clip_lo, m - 1.2 * s, 1e-12);
    CHECK_NEAR(k.lo, m - 1.2 * s, 1e-12);
    CHECK_NEAR(k.step, 2 * 1.2 * s / 255, 1e-12);

    p.enable = false;                                 // no quantization, clipping only
    std::vector<double> y = x;
    adc(y.data(), y.size(), k, p);
    for (size_t i = 0; i < x.size(); ++i) CHECK(y[i] == std::min(std::max(x[i], k.clip_lo), k.clip_hi));
}

TEST_MAIN()
