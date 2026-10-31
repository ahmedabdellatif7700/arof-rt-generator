// Sample former (layout, scaling, Q8.24 words as nn_training_model), recorder files, settings.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "check.h"
#include "core/config.h"
#include "stream/recorder.h"
#include "stream/sample_former.h"

#ifndef TEST_DATA_DIR
#define TEST_DATA_DIR "tests/data"
#endif

using namespace arof;

TEST(q8_24_rounds_half_to_even) {
    const double u = 1.0 / 16777216.0;   // one LSB (exact in float32 for these small values)
    CHECK(to_q8_24(0.5 * u) == 0);
    CHECK(to_q8_24(1.5 * u) == 2);
    CHECK(to_q8_24(2.5 * u) == 2);
    CHECK(to_q8_24(-0.5 * u) == 0);
    CHECK(to_q8_24(-1.5 * u) == -2);
    CHECK(to_q8_24(-2.5 * u) == -2);
    CHECK(to_q8_24(0.49 * u) == 0);
    CHECK(to_q8_24(1.0) == 16777216);
    CHECK(to_q8_24(-3.0) == -50331648);
    CHECK(to_q8_24(-128.0) == INT32_MIN);
    CHECK(to_q8_24(128.0) == INT32_MAX);
    CHECK(to_q8_24(1e6) == INT32_MAX);
    CHECK(to_q8_24(-1e6) == INT32_MIN);
    // the float32 cast first: 1 + 2^-24 is not a float32, it rounds to 1
    CHECK(to_q8_24(1.0 + u) == 16777216);
    CHECK(to_q8_24(2.7 + 0.3 * u) == to_q8_24(static_cast<float>(2.7)));
}

TEST(q8_24_words_as_nn_training_model) {
    // tests/data/q8_24_fixture.txt: values of training_data.mat and the words of
    // train_samples_q8_24.mem that equalizer_q8_24.py wrote for them
    std::ifstream f(TEST_DATA_DIR "/q8_24_fixture.txt");
    CHECK(f.good());
    std::string line;
    int n = 0, bad = 0;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream in(line);
        double v[6];
        std::string bar, hex;
        for (double& x : v) in >> x;
        in >> bar;
        for (int i = 0; i < 6; ++i) {
            in >> hex;
            char buf[16];
            std::snprintf(buf, sizeof buf, "%08X", static_cast<uint32_t>(to_q8_24(v[i])));
            bad += hex != buf;
        }
        ++n;
    }
    CHECK(n == 200);
    CHECK(bad == 0);
}

TEST(layout_newest_first_on_grid) {
    SampleFormer f(SampleSettings{});
    CHECK(f.words_per_sample() == 6);
    const auto& q = qam16_gray();
    std::vector<RxSymbol> s;
    for (uint8_t i = 0; i < 5; ++i) s.push_back({q[i] * 1.01 + cd(0.001 * i, 0), q[i], i, i});
    std::vector<int32_t> w;
    std::vector<double> v;
    CHECK(!f.push(s[0], w, &v));   // the first symbol has no previous tap
    for (int n = 1; n < 5; ++n) {
        w.clear();
        v.clear();
        CHECK(f.push(s[n], w, &v));
        const double k = std::sqrt(10.0);
        CHECK_NEAR(v[0], k * s[n].x.real(), 1e-15);       // Re x(n)
        CHECK_NEAR(v[1], k * s[n].x.imag(), 1e-15);       // Im x(n)
        CHECK_NEAR(v[2], k * s[n - 1].x.real(), 1e-15);   // Re x(n-1)
        CHECK_NEAR(v[3], k * s[n - 1].x.imag(), 1e-15);   // Im x(n-1)
        CHECK(v[4] == std::round(k * s[n].d.real()));      // Re d(n) on {+-1, +-3}
        CHECK(v[5] == std::round(k * s[n].d.imag()));
        CHECK(std::fabs(v[4]) == 1 || std::fabs(v[4]) == 3);
        for (int i = 0; i < 6; ++i) CHECK(w[i] == to_q8_24(v[i]));
    }
    CHECK(f.samples() == 4);
}

TEST(unit_scale_and_taps) {
    SampleSettings ss;
    ss.scale = Scale::unit;
    ss.taps = 3;
    SampleFormer f(ss);
    CHECK(f.words_per_sample() == 8);
    const auto& q = qam16_gray();
    std::vector<int32_t> w;
    std::vector<double> v;
    for (uint8_t i = 0; i < 3; ++i) f.push({q[i], q[i], i, i}, w, &v);
    CHECK(v.size() == 8);
    CHECK(v[0] == q[2].real() && v[2] == q[1].real() && v[4] == q[0].real());
    CHECK(v[6] == q[2].real());
}

TEST(train_test_schedule) {
    SampleSettings ss;
    ss.schedule = Schedule::train_test;
    ss.train_len = 3;
    ss.test_len = 2;
    SampleFormer f(ss);
    std::vector<int32_t> w;
    std::string tags;
    for (uint64_t k = 0; k < 12; ++k) {
        const bool test = f.next_is_test();
        if (f.push({0, 0, 0, k}, w)) tags += test ? 'T' : 't';
    }
    CHECK(tags == "tttTTtttTTt");
}

