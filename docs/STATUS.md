# Status and next steps

Where the work on the generator stands, so that it can be picked up again. Plan:
[rpi_stream_generator_handoff.md](rpi_stream_generator_handoff.md).

## Done

| Milestone | State | Evidence |
|---|---|---|
| 1 Skeleton | CMake, Dockerfile (arm64, bookworm, digest pinned), Makefile, config | committed |
| 2 Link, receiver, sample former, recorder | `src/link/` mirrors `matlab/link/` one module per function; `src/core/`, `src/stream/` | `make test`: 5 suites pass |
| 3 Dataset layout, tools | `tools/export_dataset.py`, `evaluate_outputs.py`, `run_lms.py`, `compare_matlab.py` | see below |
| 4 Bench, sender, control, fake KV260 | `--bench`, `stream/streamer.cpp` (generator, sender, control threads), `tools/fake_kv260.py` | loopback run below |

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

## Next steps

1. **Dockerized run on the Pi:** `make docker`, then run the image (README "Run") against
   `tools/fake_kv260.py` on the host; repeat the loopback check from inside the container.
2. **V4 long run:** 10 min against `fake_kv260.py`, with the jitter histogram.
3. **Clean-up:** delete `matlab/` and `nn_training_model/` from the working tree (not committed;
   everything needed is in `tests/data/`).
4. **Rates above ~17 k:** the generator is single-threaded. For 40-80 k samples/s, pipeline it on
   2-3 threads (TX + MZM + fiber / RAU + wireless + detector + ADC / RX), plan section 9.

## Decisions taken here (to confirm)

- **CONFIG frame payload length:** the handoff does not define it. Here `n_samples` holds the JSON
  byte count and `words_per_sample` is 0. `stream_rx` must read it this way.
- **Live changes take effect after the ring:** samples already in the ring (up to 3.2 s at 10 k)
  are sent with the old config_id; the change shows at the RECALIBRATED frame.
- **ADC outside the calibrated range:** samples saturate at the end codes (a fixed full scale).
- **Fiber:** overlap-save with +-128 taps (plan values); 2e-5 of the signal rms against
  MATLAB's single FFT at 15 km, 1.3e-4 at 100 km.

## Open points of the handoff (the user's decisions)

- Target scale: grid (default, used so far) or unit.
- Test segments: prequential only, or infer-mode TEST segments (`samples.schedule`).
- Re-acquisition after a change: default on; `rx.freeze_rx` for drift experiments.
