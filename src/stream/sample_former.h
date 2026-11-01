// Received symbols -> samples as matlab/equalizer.py (model 'real') builds them for main_demo.m
// step 8, and analysis/export_dataset.m exports them. With T taps, the sample whose newest
// symbol is k is
//   X = [Re x(k) Im x(k) Re x(k-1) Im x(k-1) ... Re x(k-T+1) Im x(k-T+1)]   (newest first)
//   Y = [Re d(k-D) Im d(k-D)],  D = floor(T/2)                               (the center tap)
// as 2T+2 Q8.24 words. Scale unit: unit-power 16QAM, levels +-1/sqrt(10), +-3/sqrt(10) (as
// equalizer.py for sigmoid and tanh networks); grid: times sqrt(10), Y on {+-1, +-3} (multilevel).
#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "link/receiver.h"

namespace arof {

enum class Scale { grid, unit };
// demo: train_len TRAIN then test_len TEST, repeating (main_demo.m step 8: 5000 + 44 991);
// train: every sample TRAIN
enum class Schedule { train, demo };

struct SampleSettings {
    int taps = 3;                               // the 6-12-2 tanh (own_tanh_6)
    Scale scale = Scale::unit;
    Schedule schedule = Schedule::demo;
    uint64_t train_len = 5000, test_len = 44991;
};

// The word equalizer_q8_24.py writes for the value v: load_dataset casts X and Y to float32,
// then fixedpoint.to_q gives sat32(int(round(v * 2^24))), Python's round: ties to even.
int32_t to_q8_24(double v);

class SampleFormer {
public:
    explicit SampleFormer(const SampleSettings& s);
    int words_per_sample() const { return 2 * s_.taps + 2; }
    // Pushes one symbol. Once taps symbols have arrived, each push forms a sample: appends its
    // words to `words` (and its values to `values`, if given) and returns true.
    bool push(const RxSymbol& s, std::vector<int32_t>& words, std::vector<double>* values = nullptr);
    // TEST flag of the next sample to be formed
    bool next_is_test() const;
    uint64_t samples() const { return n_; }

private:
    SampleSettings s_;
    double k_;
    std::deque<cd> taps_;   // received symbols, newest first
    std::deque<cd> sent_;   // their sent symbols, newest first
    uint64_t n_ = 0;
};

Scale parse_scale(const std::string& s);
Schedule parse_schedule(const std::string& s);
const char* to_string(Scale s);
const char* to_string(Schedule s);

}  // namespace arof
