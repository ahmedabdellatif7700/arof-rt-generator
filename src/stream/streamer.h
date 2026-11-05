// Real-time streaming (docs/DEMO.md section 4): three threads
//   generator  TransmitReceive -> SampleFormer -> SampleRing (1 s of samples ahead of the sender);
//              applies live settings changes (recalibration, new config_id) between chunks;
//              records the first record.max_samples sent samples when record.enable
//   sender     TCP client to the KV260, frames of rate_sps * frame_ms samples, paced by an
//              absolute-time loop (clock_nanosleep TIMER_ABSTIME), optional SCHED_FIFO
//   control    JSON lines on control_port: {"set": {...}}, {"status": true}, {"stop": true}
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

#include "core/config.h"
#include "link/transmit_receive.h"
#include "stream/recorder.h"
#include "stream/sample_former.h"
#include "stream/sample_ring.h"

namespace arof {

struct StreamStats {
    std::atomic<uint64_t> frames{0}, samples{0}, bytes{0};
    std::atomic<uint64_t> late_samples{0};      // due but not ready (generator behind)
    std::atomic<uint64_t> slips{0};             // pacing re-anchored after falling > 100 ms behind
    std::atomic<uint64_t> reconnects{0};
    std::atomic<uint64_t> lost_samples{0};      // in a frame whose send failed (connection lost)
    std::atomic<uint64_t> generated{0};         // samples formed
    std::atomic<double> gen_busy{0};            // generator busy fraction (last second)
};

class Streamer {
public:
    explicit Streamer(const Settings& s);
    ~Streamer();

    // runs until stop() (control port or signal); returns the exit code
    int run();
    void stop() { stop_.store(true); }

    // control: apply a settings change in the generator thread; blocks until done
    nlohmann::json request_set(const nlohmann::json& set);
    nlohmann::json status() const;

private:
    void generator_thread();
    void sender_thread();
    void control_thread();
    void apply_pending();

    Settings s_;
    std::unique_ptr<TransmitReceive> trx_;
    std::unique_ptr<SampleFormer> former_;
    std::unique_ptr<SampleRing> ring_;
    std::unique_ptr<Recorder> recorder_;

    std::atomic<bool> stop_{false};
    std::atomic<double> rate_sps_;
    std::atomic<uint32_t> config_id_{1};
    std::atomic<bool> streaming_{false};        // generation started (calibration done)

    // settings JSON per config_id, for the CONFIG frames
    mutable std::mutex cfg_mu_;
    std::map<uint32_t, std::string> config_json_;
    nlohmann::json last_report_;

    // pending control requests (generator thread applies them)
    struct Request {
        nlohmann::json set;
        nlohmann::json reply;
        bool done = false;
    };
    std::mutex req_mu_;
    std::condition_variable req_cv_;
    std::deque<std::shared_ptr<Request>> requests_;

    StreamStats stats_;
    std::string error_;
};

// --bench: generation without sending, samples per second
int bench(const Settings& s, double seconds);

nlohmann::json report_json(const CalibrationReport& r);

}  // namespace arof
