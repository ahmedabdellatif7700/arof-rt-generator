# Step 11: Training samples and streaming to the KV260

The part MATLAB does not have as a stream. The received symbols of step 7 become the samples
`matlab/equalizer.py` builds for `main_demo.m` step 8 (the 6-12-2 tanh, `own_tanh_6`), in the
layout `export_dataset.m` exports (`reference/arof_training_data.mat`), as Q8.24 words, and go to
the KV260 over TCP at a fixed rate. The MATLAB references are the samples and the layout; the
rest (recorder, wire format, pacing, control) is the Pi's own.

## 11a. The samples (`equalizer.py`, `export_dataset.m`)

```python
def iq_tdl(x, ks, taps):        # equalizer.py: columns [I(k) Q(k) I(k-1) Q(k-1) ...]
    for m in range(taps):
        cols += [x[:, ks - m].real, x[:, ks - m].imag]
U  = scale * iq_tdl(x, k_train, taps)
tt = scale * t[:, k_train - taps // 2]            # target: the center tap
```

- **Taps:** 3 symbols, newest first: `X = [Re x(k) Im x(k) Re x(k−1) Im x(k−1) Re x(k−2) Im x(k−2)]`,
  the 6 inputs of the 6-12-2 tanh.
- **Target:** the center tap, `Y = d(k − floor(T/2)) = d(k−1)`: the network sees one symbol after
  its target and one before.
- **Scale "unit":** unit-power 16QAM (levels ±1/√10, ±3/√10), as `equalizer.py` uses for tanh
  and sigmoid networks (scale 1.0); `grid` (×√10, {±1, ±3}) is its scale for the multilevel ones.
- **Train and test:** `equalizer.py` trains on the windows k = 6 … 5005 (MATLAB 1-based) and tests
  on k = 5006 … 49 996, back to back: 5000 + 44 991 samples, the stream's `demo` cycle.

C++: `src/stream/sample_former.*`, one sample per symbol once `taps` symbols have arrived:

```cpp
for (const cd& x : taps_) { put(k_ * x.real()); put(k_ * x.imag()); }   // newest first
const cd& c = sent_[taps / 2];                                          // the center tap
const cd d = grid ? cd(round(k_ * c.real()), round(k_ * c.imag())) : c;
put(d.real()); put(d.imag());
```

