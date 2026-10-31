// arof_stream: real-time A-RoF sample generator (port of main_demo.m steps 1-7).
// Milestone 1 skeleton: loads and prints the settings. The link, receiver, recorder,
// sender and control port follow in later milestones.

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

#include <fftw3.h>
#include <nlohmann/json.hpp>

namespace {

void usage(const char* prog) {
    std::printf(
        "usage: %s [--config FILE] [--version]\n"
        "  --config FILE   settings (default /config/default.json)\n"
        "  --version       print version and library info\n",
        prog);
}

}  // namespace

int main(int argc, char** argv) {
    std::string config_path = "/config/default.json";
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--config") && i + 1 < argc) {
            config_path = argv[++i];
        } else if (!std::strcmp(argv[i], "--version")) {
            std::printf("arof_stream 0.1.0, %s\n", fftw_version);
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

    std::ifstream in(config_path);
    if (!in) {
        std::fprintf(stderr, "cannot open config %s\n", config_path.c_str());
        return 1;
    }
    nlohmann::json cfg;
    try {
        in >> cfg;
    } catch (const nlohmann::json::exception& e) {
        std::fprintf(stderr, "%s: %s\n", config_path.c_str(), e.what());
        return 1;
    }
    std::cout << "config " << config_path << "\n" << cfg.dump(2) << "\n";
    std::cout << "skeleton build: link generation is not implemented yet\n";
    return 0;
}
