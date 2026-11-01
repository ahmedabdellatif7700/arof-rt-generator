// arof_rx: the receiving end of arof_stream on a host (a laptop in place of the KV260's stream_rx).
// Listens on TCP (or reads a saved stream), checks every frame as tools/fake_kv260.py does, and
// writes what it received to an output directory:
//   stream.raw                 every byte received (re-check later with --file)
//   samples.bin, flags.bin     int32 words per sample / 1 = TEST, as the Pi's recorder
//   train_samples_q8_24.mem,   the words as nn_training_model writes them, split by the TEST flag
//   test_samples_q8_24.mem
//   dataset.mat                the dataset rebuilt from the samples (dataset.h)
//   configs.json, summary.json the CONFIG frames' JSON, the checks and counts
//
//   arof_rx --out DIR [--port 5000] [--duration S] [--samples N]
//   arof_rx --out DIR --file stream.raw [--samples N]
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

// The checks of tools/fake_kv260.py over a byte stream fed in pieces of any size.
class FrameChecker {
public:
    explicit FrameChecker(uint64_t max_samples) : max_samples_(max_samples) {}

    // false: stop (END, a broken header, the sample limit)
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
    std::vector<std::string> errors_;
    uint64_t n_errors_ = 0, frames = 0, recal_frames = 0;
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
            } catch (const std::exception& e) {
                error(std::string("CONFIG frame: bad JSON: ") + e.what());
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

        uint64_t take = h.n_samples;
        if (max_samples_) take = std::min<uint64_t>(take, max_samples_ - flags.size());
        const auto* w = reinterpret_cast<const int32_t*>(p);
        words.insert(words.end(), w, w + take * static_cast<size_t>(h.words_per_sample));
        flags.insert(flags.end(), take, (h.flags & kTest) ? 1 : 0);
        (void)len;
        return !(max_samples_ && flags.size() >= max_samples_);
    }

    uint64_t max_samples_;
    std::vector<uint8_t> buf_;
    uint64_t consumed_ = 0, frames_data_ = 0;
    bool have_seq_ = false, have_sample_ = false;
    uint32_t seq_next_ = 0, cid_ = 0, announced_ = 0;
    uint64_t sample_next_ = 0;
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

void write_mem(const fs::path& path, const std::vector<int32_t>& words, const std::vector<uint8_t>& flags, int wps,
               uint8_t which) {
    std::FILE* f = std::fopen(path.c_str(), "w");
    for (size_t i = 0; i < flags.size(); ++i)
        if (flags[i] == which)
            for (int c = 0; c < wps; ++c)
                std::fprintf(f, "%08X\n", static_cast<uint32_t>(words[i * static_cast<size_t>(wps) + c]));
    std::fclose(f);
}

void usage() {
    std::fprintf(stderr,
                 "usage: arof_rx --out DIR [--port 5000] [--duration S] [--samples N]\n"
                 "       arof_rx --out DIR --file stream.raw [--samples N]\n");
    std::exit(2);
}

}  // namespace