TEST(recorder_files) {
    const std::string dir = std::filesystem::temp_directory_path() / "arof_rec_test";
    std::filesystem::remove_all(dir);
    {
        SampleSettings ss;
        ss.schedule = Schedule::train_test;
        ss.train_len = 2;
        ss.test_len = 1;
        SampleFormer f(ss);
        Recorder r(dir, 4, f.words_per_sample());
        const auto& q = qam16_gray();
        std::vector<int32_t> w;
        for (uint8_t i = 0; i < 10 && !r.full(); ++i) {
            const RxSymbol s{q[i] * 0.9, q[i], i, i};
            r.symbol(s);
            const bool test = f.next_is_test();
            w.clear();
            if (f.push(s, w)) r.sample(w.data(), test);
        }
        CHECK(r.full());
        r.close({{"note", "test"}});
    }
    CHECK(std::filesystem::file_size(dir + "/samples.bin") == 4 * 6 * 4);
    CHECK(std::filesystem::file_size(dir + "/flags.bin") == 4);
    CHECK(std::filesystem::file_size(dir + "/symbols.bin") == 5 * 5 * 8);   // samples + taps - 1
    std::ifstream train(dir + "/train_samples_q8_24.mem"), test(dir + "/test_samples_q8_24.mem");
    int n_train = 0, n_test = 0;
    std::string line;
    while (std::getline(train, line)) {
        CHECK(line.size() == 8);
        CHECK(line.find_first_not_of("0123456789ABCDEF") == std::string::npos);   // uppercase
        ++n_train;
    }
    while (std::getline(test, line)) { CHECK(line.size() == 8); ++n_test; }
    CHECK(n_train == 3 * 6);
    CHECK(n_test == 1 * 6);
    const auto meta = nlohmann::json::parse(std::ifstream(dir + "/meta.json"));
    CHECK(meta["n_samples"] == 4 && meta["n_test"] == 1 && meta["n_symbols"] == 5);
    std::filesystem::remove_all(dir);
}

TEST(settings_round_trip) {
    const auto j = nlohmann::json::parse(R"({
      "link": { "vpp_mv": 300, "rop_dbm": -11.75, "fiber_m": 15000, "bias_drift": 0,
                "rin_dbhz": "-inf", "pd_sat": "inf", "pa_memory": 0, "ripple_rho": 0,
                "adc_clip": "inf" },
      "stages": { "pd": { "cnr_db": 40 }, "wireless": { "enable": false } },
      "rx": { "freeze_rx": false, "calib_symbols": 50000, "train_symbols": 5000 },
      "samples": { "taps": 2, "scale": "grid", "schedule": "train" },
      "stream": { "rate_sps": 10000, "frame_ms": 1, "host": "192.168.1.10", "port": 5000,
                  "control_port": 5001 },
      "seed": 2017,
      "record": { "enable": false, "dir": "/records", "max_samples": 9998 } })");
    const Settings s = settings_from_json(j);
    CHECK(std::isinf(s.link.rin_dbhz) && s.link.rin_dbhz < 0);
    CHECK(std::isinf(s.link.pd_sat) && s.link.pd_sat > 0);
    CHECK(s.link.rop_dbm == -11.75 && s.seed == 2017 && s.samples.taps == 2);
    const Link l = s.make_link();
    CHECK(l.pd.cnr_db == 40 && !l.wireless.enable && l.wireless.nf_db == 7);
    CHECK(l.driver.gain == 24 && l.ref.a0 > 0.7);
    const Settings t = settings_from_json(to_json(s));
    CHECK(to_json(t) == to_json(s));
    CHECK(link_to_json(t.make_link()) == link_to_json(l));
}

TEST(default_config_file) {
    const Settings s = load_settings(TEST_DATA_DIR "/../../config/default.json");
    CHECK(link_to_json(s.make_link()) == link_to_json(default_link(s.tx.rs)));   // the reference link
    CHECK(s.link.vpp_mv == 300 && s.link.rop_dbm == -11.75 && s.seed == 2017);
}

TEST(unknown_settings_rejected) {
    bool threw = false;
    try { settings_from_json(nlohmann::json::parse(R"({"stages": {"pd": {"cnr": 1}}})")); }
    catch (const std::exception&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { settings_from_json(nlohmann::json::parse(R"({"link": {"rop": 1}})")); }
    catch (const std::exception&) { threw = true; }
    CHECK(threw);
}

TEST(live_set) {
    Settings s;
    CHECK(apply_set(s, {{"rop_dbm", -13}}));
    CHECK(s.link.rop_dbm == -13);
    CHECK(apply_set(s, {{"link", {{"bias_drift", 0.05}}}}));
    CHECK(s.link.bias_drift == 0.05);
    CHECK(apply_set(s, {{"stages", {{"pd", {{"cnr_db", 30}}}}}}));
    CHECK(s.make_link().pd.cnr_db == 30);
    CHECK(!apply_set(s, {{"stream", {{"rate_sps", 20000}}}}));
    CHECK(s.stream.rate_sps == 20000);
    bool threw = false;
    try { apply_set(s, {{"seed", 1}}); } catch (const std::exception&) { threw = true; }
    CHECK(threw && s.seed == 2017);
    threw = false;
    try { apply_set(s, {{"rop_dbm", -9}, {"no_such_thing", 1}}); } catch (const std::exception&) { threw = true; }
    CHECK(threw && s.link.rop_dbm == -13);   // all or nothing
}

TEST_MAIN()
