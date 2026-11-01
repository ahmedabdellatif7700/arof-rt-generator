// arof_rx: the receiving end of arof_stream on a host (a laptop in place of the KV260's stream_rx).
// Listens on TCP (or reads a saved stream), checks every frame as tools/fake_kv260.py does, and
// writes what it received to an output directory:
//   stream.raw                 every byte received (re-check later with --file)
//   samples.bin, flags.bin     int32 words per sample / 1 = TEST, as the Pi's recorder
//   train_samples_q8_24.mem,   the words as nn_training_model writes them, split by the TEST flag
//   test_samples_q8_24.mem
//   dataset.mat                the dataset rebuilt from the samples (dataset.h)
//   configs.json, summary.json the CONFIG frames' JSON, the checks and counts
//   cycle_000/, cycle_001/ ... with --cycles K: the same files for each complete cycle
//
// Cycles (schedule train_test): the Pi marks sample i TEST when i % (train_len + test_len) >=
// train_len, i = first_sample + offset, counted from 0 at its start. A cycle is the train_len +
// test_len samples from an index that is a multiple of the period: 7998 TRAIN + 2000 TEST, the
// split nn_training_model gives a 9998-sample dataset. A cycle counts only if it is complete,
// without a gap, and under one config_id; otherwise it is dropped and the next boundary is awaited.
//
//   arof_rx --out DIR [--port 5000] [--duration S] [--samples N] [--cycles K]
//   arof_rx --out DIR --file stream.raw [--samples N] [--cycles K]
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "dataset.h"
#include "stream/frame.h"

using nlohmann::json;
using namespace arof;
namespace fs = std::filesystem;

namespace {

volatile sig_atomic_t g_stop = 0;
void on_signal(int) { g_stop = 1; }

double now_s() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct Cycle {
    size_t pos;             // first sample in the received arrays
    uint64_t first_sample;  // its index in the stream
    uint32_t config_id;
};

// The checks of tools/fake_kv260.py over a byte stream fed in pieces of any size, and the cycles.
class FrameChecker {
public:
    FrameChecker(uint64_t max_samples, size_t max_cycles) : max_samples_(max_samples), max_cycles_(max_cycles) {}

    // false: stop (END, a broken header, the sample or cycle limit)
    bool feed(const uint8_t* p, size_t n, double t) {
        buf_.insert(buf_.end(), p, p + n);
        size_t pos = 0;
        bool go = true;
        while (go && buf_.size() - pos >= sizeof(FrameHeader)) {
            FrameHeader h;
            std::memcpy(&h, buf_.data() + pos, sizeof h);
            if (h.magic != kFrameMagic || h.version != kFrameVersion) {
                error("bad magic/version at byte " + std::to_string(consumed_ + pos));
                go = false;
                break;
            }
            const size_t len = (h.flags & kConfig) ? h.n_samples
                                                   : size_t{h.n_samples} * h.words_per_sample * sizeof(int32_t);
            if (buf_.size() - pos < sizeof h + len) break;
            go = frame(h, buf_.data() + pos + sizeof h, len, t);
            pos += sizeof h + len;
        }
        consumed_ += pos;
        buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(pos));
        return go;
    }

    void error(const std::string& e) {
        if (errors_.size() < 50) errors_.push_back(e);
        ++n_errors_;
    }

