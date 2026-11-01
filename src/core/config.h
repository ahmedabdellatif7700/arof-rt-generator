// Settings (config/default.json):
//   "link"     main_demo.m section 0: vpp_mv, rop_dbm and the impairment switches
//   "stages"   optional overrides of default_link.m, same structure and names,
//              e.g. {"pd": {"cnr_db": 40}, "wireless": {"enable": false}}
//   "tx"       optional overrides of default_tx.m (startup only)
//   "rx", "samples", "stream", "seed", "record"
// Infinities are the strings "inf" and "-inf".
#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include "core/link_settings.h"
#include "link/default_tx.h"
#include "link/transmit_receive.h"
#include "stream/sample_former.h"

namespace arof {

struct StreamSettings {
    double rate_sps = 10000;
    double frame_ms = 1;
    std::string host = "192.168.1.10";
    int port = 5000;
    int control_port = 5001;
    bool realtime = false;          // SCHED_FIFO for the sender thread
};

struct RecordSettings {
    bool enable = false;
    std::string dir = "/records";
    uint64_t max_samples = 49991;   // one demo cycle: 5000 train + 44 991 test
};

struct Settings {
    TxConfig tx;
    nlohmann::json stages = nlohmann::json::object();
    LinkSettings link;
    RxSettings rx;
    SampleSettings samples;
    StreamSettings stream;
    uint32_t seed = 2017;
    RecordSettings record;

    // default_link(rs) with the stage overrides, then the switches of "link"
    Link make_link() const;
};

Settings load_settings(const std::string& path);
Settings settings_from_json(const nlohmann::json& j);
nlohmann::json to_json(const Settings& s);
// default_link fields as JSON (same names as the MATLAB struct)
nlohmann::json link_to_json(const Link& l);

// Live change: {"link": {...}}, {"stages": {...}}, {"rx": {...}}, {"stream": {"rate_sps": R}},
// or the flat form {"rop_dbm": -13} for "link" keys. All or nothing. Returns true when the
// link or the receiver changed (recalibration needed).
bool apply_set(Settings& s, const nlohmann::json& set);

}  // namespace arof