int main(int argc, char** argv) {
    int port = 5000;
    double duration = 0;
    uint64_t max_samples = 0;
    std::string file, out;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (i + 1 >= argc) usage();
        if (a == "--port") port = std::stoi(argv[++i]);
        else if (a == "--duration") duration = std::stod(argv[++i]);
        else if (a == "--samples") max_samples = std::stoull(argv[++i]);
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

    FrameChecker chk(max_samples);
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
                std::fprintf(stderr, "t %3.0f s: %zu samples, %llu frames, errors %llu\n", t - t_conn,
                             chk.flags.size(), static_cast<unsigned long long>(chk.frames),
                             static_cast<unsigned long long>(chk.n_errors_));
                t_report += 5;
            }
        }
        ::close(c);
    }

    // outputs
    const fs::path dir(out);
    const size_t n = chk.flags.size();
    const int wps = chk.wps;
    {
        std::ofstream f(dir / "samples.bin", std::ios::binary);
        f.write(reinterpret_cast<const char*>(chk.words.data()),
                static_cast<std::streamsize>(chk.words.size() * sizeof(int32_t)));
        std::ofstream g(dir / "flags.bin", std::ios::binary);
        g.write(reinterpret_cast<const char*>(chk.flags.data()), static_cast<std::streamsize>(n));
    }
    write_mem(dir / "train_samples_q8_24.mem", chk.words, chk.flags, wps, 0);
    write_mem(dir / "test_samples_q8_24.mem", chk.words, chk.flags, wps, 1);
    std::ofstream(dir / "configs.json") << json(chk.configs).dump(2) << "\n";

    json summary = {{"frames", chk.frames}, {"samples", n}, {"words_per_sample", wps},
                    {"test_samples", std::count(chk.flags.begin(), chk.flags.end(), 1)},
                    {"configs", chk.configs.size()}, {"config_changes", chk.config_changes},
                    {"recalibrated_frames", chk.recal_frames}, {"end", chk.ended}};
    if (file.empty() && chk.t_last > chk.t_first) {
        summary["seconds"] = chk.t_last - t_conn;
        summary["rate_sps"] = static_cast<double>(chk.samples_after_first) / (chk.t_last - chk.t_first);
    }

    double vpp = 0, rop = 0, seed = 0, evm_cal = -1;
    bool grid = true;
    if (!chk.configs.empty()) {
        const json& c = chk.configs.front();
        const json& s = c.at("settings");
        vpp = s.at("link").at("vpp_mv").get<double>();
        rop = s.at("link").at("rop_dbm").get<double>();
        seed = s.at("seed").get<double>();
        grid = s.at("samples").at("scale").get<std::string>() == "grid";
        if (s.at("samples").at("taps").get<int>() * 2 + 2 != wps && wps) chk.error("taps of CONFIG and frames differ");
        if (c.contains("calibration") && c["calibration"].contains("evm_no_eq"))
            evm_cal = c["calibration"]["evm_no_eq"].get<double>();
    } else {
        chk.error("no CONFIG frame: dataset written without link settings");
    }
    if (n > 0) {
        const Dataset ds = reconstruct(chk.words, wps, grid);
        if (grid)
            for (double y : ds.Y)
                if (y != 1 && y != -1 && y != 3 && y != -3) {
                    chk.error("Y not on the 16QAM grid {+-1, +-3}");
                    break;
                }
        if (chk.config_changes)
            std::printf("note: the settings changed %zu time(s) during the capture; dataset.mat carries the "
                        "first CONFIG's link\n", chk.config_changes);
        write_mat((dir / "dataset.mat").string(), ds, vpp, rop, seed);
        summary["evm_no_eq"] = ds.evm_no_eq;
    }
    summary["errors"] = chk.errors_;
    summary["n_errors"] = chk.n_errors_;
    std::ofstream(dir / "summary.json") << summary.dump(2) << "\n";

    std::printf("frames %llu, samples %zu (%d words each), CONFIG %zu, RECALIBRATED frames %llu, END %s\n",
                static_cast<unsigned long long>(chk.frames), n, wps, chk.configs.size(),
                static_cast<unsigned long long>(chk.recal_frames), chk.ended ? "yes" : "no");
    if (summary.contains("rate_sps"))
        std::printf("received for %.1f s, %.1f samples/s\n", summary["seconds"].get<double>(),
                    summary["rate_sps"].get<double>());
    if (!chk.configs.empty())
        std::printf("CONFIG: %g mV, ROP %g dBm, seed %g, scale %s%s\n", vpp, rop, seed, grid ? "grid" : "unit",
                    evm_cal >= 0 ? (", calibration no-EQ EVM " + std::to_string(100 * evm_cal).substr(0, 5) + " %").c_str()
                                 : "");
    if (n > 0)
        std::printf("dataset: %zu samples, no-EQ EVM from the samples %.2f %%\n", n,
                    100 * summary["evm_no_eq"].get<double>());
    std::printf("written to %s: %ssamples.bin, flags.bin, *_q8_24.mem, dataset.mat, configs.json, summary.json\n",
                out.c_str(), file.empty() ? "stream.raw, " : "");
    std::printf("protocol errors: %s\n", chk.n_errors_ ? "" : "none");
    for (const auto& e : chk.errors_) std::printf("  %s\n", e.c_str());
    return chk.n_errors_ ? 1 : 0;
}
