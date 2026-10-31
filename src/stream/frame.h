// Wire format (handoff section 6): one TCP connection from the RPi (client) to stream_rx on
// the KV260, a stream of frames, little-endian. A 36-byte header, then
//   data frames:    n_samples * words_per_sample int32 words (the Q8.24 samples, file order)
//   CONFIG frames:  n_samples bytes of JSON text (the full settings), words_per_sample = 0
// A CONFIG frame opens the connection and follows every settings change; the first data frame
// after a change carries RECALIBRATED and the new config_id. The last frame carries END.
#pragma once

#include <cstdint>
#include <cstring>

namespace arof {

constexpr uint32_t kFrameMagic = 0x46526F41;   // "AoRF"
constexpr uint16_t kFrameVersion = 1;

enum FrameFlags : uint16_t {
    kRecalibrated = 1 << 0,
    kTest = 1 << 1,
    kEnd = 1 << 2,
    kConfig = 1 << 3,
};

#pragma pack(push, 1)
struct FrameHeader {
    uint32_t magic = kFrameMagic;
    uint16_t version = kFrameVersion;
    uint16_t flags = 0;
    uint32_t seq = 0;               // +1 per frame (CONFIG frames included)
    uint64_t first_sample = 0;      // global index of the frame's first sample
    uint32_t n_samples = 0;         // samples (data) or payload bytes (CONFIG)
    uint16_t words_per_sample = 0;  // 2*taps + 2 (data) or 0 (CONFIG)
    uint16_t reserved = 0;
    uint32_t config_id = 0;         // +1 at every settings change
    uint32_t seed = 0;
};
#pragma pack(pop)
static_assert(sizeof(FrameHeader) == 36, "frame header is 36 bytes");

// The host is little-endian (aarch64, x86-64): the header and the words go out as they are.
inline void write_header(const FrameHeader& h, unsigned char* out) { std::memcpy(out, &h, sizeof h); }

}  // namespace arof
