// The whole chain: reproducibility, block-size invariance, noise-free reception, the reference
// operating point against matlab/results/compare_results.mat, and live changes.
#include <fstream>
#include <sstream>

#include "check.h"
#include "core/link_settings.h"
#include "link/transmit_receive.h"

using namespace arof;

namespace {

const TxConfig kTx = default_tx();

RxSettings rx_fast() {
    RxSettings r;
    r.calib_symbols = 20000;
    r.train_symbols = 2000;
    return r;
}

TransmitReceive make(uint64_t seed, const RxSettings& rx = rx_fast(), Link link = default_link(kTx.rs),
                     double rop = -11.75) {
    return TransmitReceive(kTx, link, 0.3, rop, rx, seed);
}

bool same(const std::vector<RxSymbol>& a, const std::vector<RxSymbol>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].x != b[i].x || a[i].d != b[i].d || a[i].idx != b[i].idx || a[i].k != b[i].k) return false;
    return true;
}

}  // namespace

TEST(same_seed_same_stream_bit_for_bit) {
    auto a = make(7), b = make(7);
    a.calibrate();
    b.calibrate();
    std::vector<RxSymbol> xa, xb;
    a.generate(3000, xa);
    b.generate(3000, xb);
    CHECK(!xa.empty());
    CHECK(same(xa, xb));
}

TEST(block_size_does_not_change_the_stream) {
    auto a = make(11), b = make(11);
    a.calibrate();
    b.calibrate();
    std::vector<RxSymbol> xa, xb;
    a.generate(4000, xa);
    size_t left = 4000;
    for (size_t i = 0; left > 0; ++i) {
        const size_t m = std::min(left, size_t{1} + (i * 37) % 300);
        b.generate(m, xb);
        left -= m;
    }
    CHECK(same(xa, xb));
}

TEST(symbols_in_order) {
    auto g = make(3);
    g.calibrate();
    std::vector<RxSymbol> x;
    g.generate(2000, x);
    for (size_t i = 1; i < x.size(); ++i) CHECK(x[i].k == x[i - 1].k + 1);
    for (const auto& s : x) CHECK(s.d == qam16_gray()[s.idx]);
    CHECK(x.front().k > g.calibration_symbols().back().k);
}

TEST(noise_free_link_has_no_errors) {
    Link l = default_link(kTx.rs);
    l.pd.enable = false;          // no PD noise
    l.wireless.enable = false;    // no UE noise
    auto g = make(1, rx_fast(), l);
    const auto& rep = g.calibrate();
    std::vector<RxSymbol> x;
    g.generate(20000, x);
    const EvmBer q = evm_ber(x.data(), x.size());
    std::printf("    noise free: EVM %.2f %%, offset %d\n", 100 * q.evm, rep.rx.offset + 1);
    CHECK(q.ber == 0);
    CHECK(q.evm < 0.13);          // the link is distortion limited (MZM, PA, square law)
    CHECK(rep.rx.offset + 1 >= 801 && rep.rx.offset + 1 <= 851);
}

TEST(reference_point_against_matlab) {
    // compare_results.mat at 300 mV, -11.75 dBm: no-EQ EVM 13.21 %, BER 7.2e-5 (runs 4.4e-5 .. 1.1e-4),
    // 6 runs of 50000 symbols. Here 3 seeds, the same calibration block and test symbols.
    RxSettings rx;                // calib 50000, train 5000, taps 10: as experiment_setup.m
    double evm = 0, ber = 0;
    for (uint64_t seed : {1, 2, 3}) {
        auto g = make(seed, rx);
        const auto& q = g.calibrate().quality;
        std::printf("    seed %llu: %zu test symbols, EVM %.2f %%, BER %.2e\n", static_cast<unsigned long long>(seed),
                    q.n, 100 * q.evm, q.ber);
        CHECK(q.n == 44991);
        evm += q.evm / 3;
        ber += q.ber / 3;
    }
    CHECK_NEAR(evm, 0.1321, 0.003);
    CHECK(ber > 2e-5 && ber < 2.5e-4);
}

TEST(live_change_recalibrates) {
    auto g = make(5);
    g.calibrate();
    std::vector<RxSymbol> x;
    g.generate(5000, x);
    const EvmBer before = evm_ber(x.data(), x.size());
    const auto& rep = g.update(g.link(), 0.3, -15.5, rx_fast());
    CHECK(!rep.rx_frozen);
    std::vector<RxSymbol> y;
    g.generate(5000, y);
    const EvmBer after = evm_ber(y.data(), y.size());
    // compare_results.mat: 13.2 % at -11.75 dBm, 17.1 % at -15.5 dBm
    std::printf("    EVM %.2f %% at -11.75 dBm, %.2f %% at -15.5 dBm\n", 100 * before.evm, 100 * after.evm);
    CHECK_NEAR(after.evm, 0.1707, 0.006);
    for (size_t i = 1; i < y.size(); ++i) CHECK(y[i].k == y[i - 1].k + 1);
    CHECK(y.front().k > x.back().k);
}

TEST(freeze_rx_keeps_receiver_constants) {
    RxSettings rx = rx_fast();
    rx.freeze_rx = true;
    auto g = make(5, rx);
    const RxConstants c0 = g.calibrate().rx;
    const AdcConstants a0 = g.report().adc;
    LinkSettings ls;
    ls.bias_drift = 0.05;
    const auto& rep = g.update(apply_link_settings(default_link(kTx.rs), ls), 0.3, -11.75, rx);
    CHECK(rep.rx_frozen);
    CHECK(rep.rx.offset == c0.offset && rep.rx.gain == c0.gain && rep.rx.r_mean == c0.r_mean);
    CHECK(rep.adc.lo == a0.lo && rep.adc.step == a0.step);
}

TEST(impairment_switches_as_impairment_sweeps) {
    const Link base = default_link(kTx.rs);
    LinkSettings s;
    Link l = apply_link_settings(base, s);            // all off: the reference link
    CHECK(!l.pd.rin.enable && !l.pd.sat.enable && !l.rf.memory.enable && !l.ripple.enable);
    CHECK(l.mzm.vbias == base.mzm.vbias && std::isinf(l.adc.clip_std) && l.fiber.length_m == 15e3);
    s.bias_drift = 0.1;
    s.pa_memory = 2;
    s.ripple_rho = 0.25;
    s.pd_sat = 0.75;
    s.adc_clip = 1.2;
    s.rin_dbhz = -130;
    s.fiber_m = 60e3;
    l = apply_link_settings(base, s);
    CHECK_NEAR(l.mzm.vbias, 2.2 + 0.1 * 4.4, 1e-15);
    CHECK(l.rf.memory.enable && l.rf.memory.linear[0] == 2 * -0.08 && l.rf.memory.cubic[1] == 2 * 0.02);
    CHECK(l.ripple.enable && l.ripple.rho == 0.25);
    CHECK(l.pd.sat.enable && l.pd.sat.level == 0.75 && l.pd.sat.smooth == 2);
    CHECK(l.adc.clip_std == 1.2);
    CHECK(l.pd.rin.enable && l.pd.rin.dbhz == -130);
    CHECK(l.fiber.length_m == 60e3);
    CHECK(l.ref.a0 == base.ref.a0);                    // a0 stays that of default_link
}

TEST_MAIN()
