# Handoff: integrating arof-rt-generator into the main project

For whoever merges this repository into the project repository that holds the MATLAB study
(`matlab/`), the training model (`nn_training_model/`), the RTL and the KV260 software. It can be
given as is to a person or as a prompt to a coding agent working in the main repository.

What the system does, how the parts connect and how close it is to MATLAB is in
[DEMO.md](DEMO.md): read it first. This file says what moves where, which files depend on the
other parts of the project, and what is still to do.

## 1. What this repository is

A C++17 real-time generator for the Raspberry Pi: it runs the A-RoF link of `matlab/main_demo.m`
(steps 1–7) as an endless stream and sends the samples of `main_demo.m` step 8 (as
`matlab/equalizer.py` builds them for the 6-12-2 tanh) over TCP to the KV260, at a fixed rate.
Plus a receiver kit for a laptop that stands in for the KV260. All of it is done and verified
(DEMO.md section 11); the KV260 side is not started.

```
CMakeLists.txt, Makefile, Dockerfile, .dockerignore, config/default.json
src/link/      the link, one module per matlab/link/*.m function
src/core/      rng, comm (qammod, qamdemod, rcosdesign), evaluate_outputs, link_settings, config
src/stream/    sample_former (samples, Q8.24), recorder, frame.h (wire format), sample_ring,
               streamer (generator, sender, control threads)
src/main.cpp   arof_stream
tests/         5 suites (ctest): rng, link_stages, transmit_receive, samples, tools (Python)
tests/data/    MATLAB results (CSV) and the Q8.24 fixture from nn_training_model
tools/         export_dataset.py, evaluate_outputs.py, run_lms.py, recording.py,
               compare_matlab.py, fake_kv260.py
host/          the laptop kit: Makefile (SSH tunnel), arof_rx.cpp (receiver), dataset.h, mat_file.h (matio)
reference/     copies of MATLAB files: main_demo.m, equalizer.py, export_dataset.m,
               arof_training_data.mat
docs/          DEMO.md (the reference document), matlab_mirror/ (each step against MATLAB),
               this handoff
```

Build and tests: `make test` (native), `make docker` (arm64 image, runs the C++ tests in its build
stage). The Python test needs numpy and scipy (`.venv`, see DEMO.md section 10).

## 2. Where it goes

Keep it a **self-contained subdirectory** of the main repository, for example
`rpi_generator/`: its own CMake, Makefile, Dockerfile and config. It does not link with anything
else in the project; it shares formats, not code. The Pi checks out the main repository and works
in that subdirectory (`cd rpi_generator && make fake`).

Keep the history:

```
# in the main repository
git subtree add --prefix=rpi_generator <url-of-arof-rt-generator> main
```

(or `git remote add gen <url>; git fetch gen; git merge -s ours --no-commit --allow-unrelated-histories gen/main;
git read-tree --prefix=rpi_generator/ -u gen/main; git commit`).

`host/` can stay inside it (it includes `../src/stream/frame.h`), or move next to the KV260
software, with the include path adjusted.

## 3. Files that depend on the rest of the project

These are copies or derivatives of files that live in the main repository. After the merge, point
them at the originals so they cannot drift.

| Here | Comes from | Used by | After the merge |
|---|---|---|---|
| `reference/main_demo.m` | `matlab/main_demo.m` | documentation | Delete; refer to `matlab/main_demo.m` |
| `reference/equalizer.py` | `matlab/equalizer.py` | the sample definition (`sample_former`), DEMO.md | Delete; refer to `matlab/equalizer.py` |
| `reference/export_dataset.m` | `matlab/analysis/export_dataset.m` | the dataset layout (`tools/export_dataset.py`, `host/dataset.h`) | Delete; refer to the original |
| `reference/arof_training_data.mat` | `matlab/results/arof_training_data.mat` | `tests/test_tools.py` (layout and EVM checks) | Point `test_tools.py`'s `ROOT / "reference" / ...` at `matlab/results/` |
| `tests/data/matlab_compare_results.csv` | `matlab/results/compare_results.mat` (`main_compare.m`) | `tools/compare_matlab.py`, test thresholds | Keep, or regenerate from the `.mat` (header comments say how) |
| `tests/data/matlab_impairment_results.csv` | `matlab/results/impairments_results.mat` (`main_impairments.m`) | `tools/compare_matlab.py` | Same |
| `tests/data/q8_24_fixture.txt` | `nn_training_model/data/training_data.mat` and its `train_samples_q8_24.mem` (`equalizer_q8_24.py`) | `test_samples` (Q8.24 rule) | Keep, or generate from `nn_training_model` in the test |
| `src/link/*` | `matlab/link/*.m`, same names and parameters | the generator | **If a MATLAB link module changes, change its C++ twin, update its note in `docs/matlab_mirror/`, and rerun `tools/compare_matlab.py --set all`** |
| `src/stream/sample_former.*` | `equalizer.py`'s `iq_tdl` and target `t[k - taps//2]`, scale per activation | the stream | Change together with `equalizer.py` |

