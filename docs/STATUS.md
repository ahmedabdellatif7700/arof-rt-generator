# Status and next steps

Where the work on the generator stands, so that it can be picked up again. Plan:
[rpi_stream_generator_handoff.md](rpi_stream_generator_handoff.md).

## Done

| Milestone | State | Evidence |
|---|---|---|
| 1 Skeleton | CMake, Dockerfile (arm64, bookworm, digest pinned), Makefile, config | committed |
| 2 Link, receiver, sample former, recorder | `src/link/` mirrors `matlab/link/` one module per function; `src/core/`, `src/stream/` | `make test`: 5 suites pass |
| 3 Dataset layout, tools | `tools/export_dataset.py`, `evaluate_outputs.py`, `run_lms.py`, `compare_matlab.py` | see below |
| 4 Bench, sender, control, fake KV260 | `--bench`, `stream/streamer.cpp` (generator, sender, control threads), `tools/fake_kv260.py` | loopback and Docker runs below |

### Checks run on the Pi 4 (2026-10-31)

- **Against MATLAB:** `python tools/compare_matlab.py --set all` (6 seeds per point): 74/74 points of
  `compare_results.mat` and `impairments_results.mat` within tolerance (EVM within 2.5 %, BER
  within a factor of 2 where MATLAB has >= 1e-4). At 300 mV, -11.75 dBm: EVM 13.19 % (MATLAB
  13.21 %), BER 7.5e-5 (7.2e-5). The MATLAB numbers are in `tests/data/matlab_*_results.csv`.
- **Layout:** a 9998-sample recording exported by `tools/export_dataset.py` has the fields, MATLAB
  classes and shapes of `reference/arof_training_data.mat`. `nn_training_model/equalizer_q8_24.py`
  trained on it unchanged, and its `train/test_samples_q8_24.mem` are byte-identical to the
  recorder's (train_test schedule). Q8.24 rule: float32 cast first, then round half even
  (`tests/data/q8_24_fixture.txt`), uppercase hex.
- **Bench:** 35 500 samples/s on one Cortex-A72 core (1.77 M simulation samples/s);
  calibration 2.2 s. With the 2x margin: `rate_sps` up to ~17 700.
- **Loopback** (generator -> `fake_kv260.py`, 10 k samples/s, 35 s, live ROP change): rate
  10000.01 samples/s (+0.000 %), jitter 0.047 ms rms, p99 0.13 ms, no sequence gaps, no late
  samples during the 2.0 s recalibration (ring 3.2 s), END received.

### Dockerized on the Pi 4 (2026-11-01)

- **Image:** `make docker` builds in 2 min 19 s; the 4 C++ test suites pass in the build stage
  (the Python tools test needs numpy and is skipped there). In the container, `--calibrate` gives
  the native constants and results exactly; `--bench`: 34 200 samples/s (GCC 12 of bookworm).
- **V4, 10 min** (`docker run --network host --cpus 3 --cap-add SYS_NICE`, SCHED_FIFO, to
  `fake_kv260.py` on the host, ROP -11.75 -> -14.5 -> -11.75 dBm at 4 and 7 min):
  6 021 160 samples in 602 118 frames, no protocol error, no sequence or sample gap, END received;
  rate 9999.995 samples/s (-0.00005 %); jitter against the ideal schedule 0.12 ms rms, p99
  0.27 ms, max 13.4 ms (one frame); inter-arrival 0.9-1.1 ms for 95.3 % of the frames, 2 frames
  above 5 ms. Both changes: CONFIG frame, RECALIBRATED frame, recalibration 2.0-2.1 s (EVM
  15.36 % at -14.5 dBm, MATLAB 15.36 %), no late samples (ring 3.3 s).
- **main_demo.m vs the container** (50 000 symbols recorded in the container, scored by
  `tools/evaluate_outputs.py` as main_demo.m steps 8-9, 44 991 test symbols):

  | | no EQ EVM | no EQ BER | LMS EVM | LMS BER |
  |---|---|---|---|---|
  | main_demo.m (reference/DEMO.md) | 13.3 % | 1.1e-4 | 12.9 % | 1.1e-5 |
  | arof_stream in Docker | 13.3 % | 1.06e-4 | 12.9 % | 2.2e-5 |

  The LMS BERs are 2 and 4 bit errors in 179 964 bits: equal within statistics.

## Next steps

All four milestones are done. Optional:

- **Rates above ~17 k samples/s:** the generator is single-threaded (34-35 k samples/s on the Pi 4,
  ~30 % of a core at 10 k). For 40-80 k, pipeline it on 2-3 threads (TX + MZM + fiber / RAU +
  wireless + detector + ADC / RX), plan section 9.
- **KV260 side** (plan section 11): `stream_rx` must read the CONFIG frames as described below.

## Decisions taken here (to confirm)

- **CONFIG frame payload length:** the handoff does not define it. Here `n_samples` holds the JSON
  byte count and `words_per_sample` is 0. `stream_rx` must read it this way.
- **Live changes take effect after the ring:** samples already in the ring (up to 3.2 s at 10 k)
  are sent with the old config_id; the change shows at the RECALIBRATED frame.
- **ADC outside the calibrated range:** samples saturate at the end codes (a fixed full scale).
- **Fiber:** overlap-save with +-128 taps (plan values); 2e-5 of the signal rms against
  MATLAB's single FFT at 15 km, 1.3e-4 at 100 km.

- **Later (agreed 2026-11-01):** take EVM/BER out of the C++ generator (`core/evaluate_outputs.*`,
  the calibration report, `--calibrate`, the JSON, `meta.json`); keep them in `tools/` and the tests.
  Deferred until the docs walkthrough is done.

## Open points of the handoff (the user's decisions)

- Target scale: grid (default, used so far) or unit.
- ~~Test segments~~ decided 2026-11-01: infer-mode TEST segments by default, `samples.schedule =
  train_test` (7998 TRAIN + 2000 TEST, as nn_training_model's 80/20 split); `train` stays available.
- Re-acquisition after a change: default on; `rx.freeze_rx` for drift experiments.
