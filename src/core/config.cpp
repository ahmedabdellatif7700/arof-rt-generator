#include "core/config.h"

#include <cmath>
#include <fstream>
#include <stdexcept>

namespace arof {

namespace {

using nlohmann::json;

double num(const json& v, const std::string& key) {
    if (v.is_number()) return v.get<double>();
    if (v.is_string()) {
        const auto s = v.get<std::string>();
        if (s == "inf" || s == "+inf" || s == "Inf") return kInf;
        if (s == "-inf" || s == "-Inf") return -kInf;
    }
    throw std::invalid_argument("setting " + key + ": expected a number, \"inf\" or \"-inf\"");
}

json num_json(double v) {
    if (std::isinf(v)) return v > 0 ? "inf" : "-inf";
    return v;
}

// every key of `patch` must exist in `base` (recursively), with objects where base has objects
void check_known(const json& base, const json& patch, const std::string& path) {
    if (!patch.is_object()) throw std::invalid_argument(path + ": expected an object");
    for (const auto& [k, v] : patch.items()) {
        if (!base.contains(k)) throw std::invalid_argument("unknown setting " + path + "." + k);
        if (base[k].is_object()) check_known(base[k], v, path + "." + k);
    }
}

// ---- link (default_link.m) ----

#define F(obj, name) {#name, num_json(obj.name)}
#define B(obj, name) {#name, obj.name}

void get_num(const json& j, const char* k, double& out) { if (j.contains(k)) out = num(j[k], k); }
void get_bool(const json& j, const char* k, bool& out) { if (j.contains(k)) out = j[k].get<bool>(); }
void get_arr(const json& j, const char* k, std::array<double, 2>& out) {
    if (j.contains(k)) out = j[k].get<std::array<double, 2>>();
}

Link link_from_json(const json& j) {
    Link l;
    get_num(j, "lambda_sig", l.lambda_sig);
    get_num(j, "lambda_lo", l.lambda_lo);
    const json& dr = j["driver"];
    get_bool(dr, "enable", l.driver.enable); get_num(dr, "gain", l.driver.gain);
    get_num(dr, "vsat", l.driver.vsat); get_num(dr, "smooth", l.driver.smooth);
    const json& mz = j["mzm"];
    get_bool(mz, "enable", l.mzm.enable); get_num(mz, "vpi", l.mzm.vpi); get_num(mz, "vbias", l.mzm.vbias);
    const json& fi = j["fiber"];
    get_bool(fi, "enable", l.fiber.enable); get_num(fi, "length_m", l.fiber.length_m);
    get_num(fi, "D", l.fiber.D); get_num(fi, "lambda", l.fiber.lambda);
    const json& pd = j["pd"];
    get_bool(pd, "enable", l.pd.enable); get_num(pd, "rop_ref_dbm", l.pd.rop_ref_dbm);
    get_num(pd, "cnr_db", l.pd.cnr_db);
    get_bool(pd["rin"], "enable", l.pd.rin.enable); get_num(pd["rin"], "dbhz", l.pd.rin.dbhz);
    get_bool(pd["sat"], "enable", l.pd.sat.enable); get_num(pd["sat"], "level", l.pd.sat.level);
    get_num(pd["sat"], "smooth", l.pd.sat.smooth);
    const json& rf = j["rf"];
    get_bool(rf, "enable", l.rf.enable); get_num(rf, "asat", l.rf.asat); get_num(rf, "smooth", l.rf.smooth);
    const json& me = rf["memory"];
    get_bool(me, "enable", l.rf.memory.enable); get_num(me, "delay_s", l.rf.memory.delay_s);
    get_arr(me, "linear", l.rf.memory.linear); get_arr(me, "cubic", l.rf.memory.cubic);
    const json& ri = j["ripple"];
    get_bool(ri, "enable", l.ripple.enable); get_num(ri, "rho", l.ripple.rho);
    get_num(ri, "delay_s", l.ripple.delay_s); get_num(ri, "phase_rad", l.ripple.phase_rad);
    const json& wl = j["wireless"];
    get_bool(wl, "enable", l.wireless.enable); get_num(wl, "f_rf", l.wireless.f_rf);
    get_num(wl, "distance_m", l.wireless.distance_m); get_num(wl, "g_tx_dbi", l.wireless.g_tx_dbi);
    get_num(wl, "g_rx_dbi", l.wireless.g_rx_dbi); get_num(wl, "p_tx_dbm", l.wireless.p_tx_dbm);
    get_num(wl, "nf_db", l.wireless.nf_db);
    get_bool(j["det"], "enable", l.det.enable); get_num(j["det"], "bw", l.det.bw);
    const json& ad = j["adc"];
    get_bool(ad, "enable", l.adc.enable);
    if (ad.contains("bits")) l.adc.bits = ad["bits"].get<int>();
    get_num(ad, "clip_std", l.adc.clip_std);
    get_num(j["ref"], "a0", l.ref.a0);
    get_num(j["ref"], "bw", l.ref.bw);
    return l;
}

// ---- tx (default_tx.m) ----

json tx_to_json(const TxConfig& t) {
    return {{"M", t.M}, F(t, rs), F(t, fs), F(t, f_if), F(t, rolloff), {"rrc_span", t.rrc_span}};
}

TxConfig tx_from_json(const json& j) {
    TxConfig t;
    if (j.contains("M")) t.M = j["M"].get<int>();
    get_num(j, "rs", t.rs);
    get_num(j, "fs", t.fs);
    get_num(j, "f_if", t.f_if);
    get_num(j, "rolloff", t.rolloff);
    if (j.contains("rrc_span")) t.rrc_span = j["rrc_span"].get<int>();
    return t;
}

// ---- link switches (main_demo.m) ----

json switches_to_json(const LinkSettings& s) {
    return {F(s, vpp_mv), F(s, rop_dbm), F(s, fiber_m), F(s, bias_drift), F(s, rin_dbhz),
            F(s, pd_sat), F(s, pa_memory), F(s, ripple_rho), F(s, adc_clip)};
}

bool switches_from_json(LinkSettings& s, const json& j) {
    check_known(switches_to_json(s), j, "link");
    const LinkSettings before = s;
    get_num(j, "vpp_mv", s.vpp_mv);
    get_num(j, "rop_dbm", s.rop_dbm);
    get_num(j, "fiber_m", s.fiber_m);
    get_num(j, "bias_drift", s.bias_drift);
    get_num(j, "rin_dbhz", s.rin_dbhz);
    get_num(j, "pd_sat", s.pd_sat);
    get_num(j, "pa_memory", s.pa_memory);
    get_num(j, "ripple_rho", s.ripple_rho);
    get_num(j, "adc_clip", s.adc_clip);
    return switches_to_json(before) != switches_to_json(s);
}

template <class T>
void get(const json& j, const char* key, T& out) {
    if (j.contains(key)) out = j.at(key).get<T>();
}

void apply_sections(Settings& s, const json& j, bool live, bool& recal) {
    if (!j.is_object()) throw std::invalid_argument("settings: expected an object");
    for (const auto& [k, v] : j.items()) {
        if (k == "link") {
            recal |= switches_from_json(s.link, v);
        } else if (k == "stages") {
            const json base = link_to_json(default_link(s.tx.rs));
            check_known(base, v, "stages");
            if (v.contains("fiber") && v["fiber"].contains("enable") && live)
                throw std::invalid_argument("stages.fiber.enable can only be set at startup");
            json merged = s.stages;
            merged.merge_patch(v);
            recal |= merged != s.stages;
            s.stages = merged;
        } else if (k == "tx") {
            if (live) throw std::invalid_argument("tx can only be set at startup");
            check_known(tx_to_json(s.tx), v, "tx");
            s.tx = tx_from_json(v);
        } else if (k == "rx") {
            check_known(json{{"freeze_rx", 0}, {"calib_symbols", 0}, {"train_symbols", 0}, {"eval_taps", 0}}, v, "rx");
            const RxSettings before = s.rx;
            get(v, "freeze_rx", s.rx.freeze_rx);
            get(v, "calib_symbols", s.rx.calib_symbols);
            get(v, "train_symbols", s.rx.train_symbols);
            get(v, "eval_taps", s.rx.eval_taps);
            recal |= before.calib_symbols != s.rx.calib_symbols || before.train_symbols != s.rx.train_symbols;
        } else if (k == "samples") {
            if (live) throw std::invalid_argument("samples can only be set at startup");
            check_known(json{{"taps", 0}, {"scale", 0}, {"schedule", 0}, {"train_len", 0}, {"test_len", 0}}, v, "samples");
            get(v, "taps", s.samples.taps);
            if (v.contains("scale")) s.samples.scale = parse_scale(v["scale"]);
            if (v.contains("schedule")) s.samples.schedule = parse_schedule(v["schedule"]);
            get(v, "train_len", s.samples.train_len);
            get(v, "test_len", s.samples.test_len);
        } else if (k == "stream") {
            check_known(json{{"rate_sps", 0}, {"frame_ms", 0}, {"host", 0}, {"port", 0}, {"control_port", 0},
                             {"realtime", 0}}, v, "stream");
            if (live)
                for (const auto& [sk, sv] : v.items())
                    if (sk != "rate_sps") throw std::invalid_argument("stream." + sk + " can only be set at startup");
            if (v.contains("rate_sps")) s.stream.rate_sps = num(v["rate_sps"], "rate_sps");
            if (v.contains("frame_ms")) s.stream.frame_ms = num(v["frame_ms"], "frame_ms");
            get(v, "host", s.stream.host);
            get(v, "port", s.stream.port);
            get(v, "control_port", s.stream.control_port);
            get(v, "realtime", s.stream.realtime);
        } else if (k == "seed") {
            if (live) throw std::invalid_argument("seed can only be set at startup");
            s.seed = v.get<uint32_t>();
        } else if (k == "record") {
            if (live) throw std::invalid_argument("record can only be set at startup");
            check_known(json{{"enable", 0}, {"dir", 0}, {"max_samples", 0}}, v, "record");
            get(v, "enable", s.record.enable);
            get(v, "dir", s.record.dir);
            get(v, "max_samples", s.record.max_samples);
        } else if (live) {
            recal |= switches_from_json(s.link, json{{k, v}});   // flat form {"rop_dbm": -13}
        } else {
            throw std::invalid_argument("unknown setting: " + k);
        }
    }
}

}  // namespace

json link_to_json(const Link& l) {
    return {
        F(l, lambda_sig),
        F(l, lambda_lo),
        {"driver", {B(l.driver, enable), F(l.driver, gain), F(l.driver, vsat), F(l.driver, smooth)}},
        {"mzm", {B(l.mzm, enable), F(l.mzm, vpi), F(l.mzm, vbias)}},
        {"fiber", {B(l.fiber, enable), F(l.fiber, length_m), F(l.fiber, D), F(l.fiber, lambda)}},
        {"pd", {B(l.pd, enable), F(l.pd, rop_ref_dbm), F(l.pd, cnr_db),
                {"rin", {B(l.pd.rin, enable), F(l.pd.rin, dbhz)}},
                {"sat", {B(l.pd.sat, enable), F(l.pd.sat, level), F(l.pd.sat, smooth)}}}},
        {"rf", {B(l.rf, enable), F(l.rf, asat), F(l.rf, smooth),
                {"memory", {B(l.rf.memory, enable), F(l.rf.memory, delay_s), B(l.rf.memory, linear),
                            B(l.rf.memory, cubic)}}}},
        {"ripple", {B(l.ripple, enable), F(l.ripple, rho), F(l.ripple, delay_s), F(l.ripple, phase_rad)}},
        {"wireless", {B(l.wireless, enable), F(l.wireless, f_rf), F(l.wireless, distance_m),
                      F(l.wireless, g_tx_dbi), F(l.wireless, g_rx_dbi), F(l.wireless, p_tx_dbm),
                      F(l.wireless, nf_db)}},
        {"det", {B(l.det, enable), F(l.det, bw)}},
        {"adc", {B(l.adc, enable), B(l.adc, bits), F(l.adc, clip_std)}},
        {"ref", {F(l.ref, a0), F(l.ref, bw)}},
    };
}

Link Settings::make_link() const {
    json j = link_to_json(default_link(tx.rs));
    j.merge_patch(stages);
    return apply_link_settings(link_from_json(j), link);
}

Settings settings_from_json(const json& j) {
    Settings s;
    bool recal = false;
    if (j.contains("tx")) apply_sections(s, json{{"tx", j["tx"]}}, false, recal);   // stages need tx.rs
    json rest = j;
    rest.erase("tx");
    apply_sections(s, rest, false, recal);
    return s;
}

Settings load_settings(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open config " + path);
    json j;
    in >> j;
    return settings_from_json(j);
}

json to_json(const Settings& s) {
    return {
        {"tx", tx_to_json(s.tx)},
        {"link", switches_to_json(s.link)},
        {"stages", s.stages},
        {"rx", {{"freeze_rx", s.rx.freeze_rx}, {"calib_symbols", s.rx.calib_symbols},
                {"train_symbols", s.rx.train_symbols}, {"eval_taps", s.rx.eval_taps}}},
        {"samples", {{"taps", s.samples.taps}, {"scale", to_string(s.samples.scale)},
                     {"schedule", to_string(s.samples.schedule)},
                     {"train_len", s.samples.train_len}, {"test_len", s.samples.test_len}}},
        {"stream", {{"rate_sps", s.stream.rate_sps}, {"frame_ms", s.stream.frame_ms},
                    {"host", s.stream.host}, {"port", s.stream.port},
                    {"control_port", s.stream.control_port}, {"realtime", s.stream.realtime}}},
        {"seed", s.seed},
        {"record", {{"enable", s.record.enable}, {"dir", s.record.dir}, {"max_samples", s.record.max_samples}}},
    };
}

bool apply_set(Settings& s, const json& set) {
    Settings t = s;
    bool recal = false;
    apply_sections(t, set, true, recal);
    s = t;
    return recal;
}

}  // namespace arof
