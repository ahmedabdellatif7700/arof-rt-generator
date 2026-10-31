// matlab/link/transmit_receive.m as an endless stream:
//   AWG -> driver_amp -> mzm -> fiber -> photodetector -> rf_amp -> rf_response
//   -> wireless_hop -> envelope_detector -> adc -> offline DSP (receiver.h)
// transmit_receive.m runs one sequence of n_sym + 2*guard symbols and takes its statistics
// (AWG max/min, ADC range, mean(r)) and its timing and gain from that sequence. Here
// calibrate() does exactly that on a calibration block of the same size, freezes the results,
// and generate() then continues the stream from the same state with the constants fixed.
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "core/evaluate_outputs.h"
#include "link/adc.h"
#include "link/default_link.h"
#include "link/default_tx.h"
#include "link/envelope_detector.h"
#include "link/fiber.h"
#include "link/photodetector.h"
#include "link/receiver.h"
#include "link/rf_amp.h"
#include "link/rf_response.h"
#include "link/transmitter.h"
#include "link/wireless_hop.h"

namespace arof {

struct RxSettings {
    bool freeze_rx = false;         // keep the receiver constants on a live change
    size_t calib_symbols = 50000;   // n_sym of the calibration block (experiment_setup: 50000)
    size_t train_symbols = 5000;    // n_train = round(train_frac * n_sym)
    int eval_taps = 10;             // cfg.taps of evaluate_outputs.m (the LMS taps)
};

struct CalibrationReport {
    uint64_t first_symbol = 0;      // first symbol of the calibration block
    double vpp = 0;                 // max(v) - min(v) after the AWG scaling (V)
    double k_awg = 0;               // vpp / (max(v) - min(v))
    double vd_pp = 0;               // driver output peak-to-peak (V)
    AdcConstants adc;
    RxConstants rx;
    double corr = 0;
    bool rx_frozen = false;         // freeze_rx kept the receiver constants
    double pd_cnr_db = 0, ue_cnr_db = 0;
    EvmBer quality;                 // evaluate_outputs.m, no EQ, on the block's n_sym symbols
    double fiber_trunc = 0;
    double seconds = 0;
};

class TransmitReceive {
public:
    TransmitReceive(const TxConfig& cfg, const Link& link, double vpp, double rop_dbm,
                    const RxSettings& rx, uint64_t seed);
    ~TransmitReceive();

    const CalibrationReport& calibrate();
    // generate n symbols; appends the received symbols (the count may differ by a few: the
    // chain has latency)
    void generate(size_t n, std::vector<RxSymbol>& out);
    // live change of the link, the operating point or the receiver settings, then calibrate()
    const CalibrationReport& update(const Link& link, double vpp, double rop_dbm, const RxSettings& rx);

    const CalibrationReport& report() const { return report_; }
    const Link& link() const { return link_; }
    uint64_t symbols() const { return n_sym_; }
    uint64_t samples() const { return tx_.samples(); }
    const Receiver& receiver() const { return rx_; }
    // the received symbols of the last calibration block (n_sym after the guard)
    const std::vector<RxSymbol>& calibration_symbols() const { return calib_out_; }

private:
    void configure();
    // v before the AWG scaling -> samples before the ADC, appended to r
    void channel(const double* v, size_t n, std::vector<double>& r);

    TxConfig cfg_;
    Link link_;
    double vpp_, rop_dbm_;
    RxSettings rxs_;
    Transmitter tx_;
    double k_awg_ = 1;
    std::unique_ptr<Fiber> fiber_;
    Photodetector pd_;
    RfAmp amp_;
    RfResponse ripple_;
    WirelessHop wl_;
    EnvelopeDetector det_;
    AdcConstants adc_;
    Receiver rx_;
    CalibrationReport report_;
    std::vector<RxSymbol> calib_out_;
    bool calibrated_ = false;
    uint64_t n_sym_ = 0;

    std::vector<uint8_t> idx_;
    std::vector<cd> d_, E_, A_;
    std::vector<double> v_, r_, x_;
};

}  // namespace arof
