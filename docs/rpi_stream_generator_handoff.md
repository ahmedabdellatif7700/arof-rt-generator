# Handoff: real-time A-RoF sample generator on a Raspberry Pi (Docker, C++)

Implementation plan for porting the link of `matlab/main_demo.m` (steps 1–7) to a C++ program that
runs in a Docker container on a Raspberry Pi. The program generates received 16QAM symbols
continuously, at a fixed symbol clock. It formats them exactly like the training dataset of
`nn_training_model` and streams them over Ethernet to the KV260, where the engine trains on them
on chip.

Status: plan only, nothing implemented. Read with `docs/arof_link_and_equalizer.md` (the link),
`docs/dma_streaming_plan.md` (stage 1 on the KV260, which this plan builds on) and
`docs/HANDOFF.md` (engine state and register sequences).

## 0. Decisions

| Topic | Decision |
|---|---|
| Channel | The A-RoF 16QAM link of `main_demo.m`, steps 1–7, with its impairment switches |
| Language | C++17 in the container |
| Transport | Ethernet, TCP, from the RPi to a receiver program on the KV260 PS |
| Real time | **Fixed-rate symbol clock.** Samples arrive at a settable rate (default 10 k samples/s), and the engine never stalls the source. If the engine is too slow, samples are dropped and counted, as with a real receiver |
| Output format | The same per-sample layout and Q8.24 words as `train_samples_q8_24.mem` (section 2) |
| Network | 4-12-2 as on the engine: 2 taps. The number of taps is a setting, for the 6-input candidate later |

The engine trains at about 85 k samples/s (about 1180 cycles per step at 100 MHz). So the clock can
be set anywhere up to about 80 k samples/s. The physical link runs at 0.4 GBd: the clock emulates a
receiver paced by the engine, not the true symbol rate.

## 1. System overview

```
Raspberry Pi (Docker: arof-stream)                     KV260
+-----------------------------------------+            +---------------------------------------------+
| link generator (C++, port of steps 1-7) |            | PS: stream_rx (C++)                          |
|  symbols -> TX -> MZM -> fiber -> PD    |   TCP      |   frames -> jitter buffer (DDR, u-dma-buf)   |
|  -> RF amp -> wireless -> detector      | ---------> |   -> AXI DMA MM2S (docs/dma_streaming_plan)  |
|  -> ADC -> RX DSP -> x(k), d(k)         |  frames of |                                             |
| sample former: taps, scaling, Q8.24     |  Q8.24     | PL: stream pacer (1 sample / SRC_PERIOD)     |
| rate control (coarse, ~1 ms chunks)     |  samples   |   -> sample_fifo (drop + count)             |
| recorder (optional): .mem + .bin        |            |   -> stream_controller -> engine            |
+-----------------------------------------+            +---------------------------------------------+
```

The exact clock comes from the PL, not from the RPi. Ethernet and Linux both add jitter in the
millisecond range. The RPi sends at the target rate on average, the KV260 buffers about 200 ms, and
a pacer in the PL releases one sample every `SRC_PERIOD` cycles (section 5).

## 2. Output format: what "mimics the dataset file" means

The reference is the A-RoF dataset exported from MATLAB by `matlab/analysis/export_dataset.m`
(`matlab/results/arof_training_data.mat`: 16QAM, 300 mV, −11.75 dBm, seed 2017). It loads in
`nn_training_model/equalizer_q8_24.py --dataset` unchanged. The generator's recordings must have
the same layout.

**Inputs X.** Sample `n` has 4 inputs, newest symbol first:
`X(n) = [Re x(n), Im x(n), Re x(n-1), Im x(n-1)]`. In the file, `X[:,0:2]` equals `rx_aligned[1:]`
and `X[:,2:4]` equals `rx_aligned[:-1]`.

**Targets Y.** `Y(n) = [Re d(n), Im d(n)]`, the symbol sent at the newest tap. `Y` equals
`tx_aligned[1:]`.