    std::vector<int32_t> words;
    std::vector<uint8_t> flags;          // per sample, 1 = TEST
    std::vector<json> configs;
    std::vector<Cycle> cycles;
    std::vector<std::string> errors_;
    uint64_t n_errors_ = 0, frames = 0, recal_frames = 0, cycles_dropped = 0;
    int wps = 0;
    bool ended = false;
    double t_first = 0, t_last = 0;
    uint64_t samples_after_first = 0;    // for the rate: samples after the first data frame
    size_t config_changes = 0;

private:
    bool frame(const FrameHeader& h, const uint8_t* p, size_t len, double t) {
        ++frames;
        if (have_seq_ && h.seq != seq_next_)
            error("sequence gap: " + std::to_string(seq_next_) + " expected, " + std::to_string(h.seq) + " received");
        have_seq_ = true;
        seq_next_ = h.seq + 1;
        if (h.flags & kConfig) {
            try {
                configs.push_back(json::parse(std::string(reinterpret_cast<const char*>(p), len)));
                const json& s = configs.back().at("settings").at("samples");
                train_test_ = s.at("schedule").get<std::string>() == "train_test";
                train_len_ = s.at("train_len").get<uint64_t>();
                period_ = train_len_ + s.at("test_len").get<uint64_t>();
            } catch (const std::exception& e) {
                error(std::string("CONFIG frame: ") + e.what());
            }
            if (h.config_id < cid_) error("config_id went back");
            announced_ = h.config_id;
            return true;
        }
        if (h.flags & kEnd) {
            ended = true;
            return false;
        }
        if (wps == 0) wps = h.words_per_sample;
        else if (h.words_per_sample != wps) error("words_per_sample changed");
        if (have_sample_ && h.first_sample != sample_next_)
            error("sample gap: " + std::to_string(sample_next_) + " expected, " + std::to_string(h.first_sample) +
                  " received");
        have_sample_ = true;
        sample_next_ = h.first_sample + h.n_samples;
        if (h.config_id != cid_) {
            if (h.config_id != announced_) error("config_id " + std::to_string(h.config_id) + " without a CONFIG frame");
            if (frames_data_ && !(h.flags & kRecalibrated)) error("config_id change without RECALIBRATED");
            if (frames_data_) ++config_changes;
            cid_ = h.config_id;
        }
        recal_frames += (h.flags & kRecalibrated) != 0;
        if (frames_data_ == 0) t_first = t;
        else samples_after_first += h.n_samples;
        t_last = t;
        ++frames_data_;

        const bool test = (h.flags & kTest) != 0;
        uint64_t take = h.n_samples;
        if (max_samples_) take = std::min<uint64_t>(take, max_samples_ - flags.size());
        bool go = true;
        if (max_cycles_) {
            for (uint64_t j = 0; j < take; ++j)
                if (!track_cycle(h.first_sample + j, flags.size() + j, h.config_id, test)) {
                    take = j + 1;
                    go = false;
                    break;
                }
        }
        const auto* w = reinterpret_cast<const int32_t*>(p);
        words.insert(words.end(), w, w + take * static_cast<size_t>(h.words_per_sample));
        flags.insert(flags.end(), take, test ? 1 : 0);
        (void)len;
        return go && !(max_samples_ && flags.size() >= max_samples_);
    }

    // one received sample; false once max_cycles complete cycles are in
    bool track_cycle(uint64_t idx, size_t pos, uint32_t config_id, bool test) {
        if (!train_test_ || period_ == 0) {
            if (!warned_) error("--cycles needs samples.schedule train_test in the stream's CONFIG");
            warned_ = true;
            return true;
        }
        if (in_cycle_ && (idx != cyc_.first_sample + (pos - cyc_.pos) || config_id != cyc_.config_id)) {
            in_cycle_ = false;                       // a gap or a settings change: drop it
            ++cycles_dropped;
        }
        if (!in_cycle_ && idx % period_ == 0) {
            in_cycle_ = true;
            cyc_ = {pos, idx, config_id};
        }
        if (!in_cycle_) return true;
        const uint64_t k = idx - cyc_.first_sample;
        if (test != (k >= train_len_)) {
            error("TEST flag of sample " + std::to_string(idx) + " does not follow train_len/test_len");
            in_cycle_ = false;
            ++cycles_dropped;
            return true;
        }
        if (k + 1 == period_) {
            cycles.push_back(cyc_);
            in_cycle_ = false;
            return cycles.size() < max_cycles_;
        }
        return true;
    }

    uint64_t max_samples_;
    size_t max_cycles_;
    std::vector<uint8_t> buf_;
    uint64_t consumed_ = 0, frames_data_ = 0;
    bool have_seq_ = false, have_sample_ = false;
    uint32_t seq_next_ = 0, cid_ = 0, announced_ = 0;
    uint64_t sample_next_ = 0;
    bool train_test_ = false, in_cycle_ = false, warned_ = false;
    uint64_t train_len_ = 0, period_ = 0;
    Cycle cyc_{};
};

