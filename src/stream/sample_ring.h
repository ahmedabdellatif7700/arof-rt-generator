// Single-producer single-consumer ring of whole samples between the generator thread and the
// sender thread. Each slot holds the sample's words and a tag (config_id, TEST flag).
#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

namespace arof {

struct SampleTag {
    uint32_t config_id;
    bool test;
};

class SampleRing {
public:
    SampleRing(size_t capacity, int words_per_sample)
        : cap_(capacity), wps_(words_per_sample), words_(capacity * words_per_sample), tags_(capacity) {}

    size_t capacity() const { return cap_; }
    int words_per_sample() const { return wps_; }
    size_t size() const { return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire); }
    size_t free() const { return cap_ - size(); }

    // producer: one sample; false if full
    bool push(const int32_t* w, SampleTag tag) {
        const uint64_t h = head_.load(std::memory_order_relaxed);
        if (h - tail_.load(std::memory_order_acquire) >= cap_) return false;
        const size_t i = h % cap_;
        std::memcpy(&words_[i * wps_], w, sizeof(int32_t) * wps_);
        tags_[i] = tag;
        head_.store(h + 1, std::memory_order_release);
        return true;
    }

    // consumer: the tag and words of the oldest sample (only while size() > 0)
    const SampleTag& front_tag(size_t ahead = 0) const { return tags_[(tail_.load(std::memory_order_relaxed) + ahead) % cap_]; }
    // copies n samples into out (n <= size()) and removes them
    void pop(size_t n, int32_t* out) {
        uint64_t t = tail_.load(std::memory_order_relaxed);
        for (size_t k = 0; k < n; ++k, ++t)
            std::memcpy(out + k * wps_, &words_[(t % cap_) * wps_], sizeof(int32_t) * wps_);
        tail_.store(t, std::memory_order_release);
    }
    uint64_t popped() const { return tail_.load(std::memory_order_acquire); }

private:
    size_t cap_;
    int wps_;
    std::vector<int32_t> words_;
    std::vector<SampleTag> tags_;
    alignas(64) std::atomic<uint64_t> head_{0};
    alignas(64) std::atomic<uint64_t> tail_{0};
};

}  // namespace arof
