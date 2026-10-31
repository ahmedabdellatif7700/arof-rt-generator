#include "link/transmit_receive.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>

#include "link/driver_amp.h"
#include "link/mzm.h"

namespace arof {

namespace {
constexpr size_t kChunkSymbols = 128;   // 6400 samples: the stage buffers stay in cache
}

TransmitReceive::TransmitReceive(const TxConfig& cfg, const Link& link, double vpp, double rop_dbm,
                                 const RxSettings& rx, uint64_t seed)
    : cfg_(cfg), link_(link), vpp_(vpp), rop_dbm_(rop_dbm), rxs_(rx), tx_(cfg, seed),
      fiber_(std::make_unique<Fiber>(link.fiber, cfg.fs, mzm(0, link.mzm))),
      pd_(link.pd, link.ref, cfg.fs, rop_dbm, seed),
      amp_(link.rf, link.ref, rop_dbm, link.pd.rop_ref_dbm, cfg.fs), ripple_(link.ripple, cfg.fs),
      wl_(link.wireless, link.ref, cfg.fs, seed), det_(link.det, cfg.fs), rx_(cfg) {
    if (rxs_.train_symbols == 0 || rxs_.calib_symbols < rxs_.train_symbols + 2 * static_cast<size_t>(rxs_.eval_taps))
        throw std::invalid_argument("calib_symbols must exceed train_symbols + taps");
}

TransmitReceive::~TransmitReceive() = default;

void TransmitReceive::configure() {
    pd_.set(link_.pd, link_.ref, cfg_.fs, rop_dbm_);
    amp_.set(link_.rf, link_.ref, rop_dbm_, link_.pd.rop_ref_dbm, cfg_.fs);
    ripple_.set(link_.ripple, cfg_.fs);
    wl_.set(link_.wireless, link_.ref, cfg_.fs);
    det_.set(link_.det, cfg_.fs);
}

void TransmitReceive::channel(const double* v, size_t n, std::vector<double>& r) {
    E_.resize(n);
    for (size_t i = 0; i < n; ++i)                        // AWG, driver_amp, mzm
        E_[i] = mzm(driver_amp(k_awg_ * v[i], link_.driver), link_.mzm);
    A_.clear();
    fiber_->process(E_.data(), n, A_);                    // fiber (the count varies)
    pd_.process(A_.data(), A_.size());                    // photodetector
    amp_.process(A_.data(), A_.size());                   // rf_amp
    ripple_.process(A_.data(), A_.size());                // rf_response
    wl_.process(A_.data(), A_.size());                    // wireless_hop
    det_.process(A_.data(), A_.size(), x_);               // envelope_detector
    r.insert(r.end(), x_.begin(), x_.end());
}

const CalibrationReport& TransmitReceive::calibrate() {
    const auto t_start = std::chrono::steady_clock::now();
    const size_t n_sym = rxs_.calib_symbols;
    const size_t guard = static_cast<size_t>(cfg_.rrc_span);      // dropped at each end
    const size_t sps = static_cast<size_t>(cfg_.sps());
    CalibrationReport rep;
    rep.first_symbol = n_sym_;
    rep.fiber_trunc = fiber_->truncation_error();

    // transmitter over the block of n_sym + 2*guard symbols, then v * (vpp/(max(v) - min(v)))
    idx_.clear();
    d_.clear();
    v_.clear();
    tx_.process(n_sym + 2 * guard, idx_, d_, v_);
    for (size_t i = 0; i < idx_.size(); ++i) rx_.push_symbol(d_[i], idx_[i]);
    n_sym_ += idx_.size();
    const auto [vmin, vmax] = std::minmax_element(v_.begin(), v_.end());
    k_awg_ = vpp_ / (*vmax - *vmin);
    rep.k_awg = k_awg_;
    rep.vpp = k_awg_ * (*vmax - *vmin);
    rep.vd_pp = driver_amp(k_awg_ * *vmax, link_.driver) - driver_amp(k_awg_ * *vmin, link_.driver);
    const std::vector<cd> d_block = d_;

    // the channel over the block; the stream holds back up to max_holdback samples (fiber),
    // so a short tail of further symbols brings the block's last samples out
    std::vector<double> r;
    r.reserve(v_.size() + 2 * static_cast<size_t>(fiber_->max_holdback()));
    const int64_t t0 = rx_.time();
    for (size_t i = 0; i < v_.size(); i += kChunkSymbols * sps)
        channel(v_.data() + i, std::min(kChunkSymbols * sps, v_.size() - i), r);
    const size_t n_block = r.size();
    const size_t n_tail = static_cast<size_t>(fiber_->max_holdback()) / sps + guard + 2;
    idx_.clear();
    d_.clear();
    v_.clear();
    tx_.process(n_tail, idx_, d_, v_);
    for (size_t i = 0; i < idx_.size(); ++i) rx_.push_symbol(d_[i], idx_[i]);
    n_sym_ += idx_.size();
    channel(v_.data(), v_.size(), r);

    // adc: range from the block, then quantize
    const bool keep_rx = rxs_.freeze_rx && calibrated_;
    if (!keep_rx) adc_ = adc_calibrate(r.data(), n_block, link_.adc);
    adc(r.data(), r.size(), adc_, link_.adc);

    // offline DSP: mean(r), timing and gain on train = guard + (1:n_train)
    RxConstants c = rx_.constants();
    if (!keep_rx) {
        double sum = 0;
        for (size_t i = 0; i < n_block; ++i) sum += r[i];
        c.r_mean = sum / static_cast<double>(n_block);
        const Acquisition a = acquire(cfg_, tx_.rrc(), r.data(), t0, r.size(), c.r_mean, d_block.data() + guard,
                                      static_cast<int64_t>(rep.first_symbol + guard), rxs_.train_symbols);
        c.offset = a.offset;
        c.gain = a.gain;
        rep.corr = a.corr;
        rx_.set_constants(c);
    }
    rep.adc = adc_;
    rep.rx = c;
    rep.rx_frozen = keep_rx;
    rep.pd_cnr_db = link_.pd.cnr_db + 2 * (rop_dbm_ - link_.pd.rop_ref_dbm);
    rep.ue_cnr_db = wl_.cnr_db();

    // the block through the receiver (not streamed): keep = guard + (1:n_sym), scored as
    // main_demo.m step 9 scores no EQ
    std::vector<RxSymbol> out;
    rx_.process(r.data(), r.size(), out);
    calib_out_.clear();
    const uint64_t k_first = rep.first_symbol + guard;
    for (const RxSymbol& s : out)
        if (s.k >= k_first && s.k < k_first + n_sym) calib_out_.push_back(s);
    if (calib_out_.size() != n_sym) throw std::logic_error("calibration: incomplete block");
    rep.quality = evaluate_no_eq(calib_out_.data(), n_sym, rxs_.eval_taps,
                                 static_cast<double>(rxs_.train_symbols) / static_cast<double>(n_sym));

    rep.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count();
    calibrated_ = true;
    report_ = rep;
    return report_;
}

void TransmitReceive::generate(size_t n, std::vector<RxSymbol>& out) {
    if (!calibrated_) throw std::logic_error("generate() before calibrate()");
    while (n > 0) {
        const size_t m = std::min(n, kChunkSymbols);
        idx_.clear();
        d_.clear();
        v_.clear();
        tx_.process(m, idx_, d_, v_);
        for (size_t i = 0; i < m; ++i) rx_.push_symbol(d_[i], idx_[i]);
        n_sym_ += m;
        r_.clear();
        channel(v_.data(), v_.size(), r_);
        adc(r_.data(), r_.size(), adc_, link_.adc);
        rx_.process(r_.data(), r_.size(), out);
        n -= m;
    }
}

const CalibrationReport& TransmitReceive::update(const Link& link, double vpp, double rop_dbm,
                                                 const RxSettings& rx) {
    if (link.fiber.enable != link_.fiber.enable)
        throw std::invalid_argument("fiber.enable can only be set at startup");
    const bool new_fiber = link.fiber.length_m != link_.fiber.length_m || link.fiber.D != link_.fiber.D ||
                           link.fiber.lambda != link_.fiber.lambda;
    link_ = link;
    vpp_ = vpp;
    rop_dbm_ = rop_dbm;
    rxs_ = rx;
    if (new_fiber) fiber_->set(link_.fiber);
    configure();
    return calibrate();
}

}  // namespace arof