`samples.taps` (default 3; 2 gives 6 words for today's 4-12-2 engine) and `samples.scale`
(`unit` default, or `grid`) are settings.

## 11b. Q8.24 words (`nn_training_model`)

`equalizer_q8_24.py` loads the dataset as float32, then `to_q` gives `sat32(round(v · 2^24))`,
Python's round (ties to even). The Pi follows exactly that:

```cpp
const double f = static_cast<double>(static_cast<float>(v));   // astype(np.float32)
const double q = std::nearbyint(f * 16777216.0);               // ties to even
// saturate to int32
```

Each sample is `2·taps + 2` = 8 words; in `.mem` files one word per line, 8 uppercase hex
digits, two's complement.

## 11c. Recording (`--record DIR`, `src/stream/recorder.*`)

The first `max_samples` (default 49 991, one `demo` cycle) samples of the stream:

| File | Content |
|---|---|
| `train_samples_q8_24.mem`, `test_samples_q8_24.mem` | the words, as `nn_training_model` writes them (TRAIN / TEST samples) |
| `samples.bin` | int32 LE, the same words in stream order |
| `flags.bin` | one byte per sample, 1 = TEST |
| `symbols.bin` | per symbol: Re x, Im x, Re d, Im d, idx (unit power) |
| `meta.json` | settings, link, calibration, counts |

`tools/export_dataset.py` turns it into a `.mat` with the fields, classes and shapes of
`arof_training_data.mat`; `tools/recording.py` loads either.

## 11d. The wire format (`src/stream/frame.h`)

One TCP connection, Pi (client) → `stream_rx` on the KV260 (`stream.host`, `stream.port`), a
stream of frames, little-endian. A 36-byte header:

| Field | Bytes | Meaning |
|---|---|---|
| magic | 4 | `0x46526F41` ("AoRF") |
| version | 2 | 1 |
| flags | 2 | RECALIBRATED 1, TEST 2, END 4, CONFIG 8 |
| seq | 4 | +1 per frame |
| first_sample | 8 | global index of the frame's first sample |
| n_samples | 4 | samples (data) or JSON bytes (CONFIG) |
| words_per_sample | 2 | `2·taps + 2` (data) or 0 (CONFIG) |
| reserved | 2 | 0 |
| config_id | 4 | +1 at every settings change |
| seed | 4 | the generator's seed |

- **Data frame:** header + `n_samples · words_per_sample` int32 words, all samples with the same
  `config_id` and TEST flag.
- **CONFIG frame:** opens every connection and precedes every change: header + the full settings
  and calibration as JSON. The first data frame after it carries RECALIBRATED. The handoff does
  not define the CONFIG payload length; here it is `n_samples` bytes with `words_per_sample = 0`
  (`docs/DEMO.md` section 6).
- **END:** the last frame when the generator stops.

## 11e. Real-time sending (`src/stream/streamer.*`)

Three threads:

- **Generator:** `generate()` (step 10) in chunks of 128 symbols → samples → a single-producer
  single-consumer **ring**, kept 1 s ahead, and at least 1.5 × the calibration time, so a
  recalibration (~2.1 s) does not starve the sender. Applies control requests between chunks.
- **Sender:** paced on **absolute time**: `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME)` to
  `anchor + k · frame_ms`; at each tick it sends `floor(rate · elapsed) − sent` samples, so the
  rate has no drift whatever the jitter of a single wake-up. Default 10 000 samples/s, 1 ms
  frames (10 samples). Optional SCHED_FIFO (needs `--cap-add SYS_NICE`). Late samples are counted;
  beyond 0.1 s behind, the clock re-anchors (a slip). Reconnects if the KV260 drops the link.
- **Control:** TCP port `stream.control_port` (5001), JSON lines:
  `{"set": {...}}` (any setting; link changes recalibrate, step 10), `{"status": {}}` (rates,
  late samples, slips, reconnects, last calibration), `{"stop": {}}` (END, exit).

Samples already in the ring when a change arrives go out with the old `config_id`; the change
shows at the CONFIG + RECALIBRATED frames.

`samples.schedule`: `demo` (default: `train_len` = 5000 TRAIN then `test_len` = 44 991 TEST
samples, repeating, the training and test sets of `main_demo.m` step 8) or `train` (all TRAIN).

## 11f. `tools/fake_kv260.py`

A stand-in for `stream_rx`: a TCP server that checks every frame (magic, version, seq +1,
`first_sample` continuous, `words_per_sample` constant, `config_id` only rising and announced by
CONFIG, RECALIBRATED after it) and measures the rate, the jitter against the ideal schedule and
the inter-arrival times. Exits 1 on any protocol error.

## 11g. Docker

`Dockerfile`: arm64, `debian:bookworm-slim` pinned by digest; the build stage compiles with
`-mcpu=cortex-a72` (Pi 4; `cortex-a76` for a Pi 5) and runs the C++ tests; the runtime stage
holds only the binary, FFTW and `config/`. `make docker`, `make docker-run` (config and records
mounted).

## What differs from MATLAB

MATLAB writes one `.mat` per sequence; the Pi produces the same layout endlessly and sends it.
The layout and the Q8.24 words are the same; the transport is new.

## Verified

- `tests/test_samples.cpp`: `q8_24_rounds_half_to_even`; `q8_24_words_as_nn_training_model`
  (the fixture of `nn_training_model`'s words, `tests/data/q8_24_fixture.txt`, equal);
  `layout_as_equalizer_py`; `grid_scale_and_taps`; `demo_schedule`; `recorder_files`;
  the settings tests (`settings_round_trip`, `unknown_settings_rejected`, `live_set`).
- `tests/test_tools.py`: a recording of one `demo` cycle exported to `.mat` has the fields, classes,
  columns, `delay` and `scale` of `arof_training_data.mat`, X and Y as `equalizer.py` builds them;
  its `.mem` files are the Q8.24 words of that `.mat`, 5000 train + 44 991 test; no EQ 13.27 % /
  LMS 12.86 % vs the reference 13.38 % / 13.04 %.
- `host/arof_rx --cycles`: a received `demo` cycle is byte-identical to `--record` (both `.mem`
  files, samples, flags); 13.26–13.29 % per cycle.
- `docs/DEMO.md` section 11: 10 minutes in Docker to `fake_kv260.py`: 6 021 160 samples, no protocol
  error or gap, rate error −0.00005 %, jitter 0.12 ms rms, two live changes without late samples.
- `--bench`: about 35 500 samples/s native, 34 200 in Docker (the stream runs at 10 000).
