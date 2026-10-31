// Recorder: the first max_samples samples of the stream, as
//   train_samples_q8_24.mem, test_samples_q8_24.mem   one word per line, 8 uppercase hex digits,
//                                                      two's complement (nn_training_model)
//   samples.bin    int32 LE, words_per_sample words per sample, in stream order
//   flags.bin      uint8 per sample, 1 = TEST
//   symbols.bin    float64 LE per symbol: Re x, Im x, Re d, Im d, idx (unit power), every symbol
//                  that feeds a recorded sample (max_samples + taps - 1)
//   meta.json      settings, calibration, counts
// tools/export_dataset.py turns a recording into the layout of arof_training_data.mat.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

#include <nlohmann/json.hpp>

#include "link/receiver.h"

namespace arof {

class Recorder {
public:
    Recorder(const std::string& dir, uint64_t max_samples, int words_per_sample);
    ~Recorder();
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    // in stream order: each symbol, then the sample it completed (if any)
    void symbol(const RxSymbol& s);
    void sample(const int32_t* words, bool test);
    bool full() const { return n_samples_ >= max_; }
    // writes meta.json (meta plus the counts) and closes the files
    void close(nlohmann::json meta);
    uint64_t samples() const { return n_samples_; }
    const std::string& dir() const { return dir_; }

private:
    std::FILE* open(const char* name, const char* mode);
    std::string dir_;
    uint64_t max_;
    int wps_;
    uint64_t n_samples_ = 0, n_symbols_ = 0, n_test_ = 0;
    std::FILE *train_ = nullptr, *test_ = nullptr, *bin_ = nullptr, *flags_ = nullptr, *sym_ = nullptr;
};

}  // namespace arof
