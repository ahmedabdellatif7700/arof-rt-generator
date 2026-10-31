#include "stream/streamer.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <thread>
#include <vector>

#include "stream/frame.h"

namespace arof {

using nlohmann::json;

namespace {

constexpr size_t kChunkSymbols = 128;
constexpr double kMaxBehindS = 0.1;      // the sender re-anchors its clock beyond this deficit

double now_s() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

size_t next_pow2(size_t v) {
    size_t p = 1;
    while (p < v) p <<= 1;
    return p;
}

int64_t mono_ns() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000 + ts.tv_nsec;
}

void sleep_until_ns(int64_t t) {
    timespec ts{static_cast<time_t>(t / 1000000000), static_cast<long>(t % 1000000000)};
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr) == EINTR) {
    }
}

bool send_all(int fd, const void* data, size_t n) {
    const auto* p = static_cast<const unsigned char*>(data);
    while (n > 0) {
        const ssize_t k = ::send(fd, p, n, MSG_NOSIGNAL);
        if (k < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        p += k;
        n -= static_cast<size_t>(k);
    }
    return true;
}

int connect_to(const std::string& host, int port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0) return -1;
    int fd = -1;
    for (addrinfo* a = res; a; a = a->ai_next) {
        fd = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0) continue;
        if (::connect(fd, a->ai_addr, a->ai_addrlen) == 0) break;
        ::close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd >= 0) {
        const int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    }
    return fd;
}

}  // namespace

json report_json(const CalibrationReport& r) {
    auto inf = [](double v) { return std::isinf(v) ? json(v > 0 ? "inf" : "-inf") : json(v); };
    return {
        {"first_symbol", r.first_symbol},
        {"k_awg", r.k_awg},
        {"vd_pp", r.vd_pp},
        {"adc", {{"lo", r.adc.lo}, {"step", r.adc.step}, {"levels", r.adc.levels},
                 {"clip_lo", inf(r.adc.clip_lo)}, {"clip_hi", inf(r.adc.clip_hi)}}},
        {"r_mean", r.rx.r_mean},
        {"offset", r.rx.offset + 1},          // MATLAB's 1-based offset
        {"gain", {r.rx.gain.real(), r.rx.gain.imag()}},
        {"corr", r.corr},
        {"rx_frozen", r.rx_frozen},
        {"pd_cnr_db", r.pd_cnr_db},
        {"ue_cnr_db", r.ue_cnr_db},
        {"evm_no_eq", r.quality.evm},
        {"ber_no_eq", r.quality.ber},
        {"n_test", r.quality.n},
        {"fiber_trunc", r.fiber_trunc},
        {"seconds", r.seconds},
    };
}

Streamer::Streamer(const Settings& s) : s_(s), rate_sps_(s.stream.rate_sps) {
    trx_ = std::make_unique<TransmitReceive>(s_.tx, s_.make_link(), s_.link.vpp_mv * 1e-3, s_.link.rop_dbm, s_.rx,
                                             s_.seed);
    former_ = std::make_unique<SampleFormer>(s_.samples);
    if (s_.record.enable)
        recorder_ = std::make_unique<Recorder>(s_.record.dir, s_.record.max_samples, former_->words_per_sample());
}

Streamer::~Streamer() = default;

json Streamer::request_set(const json& set) {
    auto r = std::make_shared<Request>();
    r->set = set;
    std::unique_lock<std::mutex> lk(req_mu_);
    requests_.push_back(r);
    req_cv_.wait(lk, [&] { return r->done || stop_.load(); });
    return r->done ? r->reply : json{{"ok", false}, {"error", "stopping"}};
}

void Streamer::apply_pending() {
    std::deque<std::shared_ptr<Request>> todo;
    {
        std::lock_guard<std::mutex> lk(req_mu_);
        todo.swap(requests_);
    }
    for (auto& r : todo) {
        try {
            Settings t = s_;
            const bool recal = apply_set(t, r->set);
            json reply = {{"ok", true}};
            if (recal) {
                const auto& rep = trx_->update(t.make_link(), t.link.vpp_mv * 1e-3, t.link.rop_dbm, t.rx);
                const uint32_t id = config_id_.load() + 1;
                {
                    std::lock_guard<std::mutex> lk(cfg_mu_);
                    last_report_ = report_json(rep);
                    config_json_[id] = json{{"config_id", id}, {"settings", to_json(t)},
                                            {"calibration", last_report_}}.dump();
                }
                config_id_.store(id);
                reply["config_id"] = id;
                reply["calibration"] = report_json(rep);
                std::printf("config %u: %s -> recalibrated in %.2f s, no-EQ EVM %.2f %%\n", id, r->set.dump().c_str(),
                            rep.seconds, 100 * rep.quality.evm);
            }
            if (t.stream.rate_sps != s_.stream.rate_sps) {
                rate_sps_.store(t.stream.rate_sps);
                std::printf("rate %.0f samples/s\n", t.stream.rate_sps);
            }
            s_ = t;
            r->reply = reply;
        } catch (const std::exception& e) {
            r->reply = {{"ok", false}, {"error", e.what()}};
        }
        std::lock_guard<std::mutex> lk(req_mu_);
        r->done = true;
        req_cv_.notify_all();
    }
}