Also after the merge:
- **`.dockerignore`** is an allow-list (`CMakeLists.txt`, `src`, `tests`, `config`): build the
  image with the subdirectory as context, so the rest of the repository never enters it.
- **`.gitignore`** entries of this repository: `build*/`, `records/`, `.venv/`, `host/arof_rx`,
  `host/captures/`.
- **CI:** `cmake -S rpi_generator -B build && cmake --build build && ctest --test-dir build`
  works on x86 (no `-mcpu` off aarch64); it needs FFTW 3 and, for the tools test, numpy/scipy.

## 4. Contracts with the other parts

These must stay identical on both sides. They are all specified in DEMO.md; this is the checklist.

### MATLAB

- **The samples:** 3 taps newest first, I/Q interleaved; target the center tap `d(k − floor(T/2))`;
  unit-power 16QAM (levels ±1/√10, ±3/√10) for tanh and sigmoid networks, ×√10 for multilevel.
- **The dataset:** one `demo` cycle, **49 991 samples: rows 1–5000 train, 5001–49 991 test**,
  the training and test sets of `main_demo.m` step 8. Fields as `export_dataset.m` today:
  `X`, `Y`, `rx_aligned`, `tx_aligned`, `tx_symbols`, `tx_indices`, `tx_bits`, `mod_order`,
  `bits_per_symbol`, `delay`, `equalizer_taps`, `equalizer_input_size`, `equalizer_output_size`,
  `scale`, `vpp_mv`, `rop_dbm`, `seed`, `evm_no_eq`, `source`. A new field must be added on both
  sides (`tools/export_dataset.py`, `host/dataset.h`).

**Task in `matlab/analysis/export_dataset.m`** (today it exports 9998 samples, split 80/20
downstream). Change it to the `main_demo.m` step 8 set:
1. `cfg.n_sym = 50000`, the link, seed and receiver of `main_demo.m` (300 mV, −11.75 dBm, seed
   2017 by default).
2. Windows `k = (6:49996).'` for 3 taps. In general, those of `equalizer.py`:
   `k = (lms_taps − floor(lms_taps/2) + floor(taps/2) : n_sym − floor(lms_taps/2) + floor(taps/2)).'`
   with `lms_taps = 10` (`run_network.m` passes `k_start = 10`, `tau = 5`; `equalizer.py` shifts
   by `taps//2 − tau`). For 3 taps: 6 … 49 996.
3. Samples unchanged: `X(:, 2m+1:2m+2) = [real(rx(k−m)) imag(rx(k−m))]`, `Y = tx(k − delay)`,
   `delay = floor(taps/2)`, scale `'unit'`.
4. Split: rows 1–5000 train, 5001–49 991 test; say so in the header and the printout.
5. The symbol fields hold the 50 000 symbols of the sequence.

**Checks:** (1) the exported `rx_aligned`, `tx_aligned` equal `main_demo.m`'s `x`, `d` after
step 7; (2) `own_tanh_6` (3 taps, 12 hidden, tanh, lr 1e-2, 200 epochs, `main_demo.m`'s seed)
trained on rows 1–5000 and scored on 5001–49 991 with `evaluate_outputs.m` gives `main_demo.m`'s
**EVM 11.3 %, BER 5.6e-6**, identically; (3) the no-EQ EVM of the center tap `X(:,3:4)` against
`Y` on the test rows is 13.3 %.

### nn_training_model

- **Q8.24:** `to_q` after a float32 cast, round half to even, saturate; `.mem` files one word per
  line, 8 uppercase hex digits. The generator's words are byte-identical
  (`tests/data/q8_24_fixture.txt`).
- **Split:** wherever it splits a dataset 80/20, split **5000 / 44 991** instead (rows 1–5000
  train). The receiver's `train_samples_q8_24.mem` / `test_samples_q8_24.mem` per cycle already
  have that split.
- **To add:** the 6-12-2 tanh (tanh activation, 6-input RAM map), modelling the engine's
  arithmetic exactly, with golden files (loss, weights, outputs) for a recorded cycle. It is what
  tells fixed-point effects apart from bugs on the board.

### KV260 (RTL and PS software)

- **Wire protocol:** `src/stream/frame.h` (copy it, or include it from `rpi_generator/src/stream/`).
  36-byte little-endian header; flags RECALIBRATED 1, TEST 2, END 4, CONFIG 8; CONFIG frames carry
  the JSON byte count in `n_samples` with `words_per_sample = 0`; a frame never mixes TRAIN and
  TEST samples; `first_sample` is the generator's sample index (cycles start at multiples of
  `train_len + test_len`).
