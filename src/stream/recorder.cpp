#include "stream/recorder.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace arof {

Recorder::Recorder(const std::string& dir, uint64_t max_samples, int words_per_sample)
    : dir_(dir), max_(max_samples), wps_(words_per_sample) {
    std::filesystem::create_directories(dir_);
    train_ = open("train_samples_q8_24.mem", "w");
    test_ = open("test_samples_q8_24.mem", "w");
    bin_ = open("samples.bin", "wb");
    flags_ = open("flags.bin", "wb");
    sym_ = open("symbols.bin", "wb");
}

Recorder::~Recorder() {
    for (std::FILE* f : {train_, test_, bin_, flags_, sym_})
        if (f) std::fclose(f);
}

std::FILE* Recorder::open(const char* name, const char* mode) {
    const std::string path = dir_ + "/" + name;
    std::FILE* f = std::fopen(path.c_str(), mode);
    if (!f) throw std::runtime_error("cannot open " + path);
    return f;
}

void Recorder::symbol(const RxSymbol& s) {
    if (full() || !sym_) return;
    const double v[5] = {s.x.real(), s.x.imag(), s.d.real(), s.d.imag(), static_cast<double>(s.idx)};
    std::fwrite(v, sizeof v, 1, sym_);
    ++n_symbols_;
}

void Recorder::sample(const int32_t* words, bool test) {
    if (full() || !bin_) return;
    std::FILE* mem = test ? test_ : train_;
    for (int i = 0; i < wps_; ++i) std::fprintf(mem, "%08X\n", static_cast<uint32_t>(words[i]));
    std::fwrite(words, sizeof(int32_t), static_cast<size_t>(wps_), bin_);
    const uint8_t flag = test ? 1 : 0;
    std::fwrite(&flag, 1, 1, flags_);
    ++n_samples_;
    n_test_ += test;
}

void Recorder::close(nlohmann::json meta) {
    for (std::FILE** f : {&train_, &test_, &bin_, &flags_, &sym_}) {
        if (*f) std::fclose(*f);
        *f = nullptr;
    }
    meta["n_samples"] = n_samples_;
    meta["n_train"] = n_samples_ - n_test_;
    meta["n_test"] = n_test_;
    meta["n_symbols"] = n_symbols_;
    meta["words_per_sample"] = wps_;
    std::ofstream(dir_ + "/meta.json") << meta.dump(2) << "\n";
}

}  // namespace arof