void Streamer::generator_thread() {
    std::vector<RxSymbol> syms;
    std::vector<int32_t> words;
    const int wps = former_->words_per_sample();
    double busy = 0, t_win = now_s();
    // ring depth ahead of the sender: 1 s, and at least 1.5 x the calibration time, so that a
    // recalibration (the generator is busy with it) does not starve the sender
    const double depth_s = std::max(1.0, 1.5 * trx_->report().seconds);
    while (!stop_.load()) {
        const double t0 = now_s();
        apply_pending();
        const size_t target = std::min(ring_->capacity(), static_cast<size_t>(rate_sps_.load() * depth_s));
        if (ring_->size() + kChunkSymbols > target) {
            busy += now_s() - t0;
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        } else {
            syms.clear();
            trx_->generate(kChunkSymbols, syms);
            for (const RxSymbol& sym : syms) {
                const SampleTag tag{config_id_.load(), former_->next_is_test()};
                if (recorder_) recorder_->symbol(sym);
                words.clear();
                if (!former_->push(sym, words)) continue;
                if (recorder_) recorder_->sample(words.data(), tag.test);
                while (!ring_->push(words.data(), tag) && !stop_.load())
                    std::this_thread::sleep_for(std::chrono::microseconds(200));
                stats_.generated.fetch_add(1);
            }
            busy += now_s() - t0;
        }
        if (now_s() - t_win >= 1.0) {
            stats_.gen_busy.store(busy / (now_s() - t_win));
            busy = 0;
            t_win = now_s();
        }
        streaming_.store(true);
        (void)wps;
    }
    std::lock_guard<std::mutex> lk(req_mu_);   // release waiting control requests
    req_cv_.notify_all();
}