- **CONFIG JSON keys `stream_rx` needs:** `settings.samples.taps` (words per sample = 2·taps + 2),
  `.schedule` (`demo` / `train`), `.train_len`, `.test_len`, `.scale`; `settings.stream.rate_sps`
  (→ `SRC_PERIOD`); `settings.seed`, `settings.link` (to save with the results); `config_id`.
- **Behaviour:** DEMO.md sections 7–8: keep the 5000 TRAIN samples in DDR and replay them
  200 times (lr 1e-2, as `equalizer.py`), then equalize the TEST samples live with the weights
  frozen; prequential outputs on TRAIN; counters `UNDERRUN`, `DROPPED`, sequence gaps.
- **Reference implementation of the receiving side:** `host/arof_rx.cpp` (frame checks, cycle
  alignment, dataset rebuild) and `tools/fake_kv260.py` (checks and timing).

## 5. What is still to do

| # | Task | Where | Notes |
|---|---|---|---|
| 1 | `export_dataset.m` at 49 991 samples, its three checks | `matlab/` | Section 4. Then the dataset is the same object in MATLAB and on the hardware |
| 2 | Train the 6-12-2 tanh on the Pi's captured cycles with `matlab/equalizer.py` | here + `matlab/` | Expected ~11.3 % EVM, BER ~1e-5–1e-6 averaged over cycles: validates the stream before the board |
| 3 | 5000 / 44 991 split and the 6-12-2 tanh model with golden files | `nn_training_model/` | Section 4 |
| 4 | `stream_rx` on the PS: frames, DDR ring, replay, DMA, outputs, step 9 | KV260 software | DEMO.md sections 7–8 |
| 5 | Stream pacer in the PL (FIFO 512 samples, `SRC_PERIOD`, `UNDERRUN`) | RTL | On top of the DMA streaming stage |
| 6 | 6-12-2 tanh engine (tanh activation, 6 inputs) and infer mode on TEST | RTL | Until then: stream with `taps 2` for path and power only |
| 7 | Power measurement per state (12 V meter, board telemetry, Vivado estimate) | board | DEMO.md section 7 |
| 8 | Board checks: a recorded cycle on the board equals the golden files word for word; a live ROP change raises the loss at RECALIBRATED, then it recovers | board | `UNDERRUN = 0`, `DROPPED = 0`, no gaps |
| 9 | Pipelined generator (2–3 threads) for rates above ~17 k samples/s | here | Needed to measure energy per symbol across rates |
| 10 | Remove EVM/BER from the C++ generator, keep them in `tools/` and the tests | here | Agreed, not done |

## 6. Things that bit us

- **The Pi kept streaming between captures:** a receiver that connects mid-stream sees the
  stream from wherever the generator is. Cycle alignment survives because `first_sample` stays the
  sample index even across lost frames (fixed in the streamer); a receiver must align on
  `first_sample % (train_len + test_len) == 0`, not on its first frame.
- **Stale SSH tunnels hold the Pi's port 5000:** the laptop's `make` closes earlier `ssh -R`
  tunnels of that laptop; on the Pi, `make tunnel-close` frees one left by another machine.
- **Timing measured through the SSH tunnel is the tunnel's,** not the Pi's pacing (bursts at the
  start). Measure jitter with the receiver on the Pi's network.
- **Docker rebuilds:** the image depends only on `CMakeLists.txt`, `src/`, `tests/`, `config/`;
  anything else in the build context (editor caches, docs) used to trigger a full rebuild.
- **`pkill -f` patterns match their own shell** when the pattern appears in the command line:
  anchor them (`^ssh ...`).