**Scale.** The targets are the 16QAM symbols on the integer grid {±1, ±3}:
`d = qammod(idx, 16, 'gray')` without unit power, which is `sqrt(10)` times the demo's `d`. The
received symbols are scaled by the same `sqrt(10)`, so X and Y are on one grid. X then has an RMS
value of about 2.2 and a peak of about 3.6, well inside the Q8.24 range of ±128. This is a setting
(`scale: grid | unit`), and grid is the default.

**Words.** Each sample is 6 words: `x0 x1 x2 x3 t0 t1`, little-endian int32, in Q8.24. The
conversion is the same as `hardware/training/fixedpoint.to_q`:

- `round(v * 2^24)`, saturated to the int32 range;
- Python's `round` rounds halves to even, so in C++ use `std::nearbyint` under the default
  `FE_TONEAREST` mode, not `std::round`.

**Splitting into train and test.** The file is split sequentially, 80/20 (7998 + 2000 samples, as
the engine's ROMs). The generator tags every frame as `TRAIN` or `TEST` with a settable schedule.
The default is all `TRAIN`; the option is 7998 train then 2000 test, repeated. The recorder can
then write the same two `.mem` files as `nn_training_model` (section 8).

**Taps setting.** With `taps = T`, X has `2T` words, newest first, so a sample has `2T + 2` words.
The engine needs `T = 2`.

## 3. Porting steps 1–7 to a streaming generator

`main_demo.m` processes one 50 000-symbol sequence as a whole. The generator processes an endless
stream in **blocks** (default 4096 symbols = 204 800 simulation samples at 20 GS/s). Each stage
keeps state across blocks. A stage that uses a statistic of the whole sequence uses a **constant
frozen at calibration** (section 4) instead.

| Step | MATLAB (`main_demo.m`) | Streaming C++ | State / note |
|---|---|---|---|
| 1 symbols | `randi`, `qammod(…,'gray','UnitAveragePower',true)` | RNG (section 3.1), 16QAM Gray lookup table identical to `qammod` | Check the table against MATLAB once (all 16 points) |
| 1 RRC | `rcosdesign(0.75,16,50,'sqrt')`, `upfirdn` | Polyphase FIR: 801 taps, 17 nonzero per output sample | Export the taps from MATLAB to `rrc_taps.txt`; do not redesign them. 16 symbols of state |
| 1 IF | `real(s .* exp(1j*2*pi*f_if*t))` | f_if/fs = 0.04 = 1/25, so a 25-entry cos/sin table indexed by the global sample count mod 25 | Exact, phase continuous |
| 1 AWG | `v * vpp/(max(v)-min(v))` | Multiply by the constant `k_awg` | **Calibrated:** max/min depend on the sequence |
| 2 driver | Rapp, gain 24, vsat 3, s 2 | Same formula; for s = 2, `(1+u^4)^(1/4)` with `sqrt(sqrt())` | Memoryless |
| 2 MZM | `(1 + exp(1j*pi*(vb+vd)/vpi))/2` | `sincos` per sample | `bias_drift` adds to vb; it may change live |
| 3 fiber | One FFT of the whole sequence, `exp(1j*beta2/2*w^2*L)` | Overlap-save FFT (FFTW): N = 8192, overlap 256 | Delay spread at 15 km is about 41 ps (< 1 sample) and about 6 samples at 100 km, so 256 is ample. MATLAB's FFT is circular, so it differs from overlap-save only at the sequence ends, which the guard removes |
| 4 PD | ROP scaling, optional RIN, optional Rapp saturation, `noise(…, pd_cnr_db)` | Same | RIN and saturation are memoryless |
| 4 RF amp | AGC, Rapp (pa_asat 1.3), optional memory polynomial (delays `m*Dm`, `Dm = 12`) | Same; keep the last 24 samples of `u` | |
| 4 ripple | Echo at `Dr = 20` samples | Same; keep the last 20 samples | |
| 5 wireless | FSPL, `noise(…, cnr_ue)` | Same; compute `cnr_ue` once | |
| 6 detector | `abs(A).^2`, 1st-order IIR at 1.5 GHz with `zi = (1-alpha)*r(1)` | Same IIR, state carried between blocks | Only the very first sample uses the MATLAB initial condition |
| 6 ADC | Clip at `mean ± clip*std`, 8 bits over `[min,max]` | Fixed `lo`, `step` and clip limits | **Calibrated**, as a real ADC with a fixed full scale |
| 7 down-conversion | `2*(r - mean(r)) .* exp(-1j*2*pi*f_if*t)` | Subtract the constant `r_mean`, use the same 25-entry table | **Calibrated** `r_mean` |
| 7 matched filter | `conv(…, rrc)` over every sample | Compute it **only at the symbol instants** `offset + k*50`: 801 MACs per symbol instead of per sample | 800 samples of history |
| 7 timing, gain | Search over 51 offsets on the training symbols; least-squares complex gain | Done once, in acquisition (section 4), then fixed | |
| output | `x = gain * x(keep)`, `d`, `idx` | Stream of (x(k), d(k), idx(k)) to the sample former | |

### 3.1 Random numbers

- **Generator.** Use a fast seedable generator, such as xoshiro256++ with a ziggurat normal. Give
  separate streams, each derived from the seed, to the symbols, the PD noise, the UE noise and the
  RIN. Turning one source on or off then does not shift the others.
- **Reproducibility.** The same seed and settings give the same stream, bit for bit. The seed goes
  into every frame header.
- **Comparison with MATLAB.** The numbers are not MATLAB's. The match is checked in two ways
  (section 10):
  - deterministically, with the noise off and the symbols read from a file;
  - statistically, by EVM and BER, with the noise on.

### 3.2 What stays exactly as in MATLAB

- All parameters of section 0 of `main_demo.m`.
- The impairment formulas.
- The `a0` and `noise` definitions.

Put them in one header, `link_params.h`, with the same names as the MATLAB variables. The impairment
switches have the same names and the same "off" values (`-inf`, `inf`, `0`).

## 4. Startup: calibration and acquisition

The program runs these before streaming, and again after any change to the link settings. Each
re-run takes under a second at a 10 k clock.

1. **Calibration block.** Generate 50 000 symbols (n_sym of the study) with the current settings.
   Compute the frozen constants with MATLAB's rules on this block:
   - `k_awg`;
   - the ADC `lo`, `step` and clip limits;
   - `r_mean`.

   A run of `main_demo.m` with the same settings gives reference values for the comparison.
2. **Acquisition.** On the first 5000 symbols of that block (MATLAB's `n_train`), search the symbol
   timing over the 51 offsets, as in step 7, then compute the least-squares complex gain. Freeze
   both.
3. **Streaming starts.** The stream continues from the same generator state; the calibration block
   is not sent. A drop to the link's EVM gate (section 10, V2) would show that the frozen constants
   are wrong.

**Live changes.** A change of ROP, Vpp, bias drift or an impairment works as follows:

- the program goes through steps 1–2 again;
- the first frame after the change carries a new `config_id` and the flag `RECALIBRATED`;
- on the board, this marks where the channel changed while the engine trains.

**Optional `freeze_rx` setting.** It keeps the old receiver constants and applies only the channel
change, which is what a real receiver without re-acquisition sees. It is useful for drift
experiments.

## 5. Fixed-rate symbol clock, end to end

| Where | What | Size / setting |
|---|---|---|
| RPi, generator thread | Fills a ring of ready samples ahead of the sender | 1 s of samples |
| RPi, sender thread | Sends frames of `R / 1000` samples every 1 ms, with an absolute-time loop (`clock_nanosleep`, `TIMER_ABSTIME`), so the average rate equals R and the timing error does not build up | R = 10 k → 10 samples (240 B) per frame. Optional `SCHED_FIFO` |
| KV260 PS, `stream_rx` | Writes the frames into a ring in the u-dma-buf. The engine starts only once the ring holds `prefill` ms of samples. Then it issues DMA transfers of fixed chunks | prefill 200 ms; ring 16 MB (about 70 s at 10 k); chunk 4096 samples (96 KB) |
| PL, stream pacer (new) | Between the DMA and `sample_fifo`: a FIFO of whole samples that releases one sample every `SRC_PERIOD` cycles. This is the exact symbol clock | `SRC_PERIOD = 100e6 / R` (10 000 at 10 k); FIFO 512 samples (3072 words, 2 BRAM36), about 51 ms at 10 k, so Linux has ample time between DMA chunks |
| PL, `sample_fifo` | As today, `s_tready = 1`: if the engine falls behind the clock, samples are dropped and counted in `DROPPED` | Unchanged |

**Counters to read on the board:**

- `DROPPED`: the engine was too slow for the clock. It should be 0 for R < 80 k.
- `UNDERRUN` (new, in the pacer): the pacer was due to release a sample but had none. The RPi, the
  network or the PS failed to keep real time. It must be 0 for a valid run.
- `stream_rx` sequence gaps: missing frame numbers. They must be 0.

This replaces the backpressure mode of `docs/dma_streaming_plan.md` for this source:

- the DMA itself still stalls on the pacer FIFO (backpressure upstream of the pacer);
- the samples are paced after the pacer, and the engine side never stalls them.

The DMA plan's unpaced mode (as fast as the engine) stays, for replaying files.

## 6. Wire protocol (TCP, little-endian)

One TCP connection from the RPi (client) to `stream_rx` on the KV260 (server, port 5000). It carries
a stream of frames:

| Field | Type | Meaning |
|---|---|---|
| magic | u32 | `0x46526F41` ("AoRF") |
| version | u16 | 1 |
| flags | u16 | bit 0 `RECALIBRATED`, bit 1 `TEST` segment, bit 2 `END` (last frame), bit 3 `CONFIG` (JSON payload) |
| seq | u32 | frame number, +1 per frame |
| first_sample | u64 | global index of the frame's first sample |
| n_samples | u32 | samples in this frame |
| words_per_sample | u16 | 6 for 2 taps |
| reserved | u16 | 0 |
| config_id | u32 | +1 at every settings change |
| seed | u32 | generator seed |
| payload | int32 × n_samples × words_per_sample | the Q8.24 words of section 2, in file order |

The header is 36 bytes. At 10 k samples/s the stream is about 0.3 MB/s, and at 80 k about
2.2 MB/s, far below Ethernet. On connect, the first frame carries the full settings as a JSON text
payload (flag bit 3 `CONFIG`). `stream_rx` saves it next to the board results, so every run records
its conditions.

**Control port.** The live changes of section 4 arrive on a second port of the RPi container
(5001), as one JSON line per command. Examples:

```
{"set": {"rop_dbm": -13}}
{"set": {"bias_drift": 0.05}}
{"stop": true}
```

A plain `nc` or a small Python script is enough as the client.

## 7. Settings (`config/default.json`)

The defaults are `main_demo.m` section 0:

```json
{
  "link": { "vpp_mv": 300, "rop_dbm": -11.75, "fiber_m": 15000,
            "bias_drift": 0, "rin_dbhz": "-inf", "pd_sat": "inf",
            "pa_memory": 0, "ripple_rho": 0, "adc_clip": "inf" },
  "rx":   { "freeze_rx": false, "calib_symbols": 50000, "train_symbols": 5000 },
  "samples": { "taps": 2, "scale": "grid", "schedule": "train" },
  "stream": { "rate_sps": 10000, "frame_ms": 1, "host": "192.168.1.10", "port": 5000,
              "control_port": 5001 },
  "seed": 2017,
  "record": { "enable": false, "dir": "/records", "max_samples": 9998 }
}
```

## 8. Code layout and Docker

New top-level folder `rpi/arof_stream/`, on a new branch `rpi-generator` from
`matlab-paper-sim`. It ports MATLAB code, and the KV260 side lives elsewhere (section 9).

```
rpi/arof_stream/
├── Dockerfile
├── CMakeLists.txt
├── README.md
├── config/default.json
├── data/rrc_taps.txt              exported from MATLAB (rcosdesign), not redesigned
├── include/
│   ├── link_params.h              main_demo.m section 0, same names
│   ├── rng.h                      xoshiro256++, ziggurat normal, per-source streams
│   ├── link_stages.h              one class per stage, process(block) with state
│   ├── receiver.h                 calibration, acquisition, symbol-instant matched filter
│   ├── sample_former.h            taps, scaling, Q8.24 (ties to even)
│   ├── frame.h                    wire format of section 6
│   └── recorder.h
├── src/
│   ├── main.cpp                   threads: generator, sender, control
│   ├── link_stages.cpp
│   ├── receiver.cpp
│   ├── sample_former.cpp
│   ├── sender.cpp                 TCP client, absolute-time pacing
│   ├── control.cpp                JSON control port
│   └── recorder.cpp
├── tools/
│   ├── export_reference.m         MATLAB: rrc taps, a symbol file, noise-free x(k), frozen constants
│   ├── to_mat.py                  recorder output -> training_data.mat layout (X, Y, tx/rx)
│   ├── evm_ber.py                 EVM/BER of a recording (no EQ, LMS), as evaluate_outputs.m
│   └── fake_kv260.py              TCP server that checks frames and the rate, for tests without a board
└── tests/                         unit tests per stage against the MATLAB reference files
```

**Recorder.** Writes `train_samples_q8_24.mem` and `test_samples_q8_24.mem`, in the same layout as
`nn_training_model` (one 8-digit hex word per line). It also writes `samples.bin` (int32) and
`symbols.bin` (x, d, idx in float64) for `to_mat.py`. A recording is then an ordinary dataset:

- `python nn_training_model/equalizer_q8_24.py --dataset recorded.mat` gives the golden files for
  exactly the samples the board received;
- the same `.mem` files also work in xsim.

**Dockerfile.** Multi-stage build, arm64:

- **build stage:** `debian:bookworm-slim`, `build-essential cmake libfftw3-dev nlohmann-json3-dev`,
  `-O3 -mcpu=native` (or `-mcpu=cortex-a76` for a Pi 5, `cortex-a72` for a Pi 4);
- **runtime stage:** `debian:bookworm-slim` with `libfftw3-double3` and the binary only;
- pin the base image by digest in the Dockerfile.

**Run on the Pi:**

```
docker build -t arof-stream rpi/arof_stream
docker run --rm --network host --cpus 3 --cap-add SYS_NICE \
    -v $PWD/rpi/arof_stream/config:/config -v $PWD/records:/records \
    arof-stream --config /config/default.json
```

- `--network host` avoids the bridge NAT and its extra latency.
- `SYS_NICE` allows `SCHED_FIFO` for the sender.
- The generator can be built and unit-tested on the laptop (x86) too. Only the performance numbers
  need the Pi.

**Network.** A direct cable or a switch, with static addresses: for example RPi 192.168.1.20 and
KV260 192.168.1.10.

## 9. Compute budget

At R samples/s, the generator simulates 50 R complex samples/s at 20 GS/s: 0.5 M/s at 10 k and 4 M/s
at 80 k. Per simulation sample, the cost is a 17-tap polyphase FIR, a `sincos` (MZM), the FFT share,
2–4 normal draws, two Rapp terms, `|A|^2`, the IIR and the ADC. That is roughly 100–200 ns on one
Cortex-A76 core.

| Rate | Load (estimate) | Plan |
|---|---|---|
| 10 k (default) | about 0.1 core | One generator thread |
| 40 k | about 0.5 core | One thread |
| 80 k | about 1 core | Pipeline on 2–3 threads (TX+MZM+fiber / RAU+wireless+detector+ADC / RX) with block queues |

The matched filter runs only at the symbol instants, so it costs little. The first milestone
includes a benchmark (`--bench`, generate without sending) that reports the samples per second. The
`rate_sps` limit is set from it, with a margin of 2×.

## 10. Verification

| Step | Where | Check |
|---|---|---|
| V0 | laptop, unit tests | Each stage against `tools/export_reference.m`, with the noise off and the same symbols: RRC/IF, MZM, fiber, detector and receiver within 1e-9 relative. The ADC (8 bits) uses the exported frozen constants: no code may differ |
| V1 | laptop | Whole chain, noise off, symbols from a file: x(k) equals MATLAB's to 1e-9, apart from the first and last guard symbols |
| V2 | laptop and Pi | Noise on, 300 mV, −11.75 dBm, 6 seeds × 50 000 symbols (`tools/evm_ber.py`): no-EQ EVM and BER and LMS BER inside the spread of the MATLAB study's 6 runs. One run is 13.3 % EVM and 1.1e-4 BER without EQ; use the 6-run averages |
| V3 | laptop | Q8.24 words: the recorder's `.mem` from a float input equals the output of `nn_training_model`'s `to_q`, word for word |
| V4 | Pi | `--bench`: rate ≥ 2 × `rate_sps`; `fake_kv260.py`: no sequence gaps, mean rate within 0.1 %, frame-arrival jitter histogram over 10 min |
| V5 | KV260 | Record a stream and run it on the board at the clock. `UNDERRUN = 0`, `DROPPED = 0`, no gaps. LOSS, weights and outputs equal the Python golden files of the same recording, word for word |
| V6 | KV260 | Live change of ROP or bias drift during training: the loss rises at the `RECALIBRATED` frame, then recovers |

## 11. KV260 side: what this plan needs there

All of it is on `engine-completion` or later, and it depends on `docs/dma_streaming_plan.md`:

1. **Stage 1 of the DMA plan.** AXI DMA, stream mux and `dma_source` bit.
2. **Stream pacer, new RTL (`rtl/stream/stream_pacer.v`).**
   - A FIFO of whole samples (512 deep).
   - It releases one sample every `SRC_PERIOD` cycles when `SRC_CTRL.paced` is set (new bit 5).
   - New counter register `UNDERRUN`.
   - It reuses the period logic of `test_source`.
3. **`cpp/src/stream_rx.cpp`.**
   - TCP server, frame checks (magic, seq, config_id), ring in the u-dma-buf, prefill, chunked DMA.
   - Engine setup as in `docs/HANDOFF.md` I7, with `TARGET = 0` (no limit) or a set count.
   - It logs `config_id` changes with the PROCESSED count at that moment.
4. **Evaluation during training.** `output_stage` captures a2 at every step, which includes training
   steps (to confirm in `rtl/stream/output_stage.v`). Each output is computed before the update on
   its own sample, so it is an honest prediction of an unseen symbol. EVM and BER on the stream come
   from OUT_DATA, read in chunks of 2048 steps or fewer. Test segments in infer mode (`TEST` flag)
   are a second option, which `stream_rx` would switch with `STREAM_CTRL`.

## 12. Order of work

1. **M1, generator core on the laptop.**
   - `export_reference.m`, the stages, receiver, sample former, recorder.
   - Checks V0, V1, V3.
2. **M2, statistics.**
   - `evm_ber.py`, `to_mat.py`; check V2.
   - `nn_training_model` trained on a recording (float and Q8.24) as a first look at learning on
     16QAM A-RoF data.
3. **M3, streaming on the Pi.**
   - Sender, control port, Dockerfile, `fake_kv260.py`; check V4.
4. **M4, board.**
   - DMA stage 1, pacer, `stream_rx`; checks V5, V6.
   - The RTL work follows the DMA plan's own V1–V3 first.

M1–M3 need no board, and M1–M2 need no Pi.

## 13. Open points

- **RPi model.** Pi 4 or Pi 5; it changes `-mcpu` and the maximum rate. The plan assumes a 64-bit
  OS.
- **Target scale.** Grid {±1, ±3} (default) or unit power. Both fit Q8.24. The engine's
  settings (4-12-2, sigmoid, LR 1e-3) have not been run on the A-RoF 16QAM data yet, so M2 checks
  that it learns them. Otherwise the 6-input tanh candidate (RTL change) is the next step.
- **Test segments.** Evaluation during training only (prequential), or also infer-mode segments.
- **Re-acquisition after a change.** Default on; with `freeze_rx`, drift experiments are closer to a
  real receiver.