void Streamer::sender_thread() {
    if (s_.stream.realtime) {
        sched_param sp{};
        sp.sched_priority = 50;
        if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0)
            std::fprintf(stderr, "warning: SCHED_FIFO not permitted (run with --cap-add SYS_NICE)\n");
    }
    const int wps = ring_->words_per_sample();
    const uint32_t seed = s_.seed;
    uint32_t seq = 0, sent_config = 0;
    uint64_t first_sample = 0;
    bool recal_flag = false;
    std::vector<unsigned char> buf;
    std::vector<int32_t> words;
    int fd = -1;

    auto send_frame = [&](uint16_t flags, uint32_t n, uint16_t w, uint32_t cid, const void* payload, size_t bytes) {
        FrameHeader h;
        h.flags = flags;
        h.seq = seq;
        h.first_sample = first_sample;
        h.n_samples = n;
        h.words_per_sample = w;
        h.config_id = cid;
        h.seed = seed;
        buf.resize(sizeof h + bytes);
        write_header(h, buf.data());
        if (bytes) std::memcpy(buf.data() + sizeof h, payload, bytes);
        if (!send_all(fd, buf.data(), buf.size())) return false;
        ++seq;
        stats_.frames.fetch_add(1);
        stats_.bytes.fetch_add(buf.size());
        return true;
    };
    auto send_config = [&](uint32_t cid) {
        std::string js;
        {
            std::lock_guard<std::mutex> lk(cfg_mu_);
            js = config_json_[cid];
        }
        sent_config = cid;
        return send_frame(kConfig, static_cast<uint32_t>(js.size()), 0, cid, js.data(), js.size());
    };
    auto connect = [&]() {
        while (!stop_.load()) {
            fd = connect_to(s_.stream.host, s_.stream.port);
            if (fd >= 0) {
                std::printf("connected to %s:%d\n", s_.stream.host.c_str(), s_.stream.port);
                if (send_config(ring_->size() ? ring_->front_tag().config_id : config_id_.load())) return true;
                ::close(fd);
                fd = -1;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        return false;
    };

    // prefill: the ring holds its target depth before the clock starts
    while (!stop_.load() && ring_->size() < std::min(ring_->capacity(), static_cast<size_t>(rate_sps_.load())))
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (!connect()) return;

    double rate = rate_sps_.load();
    const int64_t frame_ns = static_cast<int64_t>(std::llround(s_.stream.frame_ms * 1e6));
    int64_t anchor = mono_ns();
    uint64_t k = 0, sent_since_anchor = 0;
    while (!stop_.load()) {
        ++k;
        const int64_t deadline = anchor + static_cast<int64_t>(k) * frame_ns;
        sleep_until_ns(deadline);
        if (rate_sps_.load() != rate) {                   // rate change: restart the schedule here
            rate = rate_sps_.load();
            anchor = deadline;
            k = 0;
            sent_since_anchor = 0;
            continue;
        }
        const auto due_total = static_cast<uint64_t>(std::floor(rate * static_cast<double>(k) * frame_ns / 1e9));
        uint64_t due = due_total - sent_since_anchor;
        const size_t avail = ring_->size();
        if (avail < due) {
            stats_.late_samples.fetch_add(due - avail);
            if (static_cast<double>(due - avail) > kMaxBehindS * rate) {   // give up the backlog
                stats_.slips.fetch_add(1);
                anchor = deadline;
                k = 0;
                sent_since_anchor = 0;
                due = avail;
            }
        }
        size_t n = std::min<uint64_t>(due, avail);
        sent_since_anchor += n;
        // frames: runs of samples with the same config_id and TEST flag
        while (n > 0) {
            const SampleTag tag = ring_->front_tag();
            size_t run = 1;
            while (run < n) {
                const SampleTag& t = ring_->front_tag(run);
                if (t.config_id != tag.config_id || t.test != tag.test) break;
                ++run;
            }
            if (tag.config_id != sent_config) {
                if (!send_config(tag.config_id)) goto reconnect;
                recal_flag = true;
            }
            words.resize(run * wps);
            ring_->pop(run, words.data());
            {
                const uint16_t flags = static_cast<uint16_t>((tag.test ? kTest : 0) | (recal_flag ? kRecalibrated : 0));
                if (!send_frame(flags, static_cast<uint32_t>(run), static_cast<uint16_t>(wps), tag.config_id,
                                words.data(), words.size() * sizeof(int32_t)))
                    goto reconnect;
            }
            recal_flag = false;
            first_sample += run;
            stats_.samples.fetch_add(run);
            n -= run;
            continue;
        reconnect:
            std::fprintf(stderr, "connection lost, reconnecting\n");
            ::close(fd);
            fd = -1;
            stats_.reconnects.fetch_add(1);
            if (!connect()) return;
            anchor = mono_ns();
            k = 0;
            sent_since_anchor = 0;
            break;
        }
    }
    if (fd >= 0) {
        send_frame(kEnd, 0, static_cast<uint16_t>(wps), config_id_.load(), nullptr, 0);
        ::close(fd);
    }
}

void Streamer::control_thread() {
    const int srv = ::socket(AF_INET, SOCK_STREAM, 0);
    const int one = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(s_.stream.control_port));
    if (::bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || ::listen(srv, 4) != 0) {
        std::fprintf(stderr, "warning: control port %d: %s\n", s_.stream.control_port, std::strerror(errno));
        ::close(srv);
        return;
    }
    std::printf("control port %d\n", s_.stream.control_port);
    while (!stop_.load()) {
        pollfd p{srv, POLLIN, 0};
        if (poll(&p, 1, 200) <= 0) continue;
        const int c = ::accept(srv, nullptr, nullptr);
        if (c < 0) continue;
        std::string pending;
        char chunk[4096];
        while (!stop_.load()) {
            pollfd pc{c, POLLIN, 0};
            if (poll(&pc, 1, 200) <= 0) continue;
            const ssize_t k = ::recv(c, chunk, sizeof chunk, 0);
            if (k <= 0) break;
            pending.append(chunk, static_cast<size_t>(k));
            size_t nl;
            while ((nl = pending.find('\n')) != std::string::npos) {
                const std::string line = pending.substr(0, nl);
                pending.erase(0, nl + 1);
                if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
                json reply;
                try {
                    const json cmd = json::parse(line);
                    if (cmd.contains("set")) reply = request_set(cmd["set"]);
                    else if (cmd.contains("status")) reply = status();
                    else if (cmd.contains("stop")) {
                        reply = {{"ok", true}, {"stopping", true}};
                        stop_.store(true);
                    } else reply = {{"ok", false}, {"error", "expected set, status or stop"}};
                } catch (const std::exception& e) {
                    reply = {{"ok", false}, {"error", e.what()}};
                }
                const std::string out = reply.dump() + "\n";
                send_all(c, out.data(), out.size());
            }
        }
        ::close(c);
    }
    ::close(srv);
}

