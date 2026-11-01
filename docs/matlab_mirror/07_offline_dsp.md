# Step 7: Offline DSP, received symbols

The ADC samples become one complex symbol per sent symbol: down-conversion from the IF, matched
filter, symbol timing, complex gain. This is the output of the Pi: the received symbols **without
equalization**, which become the KV260's input samples (step 11). Equalization is not done on the
Pi (step 8).

The last whole-sequence statistics: `mean(r)`, the timing offset and the gain are found on the
calibration block and then frozen.

## MATLAB

`main_demo.m` step 7 (the offline DSP part of `transmit_receive.m`):

```matlab
z = conv(2*(r - mean(r)) .* exp(-1j*2*pi*f_if*t), rrc(:));     % down-convert, matched filter
train = guard + (1:n_train).';
nominal = rrc_span*sps + 1;                             % delay of the RRC pair
best = -inf;
for o = nominal : nominal + sps                         % timing: best training correlation
    zs = z(o + (train - 1)*sps);
    c = abs(zs' * d(train)) / norm(zs);
    if c > best, best = c; offset = o; end
end
x = z(offset + (0:numel(d)-1).'*sps);                   % one sample per symbol
gain = (x(train)' * d(train)) / (x(train)' * x(train)); % complex gain, least squares
keep = guard + (1:n_sym);
x = gain * x(keep);  d = d(keep);  idx = idx(keep);     % received and sent symbols
```

`sps = 50`, `rrc_span = 16` (801 taps), `f_if = 0.8 GHz`, `guard = 16`, `n_train = 5000`.

## 7a. What it does

1. **Remove the DC** of the square law: `r − mean(r)`.
2. **Down-convert:** `× 2·exp(−j2π f_if t)`, the IF back to baseband.
3. **Matched filter:** the same RRC as the transmitter (the pair is a raised cosine).
4. **Timing:** the delay of the RRC pair is `rrc_span·sps` = 800 samples (MATLAB index 801).
   The 51 offsets 801…851 (one symbol) are tried; the one whose samples correlate best with the
   training symbols wins. It absorbs the delays of the detector filter, PA memory and so on.
5. **Sample** at `offset + k·sps`: one sample per symbol.
6. **Complex gain:** least squares on the training symbols. It undoes the link's scale and phase
   (at the defaults `−0.49 − 0.19j`).
7. **Keep** `guard + (1:n_sym)`: drop the 16 symbols at each end (the filters' edges).

## 7b. On the Pi (`src/link/receiver.*`)

Two parts:

- **`acquire()`**, on the calibration block, steps 1-6 as MATLAB: the down-converted block, the
  same 51 offsets, the same correlation `|zs'·d|/‖zs‖` on `train = guard + (1:n_train)`, the same
  least-squares gain. `mean(r)` is the mean of the block (`TransmitReceive::calibrate()`). Result:
  `RxConstants {r_mean, offset, gain}`.
- **`Receiver::process()`**, on the stream, with those constants frozen:

```cpp
const double a = 2 * (r[i] - c_.r_mean);          // remove DC, down-convert
y_.emplace_back(a * cos_[ph], -a * sin_[ph]);
...
const int64_t m = c_.offset + sps_ * k_front_;    // symbol k's instant
for (int j = 0; j < len_; ++j) zr += yy[-j].real() * rrc_[j], ...   // conv(y, rrc) at m only
out.push_back({c_.gain * cd(zr, zi), d, idx, k});
```

Streaming details:

- **Matched filter only at the symbol instants.** MATLAB computes `conv` at all 2.5 M samples and
  keeps every 50th; the Pi computes the 801-tap sum only where it is kept. Same numbers, 50× less
  work.
- **Tone table:** `exp(−j2π f_if t)` repeats every 25 samples (0.8 GHz / 20 GS/s = 2/50), so it is a
  table, indexed by the absolute sample time.
- **One time axis.** Time 0 is the first sample of symbol 0 of the program; before it the
  history is zeros, as `conv`'s padding. Offsets are relative to that axis, so they stay valid for
  every later symbol.
- **Sent symbols:** the transmitter pushes each symbol `(d, idx)` into a queue; the receiver pops
  it when its instant is reached, so every output carries the symbol actually sent, plus its
  count `k`.
- **Guard:** the calibration block has 16 extra symbols at each end, as MATLAB. In the stream the
  filters are continuous, so no symbol is dropped.

## 7c. Recalibration

A live `set` (e.g. a new ROP) recalibrates: a new block of 50 032 symbols goes through the chain
(not streamed, `k` keeps counting), the ADC range (step 6) and `RxConstants` are measured again,
and streaming resumes. Symbols whose filter history was lost to a timing change are counted
(`skipped`), not sent wrong.

`freeze_rx` (open point in the handoff, default `false`): keep the old ADC range, mean, timing
and gain, as a receiver that does not know the link changed.

## What differs on the Pi

- `mean(r)`, offset and gain from the calibration block, then frozen (MATLAB: from its own
  sequence; the same rules on a sequence of the same size).
- Nothing else: the same filter, the same tone, the same arithmetic per output symbol.

`--calibrate` prints the result as `main_demo.m` would:
`receiver: offset 802, gain -0.49175-0.19183j, ADC lo 0.0088896 step 0.00301544, mean(r) 0.383177`
(offset in MATLAB's 1-based numbering).

## Verified

`tests/test_transmit_receive.cpp`:

- `noise_free_link_has_no_errors`: PD and UE noise off → BER 0, 12.07 % EVM, offset inside
  801…851;
- `reference_point_against_matlab`: 300 mV, −11.75 dBm, 3 seeds: 44 991 test symbols each, mean
  no-EQ EVM 13.21 % ± 0.3 points (MATLAB 13.21 %);
- `symbols_in_order`, `block_size_does_not_change_the_stream`: `k` consecutive, each output's
  `d` the sent point, the same stream for any block size;
- `live_change_recalibrates`: ROP −11.75 → −15.5 dBm, 17.1 % after (MATLAB 17.07 %), `k` continues;
- `freeze_rx_keeps_receiver_constants`: with `freeze_rx` offset, gain, mean and ADC range unchanged.

`tools/compare_matlab.py`: all 74 points of `compare_results.mat` and `impairments_results.mat`
within tolerance; every one goes through this receiver.
