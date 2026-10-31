#include "stream/sample_former.h"

#include <cmath>
#include <stdexcept>

namespace arof {

int32_t to_q8_24(double v) {
    const double f = static_cast<double>(static_cast<float>(v));   // astype(np.float32)
    const double q = std::nearbyint(f * 16777216.0);               // FE_TONEAREST: ties to even
    if (!(q < 2147483647.0)) return q != q ? 0 : INT32_MAX;
    if (q <= -2147483648.0) return INT32_MIN;
    return static_cast<int32_t>(q);
}

SampleFormer::SampleFormer(const SampleSettings& s) : s_(s) {
    if (s_.taps < 1) throw std::invalid_argument("taps must be at least 1");
    k_ = s_.scale == Scale::grid ? std::sqrt(10.0) : 1.0;
}

bool SampleFormer::next_is_test() const {
    if (s_.schedule == Schedule::train) return false;
    return n_ % (s_.train_len + s_.test_len) >= s_.train_len;
}

bool SampleFormer::push(const RxSymbol& s, std::vector<int32_t>& words, std::vector<double>* values) {
    taps_.push_front(s.x);
    if (taps_.size() > static_cast<size_t>(s_.taps)) taps_.pop_back();
    if (taps_.size() < static_cast<size_t>(s_.taps)) return false;
    auto put = [&](double v) {
        words.push_back(to_q8_24(v));
        if (values) values->push_back(v);
    };
    for (const cd& x : taps_) {
        put(k_ * x.real());
        put(k_ * x.imag());
    }
    const cd d = s_.scale == Scale::grid ? cd(std::round(k_ * s.d.real()), std::round(k_ * s.d.imag())) : s.d;
    put(d.real());
    put(d.imag());
    ++n_;
    return true;
}

Scale parse_scale(const std::string& s) {
    if (s == "grid") return Scale::grid;
    if (s == "unit") return Scale::unit;
    throw std::invalid_argument("scale must be grid or unit: " + s);
}

Schedule parse_schedule(const std::string& s) {
    if (s == "train") return Schedule::train;
    if (s == "train_test") return Schedule::train_test;
    throw std::invalid_argument("schedule must be train or train_test: " + s);
}

const char* to_string(Scale s) { return s == Scale::grid ? "grid" : "unit"; }
const char* to_string(Schedule s) { return s == Schedule::train ? "train" : "train_test"; }

}  // namespace arof