int listen_on(int port) {
    // dual stack: ssh -R delivers to "localhost", which may be ::1 or 127.0.0.1
    int fd = ::socket(AF_INET6, SOCK_STREAM, 0);
    const int one = 1, zero = 0;
    if (fd >= 0) {
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        ::setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &zero, sizeof zero);
        sockaddr_in6 a{};
        a.sin6_family = AF_INET6;
        a.sin6_addr = in6addr_any;
        a.sin6_port = htons(static_cast<uint16_t>(port));
        if (::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0 && ::listen(fd, 1) == 0) return fd;
        ::close(fd);
    }
    fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(static_cast<uint16_t>(port));
    if (::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || ::listen(fd, 1) != 0) {
        std::fprintf(stderr, "arof_rx: port %d: %s\n", port, std::strerror(errno));
        std::exit(2);
    }
    return fd;
}

struct LinkInfo {
    double vpp = 0, rop = 0, seed = 0;
    bool grid = true;
};

LinkInfo link_of(const json& config) {
    const json& s = config.at("settings");
    return {s.at("link").at("vpp_mv").get<double>(), s.at("link").at("rop_dbm").get<double>(),
            s.at("seed").get<double>(), s.at("samples").at("scale").get<std::string>() == "grid"};
}

// samples.bin, flags.bin, the .mem files and dataset.mat for samples [pos, pos + n); returns the
// no-EQ EVM, or -1 without samples. Y off the 16QAM grid is reported through `bad_grid`.
double write_set(const fs::path& dir, const std::vector<int32_t>& words, const std::vector<uint8_t>& flags,
                 size_t pos, size_t n, int wps, const LinkInfo& li, bool& bad_grid) {
    fs::create_directories(dir);
    const size_t w = static_cast<size_t>(wps);
    const int32_t* s = words.data() + pos * w;
    std::ofstream(dir / "samples.bin", std::ios::binary)
        .write(reinterpret_cast<const char*>(s), static_cast<std::streamsize>(n * w * sizeof(int32_t)));
    std::ofstream(dir / "flags.bin", std::ios::binary)
        .write(reinterpret_cast<const char*>(flags.data() + pos), static_cast<std::streamsize>(n));
    for (uint8_t which : {0, 1}) {
        std::FILE* f = std::fopen((dir / (which ? "test_samples_q8_24.mem" : "train_samples_q8_24.mem")).c_str(), "w");
        for (size_t i = 0; i < n; ++i)
            if (flags[pos + i] == which)
                for (size_t c = 0; c < w; ++c) std::fprintf(f, "%08X\n", static_cast<uint32_t>(s[i * w + c]));
        std::fclose(f);
    }
    if (n == 0) return -1;
    const Dataset ds = reconstruct(std::vector<int32_t>(s, s + n * w), wps, li.grid);
    if (li.grid)
        for (double y : ds.Y)
            if (y != 1 && y != -1 && y != 3 && y != -3) {
                bad_grid = true;
                break;
            }
    write_mat((dir / "dataset.mat").string(), ds, li.vpp, li.rop, li.seed);
    return ds.evm_no_eq;
}

void usage() {
    std::fprintf(stderr,
                 "usage: arof_rx --out DIR [--port 5000] [--duration S] [--samples N] [--cycles K]\n"
                 "       arof_rx --out DIR --file stream.raw [--samples N] [--cycles K]\n");
    std::exit(2);
}

}  // namespace

