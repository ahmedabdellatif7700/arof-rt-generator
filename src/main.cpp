// arof_stream: real-time A-RoF sample generator, a streaming port of matlab/main_demo.m
// steps 1-7 (the chain of link/transmit_receive.m).

#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/config.h"
#include "link/transmit_receive.h"
#include "stream/recorder.h"
#include "stream/sample_former.h"
#include "stream/streamer.h"

using namespace arof;
using nlohmann::json;

namespace {

constexpr const char* kVersion = "0.2.0";

void usage(const char* prog) {
    std::printf(
        "usage: %s [options]\n"
        "  --config FILE    settings (default /config/default.json)\n"
        "  --set JSON       override settings, e.g. '{\"link\": {\"rop_dbm\": -13}}' (repeatable)\n"
        "  --seed N         generator seed\n"
        "  --record DIR     record the first record.max_samples samples to DIR and exit (no network)\n"
        "  --samples N      number of samples to record (record.max_samples)\n"
        "  --bench SECONDS  generate without sending for SECONDS and report the sample rate\n"
        "  --calibrate      calibrate, print the link as main_demo.m does, and exit\n"
        "  --json           with --calibrate: print the calibration as JSON instead\n"
        "  --version        print the version\n",
        prog);
}

// the lines main_demo.m prints after step 7, then its step 9 row for no EQ
void print_report(const CalibrationReport& r, const Settings& s, const Link& l) {
    const double fspl_db = 20 * std::log10(4 * kPi * l.wireless.distance_m * l.wireless.f_rf / kC0);
    std::printf("Link at %.0f mV, ROP %.2f dBm (%zu symbols, seed %u)\n", s.link.vpp_mv, s.link.rop_dbm,
                s.rx.calib_symbols, s.seed);
    std::printf("  AWG %.0f mVpp -> driver %.2f Vpp (MZM Vpi %.1f V)\n", 1e3 * r.vpp, r.vd_pp, l.mzm.vpi);
    std::printf("  PD CNR %.1f dB (%.0f dB at %.0f dBm, 2 dB per dB of ROP)\n", r.pd_cnr_db, l.pd.cnr_db,
                l.pd.rop_ref_dbm);
    std::printf("  wireless: %.1f GHz, FSPL %.1f dB over %g m, UE CNR %.1f dB\n", l.wireless.f_rf / 1e9, fspl_db,
                l.wireless.distance_m, r.ue_cnr_db);
    std::printf("  receiver: offset %d, gain %.5f%+.5fj, ADC lo %.6g step %.6g, mean(r) %.6g%s\n",
                r.rx.offset + 1, r.rx.gain.real(), r.rx.gain.imag(), r.adc.lo, r.adc.step, r.rx.r_mean,
                r.rx_frozen ? " (frozen)" : "");
    std::printf("\nTest symbols: %zu (%zu bits)\n", r.quality.n, 4 * r.quality.n);
    std::printf("  %-34s %8s %10s\n", "equalizer", "EVM %", "BER");
    std::printf("  %-34s %8.1f %10.2e\n", "w/o EQ", 100 * r.quality.evm, r.quality.ber);
    std::printf("(calibration block, %.2f s)\n", r.seconds);
}

int record(const Settings& s, TransmitReceive& trx) {
    SampleFormer former(s.samples);
    Recorder rec(s.record.dir, s.record.max_samples, former.words_per_sample());
    std::vector<RxSymbol> syms, kept;
    std::vector<int32_t> words;
    while (!rec.full()) {
        syms.clear();
        trx.generate(1024, syms);
        for (const RxSymbol& sym : syms) {
            if (rec.full()) break;
            rec.symbol(sym);
            kept.push_back(sym);
            const bool test = former.next_is_test();
            words.clear();
            if (former.push(sym, words)) rec.sample(words.data(), test);
        }
    }
    const EvmBer q = evm_ber(kept.data(), kept.size());
    const json meta = {
        {"version", kVersion},
        {"settings", to_json(s)},
        {"link", link_to_json(trx.link())},
        {"calibration", report_json(trx.report())},
        {"taps", s.samples.taps},
        {"scale", to_string(s.samples.scale)},
        {"evm_no_eq", q.evm},
        {"ber_no_eq", q.ber},
        {"first_symbol", kept.empty() ? 0 : kept.front().k},
    };
    rec.close(meta);
    std::printf("recorded %llu samples (%zu symbols) to %s: EVM %.2f %%, BER %.2e (all symbols, no EQ)\n",
                static_cast<unsigned long long>(rec.samples()), kept.size(), rec.dir().c_str(), 100 * q.evm, q.ber);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string config_path = "/config/default.json";
    std::vector<std::string> sets;
    std::string record_dir;
    long long samples = -1, seed = -1;
    double bench_s = 0;
    bool calibrate_only = false, as_json = false;
    for (int i = 1; i < argc; ++i) {
        auto arg = [&](const char* name) { return !std::strcmp(argv[i], name) && i + 1 < argc; };
        if (arg("--config")) config_path = argv[++i];
        else if (arg("--set")) sets.emplace_back(argv[++i]);
        else if (arg("--seed")) seed = std::stoll(argv[++i]);
        else if (arg("--record")) record_dir = argv[++i];
        else if (arg("--samples")) samples = std::stoll(argv[++i]);
        else if (arg("--bench")) bench_s = std::stod(argv[++i]);
        else if (!std::strcmp(argv[i], "--calibrate")) calibrate_only = true;
        else if (!std::strcmp(argv[i], "--json")) as_json = true;
        else if (!std::strcmp(argv[i], "--version")) {
            std::printf("arof_stream %s\n", kVersion);
            return 0;
        } else if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) {
            usage(argv[0]);
            return 0;
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", argv[i]);
            usage(argv[0]);
            return 2;
        }
    }

    try {
        Settings s = load_settings(config_path);
        for (const auto& j : sets) {
            json merged = to_json(s);
            merged.merge_patch(json::parse(j));
            s = settings_from_json(merged);
        }
        if (seed >= 0) s.seed = static_cast<uint32_t>(seed);
        if (!record_dir.empty()) {
            s.record.enable = true;
            s.record.dir = record_dir;
        }
        if (samples >= 0) s.record.max_samples = static_cast<uint64_t>(samples);
        if (bench_s > 0) return bench(s, bench_s);
        if (!calibrate_only && record_dir.empty()) {         // streaming (records too if record.enable)
            static Streamer* active = nullptr;
            Streamer streamer(s);
            active = &streamer;
            auto on_signal = [](int) { if (active) active->stop(); };
            std::signal(SIGINT, on_signal);
            std::signal(SIGTERM, on_signal);
            return streamer.run();
        }

        const Link link = s.make_link();
        TransmitReceive trx(s.tx, link, s.link.vpp_mv * 1e-3, s.link.rop_dbm, s.rx, s.seed);
        const CalibrationReport& rep = trx.calibrate();
        if (as_json) {
            std::printf("%s\n", json{{"settings", to_json(s)}, {"calibration", report_json(rep)}}.dump().c_str());
            if (calibrate_only) return 0;
        } else {
            print_report(rep, s, link);
        }
        if (calibrate_only) return 0;
        return record(s, trx);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
