// Received symbols -> training samples in the layout of analysis/export_dataset.m and
// nn_training_model's train_samples_q8_24.mem. With T taps, sample n is
//   X(n) = [Re x(n) Im x(n) Re x(n-1) Im x(n-1) ... ]  (newest first),  Y(n) = [Re d(n) Im d(n)]
// as 2T+2 Q8.24 words. Scale grid (export_dataset.m): rx = sqrt(10)*x, tx = round(sqrt(10)*d),
// so Y is on the integer grid {+-1, +-3}.
#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "link/receiver.h"

namespace arof {

enum class Scale { grid, unit };
enum class Schedule { train, train_test };   // all TRAIN, or train_len TRAIN then test_len TEST

struct SampleSettings {
    int taps = 2;
    Scale scale = Scale::grid;
    Schedule schedule = Schedule::train;
    uint64_t train_len = 7998, test_len = 2000;
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
    std::deque<cd> taps_;   // newest first
    uint64_t n_ = 0;
};

Scale parse_scale(const std::string& s);
Schedule parse_schedule(const std::string& s);
const char* to_string(Scale s);
const char* to_string(Schedule s);

}  // namespace arof