int main(int argc, char** argv) {
    int port = 5000;
    double duration = 0;
    uint64_t max_samples = 0;
    size_t max_cycles = 0;
    std::string file, out;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (i + 1 >= argc) usage();
        if (a == "--port") port = std::stoi(argv[++i]);
        else if (a == "--duration") duration = std::stod(argv[++i]);
        else if (a == "--samples") max_samples = std::stoull(argv[++i]);
        else if (a == "--cycles") max_cycles = std::stoull(argv[++i]);
        else if (a == "--file") file = argv[++i];
        else if (a == "--out") out = argv[++i];
        else usage();
    }
    if (out.empty()) usage();
    fs::create_directories(out);

    struct sigaction sa{};
    sa.sa_handler = on_signal;                      // no SA_RESTART: poll and recv return on Ctrl-C
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    FrameChecker chk(max_samples, max_cycles);
    std::vector<uint8_t> chunk(1 << 16);
    double t_conn = 0;
    if (!file.empty()) {
        std::ifstream in(file, std::ios::binary);
        if (!in) {
            std::fprintf(stderr, "arof_rx: cannot open %s\n", file.c_str());
            return 2;
        }
        while (in) {
            in.read(reinterpret_cast<char*>(chunk.data()), static_cast<std::streamsize>(chunk.size()));
            if (in.gcount() <= 0 || !chk.feed(chunk.data(), static_cast<size_t>(in.gcount()), 0)) break;
        }
    } else {
        const int srv = listen_on(port);
        std::fprintf(stderr, "arof_rx: listening on port %d, writing to %s\n", port, out.c_str());
        int c = -1;
        while (!g_stop && c < 0) {
            pollfd p{srv, POLLIN, 0};
            if (::poll(&p, 1, 200) > 0) c = ::accept(srv, nullptr, nullptr);
        }
        ::close(srv);
        if (c < 0) return 1;
        t_conn = now_s();
        std::fprintf(stderr, "arof_rx: connected%s\n",
                     duration > 0 ? (", stopping after " + std::to_string(static_cast<int>(duration)) + " s").c_str()
                                  : ", Ctrl-C or END stops");
        std::ofstream raw(fs::path(out) / "stream.raw", std::ios::binary);
        double t_report = t_conn + 5;
        while (!g_stop && !(duration > 0 && now_s() - t_conn >= duration)) {
            pollfd p{c, POLLIN, 0};
            if (::poll(&p, 1, 200) <= 0) continue;
            const ssize_t k = ::recv(c, chunk.data(), chunk.size(), 0);
            if (k <= 0) {
                if (k < 0 && errno == EINTR) continue;
                std::fprintf(stderr, "arof_rx: connection closed by the sender\n");
                break;
            }
            raw.write(reinterpret_cast<const char*>(chunk.data()), k);
            const double t = now_s();
            if (!chk.feed(chunk.data(), static_cast<size_t>(k), t)) break;
            if (t >= t_report) {
                std::fprintf(stderr, "t %3.0f s: %zu samples, %llu frames, %zu cycles, errors %llu\n", t - t_conn,
                             chk.flags.size(), static_cast<unsigned long long>(chk.frames), chk.cycles.size(),
                             static_cast<unsigned long long>(chk.n_errors_));
                t_report += 5;
            }
        }
        ::close(c);
    }

    // outputs: everything received, then each complete cycle
    const fs::path dir(out);
    const size_t n = chk.flags.size();
    const int wps = chk.wps;
    std::ofstream(dir / "configs.json") << json(chk.configs).dump(2) << "\n";

    LinkInfo first;
    double evm_cal = -1;
    if (!chk.configs.empty()) {
        try {
            first = link_of(chk.configs.front());
            const json& s = chk.configs.front().at("settings");
            if (wps && s.at("samples").at("taps").get<int>() * 2 + 2 != wps) chk.error("taps of CONFIG and frames differ");
            const json& c = chk.configs.front();
            if (c.contains("calibration") && c["calibration"].contains("evm_no_eq"))
                evm_cal = c["calibration"]["evm_no_eq"].get<double>();
        } catch (const std::exception& e) {
            chk.error(std::string("CONFIG frame: ") + e.what());
        }
    } else {
        chk.error("no CONFIG frame: dataset written without link settings");
    }
    bool bad_grid = false;
    const double evm = write_set(dir, chk.words, chk.flags, 0, n, wps, first, bad_grid);

    json cycles = json::array();
    for (size_t i = 0; i < chk.cycles.size(); ++i) {
        const Cycle& c = chk.cycles[i];
        const json* cfg = &chk.configs.front();     // the CONFIG that announced this cycle's config_id
        for (const json& k : chk.configs)
            if (k.value("config_id", 0u) == c.config_id) cfg = &k;
        const LinkInfo li = link_of(*cfg);
        const json& s = cfg->at("settings").at("samples");
        const size_t len = s.at("train_len").get<size_t>() + s.at("test_len").get<size_t>();
        char name[32];
        std::snprintf(name, sizeof name, "cycle_%03zu", i);
        const double e = write_set(dir / name, chk.words, chk.flags, c.pos, len, wps, li, bad_grid);
        cycles.push_back({{"dir", name}, {"first_sample", c.first_sample}, {"config_id", c.config_id},
                          {"samples", len}, {"evm_no_eq", e}});
    }
    if (bad_grid) chk.error("Y not on the 16QAM grid {+-1, +-3}");

    json summary = {{"frames", chk.frames}, {"samples", n}, {"words_per_sample", wps},
                    {"test_samples", std::count(chk.flags.begin(), chk.flags.end(), 1)},
                    {"configs", chk.configs.size()}, {"config_changes", chk.config_changes},
                    {"recalibrated_frames", chk.recal_frames}, {"end", chk.ended},
                    {"cycles", cycles}, {"cycles_dropped", chk.cycles_dropped}};
    if (file.empty() && chk.t_last > chk.t_first) {
        summary["seconds"] = chk.t_last - t_conn;
        summary["rate_sps"] = static_cast<double>(chk.samples_after_first) / (chk.t_last - chk.t_first);
    }
    if (evm >= 0) summary["evm_no_eq"] = evm;
    summary["errors"] = chk.errors_;
    summary["n_errors"] = chk.n_errors_;
    std::ofstream(dir / "summary.json") << summary.dump(2) << "\n";

    std::printf("frames %llu, samples %zu (%d words each, %lld TEST), CONFIG %zu, RECALIBRATED frames %llu, END %s\n",
                static_cast<unsigned long long>(chk.frames), n, wps,
                static_cast<long long>(summary["test_samples"].get<int64_t>()), chk.configs.size(),
                static_cast<unsigned long long>(chk.recal_frames), chk.ended ? "yes" : "no");
    if (summary.contains("rate_sps"))
        std::printf("received for %.1f s, %.1f samples/s\n", summary["seconds"].get<double>(),
                    summary["rate_sps"].get<double>());
    if (!chk.configs.empty())
        std::printf("CONFIG: %g mV, ROP %g dBm, seed %g, scale %s%s\n", first.vpp, first.rop, first.seed,
                    first.grid ? "grid" : "unit",
                    evm_cal >= 0 ? (", calibration no-EQ EVM " + std::to_string(100 * evm_cal).substr(0, 5) + " %").c_str()
                                 : "");
    if (chk.config_changes)
        std::printf("note: the settings changed %zu time(s); the top-level dataset.mat carries the first CONFIG's "
                    "link (each cycle carries its own)\n", chk.config_changes);
    if (evm >= 0) std::printf("all samples: no-EQ EVM %.2f %%\n", 100 * evm);
    for (const json& c : cycles)
        std::printf("%s: samples %llu..%llu (config %u), no-EQ EVM %.2f %%\n", c["dir"].get<std::string>().c_str(),
                    static_cast<unsigned long long>(c["first_sample"].get<uint64_t>()),
                    static_cast<unsigned long long>(c["first_sample"].get<uint64_t>() + c["samples"].get<uint64_t>() - 1),
                    c["config_id"].get<unsigned>(), 100 * c["evm_no_eq"].get<double>());
    if (max_cycles)
        std::printf("cycles: %zu of %zu complete, %llu dropped (gap or settings change)\n", chk.cycles.size(),
                    max_cycles, static_cast<unsigned long long>(chk.cycles_dropped));
    std::printf("written to %s: %ssamples.bin, flags.bin, *_q8_24.mem, dataset.mat, configs.json, summary.json%s\n",
                out.c_str(), file.empty() ? "stream.raw, " : "", chk.cycles.empty() ? "" : ", cycle_*/");
    std::printf("protocol errors: %s\n", chk.n_errors_ ? "" : "none");
    for (const auto& e : chk.errors_) std::printf("  %s\n", e.c_str());
    return chk.n_errors_ ? 1 : 0;
}
