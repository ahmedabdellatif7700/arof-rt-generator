# Step 6: User equipment, envelope detector and ADC

The UE turns the 60 GHz signal back into the IF waveform and digitizes it: a square-law detector
with a low-pass, then an 8-bit scope. The detector is a formula plus a one-pole filter (state
carried across blocks). The ADC is the second place where MATLAB uses statistics of the whole
sequence: on the Pi they are measured on the calibration block and frozen.

## MATLAB

`main_demo.m` step 6 (`transmit_receive.m` calls `envelope_detector.m` and `adc.m`):

```matlab
r = abs(A).^2;                                          % square law: back to the IF
alpha = 1 - exp(-2*pi*det_bw/fs);                       % 1st-order low-pass, 1.5 GHz
r = filter(alpha, [1, alpha - 1], r, (1 - alpha)*r(1));
if isfinite(adc_clip)                                   % limited ADC range
    r = min(max(r, mean(r) - adc_clip*std(r)), mean(r) + adc_clip*std(r));
end
lo = min(r);  step = (max(r) - lo) / (2^adc_bits - 1);  % 8-bit scope over the range
r = lo + step*round((r - lo)/step);
```

## 6a. Envelope detector (`envelope_detector.m`)

- **Square law:** `x = |A|²`. The 60 GHz carrier drops out; what remains is the IF waveform of
  step 1 (plus the distortion and noise of steps 2-5) on a DC level.
- **Low-pass** (`det.enable`): one pole at `bw = 1.5 GHz` (the IF band ends at 1.15 GHz),
  `y(n) = α·x(n) + (1 − α)·y(n−1)`, `α = 1 − exp(−2π · 1.5e9 / 20e9) ≈ 0.376`.
- **Start:** MATLAB's initial condition `(1 − α)·x(1)` makes `y(1) = x(1)` (no start-up ramp).

C++: `src/link/envelope_detector.cpp`:

```cpp
const double v = std::norm(A[i]);                 // abs(A).^2
if (first_) { y_ = v; first_ = false; }           // zi = (1-alpha)*x(1): y(1) = x(1)
y_ = alpha_ * v + (1 - alpha_) * y_;
```

The initial condition is applied once, to the first sample ever; after that `y_` carries from
block to block, so the stream equals one `filter` call over the whole sequence. Disabled: square
law only, as `envelope_detector.m`.

## 6b. ADC (`adc.m`)

Three rules, in order:

1. **Clipping** (switch `adc_clip`, default `inf` = off): limit to `mean ± clip_std·std`
   (`std` with N − 1, as MATLAB).
2. **Range:** `lo = min(r)`, `step = (max(r) − lo)/(2^bits − 1)`, `bits = 8` (255 steps).
3. **Quantize** (`adc.enable`): `lo + step·round((r − lo)/step)`.

Rules 1 and 2 need the whole sequence (`mean`, `std`, `min`, `max`). The Pi splits them:

- `adc_calibrate()`: the same rules on the calibration block (`n_sym + 2·guard` = 50 032
  symbols, 2.5 M samples), giving `clip_lo`, `clip_hi`, `lo`, `step`;
- `adc()`: each streamed sample with those constants:

```cpp
double v = std::min(std::max(x[i], c.clip_lo), c.clip_hi);
if (p.enable)                                     // lo + step*round((x - lo)/step)
    v = c.lo + c.step * std::clamp(std::round((v - c.lo) / c.step), 0.0, top);
```

The `clamp` to codes 0…255 is the one addition: in MATLAB no sample can fall outside its own
min/max; in a stream a later sample can exceed the calibrated range, and it saturates at the end
code, as a real ADC with a fixed full scale. The range is the extreme of 2.5 M samples, so this
is rare.

In `TransmitReceive::calibrate()` the range comes from the block itself (the tail added for the
fiber is quantized but not used for the statistics), as MATLAB's range comes from its sequence.
After a recalibration (a live `set`) the constants are measured again, unless `freeze_rx` keeps
the receiver as it was (step 7).

## 6c. Values at the defaults

`--calibrate` (300 mV, −11.75 dBm) prints `ADC lo 0.0088896 step 0.00301544`. With clipping:

| `adc_clip` | `lo` | `step` | no-EQ EVM (one run) |
|---|---|---|---|
| inf (default) | 0.00889 | 0.00302 | 13.2 % |
| 2 | 0.00889 | 0.00299 | 13.2 % |
| 1.2 | 0.150 | 0.00183 | 16.7 % |
| 1 | 0.189 | 0.00152 | 19.1 % |

At 2 only the top is clipped (`mean − 2·std` is below the minimum): a finer step, little damage.
At 1-1.2 the large symbols are flattened and the EVM rises fast.

## What differs on the Pi

- Detector: nothing (the state carries over exactly).
- ADC: `mean`, `std`, `min`, `max` from the calibration block, then fixed; samples beyond the range
  saturate. The constants are as MATLAB's would be on a sequence of that size.

## Verified

`tests/test_link_stages.cpp`:

- `envelope_detector_matches_filter`: 3000 samples fed in blocks of 101 against `filter` written
  out (direct form II transposed with MATLAB's `zi`): within 1e-14; disabled gives exactly `|A|²`.
- `adc_rules`: `lo` = min, `step` = range/255, every sample = MATLAB's formula; values beyond the
  range saturate at codes 0 and 255; clipping at 1.2 gives `mean ± 1.2·std` (N − 1) and the range
  between them; disabled clips without quantizing.

`tools/compare_matlab.py --set impairments --only adc_clip` (6 seeds, against
`impairments_results.mat`), all within tolerance:

| `adc_clip` | EVM Pi | MATLAB | BER Pi | MATLAB |
|---|---|---|---|---|
| 2 | 13.19 % | 13.24 % | 7.5e-05 | 9.3e-05 |
| 1.5 | 14.52 % | 14.56 % | 1.3e-04 | 1.4e-04 |
| 1.3 | 15.83 % | 15.87 % | 2.0e-04 | 2.0e-04 |
| 1.2 | 16.74 % | 16.79 % | 3.2e-04 | 3.3e-04 |
| 1.1 | 17.89 % | 17.93 % | 6.8e-04 | 6.8e-04 |
| 1 | 19.22 % | 19.27 % | 1.9e-03 | 1.9e-03 |
