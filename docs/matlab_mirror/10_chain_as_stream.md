# Step 10: The chain as a stream (`transmit_receive`)

Steps 1-7 put together. MATLAB runs the chain once over a finished sequence; the Pi runs it
forever. This note is about how: what is computed once, what carries over from block to block,
and what happens on a live change.

## MATLAB

`transmit_receive.m` (inlined in `main_demo.m` steps 1-7) is one function call:

1. draw `n_sym + 2·guard` = 50 032 symbols, shape and upconvert them (step 1);
2. every stage on the whole array: AWG scaling, driver, MZM, fiber, PD, RF amp, ripple, wireless,
   detector, ADC (steps 1-6);
3. the receiver on the whole array: `mean(r)`, timing, gain (step 7);
4. return `x`, `d`, `idx` for `keep = guard + (1:n_sym)`.

Every whole-sequence statistic (`max(v) − min(v)`, the ADC range, `mean(r)`, the offset, the gain)
comes from the same sequence that is returned.

## 10a. On the Pi (`src/link/transmit_receive.*`)

`class TransmitReceive`, one object for the whole program:

| Method | MATLAB counterpart | What it does |
|---|---|---|
| constructor | `default_link`, the stage settings | builds every stage, each noise source with its own random stream |
| `channel()` | the stage calls of steps 1-6 | AWG → driver → MZM → fiber → PD → RF amp → ripple → wireless → detector, on a chunk |
| `calibrate()` | one `transmit_receive.m` call | a 50 032-symbol block through the chain; measures and freezes the constants |
| `generate(n)` | (none: MATLAB stops) | `n` more symbols with the constants fixed |
| `update()` | a new call with other settings | live change, then `calibrate()` |

```cpp
E_[i] = mzm(driver_amp(k_awg_ * v[i], link_.driver), link_.mzm);   // AWG, driver_amp, mzm
fiber_->process(E_.data(), n, A_);                    // fiber (the count varies)
pd_.process(A_.data(), A_.size());                    // photodetector
amp_.process(A_.data(), A_.size());                   // rf_amp
ripple_.process(A_.data(), A_.size());                // rf_response
wl_.process(A_.data(), A_.size());                    // wireless_hop
det_.process(A_.data(), A_.size(), x_);               // envelope_detector
```

## 10b. `calibrate()`: one MATLAB run, then frozen

1. **Transmitter:** 50 032 symbols; each sent symbol `(d, idx)` queued for the receiver;
   `k_awg = vpp/(max(v) − min(v))` of the block (step 1).
2. **Channel** over the block in chunks of 128 symbols, then a **tail** of
   `max_holdback/sps + guard + 2` = 182 symbols, so the fiber's held-back samples come out (step 3).
3. **ADC range** from the block's samples, then quantize (step 6).
4. **`mean(r)`, timing, gain** with `acquire()` on `train = guard + (1:n_train)` (step 7).
5. **The block through the receiver**, `keep = guard + (1:n_sym)`, scored as `main_demo.m` scores
   no EQ (step 9).
6. **Report:** `k_awg`, driver Vpp, CNRs, ADC constants, offset, gain, `mean(r)`, EVM/BER, time.

The block and its tail are **not streamed**: they are the generator's own measurement. About
2.1 s on the Pi 4.

## 10c. `generate()`: the stream

- Chunks of 128 symbols (6400 samples, so the stage buffers stay in the cache).
- The same `channel()`, then the ADC with the frozen range, then `Receiver::process()` with the
  frozen `mean(r)`, offset and gain. Only `k_awg` and these constants are fixed; every other stage
  is the same formula as in MATLAB, sample by sample.
- **State carried across chunks**, so the stream is one continuous sequence: the transmitter's
  RRC history, the fiber's overlap, the PA memory and ripple delay lines, the detector's filter
  state, the receiver's matched-filter history and the queue of sent symbols, the time axis.
- **Latency:** the fiber holds back up to 8192 samples, so `generate(n)` may return a few symbols
  more or less than `n`. Each output carries its symbol count `k`; none is lost or repeated.
- Rate on the Pi 4: about 35 500 symbols/s on one core (`--bench`); the stream runs at 10 000.

## 10d. `update()`: a live change

A control `set` (step 11) that changes the link, the operating point or the receiver settings:

1. the new `Link`, `vpp`, `rop_dbm`; the fiber filter rebuilt in place if its length, `D` or
   `lambda` changed (the samples in flight are kept); `fiber.enable` is startup-only;
2. the stages reconfigured (`configure()`): noise levels, AGC, PA, ripple, wireless budget, detector;
3. `calibrate()` again: new `k_awg`, ADC range, `mean(r)`, timing, gain, unless `freeze_rx`
   keeps the receiver side (ADC range, `mean(r)`, offset, gain) as it was.

The time axis and `k` continue; the stream after the change is the next part of the same
sequence, with the new link. The streamer gives the change a new `config_id` and marks it in the
stream (step 11).

## 10e. Random numbers

| Stream | Used by |
|---|---|
| `symbols` | `randi`: the sent symbols (step 1) |
| `rin` | laser RIN (step 4) |
| `pd_noise` | PD noise (step 4) |
| `ue_noise` | UE noise (step 5) |

Each is xoshiro256++ seeded from `seed` and the stream's number (splitmix64), normals by
ziggurat (`src/core/rng.h`). Consequences:

- the same seed gives the same stream bit for bit;
- the chunk size does not change it;
- switching one impairment on does not change the other noise realizations (MATLAB's single
  global stream does, step 4);
- MATLAB's `rng(seed)` is not reproduced: the results agree statistically.

Calibration blocks draw from the same streams (`k` counts their symbols), so a run with a
recalibration is a different realization from one without; statistically the same.

## What differs on the Pi

The whole structure: one object running forever instead of one call. The differences listed in
the earlier steps all come from it: the statistics from a calibration block (steps 1, 6, 7), the
streaming fiber (step 3), the generator's own random numbers.

## Verified

`tests/test_transmit_receive.cpp`:

- `same_seed_same_stream_bit_for_bit`; `block_size_does_not_change_the_stream` (chunks of 1 to
  300 symbols give the same 4000 symbols);
- `symbols_in_order`: `k` consecutive, each `d` the point of its `idx`, the stream after the
  calibration block;
- `noise_free_link_has_no_errors`, `reference_point_against_matlab` (13.21 %, MATLAB 13.21 %);
- `live_change_recalibrates`: −11.75 → −15.5 dBm, 17.1 % after (MATLAB 17.07 %), `k` continues;
- `freeze_rx_keeps_receiver_constants`;
- `impairment_switches_as_impairment_sweeps`: each `main_demo.m` switch sets the stage as
  `impairment_sweeps.m` does.

End to end, `docs/STATUS.md`: 10 minutes in Docker with two live ROP changes, no gap in `k`, the
EVM after each change as MATLAB at that ROP (15.36 % at −14.5 dBm, MATLAB 15.36 %).
