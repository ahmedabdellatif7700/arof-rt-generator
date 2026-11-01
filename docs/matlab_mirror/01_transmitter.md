# Step 1: Transmitter (AWG)

## MATLAB

`link/transmit_receive.m` (`main_demo.m` step 1 writes the same lines inline):

```matlab
guard = rrc_span;                                   % 16 symbols dropped at each end
idx = randi([0 M-1], n_sym + 2*guard, 1);           % 4 bits per symbol
d   = qammod(idx, M, 'gray', 'UnitAveragePower', true);
s   = upfirdn(d, rrc, sps);                         % RRC pulse shaping, 50 samples/symbol
t   = (0:numel(s)-1).' / fs;
v   = real(s .* exp(1j*2*pi*f_if*t));               % up-conversion to the 0.8 GHz IF
v   = v * (vpp / (max(v) - min(v)));                % AWG output at the set Vpp
```

with `rrc = rcosdesign(0.75, 16, 50, 'sqrt')` (801 taps) from the caller.

## C++

| MATLAB | C++ | Notes |
|---|---|---|
| `randi([0 15])` | `Rng::below_pow2(4)`, `src/core/rng.h` | own generator, a stream of its own for the symbols |
| `qammod(..., 'gray', 'UnitAveragePower', true)` | `qam16_gray()`, `src/core/comm.cpp` | 16-point table, checked against `tx_indices` → `Y` of `arof_training_data.mat` |
| `rcosdesign(0.75, 16, 50, 'sqrt')` | `rcosdesign_sqrt()`, `src/core/comm.cpp` | MATLAB's formula |
| `upfirdn(d, rrc, sps)` | `Transmitter::shape()`, `src/link/transmitter.cpp` | polyphase: each output sample sums 16-17 symbols × taps; the last 17 symbols are the state |
| `exp(1j*2*pi*f_if*t)` | 25-entry cos/sin table indexed by the global sample count mod 25 | f_if/fs = 1/25: exact and phase-continuous |
| `v * vpp/(max(v) - min(v))` | `k_awg`, `src/link/transmit_receive.cpp` | calibrated once, then fixed |

## What differs on the Pi

1. **AWG scaling.** MATLAB scales by the range of its whole sequence. `calibrate()` generates a
   block the size of MATLAB's sequence (`n_sym + 2*guard` = 50 032 symbols), sets
   `k_awg = vpp / (max(v) - min(v))` on it and keeps that gain fixed, as a real AWG with a set
   output range. At the defaults `k_awg` ≈ 0.555 and the driver output is 5.44 Vpp. MATLAB's range
   also includes the 800-sample tail of `upfirdn` after the last symbol; the block's does not.
2. **Symbols.** The labels differ from MATLAB's `rng(2017)` sequence; their statistics are the
   same (uniform over the 16 points).
3. **Start of the stream.** `upfirdn` starts from zero symbols, and so does the Pi's state, so the
   first samples ramp up the same way. MATLAB drops the first `guard` symbols; the Pi does the same
   in the calibration block, which is never streamed.

## Verified

- `tests/test_link_stages.cpp`:
  - `transmitter_matches_upfirdn`: against a direct `upfirdn` + IF, within 1e-12, fed in uneven
    blocks (1, 2, 50, 7, 240 symbols);
  - `rcosdesign_sqrt_shape`: 801 taps, unit energy, symmetric, MATLAB's peak formula, the RRC pair
    is Nyquist (ISI < 2e-3 from the 16-symbol truncation);
  - `qammod_qamdemod_gray`: the 16 points, unit average power, Gray neighbours.
- `tests/test_rng.cpp` `symbols_uniform`: the 16 labels are equally likely.
