# Step 3: Fiber, chromatic dispersion

The one step where the Pi computes differently from MATLAB: same physics and same filter, applied
as a stream instead of one FFT.

## MATLAB

`fiber.m` (`main_demo.m` step 3 inline):

```matlab
beta2 = -D * lambda^2 / (2*pi*c);
f = (0:numel(E)-1).' / numel(E) * fs;
f(f >= fs/2) = f(f >= fs/2) - fs;                   % FFT bin frequencies
E = ifft(fft(E) .* exp(1j*beta2/2*(2*pi*f).^2*length_m));
```

`D = 17 ps/(nm·km)`, `lambda = 1552.90 nm`, `length_m = 15 km`.

## 3a. What it does

A pure phase in the frequency domain, `H(f) = exp(j · β2/2 · (2πf)² · L)`, |H| = 1. At 15 km
(β2 ≈ −2.17e-26 s²/m): about 8 mrad at the IF band edge (1.15 GHz), 0.64 rad at 10 GHz, a delay
spread of ~41 ps (under one 50 ps sample). Weak, but the MZM's chirp lets it change the
intensity. Not modelled, as in `fiber.m`: attenuation (the ROP sets the power at the PD) and
dispersion on the second laser (a CW tone).

## 3b. Why not one FFT

MATLAB filters the whole sequence (2.5 M samples) with one FFT; a stream has no whole sequence.
The streaming method, overlap-save, needs the filter as a finite impulse response, and the
response of `exp(jaω²)` has slowly decaying tails: it is truncated. This is the only real
approximation of the port.

## 3c. On the Pi (`src/link/fiber.cpp`)

Plan values: `nfft = 8192`, response truncated to ±128 samples, overlap 256, 7936 new outputs
per block.

- **Filter** (`Fiber::set()`): the same `H(f)` on the 8192-point grid (same wrap of
  `f >= fs/2`), inverse FFT → impulse response, keep taps −128…+128, delayed by 128 (causal),
  FFT back. The energy cut off is reported (`truncation_error()`).
- **Stream** (`process()`, `run_block()`): each block FFT → × filter → inverse FFT; 256 samples
  overlap with the previous block.
- **Timing:** the first 128 outputs ever are dropped, so output n lines up with input n as in
  MATLAB's zero-delay FFT. This matters: the receiver's timing search (step 7) spans only one
  symbol (51 offsets).
- **Start:** the history is the unmodulated carrier `mzm(0)`, the field before any symbol.
- **Latency:** up to 8192 samples (~164 symbols) held back until a block is full. Invisible in
  the data; `calibrate()` pushes a tail of 182 symbols (8192/50 + guard + 2) after the 50 032-symbol
  block so its last samples come out. The tail is not streamed either.

## 3d. Against MATLAB

`tests/test_link_stages.cpp` `fiber_matches_one_fft`: the real MZM output through MATLAB's one FFT
(written out) and through overlap-save fed in uneven blocks, compared away from the ends (where the
one FFT wraps around):

| Fiber | Energy outside ±128 taps | Max error / signal rms |
|---|---|---|
| 15 km (default) | 5.4e-9 | 1.9e-5 |
| 100 km | 2.4e-7 | 1.3e-4 |

The in-band PD noise of step 4 is about 2e-2 of the signal: the truncation is ~1000× smaller.
Longer windows shrink the error further (±512 taps: 1.4e-6 at 15 km), so truncation is its only
source. The plan's ±128 is kept. The handoff's 1e-9 target is not reachable with a practical
window (see `docs/DEMO.md` section 12).

End to end, the fiber-length sweep of `impairments_results.mat` (15, 30, 60, 100 km) is within
tolerance at every point, e.g. 13.40 % EVM at 100 km (MATLAB 13.45 %).

## 3e. Live changes and switches

- `fiber_m` (the `"link"` switch, as `impairment_sweeps.m`): live; `Fiber::set()` rebuilds the
  filter in place and keeps the samples in flight, so the time axis does not jump.
- `stages.fiber.enable = false`: pass-through, as `fiber.m` disabled; startup only (it changes the
  stage's latency).
- `length_m = 0`: exactly the identity (`fiber_zero_length_and_disabled`).