json Streamer::status() const {
    json j = {
        {"ok", true},
        {"config_id", config_id_.load()},
        {"rate_sps", rate_sps_.load()},
        {"frames", stats_.frames.load()},
        {"samples", stats_.samples.load()},
        {"bytes", stats_.bytes.load()},
        {"late_samples", stats_.late_samples.load()},
        {"slips", stats_.slips.load()},
        {"reconnects", stats_.reconnects.load()},
        {"generated", stats_.generated.load()},
        {"ring", ring_ ? ring_->size() : 0},
        {"generator_busy", stats_.gen_busy.load()},
    };
    std::lock_guard<std::mutex> lk(cfg_mu_);
    j["calibration"] = last_report_;
    return j;
}

int Streamer::run() {
    const auto& rep = trx_->calibrate();
    {
        std::lock_guard<std::mutex> lk(cfg_mu_);
        last_report_ = report_json(rep);
        config_json_[1] = json{{"config_id", 1}, {"settings", to_json(s_)}, {"calibration", last_report_}}.dump();
    }
    std::printf("calibrated in %.2f s: no-EQ EVM %.2f %%, BER %.2e (%zu test symbols)\n", rep.seconds,
                100 * rep.quality.evm, rep.quality.ber, rep.quality.n);
    const double depth_s = std::max(1.0, 1.5 * rep.seconds);
    const size_t cap = next_pow2(static_cast<size_t>(std::max(s_.stream.rate_sps, 10000.0) * (depth_s + 0.5)));
    ring_ = std::make_unique<SampleRing>(cap, former_->words_per_sample());
    std::printf("streaming %.0f samples/s to %s:%d, frames of %g ms, ring %.1f s\n", s_.stream.rate_sps,
                s_.stream.host.c_str(), s_.stream.port, s_.stream.frame_ms, depth_s);

    std::thread gen(&Streamer::generator_thread, this);
    std::thread snd(&Streamer::sender_thread, this);
    std::thread ctl(&Streamer::control_thread, this);
    const double t_start = now_s();
    uint64_t last_samples = 0;
    double last_t = t_start;
    while (!stop_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (now_s() - last_t >= 5) {
            const uint64_t n = stats_.samples.load();
            std::printf("t %.0f s: %llu samples, %.1f samples/s, ring %zu, late %llu, slips %llu, generator %.0f %%\n",
                        now_s() - t_start, static_cast<unsigned long long>(n), (n - last_samples) / (now_s() - last_t),
                        ring_->size(), static_cast<unsigned long long>(stats_.late_samples.load()),
                        static_cast<unsigned long long>(stats_.slips.load()), 100 * stats_.gen_busy.load());
            std::fflush(stdout);
            last_samples = n;
            last_t = now_s();
        }
    }
    gen.join();
    snd.join();
    ctl.join();
    if (recorder_) {
        recorder_->close({{"settings", to_json(s_)}, {"calibration", last_report_}, {"taps", s_.samples.taps},
                          {"scale", to_string(s_.samples.scale)}});
        std::printf("recorded %llu samples to %s\n", static_cast<unsigned long long>(recorder_->samples()),
                    recorder_->dir().c_str());
    }
    std::printf("stopped: %llu frames, %llu samples\n", static_cast<unsigned long long>(stats_.frames.load()),
                static_cast<unsigned long long>(stats_.samples.load()));
    return 0;
}

int bench(const Settings& s, double seconds) {
    TransmitReceive trx(s.tx, s.make_link(), s.link.vpp_mv * 1e-3, s.link.rop_dbm, s.rx, s.seed);
    const auto& rep = trx.calibrate();
    std::printf("calibration: %zu + %d symbols in %.2f s\n", s.rx.calib_symbols, 2 * s.tx.rrc_span, rep.seconds);
    SampleFormer former(s.samples);
    std::vector<RxSymbol> syms;
    std::vector<int32_t> words;
    uint64_t n = 0;
    const double t0 = now_s();
    double t = t0;
    while (t - t0 < seconds) {
        syms.clear();
        trx.generate(1024, syms);
        for (const RxSymbol& sym : syms) {
            words.clear();
            n += former.push(sym, words);
        }
        t = now_s();
    }
    const double rate = n / (t - t0);
    std::printf("bench: %llu samples in %.2f s = %.0f samples/s (%.2f M simulation samples/s at %d per symbol)\n",
                static_cast<unsigned long long>(n), t - t0, rate, rate * s.tx.sps() / 1e6, s.tx.sps());
    std::printf("       rate_sps limit with a 2x margin: %.0f samples/s\n", rate / 2);
    return 0;
}

}  // namespace arof
